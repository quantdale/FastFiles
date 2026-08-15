#pragma once
// semantic-uia-core-surfaces 1.2: deterministic UIA runtime-ID namespaces and
// stable identity hashing.
//
// Runtime IDs are arrays of ints. The leading discriminant isolates the
// surfaces (Column View / Home / Search); pane, section, and identity hashes
// make IDs stable across refreshes and distinct across panes/columns/items.
// An element whose identity no longer exists after navigation / Home refresh
// / result replacement fails lookup cleanly -- a stale element can never
// retarget the new occupant of the same row index (design D5).

#include <cstdint>
#include <string>
#include <vector>

namespace ffui {

// Leading discriminants.
inline constexpr int kUiaNsColumnView = 0;
inline constexpr int kUiaNsHome = 1;
inline constexpr int kUiaNsSearch = 2;

// FNV-1a 64-bit over a wide string (ordinal, case-sensitive). Identity is
// used for equality during a provider lifetime; the underlying model is
// already normalized (column paths come from the engine, names from the
// directory listing), so case folding is not applied here.
inline uint64_t UiaHashWide(const wchar_t* text, size_t length) {
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < length; ++i) {
        hash ^= static_cast<unsigned char>(text[i] & 0xFF);
        hash *= 1099511628211ull;
        hash ^= static_cast<unsigned char>((text[i] >> 8) & 0xFF);
        hash *= 1099511628211ull;
    }
    return hash;
}

inline uint64_t UiaHashWide(const std::wstring& text) {
    return UiaHashWide(text.data(), text.size());
}

// Mix a numeric component into a running hash.
inline uint64_t UiaHashMix(uint64_t hash, uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ull;
    return hash;
}

// Split a 64-bit hash into two ints for a UIA runtime-ID array.
inline int UiaHashLow(uint64_t hash) { return static_cast<int>(hash & 0xFFFFFFFFull); }
inline int UiaHashHigh(uint64_t hash) { return static_cast<int>((hash >> 32) & 0xFFFFFFFFull); }

// Stable identity of a column item: indexed FRN identity when present
// (volumeRowId + fileIdLow/High nonzero), else the case-insensitive full
// path. Mirrors the selection-identity policy of the real model.
inline uint64_t UiaItemIdentityHash(int64_t volumeRowId, uint64_t fileIdLow,
                                    uint64_t fileIdHigh, const std::wstring& fullPath) {
    if (volumeRowId != 0 && (fileIdLow != 0 || fileIdHigh != 0)) {
        uint64_t hash = UiaHashWide(L"frn");
        hash = UiaHashMix(hash, static_cast<uint64_t>(volumeRowId));
        hash = UiaHashMix(hash, fileIdLow);
        hash = UiaHashMix(hash, fileIdHigh);
        return hash;
    }
    return UiaHashWide(fullPath);
}

// Stable identity of a search result: indexed (volumeId + id) when present,
// else folder + name.
inline uint64_t UiaSearchIdentityHash(int64_t volumeId, uint64_t id,
                                      const std::wstring& folder, const std::wstring& name) {
    if (volumeId != 0 && id != 0) {
        uint64_t hash = UiaHashWide(L"sid");
        hash = UiaHashMix(hash, static_cast<uint64_t>(volumeId));
        hash = UiaHashMix(hash, id);
        return hash;
    }
    uint64_t hash = UiaHashWide(folder);
    hash = UiaHashMix(hash, UiaHashWide(name));
    return hash;
}

// Builders. Each returns the deterministic runtime-ID array for the element.

// Root Window element of the navigation fragment.
inline std::vector<int> UiaRootRuntimeId() { return {kUiaNsColumnView, 0, 0, 0}; }

// Pane.N.
inline std::vector<int> UiaPaneRuntimeId(int pane) { return {kUiaNsColumnView, 1, pane, 0}; }

// Column (pane N, path identity).
inline std::vector<int> UiaColumnRuntimeId(int pane, uint64_t pathHash) {
    return {kUiaNsColumnView, 2, pane, UiaHashLow(pathHash), UiaHashHigh(pathHash)};
}

// Column item (pane N, column path identity + item identity).
inline std::vector<int> UiaItemRuntimeId(int pane, uint64_t pathHash, uint64_t idHash) {
    return {kUiaNsColumnView, 3, pane, UiaHashLow(pathHash), UiaHashHigh(pathHash),
            UiaHashLow(idHash), UiaHashHigh(idHash)};
}

// Home surface elements: {1, pane, kind, section, index, valueHashLo, valueHashHi}.
inline std::vector<int> UiaHomeRootRuntimeId(int pane) {
    return {kUiaNsHome, pane, 0, 0, 0, 0, 0};
}

inline std::vector<int> UiaHomeSectionRuntimeId(int pane, int sectionKind, int sectionOrdinal) {
    return {kUiaNsHome, pane, 1, sectionKind, sectionOrdinal, 0, 0};
}

inline std::vector<int> UiaHomeClearRuntimeId(int pane, int sectionKind) {
    return {kUiaNsHome, pane, 2, sectionKind, 0, 0, 0};
}

inline std::vector<int> UiaHomeItemRuntimeId(int pane, int sectionKind, int indexInSection,
                                             uint64_t valueHash) {
    return {kUiaNsHome, pane, 3, sectionKind, indexInSection,
            UiaHashLow(valueHash), UiaHashHigh(valueHash)};
}

// Search surface elements.
inline std::vector<int> UiaSearchListRuntimeId() { return {kUiaNsSearch, 0, 0, 0}; }

inline std::vector<int> UiaSearchResultRuntimeId(uint64_t idHash) {
    return {kUiaNsSearch, 1, UiaHashLow(idHash), UiaHashHigh(idHash)};
}

// Column View group element (the single child of the root).
inline std::vector<int> UiaGroupRuntimeId() { return {kUiaNsColumnView, 4, 0, 0}; }

// Column status text element. Distinct from the column's own runtime ID so
// the UIA runtime never conflates the status with its owning column.
inline std::vector<int> UiaStatusRuntimeId(int pane, uint64_t pathHash) {
    return {kUiaNsColumnView, 5, pane, UiaHashLow(pathHash), UiaHashHigh(pathHash)};
}

// Combine cache-key components into one 64-bit key.
inline uint64_t UiaCombineKey(uint64_t a, uint64_t b) {
    return UiaHashMix(UiaHashMix(0x9E3779B97F4A7C15ull, a), b);
}

inline uint64_t UiaCombineKey(uint64_t a, uint64_t b, uint64_t c) {
    return UiaHashMix(UiaCombineKey(a, b), c);
}

} // namespace ffui
