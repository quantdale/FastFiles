#pragma once

#include <algorithm>

#include "ffui/ShellLayout.h"

namespace ffui {

struct SearchSurfaceLayout {
    ShellLayoutRect query;
    ShellLayoutRect scope;
    ShellLayoutRect sort;
    ShellLayoutRect direction;
    ShellLayoutRect status;
    ShellLayoutRect clearHistory;
    ShellLayoutRect list;
    bool compact = false;
};

inline SearchSurfaceLayout ComputeSearchSurfaceLayout(const ShellLayoutRect& surfaceRect) noexcept {
    SearchSurfaceLayout result;
    constexpr float kInset = 12.0f;
    constexpr float kGap = 8.0f;
    constexpr float kControlHeight = 32.0f;
    constexpr float kDirectionHeight = 32.0f;
    constexpr float kSingleRowMinimumWidth = 698.0f;
    constexpr float kScopeWidth = 210.0f;
    constexpr float kSortWidth = 126.0f;
    constexpr float kDirectionWidth = 146.0f;

    const float width = surfaceRect.Width();
    const float left = surfaceRect.left + kInset;
    const float right = (std::max)(left, surfaceRect.right - kInset);
    if (width >= kSingleRowMinimumWidth) {
        const float directionLeft = right - kDirectionWidth;
        const float sortLeft = directionLeft - kGap - kSortWidth;
        const float scopeLeft = sortLeft - kGap - kScopeWidth;
        const float queryRight = scopeLeft - kGap;
        result.query = {left, surfaceRect.top, (std::max)(left, queryRight),
                        surfaceRect.top + kControlHeight};
        result.scope = {scopeLeft, surfaceRect.top, scopeLeft + kScopeWidth,
                        surfaceRect.top + kControlHeight};
        result.sort = {sortLeft, surfaceRect.top, sortLeft + kSortWidth,
                       surfaceRect.top + kControlHeight};
        result.direction = {directionLeft, surfaceRect.top,
                            directionLeft + kDirectionWidth,
                            surfaceRect.top + kDirectionHeight};
        result.status = {left, surfaceRect.top + 40.0f,
                         (std::max)(left, surfaceRect.right - 150.0f),
                         surfaceRect.top + 64.0f};
        result.clearHistory = {(std::max)(left, surfaceRect.right - 126.0f),
                               surfaceRect.top + 36.0f, surfaceRect.right - kInset,
                               surfaceRect.top + 62.0f};
        // Query row + gap + filter/status row + gap + the small separation
        // before results. Keep this seam aligned with SearchPanel's collapsed
        // state so UIA bounds and the painted list start agree.
        result.list = {left, surfaceRect.top + 82.0f, right,
                       (std::max)(surfaceRect.top + 82.0f, surfaceRect.bottom - 12.0f)};
        return result;
    }

    // A narrow workspace gets a vertical control stack.  This is a geometry
    // fallback only: it preserves every control without negative widths or
    // overlapping the sidebar/content edge.
    result.compact = true;
    const float controlWidth = (std::max)(0.0f, right - left);
    const float rowTop = surfaceRect.top;
    result.query = {left, rowTop, left + controlWidth, rowTop + kControlHeight};
    constexpr float kStackStep = kControlHeight + kGap;
    result.scope = {left, rowTop + kStackStep, left + controlWidth, rowTop + kStackStep + kControlHeight};
    result.sort = {left, rowTop + 2.0f * kStackStep, left + controlWidth,
                   rowTop + 2.0f * kStackStep + kControlHeight};
    result.direction = {left, rowTop + 3.0f * kStackStep, left + controlWidth,
                        rowTop + 3.0f * kStackStep + kDirectionHeight};
    const float clearLeft = (std::max)(left, surfaceRect.right - kInset - 114.0f);
    result.status = {left, rowTop + 4.0f * kStackStep,
                     (std::max)(left, clearLeft - kGap), rowTop + 4.0f * kStackStep + 24.0f};
    result.clearHistory = {clearLeft, rowTop + 4.0f * kStackStep - 4.0f,
                           (std::max)(clearLeft, surfaceRect.right - kInset),
                           rowTop + 4.0f * kStackStep + 22.0f};
    result.list = {left, rowTop + 5.0f * kStackStep, right,
                   (std::max)(rowTop + 5.0f * kStackStep, surfaceRect.bottom - kInset)};
    return result;
}

} // namespace ffui
