#pragma once
#include <cstdint>
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

    // performance-closure-and-multisurface-acceleration 8.3: releases the
    // growth slack left by incremental insertion/growth once a bulk rebuild
    // has finished. Content-identical: every interned name and its id survive;
    // only unused capacity is returned to the allocator. Like any other
    // mutation, this invalidates views returned by Get() while it runs, and
    // subsequent Intern() calls grow the storage again with the usual
    // amortized behavior.
    void ShrinkToFit();

    std::u16string_view Get(NameId id) const noexcept;

    size_t UniqueNameCount() const noexcept { return table_.size(); }
    size_t ArenaSizeChars() const noexcept { return arena_.size(); }
    // performance-closure-and-multisurface-acceleration 8.1: capacity/detail
    // accessors for the projection-memory decomposition. Every value is a
    // live container measurement and the byte values sum to
    // MemoryUsageBytes(). The per-element hash-node approximation matches the
    // existing estimator convention (key + std::vector header); the standard
    // library's node link and allocator header stay unmodeled and are named in
    // the residual instead of being invented here.
    size_t ArenaCapacityChars() const noexcept { return arena_.capacity(); }
    size_t ArenaCapacityBytes() const noexcept { return arena_.capacity() * sizeof(char16_t); }
    size_t TableSlotCount() const noexcept { return table_.size(); }
    size_t TableSlotCapacityBytes() const noexcept { return table_.capacity() * sizeof(Slot); }
    size_t HashBucketCount() const noexcept { return hashBuckets_.bucket_count(); }
    size_t HashBucketElementCount() const noexcept { return hashBuckets_.size(); }
    // Sum of the per-bucket candidate-vector capacities. This is real interned-
    // storage metadata that MemoryUsageBytes() does not model (it counts the
    // bucket node headers only), so the memory decomposition reports it as an
    // additive, separately labelled category rather than changing the
    // estimator the production memory policy uses.
    size_t HashBucketPayloadBytes() const noexcept {
        size_t result = 0;
        for (const auto& [hash, ids] : hashBuckets_) {
            (void)hash;
            result += ids.capacity() * sizeof(NameId);
        }
        return result;
    }
    // Total capacity held beyond the live content in the two large growth
    // holders (arena and offset table). Used to decide whether a shrink is
    // material; the hash-bucket array is at most one pointer per bucket.
    size_t GrowthSlackBytes() const noexcept {
        return (arena_.capacity() - arena_.size()) * sizeof(char16_t)
            + (table_.capacity() - table_.size()) * sizeof(Slot);
    }
    size_t HashBucketArrayBytes() const noexcept { return hashBuckets_.bucket_count() * sizeof(void*); }
    size_t HashNodeApproxBytes() const noexcept {
        return hashBuckets_.size() * (sizeof(uint64_t) + sizeof(std::vector<NameId>));
    }
    size_t MemoryUsageBytes() const noexcept {
        return ArenaCapacityBytes() + TableSlotCapacityBytes()
            + HashBucketArrayBytes() + HashNodeApproxBytes();
    }

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
