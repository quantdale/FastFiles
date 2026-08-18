#pragma once
// indexed-search-allocated-size D7: the single source of truth for the Size on
// Disk wording, shared by the owner-drawn Search row and the semantic UIA
// FullDescription so the two presentations cannot drift in meaning.
//
// Pure header (no windows.h / COM): directly compilable by headless tests that
// only need the on-disk size semantics, following the established seam pattern
// of ffui::TreemapSemantics.

#include <cstdint>
#include <string>

namespace ffui {

// A pure byte formatter kept as a function pointer so each consumer keeps its
// established byte wording -- the visible row uses ffui::FormatSize, the UIA
// FullDescription uses UiaSearchSizeText -- while sharing the Size-on-Disk
// decision below. The formatter is never invoked for an unknown value.
using SearchSizeFormatter = std::wstring(*)(uint64_t);

// Returns the Size-on-Disk wording for an indexed result:
//   * unknown  -> explicit "Size on disk unknown" (must never echo logical size;
//     D4: no fabricated on-disk value)
//   * known    -> "<formatted bytes> on disk" (e.g. "8 MB on disk", "0 B on disk")
// `allocatedSizeKnown == false` is the absence sentinel, so a default-constructed
// candidate reads as unknown rather than as a fabricated zero on disk.
inline std::wstring SearchAllocatedSizeText(uint64_t allocatedSizeBytes,
                                            bool allocatedSizeKnown,
                                            SearchSizeFormatter format) {
    if (!allocatedSizeKnown) return L"Size on disk unknown";
    return format(allocatedSizeBytes) + L" on disk";
}

} // namespace ffui
