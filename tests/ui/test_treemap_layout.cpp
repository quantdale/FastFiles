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
#include "../TestSupport.h"

using namespace fftest;

namespace {

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
    constexpr float kEps = 0.01f;
    return rect.x >= x - kEps && rect.y >= y - kEps &&
           rect.x + rect.width <= x + width + kEps &&
           rect.y + rect.height <= y + height + kEps;
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

void TestClientHitTestDoesNotRequireHoverEvent() {
    using namespace ffui;
    std::vector<std::unique_ptr<TreemapNode>> owned;
    TreemapNode* tile = MakeNode(1, L"tile", owned);
    TreemapRect rect;
    rect.x = 0.0f;
    rect.y = 0.0f;
    rect.width = 100.0f;
    rect.height = 100.0f;
    rect.node = tile;
    const std::vector<TreemapRect> layout{rect};

    // The shell paints an 800x600 viewport translated by (120,80) DIPs. A
    // direct click at the translated center must resolve even when no hover
    // event was delivered first.
    Check(HitTestClientPoint(layout, 520.0f, 380.0f, 120.0f, 80.0f, 800.0f, 600.0f) == tile,
          "client click hit-testing resolves a tile without prior hover");
    Check(HitTestClientPoint(layout, 80.0f, 80.0f, 120.0f, 80.0f, 800.0f, 600.0f) == nullptr,
          "client click outside the translated treemap returns no tile");
}

// semantic-treemap-uia 34.x: proportional 8:4:2 layout test.
void TestProportional842() {
    using namespace ffui;
    std::vector<std::unique_ptr<TreemapNode>> owned;
    std::vector<TreemapNode*> nodes;
    nodes.push_back(MakeNode(80, L"large", owned));
    nodes.push_back(MakeNode(40, L"medium", owned));
    nodes.push_back(MakeNode(20, L"small", owned));

    std::vector<TreemapRect> rects;
    LayoutTreemap(rects, 0.0f, 0.0f, 100.0f, 100.0f, nodes, 0);
    Check(rects.size() == 3, "8:4:2 yields 3 rects");

    bool noOverlap = true;
    for (size_t i = 0; i < rects.size(); ++i) {
        if (!RectWithinBounds(rects[i], 0.0f, 0.0f, 100.0f, 100.0f)) noOverlap = false;
        for (size_t j = i + 1; j < rects.size(); ++j) {
            if (RectsOverlap(rects[i], rects[j])) noOverlap = false;
        }
    }
    Check(noOverlap, "8:4:2: no overlap, all in bounds");

    // Area ratios approximately 8:4:2 (i.e. 4:2:1).
    float totalArea = 0.0f;
    for (const auto& r : rects) totalArea += r.width * r.height;
    Check(NearlyEqual(totalArea, 10000.0f), "8:4:2: full area covered");

    for (const auto* node : nodes) {
        for (const auto& rect : rects) {
            if (rect.node == node) {
                const float expected = static_cast<float>(node->totalSizeBytes) / 140.0f * 10000.0f;
                Check(NearlyEqual(rect.width * rect.height, expected),
                      "8:4:2: rect area matches node's share");
            }
        }
    }

    // Hit-test each rect's center returns that node.
    for (const auto& rect : rects) {
        const float cx = rect.x + rect.width * 0.5f;
        const float cy = rect.y + rect.height * 0.5f;
        Check(HitTest(rects, cx, cy) == rect.node, "center hit-test returns owning node");
    }
}

// Different viewport shapes produce valid non-overlapping layouts.
void TestViewportShapes() {
    using namespace ffui;
    const uint64_t sizes[] = {50, 30, 20};

    auto testViewport = [&](float w, float h, const char* label) {
        std::vector<std::unique_ptr<TreemapNode>> owned;
        std::vector<TreemapNode*> nodes;
        for (int i = 0; i < 3; ++i) {
            wchar_t name[8];
            swprintf(name, 8, L"n%d", i);
            nodes.push_back(MakeNode(sizes[i], name, owned));
        }
        std::vector<TreemapRect> rects;
        LayoutTreemap(rects, 0.0f, 0.0f, w, h, nodes, 0);

        std::string msg(label);
        Check(rects.size() == 3, (msg + ": 3 rects").c_str());

        bool ok = true;
        for (size_t i = 0; i < rects.size(); ++i) {
            if (!RectWithinBounds(rects[i], 0.0f, 0.0f, w, h)) ok = false;
            for (size_t j = i + 1; j < rects.size(); ++j) {
                if (RectsOverlap(rects[i], rects[j])) ok = false;
            }
        }
        Check(ok, (msg + ": no overlap in bounds").c_str());
    };

    testViewport(100.0f, 100.0f, "square");
    testViewport(200.0f, 50.0f, "wide");
    testViewport(50.0f, 200.0f, "tall");
}

// Hundreds of tiny files produce a single catch-all tile (no overlapping rects).
void TestManyTinyFilesCatchAll() {
    using namespace ffui;
    std::vector<std::unique_ptr<TreemapNode>> owned;
    std::vector<TreemapNode*> nodes;
    // 1 large + 10 medium + 100 tiny (all below tinyShare threshold at 100x100).
    nodes.push_back(MakeNode(8000, L"big", owned));
    for (int i = 0; i < 10; ++i) {
        wchar_t name[16];
        swprintf(name, 16, L"med%d", i);
        nodes.push_back(MakeNode(100, name, owned));
    }
    for (int i = 0; i < 100; ++i) {
        wchar_t name[16];
        swprintf(name, 16, L"tiny%d", i);
        nodes.push_back(MakeNode(1, name, owned));
    }

    std::vector<std::unique_ptr<TreemapNode>> synthStorage;
    std::vector<TreemapRect> rects;
    LayoutTreemap(rects, 0.0f, 0.0f, 100.0f, 100.0f, nodes, 0, &synthStorage);

    // Expect ~12 rects (1 large + 10 medium + 1 catch-all) not 111.
    Check(rects.size() <= 14, "catch-all merges many tiny nodes");

    bool noOverlap = true;
    float totalArea = 0.0f;
    for (size_t i = 0; i < rects.size(); ++i) {
        if (!RectWithinBounds(rects[i], 0.0f, 0.0f, 100.0f, 100.0f)) noOverlap = false;
        totalArea += rects[i].width * rects[i].height;
        for (size_t j = i + 1; j < rects.size(); ++j) {
            if (RectsOverlap(rects[i], rects[j])) noOverlap = false;
        }
    }
    Check(noOverlap, "catch-all: no overlap, all in bounds");
    Check(NearlyEqual(totalArea, 10000.0f), "catch-all: full area covered");

    // At least one synthetic node was created.
    Check(!synthStorage.empty(), "catch-all: synthetic node exists");

    // Synthetic node aggregates all 100 tiny + 10 medium files.
    const auto* synth = synthStorage.back().get();
    Check(synth->synthetic, "synthetic node is marked synthetic");
    Check(synth->aggregateItemCount == 110, "synthetic aggregates all tiny+medium");
}

// All same-size tiny files -> catch-all or single honest sub-minimum tile.
void TestAllSameTiny() {
    using namespace ffui;
    std::vector<std::unique_ptr<TreemapNode>> owned;
    std::vector<TreemapNode*> nodes;
    for (int i = 0; i < 300; ++i) {
        wchar_t name[16];
        swprintf(name, 16, L"f%d", i);
        nodes.push_back(MakeNode(1, name, owned));
    }

    std::vector<std::unique_ptr<TreemapNode>> synthStorage;
    std::vector<TreemapRect> rects;
    LayoutTreemap(rects, 0.0f, 0.0f, 100.0f, 100.0f, nodes, 0, &synthStorage);

    // With 300 same-tiny nodes, expect 1 rect (catch-all) or at most a few.
    Check(rects.size() <= 2, "all-same-tiny yields 1-2 rects");

    // No overlap.
    for (size_t i = 0; i < rects.size(); ++i) {
        for (size_t j = i + 1; j < rects.size(); ++j) {
            Check(!RectsOverlap(rects[i], rects[j]), "all-same-tiny: no overlap");
        }
    }
}

// Center hit-test returns owning node for every rect in any layout.
void TestCenterHitTestSelfConsistency() {
    using namespace ffui;
    std::vector<std::unique_ptr<TreemapNode>> owned;
    std::vector<TreemapNode*> nodes;
    nodes.push_back(MakeNode(40, L"a", owned));
    nodes.push_back(MakeNode(30, L"b", owned));
    nodes.push_back(MakeNode(20, L"c", owned));
    nodes.push_back(MakeNode(10, L"d", owned));

    std::vector<TreemapRect> rects;
    LayoutTreemap(rects, 0.0f, 0.0f, 100.0f, 100.0f, nodes, 0);

    for (const auto& rect : rects) {
        const float cx = rect.x + rect.width * 0.5f;
        const float cy = rect.y + rect.height * 0.5f;
        Check(HitTest(rects, cx, cy) == rect.node, "center hit-test self-consistent");
    }
}

// Calculating nodes get a minimum-size placeholder so they remain visible.
void TestCalculatingNodePlaceholder() {
    using namespace ffui;
    std::vector<std::unique_ptr<TreemapNode>> owned;
    // One known large node + one calculating node with zero size.
    auto* large = MakeNode(90, L"known", owned);
    auto calc = std::make_unique<TreemapNode>();
    calc->name = L"calculating";
    calc->calculating = true;
    calc->totalSizeBytes = 0;
    owned.push_back(std::move(calc));
    auto* calcNode = owned.back().get();

    std::vector<TreemapNode*> nodes;
    nodes.push_back(large);
    nodes.push_back(calcNode);

    std::vector<TreemapRect> rects;
    LayoutTreemap(rects, 0.0f, 0.0f, 100.0f, 100.0f, nodes, 0);

    Check(rects.size() == 2, "calculating node gets a rect");

    // The calculating node should have a visible rect (min-share placeholder).
    bool calcHasRect = false;
    for (const auto& r : rects) {
        if (r.node == calcNode) {
            calcHasRect = true;
            Check(r.width > 0.0f && r.height > 0.0f,
                  "calculating node rect has positive dimensions");
        }
    }
    Check(calcHasRect, "calculating node gets a rect in the layout");

    // No overlap.
    for (size_t i = 0; i < rects.size(); ++i) {
        for (size_t j = i + 1; j < rects.size(); ++j) {
            Check(!RectsOverlap(rects[i], rects[j]), "calculating: no overlap");
        }
    }
}

// Single-node layout fills the entire container.
void TestSingleNodeFillsContainer() {
    using namespace ffui;
    std::vector<std::unique_ptr<TreemapNode>> owned;
    std::vector<TreemapNode*> nodes;
    nodes.push_back(MakeNode(100, L"sole", owned));

    std::vector<TreemapRect> rects;
    LayoutTreemap(rects, 5.0f, 10.0f, 90.0f, 80.0f, nodes, 0);

    Check(rects.size() == 1, "single node emits 1 rect");
    Check(NearlyEqual(rects[0].x, 5.0f), "single node x matches");
    Check(NearlyEqual(rects[0].y, 10.0f), "single node y matches");
    Check(NearlyEqual(rects[0].width, 90.0f), "single node width matches");
    Check(NearlyEqual(rects[0].height, 80.0f), "single node height matches");
}

// The catch-all's pending/failed derivation is live, not dead: a failed-only
// membership is known-partial; any pending member keeps it Calculating.
void TestSyntheticCatchAllFailureDerivation() {
    using namespace ffui;
    std::vector<std::unique_ptr<TreemapNode>> owned;

    // Derivation contract, checked directly on BuildSyntheticCatchAll: a
    // pending member keeps the catch-all Calculating; with no pending member,
    // any failure makes it known-partial; the two states are mutually
    // exclusive. (LayoutTreemap itself only merges KNOWN-size tiny members --
    // unknown-size nodes take the placeholder path -- so this contract is
    // exercised at the function level.)
    auto* pending = MakeNode(1, L"pending", owned);
    pending->calculating = true;
    auto* okA = MakeNode(1, L"a", owned);
    auto* okB = MakeNode(1, L"b", owned);
    {
        std::vector<std::unique_ptr<TreemapNode>> storage;
        std::vector<TreemapNode*> tiny{okA, okB, pending};
        BuildSyntheticCatchAll(tiny, storage, 0);
        Check(!storage.empty(), "derivation: catch-all built for pending member");
        if (!storage.empty()) {
            Check(storage.back()->calculating && !storage.back()->aggregateFailed,
                  "pending-member catch-all stays Calculating, not Partial");
        }
    }
    {
        pending->calculating = false;
        pending->aggregateFailed = true;
        std::vector<std::unique_ptr<TreemapNode>> storage;
        std::vector<TreemapNode*> tiny{okA, okB, pending};
        BuildSyntheticCatchAll(tiny, storage, 0);
        Check(!storage.empty(), "derivation: catch-all built for failed member");
        if (!storage.empty()) {
            Check(storage.back()->aggregateFailed && !storage.back()->calculating,
                  "failed-only catch-all is known-partial, never Calculating");
        }
    }

    // Layout level: two known tiny members merge into a RESOLVED catch-all
    // (neither Calculating nor Partial).
    auto* big = MakeNode(1000000, L"big", owned);
    auto* t1 = MakeNode(1, L"t1", owned);
    auto* t2 = MakeNode(1, L"t2", owned);
    std::vector<TreemapNode*> nodes{big, t1, t2};
    std::vector<std::unique_ptr<TreemapNode>> synthStorage;
    std::vector<TreemapRect> rects;
    LayoutTreemap(rects, 0.0f, 0.0f, 100.0f, 100.0f, nodes, 0, &synthStorage);
    Check(!synthStorage.empty(), "catch-all created for tiny members");
    if (!synthStorage.empty()) {
        Check(!synthStorage.back()->calculating && !synthStorage.back()->aggregateFailed,
              "resolved tiny merge is neither Calculating nor Partial");
    }

    // An unknown-size node is laid out via the placeholder path and never
    // merged into a catch-all.
    auto* unknown = MakeNode(1, L"unknown", owned);
    unknown->calculating = true;
    nodes = {big, t1, unknown};
    synthStorage.clear();
    rects.clear();
    LayoutTreemap(rects, 0.0f, 0.0f, 100.0f, 100.0f, nodes, 0, &synthStorage);
    Check(synthStorage.empty(), "unknown-size nodes take the placeholder path, not the catch-all");
}

} // namespace

int main() {
    TestLayoutCoversAreaWithoutOverlap();
    TestHitTest();
    TestClientToNormalized();
    TestClientHitTestDoesNotRequireHoverEvent();
    TestProportional842();
    TestViewportShapes();
    TestManyTinyFilesCatchAll();
    TestAllSameTiny();
    TestCenterHitTestSelfConsistency();
    TestCalculatingNodePlaceholder();
    TestSingleNodeFillsContainer();
    TestSyntheticCatchAllFailureDerivation();
    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
