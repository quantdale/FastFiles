#pragma once

#include <algorithm>
#include <array>

#include "ffui/ShellLayout.h"

namespace ffui {

// The eight Storage Analysis mode buttons retain their established widths on
// a normal content surface. Below the one-row threshold they are packed into
// deterministic rows so no control can extend back into the sidebar or beyond
// the client edge. ui-v3 hardening V3-06: the compact packing also enforces a
// label-fitting minimum button width — native radio labels have no ellipsis,
// so narrower buttons clipped their text mid-word ("Largest Fol…").
struct StorageSurfaceLayout {
    std::array<ShellLayoutRect, 8> buttons{};
    ShellLayoutRect categoryFilter;
    ShellLayoutRect status;
    ShellLayoutRect list;
    ShellLayoutRect overview;
    ShellLayoutRect viewLabel;
    ShellLayoutRect viewSelector;
    bool compactModes = false;
    int buttonRows = 0;
};

inline ShellLayoutRect StorageStatusRect(const StorageSurfaceLayout& layout, bool treemap) noexcept {
    return treemap ? ShellLayoutRect{layout.categoryFilter.left, layout.status.top,
                                    layout.status.right, layout.status.bottom} : layout.status;
}

inline StorageSurfaceLayout ComputeStorageSurfaceLayout(const ShellLayoutRect& surfaceRect) noexcept {
    StorageSurfaceLayout result;
    constexpr std::array<float, 8> kButtonWidths = {80.0f, 80.0f, 100.0f, 110.0f,
                                                     120.0f, 110.0f, 110.0f, 90.0f};
    constexpr float kLeftInset = 12.0f;
    constexpr float kRightInset = 12.0f;
    constexpr float kGap = 8.0f;
    constexpr float kButtonHeight = 32.0f;
    // Native radio-button labels cannot ellipsize; below this width the longest
    // label ("Largest Folders") clips mid-word at standard body sizes.
    constexpr float kLabelFitButtonWidth = 118.0f;
    constexpr float kCategoryStatusGap = 4.0f;
    constexpr float kMinimumStatusWidth = 100.0f;
    constexpr float kFullToolbarWidth = 1104.0f;

    const float width = surfaceRect.Width();
    const float innerWidth = (std::max)(0.0f, width - kLeftInset - kRightInset);
    const float top = surfaceRect.top;
    const float left = surfaceRect.left + kLeftInset;
    const bool fullToolbar = width >= kFullToolbarWidth;

    // original-vision-journey-recovery 4.12: at narrow widths expose the same
    // six modes through one native selector, leaving height for actual files.
    result.compactModes = innerWidth < 370.0f;
    if (result.compactModes) {
        const float navigationGap = (std::min)(kGap, innerWidth);
        const float halfWidth = (innerWidth - navigationGap) / 2.0f;
        result.buttons[0] = {left, top, left + halfWidth, top + kButtonHeight};
        result.buttons[1] = {left + halfWidth + navigationGap, top,
                             left + innerWidth, top + kButtonHeight};
        result.buttonRows = 1;
        const float viewTop = top + kButtonHeight + kGap;
        result.viewLabel = {left, viewTop, (std::min)(left + 40.0f, left + innerWidth),
                            viewTop + kButtonHeight};
        result.viewSelector = {(std::min)(result.viewLabel.right + kGap, left + innerWidth), viewTop,
                               left + innerWidth, viewTop + kButtonHeight};
        const float categoryTop = viewTop + kButtonHeight + kGap;
        result.categoryFilter = {left, categoryTop, left + innerWidth, categoryTop + kButtonHeight};
        result.status = {left, result.categoryFilter.bottom + kGap,
                         left + innerWidth, result.categoryFilter.bottom + kGap + 20.0f};
    } else if (fullToolbar) {
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
                                                           (kLabelFitButtonWidth + kGap)),
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
        result.categoryFilter = {left, secondaryTop, left + innerWidth, secondaryTop + kButtonHeight};
        result.status = {left, result.categoryFilter.bottom + kGap,
                         left + innerWidth, result.categoryFilter.bottom + kGap + 20.0f};
    }

    const float listTop = (std::max)(result.categoryFilter.bottom, result.status.bottom) + 8.0f;
    const float listLeft = surfaceRect.left + kLeftInset;
    const float listRight = (std::max)(listLeft, surfaceRect.right - kRightInset);
    const float listBottom = (std::max)(listTop, surfaceRect.bottom - 12.0f);
    result.list = {listLeft, listTop, listRight, listBottom};
    result.overview = result.list;
    return result;
}

} // namespace ffui
