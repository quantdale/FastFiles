#pragma once

#include <deque>
#include <string>
#include <vector>
#include <windows.h>

namespace ffui {

// quick-access-home 2.1 / D3: pure, headless recent-folder history. The class
// operates on a caller-owned std::deque<std::wstring> so the logic is trivially
// unit-testable without any UI, filesystem, or persistence machinery. All
// ordering follows the deque's natural front = most-recent order.
class RecentFolderHistory {
public:
    // Bounds. kMaxRecentFoldersPersisted is the on-disk cap; kHomeRecentFoldersShown
    // is how many the Home surface displays.
    static constexpr size_t kMaxRecentFoldersPersisted = 50;
    static constexpr size_t kHomeRecentFoldersShown = 10;

    // Deduplicate-to-front (most recent first) then trim the tail to the
    // persisted cap. If an equivalent entry already exists, the FIRST-SEEN
    // spelling is preserved (the existing entry is moved to front rather than
    // replaced by the new spelling). See Equivalent() for the identity policy.
    static void Record(std::deque<std::wstring>& store, const std::wstring& path);
    // Empties the store.
    static void Clear(std::deque<std::wstring>& store);
    // Returns up to maxCount most-recent-first paths.
    static std::vector<std::wstring> RecentForHome(const std::deque<std::wstring>& store, size_t maxCount);

    // EXACT equality policy (D3): lexical normalization only. NEVER touches the
    // filesystem and NEVER resolves paths. Normalization trims trailing '\'
    // except when the remainder would be a drive root (e.g. "C:") or a share
    // root (e.g. "\\server\share"); comparison is case-insensitive via
    // CompareStringOrdinal(..., TRUE). Extended-length (\\?\) and UNC forms are
    // compared on the same lexical basis after normalization; no server/share
    // case resolution is attempted. Persisted entries keep the first-seen
    // spelling.
    static bool Equivalent(const std::wstring& a, const std::wstring& b);
};

} // namespace ffui
