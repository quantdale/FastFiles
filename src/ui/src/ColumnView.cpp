#include "ColumnView.h"
#include "EngineStatusPolicy.h"
#include "SelectionModel.h"
#include "UITheme.h"
#include "UiAnimation.h"
#include "UiStyle.h"
#include "IconCache.h"
#include "ffui/SelectionIdentity.h"
#include "ffui/SnapshotPathLookup.h"
#include "ffprotocol/Diagnostics.h"
#include "ColumnViewUiaProvider.h" // semantic-uia-core-surfaces 2.4

#include <algorithm>
#include <cwchar>
#include <functional>
#include <windows.h>
#include <wrl/implements.h> // Microsoft::WRL::Make

namespace ffui {

namespace {

std::wstring JoinPath(const std::wstring& base, const std::wstring& name) {
    if (base.empty()) {
        return name;
    }
    if (base.back() == L'\\' || base.back() == L'/') {
        return base + name;
    }
    return base + L'\\' + name;
}

// Task 3.2: extension of a display name (L"report.pdf" -> L"pdf"), used to
// key the shared type-icon cache; IconKeyForExtension normalizes the leading
// dot. Dotfiles, extensionless names, and the "C:\" drive rows all yield the
// empty string, which maps to the generic file icon.
std::wstring FileExtensionOf(const std::wstring& fileName) {
    const size_t nameStart = fileName.find_last_of(L"\\/");
    const size_t nameIndex = nameStart == std::wstring::npos ? 0 : nameStart + 1;
    const size_t dot = fileName.find_last_of(L'.');
    if (dot == std::wstring::npos || dot < nameIndex || dot + 1 >= fileName.size()) {
        return {};
    }
    return fileName.substr(dot + 1);
}

void ClearUnavailableColumn(Column& column) {
    column.items.clear();
    column.selectedIndices.clear();
    column.selectionAnchor = -1;
    column.focusIndex = -1;
}

// deterministic-missing-path-state D6: terminal path states (NotFound,
// AccessDenied) force the column to Unavailable, clearing retained rows.
// Transient failures (EnumerationFailed, PublishFailed, etc.) use the
// standard Fail which preserves labeled-stale rows when a last-known-good
// generation exists.
void ApplyColumnFailure(Column& column, SnapshotFailureReason reason, uint64_t generation = 0) {
    bool applied = false;
    if (reason == SnapshotFailureReason::NoLongerAvailable
        || reason == SnapshotFailureReason::AccessDenied
        || reason == SnapshotFailureReason::PathMissing) {
        applied = column.freshness.ForceUnavailable(reason, generation);
    } else {
        applied = column.freshness.Fail(reason, generation);
    }
    if (!applied) return;
    if (column.freshness.state == SnapshotViewState::Unavailable) {
        ClearUnavailableColumn(column);
    }
}

// semantic-uia-core-surfaces 3.x/2.4: the unavailable/status messages drawn
// inside a column, extracted from the Render lambdas so the UIA projection
// exposes exactly the text the user sees.
std::wstring UnavailableMessage(const Column& column) {
    switch (column.freshness.failure) {
        case SnapshotFailureReason::AccessDenied:
            return L"You don't have permission to view this folder.";
        case SnapshotFailureReason::PublishFailed:
            return L"Unable to load this location — snapshot publication failed.";
        case SnapshotFailureReason::EnumerationFailed:
            return L"Unable to load this location — the folder could not be read.";
        case SnapshotFailureReason::SnapshotLoadFailed:
            return L"Unable to load this location — snapshot data could not be read.";
        case SnapshotFailureReason::Disconnected:
            return L"Unable to load this location — the engine is unavailable.";
        case SnapshotFailureReason::PathMissing:
        case SnapshotFailureReason::NoLongerAvailable:
            return L"This folder is no longer available.";
        case SnapshotFailureReason::None:
            break;
    }
    return L"Unable to load this location.";
}

std::wstring StatusMessage(const Column& column) {
    if (column.freshness.state == SnapshotViewState::Loading) return L"Refreshing…";
    if (column.freshness.failure == SnapshotFailureReason::Disconnected) {
        return L"Showing previous results — engine disconnected.";
    }
    if (column.freshness.failure == SnapshotFailureReason::PublishFailed) {
        return L"Showing previous results — refresh failed (snapshot publication failed).";
    }
    return L"Showing previous results — refresh failed.";
}

} // namespace

ColumnView::ColumnView() = default;

ColumnView::~ColumnView() {
    // semantic-uia-core-surfaces 1.1/2.4: flip the shared owner-lifetime
    // token BEFORE the provider is released so any UIA client still holding
    // an element of this surface gets clean failures instead of
    // use-after-free (WM_DESTROY normally releases the provider earlier).
    if (uiaState_) {
        uiaState_->alive = false;
    }
    uiaEvents_.Clear();
    uiaProvider_.Reset();
}

Column ColumnView::MakeComputerRootColumn() {
    Column root;
    root.path = L""; // sentinel: not engine-backed, populated directly below

    const DWORD driveMask = GetLogicalDrives();
    for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
        if ((driveMask & (1u << (letter - L'A'))) == 0) {
            continue;
        }
        wchar_t rootPath[] = {letter, L':', L'\\', L'\0'};
        // Keep the synthetic root consistent with the sidebar's logical-drive
        // list. Removable, optical, RAM-disk, and mapped network roots are
        // valid navigation targets too; inaccessible media is surfaced by
        // the normal degraded enumeration error state after activation.
        const UINT driveType = GetDriveTypeW(rootPath);
        if (driveType == DRIVE_UNKNOWN || driveType == DRIVE_NO_ROOT_DIR) {
            continue;
        }
        ColumnItem item;
        item.name = rootPath;
        item.isDirectory = true;
        root.items.push_back(std::move(item));
    }
    return root;
}

void ColumnView::Initialize(EngineClient* engineClient) {
    engineClient_ = engineClient;

    std::lock_guard<std::mutex> lock(columnsMutex_);
    columns_.push_back(MakeComputerRootColumn());
    focusedColumnIndex_ = 0;
}

void ColumnView::TruncateAfter(int columnIndex) {
    const int pane = activePane_;
    bool truncated = false;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        auto& columns = ActiveColumns();
        if (columnIndex + 1 < static_cast<int>(columns.size())) {
            columns.resize(columnIndex + 1);
            truncated = true;
        }
    }
    // semantic-uia-core-surfaces 2.6: structure events fire post-lock.
    if (truncated) {
        NotifyUia({UiaEventKind::StructureBulkRemoved, pane, -1, -1});
    }
}

void ColumnView::RequestColumn(int /*columnIndex*/, const std::wstring& path) {
    const int pane = activePane_;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        auto& columns = ActiveColumns();
        int& focusedIndex = ActiveFocusedColumnIndex();
        Column column;
        column.path = path;
        columns.push_back(std::move(column));
        focusedIndex = static_cast<int>(columns.size()) - 1;
        if (activePane_ == 0) verticalScrollOffset_ = 0.0f;
        else verticalScrollOffset2_ = 0.0f;
    }
    // semantic-uia-core-surfaces 2.6: structure events fire post-lock.
    NotifyUia({UiaEventKind::StructureBulkAdded, pane, -1, -1});
    if (engineClient_) {
        engineClient_->RequestDirectory(path);
    }
}

void ColumnView::SelectSingle(Column& column, int itemIndex) {
    ApplySelectionClick(column.selectedIndices, column.selectionAnchor, column.focusIndex, itemIndex, false, false);
}

void ColumnView::ToggleSelection(Column& column, int itemIndex) {
    ApplySelectionClick(column.selectedIndices, column.selectionAnchor, column.focusIndex, itemIndex, true, false);
}

void ColumnView::SelectRange(Column& column, int itemIndex) {
    ApplySelectionClick(column.selectedIndices, column.selectionAnchor, column.focusIndex, itemIndex, false, true);
}

void ColumnView::ActivateItem(int columnIndex, int itemIndex, bool control, bool shift) {
    const int pane = activePane_;
    UiaSelectionSnapshot before;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        before = CaptureUiaSelectionLocked(pane, columnIndex);
    }
    std::wstring childPath;
    bool isDirectory = false;
    bool clearedEmptyArea = false;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        auto& columns = ActiveColumns();
        if (columnIndex < 0 || columnIndex >= static_cast<int>(columns.size())) {
            return;
        }
        Column& column = columns[columnIndex];
        if (itemIndex < 0 || itemIndex >= static_cast<int>(column.items.size())) {
            // selection-input-hardening: clicking empty content area below the
            // rows clears the selection and focus like Explorer (a plain click
            // only; Ctrl/Shift clicks never destructively clear).
            if (!control && !shift &&
                (!column.selectedIndices.empty() || column.focusIndex >= 0)) {
                column.selectedIndices.clear();
                column.selectionAnchor = -1;
                column.focusIndex = -1;
                clearedEmptyArea = true;
            }
            return;
        }
        if (shift) {
            SelectRange(column, itemIndex);
        } else if (control) {
            ToggleSelection(column, itemIndex);
        } else {
            SelectSingle(column, itemIndex);
        }
        isDirectory = column.items[itemIndex].isDirectory;
        childPath = JoinPath(column.path, column.items[itemIndex].name);
    }

    // selection-input-hardening: empty-area clear fires the same selection
    // transition diff as a normal click.
    if (clearedEmptyArea) {
        NotifyUiaSelectionTransition(before, pane);
        return;
    }

    if (control || shift) {
        {
            std::lock_guard<std::mutex> lock(columnsMutex_);
            ActiveFocusedColumnIndex() = columnIndex;
        }
        NotifyUiaSelectionTransition(before, pane);
        return;
    }

    TruncateAfter(columnIndex);
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        ActiveFocusedColumnIndex() = columnIndex;
    }

    if (isDirectory) {
        // Overrides focusedColumnIndex_ to the newly created column, which
        // is the correct/deeper focus target.
        RequestColumn(columnIndex + 1, childPath);
    }
    NotifyUiaSelectionTransition(before, pane);
}

void ColumnView::OnKeyDown(int virtualKey) {
    // quick-access-home 4.5: the Home pane owns its own keyboard model.
    if (homePane_[activePane_]) {
        HandleHomeKeyDown(virtualKey);
        return;
    }
    const int pane = activePane_;
    UiaSelectionSnapshot before;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        before = CaptureUiaSelectionLocked(pane, ActiveFocusedColumnIndex());
    }
    int activateColumnIndex = -1;
    int activateItemIndex = -1;
    bool enterOpensFile = false;

    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        auto& columns = ActiveColumns();
        int& focusedIndex = ActiveFocusedColumnIndex();
        if (columns.empty()) {
            return;
        }
        focusedIndex = std::clamp(focusedIndex, 0, static_cast<int>(columns.size()) - 1);
        Column& focused = columns[focusedIndex];
        const bool controlHeld = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        // selection-input-hardening: the visible-page row count for
        // PageUp/PageDown, derived from the viewport remembered by Render and
        // the same banner inset the render path applies.
        const float contentTopInset = ColumnContentTopInset(focused.freshness.state);
        const float visibleHeight = (std::max)(0.0f, lastViewportHeight_ - kBadgeHeight - contentTopInset);
        const int pageRows = (std::max)(1, static_cast<int>(visibleHeight / kRowHeight));

        switch (virtualKey) {
            case VK_UP:
                if (!focused.items.empty()) {
                    SelectSingle(focused, std::max(0, focused.focusIndex - 1));
                }
                break;
            case VK_DOWN:
                if (!focused.items.empty()) {
                    SelectSingle(focused, std::min(static_cast<int>(focused.items.size()) - 1, focused.focusIndex + 1));
                }
                break;
            case VK_LEFT:
                focusedIndex = std::max(0, focusedIndex - 1);
                break;
            case VK_RIGHT:
                focusedIndex = std::min(static_cast<int>(columns.size()) - 1, focusedIndex + 1);
                break;
            case VK_PRIOR:
                // selection-input-hardening: standard list semantics -- focus
                // (+selection unless Ctrl) moves a full page; Shift extends
                // from the anchor.
                ApplyKeyboardFocusMove(focused.selectedIndices, focused.selectionAnchor, focused.focusIndex,
                                       ListFocusTarget(focused.focusIndex,
                                                       static_cast<int>(focused.items.size()), -pageRows),
                                       controlHeld, shiftHeld);
                break;
            case VK_NEXT:
                ApplyKeyboardFocusMove(focused.selectedIndices, focused.selectionAnchor, focused.focusIndex,
                                       ListFocusTarget(focused.focusIndex,
                                                       static_cast<int>(focused.items.size()), pageRows),
                                       controlHeld, shiftHeld);
                break;
            case VK_HOME:
                ApplyKeyboardFocusMove(focused.selectedIndices, focused.selectionAnchor, focused.focusIndex,
                                       ListFocusTarget(-1, static_cast<int>(focused.items.size()), INT_MIN),
                                       controlHeld, shiftHeld);
                break;
            case VK_END:
                ApplyKeyboardFocusMove(focused.selectedIndices, focused.selectionAnchor, focused.focusIndex,
                                       ListFocusTarget(-1, static_cast<int>(focused.items.size()), INT_MAX),
                                       controlHeld, shiftHeld);
                break;
            case VK_RETURN: {
                // selection-input-hardening: Enter over a focused NON-DIRECTORY
                // opens the item via the shell-wired `item.open` seam (the same
                // path double-click uses); directories keep navigating here.
                // No focused item: no-op (never an empty-area clear).
                const bool focusValid = focused.focusIndex >= 0 &&
                                        focused.focusIndex < static_cast<int>(focused.items.size());
                if (focusValid) {
                    activateColumnIndex = focusedIndex;
                    activateItemIndex = focused.focusIndex;
                    enterOpensFile = EnterOpensFile(focusValid, focused.items[focused.focusIndex].isDirectory);
                }
                break;
            }
            case 'A':
                // harden-selection-model-semantics 3.1: route Ctrl+A through the
                // same tested Select All seam used by ColumnView::SelectAll so the
                // real keyboard path cannot drift from the proven select-all
                // semantic. SelectAllItems also correctly invalidates anchor/focus
                // on an empty column (canonical: "no valid anchor/focus").
                if (controlHeld) {
                    SelectAllItems(focused.selectedIndices, focused.selectionAnchor, focused.focusIndex,
                                   static_cast<int>(focused.items.size()));
                }
                break;
            default:
                break;
        }
    } // release columnsMutex_ before ActivateItem (which re-acquires it) to avoid a self-deadlock

    if (activateColumnIndex >= 0) {
        ActivateItem(activateColumnIndex, activateItemIndex);
    }
    if (enterOpensFile && openFocusedItemCallback_) {
        openFocusedItemCallback_();
    }
    NotifyUiaSelectionTransition(before, pane);
}

