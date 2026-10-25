#pragma once
// Design tokens/metrics; kColumnWidth/kRowHeight/kBadgeHeight alias UiMetrics
// constants at the bottom of the class so they cannot diverge from the shared
// ramp (see modernize-ui-appearance foundation).
#include "UITheme.h"
#include "UiAnimation.h"
#include "UiComponents.h" // ui-v3 column-view: canonical row-state/brush-cache painters
#include <algorithm>
#include <array>
#include <atomic>
#include <d2d1_1.h>
#include <dwrite.h>
#include <cwctype>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <wrl/client.h>

#include "ffprotocol/SnapshotFormat.h"
#include "ffprotocol/UiProtocol.h"
#include "ffui/SnapshotFreshness.h"

#include "ffui/HomeLayout.h" // quick-access-home 4.x: pure Home layout seam
#include "HomeData.h"        // quick-access-home 4.1: HomeData / HomeActivation

#include "ffui/UiaSurface.h"  // semantic-uia-core-surfaces 1.3: pure UIA projection seam

#ifdef _DEBUG
#include "ffui/SelectionIdentity.h"
#endif

#include "EngineClient.h"
#include "Preview.h"
#include "CommandSystem.h"

#include "UiaNotifications.h" // semantic-uia-core-surfaces 2.6: semantic event sink
#include "UiaProviderBase.h"  // semantic-uia-core-surfaces 1.1: owner-lifetime token

namespace ffui {
inline constexpr UINT WM_APP_COLUMNVIEW_HOME_ACTION = WM_APP + 45;

// semantic-treemap-uia 10.x: forward declaration -- ColumnView only stores
// the pointer to the Storage Analysis treemap surface.
class TreemapUiaSurface;

// Provided by the shared type-icon cache (IconCache.h); forward-declared here
// because ColumnView only stores the pointer and never needs the complete type.
class IconCache;

struct ColumnItem {
    std::wstring name;
    bool isDirectory = false;
    uint64_t sizeBytes = 0;
    // allocated-size-storage-accuracy 6.3: size-on-disk with explicit known
    // state, carried from the snapshot for the details pane.
    uint64_t allocatedSizeBytes = 0;
    bool allocatedSizeKnown = false;
    uint32_t attributes = 0;
    uint64_t fileIdLow = 0;
    uint64_t fileIdHigh = 0;
    int64_t volumeRowId = 0;
};

struct SelectionSummary {
    std::vector<FileDescriptor> items;
    uint64_t knownSizeBytes = 0;
};

#ifdef _DEBUG
struct SelectionDiagnostics {
    std::wstring activePath;
    std::vector<std::wstring> visibleItemNames;
    std::optional<SelectionIdentity> selectedIdentity;
    int selectedIndex = -1;
    std::optional<SelectionIdentity> focusedIdentity;
    int focusedIndex = -1;
    std::wstring operationTarget;
};
#endif

struct Column {
    std::wstring path;
    std::vector<ColumnItem> items;
    std::set<int> selectedIndices;
    int selectionAnchor = -1;
    int focusIndex = -1;
    SnapshotFreshness freshness;
};

// Pure path-chain construction used by direct address/sidebar/startup
// navigation. Keeping it header-visible lets the Finder-style ancestor
// contract be regression-tested without creating a renderer or engine.
// ui-overhaul phase 3: canonical cut-ghost seam. A browse row renders ghosted
// when its joined path is in the current cut set. Both helpers are pure so the
// join/match contract is headless-testable without a renderer.
inline void AppendColumnItemPath(std::wstring& out, const std::wstring& columnPath,
                                 const std::wstring& itemName) {
    out.assign(columnPath);
    if (!out.empty() && out.back() != L'\\' && out.back() != L'/') out.push_back(L'\\');
    out.append(itemName);
}

inline bool PathIsInCutSet(const std::set<std::wstring>& cutPaths, const std::wstring& joinedPath) {
    return !cutPaths.empty() && !joinedPath.empty() && cutPaths.find(joinedPath) != cutPaths.end();
}

// ui-overhaul phase 3: the cut-ghost state feeds the repaint decision, so
// "did the effective set change" is a pure contract: empty strings never
// enter the set, duplicates collapse, and only a real membership or size
// delta is a change. Callers use this to repaint exactly when cut/copy
// feedback actually changes.
inline bool CutPathSetChanged(const std::set<std::wstring>& current,
                              const std::vector<std::wstring>& next) {
    std::set<std::wstring> candidate;
    for (const auto& path : next) {
        if (!path.empty()) candidate.insert(path);
    }
    return candidate != current;
}

inline std::vector<std::wstring> BuildNavigationPathChain(const std::wstring& path) {
    std::vector<std::wstring> result;
    if (path.starts_with(L"\\\\?\\") && path.size() >= 7 &&
        std::iswalpha(path[4]) && path[5] == L':' && path[6] == L'\\') {
        std::wstring current = path.substr(0, 7);
        result.push_back(current);
        size_t start = 7;
        while (start < path.size()) {
            const size_t end = path.find(L'\\', start);
            const std::wstring component = path.substr(start, end == std::wstring::npos
                                                               ? std::wstring::npos
                                                               : end - start);
            if (!component.empty()) {
                if (current.back() != L'\\') current += L'\\';
                current += component;
                result.push_back(current);
            }
            if (end == std::wstring::npos) break;
            start = end + 1;
        }
        return result;
    }

    if (path.starts_with(L"\\\\?\\UNC\\")) {
        const size_t serverEnd = path.find(L'\\', 8);
        const size_t shareEnd = serverEnd == std::wstring::npos
            ? std::wstring::npos : path.find(L'\\', serverEnd + 1);
        if (shareEnd == std::wstring::npos) {
            if (!path.empty()) result.push_back(path);
            return result;
        }
        std::wstring current = path.substr(0, shareEnd);
        result.push_back(current);
        size_t start = shareEnd + 1;
        while (start < path.size()) {
            const size_t end = path.find(L'\\', start);
            const std::wstring component = path.substr(start, end == std::wstring::npos
                                                               ? std::wstring::npos
                                                               : end - start);
            if (!component.empty()) {
                if (current.back() != L'\\') current += L'\\';
                current += component;
                result.push_back(current);
            }
            if (end == std::wstring::npos) break;
            start = end + 1;
        }
        return result;
    }

    if (path.size() >= 3 && path[1] == L':' && path[2] == L'\\') {
        std::wstring current = path.substr(0, 3);
        result.push_back(current);
        size_t start = 3;
        while (start < path.size()) {
            const size_t end = path.find(L'\\', start);
            const std::wstring component = path.substr(start, end == std::wstring::npos
                                                               ? std::wstring::npos
                                                               : end - start);
            if (!component.empty()) {
                if (current.back() != L'\\') current += L'\\';
                current += component;
                result.push_back(current);
            }
            if (end == std::wstring::npos) break;
            start = end + 1;
        }
        return result;
    }

    if (path.rfind(L"\\\\", 0) == 0) {
        const size_t serverEnd = path.find(L'\\', 2);
        const size_t shareEnd = serverEnd == std::wstring::npos
            ? std::wstring::npos : path.find(L'\\', serverEnd + 1);
        if (shareEnd == std::wstring::npos) {
            if (!path.empty()) result.push_back(path);
            return result;
        }
        std::wstring current = path.substr(0, shareEnd);
        result.push_back(current);
        size_t start = shareEnd + 1;
        while (start < path.size()) {
            const size_t end = path.find(L'\\', start);
            const std::wstring component = path.substr(start, end == std::wstring::npos
                                                               ? std::wstring::npos
                                                               : end - start);
            if (!component.empty()) {
                if (current.back() != L'\\') current += L'\\';
                current += component;
                result.push_back(current);
            }
            if (end == std::wstring::npos) break;
            start = end + 1;
        }
        return result;
    }

    if (!path.empty()) result.push_back(path);
    return result;
}

// Tasks 5.3-5.10: the Finder-style multi-column navigation model and its
// Direct2D/DirectWrite rendering, reading directory contents through
// EngineClient's degraded-mode-backed snapshot (design.md D5).
class ColumnView : public UiaSurface {
public:
    ColumnView();
    ~ColumnView() override;
    void Initialize(EngineClient* engineClient);

