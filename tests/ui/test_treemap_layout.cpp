// Pure treemap layout / hit-testing tests. The treemap math lives in
// TreemapLayout.h with no Direct2D dependency, so it is unit-testable without
// a live renderer; these guard the layout, hit-testing, and the
// client-to-normalized coordinate transform (the H4 hit-testing bug fix).

#include "ffui/TreemapLayout.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <memory>
#include <vector>

namespace {
int failures = 0;
void Check(bool value, const char* text) { if (!value) { std::fprintf(stderr, "FAIL: %s\n", text); ++failures; } }

bool NearlyEqual(float a, float b) { return std::fabs(a - b) < 0.001f; }

ffui::TreemapNode* MakeNode(uint64_t sizeBytes, const wchar_t* name,
                            std::vector<std::unique_ptr<ffui::TreemapNode>>& owned) {
    auto node = std::make_unique<ffui::TreemapNode>();
    node->name = name;
    node->totalSizeBytes = sizeBytes;
    owned.push_back(std::move(node));
    return owned.back().get();
}

bool RectWithinBounds(const ffui::TreemapRect& rect, float x, float y, float width, float height) {
    return rect.x >= x && rect.y >= y && rect.x + rect.width <= x + width && rect.y + rect.height <= y + height;
}

bool RectsOverlap(const ffui::TreemapRect& a, const ffui::TreemapRect& b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

void TestLayoutCoversAreaWithoutOverlap() {
    using namespace ffui;
    std::vector<std::unique_ptr<TreemapNode>> owned;
    std::vector<TreemapNode*> nodes;
    const uint64_t sizes[4] = {40, 30, 20, 10};
    for (int i = 0; i < 4; ++i) {
        wchar_t name[8];
        swprintf(name, 8, L"n%d", i);
        nodes.push_back(MakeNode(sizes[i], name, owned));
    }

    std::vector<TreemapRect> rects;
    LayoutTreemap(rects, 0.0f, 0.0f, 100.0f, 100.0f, nodes, 0);

    Check(rects.size() == 4, "layout emits one rect per node");

    bool inBoundsAndDistinct = true;
    for (size_t i = 0; i < rects.size(); ++i) {
        if (!RectWithinBounds(rects[i], 0.0f, 0.0f, 100.0f, 100.0f)) inBoundsAndDistinct = false;
        for (size_t j = i + 1; j < rects.size(); ++j) {
            if (rects[i].node == rects[j].node) inBoundsAndDistinct = false;
        }
    }
    Check(inBoundsAndDistinct, "every rect stays in bounds and owns a distinct node");

    bool overlap = false;
    for (size_t i = 0; i < rects.size(); ++i) {
        for (size_t j = i + 1; j < rects.size(); ++j) {
            if (RectsOverlap(rects[i], rects[j])) overlap = true;
        }
    }
    Check(!overlap, "layout rects do not overlap");

    float totalArea = 0.0f;
    for (const auto& rect : rects) totalArea += rect.width * rect.height;
    Check(NearlyEqual(totalArea, 100.0f * 100.0f), "layout rects tile the full area");

    bool proportional = true;
    for (const auto* node : nodes) {
        for (const auto& rect : rects) {
            if (rect.node == node) {
                const float expected = static_cast<float>(node->totalSizeBytes) / 100.0f * (100.0f * 100.0f);
                if (!NearlyEqual(rect.width * rect.height, expected)) proportional = false;
            }
        }
    }
    Check(proportional, "rect areas are proportional to node sizes");
}

void TestHitTest() {
    using namespace ffui;
    std::vector<std::unique_ptr<TreemapNode>> owned;
    TreemapNode* left = MakeNode(1, L"left", owned);
    TreemapNode* right = MakeNode(1, L"right", owned);

    TreemapRect leftRect;
    leftRect.x = 0.0f; leftRect.y = 0.0f; leftRect.width = 50.0f; leftRect.height = 100.0f; leftRect.node = left;
    TreemapRect rightRect;
    rightRect.x = 50.0f; rightRect.y = 0.0f; rightRect.width = 50.0f; rightRect.height = 100.0f; rightRect.node = right;
    const std::vector<TreemapRect> layout{leftRect, rightRect};

    Check(HitTest(layout, 25.0f, 50.0f) == left, "point inside the left rect returns its node");
    Check(HitTest(layout, 75.0f, 50.0f) == right, "point inside the right rect returns its node");
    Check(HitTest(layout, 50.0f, 50.0f) == right, "point on the shared edge hits the right rect");
    Check(HitTest(layout, 150.0f, 50.0f) == nullptr, "point far to the right of the area returns null");
    Check(HitTest(layout, 25.0f, 150.0f) == nullptr, "point below the area returns null");
}

void TestClientToNormalized() {
    using namespace ffui;
    // Viewport 800x600 under offset (120, 80): scaleX = 800/100 = 8,
    // scaleY = 600/100 = 6. Client (520, 260) is normalized (50, 30).
    const auto mapped = TreemapClientToNormalized(520.0f, 260.0f, 120.0f, 80.0f, 800.0f, 600.0f);
    Check(NearlyEqual(mapped.first, 50.0f) && NearlyEqual(mapped.second, 30.0f),
          "offset+scale client point maps back to normalized treemap coords");

    const auto offsetCorner = TreemapClientToNormalized(120.0f, 80.0f, 120.0f, 80.0f, 800.0f, 600.0f);
    Check(NearlyEqual(offsetCorner.first, 0.0f) && NearlyEqual(offsetCorner.second, 0.0f),
          "client point at the offset origin maps to normalized (0, 0)");

    const auto fractional = TreemapClientToNormalized(120.0f + 8.0f * 12.5f, 80.0f + 6.0f * 33.25f,
                                                      120.0f, 80.0f, 800.0f, 600.0f);
    Check(NearlyEqual(fractional.first, 12.5f) && NearlyEqual(fractional.second, 33.25f),
          "fractional normalized coords survive the client round trip");
}
} // namespace

int main() {
    TestLayoutCoversAreaWithoutOverlap();
    TestHitTest();
    TestClientToNormalized();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