void ColumnView::OnMouseDown(D2D1_POINT_2F clientPoint, float scrollOffset, float viewportWidth, bool control, bool shift) {
    if (clientPoint.y < kBadgeHeight) {
        // recovery-degraded-browsing D3: the top status chip is the retry
        // affordance for the Engine unavailable state only (modernize-ui-
        // appearance 3.6 kept the chip non-interactive; the Unavailable
        // state now genuinely offers an action).
        if (engineState_.load() == EngineConnectionState::Unavailable && retryEngineCallback_) {
            retryEngineCallback_();
        }
        return;
    }
    
    if (dualPane_) {
        const float paneWidth = viewportWidth / 2.0f;
        const int clickedPane = clientPoint.x < paneWidth ? 0 : 1;
        if (clickedPane != activePane_) {
            ActivatePane(clickedPane);
        }
    }
    
    const int pane = activePane_;
    if (homePane_[pane]) {
        // quick-access-home 4.8: the section-header Clear affordances first
        // (their rects are recorded in viewport coordinates by RenderHomePane).
        const auto& clears = homeClearButtons_[pane];
        const auto inRect = [&clientPoint](const D2D1_RECT_F& rect) {
            return clientPoint.x >= rect.left && clientPoint.x < rect.right &&
                   clientPoint.y >= rect.top && clientPoint.y < rect.bottom;
        };
        if (inRect(clears.recentFolders) || inRect(clears.recentSearches)) {
            if (homeActivateCallback_ != nullptr) {
                HomeActivation activation;
                activation.kind = inRect(clears.recentFolders)
                    ? HomeActionKind::ClearRecentFolders : HomeActionKind::ClearRecentSearches;
                homeActivateCallback_(activation);
            }
            return;
        }
        // quick-access-home 4.4: hit-test the Home surface. `clientPoint` is
        // converted to pane-local coords (pane origin removed) and the vertical
        // scroll offset is folded into the content offset so the Point2F lands
        // in the same coordinate space the layout was drawn in.
        const float paneWidth = dualPane_ ? viewportWidth / 2.0f : viewportWidth;
        const float paneX = (dualPane_ && pane == 1) ? paneWidth : 0.0f;
        const D2D1_POINT_2F localPoint = D2D1::Point2F(clientPoint.x - paneX, clientPoint.y);
        const HomeLayoutResult layout = ComputeHomeLayoutForPane(pane);
        const float vso = pane == 0 ? verticalScrollOffset_ : verticalScrollOffset2_;
        const float contentOffsetY = kBadgeHeight - vso;
        const int hit = ComputeHomeItemHitTest(layout, localPoint, contentOffsetY);
        HomeFocusEntry& focus = pane == 0 ? homeFocus_[0] : homeFocus_[1];
        // quick-access-home hardening (HOME-POST-02): clicking a non-actionable
        // item (unavailable known folder / hint placeholder) neither focuses
        // nor activates; a miss clears focus like before.
        const bool actionable = hit >= 0 && FindHomeItem(layout, hit) != nullptr &&
                                HomeItemFocusable(homeData_, *FindHomeItem(layout, hit));
        if (actionable) {
            focus = {HomeFocusTarget::Item, hit};
            // quick-access-home 4.4: Ctrl (control) => open in new tab; the
            // activation always routes through the callback.
            ActivateHomeItem(pane, hit, control);
        } else {
            focus = {};
        }
        return;
    }

    const float effectiveScrollOffset = activePane_ == 0 ? scrollOffset : scrollOffset2_;
    const float verticalScrollOffset = activePane_ == 0 ? verticalScrollOffset_ : verticalScrollOffset2_;
    // selection-input-hardening: the row lookup must subtract the same status-
    // banner inset the render path applies for the hit column, or every click
    // lands one row low while "Refreshing…" / stale banners are visible.
    float contentTopInset = 0.0f;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        const int columnIndex = ComputeColumnIndexAtX(clientPoint, effectiveScrollOffset,
                                                      viewportWidth, dualPane_, activePane_);
        const auto& columns = ActiveColumns();
        if (columnIndex >= 0 && columnIndex < static_cast<int>(columns.size())) {
            contentTopInset = ColumnContentTopInset(columns[static_cast<size_t>(columnIndex)].freshness.state);
        }
    }
    const auto hit = ComputeColumnHitTest(clientPoint, effectiveScrollOffset, verticalScrollOffset,
                                          contentTopInset, viewportWidth, dualPane_, activePane_);
    ActivateItem(hit.columnIndex, hit.itemIndex, control, shift);
}

void ColumnView::SetIconCache(IconCache* iconCache) {
    iconCache_ = iconCache;
}

void ColumnView::SetRepaintCallback(std::function<void()> repaint) {
    repaint_ = std::move(repaint);
}

void ColumnView::SetOpenFocusedItemCallback(std::function<void()> openFocusedItem) {
    openFocusedItemCallback_ = std::move(openFocusedItem);
}

void ColumnView::OnMouseMove(D2D1_POINT_2F clientPoint, float scrollOffset, float viewportWidth) {
    if (clientPoint.y < kBadgeHeight) {
        return; // over the status badge: not hovering any row
    }
    // selection-input-hardening: hover is paint-only and must never change the
    // active pane. Switching panes on mere mouse-over stole keyboard focus
    // (Ctrl+A / Delete / Enter would target the hovered pane); pane activation
    // happens only on an actual click (OnMouseDown) or keyboard action.
    const int pane = activePane_;
    if (homePane_[pane]) {
        // quick-access-home 4.4: paint-only hover tracking for the Home surface
        // (no selection change). `hoverColumn_` is set to -1 in Home mode so the
        // column renderer's hover test (which requires a valid column) never
        // matches; the Home draw loop tests `hoverColumn_ == -1` instead.
        // quick-access-home hardening (HOME-POST-02): non-actionable items
        // (unavailable known folders / the hint placeholder) are never hover
        // targets, so no hover overlay claims they are actionable cards.
        const float paneWidth = dualPane_ ? viewportWidth / 2.0f : viewportWidth;
        const float paneX = (dualPane_ && pane == 1) ? paneWidth : 0.0f;
        const D2D1_POINT_2F localPoint = D2D1::Point2F(clientPoint.x - paneX, clientPoint.y);
        const HomeLayoutResult layout = ComputeHomeLayoutForPane(pane);
        const float vso = pane == 0 ? verticalScrollOffset_ : verticalScrollOffset2_;
        const int rawHit = ComputeHomeItemHitTest(layout, localPoint, kBadgeHeight - vso);
        const int hit = (rawHit >= 0 && FindHomeItem(layout, rawHit) != nullptr &&
                         HomeItemFocusable(homeData_, *FindHomeItem(layout, rawHit)))
            ? rawHit : -1;
        if (hit != hoverItem_ || hoverColumn_ != -1 || !hoverActive_) {
            hoverItem_ = hit;
            hoverColumn_ = -1;
            hoverActive_ = true;
            hoverEnterMs_ = static_cast<uint64_t>(GetTickCount64());
            hoverOpacity_.AnimateTo(1.0f, kUiAnimationDefaultMs, hoverEnterMs_);
            if (repaint_) {
                repaint_();
            }
        }
        return;
    }

    const float effectiveScrollOffset = activePane_ == 0 ? scrollOffset : scrollOffset2_;
    const float verticalScrollOffset = activePane_ == 0 ? verticalScrollOffset_ : verticalScrollOffset2_;
    // selection-input-hardening: same banner inset as the mouse-down path so
    // the hover pill tracks the row the user actually points at.
    float contentTopInset = 0.0f;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        const int columnIndex = ComputeColumnIndexAtX(clientPoint, effectiveScrollOffset,
                                                      viewportWidth, dualPane_, activePane_);
        const auto& columns = ActiveColumns();
        if (columnIndex >= 0 && columnIndex < static_cast<int>(columns.size())) {
            contentTopInset = ColumnContentTopInset(columns[static_cast<size_t>(columnIndex)].freshness.state);
        }
    }
    const auto hit = ComputeColumnHitTest(clientPoint, effectiveScrollOffset, verticalScrollOffset,
                                          contentTopInset, viewportWidth, dualPane_, activePane_);
    hoverColumn_ = hit.columnIndex;
    hoverItem_ = hit.itemIndex;
    hoverActive_ = true;
    hoverEnterMs_ = static_cast<uint64_t>(GetTickCount64());
    hoverOpacity_.AnimateTo(1.0f, kUiAnimationDefaultMs, hoverEnterMs_);
    if (repaint_) {
        repaint_();
    }
}

void ColumnView::OnMouseLeave() {
    hoverActive_ = false;
    hoverLeaveMs_ = static_cast<uint64_t>(GetTickCount64());
    hoverOpacity_.AnimateTo(0.0f, kUiAnimationDefaultMs, hoverLeaveMs_);
    if (repaint_) {
        repaint_();
    }
}

bool ColumnView::HoverAnimating() const {
    return hoverActive_ ||
           (hoverLeaveMs_ != 0 && (GetTickCount64() - hoverLeaveMs_) < static_cast<uint64_t>(kUiAnimationDefaultMs));
}

std::vector<std::wstring> ColumnView::ActiveSelectionPaths() const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    std::vector<std::wstring> paths;
    const auto& columns = ActiveColumns();
    const int& focusedIndex = const_cast<ColumnView*>(this)->ActiveFocusedColumnIndex();
    if (focusedIndex < 0 || focusedIndex >= static_cast<int>(columns.size())) {
        return paths;
    }
    const Column& column = columns[focusedIndex];
    for (int index : column.selectedIndices) {
        if (index >= 0 && index < static_cast<int>(column.items.size())) {
            paths.push_back(JoinPath(column.path, column.items[index].name));
        }
    }
    return paths;
}

std::vector<SelectionItem> ColumnView::ActiveSelectionItems() const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    std::vector<SelectionItem> items;
    const auto& columns = ActiveColumns();
    const int& focusedIndex = const_cast<ColumnView*>(this)->ActiveFocusedColumnIndex();
    if (focusedIndex < 0 || focusedIndex >= static_cast<int>(columns.size())) return items;
    const Column& column = columns[focusedIndex];
    for (int index : column.selectedIndices) {
        if (index >= 0 && index < static_cast<int>(column.items.size())) {
            items.push_back({JoinPath(column.path, column.items[index].name), column.items[index].isDirectory});
        }
    }
    return items;
}

void ColumnView::SelectAll() {
    const int pane = activePane_;
    UiaSelectionSnapshot before;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        before = CaptureUiaSelectionLocked(pane, ActiveFocusedColumnIndex());
    }
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        auto& columns = ActiveColumns();
        int& focusedIndex = ActiveFocusedColumnIndex();
        if (focusedIndex < 0 || focusedIndex >= static_cast<int>(columns.size())) return;
        Column& column = columns[focusedIndex];
        SelectAllItems(column.selectedIndices, column.selectionAnchor, column.focusIndex,
                       static_cast<int>(column.items.size()));
    }
    NotifyUiaSelectionTransition(before, pane);
}

void ColumnView::RefreshActiveColumn() {
    const std::wstring path = ActivePanePath();
    if (!path.empty() && engineClient_ != nullptr) {
        MarkPathLoading(path);
        engineClient_->RequestDirectory(path);
    }
}

void ColumnView::RefreshAllColumns() {
    if (engineClient_ == nullptr) return;
    std::vector<std::wstring> paths;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        const auto collect = [&paths](std::vector<Column>& columns) {
            for (const auto& column : columns) {
                if (!column.path.empty()) paths.push_back(column.path);
            }
            for (auto& column : columns) {
                if (!column.path.empty()) column.freshness.BeginRequest();
            }
        };
        collect(columns_);
        collect(columns2_);
    }
    for (const auto& path : paths) engineClient_->RequestDirectory(path);
}

// semantic-uia-core-surfaces 2.x: the real vertical-scroll clamp for one
// pane (Home clamps against the laid-out content height; filesystem panes
// against the focused column's item count). ScrollVertical and the UIA
// Scroll pattern share this single implementation.
void ColumnView::ClampVerticalScrollForPane(int pane, float delta, float viewportHeight) {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    if (pane != 0 && pane != 1) return;
    if (homePane_[pane]) {
        const HomeLayoutResult layout = ComputeHomeLayoutForPane(pane);
        const float visibleHeight = (std::max)(0.0f, viewportHeight - kBadgeHeight);
        const float maximum = (std::max)(0.0f, layout.contentHeight - visibleHeight);
        float& offset = pane == 0 ? verticalScrollOffset_ : verticalScrollOffset2_;
        offset = std::clamp(offset + delta, 0.0f, maximum);
        return;
    }
    auto& columns = pane == 0 ? columns_ : columns2_;
    const int focusedIndex = pane == 0 ? focusedColumnIndex_ : focusedColumnIndex2_;
    size_t itemCount = 0;
    float contentTopInset = 0.0f;
    if (focusedIndex >= 0 && focusedIndex < static_cast<int>(columns.size())) {
        itemCount = columns[static_cast<size_t>(focusedIndex)].items.size();
        // selection-input-hardening: same banner inset as render/hit-test.
        contentTopInset = ColumnContentTopInset(columns[static_cast<size_t>(focusedIndex)].freshness.state);
    }
    const float visibleHeight = (std::max)(0.0f, viewportHeight - kBadgeHeight - contentTopInset);
    const float maximum = (std::max)(0.0f, static_cast<float>(itemCount) * kRowHeight - visibleHeight);
    float& offset = pane == 0 ? verticalScrollOffset_ : verticalScrollOffset2_;
    offset = std::clamp(offset + delta, 0.0f, maximum);
}

void ColumnView::ScrollVertical(float delta, float viewportHeight) {
    ClampVerticalScrollForPane(activePane_, delta, viewportHeight);
}

void ColumnView::EnsureFocusedItemVisible(float viewportHeight) {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    // quick-access-home 4.x: Home uses the focused item's rect from the layout.
    // quick-access-home hardening (HOME-POST-03): a focused Clear stop uses its
    // section header's rect.
    const int pane = activePane_;
    if (homePane_[pane]) {
        const HomeFocusEntry focus = pane == 0 ? homeFocus_[0] : homeFocus_[1];
        if (focus.target == HomeFocusTarget::None) return;
        const HomeLayoutResult layout = ComputeHomeLayoutForPane(pane);
        float itemTop = 0.0f;
        float itemBottom = 0.0f;
        if (focus.target == HomeFocusTarget::Item) {
            for (const auto& item : layout.items) {
                if (item.globalIndex == focus.itemIndex) {
                    itemTop = item.y;
                    itemBottom = item.y + item.height;
                    break;
                }
            }
        } else {
            const int wantedSection = focus.target == HomeFocusTarget::ClearRecentFolders
                ? static_cast<int>(HomeSectionKind::RecentFolders)
                : static_cast<int>(HomeSectionKind::RecentSearches);
            for (const auto& section : layout.sections) {
                if (section.sectionIndex == wantedSection) {
                    itemTop = section.headerY;
                    itemBottom = section.headerY + section.headerHeight;
                    break;
                }
            }
        }
        if (itemTop == 0.0f && itemBottom == 0.0f) return;
        const float visibleHeight = (std::max)(0.0f, viewportHeight - kBadgeHeight);
        float& offset = pane == 0 ? verticalScrollOffset_ : verticalScrollOffset2_;
        if (itemTop < offset) offset = itemTop;
        else if (itemBottom > offset + visibleHeight) offset = itemBottom - visibleHeight;
        const float maximum = (std::max)(0.0f, layout.contentHeight - visibleHeight);
        offset = std::clamp(offset, 0.0f, maximum);
        return;
    }
    auto& columns = ActiveColumns();
    const int focusedIndex = ActiveFocusedColumnIndex();
    if (focusedIndex < 0 || focusedIndex >= static_cast<int>(columns.size())) return;
    const Column& column = columns[focusedIndex];
    const int itemIndex = column.focusIndex;
    if (itemIndex < 0) return;
    // selection-input-hardening: same banner inset as render/hit-test.
    const float visibleHeight =
        (std::max)(0.0f, viewportHeight - kBadgeHeight - ColumnContentTopInset(column.freshness.state));
    const float top = static_cast<float>(itemIndex) * kRowHeight;
    const float bottom = top + kRowHeight;
    float& offset = activePane_ == 0 ? verticalScrollOffset_ : verticalScrollOffset2_;
    if (top < offset) offset = top;
    else if (bottom > offset + visibleHeight) offset = bottom - visibleHeight;
    const float maximum = (std::max)(0.0f, static_cast<float>(column.items.size()) * kRowHeight - visibleHeight);
    offset = std::clamp(offset, 0.0f, maximum);
}

