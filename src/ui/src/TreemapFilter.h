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
    // A directory whose own aggregate was still in flight when the filter
    // landed can no longer learn its true total (filtered trees never
    // re-request aggregates), so pending state converts to known-partial.
    const bool wasPending = node.calculating;
    bool incomplete = wasPending;
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
    // A filtered tree's aggregates are never re-requested (requestAggregate
    // bails while a category filter is active), so an incomplete rollup is
    // known-partial, not pending "Calculating…".
    node.calculating = false;
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
    // Same pending-to-partial conversion as FilterTreemapNode.
    const bool rootWasPending = root.calculating;
    bool incomplete = rootWasPending;
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
    // Same known-partial rule as FilterTreemapNode: filtered roots never
    // re-request aggregates, so incomplete means Partial, not Calculating.
    root.calculating = false;
    root.aggregateResolved = false;
    root.aggregateFailed = incomplete;
}

} // namespace ffui
