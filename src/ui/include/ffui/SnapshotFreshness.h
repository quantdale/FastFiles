#pragma once

#include <cstdint>

namespace ffui {

// The browser presents one of these states for each path-owned Column.  The
// state is intentionally independent from whether the last-known-good
// directory happened to contain zero entries: Current + zero entries is a
// valid empty directory, while Unavailable is a failed load.
enum class SnapshotViewState : uint8_t {
    Loading,
    Current,
    StaleWithError,
    Unavailable,
};

enum class SnapshotFailureReason : uint8_t {
    None,
    AccessDenied,
    NoLongerAvailable,
    EnumerationFailed,
    PublishFailed,
    SnapshotLoadFailed,
    PathMissing,
    Disconnected,
};

// Small, deterministic freshness state machine used by the UI model.  A
// generation is accepted only if it is not older than either the last
// accepted generation or the newest announced attempt.  That prevents a
// late/out-of-order read from clearing a newer stale state.
struct SnapshotFreshness {
    SnapshotViewState state = SnapshotViewState::Loading;
    SnapshotFailureReason failure = SnapshotFailureReason::None;
    uint64_t requestedGeneration = 0;
    uint64_t lastSuccessfulGeneration = 0;

    bool HasLastKnownGood() const noexcept { return lastSuccessfulGeneration != 0; }

    void BeginRequest() noexcept {
        state = SnapshotViewState::Loading;
        failure = SnapshotFailureReason::None;
    }

    void AnnounceGeneration(uint64_t generation) noexcept {
        if (generation == 0 || generation < requestedGeneration) return;
        requestedGeneration = generation;
        if (generation > lastSuccessfulGeneration) {
            state = SnapshotViewState::Loading;
            failure = SnapshotFailureReason::None;
        }
    }

    bool AcceptGeneration(uint64_t generation) noexcept {
        if (generation == 0 || generation < lastSuccessfulGeneration ||
            generation < requestedGeneration) {
            return false;
        }
        requestedGeneration = generation;
        lastSuccessfulGeneration = generation;
        state = SnapshotViewState::Current;
        failure = SnapshotFailureReason::None;
        return true;
    }

    bool Fail(SnapshotFailureReason reason, uint64_t generation = 0) noexcept {
        if (generation != 0 && generation < requestedGeneration) return false;
        if (generation != 0) requestedGeneration = generation;
        failure = reason;
        state = HasLastKnownGood() ? SnapshotViewState::StaleWithError
                                   : SnapshotViewState::Unavailable;
        return true;
    }

    // A restarted engine owns a new generation sequence.  The old rows may
    // remain visible during the reload, but their old generation cannot block
    // the first valid generation from the new mapping.
    void ResetForNewSource() noexcept {
        state = SnapshotViewState::Loading;
        failure = SnapshotFailureReason::None;
        requestedGeneration = 0;
        lastSuccessfulGeneration = 0;
    }
};

} // namespace ffui