void ColumnView::NavigateToPath(const std::wstring& path, const std::wstring& selectName) {
    if (path.empty()) return;
    const std::vector<std::wstring> paths = BuildNavigationPathChain(path);
    if (paths.empty()) return;
    const int pane = activePane_;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        auto& columns = ActiveColumns();
        int& focusedIndex = ActiveFocusedColumnIndex();
        columns.clear();
        for (const auto& columnPath : paths) {
            Column column;
            column.path = columnPath;
            columns.push_back(std::move(column));
        }
        focusedIndex = static_cast<int>(columns.size()) - 1;
        if (activePane_ == 0) verticalScrollOffset_ = 0.0f;
        else verticalScrollOffset2_ = 0.0f;
        pendingSelectionName_ = selectName;
        pendingSelectionPath_ = selectName.empty() ? std::wstring{} : paths.back();
    }
    // semantic-uia-core-surfaces 2.6: structure events fire post-lock.
    NotifyUia({UiaEventKind::StructureBulkRemoved, pane, -1, -1});
    NotifyUia({UiaEventKind::StructureBulkAdded, pane, -1, -1});
    if (engineClient_ != nullptr) {
        for (const auto& columnPath : paths) engineClient_->RequestDirectory(columnPath);
    }
}

void ColumnView::NavigateToComputerRoot() {
    const int pane = activePane_;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        auto& columns = ActiveColumns();
        columns.clear();
        columns.push_back(MakeComputerRootColumn());
        ActiveFocusedColumnIndex() = 0;
        if (activePane_ == 0) verticalScrollOffset_ = 0.0f;
        else verticalScrollOffset2_ = 0.0f;
        pendingSelectionName_.clear();
        pendingSelectionPath_.clear();
        hoverColumn_ = -1;
        hoverItem_ = -1;
        hoverActive_ = false;
    }
    // semantic-uia-core-surfaces 2.6: structure events fire post-lock.
    NotifyUia({UiaEventKind::StructureBulkRemoved, pane, -1, -1});
    NotifyUia({UiaEventKind::StructureBulkAdded, pane, -1, -1});
}

void ColumnView::NavigateToHierarchy(const std::wstring& fullPath, bool isDirectory) {
    if (fullPath.size() < 3) return;
    const size_t slash = fullPath.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return;
    const std::wstring containingPath = fullPath.substr(0, slash);
    const std::wstring selectedName = fullPath.substr(slash + 1);
    std::vector<std::wstring> paths;
    if (containingPath.size() >= 3 && containingPath[1] == L':') {
        std::wstring current = containingPath.substr(0, 3);
        paths.push_back(current);
        size_t start = 3;
        while (start < containingPath.size()) {
            const size_t end = containingPath.find(L'\\', start);
            const std::wstring component = containingPath.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
            if (!component.empty()) {
                if (current.back() != L'\\') current += L'\\';
                current += component;
                paths.push_back(current);
            }
            if (end == std::wstring::npos) break;
            start = end + 1;
        }
    } else {
        paths.push_back(containingPath);
    }
    const int pane = activePane_;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        auto& columns = ActiveColumns();
        int& focusedIndex = ActiveFocusedColumnIndex();
        columns.clear();
        for (const auto& path : paths) {
            Column column;
            column.path = path;
            columns.push_back(std::move(column));
        }
        focusedIndex = static_cast<int>(columns.size()) - 1;
        pendingSelectionName_ = selectedName;
        pendingSelectionPath_ = paths.empty() ? std::wstring{} : paths.back();
        if (activePane_ == 0) verticalScrollOffset_ = 0.0f;
        else verticalScrollOffset2_ = 0.0f;
    }
    // semantic-uia-core-surfaces 2.6: structure events fire post-lock.
    NotifyUia({UiaEventKind::StructureBulkRemoved, pane, -1, -1});
    NotifyUia({UiaEventKind::StructureBulkAdded, pane, -1, -1});
    for (const auto& path : paths) if (engineClient_ != nullptr) engineClient_->RequestDirectory(path);
    if (isDirectory && engineClient_ != nullptr) engineClient_->RequestDirectory(fullPath);
}

void ColumnView::NavigateToHierarchy(const std::vector<std::wstring>& segments, bool isDirectory) {
    if (segments.size() < 2) return;
    std::vector<std::wstring> paths;
    std::wstring current = segments.front();
    paths.push_back(current);
    for (size_t index = 1; index + 1 < segments.size(); ++index) {
        if (!current.empty() && current.back() != L'\\') current += L'\\';
        current += segments[index];
        paths.push_back(current);
    }
    const int pane = activePane_;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        // Search results are opened in the pane that currently owns focus.
        // The old implementation always rebuilt pane 0, so activating a
        // complete result from the right pane silently changed the left pane
        // instead and left the visible right pane unchanged.
        auto& columns = ActiveColumns();
        int& focusedIndex = ActiveFocusedColumnIndex();
        columns.clear();
        for (const auto& path : paths) {
            Column column;
            column.path = path;
            columns.push_back(std::move(column));
        }
        focusedIndex = static_cast<int>(columns.size()) - 1;
        pendingSelectionName_ = segments.back();
        pendingSelectionPath_ = paths.back();
        if (activePane_ == 0) verticalScrollOffset_ = 0.0f;
        else verticalScrollOffset2_ = 0.0f;
    }
    // semantic-uia-core-surfaces 2.6: structure events fire post-lock.
    NotifyUia({UiaEventKind::StructureBulkRemoved, pane, -1, -1});
    NotifyUia({UiaEventKind::StructureBulkAdded, pane, -1, -1});
    for (const auto& path : paths) if (engineClient_ != nullptr) engineClient_->RequestDirectory(path);
    if (isDirectory && engineClient_ != nullptr) {
        std::wstring folder = paths.back();
        if (folder.back() != L'\\') folder += L'\\';
        engineClient_->RequestDirectory(folder + segments.back());
    }
}

int ColumnView::FocusedItemIndex() const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    const auto& columns = const_cast<ColumnView*>(this)->ActiveColumns();
    const int& focusedIndex = const_cast<ColumnView*>(this)->ActiveFocusedColumnIndex();
    if (focusedIndex < 0 || focusedIndex >= static_cast<int>(columns.size())) return -1;
    return columns[focusedIndex].focusIndex;
}

std::wstring ColumnView::ActivePanePath() const {
    return PanePath(activePane_);
}

std::wstring ColumnView::PanePath(size_t pane) const {
    // stabilize-live-uia-e2e: pane-of-drop-point destination for file drops.
    // quick-access-home 4.2: a Home pane has no filesystem path -- returning the
    // empty string keeps every downstream consumer (file ops, reconciliation,
    // address-bar seeding) safe, identical to the "This PC" root.
    if (pane >= 2 || homePane_[pane]) {
        return {};
    }
    std::lock_guard<std::mutex> lock(columnsMutex_);
    const auto& columns = pane == 0 ? columns_ : columns2_;
    const int& focused = pane == 0 ? focusedColumnIndex_ : focusedColumnIndex2_;
    if (focused < 0 || focused >= static_cast<int>(columns.size())) {
        return {};
    }
    return columns[static_cast<size_t>(focused)].path;
}

// quick-access-home 4.2: data + activation plumbing. These never touch the
// engine; the shell pushes a fresh `HomeData` and owns the activation routing.
void ColumnView::SetHomeData(const HomeData& data) {
    homeData_ = data;
    // quick-access-home hardening (HOME-POST-03): after a data refresh (e.g. a
    // Clear action emptied a section) a focus target that no longer exists must
    // not stay stranded and invisible.
    RevalidateHomeFocus();
    // semantic-uia-core-surfaces 3.4: the Home tree of both panes changed.
    NotifyUia({UiaEventKind::StructureBulkRemoved, 0, -1, -1, -1, -1, /*home=*/true});
    NotifyUia({UiaEventKind::StructureBulkAdded, 0, -1, -1, -1, -1, /*home=*/true});
    NotifyUia({UiaEventKind::StructureBulkRemoved, 1, -1, -1, -1, -1, /*home=*/true});
    NotifyUia({UiaEventKind::StructureBulkAdded, 1, -1, -1, -1, -1, /*home=*/true});
}

void ColumnView::SetHomeActivateCallback(std::function<void(const HomeActivation&)> callback) {
    homeActivateCallback_ = std::move(callback);
}

bool ColumnView::IsPaneHome(int paneIndex) const {
    if (paneIndex < 0 || paneIndex > 1) return false;
    return homePane_[paneIndex];
}

void ColumnView::SetPaneHome(int paneIndex, bool home) {
    if (paneIndex < 0 || paneIndex > 1) return;
    if (home == homePane_[paneIndex]) return;
    homePane_[paneIndex] = home;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        auto& columns = paneIndex == 0 ? columns_ : columns2_;
        int& focusedIndex = paneIndex == 0 ? focusedColumnIndex_ : focusedColumnIndex2_;
        // quick-access-home 4.2: entering Home discards the pane's stale column
        // model so it is never rendered or engine-refreshed while Home is shown.
        columns.clear();
        focusedIndex = 0;
        if (paneIndex == 0) { verticalScrollOffset_ = 0.0f; scrollOffset_ = 0.0f; }
        else { verticalScrollOffset2_ = 0.0f; scrollOffset2_ = 0.0f; }
    }
    if (paneIndex == 0) homeFocus_[0] = {};
    else homeFocus_[1] = {};
    hoverItem_ = -1;
    hoverColumn_ = -1;
    hoverActive_ = false;
    // semantic-uia-core-surfaces 2.6: the pane's children changed surface.
    NotifyUia({UiaEventKind::StructureBulkRemoved, paneIndex, -1, -1});
    NotifyUia({UiaEventKind::StructureBulkAdded, paneIndex, -1, -1});
}

// quick-access-home hardening (HOME-POST-03): drop each pane's focus when its
// target no longer exists in the fresh data (cheap; called on every data push).
void ColumnView::RevalidateHomeFocus() {
    bool focusDropped = false;
    for (int pane = 0; pane < 2; ++pane) {
        HomeFocusEntry& focus = homeFocus_[static_cast<size_t>(pane)];
        if (focus.target == HomeFocusTarget::None) continue;
        const HomeLayoutResult layout = ComputeHomeLayoutForPane(pane);
        const std::vector<HomeFocusEntry> focusList = BuildHomeFocusList(layout, homeData_);
        const bool stillValid = std::find(focusList.begin(), focusList.end(), focus) != focusList.end();
        if (!stillValid) {
            focus = {};
            focusDropped = true;
        }
    }
    // semantic-uia-core-surfaces 3.4: a dropped focus target is a focus change.
    if (focusDropped) {
        NotifyUia({UiaEventKind::FocusChanged});
    }
}

void ColumnView::ActivateHomeFocus(const HomeFocusEntry& focus, bool openInNewTab) {
    if (homeActivateCallback_ == nullptr) return;
    if (focus.target == HomeFocusTarget::Item) {
        ActivateHomeItem(activePane_, focus.itemIndex, openInNewTab);
    } else if (focus.target == HomeFocusTarget::ClearRecentFolders ||
               focus.target == HomeFocusTarget::ClearRecentSearches) {
        // quick-access-home hardening (HOME-POST-03): Enter on a focused Clear
        // stop invokes the existing clear operation through the callback.
        HomeActivation activation;
        activation.kind = HomeActionKindForFocusTarget(focus.target);
        homeActivateCallback_(activation);
    }
}

void ColumnView::HomeActivateFocused() {
    const int pane = activePane_;
    if (!homePane_[pane]) return;
    ActivateHomeFocus(pane == 0 ? homeFocus_[0] : homeFocus_[1], false);
}

// quick-access-home 4.2: rebuild the Home layout for a pane from the current
// `homeData_` and the last known viewport. Pure + cheap; the empty-pinned
// placeholder keeps the Pinned section present (header + hint) so the laid-out
// sections flow below it without colliding and hit-testing stays consistent.
HomeLayoutResult ColumnView::ComputeHomeLayoutForPane(int paneIndex) const {
    // Both panes share identical half-width geometry in dual-pane mode and the
    // full width in single-pane mode, so the layout depends only on the last
    // known viewport, not on which pane index is requested.
    (void)paneIndex;
    HomeLayoutInput input{};
    const float paneWidth = dualPane_ ? (lastViewportWidth_ / 2.0f) : lastViewportWidth_;
    input.viewportWidth = (std::max)(0.0f, paneWidth);
    input.viewportHeight = lastViewportHeight_;
    input.pinnedCount = homeData_.pinned.empty() ? 1 : static_cast<int>(homeData_.pinned.size());
    input.recentFolderCount = static_cast<int>(homeData_.recentFolders.size());
    input.recentSearchCount = static_cast<int>(homeData_.recentSearches.size());
    input.knownFolderCount = static_cast<int>(homeData_.knownFolders.size());
    input.showActions = homeData_.showActions;
    return ComputeHomeLayout(input);
}

// quick-access-home 4.2/4.4: route a Home item activation through the callback.
// The section + position map back to the owning data vector. quick-access-home
// hardening (HOME-POST-02): the pure `ComputeHomeActivation` seam rejects the
// empty-Pinned hint placeholder and any empty-path Folder destination (an
// unavailable known folder must never navigate to "This PC").
void ColumnView::ActivateHomeItem(int paneIndex, int globalIndex, bool openInNewTab) {
    if (homeActivateCallback_ == nullptr) return;
    const auto activation = ComputeHomeActivation(homeData_, ComputeHomeLayoutForPane(paneIndex),
                                                  globalIndex, openInNewTab);
    if (!activation.has_value()) return;
    homeActivateCallback_(*activation);
}

