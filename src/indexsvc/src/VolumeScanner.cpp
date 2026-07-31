#include "VolumeScanner.h"

#include <winioctl.h>

#include <cstring>
#include <thread>
#include <vector>

#include "ffipc/PipeFraming.h"
#include "ffprotocol/Records.h"

#include "MftParser.h"
#include "PrivilegeVerification.h"

namespace ffindexsvc {

namespace {

constexpr uint32_t kBatchSize = 512;
constexpr size_t kEnumBufferSize = 64 * 1024;

uint64_t DecodeCursor(const std::vector<uint8_t>& cursor) {
    if (cursor.size() != sizeof(uint64_t)) {
        return 0;
    }
    uint64_t value = 0;
    std::memcpy(&value, cursor.data(), sizeof(value));
    return value;
}

std::vector<uint8_t> EncodeCursor(uint64_t nextIndex) {
    std::vector<uint8_t> cursor(sizeof(uint64_t));
    std::memcpy(cursor.data(), &nextIndex, sizeof(nextIndex));
    return cursor;
}

bool SendScanBatch(HANDLE pipe, std::mutex& writeMutex, ffprotocol::VolumeId volumeId,
                    const std::vector<ffprotocol::MftRecordV1>& records, uint64_t nextIndex) {
    const std::vector<uint8_t> cursor = EncodeCursor(nextIndex);
    const std::vector<uint8_t> serializedRecords = ffprotocol::SerializeMftBatch(records);

    ffprotocol::ScanRecordBatchHeader header{};
    header.volumeId = volumeId;
    header.recordCount = static_cast<uint32_t>(records.size());
    header.resumeCursorLengthBytes = static_cast<uint16_t>(cursor.size());

    std::vector<uint8_t> payload;
    payload.reserve(sizeof(header) + cursor.size() + serializedRecords.size());
    const auto* headerBytes = reinterpret_cast<const uint8_t*>(&header);
    payload.insert(payload.end(), headerBytes, headerBytes + sizeof(header));
    payload.insert(payload.end(), cursor.begin(), cursor.end());
    payload.insert(payload.end(), serializedRecords.begin(), serializedRecords.end());

    std::lock_guard<std::mutex> lock(writeMutex);
    return ffipc::WriteFrame(pipe, static_cast<uint16_t>(ffprotocol::MessageType::ScanRecordBatch),
                              payload.data(), static_cast<uint32_t>(payload.size()));
}

bool SendScanComplete(HANDLE pipe, std::mutex& writeMutex, ffprotocol::VolumeId volumeId) {
    ffprotocol::ScanCompletePayload payload{volumeId};
    std::lock_guard<std::mutex> lock(writeMutex);
    return ffipc::WriteFrame(pipe, static_cast<uint16_t>(ffprotocol::MessageType::ScanComplete), &payload, sizeof(payload));
}

} // namespace

void RunVolumeScan(
    HANDLE pipe, std::mutex& writeMutex, ffprotocol::VolumeId volumeId, wchar_t driveLetter,
    const std::vector<uint8_t>& resumeCursor, const std::atomic<bool>& shouldStop) {
    const wchar_t volumePath[] = {L'\\', L'\\', L'.', L'\\', driveLetter, L':', L'\0'};

    // FILE_FLAG_BACKUP_SEMANTICS + an enabled SeBackupPrivilege is what
    // lets this open succeed without administrative rights (design.md D1
    // "SeBackupPrivilege only"). Holding the privilege is not enough --
    // it must be explicitly enabled, so ensure it (idempotent, cheap) at
    // every open site rather than relying on service-startup ordering.
    if (!EnsureBackupPrivilegeEnabled()) {
        SendScanComplete(pipe, writeMutex, volumeId);
        return;
    }
    HANDLE volumeHandle = CreateFileW(
        volumePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (volumeHandle == INVALID_HANDLE_VALUE) {
        SendScanComplete(pipe, writeMutex, volumeId);
        return;
    }

    // Authoritative record/sector geometry comes from the volume itself,
    // never a hardcoded constant.
    NTFS_VOLUME_DATA_BUFFER volumeData{};
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(volumeHandle, FSCTL_GET_NTFS_VOLUME_DATA, nullptr, 0, &volumeData, sizeof(volumeData),
                          &bytesReturned, nullptr)) {
        CloseHandle(volumeHandle);
        SendScanComplete(pipe, writeMutex, volumeId);
        return;
    }

    const uint32_t recordSize = static_cast<uint32_t>(volumeData.BytesPerFileRecordSegment);
    const uint32_t bytesPerSector = static_cast<uint32_t>(volumeData.BytesPerSector);
    if (recordSize == 0 || bytesPerSector == 0) {
        CloseHandle(volumeHandle);
        SendScanComplete(pipe, writeMutex, volumeId);
        return;
    }

    const size_t outputBufferSize = sizeof(NTFS_FILE_RECORD_OUTPUT_BUFFER) - 1 + recordSize;
    std::vector<uint8_t> outputBuffer(outputBufferSize);
    std::vector<uint8_t> recordScratch(recordSize);
    std::vector<uint8_t> enumBuffer(kEnumBufferSize);

    std::vector<ffprotocol::MftRecordV1> batch;
    batch.reserve(kBatchSize);

    // The resume cursor is a record INDEX (8-byte LE); FSCTL_ENUM_USN_DATA
    // positions are FRNs, i.e. byte offsets into the $MFT (FRN == index *
    // recordSize) -- converted at each enumeration call below.
    uint64_t nextIndex = DecodeCursor(resumeCursor);
    bool writeFailed = false;
    bool enumerationEnded = false;

    while (!enumerationEnded && !shouldStop.load() && !writeFailed) {
        MFT_ENUM_DATA_V0 enumData{};
        enumData.StartFileReferenceNumber = nextIndex * recordSize;
        enumData.LowUsn = 0;
        enumData.HighUsn = MAXLONGLONG;

        DWORD enumBytesReturned = 0;
        if (!DeviceIoControl(volumeHandle, FSCTL_ENUM_USN_DATA, &enumData, sizeof(enumData), enumBuffer.data(),
                              static_cast<DWORD>(enumBuffer.size()), &enumBytesReturned, nullptr)) {
            // ERROR_HANDLE_EOF is the expected terminal condition (the
            // enumeration reached the end of the MFT); any other failure
            // ends the scan the same way rather than being retried
            // forever.
            enumerationEnded = true;
            break;
        }
        if (enumBytesReturned < sizeof(uint64_t)) {
            enumerationEnded = true; // degenerate empty reply -- treat as completion rather than looping forever
            break;
        }

        // The first 8 bytes are the FRN (byte offset) to continue from on
        // the next call -- back in cursor units, that is the index of the
        // next record to enumerate.
        uint64_t nextStartFrn = 0;
        std::memcpy(&nextStartFrn, enumBuffer.data(), sizeof(nextStartFrn));
        const uint64_t bufferEndIndex = nextStartFrn / recordSize;

        bool bufferHadRecords = false;
        // Index just past the last buffer entry we actually processed --
        // the cursor position if this buffer is abandoned mid-way
        // (cancellation), where advancing to bufferEndIndex would skip
        // records never fetched at all.
        uint64_t consumedIndex = nextIndex;
        size_t offset = sizeof(uint64_t);
        while (offset + sizeof(USN_RECORD_V2) <= enumBytesReturned && !shouldStop.load() && !writeFailed) {
            const auto* entry = reinterpret_cast<const USN_RECORD_V2*>(enumBuffer.data() + offset);
            if (entry->RecordLength == 0 || offset + entry->RecordLength > enumBytesReturned) {
                break; // malformed trailing record -- stop consuming this buffer, resume from bufferEndIndex
            }
            bufferHadRecords = true;
            offset += entry->RecordLength;

            const uint64_t frn = entry->FileReferenceNumber;
            consumedIndex = frn / recordSize + 1;
            NTFS_FILE_RECORD_INPUT_BUFFER input{};
            input.FileReferenceNumber.QuadPart = static_cast<LONGLONG>(frn);

            DWORD recordBytesReturned = 0;
            const BOOL ok = DeviceIoControl(
                volumeHandle, FSCTL_GET_NTFS_FILE_RECORD, &input, sizeof(input), outputBuffer.data(),
                static_cast<DWORD>(outputBuffer.size()), &recordBytesReturned, nullptr);
            if (!ok) {
                // tasks.md 4.3: a single unreadable/vanished record
                // (deleted between the enumeration and this fetch) is
                // skipped, not treated as fatal to the whole scan.
                continue;
            }

            const auto* output = reinterpret_cast<const NTFS_FILE_RECORD_OUTPUT_BUFFER*>(outputBuffer.data());
            // The FSCTL can return a different, nearby record when the
            // exact FRN's sequence number is stale -- verify the returned
            // record is the one that was requested before trusting it.
            if (static_cast<uint64_t>(output->FileReferenceNumber.QuadPart) != frn) {
                continue;
            }
            const uint32_t returnedRecordLength = output->FileRecordLength;
            if (returnedRecordLength == 0 || returnedRecordLength > recordSize) {
                continue;
            }

            // Fixup mutates in place -- copy into scratch so a bad record
            // never corrupts our own read buffer's next iteration.
            std::memcpy(recordScratch.data(), output->FileRecordBuffer, returnedRecordLength);
            if (ApplyFixupAndValidate(recordScratch.data(), returnedRecordLength, bytesPerSector) != FixupResult::Ok) {
                continue;
            }
            auto parsed = ParseMftAttributes(recordScratch.data(), returnedRecordLength);
            if (!parsed) {
                continue;
            }

            ffprotocol::MftRecordV1 record{};
            record.fixed.fileReferenceNumber = frn;
            record.fixed.parentFileReferenceNumber = parsed->parentFileReferenceNumber;
            record.fixed.creationTime = parsed->creationTime;
            record.fixed.lastModifiedTime = parsed->lastModifiedTime;
            record.fixed.lastAccessTime = parsed->lastAccessTime;
            record.fixed.sizeBytes = parsed->sizeBytes;
            record.fixed.fileAttributes = parsed->fileAttributes;
            record.fixed.fileNameLengthChars = static_cast<uint16_t>(parsed->fileName.size());
            record.fileName = std::move(parsed->fileName);
            batch.push_back(std::move(record));

            if (batch.size() >= kBatchSize) {
                // Mid-buffer flush: the cursor can only advance past
                // records already enumerated, so it is this record's own
                // index + 1 (FRN / recordSize), not the buffer's end.
                writeFailed = !SendScanBatch(pipe, writeMutex, volumeId, batch, consumedIndex);
                batch.clear();
            }
        }

        // The buffer is consumed (or abandoned on cancellation); advance
        // the cursor past it entirely -- including buffers whose records
        // were ALL skipped (an empty batch is still sent then, purely to
        // move the resume cursor), so a resumed scan never re-fetches the
        // same already-visited, already-rejected records forever. On a
        // mid-buffer stop the cursor only advances past the entries
        // actually processed.
        const bool bufferAbandoned = shouldStop.load() || writeFailed;
        if (!bufferAbandoned && !bufferHadRecords && bufferEndIndex <= nextIndex) {
            // No records and no forward progress -- treat as end of the
            // enumeration rather than spinning on the same position.
            enumerationEnded = true;
            break;
        }
        nextIndex = bufferAbandoned ? consumedIndex : bufferEndIndex;

        if (!writeFailed && bufferHadRecords) {
            writeFailed = !SendScanBatch(pipe, writeMutex, volumeId, batch, nextIndex);
            batch.clear();
        }
    }

    if (!batch.empty() && !writeFailed) {
        writeFailed = !SendScanBatch(pipe, writeMutex, volumeId, batch, nextIndex);
    }

    CloseHandle(volumeHandle);

    if (!writeFailed && !shouldStop.load()) {
        SendScanComplete(pipe, writeMutex, volumeId);
    }
}

} // namespace ffindexsvc
