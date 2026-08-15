#pragma once

#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>

#include "ffprotocol/SnapshotFormat.h"
#include "ffui/SnapshotPathLookup.h"

namespace ffui {

// A bounded, snapshot-only subtree result.  `complete` is false when the
// snapshot does not contain one of the requested directory's descendants (or
// when a malformed/cyclic path would otherwise make the walk unbounded).  The
// byte and item totals remain useful as lower bounds in that case, but callers
// must not present them as a complete volume total.
// allocated-size-storage-accuracy D7/D8: the allocated-size subtotal covers
// only entries whose size-on-disk is known, with the completeness pair
// (known/total counts); `allocatedComplete` is true only when every
// descendant is known. A partial subtotal is never presented as exact.
struct SnapshotAggregate {
    uint64_t itemCount = 0;
    uint64_t totalSizeBytes = 0;
    uint64_t totalAllocatedSizeBytes = 0;
    uint64_t allocatedBytesKnownCount = 0;
    uint64_t allocatedBytesTotalCount = 0;
    bool complete = false;
    bool allocatedComplete = false;
};

inline std::wstring JoinSnapshotPath(const std::wstring& base, const std::wstring& name) {
    if (base.empty()) return name;
    if (base.back() == L'\\' || base.back() == L'/') return base + name;
    return base + L'\\' + name;
}

namespace detail {

inline bool AddSnapshotCount(uint64_t& destination, uint64_t value) {
    if (std::numeric_limits<uint64_t>::max() - destination < value) {
        destination = std::numeric_limits<uint64_t>::max();
        return false;
    }
    destination += value;
    return true;
}

inline SnapshotAggregate AggregateSnapshotDirectory(
    const std::map<std::wstring, ffprotocol::SnapshotDirectory>& snapshot,
    const std::wstring& path,
    std::set<std::wstring, ffprotocol::CaseInsensitivePathLess>& activePaths) {
    if (!activePaths.insert(path).second) {
        return {0, 0, 0, 0, 0, false, false};
    }

    const auto* found = FindSnapshotDirectory(snapshot, path);
    if (found == nullptr || found->status != ffprotocol::DirectoryEnumerationStatus::Success) {
        return {0, 0, 0, 0, 0, false, false};
    }

    SnapshotAggregate result{0, 0, 0, 0, 0, true, true};
    for (const auto& entry : found->entries) {
        if (!AddSnapshotCount(result.itemCount, 1)) result.complete = false;
        // allocated-size-storage-accuracy D8: every descendant contributes
        // to the allocated total count; only known ones to the subtotal.
        ++result.allocatedBytesTotalCount;
        if (entry.allocatedSizeKnown) {
            if (!AddSnapshotCount(result.totalAllocatedSizeBytes, entry.allocatedSizeBytes)) {
                result.allocatedComplete = false;
            }
            ++result.allocatedBytesKnownCount;
        }
        if (!entry.isDirectory) {
            if (!AddSnapshotCount(result.totalSizeBytes, entry.sizeBytes)) result.complete = false;
            continue;
        }

        const SnapshotAggregate child = AggregateSnapshotDirectory(
            snapshot, JoinSnapshotPath(path, entry.name), activePaths);
        if (!AddSnapshotCount(result.itemCount, child.itemCount) ||
            !AddSnapshotCount(result.totalSizeBytes, child.totalSizeBytes)) {
            result.complete = false;
        }
        // allocated-size-storage-accuracy D7: child allocated totals merge
        // with the same saturation/completeness discipline.
        if (!AddSnapshotCount(result.totalAllocatedSizeBytes, child.totalAllocatedSizeBytes)) {
            result.allocatedComplete = false;
        }
        result.allocatedBytesKnownCount += child.allocatedBytesKnownCount;
        result.allocatedBytesTotalCount += child.allocatedBytesTotalCount;
        result.complete = result.complete && child.complete;
        result.allocatedComplete = result.allocatedComplete && child.allocatedComplete;
    }
    // Complete only when every descendant's allocated size is known AND no
    // sum saturated -- a partial or wrapped subtotal is never exact.
    result.allocatedComplete = result.allocatedComplete &&
        result.allocatedBytesKnownCount == result.allocatedBytesTotalCount;
    return result;
}

} // namespace detail

inline SnapshotAggregate AggregateSnapshotDirectory(
    const std::map<std::wstring, ffprotocol::SnapshotDirectory>& snapshot,
    const std::wstring& path) {
    std::set<std::wstring, ffprotocol::CaseInsensitivePathLess> activePaths;
    return detail::AggregateSnapshotDirectory(snapshot, path, activePaths);
}

} // namespace ffui
