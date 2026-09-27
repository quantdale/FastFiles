#include "VolumeScanner.h"

#include <winioctl.h>

#include <cstring>
#include <new>
#include <vector>

#include "ffprotocol/Diagnostics.h"
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
                    const std::vector<ffprotocol::MftRecordV1>& records, uint64_t nextFileReference,
                    std::atomic<bool>* transportFailed) {
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
                                  payload.data(), static_cast<uint32_t>(payload.size()), transportFailed);
}

// allocated-size-storage-accuracy D3/2.4: ScanRecordBatchV2 with
// MftRecordFixedV2 records (fixed layout + allocatedSizeBytes) for peers
// that negotiated protocol {2,1}+. Distinct message type + explicit
// recordFormatVersion so a legacy parser can never reinterpret the larger
// fixed records as the V1 layout.
bool SendScanBatchV2(HANDLE pipe, std::mutex& writeMutex, ffprotocol::VolumeId volumeId,
                     const std::vector<ffprotocol::MftRecordV2>& records, uint64_t nextFileReference,
                     std::atomic<bool>* transportFailed) {
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
                                  payload.data(), static_cast<uint32_t>(payload.size()), transportFailed);
}

bool SendScanComplete(HANDLE pipe, std::mutex& writeMutex, ffprotocol::VolumeId volumeId,
                      std::atomic<bool>* transportFailed) {
    ffprotocol::ScanCompletePayload payload{volumeId};
    std::lock_guard<std::mutex> lock(writeMutex);
    return WriteFrameWithDeadline(pipe, static_cast<uint16_t>(ffprotocol::MessageType::ScanComplete),
                                  &payload, sizeof(payload), transportFailed);
}

// legacy-protocol-correctness-and-raw-volume-cancellation-closure D3: every
// pre-enumeration failure path routes through the single terminal decision and
// maps its outcome onto the scan result. A peer that cannot parse ScanFailed
// receives no terminal -- FailureAbortedConnection tells the worker boundary
// to end the connection instead of emitting a false success.
ScanRunResult DeliverFailedScanTerminal(HANDLE pipe, std::mutex& writeMutex,
                                        ffprotocol::VolumeId volumeId,
                                        ffprotocol::ProtocolVersion negotiatedVersion,
                                        std::atomic<bool>* transportFailed) {
    const ScanTerminalOutcome terminal =
        SendScanTerminal(pipe, writeMutex, volumeId, negotiatedVersion, false, transportFailed);
    if (terminal == ScanTerminalOutcome::TransportFailed) {
        return ScanRunResult::TransportFailed;
    }
    if (terminal == ScanTerminalOutcome::FailureNotRepresentable) {
        return ScanRunResult::FailureAbortedConnection;
    }
    return ScanRunResult::FailedTerminalDelivered;
}

class ScanOutput {
public:
    ScanOutput(HANDLE pipe, std::mutex& writeMutex, ffprotocol::VolumeId volumeId,
               ffprotocol::ProtocolVersion negotiatedVersion, const std::atomic<bool>& shouldStop,
               std::atomic<bool>* transportFailed)
        : pipe_(pipe), writeMutex_(writeMutex), volumeId_(volumeId), negotiatedVersion_(negotiatedVersion),
          shouldStop_(shouldStop), transportFailed_(transportFailed) {}

    bool SendBatch(const std::vector<ffprotocol::MftRecordV2>& records, uint64_t nextFileReference) {
        bool sent = false;
        if (ffprotocol::ProtocolSupportsAllocatedSize(negotiatedVersion_)) {
            sent = SendScanBatchV2(pipe_, writeMutex_, volumeId_, records, nextFileReference, transportFailed_);
        } else {
            std::vector<ffprotocol::MftRecordV1> legacyRecords;
            legacyRecords.reserve(records.size());
            for (const auto& record : records) {
                legacyRecords.push_back(DowngradeMftRecordV2(record));
            }
            sent = SendScanBatch(pipe_, writeMutex_, volumeId_, legacyRecords, nextFileReference, transportFailed_);
        }
        if (!sent) {
            writeFailed_ = true;
            writeError_ = GetLastError();
        }
        return sent;
    }

    bool Failed() const noexcept { return writeFailed_; }

