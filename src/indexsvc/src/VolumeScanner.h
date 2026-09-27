#pragma once
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

#include "ffprotocol/Commands.h"
#include "ffprotocol/Records.h"

#if defined(FF_INDEXSVC_TESTING)
#include <functional>
#endif

namespace ffindexsvc {

// index-storage-and-scanning tasks.md 4.1/4.4/4.5: implements the real
// raw-volume MFT enumeration for StartVolumeScan, streaming
// ScanRecordBatch frames (and a final ScanComplete) over `pipe`.
//
// Enumeration is driven by FSCTL_ENUM_USN_DATA (64 KB buffers), which
// returns only in-use files, in MFT order -- one DeviceIoControl per
// ~hundreds of records instead of one FSCTL_GET_NTFS_FILE_RECORD per MFT
// slot, most of which would fail on unused slots. The full MFT record for
// each enumerated FileReferenceNumber is still fetched individually via
// FSCTL_GET_NTFS_FILE_RECORD (which the filesystem driver services by
// locating and reading the correct on-disk MFT record -- this process
// never hand-walks the $MFT's own non-resident data runs), then parsed
// with MftParser.h's allowlisted, skip-and-continue-on-malformed-record
// logic (tasks.md 4.2/4.3). The record index still doubles as the resume
// cursor (tasks.md 4.5/D8): resumeCursor is the 8-byte little-endian
// opaque FSCTL_ENUM_USN_DATA file-reference cursor, empty meaning "start
// from file reference 0". It is intentionally not converted to a byte
// offset or record index; Windows returns the exact value required by the
// next call as the first 8 bytes of each enumeration buffer.
//
// Writes to `pipe` are serialized via `writeMutex`, which the caller
// shares with whatever else writes to the same connection (Heartbeat
// acks, etc.) -- named-pipe writes from multiple threads on one handle
// are not implicitly serialized by the OS, so frames could otherwise
// interleave. Polls `shouldStop` between buffers/records/batches for
// prompt cancellation (StopVolumeScan or connection teardown) rather than
// only checking once per batch.
// allocated-size-storage-accuracy D3/2.4: `negotiatedVersion` selects the
// wire record layout. Peers that negotiated protocol {2,1}+
// (ProtocolSupportsAllocatedSize) receive ScanRecordBatchV2 frames with
// MftRecordFixedV2 records carrying allocated size; all other peers receive
// the exact legacy ScanRecordBatch/MftRecordFixedV1 bytes.
enum class ScanRunResult {
    Completed,
    FailedTerminalDelivered,
    Cancelled,
    TransportFailed,
    // legacy-protocol-correctness-and-raw-volume-cancellation-closure D3: the
    // scan did not complete and the negotiated peer cannot parse ScanFailed.
    // No terminal was written -- sending ScanComplete would be a false
    // success -- so the attempt ends by tearing its connection down; the
    // engine's disconnect path already treats that as non-authoritative and
    // recovers on a fresh connection.
    FailureAbortedConnection,
};

// True when the scan outcome must end its owning connection: a poisoned
// frame stream (TransportFailed) or an unreportable failure that cannot be
// represented as a terminal without lying (FailureAbortedConnection).
constexpr bool ScanRunResultRequiresConnectionAbort(ScanRunResult outcome) noexcept {
    return outcome == ScanRunResult::TransportFailed ||
           outcome == ScanRunResult::FailureAbortedConnection;
}

// The single terminal decision's result. SuccessTerminalDelivered and
// FailureTerminalDelivered mean a framed terminal is on the wire;
// FailureNotRepresentable means the caller must abort without writing
// anything; TransportFailed means the stream is already poisoned.
enum class ScanTerminalOutcome {
    SuccessTerminalDelivered,
    FailureTerminalDelivered,
    FailureNotRepresentable,
    TransportFailed,
};

ScanRunResult RunVolumeScan(
    HANDLE pipe,
    std::mutex& writeMutex,
    ffprotocol::VolumeId volumeId,
    wchar_t driveLetter,
    const std::vector<uint8_t>& resumeCursor,
    bool lowPriority,
    ffprotocol::ProtocolVersion negotiatedVersion,
    const std::atomic<bool>& shouldStop,
    std::atomic<bool>* transportFailed = nullptr);

// index-durability-false-success-closure D1/2.1, extended by
// legacy-protocol-correctness-and-raw-volume-cancellation-closure D3: the
// single terminal sender. A failed scan must never masquerade as a
// completion -- the engine treats ScanComplete as the authority to mark the
// volume indexed and to finalize reconciliation passes, which would delete
// rows a failed scan simply never reached. Peers older than {2,2} cannot
// parse ScanFailed, so for those a failed scan is not representable at all:
// no terminal is written and the caller ends the connection (the cancelled
// path already proves the engine refuses authority on disconnect). A failed
// frame write is deliberately NOT followed by a terminal: the byte stream
// may already contain a partial frame, so the service reports TransportFailed
// and the connection is recreated instead.
ScanTerminalOutcome SendScanTerminal(HANDLE pipe,
                                     std::mutex& writeMutex,
                                     ffprotocol::VolumeId volumeId,
                                     ffprotocol::ProtocolVersion negotiatedVersion,
                                     bool success,
                                     std::atomic<bool>* transportFailed = nullptr);

#if defined(FF_INDEXSVC_TESTING)
// The focused scanner fixture feeds parsed synthetic records through the
// same batch serializer, writer, failure state, and terminal decision used
// after production MFT parsing. Neither this seam nor its source callback is
// present in the FastFilesIndexSvc target.
using SyntheticScanSourceForTesting =
    std::function<bool(std::vector<ffprotocol::MftRecordV2>&)>;
using BeforeScanTerminalForTesting = std::function<void()>;

ScanRunResult RunSyntheticVolumeScanForTesting(
    HANDLE pipe,
    std::mutex& writeMutex,
    ffprotocol::VolumeId volumeId,
    ffprotocol::ProtocolVersion negotiatedVersion,
    const SyntheticScanSourceForTesting& source,
    const std::atomic<bool>& shouldStop,
    bool enumerationFailed = false,
    const BeforeScanTerminalForTesting& beforeTerminal = {},
    std::atomic<bool>* transportFailed = nullptr);

// Throws after taking ownership to verify that the production handle guard
// closes an acquired volume handle when later scan work raises an exception.
void ThrowAfterVolumeHandleAcquiredForTesting(HANDLE handle);
#endif

} // namespace ffindexsvc
