#pragma once
// semantic-treemap-uia 6/7/31: the single semantic projection of one treemap
// tile, shared by the D2D hover rendering and the UIA provider layer.
//
// There is deliberately no parallel accessibility-only state model: the
// provider consumes `TreemapUiaItemInfo` values built from the exact
// TreemapNode the renderer draws and the mouse hit-tester resolves. The D2D
// hover text and the UIA FullDescription derive from the same values through
// the same helper, so the two presentations can never drift.
//
// The structs and functions here are pure (no windows/D2D/COM); tests compile
// this header directly.

#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <string>
#include <string_view>
#include <vector>

#include "ffui/SnapshotAggregate.h"
#include "ffui/StoragePercentage.h"
#include "ffui/TreemapLayout.h"
#include "ffui/UiaEvents.h"
#include "ffui/UiaIdentity.h"
#include "ffui/UiaSurface.h"

namespace ffui {

// Display name of the synthetic aggregate tile that absorbs siblings below
// the minimum readable share (storage-analysis 4.3).
inline constexpr const wchar_t kOtherSmallItemsName[] = L"Other small items";
// Identity discriminator appended to the scope path for the catch-all's
// stable identity hash. The ".ff-" prefix makes a collision with a real
// user path practically impossible.
inline constexpr const wchar_t kOtherSmallItemsIdentitySuffix[] = L"\\.ff-other-small-items";

// Sizes render through this single formatter. Util::FormatSize delegates here
// (see Util.cpp), so the storage ListView, the treemap hover, and the UIA
// metadata all share one byte-count presentation.
inline std::wstring TreemapFormatSize(uint64_t bytes) {
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        wchar_t buffer[64]{};
        swprintf_s(buffer, L"%.1f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
        return buffer;
    }
    if (bytes >= 1024ULL * 1024ULL) {
        wchar_t buffer[64]{};
        swprintf_s(buffer, L"%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
        return buffer;
    }
    if (bytes >= 1024ULL) {
        wchar_t buffer[64]{};
        swprintf_s(buffer, L"%.1f KB", static_cast<double>(bytes) / 1024.0);
        return buffer;
    }
    return std::to_wstring(bytes) + L" B";
}

// visual-matrix EX-009 closure: degraded snapshots (volumeRowId == 0) can
// never answer an engine folder-aggregate request, so directory totals
// resolve from the immutable snapshot instead of stalling in
// "Calculating…" forever with an unknown share. Complete subtrees become
// exact; missing descendants stay explicitly partial (aggregateFailed) so no
// share is ever presented from an incomplete denominator. Indexed nodes
// (volumeRowId != 0) and files are untouched -- their totals still arrive
// through the engine aggregate path.
inline void ResolveDegradedTreemapTotals(TreemapNode& node, const SnapshotAggregate& aggregate) {
    if (!node.isDirectory || node.volumeRowId != 0) {
        return;
    }
    node.totalSizeBytes = aggregate.totalSizeBytes;
    node.totalAllocatedSizeBytes = aggregate.totalAllocatedSizeBytes;
    node.allocatedKnownCount = aggregate.allocatedBytesKnownCount;
    node.allocatedTotalCount = aggregate.allocatedBytesTotalCount;
    node.calculating = false;
    node.aggregateResolved = aggregate.complete;
    node.aggregateFailed = !aggregate.complete;
}

// A degraded subtree with no known bytes is terminal, not pending: there is
// no engine aggregate request that can resolve it. Keep the canvas text in
// sync with the same pending/partial distinction used by tile metadata.
inline constexpr std::wstring_view kTreemapCalculatingStateText =
    L"Calculating folder sizes…";
inline constexpr std::wstring_view kTreemapNoSizedDataStateText =
    L"No sized data for this scope.";

inline constexpr std::wstring_view TreemapEmptyStateText(const TreemapNode& root) {
    return root.calculating ? kTreemapCalculatingStateText : kTreemapNoSizedDataStateText;
}

// Stable identity of a treemap node: the synthetic catch-all is identified by
// the scope path plus the reserved suffix (deterministic, scope-unique, never
// collides with a real path); real nodes use the FRN-or-path rule of
// UiaItemIdentityHash.
inline uint64_t TreemapNodeIdentityHash(const TreemapNode& node, const std::wstring& scopePath) {
    if (node.synthetic) {
        return UiaHashWide(scopePath + kOtherSmallItemsIdentitySuffix);
    }
    return UiaItemIdentityHash(node.volumeRowId, node.fileIdLow, node.fileIdHigh, node.path);
}

// The "overall volume" denominator for percentage-of-volume: the OS-reported
// used bytes of the volume hosting the current scope (GetDiskFreeSpaceExW,
// the same ground truth the storage overview uses).
struct TreemapVolumeInfo {
    uint64_t usedBytes = 0;
    bool known = false;
};

// One visible treemap tile, projected to plain values. `identityHash` is the
// stable semantic identity (never a layout position); `percentOfParent` /
// `percentOfVolume` are -1 when the denominator or the item's size is not
// known. `hoverLines` are the ready-to-draw strings the D2D renderer shows;
// `fullDescription` is the canonical UIA FullDescription built from the same
// values.
struct TreemapUiaItemInfo {
    uint64_t generation = 0;     // treemap build generation at projection time
    uint64_t scopeHash = 0;      // UiaHashWide of the current scope path
    uint64_t identityHash = 0;   // stable identity (FRN or full path / catch-all)
    std::wstring name;
    std::wstring fullPath;       // empty only for the synthetic catch-all
    bool isDirectory = false;
    bool synthetic = false;      // "Other small items" aggregate
    uint64_t aggregateItemCount = 0;
    bool invokable = false;      // false only for the synthetic catch-all

    uint64_t logicalSizeBytes = 0;
    bool logicalSizeKnown = false; // false while calculating / failed

    uint64_t allocatedSizeBytes = 0;
    bool allocatedSizeKnown = false;
    bool allocatedPartial = false; // directory aggregate: known < total counts
    uint64_t allocatedKnownCount = 0;
    uint64_t allocatedTotalCount = 0;

    bool calculating = false;
    bool aggregateFailed = false;

    double percentOfParent = -1.0; // ratio or -1 (unknown denominator/size)
    double percentOfVolume = -1.0;

    float rectX = 0.0f;          // normalized 0..100 treemap rectangle
    float rectY = 0.0f;
    float rectWidth = 0.0f;
    float rectHeight = 0.0f;
    bool offscreen = false;      // no intersection with the treemap viewport

    std::wstring fullDescription; // canonical UIA FullDescription
    std::wstring itemStatus;      // "Calculating…" / "Partial" / empty
    std::wstring itemType;        // "Folder" / "File" / "Other small items"
    std::vector<std::wstring> hoverLines; // [path, size, size-on-disk, pct]
};

// Build the canonical metadata for one tile from the exact node the layout
// and hit-testing use. `parentTotalBytes`/`parentKnown` describe the current
// displayed scope (the filtered tree's own totals); `volume` is the
// percentage-of-volume denominator; `scopePath` anchors the catch-all
// identity and the displayed context.
inline TreemapUiaItemInfo BuildTreemapItemMetadata(const TreemapNode& node,
                                                   uint64_t parentTotalBytes, bool parentKnown,
                                                   const TreemapVolumeInfo& volume,
                                                   const std::wstring& scopePath,
                                                   uint64_t generation, uint64_t scopeHash,
                                                   float rectX, float rectY,
                                                   float rectWidth, float rectHeight,
                                                   bool offscreen) {
    TreemapUiaItemInfo info;
    info.generation = generation;
    info.scopeHash = scopeHash;
    info.identityHash = TreemapNodeIdentityHash(node, scopePath);
    info.name = node.name;
    info.fullPath = node.path;
    info.isDirectory = node.isDirectory;
    info.synthetic = node.synthetic;
    info.aggregateItemCount = node.aggregateItemCount;
    info.invokable = !node.synthetic && !node.path.empty();
    info.calculating = node.calculating;
    // calculating (pending) and aggregateFailed (known-partial) are mutually
    // exclusive states by construction.
    info.aggregateFailed = node.aggregateFailed;
    info.itemType = node.synthetic ? std::wstring(kOtherSmallItemsName)
                                   : (node.isDirectory ? L"Folder" : L"File");
    info.rectX = rectX;
    info.rectY = rectY;
    info.rectWidth = rectWidth;
    info.rectHeight = rectHeight;
    info.offscreen = offscreen;

    // --- logical size (files always known; aggregates honest about state) ---
    const bool aggregate = node.isDirectory || node.synthetic;
    if (!aggregate) {
        info.logicalSizeBytes = node.sizeBytes;
        info.logicalSizeKnown = true;
    } else {
        info.logicalSizeBytes = node.totalSizeBytes;
        info.logicalSizeKnown = !node.calculating && !node.aggregateFailed;
    }

    // --- allocated size with explicit known/partial state ---
    if (aggregate) {
        info.allocatedSizeBytes = node.totalAllocatedSizeBytes;
        info.allocatedKnownCount = node.allocatedKnownCount;
        info.allocatedTotalCount = node.allocatedTotalCount;
        info.allocatedSizeKnown = node.allocatedTotalCount > 0 &&
                                  node.allocatedKnownCount == node.allocatedTotalCount;
        info.allocatedPartial = node.allocatedTotalCount > 0 &&
                                node.allocatedKnownCount > 0 &&
                                node.allocatedKnownCount < node.allocatedTotalCount;
    } else {
        info.allocatedSizeBytes = node.allocatedSizeBytes;
        info.allocatedSizeKnown = node.allocatedSizeKnown;
    }

    // --- percentages (one shared StoragePercentage helper, two denominators) ---
    const uint64_t sizeForPercent = aggregate ? node.totalSizeBytes : node.sizeBytes;
    const bool sizeKnownForPercent = aggregate ? info.logicalSizeKnown : true;
    const StoragePercentage pctParent = ComputeStoragePercentage(
        sizeForPercent, parentTotalBytes, parentKnown && sizeKnownForPercent);
    const StoragePercentage pctVolume = ComputeStoragePercentage(
        sizeForPercent, volume.usedBytes, volume.known && sizeKnownForPercent);
    if (pctParent.known) info.percentOfParent = pctParent.sortKey;
    if (pctVolume.known) info.percentOfVolume = pctVolume.sortKey;

    // --- status ---
    if (node.calculating) {
        info.itemStatus = L"Calculating…";
    } else if (node.aggregateFailed) {
        info.itemStatus = L"Partial";
    }

    // --- shared text lines ---
    const std::wstring sizeLine = [&]() -> std::wstring {
        if (!aggregate) return L"Size: " + TreemapFormatSize(node.sizeBytes);
        if (node.calculating) {
            return node.totalSizeBytes > 0
                       ? L"Size: ≥ " + TreemapFormatSize(node.totalSizeBytes) + L" (Calculating…)"
                       : L"Size: Calculating…";
        }
        if (node.aggregateFailed) return L"Size: Partial";
        return L"Size: " + TreemapFormatSize(node.totalSizeBytes);
    }();

    const bool divergenceCandidate = info.logicalSizeKnown && info.allocatedSizeKnown;
    std::wstring diskLine;
    if (aggregate) {
        if (node.calculating && node.allocatedTotalCount == 0) {
            diskLine = L"Size on disk: Calculating…";
        } else if (node.allocatedTotalCount == 0) {
            diskLine = L"Size on disk: 0 B"; // exactly zero: empty subtree
        } else if (node.allocatedKnownCount == node.allocatedTotalCount) {
            diskLine = L"Size on disk: " + TreemapFormatSize(node.totalAllocatedSizeBytes);
        } else if (node.allocatedKnownCount == 0) {
            diskLine = L"Size on disk: Unknown";
        } else {
            diskLine = L"Size on disk: ≥ " + TreemapFormatSize(node.totalAllocatedSizeBytes) +
                       L" (" + std::to_wstring(node.allocatedKnownCount) + L"/" +
                       std::to_wstring(node.allocatedTotalCount) + L")";
        }
    } else {
        diskLine = node.allocatedSizeKnown
                       ? L"Size on disk: " + TreemapFormatSize(node.allocatedSizeBytes)
                       : L"Size on disk: Unknown";
    }
    // Divergence callout: both values known and materially different (>= 1.5x
    // relative, computed as (larger-smaller) >= smaller/2 so it cannot
    // overflow, plus a 4 KiB absolute floor so byte-level noise on tiny files
    // stays silent). The wording makes the two byte counts unambiguous.
    if (divergenceCandidate) {
        const uint64_t allocatedKnown = aggregate ? node.totalAllocatedSizeBytes : node.allocatedSizeBytes;
        const uint64_t logicalKnown = aggregate ? node.totalSizeBytes : node.sizeBytes;
        const uint64_t larger = std::max(logicalKnown, allocatedKnown);
        const uint64_t smaller = std::min(logicalKnown, allocatedKnown);
        if (smaller > 0 && (larger - smaller) >= smaller / 2 && (larger - smaller) >= 4096) {
            diskLine += L" (differs from logical size)";
        }
    }

    const std::wstring pctLine = L"% of parent: " + pctParent.display +
                                 L"   % of volume: " + pctVolume.display;

    // Hover lines the D2D renderer draws on the hovered tile (best-effort
    // visual; the UIA FullDescription always carries the full facts).
    if (node.synthetic) {
        info.hoverLines.push_back(L"Other small items — " + std::to_wstring(node.aggregateItemCount) + L" items");
    } else {
        // Truncate for drawing; the FullDescription keeps the full path.
        constexpr size_t kMaxHoverPath = 64;
        std::wstring path = node.path;
        if (path.size() > kMaxHoverPath) {
            path.resize(kMaxHoverPath);
            path += L"…";
        }
        info.hoverLines.push_back(std::move(path));
    }
    info.hoverLines.push_back(sizeLine);
    info.hoverLines.push_back(diskLine);
    info.hoverLines.push_back(pctLine);

    // Canonical FullDescription: one fact per line, never conflating logical
    // and allocated byte counts.
    std::wstring description = L"Storage item: " + node.name + L"\n";
    description += L"Type: " + info.itemType + L"\n";
    if (node.synthetic) {
        description += L"Aggregate: " + std::to_wstring(node.aggregateItemCount) + L" items\n";
    } else {
        description += L"Path: " + node.path + L"\n";
    }
    description += sizeLine + L"\n";
    description += diskLine + L"\n";
    description += L"% of parent: " + pctParent.display + L"\n";
    description += L"% of volume: " + pctVolume.display;
    if (!info.itemStatus.empty()) {
        description += L"\nStatus: " + info.itemStatus;
    }
    info.fullDescription = std::move(description);
    return info;
}

// The pure seam the UIA provider layer consumes (implemented by
// StorageAnalysis over the real TreemapView state). All members return plain
// values; provider code never touches TreemapNode/TreemapRect. Reads are
// bounded-lock, perform no filesystem/engine work, and never block
// unboundedly. Implementations push semantic events through TreemapEvents().
class TreemapUiaSurface {
public:
    virtual ~TreemapUiaSurface() = default;

    // Geometry of the visible treemap canvas. The host Column View geometry
    // remains authoritative for the browse surface; Storage Analysis returns
    // its own inset canvas geometry while the treemap is active.
    virtual UiaViewGeometry TreemapGeometry() const { return {}; }
    virtual bool TreemapActive() const { return false; }
    virtual uint64_t TreemapGeneration() const { return 0; }
    virtual uint64_t TreemapScopeHash() const { return 0; }
    virtual std::wstring TreemapScopePath() const { return {}; }
    virtual int TreemapTileCount() const { return 0; }
    virtual TreemapUiaItemInfo TreemapTileInfo(int index) const {
        (void)index;
        return {};
    }
    // Index of the tile with this identity in the CURRENT layout, or -1.
    virtual int TreemapTileIndex(uint64_t identityHash) const {
        (void)identityHash;
        return -1;
    }
    // Resolve a tile by its stable identity (O(1) via the layout index map).
    virtual bool TreemapFindTile(uint64_t identityHash, TreemapUiaItemInfo& out) const {
        (void)identityHash;
        (void)out;
        return false;
    }
    // Real treemap hit-test in normalized 0..100 space; empty info on miss.
    virtual TreemapUiaItemInfo TreemapTileHitTest(float normalizedX, float normalizedY) const {
        (void)normalizedX;
        (void)normalizedY;
        return {};
    }
    // Activation (navigation only). Marshals to the owning UI thread when
    // called from a UIAutomationCore worker; on the UI thread runs the exact
    // mouse-click navigation mutation.
    virtual void TreemapActivateItem(const TreemapUiaItemInfo& item) { (void)item; }
    // Event sink the provider controller subscribes to (null when absent).
    virtual UiaEventRouter* TreemapEvents() { return nullptr; }
};

} // namespace ffui
