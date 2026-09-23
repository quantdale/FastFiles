#include "VolumeScanner.h"

#include <winioctl.h>

#include <cstring>
#include <thread>
#include <vector>

#include "ffipc/PipeFraming.h"
#include "ffprotocol/Records.h"

#include "MftParser.h"
#include "PrivilegeVerification.h"
#include "WriteDeadline.h"

namespace ffindexsvc {

namespace {

constexpr uint32_t kBatchSize = 64;
constexpr size_t kEnumBufferSize = 64 * 1024;
constexpr uint64_t kMftSegmentNumberMask = 0x0000FFFFFFFFFFFFull;

uint64_t DecodeCursor(const std::vector<uint8_t>& cursor) {
    if (cursor.size() != sizeof(uint64_t)) {
        return 0;
    }
    uint64_t value = 0;
    std::memcpy(&value, cursor.data(), sizeof(value));
    return value;
}

// allocated-size-storage-accuracy D3/2.4: V2 -> V1 MFT record downgrade for
// legacy peers. V1 cannot carry allocated size; dropping the field here is
// lossless for the peer's purposes (the field simply does not exist in its
// layout) -- unlike the USN presence-mask case there is no partial-update
// hazard because full scan records always carry both values.
ffprotocol::MftRecordV1 DowngradeMftRecordV2(const ffprotocol::MftRecordV2& record) {
    ffprotocol::MftRecordV1 result{};
    result.fixed.fileReferenceNumber = record.fixed.fileReferenceNumber;
    result.fixed.parentFileReferenceNumber = record.fixed.parentFileReferenceNumber;
    result.fixed.creationTime = record.fixed.creationTime;
    result.fixed.lastModifiedTime = record.fixed.lastModifiedTime;
    result.fixed.lastAccessTime = record.fixed.lastAccessTime;
    result.fixed.sizeBytes = record.fixed.sizeBytes;
    result.fixed.fileAttributes = record.fixed.fileAttributes;
    result.fixed.fileNameLengthChars = record.fixed.fileNameLengthChars;
    result.fileName = record.fileName;
    return result;
}

std::vector<uint8_t> EncodeCursor(uint64_t nextFileReference) {
    std::vector<uint8_t> cursor(sizeof(uint64_t));
    std::memcpy(cursor.data(), &nextFileReference, sizeof(nextFileReference));
    return cursor;
}

bool SendScanBatch(HANDLE pipe, std::mutex& writeMutex, ffprotocol::VolumeId volumeId,
                    const std::vector<ffprotocol::MftRecordV1>& records, uint64_t nextFileReference) {
    const std::vector<uint8_t> cursor = EncodeCursor(nextFileReference);
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
    return WriteFrameWithDeadline(pipe, static_cast<uint16_t>(ffprotocol::MessageType::ScanRecordBatch),
                                  payload.data(), static_cast<uint32_t>(payload.size()));
}

// allocated-size-storage-accuracy D3/2.4: ScanRecordBatchV2 with
// MftRecordFixedV2 records (fixed layout + allocatedSizeBytes) for peers
// that negotiated protocol {2,1}+. Distinct message type + explicit
// recordFormatVersion so a legacy parser can never reinterpret the larger
// fixed records as the V1 layout.
bool SendScanBatchV2(HANDLE pipe, std::mutex& writeMutex, ffprotocol::VolumeId volumeId,
                     const std::vector<ffprotocol::MftRecordV2>& records, uint64_t nextFileReference) {
    const std::vector<uint8_t> cursor = EncodeCursor(nextFileReference);
    const std::vector<uint8_t> serializedRecords = ffprotocol::SerializeMftBatchV2(records);

    ffprotocol::ScanRecordBatchV2Header header{};
    header.volumeId = volumeId;
    header.recordCount = static_cast<uint32_t>(records.size());
    header.resumeCursorLengthBytes = static_cast<uint16_t>(cursor.size());
    header.recordFormatVersion = 2;

    std::vector<uint8_t> payload;
    payload.reserve(sizeof(header) + cursor.size() + serializedRecords.size());
    const auto* headerBytes = reinterpret_cast<const uint8_t*>(&header);
    payload.insert(payload.end(), headerBytes, headerBytes + sizeof(header));
    payload.insert(payload.end(), cursor.begin(), cursor.end());
    payload.insert(payload.end(), serializedRecords.begin(), serializedRecords.end());

    std::lock_guard<std::mutex> lock(writeMutex);
    return WriteFrameWithDeadline(pipe, static_cast<uint16_t>(ffprotocol::MessageType::ScanRecordBatchV2),
                                  payload.data(), static_cast<uint32_t>(payload.size()));
}

bool SendScanComplete(HANDLE pipe, std::mutex& writeMutex, ffprotocol::VolumeId volumeId) {
    ffprotocol::ScanCompletePayload payload{volumeId};
    std::lock_guard<std::mutex> lock(writeMutex);
    return WriteFrameWithDeadline(pipe, static_cast<uint16_t>(ffprotocol::MessageType::ScanComplete),
                                  &payload, sizeof(payload));
}

class BackgroundMode {
public:
    explicit BackgroundMode(bool enabled) : enabled_(enabled && SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN)) {}
    ~BackgroundMode() {
        if (enabled_) {
            SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
        }
    }

private:
    bool enabled_ = false;
};

} // namespace