// quick-access-home 4.5: keyboard model for the Home pane. Up/Down move across
// the deterministic focus list (grid-aware inside a section; a section-boundary
// move lands on the section's Clear stop), Left/Right between the two columns
// when the layout is two-wide, Enter activates the focused target (Ctrl+Enter =
// new tab for Folder/Search), Home/End jump to first/last focus target. Escape
// and all other keys are intentionally ignored here so the shell keeps owning
// them. quick-access-home hardening (HOME-POST-03): the Clear affordances are
// real focus targets; the empty-Pinned hint and unavailable known folders are
// not.
void ColumnView::HandleHomeKeyDown(int virtualKey) {
    const int pane = activePane_;
    HomeFocusEntry& focus = pane == 0 ? homeFocus_[0] : homeFocus_[1];
    const HomeLayoutResult layout = ComputeHomeLayoutForPane(pane);
    const std::vector<HomeFocusEntry> focusList = BuildHomeFocusList(layout, homeData_);
    switch (virtualKey) {
        case VK_UP:
            focus = HomeMoveFocusEntry(layout, focusList, focus, 0, -1);
            break;
        case VK_DOWN:
            focus = HomeMoveFocusEntry(layout, focusList, focus, 0, 1);
            break;
        case VK_LEFT:
            focus = HomeMoveFocusEntry(layout, focusList, focus, -1, 0);
            break;
        case VK_RIGHT:
            focus = HomeMoveFocusEntry(layout, focusList, focus, 1, 0);
            break;
        case VK_HOME:
            // quick-access-home hardening (HOME-POST-03): Home/End land on
            // items, never on a Clear stop.
            focus = HomeFirstFocusEntry(focusList);
            break;
        case VK_END:
            focus = HomeLastFocusEntry(focusList);
            break;
        case VK_RETURN: {
            const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            ActivateHomeFocus(focus, control);
            break;
        }
        default:
            break;
    }
}

std::wstring ColumnView::RootPath() const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    for (const auto& column : columns_) {
        if (!column.path.empty()) return column.path;
    }
    for (const auto& column : columns2_) {
        if (!column.path.empty()) return column.path;
    }
    return {};
}

void ColumnView::RefreshColumnFromSnapshot(Column& column,
                                           const std::map<std::wstring, ffprotocol::SnapshotDirectory>& snapshot,
                                           uint64_t generation,
                                           int pane, int columnIndex,
                                           std::vector<UiaEventNotification>& outEvents) {
    const auto* directory = FindSnapshotDirectory(snapshot, column.path);
    if (directory == nullptr) {
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                      ffprotocol::DiagnosticCode::FFDIAG_UI_SNAPSHOT_PATH_MISS,
                                      column.path, {}, L"ui-snapshot-path-miss", ERROR_FILE_NOT_FOUND, 0});
        if (column.freshness.HasLastKnownGood()) {
            ApplyColumnFailure(column, SnapshotFailureReason::PathMissing, generation);
        }
        return; // an initial request may not have reached the published view yet
    }
    ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                  ffprotocol::DiagnosticCode::FFDIAG_UI_SNAPSHOT_COLUMN_REFRESH,
                                  column.path, {}, L"ui-snapshot-column-refresh",
                                  0, static_cast<uint64_t>(directory->entries.size())});

    if (directory->status == ffprotocol::DirectoryEnumerationStatus::AccessDenied) {
        ApplyColumnFailure(column, SnapshotFailureReason::AccessDenied, generation);
        return;
    }
    if (directory->status == ffprotocol::DirectoryEnumerationStatus::NotFound) {
        ApplyColumnFailure(column, SnapshotFailureReason::NoLongerAvailable, generation);
        return;
    }
    if (directory->status == ffprotocol::DirectoryEnumerationStatus::IoError) {
        ApplyColumnFailure(column, SnapshotFailureReason::EnumerationFailed, generation);
        return;
    }

    if (!column.freshness.AcceptGeneration(generation)) return;
    std::vector<SelectionIdentity> previousIdentities;
    previousIdentities.reserve(column.items.size());
    for (const auto& item : column.items) {
        previousIdentities.push_back(MakeSelectionIdentity(column.path, item.name, item.volumeRowId,
                                                           item.fileIdLow, item.fileIdHigh));
    }
    const SelectionIndexState previousState{column.selectedIndices, column.selectionAnchor, column.focusIndex};
    const SelectionIdentityState previousSelection =
        CaptureSelectionIdentityState(previousIdentities, previousState);
    const std::wstring pendingName = pendingSelectionName_;
    const std::wstring pendingPath = pendingSelectionPath_;
    bool pendingApplied = false;
    column.items.clear();
    column.items.reserve(directory->entries.size());
    for (const auto& entry : directory->entries) {
        column.items.push_back({entry.name, entry.isDirectory, entry.sizeBytes,
                                entry.allocatedSizeBytes, entry.allocatedSizeKnown,
                                entry.attributes,
                                static_cast<uint64_t>(entry.fileIdLow), static_cast<uint64_t>(entry.fileIdHigh),
                                entry.volumeRowId});
    }
    std::sort(column.items.begin(), column.items.end(), [](const ColumnItem& a, const ColumnItem& b) {
        if (a.isDirectory != b.isDirectory) {
            return a.isDirectory > b.isDirectory; // folders first
        }
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    std::vector<SelectionIdentity> refreshedIdentities;
    refreshedIdentities.reserve(column.items.size());
    for (const auto& item : column.items) {
        refreshedIdentities.push_back(MakeSelectionIdentity(column.path, item.name, item.volumeRowId,
                                                            item.fileIdLow, item.fileIdHigh));
    }
    const SelectionIndexState restoredState = RestoreSelectionIndexState(refreshedIdentities, previousSelection);
    column.selectedIndices = restoredState.selectedIndices;
    column.selectionAnchor = restoredState.selectionAnchor;
    column.focusIndex = restoredState.focusIndex;
    if (!pendingName.empty() && MatchesPendingSnapshotPath(column.path, pendingPath)) {
        for (int index = 0; index < static_cast<int>(column.items.size()); ++index) {
            if (_wcsicmp(column.items[index].name.c_str(), pendingName.c_str()) == 0) {
                column.selectedIndices.insert(index);
                column.selectionAnchor = index;
                column.focusIndex = index;
                pendingApplied = true;
                break;
            }
        }
    }
    if (pendingApplied) {
        pendingSelectionName_.clear();
        pendingSelectionPath_.clear();
    }

    // selection-input-hardening: hover is a raw index; when the refresh
    // changed the item set, the old index now names a different row. Drop the
    // hover target for this column so the pill cannot stick to the wrong item
    // until the next mouse move.
    if (hoverColumn_ == columnIndex) {
        bool itemsChanged = previousIdentities.size() != refreshedIdentities.size();
        for (size_t i = 0; !itemsChanged && i < previousIdentities.size(); ++i) {
            itemsChanged = !SameSelectionIdentity(previousIdentities[i], refreshedIdentities[i]);
        }
        if (itemsChanged) {
            hoverColumn_ = -1;
            hoverItem_ = -1;
        }
    }

    // semantic-uia-core-surfaces 2.6: the column's children were replaced and
    // selection/focus were identity-restored; collect the notifications for
    // the caller to fire after its lock is released.
    outEvents.push_back({UiaEventKind::StructureBulkRemoved, pane, columnIndex, -1});
    outEvents.push_back({UiaEventKind::StructureBulkAdded, pane, columnIndex, -1});
    if (previousState.selectedIndices != column.selectedIndices) {
        outEvents.push_back({UiaEventKind::SelectionInvalidated, pane, columnIndex, -1});
        for (int row : column.selectedIndices) {
            if (!previousState.selectedIndices.contains(row)) {
                outEvents.push_back({UiaEventKind::ElementAddedToSelection, pane, columnIndex, row});
            }
        }
        for (int row : previousState.selectedIndices) {
            if (!column.selectedIndices.contains(row)) {
                outEvents.push_back({UiaEventKind::ElementRemovedFromSelection, pane, columnIndex, row});
            }
        }
    }
    if (previousState.focusIndex != column.focusIndex) {
        outEvents.push_back({UiaEventKind::FocusChanged, pane, columnIndex, -1});
    }
}

void ColumnView::OnSnapshotUpdated(uint64_t announcedGeneration) {
    if (!engineClient_) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        for (auto& column : columns_) {
            if (!column.path.empty()) column.freshness.AnnounceGeneration(announcedGeneration);
        }
        for (auto& column : columns2_) {
            if (!column.path.empty()) column.freshness.AnnounceGeneration(announcedGeneration);
        }
    }
    auto snapshot = engineClient_->ReadSnapshotWithGeneration();
    if (!snapshot || (announcedGeneration != 0 && snapshot->generation < announcedGeneration)) {
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                      ffprotocol::DiagnosticCode::FFDIAG_UI_SNAPSHOT_READ_FAILED,
                                      {}, {}, L"ui-snapshot-read-failed", ERROR_INVALID_DATA,
                                      announcedGeneration});
        MarkSnapshotLoadFailed(announcedGeneration);
        return;
    }
    // semantic-uia-core-surfaces 2.6: refresh notifications are collected
    // under the lock and fired after it is released (no reentrancy).
    std::vector<UiaEventNotification> uiaEvents;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        for (int columnIndex = 0; columnIndex < static_cast<int>(columns_.size()); ++columnIndex) {
            auto& column = columns_[static_cast<size_t>(columnIndex)];
            if (column.path.empty()) {
                continue; // the synthetic root (drive list) column isn't engine-backed
            }
            RefreshColumnFromSnapshot(column, snapshot->directories, snapshot->generation,
                                      0, columnIndex, uiaEvents);
        }
        for (int columnIndex = 0; columnIndex < static_cast<int>(columns2_.size()); ++columnIndex) {
            auto& column = columns2_[static_cast<size_t>(columnIndex)];
            if (column.path.empty()) {
                continue;
            }
            RefreshColumnFromSnapshot(column, snapshot->directories, snapshot->generation,
                                      1, columnIndex, uiaEvents);
        }
    }
    for (const auto& notification : uiaEvents) {
        NotifyUia(notification);
    }
}

void ColumnView::OnDirectoryError(const std::wstring& path, ffprotocol::DirectoryErrorReason reason) {
    const SnapshotFailureReason failure =
        reason == ffprotocol::DirectoryErrorReason::AccessDenied
            ? SnapshotFailureReason::AccessDenied
            : (reason == ffprotocol::DirectoryErrorReason::PublishFailed
                   ? SnapshotFailureReason::PublishFailed
                   : (reason == ffprotocol::DirectoryErrorReason::EnumerationFailed
                          ? SnapshotFailureReason::EnumerationFailed
                          : SnapshotFailureReason::NoLongerAvailable));
    std::lock_guard<std::mutex> lock(columnsMutex_);
    for (auto& column : columns_) {
        if (ffprotocol::WindowsPathEqual(column.path, path)) {
            ApplyColumnFailure(column, failure);
        }
    }
    for (auto& column : columns2_) {
        if (ffprotocol::WindowsPathEqual(column.path, path)) {
            ApplyColumnFailure(column, failure);
        }
    }
}

void ColumnView::ShowUnavailableLocation(const std::wstring& displayName) {
    const int pane = activePane_;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        Column column;
        column.path = displayName.empty() ? L"Unavailable location" : displayName;
        column.freshness.Fail(SnapshotFailureReason::NoLongerAvailable);
        columns_ = {std::move(column)};
        focusedColumnIndex_ = 0;
        pendingSelectionName_.clear();
        pendingSelectionPath_.clear();
    }
    // semantic-uia-core-surfaces 2.6: structure events fire post-lock.
    NotifyUia({UiaEventKind::StructureBulkRemoved, pane, -1, -1});
    NotifyUia({UiaEventKind::StructureBulkAdded, pane, -1, -1});
}

void ColumnView::MarkSnapshotLoadFailed(uint64_t generation) {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    for (auto& column : columns_) {
        if (!column.path.empty()) ApplyColumnFailure(column, SnapshotFailureReason::SnapshotLoadFailed, generation);
    }
    for (auto& column : columns2_) {
        if (!column.path.empty()) ApplyColumnFailure(column, SnapshotFailureReason::SnapshotLoadFailed, generation);
    }
}

void ColumnView::MarkPathLoading(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    for (auto& column : columns_) {
        if (ffprotocol::WindowsPathEqual(column.path, path)) column.freshness.BeginRequest();
    }
    for (auto& column : columns2_) {
        if (ffprotocol::WindowsPathEqual(column.path, path)) column.freshness.BeginRequest();
    }
}

void ColumnView::OnEngineConnectionState(EngineConnectionState state) {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    const auto apply = [state](std::vector<Column>& columns) {
        for (auto& column : columns) {
            if (column.path.empty()) continue;
            if (state == EngineConnectionState::Connected) {
                column.freshness.ResetForNewSource();
            } else if (state == EngineConnectionState::Unavailable) {
                ApplyColumnFailure(column, SnapshotFailureReason::Disconnected);
            } else if (column.freshness.HasLastKnownGood()) {
                ApplyColumnFailure(column, SnapshotFailureReason::Disconnected);
            }
        }
    };
    apply(columns_);
    apply(columns2_);
}

void ColumnView::SetEngineStatus(EngineConnectionState state, bool privilegedPathActive) {
    engineState_ = state;
    privilegedPathActive_ = privilegedPathActive;
}

void ColumnView::SetRetryEngineCallback(std::function<void()> retryEngine) {
    retryEngineCallback_ = std::move(retryEngine);
}

void ColumnView::SetDarkTheme(bool dark) {
    if (darkTheme_ != dark) {
        darkTheme_ = dark;
        resourcesCreated_ = false; // recreated together with the next paint
        backgroundBrush_.Reset(); borderBrush_.Reset(); textBrush_.Reset(); textSecondaryBrush_.Reset(); textOnAccentBrush_.Reset();
        selectionBrush_.Reset();
        folderGlyphBrush_.Reset(); fileGlyphBrush_.Reset(); errorBrush_.Reset();
        badgeActiveBrush_.Reset(); badgeActiveTextBrush_.Reset();
        badgeDegradedBrush_.Reset(); badgeDegradedTextBrush_.Reset();
        selectionSoftBrush_.Reset(); dividerBrush_.Reset(); hoverOverlayBrush_.Reset();
        focusStrokeBrush_.Reset(); // quick-access-home 4.3: reset the new brush so it is recreated for the theme
    }
}

float ColumnView::ContentWidth() const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    // quick-access-home 4.x: a Home pane's content fills the pane, not columns.
    if (homePane_[activePane_]) {
        return lastViewportWidth_ > 0.0f
            ? (dualPane_ ? lastViewportWidth_ / 2.0f : lastViewportWidth_)
            : 0.0f;
    }
    const auto& columns = const_cast<ColumnView*>(this)->ActiveColumns();
    return kColumnWidth * static_cast<float>(columns.size());
}

float ColumnView::PaneContentWidth(int paneIndex) const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    if (paneIndex < 0 || paneIndex > 1) return 0.0f;
    // quick-access-home 4.x: a Home pane's content fills the pane, not columns.
    if (homePane_[paneIndex]) {
        return lastViewportWidth_ > 0.0f
            ? (dualPane_ ? lastViewportWidth_ / 2.0f : lastViewportWidth_)
            : 0.0f;
    }
    const auto& columns = paneIndex == 0 ? columns_ : columns2_;
    return kColumnWidth * static_cast<float>(columns.size());
}

