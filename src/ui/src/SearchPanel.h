#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <d2d1.h>
#include <dwrite.h>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <memory>
#include <thread>
#include <vector>
#include <windows.h>
#include <commctrl.h> // ui-v3 search: HIMAGELIST drives the two-line result row height
#include <wrl/client.h>

#include "EngineClient.h"
#include "CommandSystem.h"
#include "ffui/AsyncResultGate.h"
#include "ffui/ShellLayout.h"
#include "ffui/UiaSurface.h" // semantic-uia-core-surfaces 4.x: UIA projection seam
#include "ffsearch/Search.h"
#include "ffsearch/History.h"
#include "Util.h"
#include "UiaNotifications.h" // semantic-uia-core-surfaces 4.4: semantic event sink
#include "UiaProviderBase.h"  // semantic-uia-core-surfaces 1.1: owner-lifetime token

namespace ffui {

constexpr UINT WM_APP_SEARCH_COMPLETE = WM_APP + 6;
constexpr UINT WM_APP_INDEXED_SEARCH_COMPLETE = WM_APP + 13;
constexpr UINT WM_APP_SEARCH_RESULT_ACTIVATE = WM_APP + 53; // stabilize-live-uia-e2e

class SearchPanel : public UiaSurface {
public:
    SearchPanel() = default;
    ~SearchPanel() override;
    bool Initialize(HWND owner, EngineClient* engine,
                    std::function<void(const ffsearch::Candidate&, const ffsearch::PathReconstruction&)> navigate,
                    bool retainHistory);
    // Applies the persisted setting after controls are created.  The setting
    // uses stable semantic names while the combo labels explain degraded
    // coverage, so this translation belongs at the UI seam.
    void SetDefaultScope(const std::wstring& scopeName);
    void ShowAndFocus(const std::wstring& currentPath, bool engineActive,
                      const ShellLayoutRect& surfaceRect);
    void Hide();
    void SetDarkTheme(bool dark);
    bool Visible() const { return visible_; }
    std::vector<SelectionItem> ActiveSelectionItems() const;
    bool ActiveSelectionRect(RECT& screenRect) const;
    void ActivateSelection();
    void Reposition(const ShellLayoutRect& surfaceRect);
    bool HandleOwnerCommand(WPARAM wParam, LPARAM lParam);
    bool HandleNotify(LPARAM lParam);
    bool HandleDrawItem(LPARAM lParam);
    bool HandleTimer(UINT_PTR timerId);
    bool HandleCompletion(LPARAM lParam);
    bool HandleIndexedCompletion(LPARAM lParam);
    // Invalidates the candidate materialization after a new snapshot
    // generation. The next search rebuilds it once on the worker thread.
    void OnSnapshotUpdated();
    // active-path-runtime-resilience 4.2: connection transitions invalidate
    // callbacks from the old engine/source context and re-enter the existing
    // indexed or degraded search path when the new source is ready.
    void OnEngineConnectionState(EngineConnectionState state);
    void SetEngineActive(bool active);
    void ClearHistory();
    // quick-access-home 5.6: most-recent-first queries (the underlying store is
    // dedupe-to-front), truncated to maxCount, for the Home Recent searches
    // section. Reading the store's existing entries honors the retention policy
    // (recorded queries still display even when recording is disabled).
    std::vector<std::wstring> RecentQueries(size_t maxCount) const;
    // quick-access-home 5.6: set the query text and run it through the existing
    // dispatch pipeline. Callers show the panel first (ShowAndFocus) so the
    // results surface; this method stays dumb about layout/visibility.
    void SetQueryAndDispatch(const std::wstring& query);
    // quick-access-home 5.6: invoked after search history is recorded or
    // cleared so the shell can refresh the Home Recent searches section.
    void SetHistoryChangedCallback(std::function<void()> cb);

