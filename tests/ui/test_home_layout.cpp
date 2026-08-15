#include "ffui/HomeLayout.h"

#include <cmath>
#include <cstdlib>
#include <vector>

#include "../TestSupport.h"

using namespace fftest;

namespace {

bool NearlyEqual(float left, float right) { return std::fabs(left - right) < 0.001f; }

bool ItemInsideContent(const ffui::HomeItemLayout& item, const ffui::HomeLayoutResult& layout) {
    const float eps = 0.001f;
    return item.x >= layout.contentX - eps &&
           item.y >= layout.contentY - eps &&
           item.x + item.width <= layout.contentX + layout.contentWidth + eps &&
           item.y + item.height <= layout.contentY + layout.contentHeight + eps;
}

bool NoOverlap(const std::vector<ffui::HomeItemLayout>& items) {
    const float eps = 0.001f;
    for (size_t i = 0; i < items.size(); ++i) {
        for (size_t j = i + 1; j < items.size(); ++j) {
            const auto& a = items[i];
            const auto& b = items[j];
            const bool overlaps = a.x < b.x + b.width - eps &&
                                  b.x < a.x + a.width - eps &&
                                  a.y < b.y + b.height - eps &&
                                  b.y < a.y + a.height - eps;
            if (overlaps) return false;
        }
    }
    return true;
}

// A rectangle is "before" another in reading order if it is strictly above it,
// or on the same row and not to its right. Global index order must match this.
bool ReadingOrderOk(const std::vector<ffui::HomeItemLayout>& items) {
    const float eps = 0.001f;
    for (size_t i = 1; i < items.size(); ++i) {
        const auto& prev = items[i - 1];
        const auto& cur = items[i];
        const bool prevBefore = (prev.y < cur.y - eps) ||
                                (std::fabs(prev.y - cur.y) <= eps && prev.x <= cur.x + eps);
        if (!prevBefore) return false;
    }
    return true;
}

void TestNarrowFullWidthRows() {
    ffui::HomeLayoutInput input;
    input.viewportWidth = 400.0f;
    input.viewportHeight = 600.0f;
    input.pinnedCount = 3;
    input.recentFolderCount = 5;
    input.recentSearchCount = 4;
    input.knownFolderCount = 6;
    input.showActions = true;
    const auto layout = ffui::ComputeHomeLayout(input);

    Check(layout.columnCount == 1, "narrow viewport (< 480) uses a single column");
    Check(NearlyEqual(layout.contentWidth, 400.0f - 2.0f * ffui::kHomeMargin),
          "narrow content width is the viewport minus standard margins");
    bool allFullWidth = true;
    for (const auto& item : layout.items) {
        allFullWidth = allFullWidth &&
            NearlyEqual(item.width, layout.contentWidth) &&
            NearlyEqual(item.x, layout.contentX);
    }
    Check(allFullWidth, "narrow items are full-width rows starting at the content origin");
    Check(NoOverlap(layout.items), "narrow layout has no overlapping item rectangles");
    Check(ReadingOrderOk(layout.items), "narrow items are in ascending global reading order");
}

void TestNormalOneOrTwoColumns() {
    ffui::HomeLayoutInput input;
    input.viewportWidth = 800.0f;
    input.viewportHeight = 600.0f;
    input.pinnedCount = 6;
    input.recentFolderCount = 8;
    input.showActions = false;
    const auto layout = ffui::ComputeHomeLayout(input);

    Check(layout.columnCount == 1 || layout.columnCount == 2,
          "normal viewport (480..1100) uses one or two columns");
    Check(layout.columnCount == 2, "800 DIP fits two >= 220 DIP columns, so two columns are used");
    Check(NoOverlap(layout.items), "normal layout has no overlapping item rectangles");
    Check(ReadingOrderOk(layout.items), "normal items are in ascending global reading order");
}

void TestWideContentCapped() {
    auto checkWide = [](float w, float h) {
        ffui::HomeLayoutInput input;
        input.viewportWidth = w;
        input.viewportHeight = h;
        input.pinnedCount = 4;
        input.recentFolderCount = 6;
        input.recentSearchCount = 10;
        input.knownFolderCount = 5;
        input.showActions = true;
        const auto layout = ffui::ComputeHomeLayout(input);
        Check(layout.contentWidth <= ffui::kHomeMaxContentWidth,
              "wide content width never exceeds kHomeMaxContentWidth (960)");
        Check(NearlyEqual(layout.contentWidth, ffui::kHomeMaxContentWidth),
              "wide viewport allows the full 960 DIP content width");
        Check(layout.contentX + layout.contentWidth <= w + 0.001f,
              "wide content stays within the viewport (left-aligned, never full span)");
        bool inside = true;
        for (const auto& item : layout.items) {
            inside = inside && ItemInsideContent(item, layout);
        }
        Check(inside, "wide items do not span beyond the capped content width");
        Check(NoOverlap(layout.items), "wide layout has no overlapping item rectangles");
    };
    checkWide(1920.0f, 1080.0f);
    checkWide(2560.0f, 1440.0f);
}

void TestHighDpiViewport() {
    // 1280x720 DIP is the DPI-independent equivalent of a 1920x1080 physical
    // monitor at 150% scale: physical / 1.5 == 1280x720 DIP. Because the
    // layout works purely in DIPs (the renderer applies scale on its D2D
    // context), the same invariants must hold as at any other 1280x720 DIP
    // viewport -- DPI scaling is invisible to this seam.
    ffui::HomeLayoutInput input;
    input.viewportWidth = 1280.0f;
    input.viewportHeight = 720.0f;
    input.pinnedCount = 5;
    input.recentFolderCount = 7;
    input.recentSearchCount = 12;
    input.knownFolderCount = 8;
    input.showActions = true;
    const auto layout = ffui::ComputeHomeLayout(input);

    Check(layout.columnCount == 2, "150%-DPI representative viewport uses two columns");
    bool inside = true;
    for (const auto& item : layout.items) {
        inside = inside && ItemInsideContent(item, layout);
    }
    Check(inside, "150%-DPI items lie fully inside the content rect");
    Check(NoOverlap(layout.items), "150%-DPI layout has no overlapping item rectangles");
    Check(ReadingOrderOk(layout.items), "150%-DPI items are in ascending global reading order");
}

void TestZeroItemSections() {
    ffui::HomeLayoutInput input;
    input.viewportWidth = 1200.0f;
    input.viewportHeight = 800.0f;
    input.pinnedCount = 0;
    input.recentFolderCount = 0;
    input.recentSearchCount = 0;
    input.knownFolderCount = 0;
    input.showActions = false;
    const auto layout = ffui::ComputeHomeLayout(input);
    Check(layout.totalItemCount == 0 && layout.items.empty(),
          "all-zero counts with actions hidden produce an empty result");
    Check(layout.sections.empty(), "zero-item sections contribute no section headers");
}

void TestOnlyActionsStrip() {
    ffui::HomeLayoutInput input;
    input.viewportWidth = 1000.0f;
    input.viewportHeight = 700.0f;
    input.pinnedCount = 0;
    input.recentFolderCount = 0;
    input.recentSearchCount = 0;
    input.knownFolderCount = 0;
    input.showActions = true;
    const auto layout = ffui::ComputeHomeLayout(input);
    Check(layout.totalItemCount == ffui::kHomeActionCount,
          "actions-only layout still emits the fixed quick-action items");
    Check(layout.sections.size() == 1 &&
              layout.sections.front().sectionIndex ==
                  static_cast<int>(ffui::HomeSectionKind::QuickActions),
          "the only present section is the Quick Actions section");
    Check(NoOverlap(layout.items), "actions-only layout has no overlapping item rectangles");
}

void TestLargeBoundedCounts() {
    ffui::HomeLayoutInput input;
    input.viewportWidth = 1920.0f;
    input.viewportHeight = 1080.0f;
    input.pinnedCount = 50;
    input.recentFolderCount = 10;
    input.recentSearchCount = 100;
    input.knownFolderCount = 6;
    input.showActions = true;
    const auto layout = ffui::ComputeHomeLayout(input);
    const int expected = 50 + 10 + 100 + 6 + ffui::kHomeActionCount;
    Check(layout.totalItemCount == expected, "large bounded counts produce the correct total");
    Check(layout.totalItemCount <= ffui::kHomeMaxItems,
          "large bounded counts stay within the defensive total cap");
    bool inside = true;
    for (const auto& item : layout.items) {
        inside = inside && ItemInsideContent(item, layout);
    }
    Check(inside, "large-count items all lie inside the content rect");
    Check(NoOverlap(layout.items), "large-count layout has no overlapping item rectangles");
    Check(ReadingOrderOk(layout.items), "large-count items are in ascending global order");
}

void TestSectionOrder() {
    ffui::HomeLayoutInput input;
    input.viewportWidth = 1400.0f;
    input.viewportHeight = 900.0f;
    input.pinnedCount = 3;
    input.recentFolderCount = 3;
    input.recentSearchCount = 3;
    input.knownFolderCount = 3;
    input.showActions = true;
    const auto layout = ffui::ComputeHomeLayout(input);
    Check(layout.sections.size() == 5, "all five sections are present when all have items/flags");
    bool ordered = true;
    for (size_t i = 0; i < layout.sections.size(); ++i) {
        ordered = ordered && (layout.sections[i].sectionIndex == static_cast<int>(i));
    }
    Check(ordered, "sections appear in order Pinned, Recent folders, Recent searches, "
                   "Common locations, Quick actions");
}

void TestClampingAndZeroViewport() {
    // Negative counts must behave like zero.
    ffui::HomeLayoutInput neg;
    neg.viewportWidth = 800.0f;
    neg.viewportHeight = 600.0f;
    neg.pinnedCount = -5;
    neg.recentFolderCount = -3;
    neg.recentSearchCount = -100;
    neg.knownFolderCount = -1;
    neg.showActions = false;
    const auto negLayout = ffui::ComputeHomeLayout(neg);
    Check(negLayout.totalItemCount == 0, "negative counts clamp to zero");

    // A zero-size viewport must return a valid, empty, throw-free result.
    ffui::HomeLayoutInput zero;
    zero.viewportWidth = 0.0f;
    zero.viewportHeight = 0.0f;
    zero.showActions = false;
    const auto zeroLayout = ffui::ComputeHomeLayout(zero);
    Check(zeroLayout.totalItemCount == 0 &&
              zeroLayout.contentWidth == 0.0f &&
              zeroLayout.items.empty(),
          "zero-size viewport returns a valid empty result without asserting");

    // Counts above the per-section clamp and a runaway total are bounded.
    ffui::HomeLayoutInput big;
    big.viewportWidth = 1920.0f;
    big.viewportHeight = 1080.0f;
    big.pinnedCount = 1000;
    big.recentFolderCount = 1000;
    big.recentSearchCount = 1000;
    big.knownFolderCount = 1000;
    big.showActions = true;
    const auto bigLayout = ffui::ComputeHomeLayout(big);
    Check(bigLayout.totalItemCount <= ffui::kHomeMaxItems,
          "extreme counts are bounded by the defensive total cap");
    Check(NoOverlap(bigLayout.items), "clamped extreme counts still produce no overlap");
}

void TestNoOverlapSweep() {
    const float widths[] = {320.0f, 479.0f, 480.0f, 799.0f, 1100.0f, 1920.0f};
    const float heights[] = {400.0f, 800.0f};
    for (const float w : widths) {
        for (const float h : heights) {
            ffui::HomeLayoutInput input;
            input.viewportWidth = w;
            input.viewportHeight = h;
            input.pinnedCount = 7;
            input.recentFolderCount = 9;
            input.recentSearchCount = 12;
            input.knownFolderCount = 5;
            input.showActions = true;
            const auto layout = ffui::ComputeHomeLayout(input);
            bool inside = true;
            for (const auto& item : layout.items) {
                inside = inside && ItemInsideContent(item, layout);
            }
            Check(inside, "sweep width/height items lie fully inside the content rect");
            Check(NoOverlap(layout.items), "sweep width/height items never overlap");
            Check(ReadingOrderOk(layout.items), "sweep items are in ascending global order");
        }
    }
    // Boundary assertions documented by the width set above.
    const int narrowWidth = ffui::ComputeHomeLayout(
        ffui::HomeLayoutInput{479.0f, 600.0f, 1, 1, 1, 1, false}).columnCount;
    const int wideWidth = ffui::ComputeHomeLayout(
        ffui::HomeLayoutInput{480.0f, 600.0f, 1, 1, 1, 1, false}).columnCount;
    Check(narrowWidth == 1 && wideWidth == 2,
          "479 DIP is single column, 480 DIP is two columns (matching the rule)");
}

} // namespace

int main() {
    TestNarrowFullWidthRows();
    TestNormalOneOrTwoColumns();
    TestWideContentCapped();
    TestHighDpiViewport();
    TestZeroItemSections();
    TestOnlyActionsStrip();
    TestLargeBoundedCounts();
    TestSectionOrder();
    TestClampingAndZeroViewport();
    TestNoOverlapSweep();
    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
