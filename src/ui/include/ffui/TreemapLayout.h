#pragma once

// Pure treemap layout and hit-testing math, free of any Direct2D dependency so
// it can be unit-tested in isolation. The treemap is laid out in normalized
// 0..100 space; the shell applies a Translate(sidebar, chrome) transform and a
// viewport/100 scale when drawing, so TreemapClientToNormalized inverts that
// before hit-testing (see TreemapView::HandleMouseMove).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace ffui {

constexpr float kMinRectSize = 4.0f;

struct TreemapNode {
    std::wstring name;
    std::wstring path;
    bool isDirectory = false;
    // semantic-treemap-uia 5.x: a synthetic catch-all node that aggregates
    // tiny siblings instead of one filesystem object. Its name is
    // "Other small items", its path is empty (never navigable), and its
    // size/allocated fields are the merged sums of its members.
    bool synthetic = false;
    uint64_t aggregateItemCount = 0;
    uint64_t sizeBytes = 0;
    uint64_t totalSizeBytes = 0;
    // allocated-size-storage-accuracy 7.2: the node's own size-on-disk and,
    // for directories, the subtree's allocated aggregate with its explicit
    // completeness pair. Unknown is never rendered as 0 B.
    uint64_t allocatedSizeBytes = 0;
    bool allocatedSizeKnown = false;
    uint64_t totalAllocatedSizeBytes = 0;
    uint64_t allocatedKnownCount = 0;
    uint64_t allocatedTotalCount = 0;
    bool calculating = false;
    bool aggregateResolved = false;
    bool aggregateFailed = false;
    int64_t volumeRowId = 0;
    uint64_t fileIdLow = 0;
    uint64_t fileIdHigh = 0;
    int depth = 0;
    TreemapNode* parent = nullptr;
    std::vector<std::unique_ptr<TreemapNode>> children;
};

struct TreemapRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    TreemapNode* node = nullptr;
};

inline float AspectRatio(float side1, float side2) {
    if (side1 < 0.001f || side2 < 0.001f) return 1.0f;
    return std::max(side1 / side2, side2 / side1);
}

inline void Squarify(std::vector<TreemapRect>& rects, float x, float y, float width, float height,
                     const std::vector<TreemapNode*>& nodes,
                     const std::vector<uint64_t>& effectiveSizes, int depth);

// True when a node with this size share of the container would fall below the
// minimum readable rectangle dimension (kMinRectSize along the short side).
// Deterministic per container size; the padding-inset viewport is the same
// container the tiles are drawn in, so the threshold always matches render.
inline float TinyNodeShare(float width, float height) {
    const float shortSide = std::min(width, height);
    return (shortSide < 1.0f) ? 0.0f : kMinRectSize / shortSide;
}

// Builds the synthetic "Other small items" catch-all node aggregating `tiny`
// members. The caller owns the node via `syntheticStorage` (cleared on every
// rebuild), so the node's lifetime outlives the layout and the semantic
// projection/hover can read its merged metadata.
inline TreemapNode* BuildSyntheticCatchAll(const std::vector<TreemapNode*>& tiny,
                                           std::vector<std::unique_ptr<TreemapNode>>& syntheticStorage,
                                           int depth) {
    auto synthetic = std::make_unique<TreemapNode>();
    synthetic->name = L"Other small items";
    synthetic->synthetic = true;
    synthetic->aggregateItemCount = tiny.size();
    synthetic->depth = depth;
    synthetic->isDirectory = false; // not one filesystem object; never navigable
    if (!tiny.empty()) synthetic->parent = tiny.front()->parent;

    uint64_t total = 0;
    bool incomplete = false;
    for (const TreemapNode* member : tiny) {
        if (UINT64_MAX - total < member->totalSizeBytes) {
            total = UINT64_MAX;
        } else {
            total += member->totalSizeBytes;
        }
        incomplete = incomplete || member->calculating || member->aggregateFailed;
        // Merged allocated aggregate: files contribute their own known value
        // and a 1/1 completeness pair; directories contribute their subtree
        // pair. Unknown members never contribute bytes, only totals.
        if (member->isDirectory) {
            synthetic->totalAllocatedSizeBytes += member->totalAllocatedSizeBytes;
            synthetic->allocatedKnownCount += member->allocatedKnownCount;
            synthetic->allocatedTotalCount += member->allocatedTotalCount;
        } else {
            if (member->allocatedSizeKnown) synthetic->totalAllocatedSizeBytes += member->allocatedSizeBytes;
            synthetic->allocatedKnownCount += member->allocatedSizeKnown ? 1 : 0;
            synthetic->allocatedTotalCount += 1;
        }
    }
    synthetic->totalSizeBytes = total;
    synthetic->sizeBytes = total;
    synthetic->calculating = incomplete;
    synthetic->aggregateFailed = !incomplete && std::any_of(tiny.begin(), tiny.end(),
                                                            [](const TreemapNode* n) { return n->aggregateFailed; });
    synthetic->totalAllocatedSizeBytes =
        std::min(synthetic->totalAllocatedSizeBytes, static_cast<uint64_t>(UINT64_MAX));
    synthetic->allocatedSizeKnown =
        synthetic->allocatedTotalCount > 0 && synthetic->allocatedKnownCount == synthetic->allocatedTotalCount;
    TreemapNode* raw = synthetic.get();
    syntheticStorage.push_back(std::move(synthetic));
    return raw;
}