    // Task 5.3/5.4/5.5: selecting an item in a column. Folders truncate
    // and (re)populate the next column; files never create a new column.
    // This is the "full" selection used by mouse clicks and Enter.
    void ActivateItem(int columnIndex, int itemIndex, bool control = false, bool shift = false);

    // Task 5.10: keyboard navigation. Up/Down move the visual selection
    // within the focused column only (no column population); Left/Right
    // move keyboard focus across existing columns; Enter activates the
    // focused column's current selection (equivalent to ActivateItem).
    void OnKeyDown(int virtualKey);

    // Mouse hit-testing entry point (translates a client-area point into
    // a column/item index and calls ActivateItem).
    void OnMouseDown(D2D1_POINT_2F clientPoint, float scrollOffset, float viewportWidth, bool control, bool shift);

    // Task 3 restyle: paint-only hover tracking (reuses OnMouseDown's hit-test
    // math minus the activation — hovering never selects), the repaint bridge
    // that wakes the shell while the fade is in flight, and the shared
    // type-icon cache. WindowShell keeps its animation timer alive while
    // HoverAnimating() returns true so Render can advance the fade each frame.
    void SetIconCache(IconCache* iconCache);
    // ui-overhaul phase 3: items whose paths are pending a cut/move render
    // ghosted (dimmed) until the move is pasted or the clipboard changes,
    // matching the file-manager grammar for an in-flight cut. Pass an empty
    // list to clear. Presentation-only: selection identity and operations are
    // unchanged. Returns true when the effective set changed.
    bool SetCutPaths(std::vector<std::wstring> paths);
    void SetRepaintCallback(std::function<void()> repaint);
    // selection-input-hardening: invoked when Enter is pressed over a focused
    // NON-DIRECTORY. The shell wires this to the `item.open` command path
    // (the same seam double-click uses); directories keep navigating in-process.
    void SetOpenFocusedItemCallback(std::function<void()> openFocusedItem);
    void OnMouseMove(D2D1_POINT_2F clientPoint, float scrollOffset, float viewportWidth);
    void OnMouseLeave();
    bool HoverAnimating() const;

    // Dual-pane (task 6.1/6.4): enable/disable the second navigation
    // surface and switch active pane. When disabled, only pane 0 is used.
    void SetDualPane(bool enabled);
    void SetBrowseContentActive(bool active);
    bool BrowseContentActive() const;
    bool UiaBrowseContentActive() const override { return BrowseContentActive(); }
    bool IsDualPane() const { return dualPane_; }
    void ActivatePane(int paneIndex);
    int ActivePane() const { return activePane_; }
    const std::vector<Column>& ActiveColumns() const;
    std::vector<Column>& ActiveColumns();

    // Real filesystem paths for the currently active pane selection.  File
    // operations and drag start consume this single selection contract.
    std::vector<std::wstring> ActiveSelectionPaths() const;
    std::vector<SelectionItem> ActiveSelectionItems() const;
    std::wstring ActivePanePath() const;
    // stabilize-live-uia-e2e: path of an arbitrary pane (drop-point destination).
    std::wstring PanePath(size_t pane) const;
    std::wstring RootPath() const;
    void SelectAll();
    void RefreshActiveColumn();
    // Re-issue directory requests for every materialized column after an
    // engine reconnect. The engine's snapshot mapping is process-lifetime
    // state, so a new engine instance needs the visible columns repopulated.
    void RefreshAllColumns();
    void NavigateToPath(const std::wstring& path, const std::wstring& selectName = {});
    // Return the active pane to the synthetic "This PC" drive-root column.
    // Unlike a filesystem path this state has no engine request behind it.
    void NavigateToComputerRoot();
    void NavigateToHierarchy(const std::wstring& fullPath, bool isDirectory);
    void NavigateToHierarchy(const std::vector<std::wstring>& segments, bool isDirectory);
    int FocusedItemIndex() const;

