#pragma once

#include <map>
#include <string>

#include "ffprotocol/SnapshotFormat.h"

namespace ffui {

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
