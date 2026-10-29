#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>
#include <windows.h>

#include "CategoryEngine.h"
#include "CommandSystem.h"
#include "EngineClient.h"
#include "ffui/ShellLayout.h"
#include "ffui/StorageVisibility.h"
#include "ffui/StoragePercentage.h"
#include "ffui/StorageRanking.h"
#include "ffui/TreemapSemantics.h"
#include "ffprotocol/SnapshotFormat.h"
#include "ffprotocol/UiProtocol.h"
#include "TreemapView.h"
#include "UiaProviderBase.h"
#include "SearchUiaProvider.h"
#include "Util.h"

namespace ffui {

constexpr UINT WM_APP_STORAGE_AGGREGATE = WM_APP + 10;
constexpr UINT WM_APP_STORAGE_RANKING = WM_APP + 55;

// semantic-treemap-uia 11.x: value-only payload for marshaling a UIA Invoke to
// the owning UI thread. Never carries pointers: the UI thread re-resolves the
// stable identity against CURRENT state and drops it if the tile changed.
constexpr UINT WM_APP_TREEMAP_NAVIGATE = WM_APP + 54;

struct TreemapNavigatePayload {
    uint64_t generation = 0;
    uint64_t scopeHash = 0;
    uint64_t identityHash = 0;
    std::wstring fullPath;
    bool isDirectory = false;
};

class StorageAnalysis : public TreemapUiaSurface, public UiaSurface {
public:
    StorageAnalysis() = default;
    ~StorageAnalysis();
    bool Initialize(HWND owner, EngineClient* engine,
                    std::function<void(const std::wstring& path, bool isDirectory)> navigate,
                    std::function<void()> close,
                    std::function<void(const std::wstring& commandId, const std::vector<std::wstring>& paths)> invokeCommand);
    // Applies persisted category definitions to the category view and its
    // filter. Safe before or after controls are created.
    void SetCategories(const std::vector<std::pair<std::wstring, std::wstring>>& categories);
    void ShowAndFocus(const std::wstring& currentPath, bool engineActive,
                      const ShellLayoutRect& surfaceRect);
    void Hide();
    bool Visible() const { return visible_; }
    std::vector<SelectionItem> ActiveSelectionItems() const;
    void Reposition(const ShellLayoutRect& surfaceRect);
    bool HandleOwnerCommand(WPARAM wParam, LPARAM lParam);
    bool HandleDrawItem(LPARAM lParam);
    bool HandleNotify(LPARAM lParam);
    bool HandleContextMenu(WPARAM wParam, LPARAM lParam);
    bool HandleCompletion(LPARAM lParam);
    bool HandleRankingCompletion(LPARAM lParam);
    void StopRanking();
    bool HandleTreemapCompletion(LPARAM lParam);
    bool HandleTreemapUiaNavigate(LPARAM lParam);
    bool HandleMouseMove(WPARAM wParam, LPARAM lParam);
    bool HandleLButtonDown(WPARAM wParam, LPARAM lParam);
    void SetEngineActive(bool active);
    void SetDarkTheme(bool dark);
    void OnSnapshotUpdated();
    void SetTreemapGeometry(const UiaViewGeometry& geometry);
    UiaViewGeometry TreemapGeometry() const override;
    void RenderTreemap(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory,
                       const ShellLayoutRect& surfaceRect);

    // semantic-treemap-uia 30.x: the provider controller subscribes here;
    // notifications are pushed after the view released its locks.
    void SetUiaEventSink(UiaEventSink sink, void* context) { uiaEvents_.Set(sink, context); }

    using ViewMode = StorageViewMode;
    void SetViewMode(ViewMode mode);
    ViewMode GetViewMode() const { return viewMode_; }

    // --- TreemapUiaSurface (semantic-treemap-uia 6.x): the provider consumes
    // the same bounded semantic projection the renderer and hit-tester use. ---
    bool TreemapActive() const override { return treemapView_.UiaActive(); }
    uint64_t TreemapGeneration() const override { return treemapView_.UiaGeneration(); }
    uint64_t TreemapScopeHash() const override { return treemapView_.UiaScopeHash(); }
    std::wstring TreemapScopePath() const override { return treemapView_.UiaScopePath(); }
    int TreemapTileCount() const override { return treemapView_.UiaTileCount(); }
    TreemapUiaItemInfo TreemapTileInfo(int index) const override { return treemapView_.UiaTileInfo(index); }
    int TreemapTileIndex(uint64_t identityHash) const override { return treemapView_.UiaTileIndex(identityHash); }
    bool TreemapFindTile(uint64_t identityHash, TreemapUiaItemInfo& out) const override {
        return treemapView_.UiaFindTile(identityHash, out);
    }
    TreemapUiaItemInfo TreemapTileHitTest(float normalizedX, float normalizedY) const override {
        return treemapView_.UiaTileHitTest(normalizedX, normalizedY);
    }
    void TreemapActivateItem(const TreemapUiaItemInfo& item) override;
    UiaEventRouter* TreemapEvents() override { return &uiaEvents_; }

