#pragma once
#include <algorithm>

namespace ffui {

// Keep the focused column's leading content visible, including when a narrow
// pane cannot contain a complete column. Inputs and result are viewport DIPs.
inline float ColumnRevealScrollOffset(int columnIndex, float columnWidth,
                                      float contentWidth, float viewportWidth, float currentScroll) {
    if (viewportWidth <= 0.0f || columnWidth <= 0.0f || columnIndex < 0) return currentScroll;
    const float maximum = (std::max)(0.0f, contentWidth - viewportWidth);
    float target = std::clamp(currentScroll, 0.0f, maximum);
    const float left = static_cast<float>(columnIndex) * columnWidth;
    if (columnWidth >= viewportWidth || left < target) target = left;
    else if (left + columnWidth > target + viewportWidth) target = left + columnWidth - viewportWidth;
    return std::clamp(target, 0.0f, maximum);
}

} // namespace ffui
