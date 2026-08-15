#include "RecentFolders.h"

#include <cwctype>

namespace ffui {
namespace {

// A drive root is exactly two characters: an ASCII letter followed by ':'.
bool IsDriveRoot(const std::wstring& value) {
    return value.size() == 2 && std::iswalpha(static_cast<wint_t>(value[0])) != 0 && value[1] == L':';
}

// Trims trailing '\' but preserves the trailing separator that belongs to a
// drive root (e.g. "C:" would become the invalid "C:" if trimmed, so "C:\"
// keeps its slash). Share roots ("\\server\share") do NOT keep a trailing
// slash -- the canonical form has none -- so their trailing separators are
// trimmed like any other path. See quick-access-home D3 path-identity policy.
std::wstring Normalize(const std::wstring& raw) {
    std::wstring result = raw;
    while (!result.empty() && result.back() == L'\\') {
        const std::wstring without = result.substr(0, result.size() - 1);
        if (IsDriveRoot(without)) break;
        result.pop_back();
    }
    return result;
}

} // namespace

namespace {
void Trim(std::deque<std::wstring>& store) {
    while (store.size() > RecentFolderHistory::kMaxRecentFoldersPersisted) store.pop_back();
}
}

void RecentFolderHistory::Record(std::deque<std::wstring>& store, const std::wstring& path) {
    for (auto it = store.begin(); it != store.end(); ++it) {
        if (Equivalent(*it, path)) {
            // Preserve the first-seen spelling: move the existing entry to
            // front rather than replacing it with the new spelling.
            const std::wstring existing = *it;
            store.erase(it);
            store.push_front(existing);
            Trim(store);
            return;
        }
    }
    store.push_front(path);
    Trim(store);
}

void RecentFolderHistory::Clear(std::deque<std::wstring>& store) {
    store.clear();
}

std::vector<std::wstring> RecentFolderHistory::RecentForHome(const std::deque<std::wstring>& store, size_t maxCount) {
    const size_t count = (std::min)(store.size(), maxCount);
    return std::vector<std::wstring>(store.begin(), store.begin() + static_cast<std::ptrdiff_t>(count));
}

bool RecentFolderHistory::Equivalent(const std::wstring& a, const std::wstring& b) {
    const std::wstring na = Normalize(a);
    const std::wstring nb = Normalize(b);
    return CompareStringOrdinal(na.c_str(), -1, nb.c_str(), -1, TRUE) == CSTR_EQUAL;
}

} // namespace ffui
