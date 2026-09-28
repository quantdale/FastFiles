#pragma once

// scan-generation-integrity-and-residual-certification-closure 4.2/4.3: the
// scan-start barrier decision, isolated from the service connection loop so
// its success and failure branches are directly executable in a focused test.
// The barrier is written only after the superseded worker has been joined and
// before the replacement worker is registered; a failed bounded write must
// tear the connection down instead of starting an unacknowledged worker.

namespace ffindexsvc {

enum class ScanAttemptBarrierDecision {
    // The peer either did not negotiate the barrier or accepted it: the caller
    // may register and start the replacement worker.
    Proceed,
    // The negotiated barrier write failed. The caller must discard the
    // connection; the replacement worker must not be started.
    TearDown,
};

// Invokes `writeBarrier` only when the peer negotiated the barrier, and maps
// its bounded-write result to the decision the connection loop acts on.
template <typename WriteBarrierFn>
ScanAttemptBarrierDecision ApplyScanAttemptBarrier(bool supportsBarrier, WriteBarrierFn&& writeBarrier) {
    if (!supportsBarrier) {
        // A pre-{2,3} peer never receives and never depends on the marker.
        return ScanAttemptBarrierDecision::Proceed;
    }
    return writeBarrier() ? ScanAttemptBarrierDecision::Proceed : ScanAttemptBarrierDecision::TearDown;
}

} // namespace ffindexsvc
