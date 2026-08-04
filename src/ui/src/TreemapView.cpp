#include "TreemapView.h"
#include "UITheme.h"
#include "UiStyle.h"

#include <algorithm>
#include <cmath>
#include <shlwapi.h>
#include <windowsx.h>

#pragma comment(lib, "shlwapi.lib")

namespace ffui {
namespace {

constexpr float kBorderWidth = 1.0f;
constexpr float kHoverRingAlpha = 0.5f;  // subtle accent ring on the hovered tile
constexpr int kPadding = 2;

} // namespace

TreemapView::~TreemapView() {
    Hide();
}

bool TreemapView::Initialize(HWND owner, EngineClient* engine,
                             std::function<void(const std::wstring& path)> navigate) {
    owner_ = owner;
    engine_ = engine;
    navigate_ = std::move(navigate);
    return true;
}

void TreemapView::ShowAndFocus(const std::wstring& currentPath) {
    currentPath_ = currentPath;
    visible_ = true;
    BuildTree(currentPath);
    Reposition();
}

void TreemapView::Hide() {
    if (!visible_) return;
    visible_ = false;
    root_.reset();
    layout_.clear();
    hoveredNode_ = nullptr;
}

void TreemapView::Reposition() {
    // Treemap renders in the full viewport area; no HWND repositioning needed
}

void TreemapView::OnSnapshotUpdated() {
    if (!visible_) return;
    BuildTree(currentPath_);
}

void TreemapView::EnsureCreated(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory) {
    if (resourcesCreated_) return;
    const ffui::UiTheme theme = ffui::GetUiTheme(darkTheme_);
    context->CreateSolidColorBrush(theme.background, &backgroundBrush_);
    context->CreateSolidColorBrush(theme.text, &textBrush_);
    context->CreateSolidColorBrush(theme.border, &borderBrush_);
    context->CreateSolidColorBrush(theme.hoverOverlay, &hoverOverlayBrush_);
    context->CreateSolidColorBrush(theme.treemapCalculating, &calculatingBrush_);
    dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                     DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                     12.0f, L"en-us", &textFormat_);
    textFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    resourcesCreated_ = true;
}

void TreemapView::SetDarkTheme(bool dark) {
    darkTheme_ = dark;
    resourcesCreated_ = false;
    backgroundBrush_.Reset();
    textBrush_.Reset();
    borderBrush_.Reset();
    hoverBrush_.Reset();
    hoverOverlayBrush_.Reset();
    calculatingBrush_.Reset();
}

void TreemapView::BuildTree(const std::wstring& path) {
    root_ = std::make_unique<TreemapNode>();
    root_->name = path.empty() ? L"Computer" : path;
    root_->path = path;
    root_->isDirectory = true;
    root_->totalSizeBytes = 0;

    if (!engine_) return;
    auto snapshot = engine_->ReadSnapshot();
    if (!snapshot) return;

    auto it = snapshot->find(path);
    if (it == snapshot->end()) return;
    const auto& directory = it->second;
    if (directory.status != ffprotocol::DirectoryEnumerationStatus::Success) return;

    std::vector<TreemapNode*> nodes;
    for (const auto& entry : directory.entries) {
        auto node = std::make_unique<TreemapNode>();
        node->name = entry.name;
        node->path = (path.empty() ? L"" : path + L"\\") + entry.name;
        node->isDirectory = entry.isDirectory;
        node->sizeBytes = entry.sizeBytes;
        node->totalSizeBytes = entry.isDirectory ? 0 : entry.sizeBytes;
        node->calculating = entry.isDirectory;
        node->depth = 1;
        nodes.push_back(node.get());
        root_->children.push_back(std::move(node));
    }

    // Sort: larger first for better layout
    std::sort(nodes.begin(), nodes.end(), [](TreemapNode* a, TreemapNode* b) {
        return a->totalSizeBytes > b->totalSizeBytes;
    });

    layout_.clear();
    if (!root_->children.empty()) {
        LayoutTreemap(layout_, kPadding, kPadding, 100.0f - 2 * kPadding, 100.0f - 2 * kPadding, nodes, 0);
    }
}

