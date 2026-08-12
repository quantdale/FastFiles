#pragma once

#include <map>
#include <string>
#include <string_view>

#include "ffprotocol/SnapshotFormat.h"

namespace ffui {

inline bool MatchesPendingSnapshotPath(std::wstring_view columnPath,
                                       std::wstring_view pendingPath) noexcept {
    return pendingPath.empty() || ffprotocol::WindowsPathEqual(columnPath, pendingPath);
}

// Snapshot paths originate from both Win32 enumeration and the indexed
// projection. Their spelling can differ even though Windows resolves them to
// the same directory. Keep exact lookup fast, then apply the bounded
// case-insensitive fallback for a path that came from another source.
inline const ffprotocol::SnapshotDirectory* FindSnapshotDirectory(
    const std::map<std::wstring, ffprotocol::SnapshotDirectory>& snapshot,
    const std::wstring& path) noexcept {
    const auto exact = snapshot.find(path);
    if (exact != snapshot.end()) return &exact->second;
    for (const auto& [candidate, directory] : snapshot) {
        if (ffprotocol::WindowsPathEqual(candidate, path)) return &directory;
    }
    return nullptr;
}

} // namespace ffui