    void OnSnapshotUpdated(uint64_t announcedGeneration = 0);
    void OnDirectoryError(const std::wstring& path, ffprotocol::DirectoryErrorReason reason);
    void ShowUnavailableLocation(const std::wstring& displayName);
    void OnEngineConnectionState(EngineConnectionState state);
    // recovery-degraded-browsing D3: `state` is the UI-side connection
    // lifecycle; `privilegedPathActive` mirrors the engine's index-service
    // status report. Written from EngineClient's background callback,
    // read by Render on the UI thread (both atomics).
    void SetEngineStatus(EngineConnectionState state, bool privilegedPathActive);
    // recovery-degraded-browsing 3.2: invoked when the user clicks the
    // status chip in the Unavailable state.
    void SetRetryEngineCallback(std::function<void()> retryEngine);
    void SetDarkTheme(bool dark);
    void SetScrollOffset2(float offset) { scrollOffset2_ = offset; }
    float ScrollOffset2() const { return scrollOffset2_; }
    void ScrollVertical(float delta, float viewportHeight);
    void EnsureFocusedItemVisible(float viewportHeight);
    float VerticalScrollOffset() const { return verticalScrollOffset_; }
    // Pane-aware accessor (selection-input-hardening): consumers that must
    // mirror renderer math for a specific pane (e.g. inline-rename edit
    // placement in dual-pane mode) read the matching vertical offset.
    float VerticalScrollOffset(int pane) const { return pane == 0 ? verticalScrollOffset_ : verticalScrollOffset2_; }

    // task 1.3: save/restore per-pane column state for multi-context tab switching.
    void SaveActivePaneState(std::vector<std::wstring>& outColumnPaths, int& outFocusedIndex, float& outScrollOffset) const;
    void RestoreActivePaneState(const std::vector<std::wstring>& columnPaths, int focusedIndex, float scrollOffset);
    // Save/restore a specific pane without changing which pane receives input.
    // WindowShell uses this to persist both contexts of a per-tab dual-pane
    // workspace before switching tabs.
    void SavePaneState(int paneIndex, std::vector<std::wstring>& outColumnPaths,
                       int& outFocusedIndex, float& outScrollOffset) const;
    void RestorePaneState(int paneIndex, const std::vector<std::wstring>& columnPaths,
                          int focusedIndex, float scrollOffset);

    // Dual-pane support
    int& ActiveFocusedColumnIndex();
    float& ActiveScrollOffset();

    // quick-access-home 4.2: per-pane Home mode. `SetPaneHome` switches a pane
    // between the filesystem column surface and the Home surface; entering Home
    // clears that pane's stale column model so it is never rendered or
    // engine-refreshed. `ActivePanePath()` returns L"" for a Home pane.
    void SetHomeData(const HomeData& data);
    void SetHomeActivateCallback(std::function<void(const HomeActivation&)> callback);
    void SetPaneHome(int paneIndex, bool home);
    bool IsPaneHome(int paneIndex) const;
    // Activate the active pane's currently focused Home item (keyboard Enter
    // path). Activation always routes through the `homeActivateCallback_`.
    void HomeActivateFocused();

    void Render(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory, D2D1_SIZE_F viewportSize, float scrollOffset, float scrollOffset2 = 0.0f);

    float ContentWidth() const;
    float PaneContentWidth(int paneIndex) const;
    int FocusedColumnIndex() const;
    std::optional<FileDescriptor> CurrentSelection() const;
    SelectionSummary CurrentSelectionSummary() const;
#ifdef _DEBUG
    SelectionDiagnostics CurrentSelectionDiagnostics() const;
#endif
    std::wstring CurrentPath() const;
    // Aliased to the shared UiMetrics ramp (UITheme.h) so the render and any
    // consumers referencing ColumnView::k* constants cannot drift. Runtime row
    // geometry reads BrowseRowHeight() (density profile, ui-v3); this alias
    // stays for external consumers of the compile-time default.
    static constexpr float kColumnWidth = UiMetrics::kColumnWidth;
    static constexpr float kRowHeight = UiMetrics::kRowHeight;
    static constexpr float kBadgeHeight = UiMetrics::kBadgeHeight;

    // semantic-uia-core-surfaces 2.4: UIA surface implementation. The
    // provider tree queries and mutates ONLY through these methods, which
    // project the exact state used for rendering and input (design D3).
    UiaViewGeometry UiaGeometry() const override;
    int UiaPaneCount() const override;
    int UiaActivePane() const override;
    bool UiaPaneIsHome(int pane) const override;
    int UiaColumnCount(int pane) const override;
    UiaColumnInfo UiaColumnInfoOf(int pane, int col) const override;
    UiaItemInfo UiaItemInfoOf(int pane, int col, int row) const override;
    int UiaFocusedColumn(int pane) const override;
    int UiaFocusedRow(int pane, int column) const override;
    float UiaVerticalScroll(int pane) const override;
    int UiaHomeItemCount(int pane) const override;
    UiaHomeItemInfo UiaHomeItemInfoOf(int pane, int globalIndex) const override;
    int UiaHomeSectionCount(int pane) const override;
    UiaHomeSectionInfo UiaHomeSectionInfoOf(int pane, int section) const override;
    UiaHomeScrollInfo UiaHomeScrollInfoOf(int pane) const override;
    int UiaHomeFocusTarget(int pane) const override;
    int UiaHomeFocusIndex(int pane) const override;
    void UiaActivatePane(int pane) override;
    void UiaSelectItem(int pane, int col, int row) override;
    void UiaToggleItem(int pane, int col, int row) override;
    void UiaRangeSelectItem(int pane, int col, int row) override;
    void UiaActivateItem(int pane, int col, int row) override;
    void UiaSetItemFocus(int pane, int col, int row) override;
    void UiaSetColumnFocus(int pane, int col) override;
    void UiaScrollPane(int pane, float deltaX, float deltaY) override;
    void UiaSetHorizontalScroll(int pane, float offsetDip) override;
    void UiaScrollItemIntoView(int pane, int col, int row) override;
    void UiaActivateHomeItem(int pane, int globalIndex, bool openInNewTab) override;
    void UiaActivateHomeClear(int pane, int clearKind) override;
    bool UiaDispatchHomeAction(const UiaHomeActionRequest& request) override;
    bool HandleUiaHomeAction(const UiaHomeActionRequest& request);
    void UiaSetHomeFocusItem(int pane, int globalIndex) override;
    void UiaSetHomeFocusClear(int pane, int clearKind) override;
    void UiaScrollHomeItemIntoView(int pane, int globalIndex) override;

