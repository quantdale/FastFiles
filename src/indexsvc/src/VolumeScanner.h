#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "ffprotocol/Records.h"

namespace ffindexsvc {

// Opaque scan-progress cursor (task 4.5, D8): currently just the
// StartFileReferenceNumber to resume FSCTL_ENUM_USN_DATA from, serialized
// as raw little-endian bytes so it travels as the wire protocol's opaque
// cursor field without the engine needing to interpret its contents.
std::vector<uint8_t> SerializeScanCursor(uint64_t resumeFileReferenceNumber);
std::optional<uint64_t> DeserializeScanCursor(const std::vector<uint8_t>& cursor);

// Real MFT volume scanning (tasks.md section 4): drives full-volume
// traversal via FSCTL_ENUM_USN_DATA (which Microsoft documents as
// enumerating files in MFT order -- this is the "raw MFT parsing" data
// source's traversal mechanism), fetching and parsing each visited file's
// actual raw MFT record via FetchAndParseMftRecord for the allowlisted
// fields (crucially including size, which USN_RECORD itself never
// carries). A single malformed/vanished record is skipped, not fatal
// (task 4.3).
class VolumeScanner {
public:
    // Returns false and stops iterating a batch's records to the caller
    // (e.g. because streaming that batch over the pipe failed) --
    // `resumeCursorAfterBatch` is always safe to persist as of the point
    // this callback is invoked, regardless of its return value.
    using BatchCallback = std::function<bool(const std::vector<ffprotocol::MftRecordV1>& batch, std::vector<uint8_t> resumeCursorAfterBatch)>;

    // Runs until the scan completes (visits every record once), RequestStop()
    // is observed, or `onBatch` returns false. Returns false only on an
    // unrecoverable error opening the volume itself; a clean stop or
    // completion both return true (the caller distinguishes them via
    // RequestStop()'s own state / the "scan complete" signal it sends
    // separately).
    bool Run(wchar_t driveLetter, uint64_t startFileReferenceNumber, const BatchCallback& onBatch);

    // Cooperative cancellation for an explicit StopVolumeScan (task: no
    // indefinite blocking, responsive to Stop).
    void RequestStop() { stopRequested_.store(true, std::memory_order_relaxed); }
    bool WasStopped() const noexcept { return stopRequested_.load(std::memory_order_relaxed); }

private:
    std::atomic<bool> stopRequested_{false};
};

} // namespace ffindexsvc
