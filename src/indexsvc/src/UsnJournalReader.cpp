#include "UsnJournalReader.h"

#include <winioctl.h>

#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include "ffipc/PipeFraming.h"
#include "ffprotocol/Records.h"

#include "MftParser.h"
#include "PrivilegeVerification.h"
#include "UsnRecordParser.h"
#include "WriteDeadline.h"

namespace ffindexsvc {

namespace {

constexpr uint32_t kBatchSize = 512;
constexpr size_t kReadBufferSize = 64 * 1024;
constexpr std::chrono::milliseconds kIdlePollInterval{1000};
constexpr std::chrono::milliseconds kIdlePollSliceInterval{100};
constexpr uint64_t kMftSegmentNumberMask = 0x0000FFFFFFFFFFFFull;
constexpr uint32_t kUsnReasonFileDelete = 0x00000200;

bool IsRepresentableInLegacyV1(const ffprotocol::UsnDeltaV2& record) noexcept {
    if ((record.fixed.fieldMask & ~ffprotocol::kUsnFieldMaskAll) != 0) {
        return false;
    }
    const auto required = (record.fixed.reason & kUsnReasonFileDelete) != 0
        ? ffprotocol::kUsnFieldParentId | ffprotocol::kUsnFieldName
        : ffprotocol::kUsnFieldMaskRequiredForV1;
    return (record.fixed.fieldMask & required) == required;
}

bool SendJournalOpened(HANDLE pipe, std::mutex& writeMutex, ffprotocol::VolumeId volumeId, uint64_t journalId,
                        uint64_t currentUsn) {
    ffprotocol::UsnJournalOpenedPayload payload{volumeId, journalId, currentUsn};
    std::lock_guard<std::mutex> lock(writeMutex);
    return WriteFrameWithDeadline(pipe, static_cast<uint16_t>(ffprotocol::MessageType::UsnJournalOpened),
                                  &payload, sizeof(payload));
}

bool SendJournalBatch(HANDLE pipe, std::mutex& writeMutex, ffprotocol::VolumeId volumeId,
                       const std::vector<ffprotocol::UsnDeltaV2>& records, uint64_t latestUsn,
                       ffprotocol::ProtocolVersion negotiatedVersion) {
    if (negotiatedVersion.major == 1) {
        // A major-1 peer does not know message 19 or the V2 presence mask.
        // Never encode an absent V2 field as a legacy zero: omit a partial
        // non-delete update (the full reconciliation path will recover it)
        // while still advancing the journal position with an empty V1 batch.
        std::vector<ffprotocol::UsnDeltaV1> legacyRecords;
        legacyRecords.reserve(records.size());
        for (const auto& record : records) {
            if (IsRepresentableInLegacyV1(record)) {
                legacyRecords.push_back(ffprotocol::DowngradeUsnDeltaV2(record));
            }
        }
        const std::vector<uint8_t> serializedRecords = ffprotocol::SerializeUsnDeltaBatch(legacyRecords);
        ffprotocol::JournalRecordBatchHeader header{};
        header.volumeId = volumeId;
        header.recordCount = static_cast<uint32_t>(legacyRecords.size());
        header.latestUsn = latestUsn;

        std::vector<uint8_t> payload;
        payload.reserve(sizeof(header) + serializedRecords.size());
        const auto* headerBytes = reinterpret_cast<const uint8_t*>(&header);
        payload.insert(payload.end(), headerBytes, headerBytes + sizeof(header));
        payload.insert(payload.end(), serializedRecords.begin(), serializedRecords.end());

        std::lock_guard<std::mutex> lock(writeMutex);
        return WriteFrameWithDeadline(pipe, static_cast<uint16_t>(ffprotocol::MessageType::JournalRecordBatch),
                                      payload.data(), static_cast<uint32_t>(payload.size()));
    }

    const std::vector<uint8_t> serializedRecords = ffprotocol::SerializeUsnDeltaV2Batch(records);

    ffprotocol::JournalRecordBatchV2Header header{};
    header.volumeId = volumeId;
    header.recordCount = static_cast<uint32_t>(records.size());
    header.latestUsn = latestUsn;
    header.recordFormatVersion = 2;

    std::vector<uint8_t> payload;
    payload.reserve(sizeof(header) + serializedRecords.size());
    const auto* headerBytes = reinterpret_cast<const uint8_t*>(&header);
    payload.insert(payload.end(), headerBytes, headerBytes + sizeof(header));
    payload.insert(payload.end(), serializedRecords.begin(), serializedRecords.end());

    std::lock_guard<std::mutex> lock(writeMutex);
    return WriteFrameWithDeadline(pipe, static_cast<uint16_t>(ffprotocol::MessageType::JournalRecordBatchV2),
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
    // The FSCTL can return a different, nearby record when the exact FRN's
    // sequence number is stale. Its output identifies the 48-bit MFT
    // segment while the USN record carries a sequence number in the upper
    // 16 bits, so compare the stable segment portion before trusting it.
    if ((static_cast<uint64_t>(output->FileReferenceNumber.QuadPart) & kMftSegmentNumberMask)
        != (fileReferenceNumber & kMftSegmentNumberMask)) {
        return std::nullopt;
    }
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

// Queries the volume's USN journal, creating one first if the volume
// doesn't have an active journal yet (ERROR_JOURNAL_NOT_ACTIVE) -- the
// first time this volume is indexed, a journal must be brought into
// existence so future changes are tracked at all (task 5.1's "real USN
// journal reads" presumes one exists). Returns false if no journal data
// could be obtained either way.
bool QueryOrCreateUsnJournal(HANDLE volumeHandle, USN_JOURNAL_DATA_V0& journalData) {
    DWORD bytesReturned = 0;
    BOOL ok = DeviceIoControl(volumeHandle, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &journalData, sizeof(journalData),
                               &bytesReturned, nullptr);

    if (!ok && GetLastError() == ERROR_JOURNAL_NOT_ACTIVE) {
        CREATE_USN_JOURNAL_DATA createData{};
        createData.MaximumSize = 32ull * 1024 * 1024;
        createData.AllocationDelta = 4ull * 1024 * 1024;
        if (DeviceIoControl(volumeHandle, FSCTL_CREATE_USN_JOURNAL, &createData, sizeof(createData), nullptr, 0,
                             &bytesReturned, nullptr)) {
            ok = DeviceIoControl(volumeHandle, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &journalData, sizeof(journalData),
                                  &bytesReturned, nullptr);
        }
    }
    return ok != FALSE;
}

} // namespace

JournalStreamOutcome RunUsnJournalStream(
    HANDLE pipe, std::mutex& writeMutex, ffprotocol::VolumeId volumeId, wchar_t driveLetter, uint64_t resumeUsn,
    ffprotocol::ProtocolVersion negotiatedVersion, const std::atomic<bool>& shouldStop) {
    const wchar_t volumePath[] = {L'\\', L'\\', L'.', L'\\', driveLetter, L':', L'\0'};

    // SeBackupPrivilege must be explicitly enabled (not just held) for
    // FILE_FLAG_BACKUP_SEMANTICS to bypass the normal ACL check on a raw
    // volume open -- idempotent and cheap, ensured at every open site
    // rather than relying on service-startup ordering.
    if (!EnsureBackupPrivilegeEnabled()) {
        return JournalStreamOutcome::Ended;
    }

    HANDLE volumeHandle = CreateFileW(
        volumePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (volumeHandle == INVALID_HANDLE_VALUE) {
        return JournalStreamOutcome::Ended;
    }

    USN_JOURNAL_DATA_V0 journalData{};
    if (!QueryOrCreateUsnJournal(volumeHandle, journalData)) {
        CloseHandle(volumeHandle);
        return JournalStreamOutcome::Ended;
    }

    if (!SendJournalOpened(pipe, writeMutex, volumeId, static_cast<uint64_t>(journalData.UsnJournalID),
                            static_cast<uint64_t>(journalData.NextUsn))) {
        CloseHandle(volumeHandle);
        return JournalStreamOutcome::Ended;
    }

    NTFS_VOLUME_DATA_BUFFER volumeData{};
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(volumeHandle, FSCTL_GET_NTFS_VOLUME_DATA, nullptr, 0, &volumeData, sizeof(volumeData),
                          &bytesReturned, nullptr)) {
        CloseHandle(volumeHandle);
        return JournalStreamOutcome::Ended;
    }
    const uint32_t recordSize = static_cast<uint32_t>(volumeData.BytesPerFileRecordSegment);
    const uint32_t bytesPerSector = static_cast<uint32_t>(volumeData.BytesPerSector);
    if (recordSize == 0 || bytesPerSector == 0) {
        CloseHandle(volumeHandle);
        return JournalStreamOutcome::Ended;
    }
    std::vector<uint8_t> mftOutputBuffer(sizeof(NTFS_FILE_RECORD_OUTPUT_BUFFER) - 1 + recordSize);
    std::vector<uint8_t> mftScratch(recordSize);

    USN startUsn = static_cast<USN>(resumeUsn);
    std::vector<uint8_t> readBuffer(kReadBufferSize);
    std::vector<ffprotocol::UsnDeltaV2> batch;
    batch.reserve(kBatchSize);
    uint64_t latestUsnInBatch = resumeUsn;
    JournalStreamOutcome outcome = JournalStreamOutcome::Ended;

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
            const DWORD error = GetLastError();
            if (error == ERROR_JOURNAL_ENTRY_DELETED || error == ERROR_INVALID_PARAMETER) {
                // D6/task 7.6: startUsn is no longer within the journal's
                // retained range (wrap, or the journal was deleted and
                // recreated) -- distinct from ordinary teardown so the
                // caller can tell the engine to fall back to a
                // reconciliation sweep rather than silently resume.
                outcome = JournalStreamOutcome::ResumePositionInvalid;
                break;
            }
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
        while (offset + sizeof(USN_RECORD_COMMON_HEADER) <= readBytesReturned) {
            USN_RECORD_COMMON_HEADER common{};
            std::memcpy(&common, readBuffer.data() + offset, sizeof(common));
            if (common.RecordLength == 0 || offset + common.RecordLength > readBytesReturned) {
                break; // malformed trailing record -- stop parsing this read, next iteration continues from nextUsn
            }

            const auto decoded = DecodeUsnRecord(readBuffer.data() + offset, common.RecordLength);
            if (!decoded) {
                offset += common.RecordLength;
                continue;
            }

            ffprotocol::UsnDeltaV2 delta{};
            delta.fixed.usn = decoded->usn;
            delta.fixed.fileReferenceNumber = decoded->fileReferenceNumber;
            delta.fixed.parentFileReferenceNumber = decoded->parentFileReferenceNumber;
            delta.fixed.reason = decoded->reason;
            delta.fixed.timestamp = decoded->timestamp;

            auto enriched = TryReenrichFromMft(volumeHandle, decoded->fileReferenceNumber, recordSize, bytesPerSector,
                                                mftOutputBuffer, mftScratch);
            if (enriched) {
                delta.fixed.parentFileReferenceNumber = enriched->parentFileReferenceNumber;
                delta.fixed.creationTime = enriched->creationTime;
                delta.fixed.lastModifiedTime = enriched->lastModifiedTime;
                delta.fixed.lastAccessTime = enriched->lastAccessTime;
                delta.fixed.sizeBytes = enriched->sizeBytes;
                delta.fixed.fileAttributes = enriched->fileAttributes;
                delta.fileName = enriched->fileName;
                delta.fixed.fieldMask = ffprotocol::kUsnFieldParentId | ffprotocol::kUsnFieldName |
                                        ffprotocol::kUsnFieldSizeBytes | ffprotocol::kUsnFieldCreationTime |
                                        ffprotocol::kUsnFieldLastModifiedTime | ffprotocol::kUsnFieldLastAccessTime |
                                        ffprotocol::kUsnFieldAttributes;
            } else {
                // Most commonly a delete: fall back to the raw
                // USN_RECORD's own (shorter, but still allowlisted) name
                // field rather than dropping the record entirely --
                // tasks.md 5.3/5.4 still require forwarding the change.
                delta.fixed.sizeBytes = 0;
                delta.fixed.fileAttributes = decoded->fileAttributes;
                delta.fixed.lastModifiedTime = decoded->timestamp;
                delta.fixed.fieldMask = ffprotocol::kUsnFieldParentId | ffprotocol::kUsnFieldName |
                                        ffprotocol::kUsnFieldLastModifiedTime | ffprotocol::kUsnFieldAttributes;
                // FileNameOffset/FileNameLength are untrusted on-disk bytes.
                // The outer loop only bounds RecordLength against the read
                // buffer; the name range must also be verified to lie within
                // RecordLength before it is copied, or an out-of-range pair
                // would read past the record. Reject the whole record (skip
                // forwarding; the offset below still advances) rather than
                // forward a truncated/garbage name -- consistent with the
                // reject-whole-record discipline.
                const auto* nameStart =
                    reinterpret_cast<const char16_t*>(readBuffer.data() + offset + decoded->fileNameOffset);
                delta.fileName.assign(nameStart, decoded->fileNameLength / sizeof(char16_t));
            }
            delta.fixed.fileNameLengthChars = static_cast<uint16_t>(delta.fileName.size());

            if (ffprotocol::IsFileNameLengthValid(delta.fixed.fileNameLengthChars)) {
                batch.push_back(std::move(delta));
                latestUsnInBatch = decoded->usn;
            }

            offset += common.RecordLength;
        }

        startUsn = nextUsn;

        if (!batch.empty()) {
            if (!SendJournalBatch(pipe, writeMutex, volumeId, batch, latestUsnInBatch, negotiatedVersion)) {
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
    return outcome;
}

} // namespace ffindexsvc
