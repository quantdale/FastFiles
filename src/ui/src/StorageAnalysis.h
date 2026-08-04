#pragma once

#include <algorithm>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include <windows.h>

#include "CategoryEngine.h"
#include "EngineClient.h"
#include "ffprotocol/SnapshotFormat.h"
#include "ffprotocol/UiProtocol.h"
#include "TreemapView.h"
#include "Util.h"

namespace ffui {

constexpr UINT WM_APP_STORAGE_AGGREGATE = WM_APP + 10;

class StorageAnalysis {
public:
    StorageAnalysis() = default;
    ~StorageAnalysis();
    bool Initialize(HWND owner, EngineClient* engine,
                    std::function<void(const std::wstring& path)> navigate,
                    std::function<void()> close,
                    std::function<void(const std::wstring& commandId, const std::vector<std::wstring>& paths)> invokeCommand);
    void ShowAndFocus(const std::wstring& currentPath, bool engineActive);
    void Hide();
    bool Visible() const { return visible_; }
    void Reposition();
    bool HandleOwnerCommand(WPARAM wParam, LPARAM lParam);
    bool HandleNotify(LPARAM lParam);
    bool HandleContextMenu(WPARAM wParam, LPARAM lParam);
    bool HandleTimer(UINT_PTR timerId);
    bool HandleCompletion(LPARAM lParam);
    bool HandleMouseMove(WPARAM wParam, LPARAM lParam);
    bool HandleLButtonDown(WPARAM wParam, LPARAM lParam);
    void SetEngineActive(bool active);
    void SetDarkTheme(bool dark);
    void OnSnapshotUpdated();
    void RenderTreemap(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory, D2D1_SIZE_F viewportSize, float offsetX, float offsetY);

    enum class ViewMode { Overview, DrillDown, LargestFolders, LargestFiles, ByCategory, Treemap };
    void SetViewMode(ViewMode mode);
    ViewMode GetViewMode() const { return viewMode_; }

private:
    struct DrillItem {
        std::wstring name;
        bool isDirectory = false;
        uint64_t sizeBytes = 0;
        uint64_t totalSizeBytes = 0; // subtree aggregate when known
        uint32_t attributes = 0;
        uint64_t creationTime = 0;
        uint64_t lastModifiedTime = 0;
        int64_t volumeRowId = 0;
        uint64_t fileIdLow = 0;
        uint64_t fileIdHigh = 0;
        uint64_t parentFrnLow = 0;
        uint64_t parentFrnHigh = 0;
        bool calculating = false;
        uint64_t requestId = 0;
    };

    // storage-analysis 2.1-2.4: one row of the storage overview.
    struct VolumeItem {
        std::wstring rootPath;   // "C:\"
        bool unavailable = false; // volume vanished; figures below are stale
        bool fullyIndexed = false;
        uint64_t totalBytes = 0;
        uint64_t freeBytes = 0;
        uint64_t usedBytes = 0;
    };

    struct PendingRequest {
        uint64_t requestId = 0;
        size_t itemIndex = 0;
    };

    void RefreshData();
    void RefreshOverview();
    void RequestAggregateForItem(size_t index);
    void HandleAggregateResult(uint64_t requestId, ffprotocol::FolderAggregateStatus status,
                               uint64_t itemCount, uint64_t totalSizeBytes);
    void SortItems(int column, bool ascending);
    void PopulateCategoryFilter();
    void ApplyCategoryFilter();

    HWND owner_ = nullptr;
    HWND list_ = nullptr;
    HWND overview_ = nullptr;
    HWND categoryFilter_ = nullptr; // storage-analysis 5.4: category filter combo
    HWND status_ = nullptr;
    HWND back_ = nullptr;
    HWND up_ = nullptr;
    HWND overviewButton_ = nullptr;
    HWND drillDown_ = nullptr;
    HWND largestFolders_ = nullptr;
    HWND largestFiles_ = nullptr;
    HWND byCategory_ = nullptr;
    HWND treemap_ = nullptr;

    EngineClient* engine_ = nullptr;
    std::function<void(const std::wstring& path)> navigate_;
    std::function<void()> close_;
    std::function<void(const std::wstring& commandId, const std::vector<std::wstring>& paths)> invokeCommand_;
    CategoryEngine categoryEngine_;
    TreemapView treemapView_;

    std::vector<DrillItem> items_;
    std::vector<VolumeItem> volumes_;    // storage-overview rows
    std::wstring currentPath_;
    bool visible_ = false;
    bool engineActive_ = false;
    bool darkTheme_ = false;
    ViewMode viewMode_ = ViewMode::Overview;

    int sortColumn_ = 3; // default: sort by Subtree Size descending
    bool sortAscending_ = false;

    std::vector<PendingRequest> pending_;
    std::mutex pendingMutex_;
};

} // namespace ffui
