#pragma once

#include <algorithm>
#include <set>

namespace ffui {

inline void ApplySelectionClick(std::set<int>& selected, int& anchor, int& focus,
                                int itemIndex, bool control, bool shift) {
    if (shift && anchor >= 0) {
        selected.clear();
        const int first = (std::min)(anchor, itemIndex);
        const int last = (std::max)(anchor, itemIndex);
        for (int index = first; index <= last; ++index) selected.insert(index);
        focus = itemIndex;
        return;
    }
    if (control) {
        if (!selected.erase(itemIndex)) selected.insert(itemIndex);
        if (anchor < 0) anchor = itemIndex;
        focus = itemIndex;
        return;
    }
    selected = {itemIndex};
    anchor = itemIndex;
    focus = itemIndex;
}

inline void SelectAllItems(std::set<int>& selected, int& anchor, int& focus, int itemCount) {
    selected.clear();
    for (int index = 0; index < itemCount; ++index) selected.insert(index);
    if (itemCount > 0) {
        anchor = 0;
        focus = 0;
    } else {
        anchor = -1;
        focus = -1;
    }
}

// selection-input-hardening: PageUp/PageDown/Home/End target computation for
// a flat list. `deltaRows` is +-the visible-page row count (or INT_MIN/INT_MAX
// style extremes for Home/End); the result always clamps to [0, itemCount-1].
// An empty list (itemCount <= 0) has no target (-1), and "no focus" (< 0)
// moves to the nearest endpoint in the movement direction.
inline int ListFocusTarget(int currentFocus, int itemCount, int deltaRows) {
    if (itemCount <= 0) return -1;
    const int last = itemCount - 1;
    const int base = currentFocus < 0 ? (deltaRows >= 0 ? 0 : last) : currentFocus;
    const long long target = static_cast<long long>(base) + deltaRows;
    return static_cast<int>((std::clamp)(target, 0LL, static_cast<long long>(last)));
}

// selection-input-hardening: apply a keyboard focus move (PageUp/PageDown/
// Home/End). Ctrl moves the focus WITHOUT changing the selection (standard
// Windows list semantics); Shift extends the range from the anchor; plain
// movement makes the target the single selection and re-anchors.
inline void ApplyKeyboardFocusMove(std::set<int>& selected, int& anchor, int& focus,
                                   int target, bool control, bool shift) {
    if (target < 0) return;
    if (control) {
        focus = target;
        return;
    }
    ApplySelectionClick(selected, anchor, focus, target, false, shift);
}

} // namespace ffui
