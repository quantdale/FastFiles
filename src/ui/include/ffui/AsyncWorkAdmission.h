#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <future>
#include <vector>

namespace ffui {

// original-vision-journey-recovery 4.8: retain unfinished async states. Their
// final release can join the task, so saturation must decline new work.
inline bool AdmitAsyncWork(std::vector<std::shared_future<void>>& work, std::size_t limit) {
    work.erase(std::remove_if(work.begin(), work.end(), [](const auto& item) {
        return item.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }), work.end());
    return work.size() < limit;
}

} // namespace ffui
