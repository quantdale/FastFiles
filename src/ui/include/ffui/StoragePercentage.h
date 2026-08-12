#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <string>

namespace ffui {

// A headless percentage value shared by the storage ListView and its sort
// path.  `sortKey` is a ratio in [0, 1], calculated by dividing before
// multiplying so large uint64_t byte counts cannot overflow.
struct StoragePercentage {
    bool known = false;
    double sortKey = 0.0;
    std::wstring display = L"—";
};

inline StoragePercentage ComputeStoragePercentage(uint64_t rowSizeBytes,
                                                  uint64_t parentTotalBytes,
                                                  bool rowKnown = true) {
    if (!rowKnown || rowSizeBytes == 0 || parentTotalBytes == 0) {
        return {};
    }

    const double ratio = (std::min)(1.0,
        static_cast<double>(rowSizeBytes) / static_cast<double>(parentTotalBytes));
    wchar_t buffer[64]{};
    swprintf_s(buffer, L"%.1f%%", ratio * 100.0);
    return {true, ratio, buffer};
}

// MSVC's long double has double precision, so comparisons deliberately use a
// small epsilon after division rather than multiplying 64-bit byte counts.
inline bool StoragePercentageNearlyEqual(double left, double right) noexcept {
    constexpr double kEpsilon = 1e-12;
    return (std::abs)(left - right) <= kEpsilon;
}

} // namespace ffui
