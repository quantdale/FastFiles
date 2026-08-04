#pragma once

#include <d2d1_1.h>
#include <dwrite.h>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

#include "EngineClient.h"
#include "ffprotocol/SnapshotFormat.h"
#include "ffui/TreemapLayout.h"
#include "UITheme.h"
#include "UiStyle.h"

namespace ffui {

class TreemapView {
public:
    TreemapView() = default;
    ~TreemapView();

    bool Initialize(HWND owner, EngineClient* engine,
                    std::function<void(const std::wstring& path)> navigate);
    void ShowAndFocus(const std::wstring& currentPath);
    void Hide();
    bool Visible() const { return visible_; }
    void Reposition();
    void Render(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory, D2D1_SIZE_F viewportSize, float offsetX, float offsetY);
    bool HandleNotify(LPARAM lParam);
    bool HandleMouseMove(WPARAM wParam, LPARAM lParam);
    bool HandleLButtonDown(WPARAM wParam, LPARAM lParam);
    void SetDarkTheme(bool dark);
    void OnSnapshotUpdated();
    void EnsureCreated(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory);

private:
    void BuildTree(const std::wstring& path);

    HWND owner_ = nullptr;
    EngineClient* engine_ = nullptr;
    std::function<void(const std::wstring& path)> navigate_;

    std::unique_ptr<TreemapNode> root_;
    std::vector<TreemapRect> layout_;
    std::wstring currentPath_;
    bool visible_ = false;

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
