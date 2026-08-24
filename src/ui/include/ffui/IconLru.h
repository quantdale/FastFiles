// active-path-runtime-resilience 2.5: pure bounded icon-cache recency state.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ffui {

// The UI icon cache owns device resources; this helper owns only recency
// bookkeeping so promotion/eviction remains deterministic and testable.
class IconLru {
public:
    void Touch(const std::wstring& key) {
        stamps_[key] = ++clock_;
    }

    void Clear() {
        stamps_.clear();
        clock_ = 0;
    }

    [[nodiscard]] std::vector<std::wstring> TrimTo(std::size_t maximum) {
        std::vector<std::wstring> evicted;
        while (stamps_.size() > maximum) {
            auto oldest = stamps_.end();
            for (auto candidate = stamps_.begin(); candidate != stamps_.end(); ++candidate) {
                if (oldest == stamps_.end() || candidate->second < oldest->second) {
                    oldest = candidate;
                }
            }
            if (oldest == stamps_.end()) break;
            evicted.push_back(oldest->first);
            stamps_.erase(oldest);
        }
        return evicted;
    }

    [[nodiscard]] std::size_t Size() const { return stamps_.size(); }

private:
    std::map<std::wstring, uint64_t> stamps_;
    uint64_t clock_ = 0;
};

} // namespace ffui
