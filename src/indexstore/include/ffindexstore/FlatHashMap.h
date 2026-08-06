#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <utility>
#include <vector>

#include "ffindexstore/Identity.h"

namespace ffindexstore {

template <typename Key, typename Value, typename Hash = std::hash<Key>>
class FlatHashMap {
public:
    FlatHashMap() = default;
    ~FlatHashMap() = default;
    FlatHashMap(const FlatHashMap&) = delete;
    FlatHashMap& operator=(const FlatHashMap&) = delete;
    FlatHashMap(FlatHashMap&&) noexcept = default;
    FlatHashMap& operator=(FlatHashMap&&) noexcept = default;

    size_t size() const noexcept { return count_; }
    bool empty() const noexcept { return count_ == 0; }
    size_t bucket_count() const noexcept { return keys_.size(); }

    void reserve(size_t n) {
        if (n > keys_.size()) {
            grow(pow2_ceil(n * 2));
        }
    }

    void clear() noexcept {
        keys_.clear();
        values_.clear();
        states_.clear();
        count_ = 0;
    }

    const Value* find(const Key& key) const {
        size_t idx = find_slot(key);
        if (idx == keys_.size() || states_[idx] != 1) return nullptr;
        return &values_[idx];
    }

    Value* find(const Key& key) {
        return const_cast<Value*>(static_cast<const FlatHashMap*>(this)->find(key));
    }

    bool contains(const Key& key) const { return find(key) != nullptr; }

    Value& operator[](const Key& key) {
        size_t idx = find_slot(key);
        if (idx < keys_.size() && states_[idx] == 1) {
            return values_[idx];
        }
        if (idx == keys_.size()) {
            idx = keys_.size();
            keys_.push_back(key);
            values_.push_back(Value{});
            states_.push_back(1);
        } else {
            keys_[idx] = key;
            values_[idx] = Value{};
            states_[idx] = 1;
        }
        ++count_;
        return values_[idx];
    }

    void insert(const Key& key, const Value& value) {
        size_t idx = find_slot(key);
        if (idx < keys_.size() && states_[idx] == 1) {
            values_[idx] = value;
            return;
        }
        if (idx == keys_.size()) {
            idx = keys_.size();
            keys_.push_back(key);
            values_.push_back(value);
            states_.push_back(1);
        } else {
            keys_[idx] = key;
            values_[idx] = value;
            states_[idx] = 1;
        }
        ++count_;
        if (count_ > keys_.size() * 7 / 10) {
            grow(keys_.size() * 2);
        }
    }

    void erase(const Key& key) {
        size_t idx = find_slot(key);
        if (idx == keys_.size() || states_[idx] != 1) return;
        states_[idx] = 2; // tombstone
        --count_;
    }

    struct Iterator {
        const FlatHashMap* map = nullptr;
        size_t index = 0;

        std::pair<const Key&, const Value&> operator*() const {
            return {map->keys_[index], map->values_[index]};
        }
        Iterator& operator++() {
            do { ++index; } while (index < map->keys_.size() && map->states_[index] != 1);
            return *this;
        }
        bool operator!=(const Iterator& other) const { return index != other.index; }
    };

    Iterator begin() const {
        size_t i = 0;
        while (i < keys_.size() && states_[i] != 1) ++i;
        return Iterator{this, i};
    }
    Iterator end() const { return Iterator{this, keys_.size()}; }

private:
    std::vector<Key> keys_;
    std::vector<Value> values_;
    std::vector<uint8_t> states_;
    size_t count_ = 0;
    Hash hasher_;

    static size_t pow2_ceil(size_t n) {
        if (n <= 1) return 1;
        --n;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        return n + 1;
    }

    void grow(size_t new_buckets) {
        std::vector<Key> old_keys = std::move(keys_);
        std::vector<Value> old_values = std::move(values_);
        std::vector<uint8_t> old_states = std::move(states_);
        keys_.assign(new_buckets, Key{});
        values_.assign(new_buckets, Value{});
        states_.assign(new_buckets, 0);
        count_ = 0;
        for (size_t i = 0; i < old_keys.size(); ++i) {
            if (old_states[i] == 1) {
                insert(old_keys[i], old_values[i]);
            }
        }
    }

    size_t find_slot(const Key& key) const {
        if (keys_.empty()) return 0;
        size_t mask = keys_.size() - 1;
        size_t idx = hasher_(key) & mask;
        for (;;) {
            if (states_[idx] == 0) return idx;
            if (states_[idx] == 1 && keys_[idx] == key) return idx;
            idx = (idx + 1) & mask;
        }
    }
};

// FlatChildrenMap provides EntryKey -> contiguous slice of uint32_t child
// indices. No per-key heap allocation for the vector header; all child
// indices live in one flat array.
class FlatChildrenMap {
public:
    using Value = std::vector<uint32_t>;

    size_t size() const noexcept { return keys_.size(); }
    bool empty() const noexcept { return keys_.empty(); }

