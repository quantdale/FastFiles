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

namespace ffui {

struct TreemapNode {
    std::wstring name;
    std::wstring path;
    bool isDirectory = false;
    uint64_t sizeBytes = 0;
    uint64_t totalSizeBytes = 0;
    bool calculating = false;
    int depth = 0;
    TreemapNode* parent = nullptr;
    std::vector<std::unique_ptr<TreemapNode>> children;
};

struct TreemapRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    TreemapNode* node = nullptr;
};

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
    void Render(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory, D2D1_SIZE_F viewportSize);
    bool HandleNotify(LPARAM lParam);
    bool HandleMouseMove(WPARAM wParam, LPARAM lParam);
    bool HandleLButtonDown(WPARAM wParam, LPARAM lParam);
    void SetEngineActive(bool active);
    void SetDarkTheme(bool dark);
    void OnSnapshotUpdated();
    void EnsureCreated(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory);

private:
    void BuildTree(const std::wstring& path);
    void LayoutTreemap(std::vector<TreemapRect>& rects, float x, float y, float width, float height,
                       const std::vector<TreemapNode*>& nodes, int depth);
    void Squarify(std::vector<TreemapRect>& rects, float x, float y, float width, float height,
                  const std::vector<TreemapNode*>& nodes, int depth);
    TreemapNode* HitTest(float x, float y) const;

    HWND owner_ = nullptr;
    EngineClient* engine_ = nullptr;
    std::function<void(const std::wstring& path)> navigate_;

    std::unique_ptr<TreemapNode> root_;
    std::vector<TreemapRect> layout_;
    std::wstring currentPath_;
    bool visible_ = false;
    bool engineActive_ = false;

    TreemapNode* hoveredNode_ = nullptr;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> backgroundBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> borderBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hoverBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> calculatingBrush_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat_;
    bool resourcesCreated_ = false;
    bool darkTheme_ = false;
};

} // namespace ffui
