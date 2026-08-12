#pragma once

#include <algorithm>
#include <array>

#include "ffui/ShellLayout.h"

namespace ffui {

// The eight Storage Analysis mode buttons retain their established widths on
// a normal content surface.  Below the one-row threshold they are packed into
// deterministic rows so no control can extend back into the sidebar or beyond
// the client edge.
struct StorageSurfaceLayout {
    std::array<ShellLayoutRect, 8> buttons{};
    ShellLayoutRect categoryFilter;
    ShellLayoutRect status;
    ShellLayoutRect list;
    ShellLayoutRect overview;
    int buttonRows = 0;
};

inline StorageSurfaceLayout ComputeStorageSurfaceLayout(const ShellLayoutRect& surfaceRect) noexcept {
    StorageSurfaceLayout result;
    constexpr std::array<float, 8> kButtonWidths = {80.0f, 80.0f, 100.0f, 110.0f,
                                                     120.0f, 110.0f, 110.0f, 90.0f};
    constexpr float kLeftInset = 12.0f;
    constexpr float kRightInset = 12.0f;
    constexpr float kGap = 4.0f;
    constexpr float kButtonHeight = 28.0f;
    constexpr float kMinimumCompactButtonWidth = 44.0f;
    constexpr float kCategoryStatusGap = 4.0f;
    constexpr float kMinimumStatusWidth = 100.0f;
    constexpr float kFullToolbarWidth = 1104.0f;

    const float width = surfaceRect.Width();
    const float innerWidth = (std::max)(0.0f, width - kLeftInset - kRightInset);
    const float top = surfaceRect.top;
    const float left = surfaceRect.left + kLeftInset;
    const bool fullToolbar = width >= kFullToolbarWidth;

    if (fullToolbar) {
        constexpr std::array<float, 8> kButtonLefts = {12.0f, 96.0f, 180.0f, 284.0f,
                                                        398.0f, 522.0f, 636.0f, 750.0f};
        for (size_t index = 0; index < kButtonWidths.size(); ++index) {
            const float buttonLeft = surfaceRect.left + kButtonLefts[index];
            result.buttons[index] = {buttonLeft, top,
                                     buttonLeft + kButtonWidths[index], top + kButtonHeight};
        }
        result.buttonRows = 1;

        const float categoryLeft = surfaceRect.left + 844.0f;
        const float maxStatusWidth = (std::max)(kMinimumStatusWidth,
            surfaceRect.right - kRightInset - categoryLeft - kCategoryStatusGap - 140.0f);
        const float statusWidth = (std::min)(280.0f, maxStatusWidth);
        const float statusLeft = surfaceRect.right - kRightInset - statusWidth;
        result.categoryFilter = {categoryLeft, top, statusLeft - kCategoryStatusGap, top + kButtonHeight};
        result.status = {statusLeft, top + 4.0f, surfaceRect.right - kRightInset, top + 24.0f};
    } else {
        const int columns = (std::clamp)(static_cast<int>((innerWidth + kGap) /
                                                           (kMinimumCompactButtonWidth + kGap)),
                                         1, 8);
        const float buttonWidth = columns > 0
            ? (std::max)(0.0f, (innerWidth - static_cast<float>(columns - 1) * kGap) /
                                  static_cast<float>(columns))
            : 0.0f;
        result.buttonRows = static_cast<int>((kButtonWidths.size() + static_cast<size_t>(columns) - 1) /
                                              static_cast<size_t>(columns));
        for (size_t index = 0; index < kButtonWidths.size(); ++index) {
            const int row = static_cast<int>(index / static_cast<size_t>(columns));
            const int column = static_cast<int>(index % static_cast<size_t>(columns));
            const float buttonLeft = left + static_cast<float>(column) * (buttonWidth + kGap);
            const float buttonTop = top + static_cast<float>(row) * (kButtonHeight + kGap);
            result.buttons[index] = {buttonLeft, buttonTop,
                                     buttonLeft + buttonWidth, buttonTop + kButtonHeight};
        }

        const float secondaryTop = top + static_cast<float>(result.buttonRows) *
                                         (kButtonHeight + kGap);
        const float secondaryWidth = (std::max)(0.0f, innerWidth - kCategoryStatusGap);
        const float categoryWidth = secondaryWidth / 2.0f;
        result.categoryFilter = {left, secondaryTop, left + categoryWidth, secondaryTop + kButtonHeight};
        result.status = {left + categoryWidth + kCategoryStatusGap, secondaryTop + 4.0f,
                         surfaceRect.right - kRightInset, secondaryTop + 24.0f};
    }

    const float listTop = result.categoryFilter.bottom + 8.0f;
    const float listLeft = surfaceRect.left + kLeftInset;
    const float listRight = (std::max)(listLeft, surfaceRect.right - kRightInset);
    const float listBottom = (std::max)(listTop, surfaceRect.bottom - 12.0f);
    result.list = {listLeft, listTop, listRight, listBottom};
    result.overview = result.list;
    return result;
}

} // namespace ffui
