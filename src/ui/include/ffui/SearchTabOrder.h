#pragma once

// ui-v3 search a11y hardening: pure, headless seam for the Search panel's
// keyboard focus order. The panel is not a dialog (the main loop never calls
// IsDialogMessage), so Tab traversal is managed explicitly by the panel's
// window subclasses; this seam owns the ORDER decision so it can be unit
// tested without HWNDs, mirroring the `DropFeedbackLayout.h` /
// `ResponsiveLayout.h` family.
//
// Contract (matches the visible layout top-to-bottom, left-to-right):
//   query field -> Filters disclosure -> [scope -> sort -> direction ->
//   clear history] (only while the disclosure is expanded) -> result list
//   -> wraps back to the query field. Shift+Tab walks the exact reverse.
//
// The Filters disclosure is ALWAYS in the order while the panel is open --
// collapsed or expanded -- because it is the only way to reach the hidden
// filter row. No target is ever skipped or unreachable at either width; the
// narrow two-row stack keeps the same logical order.
//
// Deliberate style note: the ordering is written as explicit comparison
// chains rather than switch statements so every edge stays greppable as one
// line (`X -> Y`) and the wrap-around edges cannot hide between cases.

namespace ffui {

enum class SearchFocusTarget {
    None = 0,
    QueryEdit,     // the query combo's edit control (panel's primary surface)
    FiltersToggle, // the always-visible Filters disclosure button
    ScopeCombo,    // expanded row only
    SortCombo,     // expanded row only
    SortDirection, // expanded row only
    ClearHistory,  // expanded row only
    ResultList,
};

// Next target after `current` in Tab order (wraps to QueryEdit).
constexpr SearchFocusTarget NextSearchFocusTarget(SearchFocusTarget current, bool filtersExpanded) {
    if (current == SearchFocusTarget::None) return SearchFocusTarget::QueryEdit;
    if (!filtersExpanded) {
        // query -> filters -> list -> query
        if (current == SearchFocusTarget::QueryEdit) return SearchFocusTarget::FiltersToggle;
        if (current == SearchFocusTarget::FiltersToggle) return SearchFocusTarget::ResultList;
        return SearchFocusTarget::QueryEdit; // ResultList, None, or stale-after-collapse
    }
    // query -> filters -> scope -> sort -> direction -> clear history -> list -> query
    if (current == SearchFocusTarget::QueryEdit || current == SearchFocusTarget::None) {
        return SearchFocusTarget::FiltersToggle;
    }
    if (current == SearchFocusTarget::FiltersToggle) return SearchFocusTarget::ScopeCombo;
    if (current == SearchFocusTarget::ScopeCombo) return SearchFocusTarget::SortCombo;
    if (current == SearchFocusTarget::SortCombo) return SearchFocusTarget::SortDirection;
    if (current == SearchFocusTarget::SortDirection) return SearchFocusTarget::ClearHistory;
    if (current == SearchFocusTarget::ClearHistory) return SearchFocusTarget::ResultList;
    return SearchFocusTarget::QueryEdit; // ResultList or stale target wraps here
}

// Previous target before `current` in Shift+Tab order (wraps to ResultList).
constexpr SearchFocusTarget PreviousSearchFocusTarget(SearchFocusTarget current, bool filtersExpanded) {
    if (!filtersExpanded) {
        // reverse of query -> filters -> list
        if (current == SearchFocusTarget::QueryEdit) return SearchFocusTarget::ResultList;
        if (current == SearchFocusTarget::FiltersToggle) return SearchFocusTarget::QueryEdit;
        return SearchFocusTarget::FiltersToggle; // ResultList, None, or stale-after-collapse
    }
    // reverse of query -> filters -> scope -> sort -> direction -> clear history -> list
    if (current == SearchFocusTarget::QueryEdit || current == SearchFocusTarget::None) {
        return SearchFocusTarget::ResultList;
    }
    if (current == SearchFocusTarget::FiltersToggle) return SearchFocusTarget::QueryEdit;
    if (current == SearchFocusTarget::ScopeCombo) return SearchFocusTarget::FiltersToggle;
    if (current == SearchFocusTarget::SortCombo) return SearchFocusTarget::ScopeCombo;
    if (current == SearchFocusTarget::SortDirection) return SearchFocusTarget::SortCombo;
    if (current == SearchFocusTarget::ClearHistory) return SearchFocusTarget::SortDirection;
    return SearchFocusTarget::ClearHistory; // ResultList wraps here
}

} // namespace ffui