    // The real owner-data report list uses the same bounded semantic list
    // provider as Search; this is its native model/interaction adapter.
    size_t UiaSearchResultCount() const override;
    UiaSearchResultInfo UiaSearchResultInfoOf(size_t index) const override;
    bool UiaSearchResultRect(size_t index, RECT& outScreen) const override;
    bool UiaSearchResultOffscreen(size_t index) const override;
    bool UiaSearchVisible() const override;
    bool UiaSearchHasKeyboardFocus() const override;
    HWND UiaSearchHwnd() const override { return list_; }
    void UiaListSetFocus() override;
    void UiaSearchSelectItem(size_t index) override;
    void UiaSearchAddToSelection(size_t index) override { UiaSearchSelectItem(index); }
    void UiaSearchRemoveFromSelection(size_t index) override;
    void UiaSearchSetFocusItem(size_t index) override;
    void UiaSearchActivateItem(size_t index) override;
    void UiaSearchScrollIntoView(size_t index) override;
    bool UiaListDispatchAction(size_t index, uint64_t expectedIdentity, unsigned action) override;
    bool UiaListResultRect(size_t index, uint64_t identity, RECT& rect) const override;
    bool UiaListResultOffscreen(size_t index, uint64_t identity) const override;
    size_t UiaListHeaderCount() const override;
    std::wstring UiaListHeaderName(size_t index) const override;
    bool UiaListHeaderRect(size_t index, RECT& outScreen) const override;
    void UiaListInvokeHeader(size_t index) override;
    uint64_t UiaListHeaderIdentity(size_t index) const override;
    bool UiaListDispatchHeader(size_t index, uint64_t identity) override;

private:
    friend class StorageRadioProvider;
    struct ListQuery;
    struct ListAction;
    static LRESULT CALLBACK ListSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                             UINT_PTR subclassId, DWORD_PTR refData);
    void ReadListQuery(ListQuery& query) const;
    bool DispatchListAction(size_t index, unsigned action, uint64_t expectedIdentity);
    bool ApplyListAction(const ListAction& action);
    void ActivateListItem(size_t index);
    void NotifyListChanged();
    Microsoft::WRL::ComPtr<SearchUiaProvider> listProvider_;
    UiaOwnerStatePtr listUiaState_;
    UiaEventRouter listEvents_;
    bool listNotificationPending_ = false;

