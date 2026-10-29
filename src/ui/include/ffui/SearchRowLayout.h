#pragma once

#include <algorithm>
#include <cstdint>

namespace ffui {

struct SearchContextLayout {
    int locationLeft = 0;
    int locationRight = 0;
    int metadataLeft = 0;
    int metadataRight = 0;
};

// original-vision-journey-recovery 3.3: inputs are physical pixels from the
// live owner-draw RECT and measured font. Only the caller's DIP padding/gap
// is scaled. Location owns at least 55% of the usable second line.
inline SearchContextLayout ComputeSearchContextLayout(int textLeft, int rowRight,
                                                      int padding, int gap, int metadataWidth) {
    const int right = (std::max)(textLeft, rowRight - (std::max)(0, padding));
    const int available = right - textLeft;
    const int width = (std::min)((std::max)(0, metadataWidth),
        static_cast<int>(static_cast<int64_t>(available) * 45 / 100));
    const int metadataLeft = right - width;
    const int locationRight = (std::max)(textLeft, metadataLeft - (std::max)(0, gap));
    return {textLeft, locationRight, metadataLeft, right};
}

} // namespace ffui
