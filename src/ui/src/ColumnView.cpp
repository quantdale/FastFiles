#include "ColumnView.h"
#include "EngineStatusPolicy.h"
#include "SelectionModel.h"
#include "UITheme.h"
#include "UiAnimation.h"
#include "UiComponents.h"
#include "UiStyle.h"
#include "IconCache.h"
#include "ffui/SelectionIdentity.h"
#include "ffui/SnapshotPathLookup.h"
#include "ffui/DisplayPath.h"
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

// ui-v3 column-view: short StatePanel heading paired with UnavailableMessage.
// Terminal and transient-failure columns read as one grammar: a canonical
// title ("Access denied" / "Path not available") plus the detailed sentence
// as the body, instead of a bare corner text (docs/ui-improvement/03 §4).
const wchar_t* UnavailableTitle(SnapshotFailureReason reason) {
    switch (reason) {
        case SnapshotFailureReason::AccessDenied:
            return L"Access denied";
        case SnapshotFailureReason::None:
        case SnapshotFailureReason::PublishFailed:
        case SnapshotFailureReason::EnumerationFailed:
        case SnapshotFailureReason::SnapshotLoadFailed:
        case SnapshotFailureReason::Disconnected:
        case SnapshotFailureReason::PathMissing:
        case SnapshotFailureReason::NoLongerAvailable:
            return L"Path not available";
    }
    return L"Path not available";
}

// ui-v3 column-view: single-source status announcement so the UIA projection
// exposes exactly what the painted StatePanel shows (heading + body). Only
// meaningful for columns with no rows; callers gate on that.
std::wstring ColumnStateAnnouncement(const Column& column) {
    switch (column.freshness.state) {
        case SnapshotViewState::Unavailable:
            return std::wstring(UnavailableTitle(column.freshness.failure)) + L". " +
                   UnavailableMessage(column);
        case SnapshotViewState::Loading:
        case SnapshotViewState::StaleWithError:
            return StatusMessage(column);
        case SnapshotViewState::Current:
            break;
    }
    return L"Empty folder";
}

// ui-v3 column-view: safe single-line label painting for browse rows. The
// fast path keeps the visible-row loop allocation-free: a name estimated to
// fit is drawn with DrawTextW, which clips to the layout rect, so it can
// never paint outside the row. Only names that plausibly overflow pay for
// one trimming text layout (character ellipsis) — bounded by the number of
// visible rows, never by directory size.
void DrawRowLabel(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory, IDWriteTextFormat* format,
                  const wchar_t* text, UINT32 textLength, const D2D1_RECT_F& rect, ID2D1Brush* brush) {
    const float availableWidth = rect.right - rect.left;
    // Generous average-advance estimate (0.62 em): wide/uppercase names err
    // toward the trimming path, which is correct-but-slower, never clipped.
    const float estimatedWidth = format->GetFontSize() * 0.62f * static_cast<float>(textLength);
    const float lineHeight = format->GetFontSize() * 1.4f;
    const float textTop = rect.top + (std::max)(0.0f, ((rect.bottom - rect.top) - lineHeight) * 0.5f);
    if (estimatedWidth <= availableWidth) {
        context->DrawTextW(text, textLength, format,
                           D2D1::RectF(rect.left, textTop, rect.right, textTop + lineHeight), brush);
        return;
    }
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (SUCCEEDED(dwriteFactory->CreateTextLayout(text, textLength, format,
                                                  availableWidth, lineHeight, &layout)) &&
        layout != nullptr) {
        // ui-overhaul phase 3: use the factory's ellipsis trimming sign. A
        // DWRITE_TRIMMING delimiter value alone does not render an ellipsis
        // (the layout silently clips), which left long names ending in a hard
        // cut instead of the documented character ellipsis.
        Microsoft::WRL::ComPtr<IDWriteInlineObject> ellipsis;
        const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        if (SUCCEEDED(dwriteFactory->CreateEllipsisTrimmingSign(layout.Get(), &ellipsis)) &&
            SUCCEEDED(layout->SetTrimming(&trimming, ellipsis.Get()))) {
            context->DrawTextLayout(D2D1::Point2F(rect.left, textTop), layout.Get(), brush,
                                    D2D1_DRAW_TEXT_OPTIONS_CLIP);
            return;
        }
    }
    context->DrawTextW(text, textLength, format, rect, brush);
}

// ---------------------------------------------------------------------------
// ui-v3 home: Home-surface helpers (presentation only)
// ---------------------------------------------------------------------------

// ui-v3 home 3: leaf display name of a folder path ("C:\Work\Reports" ->
// "Reports"). A trailing separator is ignored; a bare drive/root ("C:\")
// yields "C:" so the primary label is never blank.
std::wstring HomeLeafName(const std::wstring& path) {
    if (path.empty()) return path;
    size_t end = path.size();
    if (path[end - 1] == L'\\' || path[end - 1] == L'/') --end;
    const size_t start = path.find_last_of(L"\\/", end - 1);
    const size_t nameStart = start == std::wstring::npos ? 0 : start + 1;
    return path.substr(nameStart, end - nameStart);
}

std::wstring HomeBookmarkDisplayName(const Bookmark& bookmark) {
    return ffui::NormalizePathDerivedDisplayLabel(bookmark.displayName, bookmark.path);
}

// ui-v3 home 5: hover-revealed Clear affordances. The cursor position is not
// stored between frames, so OnMouseMove encodes "the pointer rests on a
// Recent-* header band" as one of these sentinels in `hoverItem_` (real items
// use global indices >= 0; -1 means no hover). Only the home-mode mouse and
// render paths interpret them; every browse-mode comparison is `== row` and
// therefore never matches a sentinel.
constexpr int kHomeHoverClearRecentFolders = -2;
constexpr int kHomeHoverClearRecentSearches = -3;

// ui-v3 home 5: viewport-space band of a laid-out Home section header, using
// the same transform the render path applies (status-badge inset minus the
// vertical scroll offset). `paneX` is the pane's viewport origin; callers
// working in pane-local coordinates pass 0.
D2D1_RECT_F HomeHeaderViewportRect(const ffui::HomeSectionLayout& section, float paneX, float vso) {
    const float left = paneX + section.headerX;
    const float top = ffui::UiMetrics::kBadgeHeight + section.headerY - vso;
    return D2D1::RectF(left, top, left + section.headerWidth, top + section.headerHeight);
}

// ui-v3 home 2: device-independent DirectWrite formats for the Home type ramp
// (Subtitle-size semibold section titles, Caption-size hints and secondary
// lines; sizes come from UiTypeSize/UiMetrics). Text formats carry no color
// and bind to no device, so one lazily created set serves both panes and
// every theme flip. A failed creation retries on the next frame; callers fall
// back to the surface's existing formats meanwhile.
struct HomeTextFormats {
    Microsoft::WRL::ComPtr<IDWriteTextFormat> sectionTitle;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> caption;
    IDWriteFactory* factory = nullptr;
};