    // Task 5: custom-painted query edit. The subclassed edit control
    // (SearchEditProc, a free function in the anonymous namespace of
    // SearchPanel.cpp) calls back into the panel through this method.
    void PaintSearchEdit(HWND hwnd);
    // ui-v3 search: custom-painted filter/disclosure bar (disclosure pill,
    // active-filter chips, live status line, searching progress band), the
    // in-field clear-query affordance, and the no-results state panel. All are
    // called from free subclass procs in the .cpp, hence public.
    void PaintFilterBar(HWND hwnd);
    void PaintClearQueryButton(HWND hwnd);
    // ui-v3 search: hover flag for the in-field clear affordance — touched by
    // the file-scope subclass proc, exposed as a tiny seam to keep the member
    // private.
    bool ClearQueryHot() const { return clearQueryHot_; }
    void SetClearQueryHot(bool hot) { clearQueryHot_ = hot; }
    bool ShouldPaintListEmptyState() const;
    void PaintListEmptyState(HWND hwnd);
    // ui-v3 search: hover/click plumbing for the surfaces above plus result-row
    // hover tracking on the native list (presentation only; selection identity
    // stays with the ListView).
    void HandleFilterBarMouseMove(LPARAM lParam);
    void HandleFilterBarMouseLeave();
    void HandleFilterBarClick(LPARAM lParam);
    void HandleListMouseMove(LPARAM lParam);
    void HandleListMouseLeave();
    void SetButtonHot(HWND source, bool hot);
    void ToggleFiltersExpanded();
    void SetFiltersExpanded(bool expanded);
    bool FiltersExpanded() const { return filtersExpanded_; }
    void ClearQueryText();
    // ui-v3 search: the row-height image list is owned by the panel, not the
    // ListView; released on WM_NCDESTROY of the list or panel teardown,
    // whichever comes first. Idempotent.
    void ReleaseListImageList();
    // Focus state for the Fluent query edit, set by SearchEditProc on
    // WM_SETFOCUS/WM_KILLFOCUS and read by PaintSearchEdit to draw the focus
    // treatment. Public because the subclass proc is a free function.
    bool searchEditFocused_ = false;
    // UIA ValuePattern / WM_SETTEXT changes the edit without CBN_EDITCHANGE.
    // Same debounce path as a keystroke; no-op while updatingHistory_.
    void ArmQueryDebounce();

private:
    enum class StatusKind { Info, Searching, NoResults };

    struct Work {
        uint64_t generation = 0;
        uint64_t sourceEpoch = 0;
        std::vector<ffsearch::Candidate> candidates;
        ffsearch::SearchRequest request;
    };
    struct Completion {
        uint64_t generation = 0;
        uint64_t sourceEpoch = 0;
        ffsearch::SearchResponse response;
        std::vector<ffsearch::Candidate> candidates;
    };
    struct IndexedCompletion {
        uint64_t generation = 0;
        uint64_t sourceEpoch = 0;
        ffprotocol::IndexedSearchResponse response;
    };
    struct IndexedCallbackState {
        std::mutex mutex;
        bool alive = true;
    };

    void WorkerMain();
    void InvalidateInFlightWork();
    void Dispatch(bool recordHistory = true, bool forceSnapshot = false);
    void PopulateCandidates(std::vector<ffsearch::Candidate>& candidates);
    void RefreshScopeLabels();
    void UpdateScopeAvailability(bool showFallbackNotice);
    void UpdateStatus(const std::wstring& text, StatusKind kind = StatusKind::Info);
    void LoadHistory();
    void SaveHistory() const;
    void RecordHistory(const std::wstring& query);
    // True while the in-memory history has entries not yet flushed to disk;
    // the flush runs on a coalesced UI timer instead of per keystroke.
    bool historyDirty_ = false;
    static std::filesystem::path HistoryPath();

    // Task 5: owner-draw helpers.
    void DrawSearchResultRow(const DRAWITEMSTRUCT* item);
    void DrawComboItem(const DRAWITEMSTRUCT* item);
    // Owner-draws a push button (sort direction, clear history) from the
    // shared UITheme token set; the label comes from the source window.
    void DrawTextButton(HWND source, const DRAWITEMSTRUCT* item);
    // Owner-draws the Filters disclosure pill (chevron + label) as a real
    // button surface so UIA/keyboard users can reach the collapsed row.
    void DrawFiltersButton(const DRAWITEMSTRUCT* item);
    int SystemIconIndex(const std::wstring& key);

    // ui-v3 search: presentation-state helpers for the query-first layout.
    void EnsureResultFonts();
    void EnsureRowMetrics();
    void UpdateFilterSummary();
    void UpdateClearAffordance();
    std::wstring ScopeDisplayName() const;

