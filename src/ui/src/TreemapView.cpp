#include "TreemapView.h"
#include "CategoryEngine.h"
#include "TreemapFilter.h"
#include "UITheme.h"
#include "UiStyle.h"
#include "Util.h"
#include "ffui/SnapshotAggregate.h"
#include "UiaProviderBase.h" // UIA_ItemStatusPropertyId for the aggregate-completion property event

#include <UIAutomationClient.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <shlwapi.h>
#include <windowsx.h>

#pragma comment(lib, "shlwapi.lib")

namespace ffui {
namespace {

constexpr float kBorderWidth = 1.0f;
constexpr float kHoverRingAlpha = 0.5f;  // subtle accent ring on the hovered tile
constexpr int kPadding = 2;

} // namespace

TreemapView::~TreemapView() {
    if (lifetime_) lifetime_->store(false, std::memory_order_release);
    Hide();
}

bool TreemapView::Initialize(HWND owner, EngineClient* engine,
                             NavigateHandler navigate) {
    owner_ = owner;
    engine_ = engine;
    navigate_ = std::move(navigate);
    return true;
}

void TreemapView::SetUiaEventHandler(UiaEventHandler handler) {
    uiaEventHandler_ = std::move(handler);
}

void TreemapView::EmitUiaEvent(const UiaEventNotification& notification) {
    if (uiaEventHandler_) uiaEventHandler_(notification);
}

void TreemapView::ShowAndFocus(const std::wstring& currentPath) {
    {
        std::lock_guard<std::recursive_mutex> lock(uiaMutex_);
        currentPath_ = currentPath;
        visible_.store(true, std::memory_order_release);
        BuildTree(currentPath);
        Reposition();
    }
    // semantic-treemap-uia 30.x: structure events after the lock is released.
    UiaEventNotification notification;
    notification.kind = UiaEventKind::StructureChanged;
    notification.treemap = true;
    notification.treemapScopeHash = UiaHashWide(currentPath);
    EmitUiaEvent(notification);
}

void TreemapView::Hide() {
    const bool wasVisible = visible_.load(std::memory_order_acquire);
    {
        std::lock_guard<std::recursive_mutex> lock(uiaMutex_);
        if (!visible_.load(std::memory_order_acquire)) return;
        visible_.store(false, std::memory_order_release);
        root_.reset();
        layout_.clear();
        layoutIndexByIdentity_.clear();
        syntheticNodes_.clear();
        hoveredNode_ = nullptr;
    }
    if (wasVisible) {
        UiaEventNotification notification;
        notification.kind = UiaEventKind::StructureChanged;
        notification.treemap = true;
        notification.treemapScopeHash = UiaHashWide(currentPath_);
        EmitUiaEvent(notification);
    }
}

TreemapNode* TreemapView::FindNode(TreemapNode* root, const std::wstring& path) {
    if (root == nullptr) return nullptr;
    if (root->path == path) return root;
    for (const auto& child : root->children) {
        if (auto* found = FindNode(child.get(), path)) return found;
    }
    return nullptr;
}

uint64_t TreemapView::RecomputeTotals(TreemapNode& node) {
    if (!node.isDirectory || node.aggregateResolved) return node.totalSizeBytes;

    uint64_t total = 0;
    bool pending = false;
    bool failed = false;
    for (auto& child : node.children) {
        const uint64_t childTotal = RecomputeTotals(*child);
        if (UINT64_MAX - total < childTotal) {
            total = 0;
            failed = true;
            break;
        }
        total += childTotal;
        pending = pending || child->calculating;
        failed = failed || child->aggregateFailed;
    }
    node.totalSizeBytes = total;
    // "Calculating" is reserved for genuinely pending aggregates; once every
    // child resolved, a partial subtree is known-partial, never pending.
    node.calculating = pending && !failed;
    node.aggregateFailed = !pending && failed;
    return node.totalSizeBytes;
}

void TreemapView::RebuildLayout() {
    layout_.clear();
    syntheticNodes_.clear();
    layoutIndexByIdentity_.clear();
    const auto updateTerminalEmptyState = [this]() {
        if (root_ && !root_->calculating) {
            emptyStateText_ = std::wstring(TreemapEmptyStateText(*root_));
        }
    };
    // ui-overhaul phase 9: a scope with no sized entries (empty folder, unknown
    // subtree still calculating, or a degraded scope whose snapshot carries no
    // sizes) is the deliberate empty-state surface -- never a grid of
    // background-colored zero-information tiles.
    if (!root_ || root_->children.empty() || root_->totalSizeBytes == 0) {
        updateTerminalEmptyState();
        return;
    }

    std::vector<TreemapNode*> nodes;
    nodes.reserve(root_->children.size());
    for (const auto& child : root_->children) nodes.push_back(child.get());
    LayoutTreemap(layout_, kPadding, kPadding, 100.0f - 2 * kPadding,
                  100.0f - 2 * kPadding, nodes, 0, &syntheticNodes_);

    // ui-overhaul phase 9: a scope whose entries all have zero known size must
    // present its deliberate empty-state text, not a grid of zero-area
    // rectangles that paints as a silent blank canvas. Collapsing the layout
    // here also keeps the UIA tile projection honest (no phantom items).
    const bool anySized = std::any_of(layout_.begin(), layout_.end(), [](const TreemapRect& rect) {
        return rect.width > 0.01f && rect.height > 0.01f;
    });
    if (!anySized) {
        layout_.clear();
        syntheticNodes_.clear();
        layoutIndexByIdentity_.clear();
        updateTerminalEmptyState();
        return;
    }

    // semantic-treemap-uia 16.x: identity -> layout index, rebuilt here under
    // the same lock so the provider resolves tiles in O(1).
    for (size_t i = 0; i < layout_.size(); ++i) {
        layoutIndexByIdentity_[TreemapNodeIdentityHash(*layout_[i].node, currentPath_)] = i;
    }
}

void TreemapView::Reposition() {
    // Treemap renders in the full viewport area; no HWND repositioning needed
}

void TreemapView::OnSnapshotUpdated() {
    if (!visible_.load(std::memory_order_acquire)) return;
    const std::wstring path = currentPath_;
    {
        std::lock_guard<std::recursive_mutex> lock(uiaMutex_);
        if (!visible_.load(std::memory_order_acquire)) return;
        BuildTree(path);
    }
    UiaEventNotification notification;
    notification.kind = UiaEventKind::StructureChanged;
    notification.treemap = true;
    notification.treemapScopeHash = UiaHashWide(path);
    EmitUiaEvent(notification);
}

void TreemapView::EnsureCreated(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory) {
    if (resourcesCreated_) return;
    const ffui::UiTheme theme = ffui::GetUiTheme(darkTheme_);
    context->CreateSolidColorBrush(theme.background, &backgroundBrush_);
    context->CreateSolidColorBrush(theme.text, &textBrush_);
    context->CreateSolidColorBrush(theme.border, &borderBrush_);
    context->CreateSolidColorBrush(theme.hoverOverlay, &hoverOverlayBrush_);
    context->CreateSolidColorBrush(theme.treemapCalculating, &calculatingBrush_);
    dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                     DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                     12.0f, L"en-us", &textFormat_);
    textFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    resourcesCreated_ = true;
}