int ColumnView::FocusedColumnIndex() const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    const int& focusedIndex = const_cast<ColumnView*>(this)->ActiveFocusedColumnIndex();
    return focusedIndex;
}

std::optional<FileDescriptor> ColumnView::CurrentSelection() const {
    SelectionSummary summary = CurrentSelectionSummary();
    if (summary.items.size() != 1) {
        return std::nullopt;
    }
    return summary.items.front();
}

SelectionSummary ColumnView::CurrentSelectionSummary() const {
    SelectionSummary summary;
    std::lock_guard<std::mutex> lock(columnsMutex_);
    const auto& columns = const_cast<ColumnView*>(this)->ActiveColumns();
    const int& focusedIndex = const_cast<ColumnView*>(this)->ActiveFocusedColumnIndex();
    if (focusedIndex < 0 || focusedIndex >= static_cast<int>(columns.size())) {
        return summary;
    }
    const Column& column = columns[focusedIndex];
    for (int index : column.selectedIndices) {
        if (index < 0 || index >= static_cast<int>(column.items.size())) {
            continue;
        }
        const ColumnItem& item = column.items[index];
        summary.items.push_back({JoinPath(column.path, item.name), item.sizeBytes,
                                  item.allocatedSizeBytes, item.allocatedSizeKnown,
                                  0, // maxBytes: provider default (Column View carries no preview limit)
                                  item.attributes, item.isDirectory,
                                  item.fileIdLow, item.fileIdHigh, item.volumeRowId});
        if (!item.isDirectory) summary.knownSizeBytes += item.sizeBytes;
    }
    return summary;
}

#ifdef _DEBUG
SelectionDiagnostics ColumnView::CurrentSelectionDiagnostics() const {
    SelectionDiagnostics diagnostics;
    const std::vector<std::wstring> operationTargets = ActiveSelectionPaths();
    if (!operationTargets.empty()) diagnostics.operationTarget = operationTargets.front();
    std::lock_guard<std::mutex> lock(columnsMutex_);
    const auto& columns = const_cast<ColumnView*>(this)->ActiveColumns();
    const int& focusedIndex = const_cast<ColumnView*>(this)->ActiveFocusedColumnIndex();
    if (focusedIndex < 0 || focusedIndex >= static_cast<int>(columns.size())) {
        return diagnostics;
    }

    const Column& column = columns[focusedIndex];
    diagnostics.activePath = column.path;
    diagnostics.visibleItemNames.reserve(column.items.size());
    for (const auto& item : column.items) diagnostics.visibleItemNames.push_back(item.name);
    const auto makeIdentity = [&column](int itemIndex) {
        const ColumnItem& item = column.items[static_cast<size_t>(itemIndex)];
        return MakeSelectionIdentity(column.path, item.name, item.volumeRowId,
                                     item.fileIdLow, item.fileIdHigh);
    };
    for (const int index : column.selectedIndices) {
        if (index >= 0 && index < static_cast<int>(column.items.size())) {
            diagnostics.selectedIndex = index;
            diagnostics.selectedIdentity = makeIdentity(index);
            break;
        }
    }
    if (column.focusIndex >= 0 && column.focusIndex < static_cast<int>(column.items.size())) {
        diagnostics.focusedIndex = column.focusIndex;
        diagnostics.focusedIdentity = makeIdentity(column.focusIndex);
    }
    return diagnostics;
}
#endif

std::wstring ColumnView::CurrentPath() const {
    const std::wstring path = ActivePanePath();
    return path.empty() ? L"Computer" : path;
}

void ColumnView::EnsureCreated(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory) {
    if (resourcesCreated_) {
        return;
    }
    const ffui::UiTheme theme = ffui::GetUiTheme(darkTheme_);
    context->CreateSolidColorBrush(theme.background, &backgroundBrush_);
    context->CreateSolidColorBrush(theme.border, &borderBrush_);
    context->CreateSolidColorBrush(theme.text, &textBrush_);
    context->CreateSolidColorBrush(theme.textSecondary, &textSecondaryBrush_);
    context->CreateSolidColorBrush(theme.textOnAccent, &textOnAccentBrush_);
    context->CreateSolidColorBrush(theme.accent, &selectionBrush_);
    context->CreateSolidColorBrush(theme.folderGlyph, &folderGlyphBrush_);
    context->CreateSolidColorBrush(theme.fileGlyph, &fileGlyphBrush_);
    context->CreateSolidColorBrush(theme.error, &errorBrush_);
    context->CreateSolidColorBrush(theme.badgeActiveBg, &badgeActiveBrush_);
    context->CreateSolidColorBrush(theme.badgeActiveText, &badgeActiveTextBrush_);
    context->CreateSolidColorBrush(theme.badgeDegradedBg, &badgeDegradedBrush_);
    context->CreateSolidColorBrush(theme.badgeDegradedText, &badgeDegradedTextBrush_);
    context->CreateSolidColorBrush(theme.selectionSoft, &selectionSoftBrush_);
    context->CreateSolidColorBrush(theme.dividerSubtle, &dividerBrush_);
    context->CreateSolidColorBrush(theme.hoverOverlay, &hoverOverlayBrush_);
    // quick-access-home 4.3: first consumer of the previously-unused focusStroke token.
    context->CreateSolidColorBrush(theme.focusStroke, &focusStrokeBrush_);

    dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        14.0f, L"en-us", &textFormat_);
    textFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        12.0f, L"en-us", &badgeTextFormat_);

    resourcesCreated_ = true;
}

