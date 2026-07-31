#include "ffindexstore/NamePool.h"

namespace ffindexstore {

namespace {

uint64_t HashName(std::u16string_view name) noexcept {
    // FNV-1a over the UTF-16 code units. Only used to bucket candidates
    // for the equality check against arena bytes -- collisions are
    // handled correctly, not just tolerated.
    uint64_t hash = 1469598103934665603ULL;
    for (char16_t ch : name) {
        hash ^= static_cast<uint64_t>(ch);
        hash *= 1099511628211ULL;
    }
    return hash;
}

} // namespace

void NamePool::Reserve(size_t expectedUniqueNames, size_t expectedTotalChars) {
    table_.reserve(expectedUniqueNames);
    arena_.reserve(expectedTotalChars);
    hashBuckets_.reserve(expectedUniqueNames);
}

NameId NamePool::Intern(std::u16string_view name) {
    const uint64_t hash = HashName(name);
    auto& bucket = hashBuckets_[hash];
    for (NameId candidateId : bucket) {
        const Slot& slot = table_[candidateId];
        if (slot.length == name.size()
            && std::u16string_view(arena_.data() + slot.offset, slot.length) == name) {
            return candidateId;
        }
    }

    const auto newId = static_cast<NameId>(table_.size());
    Slot slot;
    slot.offset = static_cast<uint32_t>(arena_.size());
    slot.length = static_cast<uint32_t>(name.size());
    arena_.insert(arena_.end(), name.begin(), name.end());
    table_.push_back(slot);
    bucket.push_back(newId);
    return newId;
}

std::u16string_view NamePool::Get(NameId id) const noexcept {
    if (id >= table_.size()) {
        return {};
    }
    const Slot& slot = table_[id];
    return std::u16string_view(arena_.data() + slot.offset, slot.length);
}

} // namespace ffindexstore