void TreemapView::SetDarkTheme(bool dark) {
    darkTheme_ = dark;
    resourcesCreated_ = false;
    backgroundBrush_.Reset();
    textBrush_.Reset();
    borderBrush_.Reset();
    hoverBrush_.Reset();
    hoverOverlayBrush_.Reset();
    calculatingBrush_.Reset();
}

void TreemapView::SetEmptyStateText(const std::wstring& text) {
    std::lock_guard<std::recursive_mutex> lock(uiaMutex_);
    emptyStateText_ = text;
}

void TreemapView::SetCategoryFilter(const CategoryEngine* categoryEngine, const std::wstring& categoryId) {
    std::wstring scope = currentPath_;
    {
        std::lock_guard<std::recursive_mutex> lock(uiaMutex_);
        categoryEngine_ = categoryEngine;
        categoryId_ = categoryId;
        if (visible_.load(std::memory_order_acquire)) {
            BuildTree(currentPath_);
            InvalidateRect(owner_, nullptr, FALSE);
        }
    }
    // semantic-treemap-uia 27.x: the filter changes the displayed scope, so
    // surviving identities get fresh providers and removed ones fail stale.
    UiaEventNotification notification;
    notification.kind = UiaEventKind::StructureChanged;
    notification.treemap = true;
    notification.treemapScopeHash = UiaHashWide(scope);
    EmitUiaEvent(notification);
}

