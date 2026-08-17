#pragma once

#include <d2d1_1.h>
#include <dwrite.h>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

#include "EngineClient.h"
#include "ffprotocol/SnapshotFormat.h"
#include "ffui/TreemapLayout.h"
#include "ffui/TreemapSemantics.h"
#include "UITheme.h"
#include "UiStyle.h"

namespace ffui {

class CategoryEngine;

constexpr UINT WM_APP_TREEMAP_AGGREGATE = WM_APP + 12;

struct TreemapAggregatePayload {
    uint64_t generation = 0;
    std::wstring path;
    ffprotocol::FolderAggregateStatus status = ffprotocol::FolderAggregateStatus::NotFound;
    uint64_t totalSizeBytes = 0;
    // allocated-size-storage-accuracy 7.2: allocated subtotal + completeness
    // pair from the engine's aggregate response.
    uint64_t totalAllocatedSizeBytes = 0;
    uint64_t allocatedBytesKnownCount = 0;
    uint64_t allocatedBytesTotalCount = 0;
};

class TreemapView {
public:
    using NavigateHandler = std::function<void(const std::wstring& path, bool isDirectory)>;
    using UiaEventHandler = std::function<void(const UiaEventNotification&)>;
    TreemapView() = default;
    ~TreemapView();

    bool Initialize(HWND owner, EngineClient* engine,
                    NavigateHandler navigate);
    void SetUiaEventHandler(UiaEventHandler handler);

    // UI-thread mutations (all acquire uiaMutex_; semantic events are pushed
    // through the handler AFTER the lock is released so a provider callback
    // that re-enters the view can never deadlock).
    void ShowAndFocus(const std::wstring& currentPath);
    void Hide();
    bool Visible() const { return visible_.load(std::memory_order_acquire); }
    void Reposition();
    void Render(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory, D2D1_SIZE_F viewportSize, float offsetX, float offsetY);
    bool HandleNotify(LPARAM lParam);
    bool HandleAggregateCompletion(LPARAM lParam);
    bool HandleMouseMove(WPARAM wParam, LPARAM lParam);
    bool HandleLButtonDown(WPARAM wParam, LPARAM lParam);
    void SetDarkTheme(bool dark);
    void SetCategoryFilter(const CategoryEngine* categoryEngine, const std::wstring& categoryId);
    void OnSnapshotUpdated();
    void EnsureCreated(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory);
    void SetVolumeInfo(const TreemapVolumeInfo& volume);

    // semantic-treemap-uia 6.x: the bounded, thread-safe semantic projection.
    // UIAutomationCore worker threads may call these; every accessor takes a
    // bounded lock over plain values and performs no filesystem/engine work.
    bool UiaActive() const { return visible_.load(std::memory_order_acquire); }
    uint64_t UiaGeneration() const { return buildGeneration_.load(std::memory_order_acquire); }
    std::wstring UiaScopePath() const;
    uint64_t UiaScopeHash() const;
    int UiaTileCount() const;
    TreemapUiaItemInfo UiaTileInfo(int index) const;
    int UiaTileIndex(uint64_t identityHash) const;
    bool UiaFindTile(uint64_t identityHash, TreemapUiaItemInfo& out) const;
    TreemapUiaItemInfo UiaTileHitTest(float normalizedX, float normalizedY) const;

private:
    void BuildTree(const std::wstring& path);
    void RebuildLayout();
    static TreemapNode* FindNode(TreemapNode* root, const std::wstring& path);
    static uint64_t RecomputeTotals(TreemapNode& node);
    TreemapUiaItemInfo BuildTileInfoLocked(const TreemapNode& node, const TreemapRect& rect) const;
    void EmitUiaEvent(const UiaEventNotification& notification);

    HWND owner_ = nullptr;
    EngineClient* engine_ = nullptr;
    NavigateHandler navigate_;
    UiaEventHandler uiaEventHandler_;

    // Guards every read/write of the renderable state below. UIAutomationCore
    // worker threads read the semantic projection under this lock; the UI
    // thread mutates under it too. Events are always emitted after release.
    mutable std::recursive_mutex uiaMutex_;

    std::unique_ptr<TreemapNode> root_;
    std::vector<TreemapRect> layout_;
    // Layout index by stable identity hash, rebuilt with every layout. The
    // provider resolves tiles in O(1) via this map instead of a linear walk
    // (semantic-treemap-uia 16.x).
    std::unordered_map<uint64_t, size_t> layoutIndexByIdentity_;
    // Caller-owned home of the synthetic "Other small items" catch-all node
    // created by the last RebuildLayout (semantic-treemap-uia 5.x). Cleared on
    // every rebuild; survives until then so hover/projection can read it.
    std::vector<std::unique_ptr<TreemapNode>> syntheticNodes_;
    std::wstring currentPath_;
    const CategoryEngine* categoryEngine_ = nullptr;
    std::wstring categoryId_;
    std::atomic<bool> visible_{false};
    std::atomic<uint64_t> buildGeneration_{0};
    TreemapVolumeInfo volumeInfo_;
    std::shared_ptr<std::atomic<bool>> lifetime_ = std::make_shared<std::atomic<bool>>(true);

    // The treemap is drawn under the shell's Translate(sidebar, chrome)
    // transform and scaled by viewport/100; hit-testing stores the same
    // offset/scale so client coordinates map back to normalized 0..100 space.
    D2D1_SIZE_F viewportSize_{};
    float offsetX_ = 0.0f;
    float offsetY_ = 0.0f;

    TreemapNode* hoveredNode_ = nullptr;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> backgroundBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> borderBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hoverBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hoverOverlayBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> calculatingBrush_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat_;
    bool resourcesCreated_ = false;
    bool darkTheme_ = false;
};

} // namespace ffui