    ScanRunResult Finish(bool enumerationFailed) {
        if (writeFailed_) {
            if (!shouldStop_.load()) {
                RecordFailureDiagnostic(L"scan-batch-write-failed", writeError_);
            }
            return ScanRunResult::TransportFailed;
        }
        if (shouldStop_.load()) {
            return ScanRunResult::Cancelled;
        }

        // PA-001: only a complete enumeration receives ScanComplete (or the
        // capability-gated ScanFailed); a terminal write error is fatal to
        // this stream and never gets retried on the same connection.
        const ScanTerminalOutcome terminal = SendScanTerminal(
            pipe_, writeMutex_, volumeId_, negotiatedVersion_, !enumerationFailed, transportFailed_);
        if (terminal == ScanTerminalOutcome::TransportFailed) {
            RecordFailureDiagnostic(L"scan-terminal-write-failed", GetLastError());
            return ScanRunResult::TransportFailed;
        }
        if (terminal == ScanTerminalOutcome::FailureNotRepresentable) {
            // legacy-protocol-correctness-and-raw-volume-cancellation-closure
            // D3: the negotiated peer cannot parse ScanFailed, so the honest
            // outcome is no terminal at all plus connection teardown.
            RecordFailureDiagnostic(L"scan-failure-not-representable-to-peer", ERROR_SUCCESS);
            return ScanRunResult::FailureAbortedConnection;
        }
        return enumerationFailed ? ScanRunResult::FailedTerminalDelivered : ScanRunResult::Completed;
    }

private:
    void RecordFailureDiagnostic(const wchar_t* state, DWORD error) const noexcept {
        try {
            ffprotocol::DiagnosticEvent event{};
            event.category = ffprotocol::DiagnosticCategory::IndexingError;
            event.code = ffprotocol::DiagnosticCode::FFDIAG_SCAN_FAILED;
            event.volumeId = std::to_wstring(volumeId_.value);
            event.state = state;
            event.errorCode = error;
            ffprotocol::AppendDiagnostic(event);
        } catch (...) { }
    }

    HANDLE pipe_;
    std::mutex& writeMutex_;
    ffprotocol::VolumeId volumeId_;
    ffprotocol::ProtocolVersion negotiatedVersion_;
    const std::atomic<bool>& shouldStop_;
    std::atomic<bool>* transportFailed_;
    bool writeFailed_ = false;
    DWORD writeError_ = ERROR_SUCCESS;
};

class ScopedVolumeHandle {
public:
    explicit ScopedVolumeHandle(HANDLE handle) noexcept : handle_(handle) {}
    ~ScopedVolumeHandle() { reset(); }

    ScopedVolumeHandle(const ScopedVolumeHandle&) = delete;
    ScopedVolumeHandle& operator=(const ScopedVolumeHandle&) = delete;

    HANDLE get() const noexcept { return handle_; }
    bool valid() const noexcept { return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr; }

    void reset() noexcept {
        if (valid()) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
    }

private:
    HANDLE handle_;
};

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

// legacy-protocol-correctness-and-raw-volume-cancellation-closure D3: the
// single terminal sender. A failed scan must never masquerade as a
// completion -- the engine treats ScanComplete as the authority to mark the
// volume indexed and to finalize reconciliation passes, which would delete
// rows a failed scan simply never reached. A peer older than {2,2} cannot
// parse ScanFailed, so a failed scan has no representable terminal for it:
// the caller writes nothing and ends the connection, and the engine's
// existing disconnect path refuses authority and recovers on a fresh
// connection (same shape as the silent-cancellation path).
// hardening-campaign Phase 7 H2: declared in VolumeScanner.h so the
// ServiceConnection exception boundary can emit the failure terminal too.
ScanTerminalOutcome SendScanTerminal(HANDLE pipe, std::mutex& writeMutex, ffprotocol::VolumeId volumeId,
                                     ffprotocol::ProtocolVersion negotiatedVersion, bool success,
                                     std::atomic<bool>* transportFailed) {
    if (!success) {
        if (!ffprotocol::ProtocolSupportsScanFailure(negotiatedVersion)) {
            return ScanTerminalOutcome::FailureNotRepresentable;
        }
        ffprotocol::ScanFailedPayload payload{volumeId};
        std::lock_guard<std::mutex> lock(writeMutex);
        if (!WriteFrameWithDeadline(pipe, static_cast<uint16_t>(ffprotocol::MessageType::ScanFailed),
                                    &payload, sizeof(payload), transportFailed)) {
            return ScanTerminalOutcome::TransportFailed;
        }
        return ScanTerminalOutcome::FailureTerminalDelivered;
    }
    if (!SendScanComplete(pipe, writeMutex, volumeId, transportFailed)) {
        return ScanTerminalOutcome::TransportFailed;
    }
    return ScanTerminalOutcome::SuccessTerminalDelivered;
}

#if defined(FF_INDEXSVC_TESTING)
ScanRunResult RunSyntheticVolumeScanForTesting(
    HANDLE pipe, std::mutex& writeMutex, ffprotocol::VolumeId volumeId,
    ffprotocol::ProtocolVersion negotiatedVersion, const SyntheticScanSourceForTesting& source,
    const std::atomic<bool>& shouldStop, bool enumerationFailed,
    const BeforeScanTerminalForTesting& beforeTerminal, std::atomic<bool>* transportFailed) {
    ScanOutput output(pipe, writeMutex, volumeId, negotiatedVersion, shouldStop, transportFailed);
    if (!shouldStop.load()) {
        std::vector<ffprotocol::MftRecordV2> records;
        if (source(records) && !shouldStop.load()) {
            output.SendBatch(records, 0);
        }
    }
    if (!output.Failed() && !shouldStop.load() && beforeTerminal) {
        beforeTerminal();
    }
    return output.Finish(enumerationFailed);
}

