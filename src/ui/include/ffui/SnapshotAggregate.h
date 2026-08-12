#pragma once

#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>

#include "ffprotocol/SnapshotFormat.h"

namespace ffui {

// A bounded, snapshot-only subtree result.  `complete` is false when the
// snapshot does not contain one of the requested directory's descendants (or
// when a malformed/cyclic path would otherwise make the walk unbounded).  The
// byte and item totals remain useful as lower bounds in that case, but callers
// must not present them as a complete volume total.
struct SnapshotAggregate {
    uint64_t itemCount = 0;
    uint64_t totalSizeBytes = 0;
    bool complete = false;
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
    const std::wstring& path, std::set<std::wstring>& activePaths) {
    if (!activePaths.insert(path).second) {
        return {0, 0, false};
    }

    const auto found = snapshot.find(path);
    if (found == snapshot.end() || found->second.status != ffprotocol::DirectoryEnumerationStatus::Success) {
        return {0, 0, false};
    }

    SnapshotAggregate result{0, 0, true};
    for (const auto& entry : found->second.entries) {
        if (!AddSnapshotCount(result.itemCount, 1)) result.complete = false;
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
        result.complete = result.complete && child.complete;
    }
    return result;
}

} // namespace detail

inline SnapshotAggregate AggregateSnapshotDirectory(
    const std::map<std::wstring, ffprotocol::SnapshotDirectory>& snapshot,
    const std::wstring& path) {
    std::set<std::wstring> activePaths;
    return detail::AggregateSnapshotDirectory(snapshot, path, activePaths);
}

} // namespace ffui