    static LRESULT CALLBACK TabSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                            UINT_PTR subclassId, DWORD_PTR refData);
    static LRESULT CALLBACK StorageButtonSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                                      UINT_PTR subclassId, DWORD_PTR refData);
    static LRESULT CALLBACK InactiveSelectionSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                                         UINT_PTR subclassId, DWORD_PTR refData);
    static LRESULT CALLBACK RadioSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                              UINT_PTR subclassId, DWORD_PTR refData);
    static LRESULT CALLBACK StatusSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                               UINT_PTR subclassId, DWORD_PTR refData);
    bool SelectRadioFromUia(HWND radio);
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> UiaRadioFixupProvider(HWND radio);

    struct DrillItem {
        std::wstring name;
        std::wstring folder; // original source folder, including recursive descendants
        std::wstring categoryId; // nonempty only for synthetic category summaries
        bool isDirectory = false;
        uint64_t sizeBytes = 0;
        uint64_t totalSizeBytes = 0; // subtree aggregate when known
        // allocated-size-storage-accuracy 7.1: the item's own size-on-disk
        // (files) plus the subtree's allocated aggregate with its explicit
        // completeness pair (known/total counts) for directories.
        uint64_t allocatedSizeBytes = 0;
        bool allocatedSizeKnown = false;
        uint64_t totalAllocatedSizeBytes = 0;
        uint64_t allocatedKnownCount = 0;
        uint64_t allocatedTotalCount = 0;
        uint32_t attributes = 0;
        uint64_t creationTime = 0;
        uint64_t lastModifiedTime = 0;
        int64_t volumeRowId = 0;
        uint64_t fileIdLow = 0;
        uint64_t fileIdHigh = 0;
        uint64_t parentFrnLow = 0;
        uint64_t parentFrnHigh = 0;
        bool calculating = false;
        bool aggregateFailed = false;
        uint64_t requestId = 0;
    };

    // storage-analysis 2.1-2.4: one row of the storage overview.
    struct VolumeItem {
        std::wstring rootPath;   // "C:\"
        bool unavailable = false; // volume vanished; figures below are stale
        bool rootAvailable = false;
        uint64_t totalBytes = 0;
        uint64_t freeBytes = 0;
        uint64_t usedBytes = 0;
    };

    struct PendingRequest {
        uint64_t requestId = 0;
        size_t itemIndex = 0;
    };

    void RefreshData(bool snapshotRefresh = false);
    bool RecursiveRanking() const { return viewMode_ == ViewMode::LargestFiles && activeCategoryId_.empty(); }
    void RefreshRanking(bool retainDisplay);
    void CancelRanking();
    void QueueSnapshotRanking(uint64_t generation, StorageRankingScope scope);
    void RankingWorkerMain();
    void ConfigureListColumns();
    struct RankingCallbackState {
        std::mutex mutex;
        bool alive = true;
        HWND owner = nullptr;
    };
    struct RankingWork {
        uint64_t generation = 0;
        StorageRankingScope scope;
    };
    struct RankingCompletion {
        uint64_t generation = 0;
        StorageRankingScope scope;
        bool indexed = false;
        bool available = false;
        bool truncated = false;
        std::vector<ffsearch::Candidate> candidates;
    };
    std::shared_ptr<RankingCallbackState> rankingCallbackState_;
    std::atomic<uint64_t> rankingGeneration_{0};
    uint64_t indexedRankingRequestId_ = 0;
    std::atomic<bool> rankingStopping_{false};
    std::mutex rankingWorkMutex_;
    std::condition_variable rankingWorkCv_;
    std::optional<RankingWork> pendingRanking_;
    std::thread rankingWorker_;
    void RefreshOverview();
    void ApplyModeVisibility();
    void RequestAggregateForItem(size_t index);
    void RequestAggregateForParent(const ffprotocol::SnapshotDirectory& directory);
    std::pair<uint64_t, bool> CurrentParentTotals() const;
    void HandleAggregateResult(uint64_t requestId, ffprotocol::FolderAggregateStatus status,
                               uint64_t itemCount, uint64_t totalSizeBytes,
                               uint64_t totalAllocatedSizeBytes,
                               uint64_t allocatedBytesKnownCount,
                               uint64_t allocatedBytesTotalCount);
    void SortItems(int column, bool ascending);
    std::optional<DrillItem> CaptureSelectedItem() const;
    void RestoreSelectedItem(const std::optional<DrillItem>& selected);
    void PopulateCategoryFilter();
    void UpdateCategoryFilterFromControl();
    void ApplyCategoryFilter();
    void UpdateVolumeUsedBytes();
    void UpdateNavigationButtonState();
    // semantic-treemap-uia 11/28: the single real navigation mutation shared
    // by the mouse-click callback and the UI-thread side of a UIA Invoke.
    void NavigateFromTreemap(const std::wstring& path, bool isDirectory);

    HWND owner_ = nullptr;
    HWND list_ = nullptr;
    HWND overview_ = nullptr;
    HWND categoryFilter_ = nullptr; // storage-analysis 5.4: category filter combo
    HWND status_ = nullptr;
    HWND modeLabel_ = nullptr;
    HWND modeSelector_ = nullptr;
    bool compactModes_ = false;
    HWND back_ = nullptr;
    HWND up_ = nullptr;
    HWND overviewButton_ = nullptr;
    HWND drillDown_ = nullptr;
    HWND largestFolders_ = nullptr;
    HWND largestFiles_ = nullptr;
    HWND byCategory_ = nullptr;
    HWND treemap_ = nullptr;

    EngineClient* engine_ = nullptr;
    std::function<void(const std::wstring& path, bool isDirectory)> navigate_;
    std::function<void()> close_;
    std::function<void(const std::wstring& commandId, const std::vector<std::wstring>& paths)> invokeCommand_;
    CategoryEngine categoryEngine_;
    TreemapView treemapView_;
    std::vector<std::wstring> categoryFilterIds_;
    std::wstring activeCategoryId_; // empty means All categories

    std::vector<DrillItem> items_;
    std::optional<DrillItem> rankingSelected_;
    std::optional<StorageRankingDisplayIdentity> rankingDisplay_;
    uint64_t rankingSourceEpoch_ = 1;
    std::wstring rankingSelectionScope_;
    std::optional<bool> recursiveColumns_;
    float columnsDpi_ = 0;
    int columnsViewportWidth_ = 0;
    std::vector<VolumeItem> volumes_;    // storage-overview rows
    std::wstring currentPath_;
    std::wstring displayedPath_;
    bool visible_ = false;
    ShellLayoutRect lastSurfaceRect_;
    bool engineActive_ = false;
    bool darkTheme_ = false;
    ViewMode viewMode_ = ViewMode::Overview;

    // semantic-treemap-uia 22.x: percentage-of-volume denominator -- the
    // OS-reported used bytes of the volume hosting the current scope, from the
    // same GetDiskFreeSpaceExW ground truth the storage overview uses.
    uint64_t volumeUsedBytes_ = 0;
    bool volumeUsedKnown_ = false;

    mutable std::mutex treemapGeometryMutex_;
    UiaViewGeometry treemapGeometry_;

    // semantic-treemap-uia 30.x: semantic events pushed after locks are
    // released; the provider controller registers itself via SetUiaEventSink.
    UiaEventRouter uiaEvents_;
    UiaOwnerStatePtr uiaState_;

    int sortColumn_ = 3; // default: sort by Subtree Size descending
    bool sortAscending_ = false;

    std::vector<PendingRequest> pending_;
    std::mutex pendingMutex_;
    uint64_t nextRequestId_ = 1;
    uint64_t parentAggregateTotal_ = 0;
    bool parentAggregateKnown_ = false;
    bool parentAggregatePending_ = false;
    uint64_t parentRequestId_ = 0;
};

} // namespace ffui