void ThrowAfterVolumeHandleAcquiredForTesting(HANDLE handle) {
    ScopedVolumeHandle volumeHandle(handle);
    throw std::bad_alloc{};
}
#endif

ScanRunResult RunVolumeScan(
    HANDLE pipe, std::mutex& writeMutex, ffprotocol::VolumeId volumeId, wchar_t driveLetter,
    const std::vector<uint8_t>& resumeCursor, bool lowPriority,
    ffprotocol::ProtocolVersion negotiatedVersion, const std::atomic<bool>& shouldStop,
    std::atomic<bool>* transportFailed) {
    if (shouldStop.load()) {
        return ScanRunResult::Cancelled;
    }
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
        return DeliverFailedScanTerminal(pipe, writeMutex, volumeId, negotiatedVersion, transportFailed);
    }
    // Capture the actual service token immediately before the raw-volume
    // open. The capture is observational and must not alter the call path.
    const TokenPrivilegeState tokenState = CaptureTokenPrivilegeState();
    ScopedVolumeHandle volumeHandle(CreateFileW(
        volumePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    const bool volumeOpened = volumeHandle.valid();
    const DWORD volumeOpenError = volumeOpened ? ERROR_SUCCESS : GetLastError();
    LogRawVolumeOpenDiagnostic(volumePath, tokenState,
                               ClassifyRawVolumeOpen(tokenState, volumeOpened, volumeOpenError), volumeOpenError);
    if (!volumeOpened) {
        return DeliverFailedScanTerminal(pipe, writeMutex, volumeId, negotiatedVersion, transportFailed);
    }

    // Authoritative record/sector geometry comes from the volume itself,
    // never a hardcoded constant.
    NTFS_VOLUME_DATA_BUFFER volumeData{};
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(volumeHandle.get(), FSCTL_GET_NTFS_VOLUME_DATA, nullptr, 0, &volumeData, sizeof(volumeData),
                           &bytesReturned, nullptr)) {
        volumeHandle.reset();
        return DeliverFailedScanTerminal(pipe, writeMutex, volumeId, negotiatedVersion, transportFailed);
    }

    const uint32_t recordSize = static_cast<uint32_t>(volumeData.BytesPerFileRecordSegment);
    const uint32_t bytesPerSector = static_cast<uint32_t>(volumeData.BytesPerSector);
    if (recordSize == 0 || bytesPerSector == 0) {
        volumeHandle.reset();
        return DeliverFailedScanTerminal(pipe, writeMutex, volumeId, negotiatedVersion, transportFailed);
    }

    const size_t outputBufferSize = sizeof(NTFS_FILE_RECORD_OUTPUT_BUFFER) - 1 + recordSize;
    std::vector<uint8_t> outputBuffer(outputBufferSize);
    std::vector<uint8_t> recordScratch(recordSize);
    std::vector<uint8_t> enumBuffer(kEnumBufferSize);

    std::vector<ffprotocol::MftRecordV2> batch;
    batch.reserve(kBatchSize);
    // allocated-size-storage-accuracy D3/2.4: one shared output path selects
    // the V2 record format for {2,1}+ peers and downgrades older peers to the
    // byte-compatible V1 format. The same output state also owns the
    // transport-fatal decision exercised by the portable synthetic fixture.
    ScanOutput scanOutput(pipe, writeMutex, volumeId, negotiatedVersion, shouldStop, transportFailed);

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
        if (!DeviceIoControl(volumeHandle.get(), FSCTL_ENUM_USN_DATA, &enumData, sizeof(enumData), enumBuffer.data(),
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
                volumeHandle.get(), FSCTL_GET_NTFS_FILE_RECORD, &input, sizeof(input), outputBuffer.data(),
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
                writeFailed = !scanOutput.SendBatch(batch, bufferStartFileReference);
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
            writeFailed = !scanOutput.SendBatch(batch, nextFileReference);
            batch.clear();
        }
    }

    if (!batch.empty() && !writeFailed) {
        writeFailed = !scanOutput.SendBatch(batch, nextFileReference);
    }

    volumeHandle.reset();

    // A failed batch may have left a partial frame on the byte stream. The
    // output state therefore returns TransportFailed without appending a
    // terminal; ServiceConnection closes this connection before reconnect.
    return scanOutput.Finish(enumerationFailed);
}

} // namespace ffindexsvc