    // semantic-uia-core-surfaces 2.5: provider plumbing. The shell pushes the
    // live viewport geometry on layout/DPI changes and every render,
    // registers the pane-0 horizontal-scroll writer (the shell animates it
    // like Shift+wheel), and creates/releases the fragment root.
    std::wstring UiaDetailsStatus() const override;
    void SetUiaDetailsStatus(std::wstring status);
    void SetUiaGeometry(const UiaViewGeometry& geometry);
    void SetUiaKeyboardFocusOwned(bool owned);
    void SetUiaHorizontalScrollWriter(std::function<void(float)> writer);
    void SetUiaEventSink(UiaEventSink sink, void* context);
    // semantic-treemap-uia 10.x: attaches the Storage Analysis treemap
    // surface to the fragment root so the provider tree describes the
    // treemap while it is the visible workspace.
    void SetUiaTreemapSurface(TreemapUiaSurface* treemapSurface);
    Microsoft::WRL::ComPtr<IRawElementProviderFragmentRoot> UiaRootProvider(HWND hwnd);
    void ReleaseUiaProvider();

private:
    static Column MakeComputerRootColumn();
    void TruncateAfter(int columnIndex);
    void RequestColumn(int columnIndex, const std::wstring& path);
    void RefreshColumnFromSnapshot(Column& column,
                                   const std::map<std::wstring, ffprotocol::SnapshotDirectory>& snapshot,
                                   uint64_t generation,
                                   int pane, int columnIndex,
                                   std::vector<UiaEventNotification>& outEvents);
    void MarkSnapshotLoadFailed(uint64_t generation);
    void MarkPathLoading(const std::wstring& path);
    static void SelectSingle(Column& column, int itemIndex);
    static void ToggleSelection(Column& column, int itemIndex);
    static void SelectRange(Column& column, int itemIndex);
    void EnsureCreated(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory);

    // semantic-uia-core-surfaces 2.4/2.6: UIA projection state and event
    // plumbing. Every event notification fires AFTER the mutating lock is
    // released so a UIA client callback can re-enter the provider without
    // deadlocking (design D6/D12).
    struct UiaSelectionSnapshot {
        bool valid = false;
        int targetColumn = -1;
        std::set<int> selected;
        int focusColumn = -1;
        int focusRow = -1;
    };
    UiaSelectionSnapshot CaptureUiaSelectionLocked(int pane, int targetColumn) const;
    void NotifyUia(const UiaEventNotification& notification);
    void NotifyUiaFocus();
    void NotifyUiaSelectionTransition(const UiaSelectionSnapshot& before, int pane);
    void ClampVerticalScrollForPane(int pane, float delta, float viewportHeight);
    UiaViewGeometry uiaGeometry_;
    std::wstring uiaDetailsStatus_;
    std::function<void(float)> uiaHorizontalScrollWriter_;
    UiaEventRouter uiaEvents_;
    UiaOwnerStatePtr uiaState_;
    Microsoft::WRL::ComPtr<IRawElementProviderFragmentRoot> uiaProvider_;
    // semantic-treemap-uia 10.x: the Storage Analysis treemap semantic
    // surface, passed to the provider at creation / re-wired on attach.
    ffui::TreemapUiaSurface* uiaTreemapSurface_ = nullptr;

