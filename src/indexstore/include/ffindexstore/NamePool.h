#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ffindexstore {

using NameId = uint32_t;

// A process-wide, deduplicated interned-string pool (design.md D2): a
// contiguous UTF-16 arena plus an offset/length table, with insertion-time
// dedup so identical name strings anywhere in the tree share one entry
// regardless of how many directories contain a same-named child.
//
// The dedup lookup hashes candidate strings and verifies equality against
// the arena bytes rather than storing a second copy of each string as a
// hash-map key, so the pool's total memory stays close to
// (unique-name-count * average-name-length) rather than doubling it.
class NamePool {
public:
    // task 2.5: pre-size the arena/table before a bulk rebuild to avoid
    // incremental reallocation. expectedUniqueNames and
    // expectedTotalChars are best-effort hints, not hard limits.
    void Reserve(size_t expectedUniqueNames, size_t expectedTotalChars);

    // Returns the id for `name`, inserting it if not already present.
    NameId Intern(std::u16string_view name);

    std::u16string_view Get(NameId id) const noexcept;

    size_t UniqueNameCount() const noexcept { return table_.size(); }
    size_t ArenaSizeChars() const noexcept { return arena_.size(); }

private:
    struct Slot {
        uint32_t offset = 0;
        uint32_t length = 0;
    };

    std::vector<char16_t> arena_;
    std::vector<Slot> table_;
    // hash(name) -> candidate NameIds with that hash, disambiguated by
    // comparing arena bytes -- avoids storing name content twice.
    std::unordered_map<uint64_t, std::vector<NameId>> hashBuckets_;
};

} // namespace ffindexstore
