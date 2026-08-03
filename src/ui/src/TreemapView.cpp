#include "TreemapView.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <shlwapi.h>
#include <windowsx.h>

#pragma comment(lib, "shlwapi.lib")

namespace ffui {
namespace {

constexpr float kMinRectSize = 4.0f;
constexpr float kBorderWidth = 1.0f;
constexpr int kPadding = 2;

float AspectRatio(float side1, float side2) {
    if (side1 < 0.001f || side2 < 0.001f) return 1.0f;
    return std::max(side1 / side2, side2 / side1);
}

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

void TreemapView::SetEngineActive(bool active) {
    engineActive_ = active;
}

void TreemapView::OnSnapshotUpdated() {
    if (!visible_) return;
    BuildTree(currentPath_);
}

void TreemapView::EnsureCreated(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory) {
    if (resourcesCreated_) return;
    context->CreateSolidColorBrush(D2D1::ColorF(0xF1F3F4), &backgroundBrush_);
    context->CreateSolidColorBrush(D2D1::ColorF(0x000000), &textBrush_);
    context->CreateSolidColorBrush(D2D1::ColorF(0xD8D8D8), &borderBrush_);
    context->CreateSolidColorBrush(D2D1::ColorF(0x2B6CDA), &hoverBrush_);
    context->CreateSolidColorBrush(D2D1::ColorF(0xFFF3CD), &calculatingBrush_);
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

void TreemapView::LayoutTreemap(std::vector<TreemapRect>& rects, float x, float y, float width, float height,
                                const std::vector<TreemapNode*>& nodes, int depth) {
    if (nodes.empty() || width < kMinRectSize || height < kMinRectSize) return;

    if (nodes.size() == 1) {
        TreemapRect rect;
        rect.x = x;
        rect.y = y;
        rect.width = std::max(kMinRectSize, width);
        rect.height = std::max(kMinRectSize, height);
        rect.node = nodes[0];
        rects.push_back(rect);
        return;
    }

    Squarify(rects, x, y, width, height, nodes, depth);
}

void TreemapView::Squarify(std::vector<TreemapRect>& rects, float x, float y, float width, float height,
                           const std::vector<TreemapNode*>& nodes, int /*depth*/) {
    if (nodes.empty()) return;

    const bool horizontal = width >= height;
    const float totalSize = std::max(1.0f, static_cast<float>(std::accumulate(nodes.begin(), nodes.end(), 0ULL,
                                                                              [](uint64_t sum, TreemapNode* n) {
                                                                                  return sum + n->totalSizeBytes;
                                                                              })));

    float currentPos = 0.0f;
    size_t startIdx = 0;

    while (startIdx < nodes.size()) {
        float currentSum = 0.0f;
        size_t endIdx = startIdx;
        float bestAspect = 1e10f;

        for (size_t i = startIdx; i < nodes.size(); ++i) {
            float newSum = currentSum + static_cast<float>(nodes[i]->totalSizeBytes) / totalSize;
            float newAspect;

            if (horizontal) {
                float side1 = newSum * width;
                float side2 = height;
                newAspect = AspectRatio(side1, side2);
            } else {
                float side1 = newSum * height;
                float side2 = width;
                newAspect = AspectRatio(side1, side2);
            }

            if (newAspect <= bestAspect || i == startIdx) {
                bestAspect = newAspect;
                currentSum = newSum;
                endIdx = i + 1;
            } else {
                break;
            }
        }

        const float sliceSize = currentSum * (horizontal ? width : height);

        for (size_t i = startIdx; i < endIdx; ++i) {
            TreemapRect rect;
            if (horizontal) {
                float rectWidth = (static_cast<float>(nodes[i]->totalSizeBytes) / totalSize) * width / currentSum;
                rect.x = x + currentPos;
                rect.y = y;
                rect.width = std::max(kMinRectSize, rectWidth);
                rect.height = std::max(kMinRectSize, height);
                rect.node = nodes[i];
                rects.push_back(rect);
            } else {
                float rectHeight = (static_cast<float>(nodes[i]->totalSizeBytes) / totalSize) * height / currentSum;
                rect.x = x;
                rect.y = y + currentPos;
                rect.width = std::max(kMinRectSize, width);
                rect.height = std::max(kMinRectSize, rectHeight);
                rect.node = nodes[i];
                rects.push_back(rect);
            }
        }

        if (horizontal) {
            currentPos += sliceSize;
            x += sliceSize;
            width -= sliceSize;
        } else {
            currentPos += sliceSize;
            y += sliceSize;
            height -= sliceSize;
        }

        startIdx = endIdx;
    }
}

TreemapNode* TreemapView::HitTest(float x, float y) const {
    for (auto it = layout_.rbegin(); it != layout_.rend(); ++it) {
        if (x >= it->x && x < it->x + it->width && y >= it->y && y < it->y + it->height) {
            return it->node;
        }
    }
    return nullptr;
}

void TreemapView::Render(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory, D2D1_SIZE_F viewportSize) {
    if (!visible_ || layout_.empty()) return;

    EnsureCreated(context, dwriteFactory);
    context->Clear(D2D1::ColorF(darkTheme_ ? 0x202124 : 0xFFFFFF));

    const float scaleX = viewportSize.width / 100.0f;
    const float scaleY = viewportSize.height / 100.0f;

    for (const auto& rect : layout_) {
        const float rx = rect.x * scaleX;
        const float ry = rect.y * scaleY;
        const float rw = rect.width * scaleX;
        const float rh = rect.height * scaleY;

        if (rx + rw < 0 || ry + rh < 0 || rx > viewportSize.width || ry > viewportSize.height) {
            continue;
        }

        D2D1_RECT_F drawRect = D2D1::RectF(rx, ry, rx + rw, ry + rh);
        context->FillRectangle(drawRect, rect.node->calculating ? calculatingBrush_.Get() : backgroundBrush_.Get());
        context->DrawRectangle(drawRect, borderBrush_.Get(), kBorderWidth);

        if (rect.node == hoveredNode_) {
            context->DrawRectangle(drawRect, hoverBrush_.Get(), 2.0f);
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
    const float x = static_cast<float>(GET_X_LPARAM(lParam));
    const float y = static_cast<float>(GET_Y_LPARAM(lParam));
    TreemapNode* node = HitTest(x, y);
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