    std::span<const uint32_t> find(const EntryKey& key) const {
        size_t idx = find_slot(key);
        if (idx == keys_.size() || states_[idx] != 1) return {};
        size_t off = offsets_[idx];
        size_t cnt = counts_[idx];
        return std::span<const uint32_t>(&values_[off], cnt);
    }

    bool contains(const EntryKey& key) const { return !find(key).empty(); }

    // Insert a single child index for `key`. Appends to the existing
    // value's contiguous slice, or creates a new one.
    void insert(const EntryKey& key, uint32_t childIndex) {
        if (count_ + 1 > keys_.size() * 7 / 10) {
            grow(keys_.empty() ? 8 : keys_.size() * 2);
        }
        size_t idx = find_slot(key);
        if (idx < keys_.size() && states_[idx] == 1) {
            // zero-touch-autonomous-engineering: a multi-child directory's
            // slice must stay contiguous so find() returns exactly its
            // children. Appending via push_back to the tail is only valid
            // when the slice already ends at the tail; otherwise the new
            // child would be non-contiguous and find() would read adjacent
            // slices' elements (a latent corruption exposed by rule-honoring
            // ingestion of multi-child volumes). Relocate the slice to the
            // tail first when it is not already there.
            const size_t off = offsets_[idx];
            const size_t cnt = counts_[idx];
            if (off + cnt != values_.size()) {
                values_.insert(values_.end(), values_.begin() + off, values_.begin() + off + cnt);
                offsets_[idx] = values_.size() - cnt;
            }
            values_.push_back(childIndex);
            ++counts_[idx];
            return;
        }
        if (idx == keys_.size()) {
            idx = keys_.size();
            keys_.push_back(key);
            states_.push_back(1);
            offsets_.push_back(values_.size());
            counts_.push_back(1);
            values_.push_back(childIndex);
            ++count_;
        } else {
            keys_[idx] = key;
            states_[idx] = 1;
            offsets_[idx] = values_.size();
            counts_[idx] = 1;
            values_.push_back(childIndex);
            ++count_;
        }
    }

    // Remove `childIndex` from the slice stored at `key`.
    void remove(const EntryKey& key, uint32_t childIndex) {
        size_t idx = find_slot(key);
        if (idx == keys_.size() || states_[idx] != 1) return;
        size_t off = offsets_[idx];
        size_t cnt = counts_[idx];
        for (size_t i = 0; i < cnt; ++i) {
            if (values_[off + i] == childIndex) {
                values_[off + i] = values_[off + cnt - 1];
                --counts_[idx];
                if (counts_[idx] == 0) {
                    states_[idx] = 2; // tombstone
                    --count_;
                }
                return;
            }
        }
    }

    void clear() noexcept {
        keys_.clear();
        values_.clear();
        states_.clear();
        offsets_.clear();
        counts_.clear();
    }

private:
    std::vector<EntryKey> keys_;
    std::vector<uint32_t> values_; // contiguous storage for all child-index slices
    std::vector<uint8_t> states_;
    std::vector<size_t> offsets_; // per-key offset into values_
    std::vector<size_t> counts_;  // per-key count of values
    size_t count_ = 0;

    static size_t pow2_ceil(size_t n) {
        if (n <= 1) return 1;
        --n;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        return n + 1;
    }

    void grow(size_t new_buckets) {
        std::vector<EntryKey> old_keys = std::move(keys_);
        std::vector<uint32_t> old_values = std::move(values_);
        std::vector<uint8_t> old_states = std::move(states_);
        std::vector<size_t> old_offsets = std::move(offsets_);
        std::vector<size_t> old_counts = std::move(counts_);
        keys_.assign(new_buckets, EntryKey{});
        values_.clear();
        states_.assign(new_buckets, 0);
        offsets_.assign(new_buckets, 0);
        counts_.assign(new_buckets, 0);
        count_ = 0;
        for (size_t i = 0; i < old_keys.size(); ++i) {
            if (old_states[i] == 1) {
                EntryKey key = old_keys[i];
                size_t cnt = old_counts[i];
                size_t off = old_offsets[i];
                size_t idx = find_slot(key);
                if (idx == keys_.size()) {
                    idx = keys_.size();
                    keys_.push_back(key);
                    states_.push_back(1);
                    offsets_.push_back(values_.size());
                    counts_.push_back(cnt);
                    values_.insert(values_.end(), old_values.begin() + off, old_values.begin() + off + cnt);
                    ++count_;
                } else {
                    keys_[idx] = key;
                    states_[idx] = 1;
                    offsets_[idx] = values_.size();
                    counts_[idx] = cnt;
                    values_.insert(values_.end(), old_values.begin() + off, old_values.begin() + off + cnt);
                    ++count_;
                }
            }
        }
    }

    size_t find_slot(const EntryKey& key) const {
        if (keys_.empty()) return 0;
        size_t mask = keys_.size() - 1;
        size_t idx = EntryKeyHash{}(key) & mask;
        for (;;) {
            if (states_[idx] == 0) return idx;
            if (states_[idx] == 1 && keys_[idx] == key) return idx;
            idx = (idx + 1) & mask;
        }
    }
};

} // namespace ffindexstore

