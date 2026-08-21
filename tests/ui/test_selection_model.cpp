// harden-selection-model-semantics 2.1: direct, executable regression matrix
// for the real production selection state machine in SelectionModel.h. This
// suite includes the production helper (no copy/reimplementation) and exercises
// the canonical plain/Ctrl/Shift/Ctrl+A transition table, asserting membership,
// anchor, and focus as three distinct concepts after every transition (D2).

#include "SelectionModel.h"

#include <climits>
#include <set>
#include <vector>

#include "../TestSupport.h"

using namespace fftest;
using namespace ffui;

namespace {

struct ModelState {
    std::set<int> selected;
    int anchor = -1;
    int focus = -1;
};

void Apply(ModelState& s, int itemIndex, bool control, bool shift) {
    ApplySelectionClick(s.selected, s.anchor, s.focus, itemIndex, control, shift);
}

bool SameSet(const std::set<int>& a, const std::set<int>& b) {
    return a == b;
}

void CheckState(const char* label, const ModelState& s,
                const std::set<int>& expSelected, int expAnchor, int expFocus) {
    const std::string selMsg = std::string(label) + ": selected set mismatch";
    const std::string anchorMsg = std::string(label) + ": anchor mismatch";
    const std::string focusMsg = std::string(label) + ": focus mismatch";
    Check(SameSet(s.selected, expSelected), selMsg.c_str());
    Check(s.anchor == expAnchor, anchorMsg.c_str());
    Check(s.focus == expFocus, focusMsg.c_str());
}

// 2.2 / 2.3: plain click from empty and from a multi-selection.
void TestPlainClick() {
    // From empty state.
    {
        ModelState s;
        Apply(s, 2, false, false);
        CheckState("plain-from-empty", s, {2}, 2, 2);
    }
    // From an existing multi-selection: replaces the entire selection and
    // re-establishes anchor+focus at the clicked item.
    {
        ModelState s;
        s.selected = {1, 3, 5};
        s.anchor = 1;
        s.focus = 3;
        Apply(s, 4, false, false);
        CheckState("plain-from-multiselection", s, {4}, 4, 4);
    }
}

// 2.4 / 2.5: Ctrl-click adds and removes exactly the target; unrelated members
// are preserved; focus follows the clicked item.
void TestCtrlToggle() {
    // Add an unselected item to an existing selection.
    {
        ModelState s;
        s.selected = {1, 4};
        s.anchor = 1;
        s.focus = 1;
        Apply(s, 2, true, false);
        CheckState("ctrl-add", s, {1, 2, 4}, 1, 2);
    }
    // Remove an already-selected item.
    {
        ModelState s;
        s.selected = {1, 2, 4};
        s.anchor = 1;
        s.focus = 2;
        Apply(s, 2, true, false);
        CheckState("ctrl-remove", s, {1, 4}, 1, 2);
    }
}

// 2.6: Ctrl-click as the first interaction when no anchor exists.
void TestCtrlFirstInteraction() {
    ModelState s; // anchor == -1
    Apply(s, 3, true, false);
    CheckState("ctrl-first", s, {3}, 3, 3);
    // A second Ctrl-click on a different item preserves the established anchor.
    Apply(s, 5, true, false);
    CheckState("ctrl-first-second", s, {3, 5}, 3, 5);
}

// 2.7: Shift forward and backward from a valid anchor.
void TestShiftRanges() {
    // Forward range.
    {
        ModelState s;
        s.selected = {0};
        s.anchor = 0;
        s.focus = 0;
        Apply(s, 3, false, true);
        CheckState("shift-forward", s, {0, 1, 2, 3}, 0, 3);
    }
    // Backward range: range is inclusive between anchor and clicked endpoint.
    {
        ModelState s;
        s.selected = {4};
        s.anchor = 4;
        s.focus = 4;
        Apply(s, 1, false, true);
        CheckState("shift-backward", s, {1, 2, 3, 4}, 4, 1);
    }
}

// 2.8: repeated Shift-click from one established anchor recomputes the range
// from the same anchor instead of re-anchoring to the previous endpoint.
void TestRepeatedShiftStableAnchor() {
    ModelState s;
    s.selected = {0};
    s.anchor = 0;
    s.focus = 0;
    Apply(s, 3, false, true);          // range 0..3, focus 3
    CheckState("repeated-shift-1", s, {0, 1, 2, 3}, 0, 3);
    Apply(s, 1, false, true);          // recompute from anchor 0 -> 0..1
    CheckState("repeated-shift-2", s, {0, 1}, 0, 1);
    Apply(s, 4, false, true);          // recompute from anchor 0 -> 0..4
    CheckState("repeated-shift-3", s, {0, 1, 2, 3, 4}, 0, 4);
}

// 2.9: Shift-click with no prior anchor deterministically becomes a single-item
// selection with a valid anchor/focus (the shift branch is skipped because
// anchor < 0, so it falls through to the plain-click path).
void TestShiftWithoutAnchor() {
    ModelState s; // anchor == -1
    Apply(s, 2, false, true);
    CheckState("shift-no-anchor", s, {2}, 2, 2);
}

// 2.10: mixed sequential transitions that assert membership, anchor, and focus
// independently after each step.
void TestMixedSequences() {
    // plain -> Ctrl -> Shift, all distinct fields checked each step.
    {
        ModelState s;
        Apply(s, 0, false, false);     // plain
        CheckState("mixed-a-plain", s, {0}, 0, 0);
        Apply(s, 2, true, false);      // ctrl add
        CheckState("mixed-a-ctrl", s, {0, 2}, 0, 2);
        Apply(s, 4, false, true);      // shift from anchor 0
        CheckState("mixed-a-shift", s, {0, 1, 2, 3, 4}, 0, 4);
    }
    // plain -> Shift -> Ctrl (Ctrl toggles without disturbing the range/focus).
    {
        ModelState s;
        Apply(s, 1, false, false);     // plain -> anchor 1
        CheckState("mixed-b-plain", s, {1}, 1, 1);
        Apply(s, 4, false, true);      // shift -> range 1..4
        CheckState("mixed-b-shift", s, {1, 2, 3, 4}, 1, 4);
        Apply(s, 6, true, false);      // ctrl add 6
        CheckState("mixed-b-ctrl", s, {1, 2, 3, 4, 6}, 1, 6);
    }
}

// 2.11: SelectAllItems for N>0 and N=0.
void TestSelectAll() {
    // Non-empty: selects [0..N-1] and establishes valid anchor/focus.
    {
        ModelState s;
        s.selected = {7};
        s.anchor = 7;
        s.focus = 7;
        SelectAllItems(s.selected, s.anchor, s.focus, 5);
        CheckState("select-all-nonempty", s, {0, 1, 2, 3, 4}, 0, 0);
    }
    // Empty: clears selection and invalidates anchor/focus (canonical: "no
    // valid anchor/focus" on an empty pane).
    {
        ModelState s;
        s.selected = {3};
        s.anchor = 3;
        s.focus = 3;
        SelectAllItems(s.selected, s.anchor, s.focus, 0);
        CheckState("select-all-empty", s, {}, -1, -1);
    }
}

// 2.12: invariant — every transition above was asserted via CheckState, which
// verifies selected set, anchor, and focus exactly. This final check confirms
// the whole-table invariant helper is exercised (no sentinel leaks).
void TestNoSentinelRange() {
    // A Shift without an anchor must never build a range that includes -1.
    ModelState s; // anchor == -1
    Apply(s, 0, false, true);
    Check(s.anchor >= 0, "shift-without-anchor yields a valid (>=0) anchor");
    Check(s.selected.find(-1) == s.selected.end(),
          "shift-without-anchor never introduces a -1 sentinel member");
    Check(s.selected == std::set<int>{0}, "shift-without-anchor is a single item");
}

// selection-input-hardening: PageUp/PageDown/Home/End target math.
void TestListFocusTarget() {
    Check(ListFocusTarget(0, 0, 5) == -1, "no target in an empty list");
    Check(ListFocusTarget(10, 20, 10) == 19, "page down moves by the page row count");
    Check(ListFocusTarget(15, 20, 10) == 19, "page down clamps at the last item");
    Check(ListFocusTarget(15, 20, -10) == 5, "page up moves by the page row count");
    Check(ListFocusTarget(3, 20, -10) == 0, "page up clamps at the first item");
    Check(ListFocusTarget(7, 20, INT_MIN) == 0, "home lands on the first item");
    Check(ListFocusTarget(7, 20, INT_MAX) == 19, "end lands on the last item");
    Check(ListFocusTarget(-1, 20, INT_MIN) == 0, "home from no focus lands on the first item");
    Check(ListFocusTarget(-1, 20, INT_MAX) == 19, "end from no focus lands on the last item");
    Check(ListFocusTarget(-1, 20, 10) == 10, "movement from no focus uses item 0 as base");
}

// selection-input-hardening: keyboard focus-move application semantics
// (plain = select+anchor, Ctrl = focus only, Shift = extend from anchor).
void TestApplyKeyboardFocusMove() {
    // Plain movement re-selects and re-anchors.
    {
        ModelState s;
        s.selected = {2, 4};
        s.anchor = 2;
        s.focus = 4;
        ApplyKeyboardFocusMove(s.selected, s.anchor, s.focus, 12, false, false);
        CheckState("kbd-plain", s, {12}, 12, 12);
    }
    // Ctrl moves focus without touching the selection or anchor.
    {
        ModelState s;
        s.selected = {2, 4};
        s.anchor = 2;
        s.focus = 4;
        ApplyKeyboardFocusMove(s.selected, s.anchor, s.focus, 14, true, false);
        CheckState("kbd-ctrl", s, {2, 4}, 2, 14);
    }
    // Shift extends the range from the existing anchor.
    {
        ModelState s;
        s.selected = {2};
        s.anchor = 2;
        s.focus = 2;
        ApplyKeyboardFocusMove(s.selected, s.anchor, s.focus, 6, false, true);
        CheckState("kbd-shift", s, {2, 3, 4, 5, 6}, 2, 6);
    }
    // An invalid target (-1, empty list) is a no-op.
    {
        ModelState s;
        s.selected = {1};
        s.anchor = 1;
        s.focus = 1;
        ApplyKeyboardFocusMove(s.selected, s.anchor, s.focus, -1, false, false);
        CheckState("kbd-invalid-target", s, {1}, 1, 1);
    }
}

} // namespace

int main() {
    TestPlainClick();
    TestCtrlToggle();
    TestCtrlFirstInteraction();
    TestShiftRanges();
    TestRepeatedShiftStableAnchor();
    TestShiftWithoutAnchor();
    TestMixedSequences();
    TestSelectAll();
    TestNoSentinelRange();
    TestListFocusTarget();
    TestApplyKeyboardFocusMove();

    if (FailureCount() != 0) {
        fprintf(stderr, "ffselection_model_tests: %d check(s) failed\n", FailureCount());
        return 1;
    }
    printf("ffselection_model_tests: all checks passed\n");
    return 0;
}
