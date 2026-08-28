#pragma once

#include <algorithm>
#include <vector>

#include "ffui/ResponsiveLayout.h" // ui-v3 home: content-width classification

// quick-access-home 3.1: pure, headless layout seam for the Home surface.
//
// This header owns no rendering state and pulls in no D2D / DirectWrite /
// UITheme dependencies so it can be unit-tested standalone (the test target
// only adds `../../src/ui/include` to its include path). It mirrors the
// established `ShellLayout.h` / `DetailsLayout.h` family: a small input
// struct, a result struct, and one total `ComputeHomeLayout` function.
//
// DPI note: every value here is a DIP (device-independent pixel). The renderer
// applies DPI scaling itself (Renderer::ApplyDpi sets the Direct2D context
// DPI), so this layout is DPI-independent by construction -- a 1280x720 DIP
// viewport produces identical geometry whether it is a 100% 1280x720 monitor
// or a 150%-scaled 1920x1080 monitor. Inputs and outputs never need a scale
// factor.
//
// The function is total: any input, including a zero-size viewport or negative
// counts, returns a well-formed result. Counts are already bounded upstream
// (<= 10 recents, <= 100 searches, ...) but the seam clamps defensively.

namespace ffui {

// quick-access-home 3.1: spacing/geometry constants. These intentionally
// mirror the lived-in `UiMetrics` values used by the renderer (kSpaceS = 8,
// kSpaceM = 16, kRadiusMedium = 8) so the headless layout stays visually
// consistent with the rest of the UI without taking a dependency on
// `src/ui/src/UITheme.h`.
constexpr float kHomeMargin = 16.0f;          // kSpaceM: outer content margin
constexpr float kHomeSpaceS = 8.0f;           // kSpaceS: inter-item / inter-column gap
constexpr float kHomeSpaceM = 16.0f;          // kSpaceM: legacy inter-section spacing band
// ui-v3 home 2: sections separate by a generous kSpaceL band so grouping is
// carried by whitespace and typography instead of boxes.
constexpr float kHomeSectionGap = 24.0f;      // kSpaceL: inter-section spacing band
constexpr float kHomeRadiusSmall = 4.0f;      // kRadiusSmall: row corner radius
constexpr float kHomeRadiusMedium = 8.0f;     // kRadiusMedium: card corner radius
// ui-v3 home 2: headers stack a Subtitle-size semibold section title over an
// optional Caption-size count hint; primary destination rows carry two text
// lines.
constexpr float kHomeHeaderHeight = 44.0f;    // section header band height
constexpr float kHomeRowHeight = 52.0f;       // 1-column two-line item row height
constexpr float kHomeCardHeight = 56.0f;      // 2-column two-line item card height
constexpr float kHomeSingleRowHeight = 36.0f; // compact single-line utility row height
constexpr float kHomeMaxContentWidth = 960.0f; // WIDE: content capped + left-aligned
constexpr float kHomeNarrowMaxWidth = 480.0f;  // viewport < this => single column
constexpr float kHomeWideMinWidth = 1100.0f;   // viewport >= this => wide content cap
constexpr float kHomeMinColumnWidth = 220.0f;  // 2-col needs two columns of >= this
constexpr int kHomeMaxItems = 200;             // defensive total-item cap
constexpr int kHomeActionCount = 5;            // New Tab/Search/Analyze Storage/Open Path/Settings

enum class HomeSectionKind {
    Pinned = 0,
    RecentFolders = 1,
    RecentSearches = 2,
    CommonLocations = 3,
    QuickActions = 4,
};

// ui-v3 home 1/3: primary destinations (Pinned / Recent folders) carry a name
// plus a secondary path line and get taller two-line rows (or cards in the
// two-column grid). Everything else -- Recent searches (tertiary weight),
// Common locations and Quick actions (utility) -- stays a compact single-line
// link / nav row instead of competing at destination-card weight. Pure + total
// so the seam, the renderer, and any consumer agree on one geometry source.
inline float HomeSectionItemHeight(HomeSectionKind kind, int columnCount) noexcept {
    const bool twoLine = kind == HomeSectionKind::Pinned ||
                         kind == HomeSectionKind::RecentFolders;
    if (!twoLine) {
        return kHomeSingleRowHeight;
    }
    return columnCount == 2 ? kHomeCardHeight : kHomeRowHeight;
}

struct HomeLayoutInput {
    float viewportWidth = 0.0f;
    float viewportHeight = 0.0f;
    int pinnedCount = 0;
    int recentFolderCount = 0;
    int recentSearchCount = 0;
    int knownFolderCount = 0;
    bool showActions = false;
};

// One flat item. Both hit-testing and keyboard focus read rectangles from this
// single source of truth, keyed by `globalIndex`. `sectionIndex` + `indexInSection`
// let a consumer map back to its owning section / position within the section.
struct HomeItemLayout {
    int sectionIndex = 0;
    int indexInSection = 0;
    int globalIndex = 0;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float radius = kHomeRadiusMedium;
};

// Per-section bounding rect (header band + item grid) and the slice of the
// flat `items` vector it owns (itemStart .. itemStart + itemCount).
struct HomeSectionLayout {
    int sectionIndex = 0;
    int itemStart = 0;
    int itemCount = 0;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float headerX = 0.0f;
    float headerY = 0.0f;
    float headerWidth = 0.0f;
    float headerHeight = kHomeHeaderHeight;
};

struct HomeLayoutResult {
    float contentX = 0.0f;
    float contentY = 0.0f;
    float contentWidth = 0.0f;
    float contentHeight = 0.0f;   // grows to fit all sections; may exceed viewportHeight (scroll)
    int columnCount = 1;          // 1 or 2
    // ui-v3 home 6: the responsive classification of this layout's content
    // width (single-pane semantics; the seam already sees per-pane width).
    // Informational for consumers -- e.g. the renderer drops Caption-size
    // count hints in Narrow -- and never affects geometry.
    UiResponsiveState responsiveState = UiResponsiveState::Normal;
    std::vector<HomeSectionLayout> sections;
    std::vector<HomeItemLayout> items;
    int totalItemCount = 0;
};

// quick-access-home 3.1: compute the Home surface layout for the given input.
// Guarantees (for every input): no two item rectangles overlap; every item
// rectangle lies fully inside the content rect; items are emitted in ascending
// global index order (top-to-bottom, then left-to-right). A zero-item section
// produces no items and no header and disappears entirely -- except the quick
// actions section, which is present whenever `showActions` is true.
inline HomeLayoutResult ComputeHomeLayout(const HomeLayoutInput& input) noexcept {
    HomeLayoutResult result;

    // viewportHeight is intentionally not used here: the Home surface scrolls
    // vertically, so content height is derived from the laid-out sections
    // rather than clamped to the viewport (see contentHeight below).
    const float viewportWidth = (std::max)(0.0f, input.viewportWidth);

    // Content rect: horizontal extent is viewport-minus-margins (and capped in
    // WIDE mode); vertical extent grows downward to contain every section so a
    // long list scrolls instead of clipping. The horizontal bounds are the real
    // "clipping bounds" consumers must honor.
    float contentWidth;
    if (viewportWidth >= kHomeWideMinWidth) {
        contentWidth = (std::min)(viewportWidth - 2.0f * kHomeMargin, kHomeMaxContentWidth);
    } else {
        contentWidth = viewportWidth - 2.0f * kHomeMargin;
    }
    contentWidth = (std::max)(0.0f, contentWidth);

    const float contentX = kHomeMargin;
    const float contentY = kHomeMargin;

    // NARROW (< 480) is always one column. NORMAL / WIDE use two columns only
    // when two columns of at least kHomeMinColumnWidth each fit with spacing.
    const int columnCount = (viewportWidth < kHomeNarrowMaxWidth)
        ? 1
        : (contentWidth >= 2.0f * kHomeMinColumnWidth + kHomeSpaceS ? 2 : 1);

    result.contentX = contentX;
    result.contentY = contentY;
    result.contentWidth = contentWidth;
    result.columnCount = columnCount;
    // ui-v3 home 6: classify once so consumers agree on the responsive state
    // (single-pane semantics -- the seam already sees the effective width).
    result.responsiveState = ClassifyContentWidth(contentWidth, false);

    // Defensive clamping: negative counts behave like zero; each count is also
    // capped per-section, then a global budget enforces the total cap so the
    // flat list never exceeds kHomeMaxItems.
    auto clampCount = [](int c) -> int {
        if (c < 0) c = 0;
        if (c > kHomeMaxItems) c = kHomeMaxItems;
        return c;
    };

    struct SectionDef {
        int index;
        int count;
    };
    SectionDef defs[5] = {
        {static_cast<int>(HomeSectionKind::Pinned), clampCount(input.pinnedCount)},
        {static_cast<int>(HomeSectionKind::RecentFolders), clampCount(input.recentFolderCount)},
        {static_cast<int>(HomeSectionKind::RecentSearches), clampCount(input.recentSearchCount)},
        {static_cast<int>(HomeSectionKind::CommonLocations), clampCount(input.knownFolderCount)},
        {static_cast<int>(HomeSectionKind::QuickActions), input.showActions ? kHomeActionCount : 0},
    };

    int budget = kHomeMaxItems;
    for (auto& def : defs) {
        const int allotted = (std::min)(def.count, budget);
        def.count = (std::max)(0, allotted);
        budget -= def.count;
    }

    const float colGap = kHomeSpaceS;
    const float colWidth = (columnCount == 1)
        ? contentWidth
        : (contentWidth - colGap) / 2.0f;

    float currentY = contentY;
    float contentBottom = contentY;
    int globalIndex = 0;

    for (const auto& def : defs) {
        if (def.count == 0) {
            continue; // zero-item section disappears entirely (header included)
        }

        HomeSectionLayout section;
        section.sectionIndex = def.index;
        section.itemStart = static_cast<int>(result.items.size());
        section.itemCount = def.count; // the section owns items [itemStart, itemStart + itemCount)
        section.x = contentX;
        section.y = currentY;
        section.width = contentWidth;
        section.headerX = contentX;
        section.headerY = currentY;
        section.headerWidth = contentWidth;
        section.headerHeight = kHomeHeaderHeight;

        const float itemsTop = currentY + kHomeHeaderHeight;
        // ui-v3 home 3: row height follows the section's content model
        // (two-line destination rows vs compact utility rows), not a single
        // surface-wide constant.
        const float itemHeight = HomeSectionItemHeight(static_cast<HomeSectionKind>(def.index), columnCount);

        for (int i = 0; i < def.count; ++i) {
            const int col = i % columnCount;
            const int row = i / columnCount;
            HomeItemLayout item;
            item.sectionIndex = def.index;
            item.indexInSection = i;
            item.globalIndex = globalIndex++;
            item.x = contentX + static_cast<float>(col) * (colWidth + colGap);
            item.y = itemsTop + static_cast<float>(row) * (itemHeight + kHomeSpaceS);
            item.width = colWidth;
            item.height = itemHeight;
            // ui-v3 corner-radius contract: rows kRadiusSmall, cards kRadiusMedium.
            item.radius = columnCount == 2 ? kHomeRadiusMedium : kHomeRadiusSmall;
            result.items.push_back(item);
        }

        const int rowCount = (def.count + columnCount - 1) / columnCount;
        const float itemsBottom = itemsTop +
            static_cast<float>(rowCount) * itemHeight +
            static_cast<float>(rowCount > 0 ? rowCount - 1 : 0) * kHomeSpaceS;

        section.height = itemsBottom - currentY;
        result.sections.push_back(section);

        currentY = itemsBottom + kHomeSectionGap;
        contentBottom = itemsBottom;
    }

    result.totalItemCount = globalIndex;
    result.contentHeight = (std::max)(0.0f, contentBottom - contentY);
    return result;
}

} // namespace ffui