// index-durability-false-success-closure D1/2.1: the single terminal sender.
// A failed scan must never masquerade as a completion -- the engine treats
// ScanComplete as the authority to mark the volume indexed and to finalize
// reconciliation passes, which would delete rows a failed scan simply never
// reached. Peers older than {2,2} cannot parse ScanFailed, so they keep
// receiving today's ScanComplete byte-for-byte on the same paths.
// hardening-campaign Phase 7 H2: now declared in VolumeScanner.h so the
// ServiceConnection exception boundary can emit the failure terminal too.
bool SendScanTerminal(HANDLE pipe, std::mutex& writeMutex, ffprotocol::VolumeId volumeId,
                      ffprotocol::ProtocolVersion negotiatedVersion, bool success) {
    if (!success && ffprotocol::ProtocolSupportsScanFailure(negotiatedVersion)) {
        ffprotocol::ScanFailedPayload payload{volumeId};
        std::lock_guard<std::mutex> lock(writeMutex);
        return WriteFrameWithDeadline(pipe, static_cast<uint16_t>(ffprotocol::MessageType::ScanFailed),
                                      &payload, sizeof(payload));
    }
    return SendScanComplete(pipe, writeMutex, volumeId);
}

void RunVolumeScan(
    HANDLE pipe, std::mutex& writeMutex, ffprotocol::VolumeId volumeId, wchar_t driveLetter,
    const std::vector<uint8_t>& resumeCursor, bool lowPriority,
    ffprotocol::ProtocolVersion negotiatedVersion, const std::atomic<bool>& shouldStop) {
    // THREAD_MODE_BACKGROUND lowers both CPU and I/O scheduling priority
    // for the lifetime of a reconciliation worker. It is thread-local, so
    // normal scans and journal readers remain responsive.
    BackgroundMode backgroundMode(lowPriority);
    const wchar_t volumePath[] = {L'\\', L'\\', L'.', L'\\', driveLetter, L':', L'\0'};

    // FILE_FLAG_BACKUP_SEMANTICS + an enabled SeBackupPrivilege is what
    // lets this open succeed without administrative rights (design.md D1
    // "SeBackupPrivilege only"). Holding the privilege is not enough --
    // it must be explicitly enabled, so ensure it (idempotent, cheap) at
    // every open site rather than relying on service-startup ordering.
    if (!EnsureBackupPrivilegeEnabled()) {
        const DWORD privilegeEnableError = GetLastError();
        const TokenPrivilegeState tokenState = CaptureTokenPrivilegeState();
        // Preserve the existing fail-closed behavior, but retain enough
        // evidence to distinguish a missing/disabled privilege from a raw
        // volume denial when the enable step itself prevented the open.
        LogRawVolumeOpenDiagnostic(
            volumePath, tokenState,
            ClassifyRawVolumeOpen(tokenState, false, privilegeEnableError), privilegeEnableError);
        SendScanTerminal(pipe, writeMutex, volumeId, negotiatedVersion, false);
        return;
    }
    // Capture the actual service token immediately before the raw-volume
    // open. The capture is observational and must not alter the call path.
    const TokenPrivilegeState tokenState = CaptureTokenPrivilegeState();
    HANDLE volumeHandle = CreateFileW(
        volumePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    const bool volumeOpened = volumeHandle != INVALID_HANDLE_VALUE;
    const DWORD volumeOpenError = volumeOpened ? ERROR_SUCCESS : GetLastError();
    LogRawVolumeOpenDiagnostic(volumePath, tokenState,
                               ClassifyRawVolumeOpen(tokenState, volumeOpened, volumeOpenError), volumeOpenError);
    if (!volumeOpened) {
        SendScanTerminal(pipe, writeMutex, volumeId, negotiatedVersion, false);
        return;
    }

    // Authoritative record/sector geometry comes from the volume itself,
    // never a hardcoded constant.
    NTFS_VOLUME_DATA_BUFFER volumeData{};
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(volumeHandle, FSCTL_GET_NTFS_VOLUME_DATA, nullptr, 0, &volumeData, sizeof(volumeData),
                           &bytesReturned, nullptr)) {
        CloseHandle(volumeHandle);
        SendScanTerminal(pipe, writeMutex, volumeId, negotiatedVersion, false);
        return;
    }

    const uint32_t recordSize = static_cast<uint32_t>(volumeData.BytesPerFileRecordSegment);
    const uint32_t bytesPerSector = static_cast<uint32_t>(volumeData.BytesPerSector);
    if (recordSize == 0 || bytesPerSector == 0) {
        CloseHandle(volumeHandle);
        SendScanTerminal(pipe, writeMutex, volumeId, negotiatedVersion, false);
        return;
    }

    const size_t outputBufferSize = sizeof(NTFS_FILE_RECORD_OUTPUT_BUFFER) - 1 + recordSize;
    std::vector<uint8_t> outputBuffer(outputBufferSize);
    std::vector<uint8_t> recordScratch(recordSize);
    std::vector<uint8_t> enumBuffer(kEnumBufferSize);

    std::vector<ffprotocol::MftRecordV2> batch;
    batch.reserve(kBatchSize);
    // allocated-size-storage-accuracy D3/2.4: records accumulate in the
    // additive V2 shape; at send time they go out as V2 (peer negotiated
    // {2,1}+) or are downgraded to the exact legacy V1 bytes for older
    // peers. One accumulation path, two wire encodings.
    const bool sendAllocated = ffprotocol::ProtocolSupportsAllocatedSize(negotiatedVersion);
    const auto sendBatch = [&](const std::vector<ffprotocol::MftRecordV2>& records,
                               uint64_t nextFileReference) {
        if (sendAllocated) {
            return SendScanBatchV2(pipe, writeMutex, volumeId, records, nextFileReference);
        }
        std::vector<ffprotocol::MftRecordV1> legacyRecords;
        legacyRecords.reserve(records.size());
        for (const auto& record : records) {
            legacyRecords.push_back(DowngradeMftRecordV2(record));
        }
        return SendScanBatch(pipe, writeMutex, volumeId, legacyRecords, nextFileReference);
    };

    // FSCTL_ENUM_USN_DATA returns the exact opaque file-reference value
    // required by its next call as the first 8 bytes of each output
    // buffer. Preserve it verbatim in the resume cursor.
    uint64_t nextFileReference = DecodeCursor(resumeCursor);
    size_t recordsSinceYield = 0;
    bool writeFailed = false;
    bool enumerationEnded = false;
    bool enumerationFailed = false;

    while (!enumerationEnded && !shouldStop.load() && !writeFailed) {
        MFT_ENUM_DATA_V0 enumData{};
        const uint64_t bufferStartFileReference = nextFileReference;
        enumData.StartFileReferenceNumber = nextFileReference;
        enumData.LowUsn = 0;
        enumData.HighUsn = MAXLONGLONG;

        DWORD enumBytesReturned = 0;
        if (!DeviceIoControl(volumeHandle, FSCTL_ENUM_USN_DATA, &enumData, sizeof(enumData), enumBuffer.data(),
                              static_cast<DWORD>(enumBuffer.size()), &enumBytesReturned, nullptr)) {
            // index-durability-false-success-closure 2.2: ERROR_HANDLE_EOF
            // is the expected terminal condition (the enumeration reached
            // the end of the MFT). Any other failure -- I/O error, journal
            // gone, disk pulled -- is an incomplete scan, not a
            // completion: it stops the same way (never retried forever)
            // but the terminal message distinguishes the two.
            enumerationEnded = true;
            enumerationFailed = GetLastError() != ERROR_HANDLE_EOF;
            break;
        }
        if (enumBytesReturned < sizeof(uint64_t)) {
            // Degenerate reply with no cursor: cannot resume and cannot be
            // trusted as a full enumeration -- treat as a failure, not an
            // silent completion.
            enumerationEnded = true;
            enumerationFailed = true;
            break;
        }

        uint64_t bufferNextFileReference = 0;
        std::memcpy(&bufferNextFileReference, enumBuffer.data(), sizeof(bufferNextFileReference));

        bool bufferHadRecords = false;
        size_t offset = sizeof(uint64_t);
        while (offset + sizeof(USN_RECORD_V2) <= enumBytesReturned && !shouldStop.load() && !writeFailed) {
            const auto* entry = reinterpret_cast<const USN_RECORD_V2*>(enumBuffer.data() + offset);
            if (entry->RecordLength == 0 || offset + entry->RecordLength > enumBytesReturned) {
                break; // malformed trailing record -- stop consuming this buffer
            }
            bufferHadRecords = true;
            offset += entry->RecordLength;
            if (lowPriority && ++recordsSinceYield >= 128) {
                Sleep(1); // pace every enumerated record, including entries that vanish or fail validation
                recordsSinceYield = 0;
            }

            const uint64_t frn = entry->FileReferenceNumber;
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
            // A USN file reference carries the MFT sequence number in its
            // upper 16 bits. FSCTL_GET_NTFS_FILE_RECORD identifies the
            // returned segment by the lower 48-bit MFT record number, so
            // compare that stable segment portion rather than rejecting
            // every valid record whose sequence is nonzero.
            if ((static_cast<uint64_t>(output->FileReferenceNumber.QuadPart) & kMftSegmentNumberMask)
                != (frn & kMftSegmentNumberMask)) {
                continue;
            }
            const uint32_t returnedRecordLength = output->FileRecordLength;
            if (returnedRecordLength == 0 || returnedRecordLength > recordSize) {
                continue;
            }

            // Fixup mutates in place -- copy into scratch so a bad record
            // never corrupts our own read buffer's next iteration.
            std::memcpy(recordScratch.data(), output->FileRecordBuffer, returnedRecordLength);
            const FixupResult fixupResult = ApplyFixupAndValidate(recordScratch.data(), returnedRecordLength, bytesPerSector);
            if (fixupResult != FixupResult::Ok) {
                continue;
            }
            auto parsed = ParseMftAttributes(recordScratch.data(), returnedRecordLength);
            if (!parsed) {
                continue;
            }

            ffprotocol::MftRecordV2 record{};
            record.fixed.fileReferenceNumber = frn;
            record.fixed.parentFileReferenceNumber = parsed->parentFileReferenceNumber;
            record.fixed.creationTime = parsed->creationTime;
            record.fixed.lastModifiedTime = parsed->lastModifiedTime;
            record.fixed.lastAccessTime = parsed->lastAccessTime;
            record.fixed.sizeBytes = parsed->sizeBytes;
            // allocated-size-storage-accuracy D2: both cached $FILE_NAME
            // sizes flow from the same allowlisted parse.
            record.fixed.allocatedSizeBytes = parsed->allocatedSizeBytes;
            record.fixed.fileAttributes = parsed->fileAttributes;
            record.fixed.fileNameLengthChars = static_cast<uint16_t>(parsed->fileName.size());
            record.fileName = std::move(parsed->fileName);
            batch.push_back(std::move(record));
            if (batch.size() >= kBatchSize) {
                // A mid-buffer cursor stays at the buffer start. A crash
                // can replay a bounded prefix, which ingestion safely
                // upserts; advancing to the buffer's next cursor here
                // could skip records not processed yet.
                writeFailed = !sendBatch(batch, bufferStartFileReference);
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
        if (!bufferAbandoned && !bufferHadRecords && bufferNextFileReference <= nextFileReference) {
            // No records and no forward progress -- treat as end of the
            // enumeration rather than spinning on the same position.
            enumerationEnded = true;
            break;
        }
        if (!bufferAbandoned) {
            nextFileReference = bufferNextFileReference;
        }

        if (!writeFailed && bufferHadRecords) {
            writeFailed = !sendBatch(batch, nextFileReference);
            batch.clear();
        }
    }

    if (!batch.empty() && !writeFailed) {
        writeFailed = !sendBatch(batch, nextFileReference);
    }

    CloseHandle(volumeHandle);

    if (!writeFailed && !shouldStop.load()) {
        // PA-001: only an enumeration that reached the end of the MFT gets a
        // ScanComplete; an enumeration cut short by a non-EOF fault sends
        // the ScanFailed terminal (or ScanComplete only to pre-{2,2} peers,
        // which have no failure vocabulary and keep today's behavior).
        SendScanTerminal(pipe, writeMutex, volumeId, negotiatedVersion, !enumerationFailed);
    }
}

} // namespace ffindexsvc
