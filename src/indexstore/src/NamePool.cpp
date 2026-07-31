#include "ffindexstore/NamePool.h"

namespace ffindexstore {

NameId NamePool::Intern(std::u16string_view name) {
    // std::unordered_map<std::u16string, ...>::find has no heterogeneous
    // lookup for string_view pre-C++20 transparent hashing here, so build
    // the key once; this is only paid on first sight of a distinct name in
    // the common case (find below still requires materializing a
    // std::u16string for comparison either way).
    std::u16string key(name);
    if (auto it = index_.find(key); it != index_.end()) {
        return it->second;
    }

    const auto offset = static_cast<uint32_t>(arena_.size());
    arena_.insert(arena_.end(), name.begin(), name.end());
    const auto id = static_cast<NameId>(offsets_.size());
    offsets_.push_back(Slot{offset, static_cast<uint32_t>(name.size())});
    index_.emplace(std::move(key), id);
    return id;
}

std::u16string_view NamePool::Lookup(NameId id) const noexcept {
    if (id >= offsets_.size()) {
        return {};
    }
    const Slot& slot = offsets_[id];
    return std::u16string_view(arena_.data() + slot.offset, slot.length);
}

void NamePool::Clear() {
    arena_.clear();
    offsets_.clear();
    index_.clear();
}

void NamePool::Reserve(size_t expectedDistinctNames, size_t expectedTotalChars) {
    offsets_.reserve(expectedDistinctNames);
    arena_.reserve(expectedTotalChars);
}

} // namespace ffindexstore