void ColumnView::Render(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory, D2D1_SIZE_F viewportSize, float scrollOffset, float scrollOffset2) {
    EnsureCreated(context, dwriteFactory);

    // quick-access-home 4.x: remember the viewport so input handlers (which
    // receive no geometry) can rebuild the Home layout for hit-testing / focus.
    lastViewportWidth_ = viewportSize.width;
    lastViewportHeight_ = viewportSize.height;

    const ffui::UiTheme theme = ffui::GetUiTheme(darkTheme_);
    context->Clear(theme.background);

    // recovery-degraded-browsing D3: engine-connection-status text as a
    // rounded chip. Paint-only except for the Unavailable state, where a
    // click retries the engine connection (see OnMouseDown).
    D2D1_RECT_F badgeRect = D2D1::RectF(8.0f, 4.0f, viewportSize.width - 8.0f, kBadgeHeight - 4.0f);
    const EngineConnectionState connectionState = engineState_.load();
    const bool privilegedActive = privilegedPathActive_.load();
    const EngineStatusPresentation statusPresentation =
        GetEngineStatusPresentation(connectionState, privilegedActive);
    ID2D1SolidColorBrush* badgeBrush = badgeDegradedBrush_.Get();
    ID2D1SolidColorBrush* badgeTextBrush = badgeDegradedTextBrush_.Get();
    if (statusPresentation.privilegedPathActive) {
        badgeBrush = badgeActiveBrush_.Get();
        badgeTextBrush = badgeActiveTextBrush_.Get();
    } else if (connectionState == EngineConnectionState::Connected) {
        badgeBrush = badgeDegradedBrush_.Get();
        badgeTextBrush = badgeDegradedTextBrush_.Get();
    }
    ffui::UiFillRoundedRect(context, badgeRect, badgeBrush, ffui::UiMetrics::kRadiusSmall);
    D2D1_RECT_F badgeTextRect = D2D1::RectF(16.0f, 4.0f, viewportSize.width - 16.0f, kBadgeHeight - 4.0f);
    context->DrawText(statusPresentation.text, static_cast<UINT32>(wcslen(statusPresentation.text)),
                      badgeTextFormat_.Get(), badgeTextRect,
                      badgeTextBrush);

    // High Contrast: drive every selection through the system highlight
    // (UiEnsureSolidBrush compares colors and reuses the cached brush) and
    // suppress the token hover overlay in the row loop below.
    const bool highContrast = ffui::UiSystemHighContrast();
    if (highContrast) {
        ffui::UiEnsureSolidBrush(context, ffui::ToD2DColor(GetSysColor(COLOR_HIGHLIGHT)), &selectionBrush_);
    }

    const uint64_t nowMs = static_cast<uint64_t>(GetTickCount64());
    // Task 3.4: there is a single hover target, so the fade advances once per
    // frame; the shell only repaints while HoverAnimating() holds.
    hoverOpacity_.Tick(nowMs);
    // quick-access-home 4.3: hoisted to Render scope so both the column and
    // Home render lambdas can read the lingering fade-out window.
    const bool lingerFade = hoverLeaveMs_ != 0 &&
        (nowMs - hoverLeaveMs_) < static_cast<uint64_t>(kUiAnimationDefaultMs);

    std::lock_guard<std::mutex> lock(columnsMutex_);

    auto RenderPane = [&](const std::vector<Column>& columns, float paneX, float paneWidth, float paneScroll,
                          float verticalScrollOffset, bool isActivePane) {
        for (int i = 0; i < static_cast<int>(columns.size()); ++i) {
            const float x = paneX + i * kColumnWidth - paneScroll;
            if (x + kColumnWidth < paneX || x > paneX + paneWidth) {
                continue; // culled
            }
            const Column& column = columns[i];

            D2D1_RECT_F columnRect = D2D1::RectF(x, kBadgeHeight, x + kColumnWidth, viewportSize.height);
            context->FillRectangle(columnRect, backgroundBrush_.Get());
            // Task 3.6: subtle 1-DIP hairline between columns.
            context->DrawLine(D2D1_POINT_2F{x + kColumnWidth, kBadgeHeight}, D2D1_POINT_2F{x + kColumnWidth, viewportSize.height},
                              dividerBrush_.Get(), 1.0f);

            const std::wstring unavailableMessage = UnavailableMessage(column);
            const std::wstring statusMessage = StatusMessage(column);
            if (column.freshness.state == SnapshotViewState::Unavailable) {
                D2D1_RECT_F messageRect = D2D1::RectF(x + 8, kBadgeHeight + 8, x + kColumnWidth - 8, viewportSize.height - 8);
                context->DrawText(unavailableMessage.c_str(), static_cast<UINT32>(unavailableMessage.size()),
                                  textFormat_.Get(), messageRect, errorBrush_.Get());
                continue;
            }

            float contentTop = kBadgeHeight;
            if (column.freshness.state == SnapshotViewState::Loading ||
                column.freshness.state == SnapshotViewState::StaleWithError) {
                D2D1_RECT_F statusRect = D2D1::RectF(x + 8, kBadgeHeight + 4, x + kColumnWidth - 8,
                                                     kBadgeHeight + 24);
                context->DrawText(statusMessage.c_str(), static_cast<UINT32>(statusMessage.size()),
                                  textFormat_.Get(), statusRect,
                                  column.freshness.state == SnapshotViewState::StaleWithError
                                      ? errorBrush_.Get() : textSecondaryBrush_.Get());
                // selection-input-hardening: the inset comes from the same
                // ColumnContentTopInset seam the hit-test/scroll paths use.
                contentTop += ColumnContentTopInset(column.freshness.state);
            }

            if (column.items.empty()) {
                if (column.freshness.state != SnapshotViewState::Current) continue;
                const wchar_t* emptyMessage = L"This folder is empty";
                D2D1_RECT_F messageRect = D2D1::RectF(x + 8, contentTop + 8, x + kColumnWidth - 8, viewportSize.height - 8);
                context->DrawText(emptyMessage, static_cast<UINT32>(wcslen(emptyMessage)), textFormat_.Get(), messageRect, textSecondaryBrush_.Get());
                continue;
            }

            for (int r = 0; r < static_cast<int>(column.items.size()); ++r) {
                const float y = contentTop + r * kRowHeight - verticalScrollOffset;
                if (y + kRowHeight < contentTop || y > viewportSize.height) {
                    continue;
                }

                const ColumnItem& item = column.items[r];
                const bool isSelected = column.selectedIndices.contains(r);
                const bool isFocusedColumn = (i == (activePane_ == 0 ? focusedColumnIndex_ : focusedColumnIndex2_));
                // Task 3.4: hover tracks the active pane only; the lingering
                // fade-out window (150 ms after OnMouseLeave) keeps the pill
                // visible while it dims. Overlay is skipped in High Contrast.
                // (lingerFade is hoisted to Render scope and shared with the
                // Home render lambda.)
                const bool hoverRow = isActivePane && hoverColumn_ == i && hoverItem_ == r && !isSelected && (hoverActive_ || lingerFade);
                if (hoverRow && !highContrast) {
                    const float alpha = hoverOpacity_.Value();
                    if (alpha > 0.001f) {
                        D2D1_RECT_F hoverRect = D2D1::RectF(x + 4, y + 2, x + kColumnWidth - 4, y + kRowHeight - 2);
                        D2D1_COLOR_F transparentOverlay = theme.hoverOverlay;
                        transparentOverlay.a = 0.0f;
                        const D2D1_COLOR_F overlayColor = ffui::UiLerpColor(transparentOverlay, theme.hoverOverlay, alpha);
                        ffui::UiFillHoverOverlay(context, hoverRect, ffui::UiMetrics::kRadiusSmall, overlayColor, &hoverOverlayBrush_);
                    }
                }

                // Task 3.3: rounded selection pill; the focused column uses the
                // full accent (text flips to textOnAccent), elsewhere the soft
                // translucent fill keeps body text legible.
                if (isSelected) {
                    D2D1_RECT_F selectionRect = D2D1::RectF(x + 4, y + 2, x + kColumnWidth - 4, y + kRowHeight - 2);
                    ID2D1SolidColorBrush* selectionBrush = (highContrast || isFocusedColumn) ? selectionBrush_.Get() : selectionSoftBrush_.Get();
                    ffui::UiFillRoundedRect(context, selectionRect, selectionBrush, ffui::UiMetrics::kRadiusSmall);
                    // selection-input-hardening: the keyboard focus row gets a
                    // distinct focusStroke ring so it is distinguishable from
                    // the other selected rows in a multi-selection. High
                    // Contrast already signals focus via the system highlight
                    // fill, matching the Home surface's fallback.
                    if (isFocusedColumn && r == column.focusIndex && !highContrast && focusStrokeBrush_ != nullptr) {
                        D2D1_ROUNDED_RECT fr{D2D1::RectF(x + 2.5f, y + 0.5f, x + kColumnWidth - 2.5f, y + kRowHeight - 0.5f),
                                             ffui::UiMetrics::kRadiusSmall, ffui::UiMetrics::kRadiusSmall};
                        context->DrawRoundedRectangle(&fr, focusStrokeBrush_.Get(), 1.0f);
                    }
                }

                // Task 3.2: type icon from the shared cache; the themed glyph
                // rectangle stays as the placeholder until the bitmap arrives.
                std::wstring iconKey = item.isDirectory ? std::wstring(ffui::FolderKey())
                                                        : ffui::IconKeyForExtension(FileExtensionOf(item.name));
                if (iconCache_ != nullptr) {
                    iconCache_->Prefetch(iconKey); // cheap: dedupes against cache/pending/queued
                }
                ID2D1Bitmap1* icon = nullptr;
                if (iconCache_ != nullptr && iconCache_->Get(context, iconKey, &icon) && icon != nullptr) {
                    const float iconY = y + (kRowHeight - ffui::UiMetrics::kIconSize) / 2.0f;
                    context->DrawBitmap(icon,
                                        D2D1::RectF(x + 8, iconY, x + 8 + ffui::UiMetrics::kIconSize, iconY + ffui::UiMetrics::kIconSize),
                                        1.0f, D2D1_INTERPOLATION_MODE_LINEAR);
                } else {
                    D2D1_RECT_F glyphRect = D2D1::RectF(x + 8, y + kRowHeight / 2 - 4, x + 16, y + kRowHeight / 2 + 4);
                    context->FillRectangle(glyphRect, item.isDirectory ? folderGlyphBrush_.Get() : fileGlyphBrush_.Get());
                }

                std::wstring label = item.name;
                if (item.isDirectory && (label.empty() || label.back() != L'\\')) {
                    label += L'\\';
                }
                D2D1_RECT_F textRect = D2D1::RectF(x + 24, y, x + kColumnWidth - 4, y + kRowHeight);
                // White text on the accent selection for contrast in both
                // themes; soft (unfocused) selections keep the normal text
                // brush so body text stays legible on the translucent fill.
                context->DrawText(label.c_str(), static_cast<UINT32>(label.size()), textFormat_.Get(), textRect,
                                  isSelected && isFocusedColumn ? textOnAccentBrush_.Get() : textBrush_.Get());
                // Task 3.3: folder chevron affordance at the right edge.
                if (item.isDirectory) {
                    D2D1_RECT_F chevronRect = D2D1::RectF(x + kColumnWidth - 16, y, x + kColumnWidth - 4, y + kRowHeight);
                    context->DrawText(L"\u203A", 1, textFormat_.Get(), chevronRect, textSecondaryBrush_.Get());
                }
            }
        }
    };

    // quick-access-home 4.3: render the Home surface for one pane. Drawn in the
    // pane's rect (pane-local origin already removed via `paneX`), starting
    // below the status badge like the column surface. The vertical scroll offset
    // is applied so long lists scroll; a clip rect keeps content out of the
    // badge band and off the dual-pane divider. High Contrast suppresses the
    // token hover overlay and the focus ring and uses the system highlight.
    auto RenderHomePane = [&](int paneIndex, float paneX, float paneWidth, float viewportHeight) {
        if (!homePane_[paneIndex]) return;
        const float vso = paneIndex == 0 ? verticalScrollOffset_ : verticalScrollOffset2_;
        const bool isActivePane = (activePane_ == paneIndex);
        // quick-access-home hardening (HOME-POST-03): the focus is a typed
        // target (item or a section-header Clear stop), never a bare index.
        const HomeFocusEntry focusEntry = paneIndex == 0 ? homeFocus_[0] : homeFocus_[1];

        HomeLayoutInput input{};
        input.viewportWidth = (std::max)(0.0f, paneWidth);
        input.viewportHeight = viewportHeight;
        // Empty `pinned` is surfaced as one placeholder item so the Pinned
        // section (header + "Pin folders to see them here." hint) still renders
        // before the real sections, keeping hit-testing consistent.
        input.pinnedCount = homeData_.pinned.empty() ? 1 : static_cast<int>(homeData_.pinned.size());
        input.recentFolderCount = static_cast<int>(homeData_.recentFolders.size());
        input.recentSearchCount = static_cast<int>(homeData_.recentSearches.size());
        input.knownFolderCount = static_cast<int>(homeData_.knownFolders.size());
        input.showActions = homeData_.showActions;
        const HomeLayoutResult layout = ComputeHomeLayout(input);

        static const wchar_t* kSectionTitles[] = {
            L"Pinned", L"Recent folders", L"Recent searches", L"Common locations", L"Quick actions"};

        // Clip to the pane minus the badge band.
        D2D1_RECT_F clip = D2D1::RectF(paneX, kBadgeHeight, paneX + paneWidth, viewportSize.height);
        context->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);

        // Section headers (drawn above each section's items). Recent folders /
        // Recent searches additionally expose a right-aligned "Clear"
        // affordance whose hit rect is recorded for OnMouseDown.
        homeClearButtons_[paneIndex] = {};
        for (const auto& section : layout.sections) {
            const float hx = paneX + section.headerX;
            const float hy = kBadgeHeight + section.headerY - vso;
            const wchar_t* title = (section.sectionIndex >= 0 && section.sectionIndex <= 4)
                ? kSectionTitles[section.sectionIndex] : L"";
            D2D1_RECT_F hdrRect = D2D1::RectF(hx, hy, hx + section.headerWidth, hy + section.headerHeight);
            context->DrawText(title, static_cast<UINT32>(wcslen(title)), badgeTextFormat_.Get(), hdrRect,
                              textSecondaryBrush_.Get());
            if (section.sectionIndex == static_cast<int>(HomeSectionKind::RecentFolders) ||
                section.sectionIndex == static_cast<int>(HomeSectionKind::RecentSearches)) {
                const float clearWidth = 44.0f;
                const D2D1_RECT_F clearRect = D2D1::RectF(hx + section.headerWidth - clearWidth, hy,
                                                          hx + section.headerWidth, hy + section.headerHeight);
                // quick-access-home hardening (HOME-POST-03): a focused Clear
                // stop gets the same treatment as a focused item -- the system
                // highlight fill in High Contrast, the focusStroke ring and
                // primary text otherwise.
                const bool isClearFocused = isActivePane && (
                    (focusEntry.target == HomeFocusTarget::ClearRecentFolders &&
                     section.sectionIndex == static_cast<int>(HomeSectionKind::RecentFolders)) ||
                    (focusEntry.target == HomeFocusTarget::ClearRecentSearches &&
                     section.sectionIndex == static_cast<int>(HomeSectionKind::RecentSearches)));
                if (isClearFocused) {
                    if (highContrast) {
                        context->FillRoundedRectangle(
                            D2D1::RoundedRect(D2D1::RectF(clearRect.left - 1.0f, clearRect.top + 1.0f,
                                                          clearRect.right + 1.0f, clearRect.bottom - 1.0f),
                                              kHomeRadiusMedium, kHomeRadiusMedium),
                            selectionBrush_.Get());
                    } else if (focusStrokeBrush_ != nullptr) {
                        D2D1_ROUNDED_RECT fr{D2D1::RectF(clearRect.left - 1.5f, clearRect.top + 1.5f,
                                                         clearRect.right + 1.5f, clearRect.bottom - 1.5f),
                                             kHomeRadiusMedium, kHomeRadiusMedium};
                        context->DrawRoundedRectangle(&fr, focusStrokeBrush_.Get(), 1.0f);
                    }
                }
                context->DrawText(L"Clear", 5, badgeTextFormat_.Get(), clearRect,
                                  isClearFocused ? textBrush_.Get() : textSecondaryBrush_.Get());
                if (section.sectionIndex == static_cast<int>(HomeSectionKind::RecentFolders)) {
                    homeClearButtons_[paneIndex].recentFolders = clearRect;
                } else {
                    homeClearButtons_[paneIndex].recentSearches = clearRect;
                }
            }
        }

        for (const auto& item : layout.items) {
            const float x = paneX + item.x;
            const float y = kBadgeHeight + item.y - vso;
            const float w = item.width;
            const float h = item.height;
            const bool isFocused = isActivePane && focusEntry.target == HomeFocusTarget::Item &&
                                   item.globalIndex == focusEntry.itemIndex;
            // quick-access-home hardening (HOME-POST-02): an unavailable known
            // folder (empty path) renders with the disabled/secondary text
            // treatment, matching the sidebar's disabled known-folder rows.
            const bool itemDisabled = !HomeItemFocusable(homeData_, item);

            std::wstring primary;
            std::wstring secondary;
            std::wstring glyph;
            bool isFolderIcon = false;
            switch (static_cast<HomeSectionKind>(item.sectionIndex)) {
                case HomeSectionKind::Pinned:
                    if (homeData_.pinned.empty()) {
                        primary = L"Pin folders to see them here."; // hint placeholder
                    } else {
                        const auto& b = homeData_.pinned[item.indexInSection];
                        primary = b.displayName.empty() ? b.path : b.displayName;
                        secondary = b.path;
                        isFolderIcon = true;
                    }
                    break;
                case HomeSectionKind::RecentFolders:
                    primary = homeData_.recentFolders[item.indexInSection];
                    isFolderIcon = true;
                    break;
                case HomeSectionKind::RecentSearches:
                    primary = homeData_.recentSearches[item.indexInSection];
                    glyph = L"\uE721"; // magnifier
                    break;
                case HomeSectionKind::CommonLocations: {
                    const auto& b = homeData_.knownFolders[item.indexInSection];
                    primary = b.displayName.empty() ? b.path : b.displayName;
                    secondary = b.path;
                    isFolderIcon = true;
                    break;
                }
                case HomeSectionKind::QuickActions: {
                    static const wchar_t* kLabels[] = {L"New Tab", L"Search", L"Open Path", L"Settings"};
                    static const wchar_t* kGlyphs[] = {L"\uE8A7", L"\uE721", L"\uE8B7", L"\uE713"};
                    if (item.indexInSection >= 0 && item.indexInSection < 4) {
                        primary = kLabels[item.indexInSection];
                        glyph = kGlyphs[item.indexInSection];
                    }
                    break;
                }
            }

            const bool hoverRow = isActivePane && !itemDisabled &&
                                  hoverItem_ == item.globalIndex && hoverColumn_ == -1 &&
                                  (hoverActive_ || lingerFade) && !highContrast;

            // Focused fill (selection pill equivalent). High Contrast reuses the
            // system highlight brush set up above; otherwise the soft fill.
            if (isFocused) {
                D2D1_RECT_F fillRect = D2D1::RectF(x + 2.0f, y + 2.0f, x + w - 2.0f, y + h - 2.0f);
                ID2D1SolidColorBrush* fillBrush = highContrast ? selectionBrush_.Get() : selectionSoftBrush_.Get();
                ffui::UiFillRoundedRect(context, fillRect, fillBrush, kHomeRadiusMedium);
            }

            // Hover overlay (token overlay, suppressed in High Contrast).
            if (hoverRow) {
                const float alpha = hoverOpacity_.Value();
                if (alpha > 0.001f) {
                    D2D1_RECT_F hoverRect = D2D1::RectF(x + 2.0f, y + 2.0f, x + w - 2.0f, y + h - 2.0f);
                    D2D1_COLOR_F transparentOverlay = theme.hoverOverlay;
                    transparentOverlay.a = 0.0f;
                    const D2D1_COLOR_F overlayColor = ffui::UiLerpColor(transparentOverlay, theme.hoverOverlay, alpha);
                    ffui::UiFillHoverOverlay(context, hoverRect, kHomeRadiusMedium, overlayColor, &hoverOverlayBrush_);
                }
            }

            // Focus ring: first consumer of the theme.focusStroke token; suppressed
            // in High Contrast (the filled pill already signals focus there).
            if (isFocused && !highContrast && focusStrokeBrush_ != nullptr) {
                D2D1_ROUNDED_RECT fr{D2D1::RectF(x + 1.5f, y + 1.5f, x + w - 1.5f, y + h - 1.5f),
                                     kHomeRadiusMedium, kHomeRadiusMedium};
                context->DrawRoundedRectangle(&fr, focusStrokeBrush_.Get(), 1.0f);
            }

            // Icon / glyph + text. Folder items use the shared IconCache
            // (Prefetch is cheap); everything else is a best-effort glyph char
            // drawn in the existing font (label remains the source of truth).
            float textX = x + 8.0f;
            if (isFolderIcon) {
                if (iconCache_ != nullptr) {
                    iconCache_->Prefetch(ffui::FolderKey());
                }
                ID2D1Bitmap1* icon = nullptr;
                if (iconCache_ != nullptr && iconCache_->Get(context, ffui::FolderKey(), &icon) && icon != nullptr) {
                    const float iconY = y + (h - ffui::UiMetrics::kIconSize) / 2.0f;
                    context->DrawBitmap(icon,
                        D2D1::RectF(x + 8.0f, iconY, x + 8.0f + ffui::UiMetrics::kIconSize, iconY + ffui::UiMetrics::kIconSize),
                        1.0f, D2D1_INTERPOLATION_MODE_LINEAR);
                } else {
                    D2D1_RECT_F glyphRect = D2D1::RectF(x + 8.0f, y + h / 2.0f - 4.0f, x + 16.0f, y + h / 2.0f + 4.0f);
                    context->FillRectangle(glyphRect, folderGlyphBrush_.Get());
                }
                textX = x + 32.0f;
            } else if (!glyph.empty()) {
                D2D1_RECT_F glyphRect = D2D1::RectF(x + 8.0f, y, x + 32.0f, y + h);
                context->DrawText(glyph.c_str(), 1, textFormat_.Get(), glyphRect, textSecondaryBrush_.Get());
                textX = x + 32.0f;
            }

            if (!primary.empty()) {
                const float halfH = y + h / 2.0f;
                D2D1_RECT_F primaryRect = D2D1::RectF(textX, y + 2.0f, x + w - 8.0f,
                                                      secondary.empty() ? y + h - 2.0f : halfH);
                context->DrawText(primary.c_str(), static_cast<UINT32>(primary.size()), textFormat_.Get(),
                                  primaryRect, itemDisabled ? textSecondaryBrush_.Get() : textBrush_.Get());
            }
            if (!secondary.empty()) {
                D2D1_RECT_F secondaryRect = D2D1::RectF(textX, y + h / 2.0f, x + w - 8.0f, y + h - 2.0f);
                context->DrawText(secondary.c_str(), static_cast<UINT32>(secondary.size()), textFormat_.Get(),
                                  secondaryRect, textSecondaryBrush_.Get());
            }
        }

        context->PopAxisAlignedClip();
    };

    if (dualPane_) {
        const float paneWidth = viewportSize.width / 2.0f;
        if (homePane_[0]) RenderHomePane(0, 0.0f, paneWidth, viewportSize.height);
        else RenderPane(columns_, 0.0f, paneWidth, scrollOffset, verticalScrollOffset_, activePane_ == 0);
        // Keep the dual-pane divider regardless of which surface each pane shows.
        context->DrawLine(D2D1_POINT_2F{paneWidth, kBadgeHeight}, D2D1_POINT_2F{paneWidth, viewportSize.height},
                           borderBrush_.Get(), 2.0f);
        if (homePane_[1]) RenderHomePane(1, paneWidth, paneWidth, viewportSize.height);
        else RenderPane(columns2_, paneWidth, paneWidth, scrollOffset2, verticalScrollOffset2_, activePane_ == 1);
    } else {
        if (homePane_[0]) RenderHomePane(0, 0.0f, viewportSize.width, viewportSize.height);
        else RenderPane(columns_, 0.0f, viewportSize.width, scrollOffset, verticalScrollOffset_, true);
    }
}

