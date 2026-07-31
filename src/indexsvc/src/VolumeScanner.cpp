#include "VolumeScanner.h"

#include <winioctl.h>

#include <cstring>
#include <thread>
#include <vector>

#include "ffipc/PipeFraming.h"
#include "ffprotocol/Records.h"

#include "MftParser.h"

namespace ffindexsvc {

namespace {

constexpr uint32_t kBatchSize = 512;

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

    // FILE_FLAG_BACKUP_SEMANTICS + the process's already-enabled
    // SeBackupPrivilege (enabled once at service startup -- see
    // PrivilegeVerification.cpp) is what lets this open succeed without
    // administrative rights (design.md D1 "SeBackupPrivilege only").
    HANDLE volumeHandle = CreateFileW(
        volumePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (volumeHandle == INVALID_HANDLE_VALUE) {
        SendScanComplete(pipe, writeMutex, volumeId);
        return;
    }

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
    const uint64_t totalRecords = static_cast<uint64_t>(volumeData.MftValidDataLength.QuadPart) / recordSize;

    const size_t outputBufferSize = sizeof(NTFS_FILE_RECORD_OUTPUT_BUFFER) - 1 + recordSize;
    std::vector<uint8_t> outputBuffer(outputBufferSize);
    std::vector<uint8_t> recordScratch(recordSize);

    std::vector<ffprotocol::MftRecordV1> batch;
    batch.reserve(kBatchSize);

    uint64_t index = DecodeCursor(resumeCursor);
    bool writeFailed = false;
    for (; index < totalRecords && !shouldStop.load() && !writeFailed; ++index) {
        NTFS_FILE_RECORD_INPUT_BUFFER input{};
        input.FileReferenceNumber.QuadPart = static_cast<LONGLONG>(index);

        DWORD recordBytesReturned = 0;
        const BOOL ok = DeviceIoControl(
            volumeHandle, FSCTL_GET_NTFS_FILE_RECORD, &input, sizeof(input), outputBuffer.data(),
            static_cast<DWORD>(outputBuffer.size()), &recordBytesReturned, nullptr);
        if (!ok) {
            // tasks.md 4.3: a single unreadable/nonexistent record is
            // skipped, not treated as fatal to the whole scan.
            continue;
        }

        const auto* output = reinterpret_cast<const NTFS_FILE_RECORD_OUTPUT_BUFFER*>(outputBuffer.data());
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
        record.fixed.fileReferenceNumber = static_cast<uint64_t>(output->FileReferenceNumber.QuadPart);
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
            writeFailed = !SendScanBatch(pipe, writeMutex, volumeId, batch, index + 1);
            batch.clear();
        }
    }

    if (!batch.empty() && !writeFailed) {
        writeFailed = !SendScanBatch(pipe, writeMutex, volumeId, batch, index);
    }

    CloseHandle(volumeHandle);

    if (!writeFailed && !shouldStop.load()) {
        SendScanComplete(pipe, writeMutex, volumeId);
    }
}

} // namespace ffindexsvc
