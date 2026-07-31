#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ffindexstore {

using NameId = uint32_t;
constexpr NameId kInvalidNameId = UINT32_MAX;

// Process-wide interned/deduplicated name pool (design.md D2, tasks.md
// 2.2): a contiguous UTF-16 arena plus an offset/length table, deduplicated
// at insertion time via a hash lookup, so identical name strings anywhere
// in the tree (".git", "node_modules", a common filename) share one entry
// regardless of how many directories contain a same-named child.
//
// The dedup map (`index_`) owns its own copies of each distinct name --
// deliberately not string_views into the arena, since the arena
// (std::vector<char16_t>) may reallocate on growth and invalidate any
// pointer into it. That duplicates each *distinct* name's storage once
// more, which is a bounded, small cost relative to the total entry count
// (D2's whole premise is that the distinct-name count is small).
class NamePool {
public:
    // Returns the existing id if `name` was already interned, otherwise
    // inserts it and returns the new id.
    NameId Intern(std::u16string_view name);

    std::u16string_view Lookup(NameId id) const noexcept;

    size_t DistinctNameCount() const noexcept { return offsets_.size(); }
    // Total bytes held by the arena (for RAM budget accounting, task 9.7).
    size_t ArenaBytes() const noexcept { return arena_.size() * sizeof(char16_t); }

    void Clear();

    // Pre-reserves arena/table capacity ahead of a bulk load (task 2.5).
    void Reserve(size_t expectedDistinctNames, size_t expectedTotalChars);

private:
    struct Slot {
        uint32_t offset;
        uint32_t length;
    };

    std::vector<char16_t> arena_;
    std::vector<Slot> offsets_;
    std::unordered_map<std::u16string, NameId> index_;
};

} // namespace ffindexstore
