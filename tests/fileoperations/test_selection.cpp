#include "SelectionModel.h"

#include <cstdlib>
#include <iostream>
#include "../TestSupport.h"

using namespace fftest;


int main() {
    std::set<int> firstPane;
    std::set<int> secondPane;
    int firstAnchor = -1, firstFocus = -1, secondAnchor = -1, secondFocus = -1;
    ffui::ApplySelectionClick(firstPane, firstAnchor, firstFocus, 2, false, false);
    Check(firstPane == std::set<int>{2} && firstAnchor == 2, "plain click failed");
    ffui::ApplySelectionClick(firstPane, firstAnchor, firstFocus, 4, true, false);
    Check(firstPane == (std::set<int>{2, 4}) && firstAnchor == 2 && firstFocus == 4, "Ctrl-click add failed");
    ffui::ApplySelectionClick(firstPane, firstAnchor, firstFocus, 4, true, false);
    Check(firstPane == std::set<int>{2}, "Ctrl-click remove failed");
    ffui::ApplySelectionClick(firstPane, firstAnchor, firstFocus, 5, false, true);
    Check(firstPane == (std::set<int>{2, 3, 4, 5}) && firstAnchor == 2, "Shift-click range failed");
    ffui::ApplySelectionClick(firstPane, firstAnchor, firstFocus, 3, false, true);
    Check(firstPane == (std::set<int>{2, 3}) && firstAnchor == 2, "repeated Shift-click changed anchor");
    ffui::ApplySelectionClick(secondPane, secondAnchor, secondFocus, 1, false, false);
    ffui::SelectAllItems(firstPane, firstAnchor, firstFocus, 6);
    Check(firstPane.size() == 6 && secondPane == std::set<int>{1}, "Ctrl+A leaked across panes");
    std::cout << "selection model tests passed\n";
    return fftest::FailureCount();
}
