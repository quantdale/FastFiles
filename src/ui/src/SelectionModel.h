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

} // namespace ffui