// Lays out one level of nodes in normalized 0..100 space. Sizes are shares of
// the total; every generated rectangle is an exact, non-overlapping,
// container-contained tile (band math, no clamping) -- clamping individual
// rectangles to a minimum dimension was removed because it corrupted the
// tiling (overlaps, runaway coverage, tiles escaping the parent).
//
// Partition rules (semantic-treemap-uia 5.x, storage-analysis 4.3):
//  - Known nodes whose share falls below TinyNodeShare -- including exact
//    zero-byte items -- are "tiny". Two or more tiny siblings aggregate into
//    one synthetic "Other small items" tile laid out by its merged size; a
//    single tiny sibling keeps its honest sub-minimum geometry.
//  - Unknown-size nodes (calculating / aggregateFailed) are NEVER merged into
//    the catch-all: their size is unknown, not small. They are laid out by
//    their known partial share, and a zero partial share uses the minimum
//    share as an explicit placeholder so the "Calculating…" tile stays
//    visible, clickable, and hoverable until it resolves.
//
// `syntheticStorage` is the caller-owned home of any catch-all node created
// by this call.
inline void LayoutTreemap(std::vector<TreemapRect>& rects, float x, float y, float width, float height,
                          const std::vector<TreemapNode*>& nodes, int depth,
                          std::vector<std::unique_ptr<TreemapNode>>* syntheticStorage = nullptr) {
    if (nodes.empty() || width < kMinRectSize || height < kMinRectSize) return;

    const float totalSize = std::max(1.0f, static_cast<float>(std::accumulate(
        nodes.begin(), nodes.end(), 0ULL, [](uint64_t sum, TreemapNode* n) {
            return sum + n->totalSizeBytes;
        })));
    const float minShare = TinyNodeShare(width, height);
    const uint64_t placeholderSize = std::max<uint64_t>(1, static_cast<uint64_t>(minShare * totalSize));

    std::vector<TreemapNode*> layoutNodes;
    layoutNodes.reserve(nodes.size() + 1);
    std::vector<TreemapNode*> tiny;
    for (TreemapNode* node : nodes) {
        const bool sizeUnknown = node->calculating || node->aggregateFailed;
        if (!sizeUnknown) {
            const float share = static_cast<float>(node->totalSizeBytes) / totalSize;
            if (share < minShare) {
                tiny.push_back(node);
                continue;
            }
        }
        layoutNodes.push_back(node);
    }
    TreemapNode* catchAll = nullptr;
    if (tiny.size() >= 2 && syntheticStorage != nullptr) {
        catchAll = BuildSyntheticCatchAll(tiny, *syntheticStorage, depth);
        layoutNodes.push_back(catchAll);
    } else {
        layoutNodes.insert(layoutNodes.end(), tiny.begin(), tiny.end());
    }

    // Largest-first order keeps band grouping deterministic (the catch-all
    // participates by its merged size).
    std::sort(layoutNodes.begin(), layoutNodes.end(), [](TreemapNode* left, TreemapNode* right) {
        return left->totalSizeBytes > right->totalSizeBytes;
    });

    // Effective sizes driving the band math: real sizes for known nodes and
    // the catch-all, the minimum-share placeholder for unknown-size nodes.
    std::vector<uint64_t> effectiveSizes;
    effectiveSizes.reserve(layoutNodes.size());
    for (TreemapNode* node : layoutNodes) {
        if ((node->calculating || node->aggregateFailed) && !node->synthetic) {
            effectiveSizes.push_back(std::max(node->totalSizeBytes, placeholderSize));
        } else {
            effectiveSizes.push_back(std::max<uint64_t>(1, node->totalSizeBytes));
        }
    }

    if (layoutNodes.size() == 1) {
        TreemapRect rect;
        rect.x = x;
        rect.y = y;
        rect.width = width;
        rect.height = height;
        rect.node = layoutNodes[0];
        rects.push_back(rect);
        return;
    }

    Squarify(rects, x, y, width, height, layoutNodes, effectiveSizes, depth);
}