void ColumnView::SetDualPane(bool enabled) {
    if (dualPane_ == enabled) return;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        if (enabled) {
            // NavigationWorkspace creates the second context at the current
            // location. Clone the visible column model so the new pane is useful
            // immediately instead of presenting an empty surface until another
            // navigation event occurs.
            // quick-access-home 4.2: if pane 0 is Home, do NOT clone its (empty)
            // column model into pane 2 -- the shell sets pane 2's state right after.
            if (homePane_[0]) {
                columns2_.clear();
            } else {
                columns2_ = columns_;
            }
            focusedColumnIndex2_ = focusedColumnIndex_;
            scrollOffset2_ = scrollOffset_;
            verticalScrollOffset2_ = verticalScrollOffset_;
        }
        dualPane_ = enabled;
        if (!enabled) {
            // NavigationWorkspace keeps whichever pane was active when the user
            // disabled dual-pane mode. Promote that pane's rendered model to the
            // single-pane slot instead of silently reverting to the old left pane.
            if (activePane_ == 1) {
                columns_ = std::move(columns2_);
                focusedColumnIndex_ = focusedColumnIndex2_;
                scrollOffset_ = scrollOffset2_;
                verticalScrollOffset_ = verticalScrollOffset2_;
            }
            columns2_.clear();
            activePane_ = 0;
        }
    }
    // semantic-uia-core-surfaces 2.6: pane set changed on the Column View group.
    NotifyUia({UiaEventKind::StructureBulkAdded, -1, -1, -1, -1, -1, /*home=*/false, /*group=*/true});
    NotifyUia({UiaEventKind::StructureBulkRemoved, -1, -1, -1, -1, -1, /*home=*/false, /*group=*/true});
}

void ColumnView::ActivatePane(int paneIndex) {
    bool changed = false;
    if (paneIndex == 0 || (dualPane_ && paneIndex == 1)) {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        changed = activePane_ != paneIndex;
        activePane_ = paneIndex;
    }
    // semantic-uia-core-surfaces 2.6: pane switch moves the logical focus.
    if (changed) {
        NotifyUia({UiaEventKind::FocusChanged});
    }
}

std::vector<Column>& ColumnView::ActiveColumns() {
    return activePane_ == 0 ? columns_ : columns2_;
}

const std::vector<Column>& ColumnView::ActiveColumns() const {
    return activePane_ == 0 ? columns_ : columns2_;
}

int& ColumnView::ActiveFocusedColumnIndex() {
    return activePane_ == 0 ? focusedColumnIndex_ : focusedColumnIndex2_;
}

float& ColumnView::ActiveScrollOffset() {
    return activePane_ == 0 ? scrollOffset_ : scrollOffset2_;
}

void ColumnView::SaveActivePaneState(std::vector<std::wstring>& outColumnPaths, int& outFocusedIndex, float& outScrollOffset) const {
    SavePaneState(activePane_, outColumnPaths, outFocusedIndex, outScrollOffset);
}

void ColumnView::SavePaneState(int paneIndex, std::vector<std::wstring>& outColumnPaths,
                               int& outFocusedIndex, float& outScrollOffset) const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    // quick-access-home 4.2: a Home pane has no column model to persist, so the
    // shell treats an empty result as "re-navigate from currentPath" (which for
    // Home means staying Home). Stale column paths must never persist.
    if (paneIndex < 0 || paneIndex > 1 || (paneIndex == 1 && !dualPane_) || homePane_[paneIndex]) {
        outColumnPaths.clear();
        outFocusedIndex = 0;
        outScrollOffset = 0.0f;
        return;
    }
    const auto& columns = paneIndex == 0 ? columns_ : columns2_;
    const int focusedIndex = paneIndex == 0 ? focusedColumnIndex_ : focusedColumnIndex2_;
    const float scrollOffset = paneIndex == 0 ? scrollOffset_ : scrollOffset2_;
    outColumnPaths.clear();
    outColumnPaths.reserve(columns.size());
    for (const auto& column : columns) {
        outColumnPaths.push_back(column.path);
    }
    outFocusedIndex = focusedIndex;
    outScrollOffset = scrollOffset;
}

void ColumnView::RestoreActivePaneState(const std::vector<std::wstring>& columnPaths, int focusedIndex, float scrollOffset) {
    RestorePaneState(activePane_, columnPaths, focusedIndex, scrollOffset);
}

void ColumnView::RestorePaneState(int paneIndex, const std::vector<std::wstring>& columnPaths,
                                  int focusedIndex, float scrollOffset) {
    if (columnPaths.empty()) return;
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        // quick-access-home 4.2: ignore restore for Home panes; the shell sets
        // Home state separately via SetPaneHome, so no stale columns reappear.
        if (paneIndex < 0 || paneIndex > 1 || (paneIndex == 1 && !dualPane_) || homePane_[paneIndex]) return;
        auto& columns = paneIndex == 0 ? columns_ : columns2_;
        int& focusedIndexRef = paneIndex == 0 ? focusedColumnIndex_ : focusedColumnIndex2_;
        float& scrollOffsetRef = paneIndex == 0 ? scrollOffset_ : scrollOffset2_;
        columns.clear();
        for (const auto& path : columnPaths) {
            Column column;
            column.path = path;
            columns.push_back(std::move(column));
        }
        focusedIndexRef = std::clamp(focusedIndex, 0, static_cast<int>(columns.size()) - 1);
        scrollOffsetRef = scrollOffset;
        changed = true;
    }
    // semantic-uia-core-surfaces 2.6: structure events fire post-lock.
    if (changed) {
        NotifyUia({UiaEventKind::StructureBulkRemoved, paneIndex, -1, -1});
        NotifyUia({UiaEventKind::StructureBulkAdded, paneIndex, -1, -1});
    }
    for (const auto& path : columnPaths) {
        if (!path.empty() && engineClient_ != nullptr) engineClient_->RequestDirectory(path);
    }
}

// ---------------------------------------------------------------------------
// semantic-uia-core-surfaces: UiaSurface implementation. Every query
// projects real render/interaction state; every action routes into the exact
// mutation logic keyboard/mouse input uses (design D3/D7-D10).
// ---------------------------------------------------------------------------

UiaViewGeometry ColumnView::UiaGeometry() const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    return uiaGeometry_;
}

int ColumnView::UiaPaneCount() const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    return dualPane_ ? 2 : 1;
}

int ColumnView::UiaActivePane() const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    return activePane_;
}

bool ColumnView::UiaPaneIsHome(int pane) const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    return (pane == 0 || pane == 1) && homePane_[static_cast<size_t>(pane)];
}

int ColumnView::UiaColumnCount(int pane) const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    if (pane != 0 && pane != 1) return 0;
    return static_cast<int>((pane == 0 ? columns_ : columns2_).size());
}

UiaColumnInfo ColumnView::UiaColumnInfoOf(int pane, int col) const {
    UiaColumnInfo info;
    std::lock_guard<std::mutex> lock(columnsMutex_);
    if (pane != 0 && pane != 1) return info;
    const auto& columns = pane == 0 ? columns_ : columns2_;
    if (col < 0 || col >= static_cast<int>(columns.size())) return info;
    const Column& column = columns[static_cast<size_t>(col)];
    info.path = column.path;
    info.itemCount = static_cast<int>(column.items.size());
    switch (column.freshness.state) {
        case SnapshotViewState::Unavailable:
            info.status = UiaColumnStatus::Unavailable;
            info.statusText = UnavailableMessage(column);
            break;
        case SnapshotViewState::Loading:
        case SnapshotViewState::StaleWithError:
            info.status = UiaColumnStatus::Loading;
            info.statusText = StatusMessage(column);
            break;
        case SnapshotViewState::Current:
            if (column.items.empty()) {
                info.status = UiaColumnStatus::Empty;
                info.statusText = L"This folder is empty";
            }
            break;
    }
    return info;
}

UiaItemInfo ColumnView::UiaItemInfoOf(int pane, int col, int row) const {
    UiaItemInfo info;
    std::lock_guard<std::mutex> lock(columnsMutex_);
    if (pane != 0 && pane != 1) return info;
    const auto& columns = pane == 0 ? columns_ : columns2_;
    if (col < 0 || col >= static_cast<int>(columns.size())) return info;
    const Column& column = columns[static_cast<size_t>(col)];
    if (row < 0 || row >= static_cast<int>(column.items.size())) return info;
    const ColumnItem& item = column.items[static_cast<size_t>(row)];
    info.name = item.name;
    info.isDirectory = item.isDirectory;
    info.sizeBytes = item.sizeBytes;
    info.attributes = item.attributes;
    info.volumeRowId = item.volumeRowId;
    info.fileIdLow = item.fileIdLow;
    info.fileIdHigh = item.fileIdHigh;
    info.fullPath = JoinPath(column.path, item.name);
    info.selected = column.selectedIndices.contains(row);
    info.focused = column.focusIndex == row;
    return info;
}

int ColumnView::UiaFocusedColumn(int pane) const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    if (pane != 0 && pane != 1) return 0;
    return pane == 0 ? focusedColumnIndex_ : focusedColumnIndex2_;
}

int ColumnView::UiaFocusedRow(int pane, int column) const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    if (pane != 0 && pane != 1) return -1;
    const auto& columns = pane == 0 ? columns_ : columns2_;
    if (column < 0 || column >= static_cast<int>(columns.size())) return -1;
    return columns[static_cast<size_t>(column)].focusIndex;
}

float ColumnView::UiaVerticalScroll(int pane) const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    if (pane != 0 && pane != 1) return 0.0f;
    return pane == 0 ? verticalScrollOffset_ : verticalScrollOffset2_;
}

int ColumnView::UiaHomeItemCount(int pane) const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    if (pane != 0 && pane != 1) return 0;
    return ComputeHomeLayoutForPane(pane).totalItemCount;
}

UiaHomeItemInfo ColumnView::UiaHomeItemInfoOf(int pane, int globalIndex) const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    UiaHomeItemInfo info;
    if (pane != 0 && pane != 1) return info;
    const HomeLayoutResult layout = ComputeHomeLayoutForPane(pane);
    const HomeItemLayout* found = FindHomeItem(layout, globalIndex);
    if (found == nullptr) return info;
    info.sectionKind = found->sectionIndex;
    info.indexInSection = found->indexInSection;
    info.globalIndex = globalIndex;
    info.x = found->x;
    info.y = found->y;
    info.width = found->width;
    info.height = found->height;
    info.enabled = HomeItemFocusable(homeData_, *found);
    switch (static_cast<HomeSectionKind>(found->sectionIndex)) {
        case HomeSectionKind::Pinned:
            if (homeData_.pinned.empty()) {
                info.primary = L"Pin folders to see them here."; // hint placeholder
                info.enabled = false;
            } else {
                const auto& b = homeData_.pinned[static_cast<size_t>(found->indexInSection)];
                info.primary = b.displayName.empty() ? b.path : b.displayName;
                info.secondary = b.path;
                info.value = b.path;
            }
            break;
        case HomeSectionKind::RecentFolders:
            info.primary = homeData_.recentFolders[static_cast<size_t>(found->indexInSection)];
            info.value = info.primary;
            break;
        case HomeSectionKind::RecentSearches:
            info.primary = homeData_.recentSearches[static_cast<size_t>(found->indexInSection)];
            info.value = info.primary;
            break;
        case HomeSectionKind::CommonLocations: {
            const auto& b = homeData_.knownFolders[static_cast<size_t>(found->indexInSection)];
            info.primary = b.displayName.empty() ? b.path : b.displayName;
            info.secondary = b.path;
            info.value = b.path;
            break;
        }
        case HomeSectionKind::QuickActions: {
            static const wchar_t* kLabels[] = {L"New Tab", L"Search", L"Open Path", L"Settings"};
            static const wchar_t* kCommands[] = {
                L"navigation.new-tab", L"search.focus", L"navigation.focus-path", L"app.settings"};
            if (found->indexInSection >= 0 && found->indexInSection < 4) {
                info.primary = kLabels[found->indexInSection];
                info.value = kCommands[found->indexInSection];
            }
            break;
        }
    }
    return info;
}

int ColumnView::UiaHomeSectionCount(int pane) const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    if (pane != 0 && pane != 1) return 0;
    return static_cast<int>(ComputeHomeLayoutForPane(pane).sections.size());
}

UiaHomeSectionInfo ColumnView::UiaHomeSectionInfoOf(int pane, int section) const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    UiaHomeSectionInfo info;
    if (pane != 0 && pane != 1) return info;
    const HomeLayoutResult layout = ComputeHomeLayoutForPane(pane);
    if (section < 0 || section >= static_cast<int>(layout.sections.size())) return info;
    const HomeSectionLayout& s = layout.sections[static_cast<size_t>(section)];
    info.sectionKind = s.sectionIndex;
    info.itemCount = s.itemCount;
    info.firstGlobalIndex = s.itemStart;
    info.headerX = s.headerX;
    info.headerY = s.headerY;
    info.headerWidth = s.headerWidth;
    info.headerHeight = s.headerHeight;
    info.clearAffordance = s.itemCount > 0 &&
        (s.sectionIndex == static_cast<int>(HomeSectionKind::RecentFolders) ||
         s.sectionIndex == static_cast<int>(HomeSectionKind::RecentSearches));
    return info;
}

UiaHomeScrollInfo ColumnView::UiaHomeScrollInfoOf(int pane) const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    UiaHomeScrollInfo info;
    if (pane != 0 && pane != 1) return info;
    const HomeLayoutResult layout = ComputeHomeLayoutForPane(pane);
    info.columnCount = layout.columnCount;
    info.contentHeightDip = layout.contentHeight;
    return info;
}

int ColumnView::UiaHomeFocusTarget(int pane) const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    if (pane != 0 && pane != 1) return kUiaHomeFocusNone;
    const HomeFocusEntry focus = pane == 0 ? homeFocus_[0] : homeFocus_[1];
    switch (focus.target) {
        case HomeFocusTarget::Item: return kUiaHomeFocusItem;
        case HomeFocusTarget::ClearRecentFolders: return kUiaHomeFocusClearRecentFolders;
        case HomeFocusTarget::ClearRecentSearches: return kUiaHomeFocusClearRecentSearches;
        default: return kUiaHomeFocusNone;
    }
}

int ColumnView::UiaHomeFocusIndex(int pane) const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    if (pane != 0 && pane != 1) return -1;
    return (pane == 0 ? homeFocus_[0] : homeFocus_[1]).itemIndex;
}

void ColumnView::UiaActivatePane(int pane) {
    ActivatePane(pane);
}

void ColumnView::UiaSelectItem(int pane, int col, int row) {
    UiaSelectionSnapshot before;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        before = CaptureUiaSelectionLocked(pane, col);
    }
    if (!before.valid) return;
    if (pane != activePane_) ActivatePane(pane);
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        auto& columns = pane == 0 ? columns_ : columns2_;
        if (col < 0 || col >= static_cast<int>(columns.size())) return;
        Column& column = columns[static_cast<size_t>(col)];
        if (row < 0 || row >= static_cast<int>(column.items.size())) return;
        SelectSingle(column, row);
        int& focusedIndex = pane == 0 ? focusedColumnIndex_ : focusedColumnIndex2_;
        focusedIndex = col;
    }
    NotifyUiaSelectionTransition(before, pane);
}