    HWND owner_ = nullptr;
    HWND query_ = nullptr;
    HWND queryEdit_ = nullptr;
    HWND scope_ = nullptr;
    HWND sort_ = nullptr;
    HWND list_ = nullptr;
    // ui-v3 search: the old status STATIC was folded into this bar; it hosts
    // the Filters disclosure, active-filter chips, and the live status line.
    HWND filterBar_ = nullptr;
    // ui-v3 search: in-field clear-query affordance (visible while text is
    // present); a subclassed STATIC so no owner WM_COMMAND routing changes.
    // ui-v3 search a11y: the Filters disclosure is a real owner-draw BUTTON
    // (not a painted region of the bar) so it stays UIA-discoverable with an
    // actionable pattern and keyboard-reachable even while the scope/sort row
    // it reveals is hidden by the collapsed state.
    HWND filtersToggle_ = nullptr;
    HWND clearQuery_ = nullptr;
    HWND clearHistory_ = nullptr;
    HWND sortDirection_ = nullptr;
    EngineClient* engine_ = nullptr;
    std::function<void(const ffsearch::Candidate&, const ffsearch::PathReconstruction&)> navigate_;
    std::vector<ffsearch::SearchResult> results_;
    std::vector<ffsearch::Candidate> candidates_;
    std::vector<std::wstring> history_;
    ffsearch::SearchHistory historyStore_;
    std::wstring currentPath_;
    std::wstring currentPrimaryTerm_;
    std::function<void()> historyChanged_;
    bool visible_ = false;
    bool engineActive_ = false;
    bool persistentCoverage_ = false;
    bool retainHistory_ = true;
    bool updatingHistory_ = false;
    bool descending_ = false;
    bool snapshotFallback_ = false;
    // ui-v3 search: filters/sort live behind a compact disclosure row so the
    // permanent chrome stays quiet; chips surface only non-default state.
    bool filtersExpanded_ = false;
    bool clearQueryHot_ = false;      // hover on the in-field clear affordance
    bool filtersHot_ = false;         // hover on the Filters disclosure button
    bool directionHot_ = false;       // hover tracking for owner-draw buttons
    bool clearHistoryHot_ = false;
    int hoveredResultRow_ = -1;       // presentation-only row hover on the list
    int hoveredChip_ = -1;            // filter-bar hit: 1 scope, 2 sort
    float progressPhase_ = 0.0f;      // indeterminate band phase while searching
    ShellLayoutRect lastSurfaceRect_{};

    // ui-v3 search: cached chip model, rebuilt only when an input changes
    // (scope/sort selection, direction, engine state) — never per paint.
    bool scopeChipActive_ = false;
    bool sortChipActive_ = false;
    std::wstring scopeChipText_;
    std::wstring sortChipText_;
    // Chip geometry from the last filter-bar paint; used for hit-testing
    // (same paint-driven pattern as the sidebar rows). The Filters disclosure
    // itself is a real child button placed by Reposition, not a painted chip.
    RECT scopeChipRect_{};
    RECT sortChipRect_{};

    // ui-v3 search: DPI-aware fonts for the two-line result rows and the
    // filter bar; recreated when the system DPI scale changes.
    HFONT resultNameFont_ = nullptr;
    HFONT resultMatchFont_ = nullptr;
    HFONT resultCaptionFont_ = nullptr;
    float resultFontScale_ = -1.0f;
    // Row-height driver: a ListView sizes report rows to its small image list,
    // so a one-entry image list at the two-line height sets the row metrics.
    HIMAGELIST resultListImages_ = nullptr;
    int resultListRowHeightPx_ = 0;

    // ui-v3 search: the live status line text (the old STATIC is gone);
    // rendered by PaintFilterBar together with the chips.
    std::wstring statusText_;
    // Coalesces snapshot-generation refreshes: when generations publish faster
    // than the refresh interval, only a dirty flag is set and the re-dispatch
    // happens on the next debounce/visibility change.
    bool snapshotRefreshPending_ = false;
    std::chrono::steady_clock::time_point lastSnapshotRefresh_{};
    uint64_t generation_ = 0;
    uint64_t sourceEpoch_ = 1;
    AsyncResultGate resultGate_;
    std::atomic<uint64_t> indexedRequestId_{0};

    std::thread worker_;
    std::mutex workMutex_;
    std::condition_variable workCv_;
    std::optional<Work> pendingWork_;
    std::atomic<uint64_t> currentGeneration_{0};
    std::atomic<uint64_t> currentSourceEpoch_{1};
    std::atomic<bool> stopping_{false};
    std::shared_ptr<IndexedCallbackState> indexedCallbackState_ =
        std::make_shared<IndexedCallbackState>();

    // Candidate construction is the expensive snapshot-to-search projection.
    // Keep one immutable worker-built copy per published snapshot generation
    // so typing successive queries does not repeatedly parse and materialize
    // the entire mapping.
    std::mutex candidateCacheMutex_;
    std::shared_ptr<const std::vector<ffsearch::Candidate>> candidateCache_;
    std::atomic<uint64_t> candidateCacheEpoch_{0};

