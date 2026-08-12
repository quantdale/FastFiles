#include "TreemapView.h"
#include "CategoryEngine.h"
#include "TreemapFilter.h"
#include "UITheme.h"
#include "UiStyle.h"
#include "ffui/SnapshotAggregate.h"

#include <algorithm>
#include <cmath>
#include <functional>
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
    if (lifetime_) lifetime_->store(false, std::memory_order_release);
    Hide();
}

bool TreemapView::Initialize(HWND owner, EngineClient* engine,
                             NavigateHandler navigate) {
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

TreemapNode* TreemapView::FindNode(TreemapNode* root, const std::wstring& path) {
    if (root == nullptr) return nullptr;
    if (root->path == path) return root;
    for (const auto& child : root->children) {
        if (auto* found = FindNode(child.get(), path)) return found;
    }
    return nullptr;
}

uint64_t TreemapView::RecomputeTotals(TreemapNode& node) {
    if (!node.isDirectory || node.aggregateResolved) return node.totalSizeBytes;

    uint64_t total = 0;
    bool incomplete = false;
    for (auto& child : node.children) {
        const uint64_t childTotal = RecomputeTotals(*child);
        if (UINT64_MAX - total < childTotal) {
            total = 0;
            incomplete = true;
            break;
        }
        total += childTotal;
        incomplete = incomplete || child->calculating || child->aggregateFailed;
    }
    node.totalSizeBytes = total;
    node.calculating = incomplete;
    return node.totalSizeBytes;
}

void TreemapView::RebuildLayout() {
    layout_.clear();
    if (!root_ || root_->children.empty()) return;

    std::vector<TreemapNode*> nodes;
    nodes.reserve(root_->children.size());
    for (const auto& child : root_->children) nodes.push_back(child.get());
    std::sort(nodes.begin(), nodes.end(), [](TreemapNode* left, TreemapNode* right) {
        return left->totalSizeBytes > right->totalSizeBytes;
    });
    LayoutTreemap(layout_, kPadding, kPadding, 100.0f - 2 * kPadding,
                  100.0f - 2 * kPadding, nodes, 0);
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

void TreemapView::SetCategoryFilter(const CategoryEngine* categoryEngine, const std::wstring& categoryId) {
    categoryEngine_ = categoryEngine;
    categoryId_ = categoryId;
    if (visible_) {
        BuildTree(currentPath_);
        InvalidateRect(owner_, nullptr, FALSE);
    }
}

void TreemapView::BuildTree(const std::wstring& path) {
    const uint64_t generation = ++buildGeneration_;
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

    const bool categoryFilterActive = categoryEngine_ != nullptr && !categoryId_.empty();

    const auto joinPath = [](const std::wstring& base, const std::wstring& name) {
        if (base.empty()) return name;
        if (base.back() == L'\\' || base.back() == L'/') return base + name;
        return base + L"\\" + name;
    };

    const HWND targetWindow = owner_;
    const auto lifetime = lifetime_;
    const auto engine = engine_;
    const auto requestAggregate = [engine, targetWindow, lifetime, generation, categoryFilterActive](const TreemapNode& node) {
        if (engine == nullptr || targetWindow == nullptr || node.volumeRowId == 0 ||
            (node.fileIdLow == 0 && node.fileIdHigh == 0) || !node.calculating || categoryFilterActive) {
            return;
        }
        const std::wstring nodePath = node.path;
        engine->RequestFolderAggregate(
            node.volumeRowId, node.fileIdLow, node.fileIdHigh,
            [targetWindow, lifetime, generation, nodePath](uint64_t /*requestId*/,
                                                            ffprotocol::FolderAggregateStatus status,
                                                            uint64_t /*itemCount*/, uint64_t totalSizeBytes) {
                if (!lifetime->load(std::memory_order_acquire)) return;
                auto payload = std::make_unique<TreemapAggregatePayload>();
                payload->generation = generation;
                payload->path = nodePath;
                payload->status = status;
                payload->totalSizeBytes = totalSizeBytes;
                if (PostMessageW(targetWindow, WM_APP_TREEMAP_AGGREGATE, 0,
                                 reinterpret_cast<LPARAM>(payload.get()))) {
                    payload.release();
                }
            });
    };

    // Directory entries in degraded snapshots do not carry recursive sizes.
    // Reconstruct the aggregate from any browsed child directories already
    // present in the same immutable snapshot. Unknown subtrees remain marked
    // calculating rather than being rendered as misleading zero-byte tiles.
    const std::function<uint64_t(TreemapNode&)> populate = [&](TreemapNode& parent) -> uint64_t {
        const auto directoryIt = snapshot->find(parent.path);
        if (directoryIt == snapshot->end() ||
            directoryIt->second.status != ffprotocol::DirectoryEnumerationStatus::Success) {
            parent.calculating = true;
            return 0;
        }

        uint64_t total = 0;
        bool incomplete = false;
        for (const auto& entry : directoryIt->second.entries) {
            auto child = std::make_unique<TreemapNode>();
            child->name = entry.name;
            child->path = joinPath(parent.path, entry.name);
            child->isDirectory = entry.isDirectory;
            child->sizeBytes = entry.sizeBytes;
            child->totalSizeBytes = entry.isDirectory ? 0 : entry.sizeBytes;
            child->calculating = entry.isDirectory;
            child->volumeRowId = entry.volumeRowId;
            child->fileIdLow = entry.fileIdLow;
            child->fileIdHigh = entry.fileIdHigh;
            child->depth = parent.depth + 1;
            child->parent = &parent;
            if (child->isDirectory) {
                child->totalSizeBytes = populate(*child);
                if (child->calculating || child->aggregateFailed) incomplete = true;
                requestAggregate(*child);
            }
            if (UINT64_MAX - total < child->totalSizeBytes) {
                total = UINT64_MAX;
                incomplete = true;
            } else {
                total += child->totalSizeBytes;
            }
            parent.children.push_back(std::move(child));
        }
        parent.totalSizeBytes = total;
        parent.calculating = incomplete;
        return total;
    };

    for (const auto& entry : directory.entries) {
        auto node = std::make_unique<TreemapNode>();
        node->name = entry.name;
        node->path = joinPath(path, entry.name);
        node->isDirectory = entry.isDirectory;
        node->sizeBytes = entry.sizeBytes;
        node->totalSizeBytes = entry.isDirectory ? 0 : entry.sizeBytes;
        node->calculating = entry.isDirectory;
        node->volumeRowId = entry.volumeRowId;
        node->fileIdLow = entry.fileIdLow;
        node->fileIdHigh = entry.fileIdHigh;
        node->depth = 1;
        node->parent = root_.get();
        if (node->isDirectory) {
            node->totalSizeBytes = populate(*node);
            requestAggregate(*node);
        }
        root_->children.push_back(std::move(node));
    }

    if (categoryFilterActive) {
        FilterTreemapTree(*root_, *categoryEngine_, categoryId_);
    }

    RebuildLayout();
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

bool TreemapView::HandleAggregateCompletion(LPARAM lParam) {
    std::unique_ptr<TreemapAggregatePayload> payload(reinterpret_cast<TreemapAggregatePayload*>(lParam));
    if (!payload || !visible_ || payload->generation != buildGeneration_) return true;
    if (categoryEngine_ != nullptr && !categoryId_.empty()) return true;

    TreemapNode* node = FindNode(root_.get(), payload->path);
    if (node == nullptr || !node->isDirectory) return true;

    if (payload->status == ffprotocol::FolderAggregateStatus::Resolved) {
        node->totalSizeBytes = payload->totalSizeBytes;
        node->calculating = false;
        node->aggregateResolved = true;
        node->aggregateFailed = false;
    } else {
        node->totalSizeBytes = 0;
        node->calculating = true;
        node->aggregateResolved = false;
        node->aggregateFailed = true;
    }

    if (root_) RecomputeTotals(*root_);
    RebuildLayout();
    InvalidateRect(owner_, nullptr, FALSE);
    return true;
}

bool TreemapView::HandleMouseMove(WPARAM /*wParam*/, LPARAM lParam) {
    if (!visible_) return false;
    // lParam holds raw client coordinates; the treemap is drawn under the
    // shell's Translate(sidebar, chrome) transform and scaled by viewport/100,
    // so translate back into normalized 0..100 treemap space before hit-testing.
    const float clientX = static_cast<float>(GET_X_LPARAM(lParam));
    const float clientY = static_cast<float>(GET_Y_LPARAM(lParam));
    if (viewportSize_.width <= 0.0f || viewportSize_.height <= 0.0f) return false;
    TreemapNode* node = HitTestClientPoint(layout_, clientX, clientY, offsetX_, offsetY_,
                                           viewportSize_.width, viewportSize_.height);
    if (node != hoveredNode_) {
        hoveredNode_ = node;
        return true;
    }
    return false;
}

bool TreemapView::HandleLButtonDown(WPARAM /*wParam*/, LPARAM lParam) {
    if (!visible_ || layout_.empty() || viewportSize_.width <= 0.0f || viewportSize_.height <= 0.0f) {
        return false;
    }

    // Resolve the click independently of hover state. A view switch does not
    // guarantee a subsequent WM_MOUSEMOVE (for example when the pointer is
    // already stationary over a tile), so requiring hoveredNode_ made the
    // first click on a freshly opened treemap a no-op.
    const float clientX = static_cast<float>(GET_X_LPARAM(lParam));
    const float clientY = static_cast<float>(GET_Y_LPARAM(lParam));
    hoveredNode_ = HitTestClientPoint(layout_, clientX, clientY, offsetX_, offsetY_,
                                       viewportSize_.width, viewportSize_.height);
    if (navigate_ && hoveredNode_ != nullptr && !hoveredNode_->path.empty()) {
        navigate_(hoveredNode_->path, hoveredNode_->isDirectory);
        return true;
    }
    return false;
}

} // namespace ffui