inline void Squarify(std::vector<TreemapRect>& rects, float x, float y, float width, float height,
                     const std::vector<TreemapNode*>& nodes, const std::vector<uint64_t>& effectiveSizes,
                     int /*depth*/) {
    if (nodes.empty()) return;

    const bool horizontal = width >= height;
    // Sum of the EFFECTIVE sizes: unknown nodes participate by their
    // placeholder share, so shares sum to exactly 1 and the band math covers
    // the container with no residual. This is what makes the layout use the
    // caller's placeholder for "Calculating…" tiles.
    const float totalSize = std::max(1.0f, static_cast<float>(std::accumulate(
        effectiveSizes.begin(), effectiveSizes.end(), 0ULL)));

    // 'pos' is the running offset along the long axis (x when horizontal, y
    // otherwise); each slice is a band of width/height 'sliceSize' whose nodes
    // are stacked along the short axis, so the rects tile the area exactly
    // without overlapping.
    float pos = 0.0f;
    size_t startIdx = 0;

    while (startIdx < nodes.size()) {
        float currentSum = 0.0f;
        size_t endIdx = startIdx;
        float bestAspect = 1e10f;

        for (size_t i = startIdx; i < nodes.size(); ++i) {
            float newSum = currentSum + static_cast<float>(effectiveSizes[i]) / totalSize;
            float newAspect;

            if (horizontal) {
                float side1 = newSum * width;
                float side2 = height;
                newAspect = AspectRatio(side1, side2);
            } else {
                float side1 = newSum * height;
                float side2 = width;
                newAspect = AspectRatio(side1, side2);
            }

            if (newAspect <= bestAspect || i == startIdx) {
                bestAspect = newAspect;
                currentSum = newSum;
                endIdx = i + 1;
            } else {
                break;
            }
        }

        const float sliceSize = currentSum * (horizontal ? width : height);

        float innerPos = 0.0f;
        for (size_t i = startIdx; i < endIdx; ++i) {
            TreemapRect rect;
            if (horizontal) {
                // Vertical band of width sliceSize and full height; nodes stack
                // vertically, each spanning the band width. Exact shares -- no
                // clamping: the members were already partitioned so every laid
                // out node has a readable dimension, and clamping here is what
                // used to break the tiling invariant.
                float rectHeight = (static_cast<float>(effectiveSizes[i]) / totalSize) * height / currentSum;
                rect.x = x + pos;
                rect.y = y + innerPos;
                rect.width = sliceSize;
                rect.height = rectHeight;
                rect.node = nodes[i];
                rects.push_back(rect);
                innerPos += rectHeight;
            } else {
                // Horizontal band of height sliceSize and full width; nodes
                // stack horizontally, each spanning the band height.
                float rectWidth = (static_cast<float>(effectiveSizes[i]) / totalSize) * width / currentSum;
                rect.x = x + innerPos;
                rect.y = y + pos;
                rect.width = rectWidth;
                rect.height = sliceSize;
                rect.node = nodes[i];
                rects.push_back(rect);
                innerPos += rectWidth;
            }
        }

        pos += sliceSize;
        startIdx = endIdx;
    }
}

inline TreemapNode* HitTest(const std::vector<TreemapRect>& layout, float x, float y) {
    for (auto it = layout.rbegin(); it != layout.rend(); ++it) {
        if (x >= it->x && x < it->x + it->width && y >= it->y && y < it->y + it->height) {
            return it->node;
        }
    }
    return nullptr;
}

// Inverts the shell's Translate(offsetX, offsetY) + viewport/100 scale applied
// when drawing the treemap, mapping client coordinates back into normalized
// 0..100 treemap space. Mirrors the math in TreemapView::HandleMouseMove.
inline std::pair<float, float> TreemapClientToNormalized(float clientX, float clientY, float offsetX, float offsetY,
                                                         float viewportW, float viewportH) {
    const float scaleX = viewportW / 100.0f;
    const float scaleY = viewportH / 100.0f;
    const float x = (clientX - offsetX) / scaleX;
    const float y = (clientY - offsetY) / scaleY;
    return {x, y};
}

// Convert a client-space point through the same shell transform used for
// painting and resolve the tile directly. Keeping click and hover mapping in
// one pure seam prevents a click from depending on a preceding WM_MOUSEMOVE.
inline TreemapNode* HitTestClientPoint(const std::vector<TreemapRect>& layout,
                                       float clientX, float clientY,
                                       float offsetX, float offsetY,
                                       float viewportW, float viewportH) {
    const auto [x, y] = TreemapClientToNormalized(clientX, clientY, offsetX, offsetY,
                                                   viewportW, viewportH);
    return HitTest(layout, x, y);
}

} // namespace ffui