void TreemapView::BuildTree(const std::wstring& path) {
    const uint64_t generation = ++buildGeneration_;
    root_ = std::make_unique<TreemapNode>();
    root_->name = path.empty() ? L"Computer" : path;
    root_->path = path;
    root_->isDirectory = true;
    root_->totalSizeBytes = 0;

    if (!engine_) {
        emptyStateText_ = L"Waiting for index snapshot…";
        return;
    }
    auto snapshot = engine_->ReadSnapshot();
    if (!snapshot) {
        emptyStateText_ = L"Waiting for index snapshot…";
        return;
    }

    auto it = snapshot->find(path);
    if (it == snapshot->end()) {
        emptyStateText_ = std::wstring(kTreemapNoSizedDataStateText);
        return;
    }
    const auto& directory = it->second;
    if (directory.status != ffprotocol::DirectoryEnumerationStatus::Success) {
        emptyStateText_ = std::wstring(kTreemapNoSizedDataStateText);
        return;
    }

    const bool categoryFilterActive = categoryEngine_ != nullptr && !categoryId_.empty();

    const auto joinPath = [](const std::wstring& base, const std::wstring& name) {
        if (base.empty()) return name;
        if (base.back() == L'\\' || base.back() == L'/') return base + name;
        return base + L"\\" + name;
    };

    const HWND targetWindow = owner_;
    const auto lifetime = lifetime_;
    const auto engine = engine_;
    const auto requestAggregate = [engine, targetWindow, lifetime, generation, categoryFilterActive](const TreemapNode& node) {
        if (engine == nullptr || targetWindow == nullptr || node.volumeRowId == 0 ||
            (node.fileIdLow == 0 && node.fileIdHigh == 0) || !node.calculating || categoryFilterActive) {
            return;
        }
        const std::wstring nodePath = node.path;
        engine->RequestFolderAggregate(
            node.volumeRowId, node.fileIdLow, node.fileIdHigh,
            [targetWindow, lifetime, generation, nodePath](uint64_t /*requestId*/,
                                                            ffprotocol::FolderAggregateStatus status,
                                                            uint64_t /*itemCount*/, uint64_t totalSizeBytes,
                                                            uint64_t totalAllocatedSizeBytes,
                                                            uint64_t allocatedBytesKnownCount,
                                                            uint64_t allocatedBytesTotalCount) {
                if (!lifetime->load(std::memory_order_acquire)) return;
                auto payload = std::make_unique<TreemapAggregatePayload>();
                payload->generation = generation;
                payload->path = nodePath;
                payload->status = status;
                payload->totalSizeBytes = totalSizeBytes;
                // allocated-size-storage-accuracy 7.2: allocated subtotal +
                // completeness pair ride the same async result.
                payload->totalAllocatedSizeBytes = totalAllocatedSizeBytes;
                payload->allocatedBytesKnownCount = allocatedBytesKnownCount;
                payload->allocatedBytesTotalCount = allocatedBytesTotalCount;
                if (PostMessageW(targetWindow, WM_APP_TREEMAP_AGGREGATE, 0,
                                 reinterpret_cast<LPARAM>(payload.get()))) {
                    payload.release();
                }
            });
    };

    // Directory entries in degraded snapshots do not carry recursive sizes.
    // Only the scope's depth-1 entries become tiles (the layout renders
    // root_->children exclusively), and only those directories get one
    // aggregate request each -- the build never walks the whole indexed
    // subtree, which would flood both the UI thread and the engine on large
    // volumes. Unknown subtrees remain marked calculating rather than being
    // rendered as misleading zero-byte tiles until their aggregate resolves.
    for (const auto& entry : directory.entries) {
        auto node = std::make_unique<TreemapNode>();
        node->name = entry.name;
        node->path = joinPath(path, entry.name);
        node->isDirectory = entry.isDirectory;
        node->sizeBytes = entry.sizeBytes;
        // allocated-size-storage-accuracy 7.2: per-entry size-on-disk with
        // its explicit known state.
        node->allocatedSizeBytes = entry.allocatedSizeBytes;
        node->allocatedSizeKnown = entry.allocatedSizeKnown;
        node->totalSizeBytes = entry.isDirectory ? 0 : entry.sizeBytes;
        node->calculating = entry.isDirectory;
        node->volumeRowId = entry.volumeRowId;
        node->fileIdLow = entry.fileIdLow;
        node->fileIdHigh = entry.fileIdHigh;
        node->depth = 1;
        node->parent = root_.get();
        if (node->isDirectory) {
            if (node->volumeRowId == 0) {
                // Degraded scope: no engine aggregate can ever arrive.
                // Resolve from the snapshot (same fallback the storage
                // list path uses) instead of stalling unknown forever.
                ResolveDegradedTreemapTotals(
                    *node, AggregateSnapshotDirectory(*snapshot, node->path));
            } else {
                requestAggregate(*node);
            }
        }
        root_->children.push_back(std::move(node));
    }

    if (categoryFilterActive) {
        FilterTreemapTree(*root_, *categoryEngine_, categoryId_);
    }

    // Totals must reflect the displayed (possibly filtered) tree: without
    // this the scope total stays zero and every share is unknown, even when
    // every child size is already known. Aggregate completions recompute
    // again on arrival (HandleAggregateCompletion).
    RecomputeTotals(*root_);
    RebuildLayout();
}