void TreemapView::Render(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory, D2D1_SIZE_F viewportSize, float offsetX, float offsetY) {
    if (!visible_ || layout_.empty()) return;

    EnsureCreated(context, dwriteFactory);
    const ffui::UiTheme theme = ffui::GetUiTheme(darkTheme_);
    context->Clear(theme.background);

    // Capture the same offset/scale the shell applies to draw the treemap so
    // HandleMouseMove can convert client coordinates back into normalized
    // 0..100 treemap space.
    viewportSize_ = viewportSize;
    offsetX_ = offsetX;
    offsetY_ = offsetY;

    const float scaleX = viewportSize.width / 100.0f;
    const float scaleY = viewportSize.height / 100.0f;

    // Rounded tiles use the shared small radius; UiFillRoundedRect clamps it to
    // half the smaller tile side so tiny tiles never produce malformed geometry.
    constexpr float tileRadius = ffui::UiMetrics::kRadiusSmall;

    // High Contrast suppresses the translucent interaction overlays so the
    // user's system colors are never layered over (see UITheme.h).
    const bool hoverLiftEnabled = hoveredNode_ != nullptr && !ffui::UiSystemHighContrast();

    for (const auto& rect : layout_) {
        const float rx = rect.x * scaleX;
        const float ry = rect.y * scaleY;
        const float rw = rect.width * scaleX;
        const float rh = rect.height * scaleY;

        if (rx + rw < 0 || ry + rh < 0 || rx > viewportSize.width || ry > viewportSize.height) {
            continue;
        }

        // Draw each tile inset by ~1 DIP per side, leaving a ~2 DIP gutter
        // between adjacent tiles. Layout computation is untouched; only the
        // drawing rect is inset, shrinking on tiny tiles so it never inverts.
        const float inset = std::min(1.0f, std::min(rw, rh) * 0.25f);
        D2D1_RECT_F drawRect = D2D1::RectF(rx + inset, ry + inset, rx + rw - inset, ry + rh - inset);
        ffui::UiFillRoundedRect(context, drawRect, rect.node->calculating ? calculatingBrush_.Get() : backgroundBrush_.Get(), tileRadius);
        ffui::UiDrawRoundedRect(context, drawRect, borderBrush_.Get(), tileRadius, kBorderWidth);

        if (hoverLiftEnabled && rect.node == hoveredNode_) {
            // Token-coherent hover lift: elevated fill on top of the base tile,
            // plus a subtle accent ring at reduced alpha (replaces the old 2px
            // accent outline).
            ffui::UiFillHoverOverlay(context, drawRect, tileRadius, theme.hoverOverlay, &hoverOverlayBrush_);
            D2D1_COLOR_F hoverRing = theme.accent;
            hoverRing.a *= kHoverRingAlpha;
            if (SUCCEEDED(ffui::UiEnsureSolidBrush(context, hoverRing, &hoverBrush_))) {
                ffui::UiDrawRoundedRect(context, drawRect, hoverBrush_.Get(), tileRadius, kBorderWidth);
            }
        }

        if (rw > 40.0f && rh > 20.0f) {
            std::wstring label = rect.node->name;
            if (rect.node->isDirectory && !label.empty() && label.back() != L'\\') {
                label += L'\\';
            }
            D2D1_RECT_F textRect = D2D1::RectF(rx + 4, ry + 2, rx + rw - 4, ry + rh - 2);
            context->DrawText(label.c_str(), static_cast<UINT32>(label.size()), textFormat_.Get(), textRect, textBrush_.Get());
        }
    }
}

bool TreemapView::HandleNotify(LPARAM lParam) {
    (void)lParam;
    return false;
}

bool TreemapView::HandleMouseMove(WPARAM /*wParam*/, LPARAM lParam) {
    if (!visible_) return false;
    // lParam holds raw client coordinates; the treemap is drawn under the
    // shell's Translate(sidebar, chrome) transform and scaled by viewport/100,
    // so translate back into normalized 0..100 treemap space before hit-testing.
    const float clientX = static_cast<float>(GET_X_LPARAM(lParam));
    const float clientY = static_cast<float>(GET_Y_LPARAM(lParam));
    if (viewportSize_.width <= 0.0f || viewportSize_.height <= 0.0f) return false;
    const auto [x, y] = TreemapClientToNormalized(clientX, clientY, offsetX_, offsetY_,
                                                  viewportSize_.width, viewportSize_.height);
    TreemapNode* node = HitTest(layout_, x, y);
    if (node != hoveredNode_) {
        hoveredNode_ = node;
        return true;
    }
    return false;
}

bool TreemapView::HandleLButtonDown(WPARAM /*wParam*/, LPARAM /*lParam*/) {
    if (!visible_ || !hoveredNode_) return false;
    if (navigate_ && !hoveredNode_->path.empty()) {
        navigate_(hoveredNode_->path);
        return true;
    }
    return false;
}

} // namespace ffui