void ColumnView::UiaToggleItem(int pane, int col, int row) {
    UiaSelectionSnapshot before;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        before = CaptureUiaSelectionLocked(pane, col);
    }
    if (!before.valid) return;
    if (pane != activePane_) ActivatePane(pane);
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        auto& columns = pane == 0 ? columns_ : columns2_;
        if (col < 0 || col >= static_cast<int>(columns.size())) return;
        Column& column = columns[static_cast<size_t>(col)];
        if (row < 0 || row >= static_cast<int>(column.items.size())) return;
        ToggleSelection(column, row);
        int& focusedIndex = pane == 0 ? focusedColumnIndex_ : focusedColumnIndex2_;
        focusedIndex = col;
    }
    NotifyUiaSelectionTransition(before, pane);
}

void ColumnView::UiaRangeSelectItem(int pane, int col, int row) {
    UiaSelectionSnapshot before;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        before = CaptureUiaSelectionLocked(pane, col);
    }
    if (!before.valid) return;
    if (pane != activePane_) ActivatePane(pane);
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        auto& columns = pane == 0 ? columns_ : columns2_;
        if (col < 0 || col >= static_cast<int>(columns.size())) return;
        Column& column = columns[static_cast<size_t>(col)];
        if (row < 0 || row >= static_cast<int>(column.items.size())) return;
        SelectRange(column, row);
        int& focusedIndex = pane == 0 ? focusedColumnIndex_ : focusedColumnIndex2_;
        focusedIndex = col;
    }
    NotifyUiaSelectionTransition(before, pane);
}

void ColumnView::UiaActivateItem(int pane, int col, int row) {
    if (pane != activePane_) ActivatePane(pane);
    ActivateItem(col, row, false, false);
}

void ColumnView::UiaSetItemFocus(int pane, int col, int row) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        if (pane != 0 && pane != 1) return;
        auto& columns = pane == 0 ? columns_ : columns2_;
        if (col < 0 || col >= static_cast<int>(columns.size())) return;
        if (row < 0 || row >= static_cast<int>(columns[static_cast<size_t>(col)].items.size())) return;
        int& focusedIndex = pane == 0 ? focusedColumnIndex_ : focusedColumnIndex2_;
        Column& column = columns[static_cast<size_t>(col)];
        changed = focusedIndex != col || column.focusIndex != row;
        if (changed) {
            focusedIndex = col;
            column.focusIndex = row;
        }
    }
    if (changed) {
        NotifyUiaFocus();
    }
}

void ColumnView::UiaSetColumnFocus(int pane, int col) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        if (pane != 0 && pane != 1) return;
        auto& columns = pane == 0 ? columns_ : columns2_;
        if (col < 0 || col >= static_cast<int>(columns.size())) return;
        int& focusedIndex = pane == 0 ? focusedColumnIndex_ : focusedColumnIndex2_;
        changed = focusedIndex != col;
        focusedIndex = col;
    }
    if (changed) {
        NotifyUiaFocus();
    }
}

void ColumnView::UiaScrollPane(int pane, float deltaX, float deltaY) {
    if (pane != 0 && pane != 1) return;
    // selection-input-hardening: snapshot the shared scroll state under the
    // mutex like every other accessor (a UIA client can race UI-thread
    // updates); ClampVerticalScrollForPane / UiaSetHorizontalScroll take the
    // lock themselves, so nothing is held across them.
    float heightDip = 0.0f;
    float currentHorizontal = 0.0f;
    bool home = false;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        heightDip = uiaGeometry_.heightDip;
        home = homePane_[static_cast<size_t>(pane)];
        currentHorizontal = pane == 0 ? uiaGeometry_.hScroll0Dip : scrollOffset2_;
    }
    if (deltaY != 0.0f) {
        ClampVerticalScrollForPane(pane, deltaY, heightDip);
    }
    if (deltaX != 0.0f && !home) {
        UiaSetHorizontalScroll(pane, currentHorizontal + deltaX);
    }
}

void ColumnView::UiaSetHorizontalScroll(int pane, float offsetDip) {
    if (pane != 0 && pane != 1) return;
    if (homePane_[static_cast<size_t>(pane)]) return;
    float clamped = 0.0f;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        const auto& columns = pane == 0 ? columns_ : columns2_;
        const float paneWidth = dualPane_ ? (uiaGeometry_.widthDip / 2.0f) : uiaGeometry_.widthDip;
        const float maxScroll = (std::max)(0.0f,
            static_cast<float>(columns.size()) * kColumnWidth - paneWidth);
        clamped = std::clamp(offsetDip, 0.0f, maxScroll);
        if (pane == 1) {
            scrollOffset2_ = clamped;
            // selection-model-interactions click fix: the UIA providers derive
            // pane-1 BoundingRectangles from uiaGeometry_.hScroll1Dip, which is
            // otherwise only re-pushed by WindowShell::Render. A direct
            // Scroll-pattern write must move the cached geometry together with
            // scrollOffset2_, or the reported row rects (and native clicks at
            // their centers) stay at the pre-scroll position while the render
            // and hit-test paths already use the new offset.
            uiaGeometry_.hScroll1Dip = clamped;
        }
    }
    // Pane 0's horizontal offset is shell-animated (Shift+wheel path); the
    // shell registered the writer and animates the same FloatAnimation.
    if (pane == 0 && uiaHorizontalScrollWriter_) {
        uiaHorizontalScrollWriter_(clamped);
    }
    // Pane 1 has no animation bridge: request a frame directly so the painted
    // rows catch up with the offset the providers now report.
    if (pane == 1 && repaint_) {
        repaint_();
    }
}

void ColumnView::UiaScrollItemIntoView(int pane, int col, int row) {
    bool scrolled = false;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        if (pane != 0 && pane != 1) return;
        auto& columns = pane == 0 ? columns_ : columns2_;
        if (col < 0 || col >= static_cast<int>(columns.size())) return;
        Column& column = columns[static_cast<size_t>(col)];
        if (row < 0 || row >= static_cast<int>(column.items.size())) return;
        // Same clamp math as EnsureFocusedItemVisible, without touching focus
        // (including the same status-banner inset).
        const float visibleHeight =
            (std::max)(0.0f, uiaGeometry_.heightDip - kBadgeHeight - ColumnContentTopInset(column.freshness.state));
        const float top = static_cast<float>(row) * kRowHeight;
        const float bottom = top + kRowHeight;
        float& offset = pane == 0 ? verticalScrollOffset_ : verticalScrollOffset2_;
        if (top < offset) offset = top;
        else if (bottom > offset + visibleHeight) offset = bottom - visibleHeight;
        const float maximum = (std::max)(0.0f,
            static_cast<float>(column.items.size()) * kRowHeight - visibleHeight);
        offset = std::clamp(offset, 0.0f, maximum);
        scrolled = true;
    }
    // Providers query the vertical offset live (UiaVerticalScroll), but the
    // painted rows lag until the next frame; request one post-lock so a click
    // at the freshly reported bounds lands on the moved row.
    if (scrolled && repaint_) {
        repaint_();
    }
}

void ColumnView::UiaActivateHomeItem(int pane, int globalIndex, bool openInNewTab) {
    if (pane != 0 && pane != 1) return;
    ActivateHomeItem(pane, globalIndex, openInNewTab);
}

void ColumnView::UiaActivateHomeClear(int pane, int clearKind) {
    if (pane != 0 && pane != 1 || homeActivateCallback_ == nullptr) return;
    HomeActivation activation;
    activation.kind = clearKind == kUiaHomeClearRecentFolders
        ? HomeActionKind::ClearRecentFolders : HomeActionKind::ClearRecentSearches;
    homeActivateCallback_(activation);
}

void ColumnView::UiaSetHomeFocusItem(int pane, int globalIndex) {
    if (pane != 0 && pane != 1) return;
    const HomeLayoutResult layout = ComputeHomeLayoutForPane(pane);
    const HomeItemLayout* found = FindHomeItem(layout, globalIndex);
    if (found == nullptr || !HomeItemFocusable(homeData_, *found)) return;
    HomeFocusEntry& focus = pane == 0 ? homeFocus_[0] : homeFocus_[1];
    const HomeFocusEntry target{HomeFocusTarget::Item, globalIndex};
    if (focus == target) return;
    focus = target;
    NotifyUiaFocus();
}

void ColumnView::UiaSetHomeFocusClear(int pane, int clearKind) {
    if (pane != 0 && pane != 1) return;
    const int wantedSection = clearKind == kUiaHomeClearRecentFolders
        ? static_cast<int>(HomeSectionKind::RecentFolders)
        : static_cast<int>(HomeSectionKind::RecentSearches);
    const HomeLayoutResult layout = ComputeHomeLayoutForPane(pane);
    bool present = false;
    for (const auto& section : layout.sections) {
        if (section.sectionIndex == wantedSection && section.itemCount > 0) {
            present = true;
            break;
        }
    }
    if (!present) return;
    HomeFocusEntry& focus = pane == 0 ? homeFocus_[0] : homeFocus_[1];
    const HomeFocusTarget target = clearKind == kUiaHomeClearRecentFolders
        ? HomeFocusTarget::ClearRecentFolders : HomeFocusTarget::ClearRecentSearches;
    if (focus.target == target && focus.itemIndex == -1) return;
    focus = {target, -1};
    NotifyUiaFocus();
}

void ColumnView::UiaScrollHomeItemIntoView(int pane, int globalIndex) {
    if (pane != 0 && pane != 1 || !homePane_[static_cast<size_t>(pane)]) return;
    const HomeLayoutResult layout = ComputeHomeLayoutForPane(pane);
    float itemTop = 0.0f;
    float itemBottom = 0.0f;
    for (const auto& item : layout.items) {
        if (item.globalIndex == globalIndex) {
            itemTop = item.y;
            itemBottom = item.y + item.height;
            break;
        }
    }
    if (itemTop == 0.0f && itemBottom == 0.0f) return;
    std::lock_guard<std::mutex> lock(columnsMutex_);
    const float visibleHeight = (std::max)(0.0f, uiaGeometry_.heightDip - kBadgeHeight);
    float& offset = pane == 0 ? verticalScrollOffset_ : verticalScrollOffset2_;
    if (itemTop < offset) offset = itemTop;
    else if (itemBottom > offset + visibleHeight) offset = itemBottom - visibleHeight;
    const float maximum = (std::max)(0.0f, layout.contentHeight - visibleHeight);
    offset = std::clamp(offset, 0.0f, maximum);
}

// --- provider plumbing ------------------------------------------------------

void ColumnView::SetUiaGeometry(const UiaViewGeometry& geometry) {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    uiaGeometry_ = geometry;
}

void ColumnView::SetUiaKeyboardFocusOwned(bool owned) {
    // Kept in sync with the real focus owner by the shell (WM_SETFOCUS /
    // WM_KILLFOCUS and every render); providers project it into
    // HasKeyboardFocus.
    std::lock_guard<std::mutex> lock(columnsMutex_);
    uiaGeometry_.keyboardFocus = owned;
}

void ColumnView::SetUiaHorizontalScrollWriter(std::function<void(float)> writer) {
    uiaHorizontalScrollWriter_ = std::move(writer);
}

void ColumnView::SetUiaEventSink(UiaEventSink sink, void* context) {
    uiaEvents_.Set(sink, context);
}

void ColumnView::SetUiaTreemapSurface(TreemapUiaSurface* treemapSurface) {
    uiaTreemapSurface_ = treemapSurface;
    if (uiaProvider_ != nullptr) {
        auto* provider = static_cast<ColumnViewUiaProvider*>(uiaProvider_.Get());
        if (provider != nullptr) {
            provider->SetTreemapSurface(treemapSurface);
        }
    }
}

Microsoft::WRL::ComPtr<IRawElementProviderFragmentRoot> ColumnView::UiaRootProvider(HWND hwnd) {
    if (uiaProvider_ == nullptr) {
        if (!uiaState_) {
            uiaState_ = std::make_shared<UiaOwnerState>();
        }
        auto provider = Microsoft::WRL::ComPtr<ColumnViewUiaProvider>(
            new ColumnViewUiaProvider(uiaState_, this, hwnd, uiaTreemapSurface_));
        uiaProvider_ = provider;
        uiaEvents_.Set(&ColumnViewUiaProvider::SinkThunk, provider.Get());
    }
    return uiaProvider_;
}

void ColumnView::ReleaseUiaProvider() {
    // semantic-uia-core-surfaces 1.1/2.5: flip the owner-lifetime token
    // first so client-held elements become safe zombies, then drop the app's
    // own references. UIAutomationCore keeps its own references on behalf of
    // clients and disconnects them when the HWND is destroyed.
    uiaEvents_.Clear();
    if (uiaProvider_ != nullptr) {
        auto* provider = static_cast<ColumnViewUiaProvider*>(uiaProvider_.Get());
        if (provider != nullptr) {
            // Detach the treemap event subscription on the UI thread before
            // the tree is dropped (the surface outlives the provider tree).
            provider->SetTreemapSurface(nullptr);
        }
    }
    if (uiaState_) {
        uiaState_->alive = false;
    }
    uiaState_.reset();
    uiaProvider_.Reset();
}

void ColumnView::NotifyUia(const UiaEventNotification& notification) {
    uiaEvents_.Notify(notification);
}

void ColumnView::NotifyUiaFocus() {
    NotifyUia({UiaEventKind::FocusChanged});
}

ColumnView::UiaSelectionSnapshot ColumnView::CaptureUiaSelectionLocked(int pane, int targetColumn) const {
    UiaSelectionSnapshot snapshot;
    snapshot.targetColumn = targetColumn;
    if (pane != 0 && pane != 1) return snapshot;
    const auto& columns = pane == 0 ? columns_ : columns2_;
    snapshot.focusColumn = pane == 0 ? focusedColumnIndex_ : focusedColumnIndex2_;
    if (snapshot.focusColumn >= 0 && snapshot.focusColumn < static_cast<int>(columns.size())) {
        snapshot.focusRow = columns[static_cast<size_t>(snapshot.focusColumn)].focusIndex;
    }
    if (targetColumn < 0 || targetColumn >= static_cast<int>(columns.size())) return snapshot;
    const Column& column = columns[static_cast<size_t>(targetColumn)];
    snapshot.valid = true;
    snapshot.selected = column.selectedIndices;
    return snapshot;
}

void ColumnView::NotifyUiaSelectionTransition(const UiaSelectionSnapshot& before, int pane) {
    if (!uiaEvents_.HasSink()) return;
    UiaSelectionSnapshot now;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        now = CaptureUiaSelectionLocked(pane, before.targetColumn);
    }
    if (!before.valid && !now.valid) return;
    if (before.selected != now.selected) {
        NotifyUia({UiaEventKind::SelectionInvalidated, pane, before.targetColumn, -1});
        for (int row : now.selected) {
            if (!before.selected.contains(row)) {
                NotifyUia({UiaEventKind::ElementAddedToSelection, pane, before.targetColumn, row});
            }
        }
        for (int row : before.selected) {
            if (!now.selected.contains(row)) {
                NotifyUia({UiaEventKind::ElementRemovedFromSelection, pane, before.targetColumn, row});
            }
        }
    }
    if (before.focusColumn != now.focusColumn || before.focusRow != now.focusRow) {
        NotifyUia({UiaEventKind::FocusChanged, pane, before.targetColumn, -1});
    }
}

} // namespace ffui
