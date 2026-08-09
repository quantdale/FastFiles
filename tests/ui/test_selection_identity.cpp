#include "ffui/SelectionIdentity.h"

#include <cstdlib>
#include <vector>

#include "../TestSupport.h"

using namespace fftest;

namespace {

ffui::SelectionIdentity PathItem(const wchar_t* path) {
    return {ffui::NormalizeSelectionPath(path), 0, 0, 0, false};
}

ffui::SelectionIdentity IndexedItem(const wchar_t* path, int64_t volume, uint64_t id) {
    return {ffui::NormalizeSelectionPath(path), volume, id, 0, true};
}

ffui::SelectionIndexState Restore(const std::vector<ffui::SelectionIdentity>& before,
                                  const ffui::SelectionIndexState& beforeState,
                                  const std::vector<ffui::SelectionIdentity>& after) {
    const auto captured = ffui::CaptureSelectionIdentityState(before, beforeState);
    return ffui::RestoreSelectionIndexState(after, captured);
}

void TestInsertionBeforeSelection() {
    const std::vector<ffui::SelectionIdentity> before{PathItem(L"C:\\folder\\A.txt"),
                                                       PathItem(L"C:\\folder\\B.txt")};
    const std::vector<ffui::SelectionIdentity> after{PathItem(L"C:\\folder\\000-new.txt"),
                                                      PathItem(L"C:\\folder\\A.txt"),
                                                      PathItem(L"C:\\folder\\B.txt")};
    const auto restored = Restore(before, {{1}, 1, 1}, after);
    Check(restored.selectedIndices == std::set<int>{2} && restored.focusIndex == 2,
          "insertion before selection preserves the selected logical path");
}

void TestSortReorder() {
    const std::vector<ffui::SelectionIdentity> before{PathItem(L"C:\\folder\\A.txt"),
                                                       PathItem(L"C:\\folder\\B.txt")};
    const std::vector<ffui::SelectionIdentity> after{PathItem(L"C:\\folder\\B.txt"),
                                                      PathItem(L"C:\\folder\\A.txt")};
    const auto restored = Restore(before, {{0}, 0, 0}, after);
    Check(restored.selectedIndices == std::set<int>{1} && restored.focusIndex == 1,
          "sorting/reordering follows the selected logical path");
}

void TestMissingSelectionClears() {
    const std::vector<ffui::SelectionIdentity> before{PathItem(L"C:\\folder\\A.txt"),
                                                       PathItem(L"C:\\folder\\B.txt")};
    const std::vector<ffui::SelectionIdentity> after{PathItem(L"C:\\folder\\B.txt")};
    const auto restored = Restore(before, {{0}, 0, 0}, after);
    Check(restored.selectedIndices.empty() && restored.selectionAnchor == -1 && restored.focusIndex == -1,
          "a missing selected item clears selection, anchor, and focus");
}

void TestUnrelatedRowDoesNotInheritSelection() {
    const std::vector<ffui::SelectionIdentity> before{PathItem(L"C:\\folder\\A.txt")};
    const std::vector<ffui::SelectionIdentity> after{PathItem(L"C:\\folder\\000-new.txt")};
    const auto restored = Restore(before, {{0}, 0, 0}, after);
    Check(restored.selectedIndices.empty() && restored.focusIndex == -1,
          "an unrelated replacement row never inherits selection or focus");
}

void TestFocusIsIndependent() {
    const std::vector<ffui::SelectionIdentity> before{PathItem(L"C:\\folder\\A.txt"),
                                                       PathItem(L"C:\\folder\\B.txt")};
    const std::vector<ffui::SelectionIdentity> after{PathItem(L"C:\\folder\\B.txt"),
                                                      PathItem(L"C:\\folder\\A.txt")};
    const auto restored = Restore(before, {{0}, 0, 1}, after);
    Check(restored.selectedIndices == std::set<int>{1} && restored.selectionAnchor == 1 && restored.focusIndex == 0,
          "focus and selection each follow their own logical identity");
}

void TestDegradedPathIdentity() {
    const auto before = PathItem(L"C:\\Folder\\A.txt");
    const auto samePath = PathItem(L"c:/folder/a.txt");
    const auto differentPath = PathItem(L"C:\\Folder\\B.txt");
    Check(ffui::SameSelectionIdentity(before, samePath),
          "degraded entries use case-insensitive full-path identity");
    Check(!ffui::SameSelectionIdentity(before, differentPath),
          "degraded path identity rejects a different entry");
}

void TestIndexedStableIdentity() {
    const auto renamed = IndexedItem(L"C:\\folder\\renamed.txt", 7, 42);
    const auto original = IndexedItem(L"C:\\folder\\original.txt", 7, 42);
    const auto replacement = IndexedItem(L"C:\\folder\\original.txt", 7, 43);
    Check(ffui::SameSelectionIdentity(original, renamed),
          "indexed identity follows a proven rename by volume and file id");
    Check(!ffui::SameSelectionIdentity(original, replacement),
          "indexed identity rejects a different file at the same path");
}

void TestOperationTargetUsesRestoredRow() {
    const std::vector<ffui::SelectionIdentity> before{PathItem(L"C:\\folder\\A.txt"),
                                                       PathItem(L"C:\\folder\\B.txt")};
    const std::vector<ffui::SelectionIdentity> after{PathItem(L"C:\\folder\\000-new.txt"),
                                                      PathItem(L"C:\\folder\\B.txt"),
                                                      PathItem(L"C:\\folder\\A.txt")};
    const auto restored = Restore(before, {{1}, 1, 1}, after);
    std::wstring operationTarget;
    for (const int index : restored.selectedIndices) {
        operationTarget = after[static_cast<size_t>(index)].path;
    }
    Check(operationTarget == L"C:\\folder\\B.txt",
          "operation target resolves from the restored logical selection");
}

} // namespace

int main() {
    TestInsertionBeforeSelection();
    TestSortReorder();
    TestMissingSelectionClears();
    TestUnrelatedRowDoesNotInheritSelection();
    TestFocusIsIndependent();
    TestDegradedPathIdentity();
    TestIndexedStableIdentity();
    TestOperationTargetUsesRestoredRow();
    return FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
