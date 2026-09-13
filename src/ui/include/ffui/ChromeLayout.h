#pragma once

// ui-overhaul phase 1: explicit responsive policy for the top navigation
// chrome.  The chrome used to place its children with scattered `if (width <
// ...)` branches and per-control literals; this header is the single pure seam
// that classifies the available width and derives every command/tab/toolbar
// rectangle from it.  Inputs and outputs are DIPs; callers scale to physical
// pixels at their Win32 positioning boundary (matching ShellLayout.h).
//
// The policy keeps the location signal (breadcrumb / address field) usable at
// every width: the search affordance collapses to its icon first, command
// buttons never change size, and tabs shrink to a readable floor before an
// overflow affordance takes over.

#include <algorithm>

namespace ffui {

enum class ChromeWidthClass {
    Narrow,
    Normal,
    Wide,
};

struct ChromeRect {
    float left = 0.0f;
    float top = 0.0f;
    float size = 0.0f;   // width; equals height for square command targets
    float height = 0.0f; // explicit height; 0 means "square" (use size)

    constexpr float right() const noexcept { return left + size; }
    constexpr float bottom() const noexcept { return top + Height(); }
    constexpr float Width() const noexcept { return size; }
    constexpr float Height() const noexcept { return height > 0.0f ? height : size; }
};

// Tab strip geometry.  All tabs share one width at rest; when even the
// readable floor cannot fit, `overflow` reports that a bounded window of tabs
// is visible and the caller must expose the rest through the overflow
// affordance.
struct ChromeTabsLayout {
    float left = 0.0f;
    float right = 0.0f;
    float top = 0.0f;
    float height = 0.0f;
    float gap = 0.0f;
    float minWidth = 0.0f;
    float maxWidth = 0.0f;
    float width = 0.0f;       // uniform painted width (0 when tabCount == 0)
    bool overflow = false;
    ChromeRect overflowButton;
    int visibleFirst = 0;     // index of the first visible tab
    int visibleCount = 0;     // number of visible tabs
};

// Vertical band occupied by the location row. Painters, the address editor,
// and the click target must all derive from this instead of any control's
// width: a band computed from a control's *width* silently grows past the
// chrome clip and hides the breadcrumb text.
struct ChromeBand {
    float top = 0.0f;
    float bottom = 0.0f;
    constexpr float Height() const noexcept { return bottom - top; }
};

struct ChromeLayout {
    ChromeWidthClass widthClass = ChromeWidthClass::Normal;
    float chromeHeight = 72.0f;
    ChromeTabsLayout tabs;
    ChromeRect settings;
    ChromeRect newTab;
    ChromeRect back;
    ChromeRect forward;
    ChromeRect up;
    float addressLeft = 0.0f;
    float addressRight = 0.0f;
    bool searchCollapsed = false;
    ChromeRect search;
};

inline ChromeWidthClass ClassifyChromeWidth(float widthDip) {
    // Thresholds are chosen from the workspace contract rather than a single
    // control: at Narrow the search affordance must collapse for the
    // breadcrumb/address region to keep a usable width; Wide only widens the
    // search field and tab ceiling.
    if (widthDip < 760.0f) return ChromeWidthClass::Narrow;
    if (widthDip < 1180.0f) return ChromeWidthClass::Normal;
    return ChromeWidthClass::Wide;
}

// The one location-band definition shared by the breadcrumb painter and the
// address click target.
inline ChromeBand ChromeLocationBand(const ChromeLayout& layout) {
    const float top = layout.search.top;
    return {top, top + layout.search.Height()};
}

// The location row split into the editable field and an optional right-aligned
// error slot.  The address edit is a native child that paints its own rect
// opaquely, so an address error is only readable when it owns a slot the edit
// does not cover; the error never takes more than half the row.
struct ChromeLocationRow {
    ChromeRect field;
    ChromeRect error;
    bool hasError = false;
};

inline ChromeLocationRow ComputeChromeLocationRow(const ChromeLayout& layout, float errorWidthDip) {
    const ChromeBand band = ChromeLocationBand(layout);
    const float total = (std::max)(0.0f, layout.addressRight - layout.addressLeft);
    const float errorWidth = std::clamp(errorWidthDip, 0.0f, total * 0.5f);
    ChromeLocationRow row;
    row.hasError = errorWidth > 0.0f;
    row.field = {layout.addressLeft, band.top, (std::max)(0.0f, total - errorWidth), band.Height()};
    if (row.hasError) {
        row.error = {layout.addressRight - errorWidth, band.top, errorWidth, band.Height()};
    }
    return row;
}

inline ChromeLayout ComputeChromeLayout(float clientWidthDip, int tabCount, int activeTabIndex) {
    constexpr float kChromeHeight = 72.0f;
    constexpr float kRowTop = 4.0f;
    constexpr float kRowHeight = 28.0f;
    constexpr float kToolbarTop = 40.0f;
    constexpr float kEdge = 4.0f;
    constexpr float kCommandSize = 28.0f;
    constexpr float kCommandGap = 4.0f;
    constexpr float kAddressGap = 8.0f;
    constexpr float kTabMinWidth = 96.0f;
    constexpr float kTabMaxWidth = 220.0f;
    constexpr float kTabGap = 4.0f;
    constexpr float kOverflowWidth = 30.0f;

    ChromeLayout layout;
    layout.chromeHeight = kChromeHeight;
    const float width = (std::max)(0.0f, clientWidthDip);
    layout.widthClass = ClassifyChromeWidth(width);

    // Trailing row controls: New Tab then Settings, both one command square.
    layout.newTab = {width - kEdge - kCommandSize, kRowTop, kCommandSize};
    layout.settings = {layout.newTab.left - kCommandGap - kCommandSize, kRowTop, kCommandSize};

    // Tab strip spans from the left edge to the trailing controls.
    ChromeTabsLayout& tabs = layout.tabs;
    tabs.left = kEdge;
    tabs.right = (std::max)(tabs.left, layout.settings.left - kAddressGap);
    tabs.top = kRowTop;
    tabs.height = kRowHeight;
    tabs.gap = kTabGap;
    tabs.minWidth = kTabMinWidth;
    tabs.maxWidth = kTabMaxWidth;
    tabs.overflowButton = {0.0f, kRowTop, 0.0f};
    const int count = (std::max)(0, tabCount);
    tabs.visibleCount = count;
    if (count > 0) {
        const float stripWidth = (std::max)(0.0f, tabs.right - tabs.left);
        const float ideal = (stripWidth - kTabGap * static_cast<float>(count - 1)) /
                            static_cast<float>(count);
        tabs.width = std::clamp(ideal, kTabMinWidth, kTabMaxWidth);
        const float total = tabs.width * static_cast<float>(count) +
                            kTabGap * static_cast<float>(count - 1);
        if (total > stripWidth + 0.01f) {
            // Even the floor does not fit: show a bounded window of tabs around
            // the active one and reserve the trailing slot for the overflow
            // affordance.  The active tab is always inside the window.
            const float windowWidth = (std::max)(0.0f, stripWidth - kOverflowWidth - kTabGap);
            const int capacity = (std::max)(1, static_cast<int>(
                (windowWidth + kTabGap) / (kTabMinWidth + kTabGap)));
            const int visible = (std::min)(count, capacity);
            const int centered = activeTabIndex - visible / 2;
            tabs.visibleFirst = std::clamp(centered, 0, count - visible);
            tabs.visibleCount = visible;
            tabs.width = kTabMinWidth;
            tabs.overflow = true;
            tabs.overflowButton = {tabs.right - kOverflowWidth, kRowTop, kOverflowWidth, kRowHeight};
        }
    }

    // Toolbar row: Back / Forward / Up command squares, then the location
    // region, then the search affordance at the trailing edge.
    layout.back = {kEdge, kToolbarTop, kCommandSize};
    layout.forward = {layout.back.right() + kCommandGap, kToolbarTop, kCommandSize};
    layout.up = {layout.forward.right() + kCommandGap, kToolbarTop, kCommandSize};
    const float trailing = width - kEdge;
    const bool collapsed = layout.widthClass == ChromeWidthClass::Narrow;
    layout.searchCollapsed = collapsed;
    const float searchWidth = collapsed ? kCommandSize
                                        : (layout.widthClass == ChromeWidthClass::Wide ? 240.0f : 200.0f);
    layout.search = {trailing - searchWidth, kToolbarTop, searchWidth, kRowHeight};
    layout.addressLeft = layout.up.right() + kAddressGap;
    // The location region yields to the search affordance rather than
    // overlapping it; degenerate widths produce an empty (not double-painted)
    // address band.
    layout.addressRight = (std::max)(layout.addressLeft, layout.search.left - kAddressGap);
    return layout;
}

} // namespace ffui
