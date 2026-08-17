#pragma once
// semantic-uia-core-surfaces 1.2 + semantic-treemap-uia 13.x: deterministic
// UIA runtime-ID namespaces and stable identity hashing.
//
// Runtime IDs are arrays of ints. The leading discriminant isolates the
// surfaces (Column View / Home / Search / Treemap); pane, section, and
// identity hashes make IDs stable across refreshes and distinct across
// panes/columns/items/tiles. An element whose identity no longer exists
// after navigation / Home refresh / result replacement / treemap scope
// change fails lookup cleanly -- a stale element can never retarget the new
// occupant of the same row index (design D5).
//
// The hash helpers live in ffui/UiaIdentity.h (pure, shared with the
// treemap semantic seam); this header adds only the runtime-ID builders.

#include <cstdint>
#include <vector>

#include "ffui/UiaIdentity.h"

namespace ffui {

// Leading discriminants.
inline constexpr int kUiaNsColumnView = 0;
inline constexpr int kUiaNsHome = 1;
inline constexpr int kUiaNsSearch = 2;
inline constexpr int kUiaNsTreemap = 3;

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

// Treemap group element (scoped: the group identity changes with the scope,
// so a client holding the old group after navigation sees it fail cleanly).
inline std::vector<int> UiaTreemapGroupRuntimeId(uint64_t scopeHash) {
    return {kUiaNsTreemap, 0, UiaHashLow(scopeHash), UiaHashHigh(scopeHash)};
}

// One treemap tile within a scope: {3, 1, scopeLo, scopeHi, idLo, idHi}.
inline std::vector<int> UiaTreemapTileRuntimeId(uint64_t scopeHash, uint64_t idHash) {
    return {kUiaNsTreemap, 1, UiaHashLow(scopeHash), UiaHashHigh(scopeHash),
            UiaHashLow(idHash), UiaHashHigh(idHash)};
}

// Combine cache-key components into one 64-bit key.
inline uint64_t UiaCombineKey(uint64_t a, uint64_t b) {
    return UiaHashMix(UiaHashMix(0x9E3779B97F4A7C15ull, a), b);
}

inline uint64_t UiaCombineKey(uint64_t a, uint64_t b, uint64_t c) {
    return UiaHashMix(UiaCombineKey(a, b), c);
}

} // namespace ffui
