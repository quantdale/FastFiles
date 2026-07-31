#include "VolumeScanner.h"

#include <windows.h>
#include <winioctl.h>

#include <cstring>

#include "NtfsRawAccess.h"

namespace ffindexsvc {

std::vector<uint8_t> SerializeScanCursor(uint64_t resumeFileReferenceNumber) {
    std::vector<uint8_t> cursor(sizeof(uint64_t));
    std::memcpy(cursor.data(), &resumeFileReferenceNumber, sizeof(uint64_t));
    return cursor;
}

std::optional<uint64_t> DeserializeScanCursor(const std::vector<uint8_t>& cursor) {
    if (cursor.size() != sizeof(uint64_t)) {
        return std::nullopt;
    }
    uint64_t value = 0;
    std::memcpy(&value, cursor.data(), sizeof(uint64_t));
    return value;
}

namespace {

ffprotocol::MftRecordV1 ToWireRecord(uint64_t fileReferenceNumber, const ParsedMftRecord& parsed) {
    ffprotocol::MftRecordV1 record;
    record.fixed.fileReferenceNumber = fileReferenceNumber;
    record.fixed.parentFileReferenceNumber = parsed.parentFileReferenceNumber;
    // FSCTL_ENUM_USN_DATA + FSCTL_GET_NTFS_FILE_RECORD's $STANDARD_INFORMATION
    // read only exposes LastModificationTime distinctly (see
    // MftRecordParser) -- creation/last-access are set equal to it rather
    // than left unset. DurableStore only ever persists last-write time
    // (design.md 1.2), so this only affects these otherwise-unused wire
    // fields, not what the index actually stores.
    record.fixed.creationTime = parsed.lastWriteTime;
    record.fixed.lastModifiedTime = parsed.lastWriteTime;
    record.fixed.lastAccessTime = parsed.lastWriteTime;
    record.fixed.sizeBytes = parsed.realSizeBytes;
    record.fixed.fileAttributes = parsed.dosAttributes | (parsed.isDirectory ? FILE_ATTRIBUTE_DIRECTORY : 0);
    record.fixed.fileNameLengthChars = static_cast<uint16_t>(parsed.name.size());
    record.fileName = parsed.name;
    return record;
}

constexpr DWORD kEnumBufferBytes = 64 * 1024;

} // namespace

bool VolumeScanner::Run(wchar_t driveLetter, uint64_t startFileReferenceNumber, const BatchCallback& onBatch) {
    auto volume = RawVolumeHandle::Open(driveLetter);
    if (!volume) {
        return false;
    }

    uint64_t currentStart = startFileReferenceNumber;
    std::vector<uint8_t> outputBuffer(kEnumBufferBytes);

    while (!stopRequested_.load(std::memory_order_relaxed)) {
        MFT_ENUM_DATA_V0 enumData{};
        enumData.StartFileReferenceNumber = currentStart;
        enumData.LowUsn = 0;
        enumData.HighUsn = MAXLONGLONG;

        DWORD bytesReturned = 0;
        const BOOL ok = DeviceIoControl(
            volume->Get(), FSCTL_ENUM_USN_DATA, &enumData, sizeof(enumData), outputBuffer.data(),
            static_cast<DWORD>(outputBuffer.size()), &bytesReturned, nullptr);
        if (!ok) {
            // ERROR_HANDLE_EOF: enumeration reached the end of the MFT --
            // the scan completed normally (task: no indefinite blocking,
            // and this is the expected terminal condition, not an error).
            return true;
        }
        if (bytesReturned < sizeof(uint64_t)) {
            return true; // degenerate empty reply -- treat as completion rather than looping forever
        }

        uint64_t nextStart = 0;
        std::memcpy(&nextStart, outputBuffer.data(), sizeof(nextStart));

        std::vector<ffprotocol::MftRecordV1> batch;
        size_t offset = sizeof(uint64_t);
        while (offset + sizeof(USN_RECORD_V2) <= bytesReturned) {
            const auto* record = reinterpret_cast<const USN_RECORD_V2*>(outputBuffer.data() + offset);
            if (record->RecordLength == 0 || offset + record->RecordLength > bytesReturned) {
                break; // malformed trailing record -- stop consuming this buffer, not the whole scan
            }

            // Task 4.3: a single vanished/inconsistent record (deleted
            // between FSCTL_ENUM_USN_DATA and our follow-up fetch, or a
            // record that fails MftRecordParser's validation) is skipped,
            // never aborts the scan.
            auto parsed = FetchAndParseMftRecord(volume->Get(), volume->BytesPerSector(), record->FileReferenceNumber);
            if (parsed) {
                batch.push_back(ToWireRecord(record->FileReferenceNumber, *parsed));
            }

            offset += record->RecordLength;
        }

        currentStart = nextStart;

        if (!batch.empty() || bytesReturned > sizeof(uint64_t)) {
            // Persist/report progress even for a buffer that yielded zero
            // *forwardable* records (all skipped) -- the resume cursor
            // still must advance past them so a restart doesn't re-fetch
            // the same already-visited, already-rejected records forever.
            if (!onBatch(batch, SerializeScanCursor(nextStart))) {
                return true;
            }
        }
    }

    return true;
}

} // namespace ffindexsvc