    EngineClient* engineClient_ = nullptr;
    mutable std::mutex columnsMutex_;
    std::vector<Column> columns_;
    std::vector<Column> columns2_;
    // ui-overhaul phase 3: full paths pending a cut/move; rows matching an entry
    // render ghosted. Guarded by columnsMutex_.
    std::set<std::wstring> cutPathSet_;
    int focusedColumnIndex_ = 0;
    int focusedColumnIndex2_ = 0;
    int activePane_ = 0;
    bool dualPane_ = false;
    bool browseContentActive_ = true;
    // Written from EngineClient's background callback, read by Render on the
    // UI thread (recovery-degraded-browsing D3).
    std::atomic<EngineConnectionState> engineState_{EngineConnectionState::Starting};
    std::atomic<bool> privilegedPathActive_{false};
    std::function<void()> retryEngineCallback_;

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> backgroundBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> borderBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textSecondaryBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> selectionBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> folderGlyphBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fileGlyphBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> errorBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> badgeActiveBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> badgeActiveTextBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> badgeDegradedBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> badgeDegradedTextBrush_;
    // ui-overhaul phase 3: cached measurement of the engine-status chip text so
    // the chip is sized to its content without a per-frame layout allocation.
    std::wstring badgeMeasureText_;
    float badgeTextWidth_ = 0.0f;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> selectionSoftBrush_;  // unfocused-selection soft fill
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> dividerBrush_;        // subtle 1-DIP column hairline
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hoverOverlayBrush_;   // hover overlay (alpha faded per frame)
    // quick-access-home 4.3: first consumer of the previously-unused
    // `focusStroke` theme token -- the 1-DIP focus ring around the focused Home item.
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> focusStrokeBrush_;
    // ui-v3 column-view: canonical row-state presentation brushes. The two
    // UiBrushCache slots feed the shared UiComponents painters (DrawUiStateFill,
    // DrawUiFocusIndicator, DrawUiStatePanel) so steady-state paint creates no
    // D2D objects; textTertiaryBrush_ quiets the folder continuation chevron;
    // focusedColumnFillBrush_ is the subtle surface lift of the active pane's
    // focused column.
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textTertiaryBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> focusedColumnFillBrush_;
    UiBrushCache rowBrushes_;
    UiBrushCache stateBrushes_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> badgeTextFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> badgeLineLayout_;
    float badgeLineWidth_ = 0.0f;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> glyphFormat_;
    // Task 3 restyle: hover overlay state (see OnMouseMove/OnMouseLeave/
    // HoverAnimating). hoverOpacity_ is advanced by Render every frame the
    // shell repaints under the HoverAnimating() contract.
    IconCache* iconCache_ = nullptr;
    std::function<void()> repaint_;
    // selection-input-hardening: Enter-over-file open request (see setter).
    std::function<void()> openFocusedItemCallback_;
    int hoverItem_ = -1;
    int hoverColumn_ = -1;
    bool hoverActive_ = false;
    uint64_t hoverEnterMs_ = 0;
    uint64_t hoverLeaveMs_ = 0;
    ffui::FloatAnimation hoverOpacity_;
    bool resourcesCreated_ = false;
    bool darkTheme_ = false;
    std::wstring pendingSelectionName_;
    std::wstring pendingSelectionPath_;
    float scrollOffset_ = 0.0f;
    float scrollOffset2_ = 0.0f;
    float verticalScrollOffset_ = 0.0f;
    float verticalScrollOffset2_ = 0.0f;

    // quick-access-home 4.2: Home surface state. `homePane_` marks which panes
    // are showing the Home surface instead of columns; `homeFocus_` is the
    // keyboard/hover focus (a typed target: an item or a section-header Clear
    // affordance -- quick-access-home hardening HOME-POST-03) in each pane's
    // Home item list; the viewport size is remembered from Render so input
    // handlers (which receive no geometry) can rebuild the layout for
    // hit-testing / focus movement.
    HomeData homeData_;
    std::array<bool, 2> homePane_{false, false};
    std::function<void(const HomeActivation&)> homeActivateCallback_;
    std::array<HomeFocusEntry, 2> homeFocus_{};
    float lastViewportWidth_ = 0.0f;
    float lastViewportHeight_ = 0.0f;

    // quick-access-home 4.2: rebuild the Home layout for a pane from `homeData_`
    // and the last known viewport. Pure + cheap; used by Render and input
    // handlers. Empty `pinned` is surfaced as a single placeholder item (the
    // "Pin folders to see them here." hint) so the Pinned section still renders
    // with its header before the real sections, keeping hit-testing consistent.
    HomeLayoutResult ComputeHomeLayoutForPane(int paneIndex) const;
    void ActivateHomeItem(int paneIndex, int globalIndex, bool openInNewTab);
    void HandleHomeKeyDown(int virtualKey);
    // quick-access-home hardening (HOME-POST-03): route activation for the
    // currently focused Home target (item, or a section-header Clear stop).
    void ActivateHomeFocus(const HomeFocusEntry& focus, bool openInNewTab);
    // Drop a pane's focus when its target no longer exists in the fresh data
    // (called from SetHomeData so clearing a section cannot strand focus).
    void RevalidateHomeFocus();

