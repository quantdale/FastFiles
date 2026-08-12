#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>

#include "CategoryEngine.h"
#include "ffui/TreemapLayout.h"

namespace ffui {

// Pure hierarchy filtering used before Treemap layout. Files are classified by
// CategoryEngine; directories are retained only when they contain a retained
// descendant, and their totals are recomputed from those descendants.
inline bool FilterTreemapNode(TreemapNode& node, const CategoryEngine& categoryEngine,
                              const std::wstring& categoryId) {
    if (!node.isDirectory) {
        return categoryEngine.Matches(node.name, categoryId);
    }

    auto retainedEnd = std::remove_if(
        node.children.begin(), node.children.end(),
        [&categoryEngine, &categoryId](const std::unique_ptr<TreemapNode>& child) {
            if (child->isDirectory) {
                return !FilterTreemapNode(*child, categoryEngine, categoryId);
            }
            return !categoryEngine.Matches(child->name, categoryId);
        });
    node.children.erase(retainedEnd, node.children.end());
    if (node.children.empty()) return false;

    uint64_t total = 0;
    bool incomplete = false;
    for (const auto& child : node.children) {
        if (std::numeric_limits<uint64_t>::max() - total < child->totalSizeBytes) {
            total = std::numeric_limits<uint64_t>::max();
            incomplete = true;
        } else {
            total += child->totalSizeBytes;
        }
        incomplete = incomplete || child->calculating || child->aggregateFailed;
    }
    node.totalSizeBytes = total;
    node.calculating = incomplete;
    node.aggregateResolved = false;
    node.aggregateFailed = incomplete;
    return true;
}

// Apply a non-All category to a root container. The root itself is retained
// as the scope anchor even when the filtered result is empty.
inline void FilterTreemapTree(TreemapNode& root, const CategoryEngine& categoryEngine,
                              const std::wstring& categoryId) {
    if (categoryId.empty()) return;

    auto retainedEnd = std::remove_if(
        root.children.begin(), root.children.end(),
        [&categoryEngine, &categoryId](const std::unique_ptr<TreemapNode>& child) {
            if (child->isDirectory) {
                return !FilterTreemapNode(*child, categoryEngine, categoryId);
            }
            return !categoryEngine.Matches(child->name, categoryId);
        });
    root.children.erase(retainedEnd, root.children.end());

    uint64_t total = 0;
    bool incomplete = false;
    for (const auto& child : root.children) {
        if (std::numeric_limits<uint64_t>::max() - total < child->totalSizeBytes) {
            total = std::numeric_limits<uint64_t>::max();
            incomplete = true;
        } else {
            total += child->totalSizeBytes;
        }
        incomplete = incomplete || child->calculating || child->aggregateFailed;
    }
    root.totalSizeBytes = total;
    root.calculating = incomplete;
    root.aggregateResolved = false;
    root.aggregateFailed = incomplete;
}

} // namespace ffui