    // Task 5: cached Direct2D/DirectWrite resources for the custom-painted query
    // edit. Created once and reused across paints; torn down and recreated on
    // theme change or DPI change (see PaintSearchEdit).
    Microsoft::WRL::ComPtr<ID2D1Factory> searchFactory_;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> searchTarget_;
    Microsoft::WRL::ComPtr<IDWriteFactory> searchWriteFactory_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> searchFormat_;
    // ui-v3 search: optional leading search glyph format ("Segoe MDL2 Assets");
    // the field renders without the glyph when the family is unavailable.
    Microsoft::WRL::ComPtr<IDWriteTextFormat> searchGlyphFormat_;
    bool searchResourcesReady_ = false;
    float lastSearchDpi_ = -1.0f;

    // Task 5: system image-list (SHIL_SMALL) index per file type key (".txt",
    // "folder"), resolved lazily via SHGetFileInfoW / SHGetStockIconInfo.
    std::map<std::wstring, int> iconIndexCache_;
    StatusKind statusKind_ = StatusKind::Info;
    // Parse diagnostics from the current query (invalid filters / unknown
    // keys), prefixed to the next status update so the user sees that part of
    // the query was ignored.
    std::wstring statusWarning_;

    // semantic-uia-core-surfaces 4.x: UIA surface implementation for the
    // search result list. The provider tree queries and mutates ONLY through
    // these methods, which project the real result model and native list
    // state (design D15).
    size_t UiaSearchResultCount() const override;
    UiaSearchResultInfo UiaSearchResultInfoOf(size_t index) const override;
    bool UiaSearchResultRect(size_t index, RECT& outScreen) const override;
    bool UiaSearchResultOffscreen(size_t index) const override;
    bool UiaSearchVisible() const override;
    bool UiaSearchHasKeyboardFocus() const override;
    HWND UiaSearchHwnd() const override;
    void UiaSearchSelectItem(size_t index) override;
    void UiaSearchAddToSelection(size_t index) override;
    void UiaSearchRemoveFromSelection(size_t index) override;
    void UiaSearchSetFocusItem(size_t index) override;
    void UiaSearchActivateItem(size_t index) override;
    void UiaSearchScrollIntoView(size_t index) override;

    // semantic-uia-core-surfaces 4.2/4.4: provider plumbing. The list HWND is
    // subclassed (SearchListProc) so WM_GETOBJECT returns the fragment root;
    // everything else falls through to the native control. Public because the
    // subclass proc and the shell own the lifecycle.
public:
    // stabilize-live-uia-e2e: UIA provider calls can execute on a
    // UIAutomationCore worker thread rather than the UI thread that owns the
    // search list. Navigation mutates UI state (workspace, columns, chrome
    // tab rebuild) and MUST run on the UI thread: windows created on a
    // foreign thread (tab buttons) can never be destroyed by the UI thread
    // (DestroyWindow fails cross-thread), leaving orphaned duplicate tabs
    // that stall UIA enumeration. UiaSearchActivateItem therefore posts
    // WM_APP_SEARCH_RESULT_ACTIVATE to the owner when invoked off-thread and
    // WindowShell runs ActivateResultItem on the UI thread. The inline path
    // is unchanged when the call already arrives on the UI thread.
    static bool SearchActivationNeedsUiThreadMarshal(DWORD currentThreadId, DWORD listWindowThreadId) {
        return listWindowThreadId != 0 && currentThreadId != listWindowThreadId;
    }
    // Runs the real search-to-navigation activation on the calling (UI)
    // thread; re-validates the index against the current result set.
    void ActivateResultItem(size_t index);

    void SetUiaEventSink(UiaEventSink sink, void* context);
    Microsoft::WRL::ComPtr<IRawElementProviderFragmentRoot> UiaRootProvider(HWND listHwnd);
    // ui-v3 search a11y hardening: property/pattern fixup for the panel's
    // owner-draw push buttons (Filters disclosure, sort direction, clear
    // history). Under a custom fragment root these HWNDs fall through to the
    // default MSAA proxy, which reports ControlType=Pane with no Invoke
    // pattern and IsKeyboardFocusable=false -- contradicting what they are.
    // The returned simple provider keeps the default HWND host provider as
    // its fallback and overrides exactly ControlType/Button semantics,
    // Invoke, and keyboard-focusability.
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> UiaButtonFixupProvider(HWND button);
    // ui-v3 search a11y hardening: explicit Tab traversal (the main loop has
    // no IsDialogMessage, so nothing else moves focus between these controls).
    // Order comes from the ffui::SearchTabOrder headless seam.
    void CycleSearchFocus(HWND from, bool forward);
    void ReleaseUiaProvider();

private:
    void NotifyUia(const UiaEventNotification& notification);

    UiaEventRouter uiaEvents_;
    UiaOwnerStatePtr uiaState_;
    Microsoft::WRL::ComPtr<IRawElementProviderFragmentRoot> uiaProvider_;
};

} // namespace ffui