    // quick-access-home 4.8: hit rects (viewport coordinates) of the "Clear"
    // affordances drawn in the Recent folders / Recent searches section
    // headers, one pair per pane. Refreshed every Home render and hit-tested
    // in OnMouseDown so clearing never needs keyboard focus plumbing.
    struct HomeClearButtons {
        D2D1_RECT_F recentFolders{0.0f, 0.0f, 0.0f, 0.0f};
        D2D1_RECT_F recentSearches{0.0f, 0.0f, 0.0f, 0.0f};
    };
    std::array<HomeClearButtons, 2> homeClearButtons_{};
};

// Converts a navigation-surface point into the active pane's local column and
// row.  The point passed by WindowShell is relative to the whole navigation
// viewport; in dual-pane mode pane 1 therefore needs its origin removed before
// horizontal scrolling and column-width division.  Keeping this calculation
// as a pure seam makes the input contract testable without creating a renderer
// or an engine process.
struct ColumnHitTestResult {
    int columnIndex = -1;
    int itemIndex = -1;
};

// ui-v3 column-view: runtime row geometry comes from the shared density
// profile (UITheme.h) instead of a local constant, so Compact keeps today's
// UiMetrics::kRowHeight exactly and Comfortable scales the whole surface —
// render, hit-test seams, and scroll clamps all read this one function.
inline float BrowseRowHeight() {
    return GetUiDensity(gUiDensityProfile).rowHeight;
}

// selection-input-hardening: height (DIPs) of the per-column status banner
// ("Refreshing…" / "Showing previous results"). The render path shifts rows
// down by this inset and the input/scroll paths must subtract the same value,
// so both derive it from this single constant via ColumnContentTopInset.
constexpr float kStatusBannerInsetDips = 24.0f;

// The per-column content-top inset: rows start below the status banner only
// while that column is Loading or StaleWithError. Used by BOTH the render
// path and the hit-test/scroll-clamp paths so they can never diverge.
inline float ColumnContentTopInset(SnapshotViewState state) {
    return state == SnapshotViewState::Loading || state == SnapshotViewState::StaleWithError
        ? kStatusBannerInsetDips : 0.0f;
}

// Column index for a client x coordinate (pane origin and horizontal scroll
// removed). Hit-testing needs the column BEFORE it can know that column's
// status-banner inset, so the column lookup is split from the row lookup.
inline int ComputeColumnIndexAtX(D2D1_POINT_2F clientPoint,
                                 float horizontalScrollOffset,
                                 float viewportWidth,
                                 bool dualPane,
                                 int activePane) {
    if (clientPoint.x < 0.0f || viewportWidth <= 0.0f) {
        return -1;
    }
    float localX = clientPoint.x;
    if (dualPane) {
        const float paneWidth = viewportWidth / 2.0f;
        if (paneWidth <= 0.0f || activePane < 0 || activePane > 1) {
            return -1;
        }
        if (activePane == 1) {
            if (localX < paneWidth || localX >= viewportWidth) {
                return -1;
            }
            localX -= paneWidth;
        } else if (localX >= paneWidth) {
            return -1;
        }
    }
    const float contentX = localX + horizontalScrollOffset;
    if (contentX < 0.0f) {
        return -1;
    }
    return static_cast<int>(contentX / UiMetrics::kColumnWidth);
}

// A folder is activated by the first click in Finder-style Column View.  The
// native double-click gesture therefore has one remaining meaning: open the
// already-selected file with its registered shell association.  Keep this
// policy pure so the message-routing seam can be regression-tested without a
// renderer or an engine process.
inline bool ShouldOpenSelectionOnDoubleClick(std::span<const SelectionItem> selection) {
    return selection.size() == 1 && !selection.front().isDirectory;
}

// selection-input-hardening: Enter over a focused NON-DIRECTORY opens the
// item (the `item.open` command path); Enter over a directory keeps the
// Finder-style navigate-into-column behavior. Pure seam so the routing
// decision is regression-tested without a renderer.
inline bool EnterOpensFile(bool focusValid, bool isDirectory) {
    return focusValid && !isDirectory;
}

inline ColumnHitTestResult ComputeColumnHitTest(D2D1_POINT_2F clientPoint,
                                                float horizontalScrollOffset,
                                                float verticalScrollOffset,
                                                float contentTopInset,
                                                float viewportWidth,
                                                bool dualPane,
                                                int activePane) {
    if (clientPoint.y < UiMetrics::kBadgeHeight || clientPoint.x < 0.0f || viewportWidth <= 0.0f) {
        return {};
    }

    float localX = clientPoint.x;
    if (dualPane) {
        const float paneWidth = viewportWidth / 2.0f;
        if (paneWidth <= 0.0f || activePane < 0 || activePane > 1) {
            return {};
        }
        if (activePane == 1) {
            if (localX < paneWidth || localX >= viewportWidth) {
                return {};
            }
            localX -= paneWidth;
        } else if (localX >= paneWidth) {
            return {};
        }
    }

    const float contentX = localX + horizontalScrollOffset;
    // The same banner inset the render path applies (ColumnContentTopInset);
    // without it a visible status banner shifts every hit by one row.
    const float contentY = clientPoint.y - UiMetrics::kBadgeHeight - contentTopInset + verticalScrollOffset;
    if (contentX < 0.0f || contentY < 0.0f) {
        return {};
    }
    return {static_cast<int>(contentX / UiMetrics::kColumnWidth),
            static_cast<int>(contentY / BrowseRowHeight())};
}

// quick-access-home 4.4: pure hit-test over the Home surface. `clientPoint` is
// already in pane-local DIP coordinates (the caller has removed the pane
// origin), and `contentOffsetY` is the vertical offset the surface is drawn at
// (the status badge height; plus any vertical scroll offset applied at draw
// time). Returns the `globalIndex` of the item whose rect contains the point,
// or -1. The `items` vector is bounded (<= kHomeMaxItems = 200) and in
// ascending global order, so a short linear scan is sufficient and exact.
inline int ComputeHomeItemHitTest(const ffui::HomeLayoutResult& layout, D2D1_POINT_2F clientPoint, float contentOffsetY) {
    const float py = clientPoint.y - contentOffsetY;
    for (const auto& item : layout.items) {
        if (clientPoint.x >= item.x && clientPoint.x <= item.x + item.width &&
            py >= item.y && py <= item.y + item.height) {
            return item.globalIndex;
        }
    }
    return -1;
}

// quick-access-home 4.5: grid-aware Home focus movement. `deltaY` moves a row
// (+-columnCount in global index); `deltaX` moves a column (+-1) only when the
// layout is two columns, otherwise it is a no-op. Out-of-range moves clamp at
// the list bounds. A -1 "no focus" index with any movement lands on the first
// item. Pure and total: a layout with no items (totalItemCount <= 0) returns
// -1. Depends only on `columnCount` / `totalItemCount` so it works on a
// partially-built layout (no `items` needed).
inline int HomeMoveFocus(const ffui::HomeLayoutResult& layout, int currentGlobalIndex, int deltaX, int deltaY) {
    if (layout.totalItemCount <= 0) {
        return -1;
    }
    if (currentGlobalIndex < 0) {
        return 0; // any movement from "no focus" selects the first item
    }
    const int columnCount = (std::max)(1, layout.columnCount);
    int next = currentGlobalIndex + deltaY * columnCount;
    if (deltaX != 0 && columnCount == 2) {
        next += deltaX;
    }
    const int last = (std::max)(0, layout.totalItemCount - 1);
    return (std::clamp)(next, 0, last);
}

// quick-access-home 4.5: Home/End jumps (first / last item of the whole list).
inline int HomeFirstItem(const ffui::HomeLayoutResult& layout) {
    return layout.totalItemCount <= 0 ? -1 : 0;
}

inline int HomeLastItem(const ffui::HomeLayoutResult& layout) {
    return layout.totalItemCount <= 0 ? -1 : (std::max)(0, layout.totalItemCount - 1);
}

// quick-access-home hardening (HOME-POST-02/03): pure Home focus-model seams.
//
// `HomeItemFocusable` decides whether an item participates in keyboard focus,
// hover, and activation: the empty-Pinned hint placeholder is not actionable,
// and an unavailable known folder (empty path) is neither focusable nor
// activatable -- it can never navigate to "This PC".
//
// `BuildHomeFocusList` is the deterministic traversal order: the section-header
// Clear stop of a Recent folders / Recent searches section precedes that
// section's items (matching the header-above-items spatial order), and items
// appear in layout order. A section with no items draws no affordance and
// therefore contributes no target.
//
// `HomeMoveFocusEntry` moves within that list: inside a section the existing
// grid semantics are preserved (Up/Down jump +-columnCount, Left/Right move
// between columns); a move that crosses a section boundary lands on the
// neighboring section's header stop (its Clear affordance when present).
// Initial focus and Home/End always land on items, never on a Clear stop, so a
// stray Enter cannot wipe a history. Non-actionable items are never focused.

inline const ffui::HomeItemLayout* FindHomeItem(const ffui::HomeLayoutResult& layout, int globalIndex) {
    for (const auto& item : layout.items) {
        if (item.globalIndex == globalIndex) return &item;
    }
    return nullptr;
}

inline bool HomeItemFocusable(const ffui::HomeData& data, const ffui::HomeItemLayout& item) {
    switch (static_cast<ffui::HomeSectionKind>(item.sectionIndex)) {
        case ffui::HomeSectionKind::Pinned:
            return item.indexInSection >= 0 &&
                   item.indexInSection < static_cast<int>(data.pinned.size()) &&
                   !data.pinned[static_cast<size_t>(item.indexInSection)].path.empty();
        case ffui::HomeSectionKind::RecentFolders:
            return item.indexInSection >= 0 &&
                   item.indexInSection < static_cast<int>(data.recentFolders.size()) &&
                   !data.recentFolders[static_cast<size_t>(item.indexInSection)].empty();
        case ffui::HomeSectionKind::RecentSearches:
            return item.indexInSection >= 0 &&
                   item.indexInSection < static_cast<int>(data.recentSearches.size());
        case ffui::HomeSectionKind::CommonLocations:
            // An unavailable known folder (empty path) is disabled: not
            // focusable, not hoverable, not activatable.
            return item.indexInSection >= 0 &&
                   item.indexInSection < static_cast<int>(data.knownFolders.size()) &&
                   !data.knownFolders[static_cast<size_t>(item.indexInSection)].path.empty();
        case ffui::HomeSectionKind::QuickActions:
            return item.indexInSection >= 0 && item.indexInSection < ffui::kHomeActionCount;
    }
    return false;
}

inline int HomeSectionOf(const ffui::HomeLayoutResult& layout, int globalIndex) {
    for (const auto& section : layout.sections) {
        if (globalIndex >= section.itemStart && globalIndex < section.itemStart + section.itemCount) {
            return section.sectionIndex;
        }
    }
    return -1;
}

inline std::vector<ffui::HomeFocusEntry> BuildHomeFocusList(const ffui::HomeLayoutResult& layout,
                                                            const ffui::HomeData& data) {
    std::vector<ffui::HomeFocusEntry> entries;
    for (const auto& section : layout.sections) {
        // The Clear affordance of a section with items precedes the section's
        // items (header above items). A section with no items draws no
        // affordance and contributes no target.
        if (section.itemCount > 0) {
            if (section.sectionIndex == static_cast<int>(ffui::HomeSectionKind::RecentFolders)) {
                entries.push_back({ffui::HomeFocusTarget::ClearRecentFolders, -1});
            } else if (section.sectionIndex == static_cast<int>(ffui::HomeSectionKind::RecentSearches)) {
                entries.push_back({ffui::HomeFocusTarget::ClearRecentSearches, -1});
            }
        }
        for (int i = 0; i < section.itemCount; ++i) {
            const size_t index = static_cast<size_t>(section.itemStart + i);
            if (index >= layout.items.size()) break;
            const auto& item = layout.items[index];
            if (HomeItemFocusable(data, item)) {
                entries.push_back({ffui::HomeFocusTarget::Item, item.globalIndex});
            }
        }
    }
    return entries;
}

// First / last ITEM target of the focus list. Initial focus, Home, and End land
// on items only: a Clear stop is reached by traversing across a section
// boundary, never by "jumping" onto it, so a stray Enter cannot clear history.
inline ffui::HomeFocusEntry HomeFirstFocusEntry(const std::vector<ffui::HomeFocusEntry>& focusList) {
    for (const auto& entry : focusList) {
        if (entry.target == ffui::HomeFocusTarget::Item) return entry;
    }
    return {};
}

inline ffui::HomeFocusEntry HomeLastFocusEntry(const std::vector<ffui::HomeFocusEntry>& focusList) {
    for (auto it = focusList.rbegin(); it != focusList.rend(); ++it) {
        if (it->target == ffui::HomeFocusTarget::Item) return *it;
    }
    return {};
}

inline ffui::HomeFocusEntry HomeMoveFocusEntry(const ffui::HomeLayoutResult& layout,
                                               const std::vector<ffui::HomeFocusEntry>& focusList,
                                               ffui::HomeFocusEntry current,
                                               int deltaX, int deltaY) {
    if (focusList.empty()) return {};
    int pos = -1;
    for (int i = 0; i < static_cast<int>(focusList.size()); ++i) {
        if (focusList[static_cast<size_t>(i)] == current) {
            pos = i;
            break;
        }
    }
    // No current focus: any movement selects the first item (never a Clear stop).
    if (pos < 0) return HomeFirstFocusEntry(focusList);

    if (deltaX != 0 && current.target == ffui::HomeFocusTarget::Item) {
        const int grid = HomeMoveFocus(layout, current.itemIndex, deltaX, 0);
        if (grid == current.itemIndex) return current; // horizontal clamp / single column
        for (const auto& entry : focusList) {
            if (entry.target == ffui::HomeFocusTarget::Item && entry.itemIndex == grid) return entry;
        }
        return current; // horizontal neighbor is not focusable: stay put
    }
    if (deltaY == 0) return current;

    if (current.target == ffui::HomeFocusTarget::Item) {
        const int currentSection = HomeSectionOf(layout, current.itemIndex);
        const int grid = HomeMoveFocus(layout, current.itemIndex, 0, deltaY);
        if (grid != current.itemIndex && HomeSectionOf(layout, grid) == currentSection) {
            // Same-section grid jump; use the grid target when it is focusable,
            // otherwise skip the section's non-focusable items in the movement
            // direction (unavailable known folders are skipped, never focused).
            for (const auto& entry : focusList) {
                if (entry.target == ffui::HomeFocusTarget::Item && entry.itemIndex == grid) return entry;
            }
            int k = pos + deltaY;
            while (k >= 0 && k < static_cast<int>(focusList.size())) {
                const auto& entry = focusList[static_cast<size_t>(k)];
                if (entry.target != ffui::HomeFocusTarget::Item) break;
                if (HomeSectionOf(layout, entry.itemIndex) != currentSection) break;
                return entry;
            }
        }
        // Boundary (or clamped) move: scan the focus list in the movement
        // direction for the first entry that belongs to the boundary -- the
        // neighboring section's header Clear stop, or the first item of the
        // neighboring section. Same-section entries are skipped so a two-column
        // boundary move never jumps sideways.
        for (int k = pos + deltaY; k >= 0 && k < static_cast<int>(focusList.size()); k += deltaY) {
            const auto& entry = focusList[static_cast<size_t>(k)];
            if (entry.target != ffui::HomeFocusTarget::Item) return entry; // header Clear stop
            if (HomeSectionOf(layout, entry.itemIndex) != currentSection) return entry;
        }
        const int next = (std::clamp)(pos + deltaY, 0, static_cast<int>(focusList.size()) - 1);
        return focusList[static_cast<size_t>(next)];
    }

    // Clear target: vertical movement steps to the neighboring entry.
    const int next = (std::clamp)(pos + deltaY, 0, static_cast<int>(focusList.size()) - 1);
    return focusList[static_cast<size_t>(next)];
}

// quick-access-home hardening (HOME-POST-02): pure activation mapping. Returns
// nullopt for non-actionable items -- the empty-Pinned hint placeholder, and
// ANY Folder destination whose path is empty (an unavailable known folder or a
// malformed entry must NEVER navigate to "This PC"; Home -> This PC happens
// only through an explicit This PC action).
inline std::optional<ffui::HomeActivation> ComputeHomeActivation(const ffui::HomeData& data,
                                                                 const ffui::HomeLayoutResult& layout,
                                                                 int globalIndex, bool openInNewTab) {
    const ffui::HomeItemLayout* found = FindHomeItem(layout, globalIndex);
    if (found == nullptr || !HomeItemFocusable(data, *found)) return std::nullopt;

    ffui::HomeActivation activation;
    switch (static_cast<ffui::HomeSectionKind>(found->sectionIndex)) {
        case ffui::HomeSectionKind::Pinned:
            activation.kind = ffui::HomeActionKind::Folder;
            activation.value = data.pinned[static_cast<size_t>(found->indexInSection)].path;
            activation.openInNewTab = openInNewTab;
            break;
        case ffui::HomeSectionKind::RecentFolders:
            activation.kind = ffui::HomeActionKind::Folder;
            activation.value = data.recentFolders[static_cast<size_t>(found->indexInSection)];
            activation.openInNewTab = openInNewTab;
            break;
        case ffui::HomeSectionKind::RecentSearches:
            activation.kind = ffui::HomeActionKind::Search;
            activation.value = data.recentSearches[static_cast<size_t>(found->indexInSection)];
            activation.openInNewTab = openInNewTab;
            break;
        case ffui::HomeSectionKind::CommonLocations:
            activation.kind = ffui::HomeActionKind::Folder;
            activation.value = data.knownFolders[static_cast<size_t>(found->indexInSection)].path;
            activation.openInNewTab = openInNewTab;
            break;
        case ffui::HomeSectionKind::QuickActions: {
            // quick-access-home 4.7 / final-product-completion 5.1: New Tab /
            // Search / Analyze Storage / Open Path / Settings map to existing
            // commands only (design D4).
            static const wchar_t* kCommands[] = {
                L"navigation.new-tab", L"search.focus", L"storage.analyze", L"navigation.focus-path",
                L"app.settings"};
            activation.kind = ffui::HomeActionKind::QuickAction;
            activation.value = kCommands[found->indexInSection];
            break;
        }
    }
    // Defensive: a Folder activation must always carry a real path. An empty
    // destination is not "This PC" -- it is malformed state and is rejected.
    if (activation.kind == ffui::HomeActionKind::Folder && activation.value.empty()) {
        return std::nullopt;
    }
    return activation;
}

// quick-access-home hardening (HOME-POST-03): the Clear stop's activation kind.
inline ffui::HomeActionKind HomeActionKindForFocusTarget(ffui::HomeFocusTarget target) {
    switch (target) {
        case ffui::HomeFocusTarget::ClearRecentFolders: return ffui::HomeActionKind::ClearRecentFolders;
        case ffui::HomeFocusTarget::ClearRecentSearches: return ffui::HomeActionKind::ClearRecentSearches;
        case ffui::HomeFocusTarget::None:
        case ffui::HomeFocusTarget::Item:
            break; // non-Clear targets have no Clear activation
    }
    return ffui::HomeActionKind::Folder;
}

} // namespace ffui