HomeTextFormats& HomeFormats() {
    static HomeTextFormats formats;
    return formats;
}

void EnsureHomeFormats(IDWriteFactory* factory) {
    HomeTextFormats& formats = HomeFormats();
    if (factory == nullptr || formats.factory == factory) return;
    if (FAILED(UiCreateTextFormat(factory, ffui::UiTypeSize(ffui::UiTypeRole::Subtitle),
                                  DWRITE_FONT_WEIGHT_SEMI_BOLD, L"Segoe UI",
                                  &formats.sectionTitle))) {
        return;
    }
    formats.sectionTitle->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    if (FAILED(UiCreateTextFormat(factory, ffui::UiTypeSize(ffui::UiTypeRole::Caption),
                                  DWRITE_FONT_WEIGHT_NORMAL, L"Segoe UI",
                                  &formats.caption))) {
        formats.sectionTitle.Reset();
        return;
    }
    formats.caption->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    // ui-v3 home: single-line labels trim with an ellipsis so long names and
    // paths can never paint outside their item bounds (UX-001 stays fixed).
    const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    formats.sectionTitle->SetTrimming(&trimming, nullptr);
    formats.caption->SetTrimming(&trimming, nullptr);
    formats.factory = factory;
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
    // The synthetic drive list is populated synchronously below; it does not
    // have an engine generation that could still be loading.
    root.freshness.state = SnapshotViewState::Current;

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

bool ColumnView::ActivateItem(int columnIndex, int itemIndex, bool control, bool shift,
                              const UiaBrowseActionRequest* expected) {
    const int pane = expected ? expected->pane : activePane_;
    UiaSelectionSnapshot before;
    std::wstring childPath;
    bool isDirectory = false;
    bool clearedEmptyArea = false;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        if (expected && !MatchesUiaBrowseTargetLocked(*expected)) return false;
        before = CaptureUiaSelectionLocked(pane, columnIndex);
        auto& columns = pane == 0 ? columns_ : columns2_;
        if (columnIndex < 0 || columnIndex >= static_cast<int>(columns.size())) {
            return false;
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
            return false;
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

    if (pane != activePane_) ActivatePane(pane);

    // selection-input-hardening: empty-area clear fires the same selection
    // transition diff as a normal click.
    if (clearedEmptyArea) {
        NotifyUiaSelectionTransition(before, pane);
        return true;
    }

    if (control || shift) {
        {
            std::lock_guard<std::mutex> lock(columnsMutex_);
            ActiveFocusedColumnIndex() = columnIndex;
        }
        NotifyUiaSelectionTransition(before, pane);
        return true;
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
    return true;
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
        const int pageRows = (std::max)(1, static_cast<int>(visibleHeight / BrowseRowHeight()));

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
        // state now genuinely offers an action). ui-overhaul phase 3: the hit
        // area matches the content-sized chip, not the whole band.
        const float chipRight = 8.0f + (std::min)((std::max)(0.0f, viewportWidth - 16.0f),
                                                  badgeTextWidth_ + 28.0f);
        if (clientPoint.x <= chipRight && engineState_.load() == EngineConnectionState::Unavailable &&
            retryEngineCallback_) {
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
        // quick-access-home 4.8: the section-header Clear affordances first.
        // RenderHomePane records their viewport-space rects only while the
        // link is revealed (header-band hover or keyboard focus), so a hidden
        // link is never clickable either.
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

bool ColumnView::SetCutPaths(std::vector<std::wstring> paths) {
    // ui-overhaul phase 3: presentation-only cut ghost state. Paths are
    // canonical filesystem paths (same lineage as the column/selection paths),
    // so a direct string match decides the ghosted treatment. Returns true when
    // the effective set changed so the caller can schedule the repaint that
    // makes keyboard cut/copy feedback immediate.
    std::lock_guard<std::mutex> lock(columnsMutex_);
    if (!CutPathSetChanged(cutPathSet_, paths)) return false;
    cutPathSet_.clear();
    for (auto& path : paths) {
        if (!path.empty()) cutPathSet_.insert(std::move(path));
    }
    return true;
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
        // ui-v3 home 5: resting on a Recent-* header band reveals that
        // section's Clear affordance. The band hover is encoded as a sentinel
        // hover target so RenderHomePane can show -- and fade out -- the link
        // without storing the cursor position between frames.
        int hit = -1;
        for (const auto& section : layout.sections) {
            const bool recentHeader =
                section.sectionIndex == static_cast<int>(HomeSectionKind::RecentFolders) ||
                section.sectionIndex == static_cast<int>(HomeSectionKind::RecentSearches);
            if (!recentHeader) continue;
            const D2D1_RECT_F band = HomeHeaderViewportRect(section, 0.0f, vso);
            if (localPoint.x >= band.left && localPoint.x < band.right &&
                localPoint.y >= band.top && localPoint.y < band.bottom) {
                hit = section.sectionIndex == static_cast<int>(HomeSectionKind::RecentFolders)
                          ? kHomeHoverClearRecentFolders
                          : kHomeHoverClearRecentSearches;
                break;
            }
        }
        if (hit == -1) {
            const int rawHit = ComputeHomeItemHitTest(layout, localPoint, kBadgeHeight - vso);
            hit = (rawHit >= 0 && FindHomeItem(layout, rawHit) != nullptr &&
                   HomeItemFocusable(homeData_, *FindHomeItem(layout, rawHit)))
                      ? rawHit : -1;
        }
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
    const float maximum = (std::max)(0.0f, static_cast<float>(itemCount) * BrowseRowHeight() - visibleHeight);
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
    const float top = static_cast<float>(itemIndex) * BrowseRowHeight();
    const float bottom = top + BrowseRowHeight();
    float& offset = activePane_ == 0 ? verticalScrollOffset_ : verticalScrollOffset2_;
    if (top < offset) offset = top;
    else if (bottom > offset + visibleHeight) offset = bottom - visibleHeight;
    const float maximum = (std::max)(0.0f, static_cast<float>(column.items.size()) * BrowseRowHeight() - visibleHeight);
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
// `homeData_` and the last known viewport. Pure + cheap; ui-v3 home 4: empty
// sections (including an empty Pinned list) emit no items and no header at
// all -- the renderer draws the first-run explanation when nothing remains.
HomeLayoutResult ColumnView::ComputeHomeLayoutForPane(int paneIndex) const {
    // Both panes share identical half-width geometry in dual-pane mode and the
    // full width in single-pane mode, so the layout depends only on the last
    // known viewport, not on which pane index is requested.
    (void)paneIndex;
    HomeLayoutInput input{};
    const float paneWidth = dualPane_ ? (lastViewportWidth_ / 2.0f) : lastViewportWidth_;
    input.viewportWidth = (std::max)(0.0f, paneWidth);
    input.viewportHeight = lastViewportHeight_;
    input.pinnedCount = static_cast<int>(homeData_.pinned.size());
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
        backgroundBrush_.Reset(); borderBrush_.Reset(); textBrush_.Reset(); textSecondaryBrush_.Reset();
        selectionBrush_.Reset();
        folderGlyphBrush_.Reset(); fileGlyphBrush_.Reset(); errorBrush_.Reset();
        badgeActiveBrush_.Reset(); badgeActiveTextBrush_.Reset();
        badgeDegradedBrush_.Reset(); badgeDegradedTextBrush_.Reset();
        selectionSoftBrush_.Reset(); dividerBrush_.Reset(); hoverOverlayBrush_.Reset();
        focusStrokeBrush_.Reset(); // quick-access-home 4.3: reset the new brush so it is recreated for the theme
        // ui-v3 column-view: shared-painter caches re-resolve on next paint.
        textTertiaryBrush_.Reset(); focusedColumnFillBrush_.Reset();
        rowBrushes_.Reset(); stateBrushes_.Reset();
        textFormat_.Reset(); badgeTextFormat_.Reset(); glyphFormat_.Reset();
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
    // ui-v3 column-view: quiet tertiary text + the focused-column surface lift.
    context->CreateSolidColorBrush(theme.textTertiary, &textTertiaryBrush_);
    context->CreateSolidColorBrush(theme.surfaceSubtle, &focusedColumnFillBrush_);

    dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        ffui::UiTypeSize(ffui::UiTypeRole::Body), L"en-us", &textFormat_);
    textFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        ffui::UiTypeSize(ffui::UiTypeRole::Caption), L"en-us", &badgeTextFormat_);
    dwriteFactory->CreateTextFormat(
        L"Segoe MDL2 Assets", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, ffui::UiMetrics::kIconSize, L"en-us", &glyphFormat_);
    if (glyphFormat_) {
        glyphFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        glyphFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        glyphFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    resourcesCreated_ = true;
}

void ColumnView::Render(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory, D2D1_SIZE_F viewportSize, float scrollOffset, float scrollOffset2) {
    if (!BrowseContentActive()) {
        context->Clear(ffui::GetUiTheme(darkTheme_).background);
        return;
    }
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
    // ui-overhaul phase 3: the chip is sized to its text instead of spanning
    // the whole workspace -- degraded mode stays first-class without painting a
    // permanent full-width amber band over the file content.
    const EngineStatusPresentation statusPresentation =
        GetEngineStatusPresentation(engineState_.load(), privilegedPathActive_.load());
    const std::wstring statusText = statusPresentation.text;
    if (badgeMeasureText_ != statusText) {
        badgeMeasureText_ = statusText;
        badgeTextWidth_ = 0.0f;
        Microsoft::WRL::ComPtr<IDWriteTextLayout> measureLayout;
        if (badgeTextFormat_ != nullptr &&
            SUCCEEDED(dwriteFactory->CreateTextLayout(statusText.c_str(),
                                                      static_cast<UINT32>(statusText.size()),
                                                      badgeTextFormat_.Get(), 100000.0f,
                                                      kBadgeHeight, &measureLayout)) &&
            measureLayout != nullptr) {
            DWRITE_TEXT_METRICS metrics{};
            if (SUCCEEDED(measureLayout->GetMetrics(&metrics))) {
                badgeTextWidth_ = metrics.width;
            }
        }
    }
    const float badgeWidth = (std::min)((std::max)(0.0f, viewportSize.width - 16.0f),
                                       badgeTextWidth_ + 28.0f);
    D2D1_RECT_F badgeRect = D2D1::RectF(8.0f, 4.0f, 8.0f + badgeWidth, kBadgeHeight - 4.0f);
    const EngineConnectionState connectionState = engineState_.load();
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
    D2D1_RECT_F badgeTextRect = D2D1::RectF(badgeRect.left + 10.0f, 4.0f,
                                            badgeRect.right - 10.0f, kBadgeHeight - 4.0f);
    context->DrawText(statusText.c_str(), static_cast<UINT32>(statusText.size()),
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

    // ui-v3 column-view: per-pane scratch reused across rows — capacity-stable
    // assigns keep the visible-row loop free of steady-state heap traffic.
    std::wstring labelBuffer;
    // ui-overhaul phase 3: scratch for the cut-ghost path comparison (same
    // capacity-stable reuse so ghosting adds no per-row allocation).
    std::wstring cutPathBuffer;

    auto RenderPane = [&](const std::vector<Column>& columns, float paneX, float paneWidth, float paneScroll,
                          float verticalScrollOffset, bool isActivePane, int focusedColumnIndex) {
        // column-uia-identity-dispatch: a partially visible column belongs
        // only to its own pane, including its background and focus ring.
        context->PushAxisAlignedClip(D2D1::RectF(paneX, kBadgeHeight,
            paneX + (std::max)(0.0f, paneWidth), (std::max)(kBadgeHeight, viewportSize.height)),
            D2D1_ANTIALIAS_MODE_ALIASED);
        // ui-v3 column-view: one density read per pane paint drives row height
        // and icon size so every column shares identical baselines.
        const float rowHeight = BrowseRowHeight();
        const float iconSize = ffui::GetUiDensity(ffui::gUiDensityProfile).iconSize;
        for (int i = 0; i < static_cast<int>(columns.size()); ++i) {
            const float x = paneX + i * kColumnWidth - paneScroll;
            if (x + kColumnWidth < paneX || x > paneX + paneWidth) {
                continue; // culled
            }
            const Column& column = columns[i];

            // ui-v3 column-view: active-pane cue without decoration — the
            // focused column lifts one surface step (surfaceSubtle) and carries
            // the stronger separator; every other column keeps the quiet
            // background and the dividerSubtle hairline.
            const bool focusedColumn = isActivePane && i == focusedColumnIndex;
            D2D1_RECT_F columnRect = D2D1::RectF(x, kBadgeHeight, x + kColumnWidth, viewportSize.height);
            context->FillRectangle(columnRect,
                                   focusedColumn && !highContrast ? focusedColumnFillBrush_.Get()
                                                                  : backgroundBrush_.Get());
            // Task 3.6 / ui-v3 column-view: hairline between columns, slightly
            // stronger only where interaction (keyboard focus) requires more.
            context->DrawLine(D2D1_POINT_2F{x + kColumnWidth, kBadgeHeight}, D2D1_POINT_2F{x + kColumnWidth, viewportSize.height},
                              focusedColumn ? borderBrush_.Get() : dividerBrush_.Get(), 1.0f);

            const std::wstring unavailableMessage = UnavailableMessage(column);
            const std::wstring statusMessage = StatusMessage(column);
            if (column.freshness.state == SnapshotViewState::Unavailable) {
                // ui-v3 column-view: terminal states explain themselves through
                // the shared StatePanel grammar instead of a bare corner text.
                D2D1_RECT_F panelRect = D2D1::RectF(x + ffui::UiMetrics::kSpaceXs, kBadgeHeight + ffui::UiMetrics::kSpaceXs,
                                                    x + kColumnWidth - ffui::UiMetrics::kSpaceXs,
                                                    viewportSize.height - ffui::UiMetrics::kSpaceXs);
                ffui::DrawUiStatePanel(context, panelRect, badgeTextFormat_.Get(),
                                       UnavailableTitle(column.freshness.failure),
                                       textFormat_.Get(), unavailableMessage.c_str(),
                                       theme, highContrast, &stateBrushes_);
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
                // ui-v3 column-view: no column is ever a blank rectangle.
                // Loading, empty, and failed-with-no-rows states center a short
                // title plus a concise explanation.
                const wchar_t* stateTitle = nullptr;
                const wchar_t* stateBody = nullptr;
                if (column.freshness.state == SnapshotViewState::Loading) {
                    stateTitle = L"Loading\u2026"; // title alone; the banner already says "Refreshing…"
                } else if (column.freshness.state == SnapshotViewState::StaleWithError) {
                    stateTitle = L"Path not available";
                    stateBody = statusMessage.c_str();
                } else {
                    stateTitle = L"Empty folder";
                    stateBody = L"This location contains no items.";
                }
                D2D1_RECT_F panelRect = D2D1::RectF(x + ffui::UiMetrics::kSpaceXs, contentTop + ffui::UiMetrics::kSpaceXs,
                                                    x + kColumnWidth - ffui::UiMetrics::kSpaceXs,
                                                    viewportSize.height - ffui::UiMetrics::kSpaceXs);
                ffui::DrawUiStatePanel(context, panelRect, badgeTextFormat_.Get(), stateTitle,
                                       textFormat_.Get(), stateBody, theme, highContrast, &stateBrushes_);
                continue;
            }

            for (int r = 0; r < static_cast<int>(column.items.size()); ++r) {
                const float y = contentTop + r * rowHeight - verticalScrollOffset;
                if (y + rowHeight < contentTop || y > viewportSize.height) {
                    continue;
                }

                const ColumnItem& item = column.items[r];
                // ui-overhaul phase 3: a cut item stays ghosted (dimmed) until
                // the pending move is pasted or the clipboard changes. The
                // path comparison reuses one scratch buffer per pane.
                bool cutRow = false;
                if (!cutPathSet_.empty() && !column.path.empty()) {
                    AppendColumnItemPath(cutPathBuffer, column.path, item.name);
                    cutRow = PathIsInCutSet(cutPathSet_, cutPathBuffer);
                }
                const bool isSelected = column.selectedIndices.contains(r);
                // ui-v3 column-view: canonical O(1)/allocation-free state
                // resolution. A selection in the active pane's focused column is
                // SelectedFocused (accent); every other selection — other
                // columns or the inactive pane — is SelectedUnfocused (soft).
                const ffui::UiVisualState rowState =
                    !isSelected ? ffui::UiVisualState::Rest
                                : (focusedColumn ? ffui::UiVisualState::SelectedFocused
                                                 : ffui::UiVisualState::SelectedUnfocused);
                const ffui::UiRowPalette palette = ffui::ResolveUiRowPalette(theme, rowState, highContrast);
                const D2D1_RECT_F rowRect = D2D1::RectF(x + 4.0f, y + 2.0f, x + kColumnWidth - 4.0f,
                                                        y + rowHeight - 2.0f);
                if (rowState != ffui::UiVisualState::Rest) {
                    ffui::DrawUiStateFill(context, rowRect, palette, ffui::UiMetrics::kRadiusSmall,
                                          &rowBrushes_);
                }

                // Task 3.4 / ui-v3 column-view: hover stays an animated token
                // overlay, active pane only, never on a selected row — hover
                // must not look interchangeable with soft selection. The
                // lingering fade-out window (150 ms after OnMouseLeave) keeps
                // the pill visible while it dims; High Contrast suppresses it.
                const bool hoverRow = isActivePane && hoverColumn_ == i && hoverItem_ == r &&
                                      !isSelected && (hoverActive_ || lingerFade);
                if (hoverRow && !highContrast) {
                    const float alpha = hoverOpacity_.Value();
                    if (alpha > 0.001f) {
                        D2D1_COLOR_F transparentOverlay = theme.hoverOverlay;
                        transparentOverlay.a = 0.0f;
                        const D2D1_COLOR_F overlayColor = ffui::UiLerpColor(transparentOverlay, theme.hoverOverlay, alpha);
                        ffui::UiFillHoverOverlay(context, rowRect, ffui::UiMetrics::kRadiusSmall, overlayColor, &hoverOverlayBrush_);
                    }
                }

                // Task 3.2 / ui-v3 column-view: type icon from the shared cache;
                // the slot geometry (position + size) derives from the density
                // profile so icon/text baselines are identical across columns.
                std::wstring iconKey = item.isDirectory ? std::wstring(ffui::FolderKey())
                                                        : ffui::IconKeyForExtension(FileExtensionOf(item.name));
                if (iconCache_ != nullptr) {
                    iconCache_->Prefetch(iconKey); // cheap: dedupes against cache/pending/queued
                }
                ID2D1Bitmap1* icon = nullptr;
                const float iconX = x + ffui::UiMetrics::kSpaceS;
                const float iconY = y + (rowHeight - iconSize) / 2.0f;
                if (iconCache_ != nullptr && iconCache_->Get(context, iconKey, &icon) && icon != nullptr) {
                    // ui-overhaul phase 3: a cut item's icon is dimmed so the
                    // pending move reads before hover/selection takes over.
                    context->DrawBitmap(icon,
                                        D2D1::RectF(iconX, iconY, iconX + iconSize, iconY + iconSize),
                                        cutRow ? 0.45f : 1.0f, D2D1_INTERPOLATION_MODE_LINEAR);
                } else {
                    const float placeholderSide = iconSize * 0.5f;
                    const float centerX = iconX + iconSize * 0.5f;
                    const float centerY = y + rowHeight * 0.5f;
                    D2D1_RECT_F glyphRect = D2D1::RectF(centerX - placeholderSide * 0.5f, centerY - placeholderSide * 0.5f,
                                                        centerX + placeholderSide * 0.5f, centerY + placeholderSide * 0.5f);
                    context->FillRectangle(glyphRect, item.isDirectory ? folderGlyphBrush_.Get() : fileGlyphBrush_.Get());
                }

                // ui-v3 column-view: file name is primary (palette.text —
                // textOnAccent on the focused selection, otherwise theme.text).
                // Directory display names gain the trailing separator via a
                // reused buffer instead of a per-row allocation.
                const wchar_t* labelText = nullptr;
                UINT32 labelLength = 0;
                if (item.isDirectory) {
                    labelBuffer.assign(item.name);
                    if (labelBuffer.empty() || labelBuffer.back() != L'\\') {
                        labelBuffer.push_back(L'\\');
                    }
                    labelText = labelBuffer.c_str();
                    labelLength = static_cast<UINT32>(labelBuffer.size());
                } else {
                    labelText = item.name.c_str();
                    labelLength = static_cast<UINT32>(item.name.size());
                }
                // Directories reserve the chevron gutter; files may use it.
                const float textRight = item.isDirectory ? x + kColumnWidth - 20.0f
                                                         : x + kColumnWidth - ffui::UiMetrics::kSpaceS;
                const D2D1_RECT_F textRect = D2D1::RectF(iconX + iconSize + ffui::UiMetrics::kSpaceXs, y,
                                                         textRight, y + rowHeight);
                if (SUCCEEDED(ffui::UiEnsureSolidBrush(context,
                        cutRow && rowState != ffui::UiVisualState::SelectedFocused
                            ? (highContrast ? ffui::ToD2DColor(GetSysColor(COLOR_GRAYTEXT))
                                            : theme.textDisabled)
                            : palette.text,
                        &rowBrushes_.text))) {
                    DrawRowLabel(context, dwriteFactory, textFormat_.Get(), labelText, labelLength,
                                 textRect, rowBrushes_.text.Get());
                }
                // ui-v3 column-view: continuation affordance — a quiet tertiary
                // chevron, vertically centered with the label baseline.
                if (item.isDirectory) {
                    const D2D1_RECT_F chevronRect = D2D1::RectF(x + kColumnWidth - 16.0f, y,
                                                                x + kColumnWidth - ffui::UiMetrics::kSpaceXs,
                                                                y + rowHeight);
                    if (SUCCEEDED(ffui::UiEnsureSolidBrush(context, theme.textTertiary, &textTertiaryBrush_))) {
                        ffui::UiDrawTextCentered(context, textFormat_.Get(), L"\u203A", chevronRect,
                                                 textTertiaryBrush_.Get());
                    }
                }

                // ui-v3 column-view: keyboard-focus indicator (inset focusStroke
                // ring), independent of selection and drawn last so it sits
                // above fills and overlays. High Contrast falls back to the
                // system window-text color inside DrawUiFocusIndicator.
                if (focusedColumn && r == column.focusIndex) {
                    ffui::DrawUiFocusIndicator(context, rowRect, theme, highContrast,
                                               ffui::UiMetrics::kRadiusSmall, &rowBrushes_);
                }
            }
        }
        context->PopAxisAlignedClip();
    };

    // ui-v3 home: Home rows reuse `DrawRowLabel` above -- the bounded painter
    // browse rows already use (strict layout-rect clipping plus character
    // ellipsis), so long user paths can never paint across a neighboring card
    // or row (the UX-001 contract). The UIA Name/Value surface keeps every
    // full value untruncated.

    // quick-access-home 4.3: render the Home surface for one pane. Drawn in the
    // pane's rect (pane-local origin already removed via `paneX`), starting
    // below the status badge like the column surface. The vertical scroll offset
    // is applied so long lists scroll; a clip rect keeps content out of the
    // badge band and off the dual-pane divider. High Contrast suppresses the
    // token hover overlay and resolves fills through the shared row palette.
    auto RenderHomePane = [&](int paneIndex, float paneX, float paneWidth, float viewportHeight) {
        if (!homePane_[paneIndex]) return;
        const float vso = paneIndex == 0 ? verticalScrollOffset_ : verticalScrollOffset2_;
        const bool isActivePane = (activePane_ == paneIndex);
        // quick-access-home hardening (HOME-POST-03): the focus is a typed
        // target (item or a section-header Clear stop), never a bare index.
        const HomeFocusEntry focusEntry = paneIndex == 0 ? homeFocus_[0] : homeFocus_[1];

        // ui-v3 home: the layout comes from the exact seam hit-testing,
        // keyboard focus, scroll clamping, and UIA use
        // (`ComputeHomeLayoutForPane`), so painted geometry can never drift
        // from what input handling sees.
        EnsureHomeFormats(dwriteFactory);
        const HomeTextFormats& homeFormats = HomeFormats();
        IDWriteTextFormat* const titleFormat =
            homeFormats.sectionTitle ? homeFormats.sectionTitle.Get() : badgeTextFormat_.Get();
        IDWriteTextFormat* const captionFormat =
            homeFormats.caption ? homeFormats.caption.Get() : badgeTextFormat_.Get();
        const HomeLayoutResult layout = ComputeHomeLayoutForPane(paneIndex);

        static const wchar_t* kSectionTitles[] = {
            L"Pinned", L"Recent folders", L"Recent searches", L"Common locations", L"Quick actions"};

        // Clip to the pane minus the badge band.
        D2D1_RECT_F clip = D2D1::RectF(paneX, kBadgeHeight, paneX + paneWidth, viewportHeight);
        context->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);

        // ui-v3 home 4: no dead surface. Empty sections disappear entirely
        // (the seam emits neither items nor a header for them); when there is
        // nothing at all -- no pinned folders, recents, searches, known
        // locations, or actions -- one lightweight centered explanation
        // replaces the blank pane.
        if (layout.totalItemCount == 0) {
            const D2D1_RECT_F panelRect = D2D1::RectF(paneX + kHomeMargin, kBadgeHeight + kHomeMargin,
                                                      paneX + paneWidth - kHomeMargin,
                                                      viewportHeight - kHomeMargin);
            ffui::DrawUiStatePanel(context, panelRect, titleFormat, L"Nothing here yet",
                                   captionFormat,
                                   L"Pinned folders, recent locations, and recent searches will appear here as you work.",
                                   theme, highContrast, &stateBrushes_);
            context->PopAxisAlignedClip();
            return;
        }

        // ui-v3 home 6: Narrow drops the Caption-size count hints; geometry
        // (column count, wide content cap) is already reflowed by the seam
        // through ffui::ClassifyContentWidth.
        const bool narrowLayout = layout.responsiveState == UiResponsiveState::Narrow;

        // ui-v3 home 2: section headers carry the hierarchy -- Subtitle-size
        // semibold titles with a Caption-size count hint for the
        // variable-content sections (ui-v3 home 1), generous kSpaceL-sized
        // bands between sections coming from the seam itself. Recent folders /
        // Recent searches additionally expose a right-aligned "Clear" link,
        // revealed only while the pointer rests on the header band or while
        // the Clear stop owns keyboard focus (ui-v3 home 5: contextual action
        // without permanent icon clutter); its hit rect is recorded for
        // OnMouseDown only while revealed, so a hidden link is never clickable.
        homeClearButtons_[paneIndex] = {};
        for (const auto& section : layout.sections) {
            const float hx = paneX + section.headerX;
            const float hy = kBadgeHeight + section.headerY - vso;
            const wchar_t* title = (section.sectionIndex >= 0 && section.sectionIndex <= 4)
                ? kSectionTitles[section.sectionIndex] : L"";
            context->DrawText(title, static_cast<UINT32>(wcslen(title)), titleFormat,
                              D2D1::RectF(hx, hy - 2.0f, hx + section.headerWidth, hy + 24.0f),
                              textBrush_.Get());
            const bool hasClear =
                section.sectionIndex == static_cast<int>(HomeSectionKind::RecentFolders) ||
                section.sectionIndex == static_cast<int>(HomeSectionKind::RecentSearches);
            const bool counted =
                section.sectionIndex == static_cast<int>(HomeSectionKind::Pinned) || hasClear;
            if (!narrowLayout && counted) {
                wchar_t hintBuffer[32];
                swprintf(hintBuffer, 32, L"%d items", section.itemCount);
                const float hintRight = hasClear ? hx + section.headerWidth - 56.0f
                                                 : hx + section.headerWidth;
                context->DrawText(hintBuffer, static_cast<UINT32>(wcslen(hintBuffer)), captionFormat,
                                  D2D1::RectF(hx, hy + 22.0f, hintRight, hy + section.headerHeight),
                                  textSecondaryBrush_.Get());
            }
            if (!hasClear) continue;

            const float clearWidth = 44.0f;
            const D2D1_RECT_F clearRect = D2D1::RectF(hx + section.headerWidth - clearWidth, hy,
                                                      hx + section.headerWidth, hy + section.headerHeight);
            // quick-access-home hardening (HOME-POST-03): a focused Clear stop
            // keeps its focus treatment even when the pointer is elsewhere --
            // keyboard access never depends on the hover reveal.
            const bool isClearFocused = isActivePane && (
                (focusEntry.target == HomeFocusTarget::ClearRecentFolders &&
                 section.sectionIndex == static_cast<int>(HomeSectionKind::RecentFolders)) ||
                (focusEntry.target == HomeFocusTarget::ClearRecentSearches &&
                 section.sectionIndex == static_cast<int>(HomeSectionKind::RecentSearches)));
            const bool clearHovered = isActivePane && hoverColumn_ == -1 &&
                                      (hoverActive_ || lingerFade) &&
                                      ((section.sectionIndex == static_cast<int>(HomeSectionKind::RecentFolders) &&
                                        hoverItem_ == kHomeHoverClearRecentFolders) ||
                                       (section.sectionIndex == static_cast<int>(HomeSectionKind::RecentSearches) &&
                                        hoverItem_ == kHomeHoverClearRecentSearches));
            if (!isClearFocused && !clearHovered) continue;
            if (isClearFocused) {
                if (highContrast) {
                    // High Contrast: system highlight fill behind the label
                    // (the label flips to highlight text below).
                    context->FillRoundedRectangle(
                        D2D1::RoundedRect(D2D1::RectF(clearRect.left - 1.0f, clearRect.top + 1.0f,
                                                      clearRect.right + 1.0f, clearRect.bottom - 1.0f),
                                          kHomeRadiusMedium, kHomeRadiusMedium),
                        selectionBrush_.Get());
                }
                // Shared keyboard-focus grammar (inset focusStroke ring).
                ffui::DrawUiFocusIndicator(context, clearRect, theme, highContrast,
                                           ffui::UiMetrics::kRadiusSmall, &rowBrushes_);
            }
            ID2D1Brush* clearLabelBrush = textBrush_.Get();
            if (isClearFocused && highContrast) {
                if (SUCCEEDED(ffui::UiEnsureSolidBrush(
                        context, ffui::ToD2DColor(GetSysColor(COLOR_HIGHLIGHTTEXT)), &rowBrushes_.text))) {
                    clearLabelBrush = rowBrushes_.text.Get();
                }
            } else if (!isClearFocused && !highContrast) {
                // Revealed link: the accent token reads as an action rather
                // than a heading; High Contrast keeps the theme text brush.
                clearLabelBrush = selectionBrush_.Get();
            }
            if (section.sectionIndex == static_cast<int>(HomeSectionKind::RecentFolders)) {
                homeClearButtons_[paneIndex].recentFolders = clearRect;
            } else {
                homeClearButtons_[paneIndex].recentSearches = clearRect;
            }
            ffui::UiDrawTextCentered(context, captionFormat, L"Clear", clearRect, clearLabelBrush);
        }

        // ui-v3 home: single-line item text through one helper — the caller
        // picks the format (Body for primary names, Caption for secondary
        // paths). ui-v3 hardening V3-04: both lines go through the same
        // bounded character-ellipsis trimming DrawRowLabel already provides for
        // column labels; raw DrawTextW with a NO_WRAP format used to overflow
        // long recent-folder paths across the neighboring grid column.
        const auto DrawHomeText = [&](const std::wstring& text, const D2D1_RECT_F& rect,
                                      ID2D1Brush* brush, IDWriteTextFormat* format) {
            if (text.empty() || brush == nullptr || format == nullptr) {
                return;
            }
            DrawRowLabel(context, dwriteFactory, format, text.c_str(),
                         static_cast<UINT32>(text.size()), rect, brush);
        };
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
                        primary = HomeBookmarkDisplayName(b);
                        secondary = ffui::NormalizeDisplayPath(b.path);
                        isFolderIcon = true;
                    }
                    break;
                case HomeSectionKind::RecentFolders:
                    // ui-overhaul phase 4: recent destinations read as a
                    // name + path card (matching Pinned/Common locations)
                    // instead of a single overlong path line.
                    primary = HomeLeafName(homeData_.recentFolders[item.indexInSection]);
                    secondary = ffui::NormalizeDisplayPath(homeData_.recentFolders[item.indexInSection]);
                    isFolderIcon = true;
                    break;
                case HomeSectionKind::RecentSearches:
                    primary = homeData_.recentSearches[item.indexInSection];
                    glyph = L"\uE721"; // magnifier
                    break;
                case HomeSectionKind::CommonLocations: {
                    const auto& b = homeData_.knownFolders[item.indexInSection];
                    primary = HomeBookmarkDisplayName(b);
                    secondary = ffui::NormalizeDisplayPath(b.path);
                    isFolderIcon = true;
                    break;
                }
                case HomeSectionKind::QuickActions: {
                    static const wchar_t* kLabels[] = {L"New Tab", L"Search", L"Analyze Storage", L"Open Path",
                                                       L"Settings"};
                    static const wchar_t* kGlyphs[] = {L"\uE8A7", L"\uE721", L"\uE9D2", L"\uE8B7", L"\uE713"};
                    if (item.indexInSection >= 0 && item.indexInSection < ffui::kHomeActionCount) {
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
                IDWriteTextFormat* format = glyphFormat_ ? glyphFormat_.Get() : textFormat_.Get();
                context->DrawText(glyph.c_str(), 1, format, glyphRect, textSecondaryBrush_.Get());
                textX = x + 32.0f;
            }

            if (!primary.empty()) {
                const float halfH = y + h / 2.0f;
                D2D1_RECT_F primaryRect = D2D1::RectF(textX, y + 2.0f, x + w - 8.0f,
                                                      secondary.empty() ? y + h - 2.0f : halfH);
                DrawHomeText(primary, primaryRect,
                             itemDisabled ? textSecondaryBrush_.Get() : textBrush_.Get(),
                             textFormat_.Get());
            }
            if (!secondary.empty()) {
                D2D1_RECT_F secondaryRect = D2D1::RectF(textX, y + h / 2.0f, x + w - 8.0f, y + h - 2.0f);
                DrawHomeText(secondary, secondaryRect, textSecondaryBrush_.Get(), captionFormat);
            }
        }

        context->PopAxisAlignedClip();
    };

    if (dualPane_) {
        const float paneWidth = viewportSize.width / 2.0f;
        if (homePane_[0]) RenderHomePane(0, 0.0f, paneWidth, viewportSize.height);
        else RenderPane(columns_, 0.0f, paneWidth, scrollOffset, verticalScrollOffset_, activePane_ == 0,
                        focusedColumnIndex_);
        // Keep the dual-pane divider regardless of which surface each pane shows.
        context->DrawLine(D2D1_POINT_2F{paneWidth, kBadgeHeight}, D2D1_POINT_2F{paneWidth, viewportSize.height},
                           borderBrush_.Get(), 2.0f);
        if (homePane_[1]) RenderHomePane(1, paneWidth, paneWidth, viewportSize.height);
        else RenderPane(columns2_, paneWidth, paneWidth, scrollOffset2, verticalScrollOffset2_, activePane_ == 1,
                        focusedColumnIndex2_);
    } else {
        if (homePane_[0]) RenderHomePane(0, 0.0f, viewportSize.width, viewportSize.height);
        else RenderPane(columns_, 0.0f, viewportSize.width, scrollOffset, verticalScrollOffset_, true,
                        focusedColumnIndex_);
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
    // semantic-uia-core-surfaces 2.6: report the actual group transition.
    // Emitting both directions for every toggle made UIA clients observe an
    // impossible remove/add pair and obscured the fragment replacement
    // boundary.
    NotifyUia({enabled ? UiaEventKind::StructureBulkAdded : UiaEventKind::StructureBulkRemoved,
               -1, -1, -1, -1, -1, /*home=*/false, /*group=*/true});
}

void ColumnView::SetBrowseContentActive(bool active) {
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        if (browseContentActive_ == active) return;
        browseContentActive_ = active;
    }
    // original-vision-journey-recovery: a tool replaces the complete Browse
    // presentation. Retire its fragment, including client-held Home items,
    // and mint a fresh fragment when Browse is presented again.
    NotifyUia({active ? UiaEventKind::StructureBulkAdded : UiaEventKind::StructureBulkRemoved,
               -1, -1, -1, -1, -1, /*home=*/false, /*group=*/true});
}

bool ColumnView::BrowseContentActive() const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    return browseContentActive_;
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
    info.displayPath = ffui::NormalizeDisplayPath(column.path);
    info.itemCount = static_cast<int>(column.items.size());
    switch (column.freshness.state) {
        case SnapshotViewState::Unavailable:
            info.status = UiaColumnStatus::Unavailable;
            // ui-v3 column-view: announce exactly what the StatePanel paints
            // (canonical heading + detailed sentence).
            info.statusText = ColumnStateAnnouncement(column);
            break;
        case SnapshotViewState::Loading:
        case SnapshotViewState::StaleWithError:
            info.status = UiaColumnStatus::Loading;
            info.statusText = StatusMessage(column);
            break;
        case SnapshotViewState::Current:
            if (column.items.empty()) {
                info.status = UiaColumnStatus::Empty;
                info.statusText = L"Empty folder"; // ui-v3 column-view: matches the painted panel title
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
    info.displayFullPath = ffui::NormalizeDisplayPath(info.fullPath);
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
        case HomeSectionKind::Pinned: {
            // ui-v3 home 4: an empty Pinned list emits no items at all (no
            // dead hint box), so an emitted item always maps to a bookmark.
            const auto& b = homeData_.pinned[static_cast<size_t>(found->indexInSection)];
            info.primary = HomeBookmarkDisplayName(b);
            info.secondary = ffui::NormalizeDisplayPath(b.path);
            info.value = b.path;
            break;
        }
        case HomeSectionKind::RecentFolders: {
            // ui-v3 home 3: Name mirrors the painted leaf-name label while the
            // full path rides the secondary line and stays the Value, so
            // activation and identity are unchanged.
            const std::wstring& path =
                homeData_.recentFolders[static_cast<size_t>(found->indexInSection)];
            const std::wstring displayPath = ffui::NormalizeDisplayPath(path);
            info.primary = HomeLeafName(displayPath);
            if (info.primary.empty()) info.primary = displayPath;
            info.secondary = displayPath;
            info.value = path;
            break;
        }
        case HomeSectionKind::RecentSearches:
            info.primary = homeData_.recentSearches[static_cast<size_t>(found->indexInSection)];
            info.value = info.primary;
            break;
        case HomeSectionKind::CommonLocations: {
            const auto& b = homeData_.knownFolders[static_cast<size_t>(found->indexInSection)];
            info.primary = HomeBookmarkDisplayName(b);
            info.secondary = ffui::NormalizeDisplayPath(b.path);
            info.value = b.path;
            break;
        }
        case HomeSectionKind::QuickActions: {
            static const wchar_t* kLabels[] = {L"New Tab", L"Search", L"Analyze Storage", L"Open Path",
                                               L"Settings"};
            static const wchar_t* kCommands[] = {
                L"navigation.new-tab", L"search.focus", L"storage.analyze", L"navigation.focus-path",
                L"app.settings"};
            if (found->indexInSection >= 0 && found->indexInSection < ffui::kHomeActionCount) {
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

void ColumnView::ScrollUiaItemIntoViewLocked(int pane, Column& column, int row) {
    // Same clamp math as EnsureFocusedItemVisible, without touching focus
    // (including the same status-banner inset).
    const float visibleHeight =
        (std::max)(0.0f, uiaGeometry_.heightDip - kBadgeHeight - ColumnContentTopInset(column.freshness.state));
    const float top = static_cast<float>(row) * BrowseRowHeight();
    const float bottom = top + BrowseRowHeight();
    float& offset = pane == 0 ? verticalScrollOffset_ : verticalScrollOffset2_;
    if (top < offset) offset = top;
    else if (bottom > offset + visibleHeight) offset = bottom - visibleHeight;
    const float maximum = (std::max)(0.0f,
        static_cast<float>(column.items.size()) * BrowseRowHeight() - visibleHeight);
    offset = std::clamp(offset, 0.0f, maximum);
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
        ScrollUiaItemIntoViewLocked(pane, column, row);
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

bool ColumnView::MatchesUiaBrowseTargetLocked(const UiaBrowseActionRequest& request) const {
    if (!request.ownerLifetime || !request.ownerLifetime->alive ||
        !request.fragmentLifetime || !request.fragmentLifetime->alive || !browseContentActive_ ||
        request.pane < 0 || request.pane >= (dualPane_ ? 2 : 1) || homePane_[static_cast<size_t>(request.pane)]) return false;
    const auto& columns = request.pane == 0 ? columns_ : columns2_;
    if (request.column < 0 || request.column >= static_cast<int>(columns.size())) return false;
    const auto& column = columns[static_cast<size_t>(request.column)];
    if (UiaHashWide(column.path) != request.pathHash) return false;
    if (request.kind == UiaBrowseActionKind::FocusColumn) return true;
    if (request.row < 0 || request.row >= static_cast<int>(column.items.size())) return false;
    const auto& item = column.items[static_cast<size_t>(request.row)];
    return UiaItemIdentityHash(item.volumeRowId, item.fileIdLow, item.fileIdHigh,
                               JoinPath(column.path, item.name)) == request.itemHash;
}

bool ColumnView::UiaDispatchBrowseAction(const UiaBrowseActionRequest& request) {
    const HWND owner = UiaGeometry().hwnd;
    if (owner == nullptr || !IsWindow(owner)) return false;
    const DWORD thread = GetWindowThreadProcessId(owner, nullptr);
    if (thread == 0) return false;
    // Route even same-thread callers through the shell's canonical post-action
    // reconciliation. No model lock is held across this synchronous dispatch.
    return SendMessageW(owner, WM_APP_COLUMNVIEW_BROWSE_ACTION, 0,
                        reinterpret_cast<LPARAM>(&request)) != 0;
}

bool ColumnView::HandleUiaBrowseAction(const UiaBrowseActionRequest& request) {
    const HWND owner = UiaGeometry().hwnd;
    if (owner == nullptr || GetWindowThreadProcessId(owner, nullptr) != GetCurrentThreadId() ||
        uiaProvider_ == nullptr || request.ownerLifetime != uiaState_ || !uiaState_->alive) return false;
    const auto* provider = static_cast<ColumnViewUiaProvider*>(uiaProvider_.Get());
    if (request.kind == UiaBrowseActionKind::FocusRoot) {
        if (request.fragmentLifetime && (request.fragmentLifetime != provider->FragmentState() ||
            !request.fragmentLifetime->alive || !UiaBrowseContentActive())) return false;
        ::SetFocus(owner);
        return GetFocus() == owner;
    }
    if (request.fragmentLifetime != provider->FragmentState()) return false;
    if (request.kind == UiaBrowseActionKind::FocusPane) {
        {
            std::lock_guard<std::mutex> lock(columnsMutex_);
            if (!request.fragmentLifetime || !request.fragmentLifetime->alive || !browseContentActive_ ||
                request.pane < 0 || request.pane >= (dualPane_ ? 2 : 1)) return false;
        }
        ActivatePane(request.pane);
        ::SetFocus(owner);
        return GetFocus() == owner;
    }
    if (request.kind == UiaBrowseActionKind::Invoke) {
        return ActivateItem(request.column, request.row, false, false, &request);
    }
    UiaSelectionSnapshot before;
    bool notifySelection = false;
    bool notifyFocus = false;
    bool scrolled = false;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        if (!MatchesUiaBrowseTargetLocked(request)) return false;
        auto& column = (request.pane == 0 ? columns_ : columns2_)[static_cast<size_t>(request.column)];
        int& focus = request.pane == 0 ? focusedColumnIndex_ : focusedColumnIndex2_;
        before = CaptureUiaSelectionLocked(request.pane, request.column);
        switch (request.kind) {
        case UiaBrowseActionKind::Select:
            SelectSingle(column, request.row);
            focus = request.column;
            notifySelection = true;
            break;
        case UiaBrowseActionKind::AddSelection:
        case UiaBrowseActionKind::RemoveSelection: {
            const bool selected = column.selectedIndices.contains(request.row);
            const bool wanted = request.kind == UiaBrowseActionKind::AddSelection;
            if (selected != wanted) {
                ToggleSelection(column, request.row);
                focus = request.column;
                notifySelection = true;
            }
            break;
        }
        case UiaBrowseActionKind::FocusColumn:
            notifyFocus = focus != request.column;
            focus = request.column;
            break;
        case UiaBrowseActionKind::FocusItem:
            notifyFocus = focus != request.column || column.focusIndex != request.row;
            focus = request.column;
            column.focusIndex = request.row;
            break;
        case UiaBrowseActionKind::ScrollItem: {
            ScrollUiaItemIntoViewLocked(request.pane, column, request.row);
            scrolled = true;
            break;
        }
        default: return false;
        }
    }
    if (notifySelection) {
        ActivatePane(request.pane);
        NotifyUiaSelectionTransition(before, request.pane);
    }
    if (request.kind == UiaBrowseActionKind::FocusItem || request.kind == UiaBrowseActionKind::FocusColumn) {
        ActivatePane(request.pane);
        ::SetFocus(owner);
        if (notifyFocus) NotifyUiaFocus();
        return GetFocus() == owner;
    }
    if (scrolled && repaint_) repaint_();
    return true;
}

bool ColumnView::UiaDispatchHomeAction(const UiaHomeActionRequest& request) {
    const HWND owner = UiaGeometry().hwnd;
    if (owner == nullptr || !IsWindow(owner)) return false;
    const DWORD ownerThread = GetWindowThreadProcessId(owner, nullptr);
    if (ownerThread == 0) return false;
    if (ownerThread == GetCurrentThreadId()) return HandleUiaHomeAction(request);
    // Synchronous dispatch keeps this closed-data request alive until the
    // owner has validated it. Never execute shell callbacks on failure.
    return SendMessageW(owner, WM_APP_COLUMNVIEW_HOME_ACTION, 0,
                        reinterpret_cast<LPARAM>(&request)) != 0;
}

bool ColumnView::HandleUiaHomeAction(const UiaHomeActionRequest& request) {
    const HWND owner = UiaGeometry().hwnd;
    if (owner == nullptr || GetWindowThreadProcessId(owner, nullptr) != GetCurrentThreadId() ||
        uiaProvider_ == nullptr || request.ownerLifetime != uiaState_) return false;
    const auto* provider = static_cast<ColumnViewUiaProvider*>(uiaProvider_.Get());
    if (request.fragmentLifetime != provider->FragmentState()) return false;
    // Validation and the canonical activation now share the UI thread. A
    // changed item, retired fragment, or hidden workspace cannot retarget.
    return UiaSurface::UiaDispatchHomeAction(request);
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

std::wstring ColumnView::UiaDetailsStatus() const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    return uiaDetailsStatus_;
}

void ColumnView::SetUiaDetailsStatus(std::wstring status) {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    uiaDetailsStatus_ = std::move(status);
}

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
