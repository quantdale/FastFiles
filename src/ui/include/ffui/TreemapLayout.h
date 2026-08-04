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
    uint64_t sizeBytes = 0;
    uint64_t totalSizeBytes = 0;
    bool calculating = false;
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
                     const std::vector<TreemapNode*>& nodes, int depth);

inline void LayoutTreemap(std::vector<TreemapRect>& rects, float x, float y, float width, float height,
                          const std::vector<TreemapNode*>& nodes, int depth) {
    if (nodes.empty() || width < kMinRectSize || height < kMinRectSize) return;

    if (nodes.size() == 1) {
        TreemapRect rect;
        rect.x = x;
        rect.y = y;
        rect.width = std::max(kMinRectSize, width);
        rect.height = std::max(kMinRectSize, height);
        rect.node = nodes[0];
        rects.push_back(rect);
        return;
    }

    Squarify(rects, x, y, width, height, nodes, depth);
}

inline void Squarify(std::vector<TreemapRect>& rects, float x, float y, float width, float height,
                     const std::vector<TreemapNode*>& nodes, int /*depth*/) {
    if (nodes.empty()) return;

    const bool horizontal = width >= height;
    const float totalSize = std::max(1.0f, static_cast<float>(std::accumulate(nodes.begin(), nodes.end(), 0ULL,
                                                                              [](uint64_t sum, TreemapNode* n) {
                                                                                  return sum + n->totalSizeBytes;
                                                                              })));

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
            float newSum = currentSum + static_cast<float>(nodes[i]->totalSizeBytes) / totalSize;
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
                // vertically, each spanning the band width.
                float rectHeight = (static_cast<float>(nodes[i]->totalSizeBytes) / totalSize) * height / currentSum;
                rect.x = x + pos;
                rect.y = y + innerPos;
                rect.width = std::max(kMinRectSize, sliceSize);
                rect.height = std::max(kMinRectSize, rectHeight);
                rect.node = nodes[i];
                rects.push_back(rect);
                innerPos += rectHeight;
            } else {
                // Horizontal band of height sliceSize and full width; nodes
                // stack horizontally, each spanning the band height.
                float rectWidth = (static_cast<float>(nodes[i]->totalSizeBytes) / totalSize) * width / currentSum;
                rect.x = x + innerPos;
                rect.y = y + pos;
                rect.width = std::max(kMinRectSize, rectWidth);
                rect.height = std::max(kMinRectSize, sliceSize);
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

} // namespace ffui