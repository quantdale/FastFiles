#pragma once

#include <cstdint>

namespace ffui {

// The identity used by the engine's folder aggregate query.  The snapshot
// generation is deliberately not part of this key: a new snapshot invalidates
// the request state explicitly so a changed folder is queried again.
struct FolderAggregateKey {
    int64_t volumeRowId = 0;
    uint64_t fileIdLow = 0;
    uint64_t fileIdHigh = 0;

    bool operator==(const FolderAggregateKey& other) const noexcept {
        return volumeRowId == other.volumeRowId &&
               fileIdLow == other.fileIdLow &&
               fileIdHigh == other.fileIdHigh;
    }
};

// Small UI-thread-owned state seam for Details folder aggregate requests.  A
// reset makes both a re-query necessary and any older posted response stale.
struct FolderAggregateRequestState {
    FolderAggregateKey key{};
    uint64_t requestId = 0;
    bool valid = false;

    bool NeedsRequest(const FolderAggregateKey& candidate) const noexcept {
        return !valid || !(key == candidate);
    }

    void Begin(const FolderAggregateKey& candidate) noexcept {
        key = candidate;
        requestId = 0;
        valid = true;
    }

    void SetRequestId(uint64_t id) noexcept { requestId = id; }

    void Invalidate() noexcept {
        key = {};
        requestId = 0;
        valid = false;
    }

    bool Accepts(uint64_t responseId) const noexcept {
        return valid && requestId != 0 && responseId == requestId;
    }
};

} // namespace ffui