void TreemapView::Render(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory, D2D1_SIZE_F viewportSize, float offsetX, float offsetY) {
    std::lock_guard<std::recursive_mutex> lock(uiaMutex_);
    if (!visible_.load(std::memory_order_acquire)) return;

    EnsureCreated(context, dwriteFactory);
    const ffui::UiTheme theme = ffui::GetUiTheme(darkTheme_);
    context->Clear(theme.background);

    if (layout_.empty()) {
        // ui-v3 hardening V3-07: a scope load in progress (or a scope with no
        // sized data) is a deliberate state surface with honest text — never a
        // silent blank canvas.
        std::wstring message;
        {
            message = emptyStateText_;
        }
        if (!message.empty()) {
            // ui-overhaul phase 9: center the honest state in the treemap
            // viewport instead of tucking it under the mode toolbar.
            const float lineHeight = 18.0f;
            const float centerY = (std::max)(0.0f, (viewportSize.height - lineHeight) * 0.5f);
            D2D1_RECT_F messageRect = D2D1::RectF(16.0f, centerY,
                                                  (std::max)(16.0f, viewportSize.width - 16.0f),
                                                  centerY + lineHeight);
            context->DrawText(message.c_str(), static_cast<UINT32>(message.size()),
                              textFormat_.Get(), messageRect, textBrush_.Get(),
                              D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
        }
        return;
    }

    // Capture the same offset/scale the shell applies to draw the treemap so
    // HandleMouseMove can convert client coordinates back into normalized
    // 0..100 treemap space.
    viewportSize_ = viewportSize;
    offsetX_ = offsetX;
    offsetY_ = offsetY;

    const float scaleX = viewportSize.width / 100.0f;
    const float scaleY = viewportSize.height / 100.0f;

    // Rounded tiles use the shared small radius; UiFillRoundedRect clamps it to
    // half the smaller tile side so tiny tiles never produce malformed geometry.
    constexpr float tileRadius = ffui::UiMetrics::kRadiusSmall;

    // High Contrast suppresses the translucent interaction overlays so the
    // user's system colors are never layered over (see UITheme.h).
    const bool hoverLiftEnabled = hoveredNode_ != nullptr && !ffui::UiSystemHighContrast();

    for (const auto& rect : layout_) {
        const float rx = rect.x * scaleX;
        const float ry = rect.y * scaleY;
        const float rw = rect.width * scaleX;
        const float rh = rect.height * scaleY;

        if (rx + rw < 0 || ry + rh < 0 || rx > viewportSize.width || ry > viewportSize.height) {
            continue;
        }

        // Draw each tile inset by ~1 DIP per side, leaving a ~2 DIP gutter
        // between adjacent tiles. Layout computation is untouched; only the
        // drawing rect is inset, shrinking on tiny tiles so it never inverts.
        const float inset = std::min(1.0f, std::min(rw, rh) * 0.25f);
        D2D1_RECT_F drawRect = D2D1::RectF(rx + inset, ry + inset, rx + rw - inset, ry + rh - inset);
        ffui::UiFillRoundedRect(context, drawRect, rect.node->calculating ? calculatingBrush_.Get() : backgroundBrush_.Get(), tileRadius);
        ffui::UiDrawRoundedRect(context, drawRect, borderBrush_.Get(), tileRadius, kBorderWidth);

        if (hoverLiftEnabled && rect.node == hoveredNode_) {
            // Token-coherent hover lift: elevated fill on top of the base tile,
            // plus a subtle accent ring at reduced alpha (replaces the old 2px
            // accent outline).
            ffui::UiFillHoverOverlay(context, drawRect, tileRadius, theme.hoverOverlay, &hoverOverlayBrush_);
            D2D1_COLOR_F hoverRing = theme.accent;
            hoverRing.a *= kHoverRingAlpha;
            if (SUCCEEDED(ffui::UiEnsureSolidBrush(context, hoverRing, &hoverBrush_))) {
                ffui::UiDrawRoundedRect(context, drawRect, hoverBrush_.Get(), tileRadius, kBorderWidth);
            }
        }

        if (rw > 40.0f && rh > 20.0f) {
            std::wstring label = rect.node->name;
            if (rect.node->isDirectory && !label.empty() && label.back() != L'\\') {
                label += L'\\';
            }
            D2D1_RECT_F textRect = D2D1::RectF(rx + 4, ry + 2, rx + rw - 4, ry + rh - 2);
            context->DrawText(label.c_str(), static_cast<UINT32>(label.size()), textFormat_.Get(), textRect, textBrush_.Get());
        }

        // semantic-treemap-uia 7.x: hover metadata comes from the same pure
        // BuildTreemapItemMetadata helper the UIA projection uses, so the
        // rendered text and the accessible description can never drift. Full
        // path, logical size, allocated size (with honest unknown/partial
        // state), and both percentages are the canonical facts (spec
        // storage-drilldown-and-treemap). The tile must be large enough to
        // draw the lines; the FullDescription always carries everything.
        if (rect.node != nullptr && rect.node == hoveredNode_ && rw > 160.0f) {
            const TreemapUiaItemInfo hoverInfo = BuildTileInfoLocked(*rect.node, rect);
            const bool full = rh > 76.0f;    // path/size/disk/percentages
            const bool compact = rh > 52.0f; // path/size
            if (full || compact) {
                const int lineCount = full ? 4 : 2;
                float lineY = ry + rh - (full ? 68.0f : 36.0f);
                for (int i = 0; i < lineCount && static_cast<size_t>(i) < hoverInfo.hoverLines.size(); ++i) {
                    const std::wstring& line = hoverInfo.hoverLines[static_cast<size_t>(i)];
                    D2D1_RECT_F lineRect = D2D1::RectF(rx + 4, lineY, rx + rw - 4, lineY + 16.0f);
                    context->DrawText(line.c_str(), static_cast<UINT32>(line.size()), textFormat_.Get(),
                                      lineRect, textBrush_.Get());
                    lineY += 17.0f;
                }
            }
        }
    }
}

bool TreemapView::HandleNotify(LPARAM lParam) {
    (void)lParam;
    return false;
}

bool TreemapView::HandleAggregateCompletion(LPARAM lParam) {
    std::unique_ptr<TreemapAggregatePayload> payload(reinterpret_cast<TreemapAggregatePayload*>(lParam));
    if (!payload || payload->generation != buildGeneration_.load(std::memory_order_acquire)) return true;
    if (categoryEngine_ != nullptr && !categoryId_.empty()) return true;

    std::wstring scope = currentPath_;
    uint64_t tileIdentity = 0;
    std::wstring oldStatus;
    std::wstring newStatus;
    {
        std::lock_guard<std::recursive_mutex> lock(uiaMutex_);
        if (!visible_.load(std::memory_order_acquire)) return true;

        TreemapNode* node = FindNode(root_.get(), payload->path);
        if (node == nullptr || !node->isDirectory) return true;
        tileIdentity = TreemapNodeIdentityHash(*node, currentPath_);
        oldStatus = node->calculating ? L"Calculating…" : L"Partial";

        if (payload->status == ffprotocol::FolderAggregateStatus::Resolved) {
            node->totalSizeBytes = payload->totalSizeBytes;
            // allocated-size-storage-accuracy 7.2: store the aggregate's
            // allocated subtotal and completeness pair; rendering derives
            // exact / partial / unknown from the counts.
            node->totalAllocatedSizeBytes = payload->totalAllocatedSizeBytes;
            node->allocatedKnownCount = payload->allocatedBytesKnownCount;
            node->allocatedTotalCount = payload->allocatedBytesTotalCount;
            node->calculating = false;
            node->aggregateResolved = true;
            node->aggregateFailed = false;
            newStatus = L"";
        } else {
            // Known-partial, not pending: a failed aggregate must transition
            // out of "Calculating…" into the honest "Partial" state.
            node->totalSizeBytes = 0;
            node->totalAllocatedSizeBytes = 0;
            node->allocatedKnownCount = 0;
            node->allocatedTotalCount = 0;
            node->calculating = false;
            node->aggregateResolved = false;
            node->aggregateFailed = true;
            newStatus = L"Partial";
        }

        if (root_) RecomputeTotals(*root_);
        RebuildLayout();
        InvalidateRect(owner_, nullptr, FALSE);
    }

    // semantic-treemap-uia 25/44.x: the tile keeps its semantic identity --
    // only its metadata / bounds / ItemStatus resolved. Notify the provider
    // after the lock: PropertyChanged (ItemStatus) on the surviving tile.
    UiaEventNotification notification;
    notification.kind = UiaEventKind::PropertyChanged;
    notification.treemap = true;
    notification.treemapScopeHash = UiaHashWide(scope);
    notification.treemapTileHash = tileIdentity;
    notification.propertyId = UIA_ItemStatusPropertyId;
    notification.propertyOldText = std::move(oldStatus);
    notification.propertyNewText = std::move(newStatus);
    EmitUiaEvent(notification);
    return true;
}

bool TreemapView::HandleMouseMove(WPARAM /*wParam*/, LPARAM lParam) {
    if (!visible_.load(std::memory_order_acquire)) return false;
    std::lock_guard<std::recursive_mutex> lock(uiaMutex_);
    if (!visible_.load(std::memory_order_acquire)) return false;
    // lParam holds client coordinates converted to DIPs by WindowShell; the
    // treemap stores its global inset-canvas origin and scales it by
    // viewport/100, so translate back into normalized 0..100 space.
    const float clientX = static_cast<float>(GET_X_LPARAM(lParam));
    const float clientY = static_cast<float>(GET_Y_LPARAM(lParam));
    if (viewportSize_.width <= 0.0f || viewportSize_.height <= 0.0f) return false;
    TreemapNode* node = HitTestClientPoint(layout_, clientX, clientY, offsetX_, offsetY_,
                                           viewportSize_.width, viewportSize_.height);
    if (node != hoveredNode_) {
        hoveredNode_ = node;
        return true;
    }
    return false;
}

bool TreemapView::HandleLButtonDown(WPARAM /*wParam*/, LPARAM lParam) {
    if (!visible_.load(std::memory_order_acquire)) return false;
    std::lock_guard<std::recursive_mutex> lock(uiaMutex_);
    if (!visible_.load(std::memory_order_acquire) || layout_.empty() ||
        viewportSize_.width <= 0.0f || viewportSize_.height <= 0.0f) {
        return false;
    }

    // Resolve the click independently of hover state. A view switch does not
    // guarantee a subsequent WM_MOUSEMOVE (for example when the pointer is
    // already stationary over a tile), so requiring hoveredNode_ made the
    // first click on a freshly opened treemap a no-op.
    const float clientX = static_cast<float>(GET_X_LPARAM(lParam));
    const float clientY = static_cast<float>(GET_Y_LPARAM(lParam));
    hoveredNode_ = HitTestClientPoint(layout_, clientX, clientY, offsetX_, offsetY_,
                                       viewportSize_.width, viewportSize_.height);
    if (navigate_ && hoveredNode_ != nullptr && !hoveredNode_->path.empty()) {
        navigate_(hoveredNode_->path, hoveredNode_->isDirectory);
        return true;
    }
    return false;
}

void TreemapView::SetVolumeInfo(const TreemapVolumeInfo& volume) {
    std::lock_guard<std::recursive_mutex> lock(uiaMutex_);
    volumeInfo_ = volume;
}

// --- semantic projection (semantic-treemap-uia 6/13/21/22) ---

std::wstring TreemapView::UiaScopePath() const {
    std::lock_guard<std::recursive_mutex> lock(uiaMutex_);
    return currentPath_;
}

uint64_t TreemapView::UiaScopeHash() const {
    std::lock_guard<std::recursive_mutex> lock(uiaMutex_);
    return UiaHashWide(currentPath_);
}

int TreemapView::UiaTileCount() const {
    std::lock_guard<std::recursive_mutex> lock(uiaMutex_);
    return static_cast<int>(layout_.size());
}

TreemapUiaItemInfo TreemapView::BuildTileInfoLocked(const TreemapNode& node, const TreemapRect& rect) const {
    // The displayed parent/scope total is the filtered tree's own root total:
    // percentages use the category-filtered displayed scope (existing product
    // behavior). Unknown root totals make the parent percentage unknown too,
    // and so does a partial root: shares of an incomplete denominator are
    // never presented as exact (visual-matrix EX-009).
    const uint64_t parentTotal = root_ ? root_->totalSizeBytes : 0;
    const bool parentKnown =
        root_ ? (!root_->calculating && !root_->aggregateFailed) : false;

    bool offscreen = false;
    if (viewportSize_.width > 0.0f && viewportSize_.height > 0.0f) {
        const float rx = rect.x * (viewportSize_.width / 100.0f);
        const float ry = rect.y * (viewportSize_.height / 100.0f);
        const float rw = rect.width * (viewportSize_.width / 100.0f);
        const float rh = rect.height * (viewportSize_.height / 100.0f);
        if (rx + rw < 0 || ry + rh < 0 || rx > viewportSize_.width || ry > viewportSize_.height) {
            offscreen = true;
        }
    }
    return BuildTreemapItemMetadata(node, parentTotal, parentKnown, volumeInfo_, currentPath_,
                                    buildGeneration_.load(std::memory_order_acquire),
                                    UiaHashWide(currentPath_),
                                    rect.x, rect.y, rect.width, rect.height, offscreen);
}

TreemapUiaItemInfo TreemapView::UiaTileInfo(int index) const {
    std::lock_guard<std::recursive_mutex> lock(uiaMutex_);
    if (index < 0 || static_cast<size_t>(index) >= layout_.size()) return {};
    const TreemapRect& rect = layout_[static_cast<size_t>(index)];
    if (rect.node == nullptr) return {};
    return BuildTileInfoLocked(*rect.node, rect);
}

int TreemapView::UiaTileIndex(uint64_t identityHash) const {
    std::lock_guard<std::recursive_mutex> lock(uiaMutex_);
    const auto it = layoutIndexByIdentity_.find(identityHash);
    if (it == layoutIndexByIdentity_.end()) return -1;
    return static_cast<int>(it->second);
}

bool TreemapView::UiaFindTile(uint64_t identityHash, TreemapUiaItemInfo& out) const {
    std::lock_guard<std::recursive_mutex> lock(uiaMutex_);
    const auto it = layoutIndexByIdentity_.find(identityHash);
    if (it == layoutIndexByIdentity_.end()) return false;
    const TreemapRect& rect = layout_[it->second];
    if (rect.node == nullptr) return false;
    out = BuildTileInfoLocked(*rect.node, rect);
    return true;
}

TreemapUiaItemInfo TreemapView::UiaTileHitTest(float normalizedX, float normalizedY) const {
    std::lock_guard<std::recursive_mutex> lock(uiaMutex_);
    TreemapNode* node = HitTest(layout_, normalizedX, normalizedY);
    if (node == nullptr) return {};
    for (const auto& rect : layout_) {
        if (rect.node == node) return BuildTileInfoLocked(*rect.node, rect);
    }
    return {};
}

} // namespace ffui
