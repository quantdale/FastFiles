#pragma once

#include <algorithm>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include <windows.h>

#include "EngineClient.h"
#include "ffprotocol/SnapshotFormat.h"
#include "ffprotocol/UiProtocol.h"

namespace ffui {

constexpr UINT WM_APP_STORAGE_AGGREGATE = WM_APP + 8;

class StorageAnalysis {
public:
    StorageAnalysis() = default;
    ~StorageAnalysis();
    bool Initialize(HWND owner, EngineClient* engine,
                    std::function<void(const std::wstring& path)> navigate,
                    std::function<void()> close);
    void ShowAndFocus(const std::wstring& currentPath, bool engineActive);
    void Hide();
    bool Visible() const { return visible_; }
    void Reposition();
    bool HandleOwnerCommand(WPARAM wParam, LPARAM lParam);
    bool HandleNotify(LPARAM lParam);
    bool HandleTimer(UINT_PTR timerId);
    bool HandleCompletion(LPARAM lParam);
    void SetEngineActive(bool active);
    void OnSnapshotUpdated();

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

    struct PendingRequest {
        uint64_t requestId = 0;
        size_t itemIndex = 0;
    };

    void WorkerMain();
    void RefreshData();
    void RequestAggregateForItem(size_t index);
    void HandleAggregateResult(uint64_t requestId, ffprotocol::FolderAggregateStatus status,
                               uint64_t itemCount, uint64_t totalSizeBytes);
    static std::wstring FormatSize(uint64_t bytes);
    static std::wstring FormatPercent(uint64_t part, uint64_t whole);

    HWND owner_ = nullptr;
    HWND list_ = nullptr;
    HWND status_ = nullptr;
    HWND back_ = nullptr;
    HWND up_ = nullptr;

    EngineClient* engine_ = nullptr;
    std::function<void(const std::wstring& path)> navigate_;
    std::function<void()> close_;

    std::vector<DrillItem> items_;
    std::wstring currentPath_;
    bool visible_ = false;
    bool engineActive_ = false;
    uint64_t generation_ = 0;

    std::thread worker_;
    std::mutex workMutex_;
    std::condition_variable workCv_;
    std::atomic<bool> stopping_{false};
    std::atomic<uint64_t> currentGeneration_{0};

    std::vector<PendingRequest> pending_;
    std::mutex pendingMutex_;
};

} // namespace ffui
