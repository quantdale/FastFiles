#include "UsnJournalReader.h"

#include <winioctl.h>

#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include "ffipc/PipeFraming.h"
#include "ffprotocol/Records.h"

#include "MftParser.h"

namespace ffindexsvc {

namespace {

constexpr uint32_t kBatchSize = 512;
constexpr size_t kReadBufferSize = 64 * 1024;
constexpr std::chrono::milliseconds kIdlePollInterval{1000};
constexpr std::chrono::milliseconds kIdlePollSliceInterval{100};

bool SendJournalOpened(HANDLE pipe, std::mutex& writeMutex, ffprotocol::VolumeId volumeId, uint64_t journalId) {
    ffprotocol::UsnJournalOpenedPayload payload{volumeId, journalId};
    std::lock_guard<std::mutex> lock(writeMutex);
    return ffipc::WriteFrame(pipe, static_cast<uint16_t>(ffprotocol::MessageType::UsnJournalOpened), &payload, sizeof(payload));
}

bool SendJournalBatch(HANDLE pipe, std::mutex& writeMutex, ffprotocol::VolumeId volumeId,
                       const std::vector<ffprotocol::UsnDeltaV1>& records, uint64_t latestUsn) {
    const std::vector<uint8_t> serializedRecords = ffprotocol::SerializeUsnDeltaBatch(records);

    ffprotocol::JournalRecordBatchHeader header{};
    header.volumeId = volumeId;
    header.recordCount = static_cast<uint32_t>(records.size());
    header.latestUsn = latestUsn;

    std::vector<uint8_t> payload;
    payload.reserve(sizeof(header) + serializedRecords.size());
    const auto* headerBytes = reinterpret_cast<const uint8_t*>(&header);
    payload.insert(payload.end(), headerBytes, headerBytes + sizeof(header));
    payload.insert(payload.end(), serializedRecords.begin(), serializedRecords.end());

    std::lock_guard<std::mutex> lock(writeMutex);
    return ffipc::WriteFrame(pipe, static_cast<uint16_t>(ffprotocol::MessageType::JournalRecordBatch),
                              payload.data(), static_cast<uint32_t>(payload.size()));
}

// Re-reads a changed file's current allowlisted fields via the same
// mechanism VolumeScanner.cpp uses, rather than trusting USN_RECORD's own
// truncated fields (tasks.md 5.3). Returns std::nullopt if the record can
// no longer be read this way (most commonly because it was deleted).
std::optional<ParsedMftRecord> TryReenrichFromMft(
    HANDLE volumeHandle, uint64_t fileReferenceNumber, uint32_t recordSize, uint32_t bytesPerSector,
    std::vector<uint8_t>& outputBuffer, std::vector<uint8_t>& scratch) {
    NTFS_FILE_RECORD_INPUT_BUFFER input{};
    input.FileReferenceNumber.QuadPart = static_cast<LONGLONG>(fileReferenceNumber);

    DWORD bytesReturned = 0;
    if (!DeviceIoControl(volumeHandle, FSCTL_GET_NTFS_FILE_RECORD, &input, sizeof(input), outputBuffer.data(),
                          static_cast<DWORD>(outputBuffer.size()), &bytesReturned, nullptr)) {
        return std::nullopt;
    }
    const auto* output = reinterpret_cast<const NTFS_FILE_RECORD_OUTPUT_BUFFER*>(outputBuffer.data());
    const uint32_t returnedRecordLength = output->FileRecordLength;
    if (returnedRecordLength == 0 || returnedRecordLength > recordSize) {
        return std::nullopt;
    }
    std::memcpy(scratch.data(), output->FileRecordBuffer, returnedRecordLength);
    if (ApplyFixupAndValidate(scratch.data(), returnedRecordLength, bytesPerSector) != FixupResult::Ok) {
        return std::nullopt;
    }
    return ParseMftAttributes(scratch.data(), returnedRecordLength);
}

} // namespace

void RunUsnJournalStream(
    HANDLE pipe, std::mutex& writeMutex, ffprotocol::VolumeId volumeId, wchar_t driveLetter, uint64_t resumeUsn,
    const std::atomic<bool>& shouldStop) {
    const wchar_t volumePath[] = {L'\\', L'\\', L'.', L'\\', driveLetter, L':', L'\0'};

    HANDLE volumeHandle = CreateFileW(
        volumePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (volumeHandle == INVALID_HANDLE_VALUE) {
        return;
    }

    USN_JOURNAL_DATA_V0 journalData{};
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(volumeHandle, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &journalData, sizeof(journalData),
                          &bytesReturned, nullptr)) {
        CloseHandle(volumeHandle);
        return;
    }

    if (!SendJournalOpened(pipe, writeMutex, volumeId, static_cast<uint64_t>(journalData.UsnJournalID))) {
        CloseHandle(volumeHandle);
        return;
    }

    NTFS_VOLUME_DATA_BUFFER volumeData{};
    if (!DeviceIoControl(volumeHandle, FSCTL_GET_NTFS_VOLUME_DATA, nullptr, 0, &volumeData, sizeof(volumeData),
                          &bytesReturned, nullptr)) {
        CloseHandle(volumeHandle);
        return;
    }
    const uint32_t recordSize = static_cast<uint32_t>(volumeData.BytesPerFileRecordSegment);
    const uint32_t bytesPerSector = static_cast<uint32_t>(volumeData.BytesPerSector);
    if (recordSize == 0 || bytesPerSector == 0) {
        CloseHandle(volumeHandle);
        return;
    }
    std::vector<uint8_t> mftOutputBuffer(sizeof(NTFS_FILE_RECORD_OUTPUT_BUFFER) - 1 + recordSize);
    std::vector<uint8_t> mftScratch(recordSize);

    USN startUsn = static_cast<USN>(resumeUsn);
    std::vector<uint8_t> readBuffer(kReadBufferSize);
    std::vector<ffprotocol::UsnDeltaV1> batch;
    batch.reserve(kBatchSize);
    uint64_t latestUsnInBatch = resumeUsn;

    while (!shouldStop.load()) {
        READ_USN_JOURNAL_DATA_V0 readRequest{};
        readRequest.StartUsn = startUsn;
        readRequest.ReasonMask = 0xFFFFFFFF;
        readRequest.ReturnOnlyOnClose = FALSE;
        readRequest.Timeout = 0;
        readRequest.BytesToWaitFor = 0;
        readRequest.UsnJournalID = journalData.UsnJournalID;

        DWORD readBytesReturned = 0;
        if (!DeviceIoControl(volumeHandle, FSCTL_READ_USN_JOURNAL, &readRequest, sizeof(readRequest), readBuffer.data(),
                              static_cast<DWORD>(readBuffer.size()), &readBytesReturned, nullptr)) {
            // A recreated/invalidated journal (or any other read failure)
            // ends this stream -- the engine detects the discontinuity
            // via a subsequent OpenUsnJournal reporting a different
            // JournalId (design.md D6), not via an in-band error here.
            break;
        }
        if (readBytesReturned < sizeof(USN)) {
            break; // malformed reply -- nothing safe to do but stop
        }

        USN nextUsn = 0;
        std::memcpy(&nextUsn, readBuffer.data(), sizeof(USN));

        size_t offset = sizeof(USN);
        while (offset + sizeof(USN_RECORD_V2) <= readBytesReturned) {
            const auto* record = reinterpret_cast<const USN_RECORD_V2*>(readBuffer.data() + offset);
            if (record->RecordLength == 0 || offset + record->RecordLength > readBytesReturned) {
                break; // malformed trailing record -- stop parsing this read, next iteration continues from nextUsn
            }

            ffprotocol::UsnDeltaV1 delta{};
            delta.fixed.usn = static_cast<uint64_t>(record->Usn);
            delta.fixed.fileReferenceNumber = record->FileReferenceNumber;
            delta.fixed.parentFileReferenceNumber = record->ParentFileReferenceNumber;
            delta.fixed.reason = record->Reason;
            std::memcpy(&delta.fixed.timestamp, &record->TimeStamp, sizeof(delta.fixed.timestamp));

            auto enriched = TryReenrichFromMft(volumeHandle, record->FileReferenceNumber, recordSize, bytesPerSector,
                                                mftOutputBuffer, mftScratch);
            if (enriched) {
                delta.fixed.parentFileReferenceNumber = enriched->parentFileReferenceNumber;
                delta.fixed.sizeBytes = enriched->sizeBytes;
                delta.fixed.fileAttributes = enriched->fileAttributes;
                delta.fileName = enriched->fileName;
            } else {
                // Most commonly a delete: fall back to the raw
                // USN_RECORD's own (shorter, but still allowlisted) name
                // field rather than dropping the record entirely --
                // tasks.md 5.3/5.4 still require forwarding the change.
                delta.fixed.sizeBytes = 0;
                delta.fixed.fileAttributes = record->FileAttributes;
                const auto* nameStart =
                    reinterpret_cast<const char16_t*>(reinterpret_cast<const uint8_t*>(record) + record->FileNameOffset);
                delta.fileName.assign(nameStart, record->FileNameLength / sizeof(char16_t));
            }
            delta.fixed.fileNameLengthChars = static_cast<uint16_t>(delta.fileName.size());

            if (ffprotocol::IsFileNameLengthValid(delta.fixed.fileNameLengthChars)) {
                batch.push_back(std::move(delta));
                latestUsnInBatch = static_cast<uint64_t>(record->Usn);
            }

            offset += record->RecordLength;
        }

        startUsn = nextUsn;

        if (!batch.empty()) {
            if (!SendJournalBatch(pipe, writeMutex, volumeId, batch, latestUsnInBatch)) {
                break;
            }
            batch.clear();
        } else {
            // Caught up to the journal's current end -- task 5.5 requires
            // not blocking indefinitely, so this is a short, cancellable
            // sleep rather than a tight poll loop or a blocking read.
            // Sliced so StopVolumeScan/CloseUsnJournal/disconnect (which
            // wait for this thread to join) aren't stuck behind a full
            // uninterruptible sleep.
            for (auto waited = std::chrono::milliseconds::zero();
                 waited < kIdlePollInterval && !shouldStop.load(); waited += kIdlePollSliceInterval) {
                std::this_thread::sleep_for(kIdlePollSliceInterval);
            }
        }
    }

    CloseHandle(volumeHandle);
}

} // namespace ffindexsvc
