#include "ColumnView.h"
#include "EngineStatusPolicy.h"
#include "SelectionModel.h"
#include "UITheme.h"
#include "UiAnimation.h"
#include "UiStyle.h"
#include "IconCache.h"
#include "ffui/SelectionIdentity.h"
#include "ffui/SnapshotPathLookup.h"
#include "ffprotocol/Diagnostics.h"

#include <algorithm>
#include <cwchar>
#include <functional>
#include <windows.h>

namespace ffui {

namespace {

std::wstring JoinPath(const std::wstring& base, const std::wstring& name) {
    if (base.empty()) {
        return name;
    }
    if (base.back() == L'\\' || base.back() == L'/') {
        return base + name;
    }
    return base + L'\\' + name;
}

// Task 3.2: extension of a display name (L"report.pdf" -> L"pdf"), used to
// key the shared type-icon cache; IconKeyForExtension normalizes the leading
// dot. Dotfiles, extensionless names, and the "C:\" drive rows all yield the
// empty string, which maps to the generic file icon.
std::wstring FileExtensionOf(const std::wstring& fileName) {
    const size_t nameStart = fileName.find_last_of(L"\\/");
    const size_t nameIndex = nameStart == std::wstring::npos ? 0 : nameStart + 1;
    const size_t dot = fileName.find_last_of(L'.');
    if (dot == std::wstring::npos || dot < nameIndex || dot + 1 >= fileName.size()) {
        return {};
    }
    return fileName.substr(dot + 1);
}

void ClearUnavailableColumn(Column& column) {
    column.items.clear();
    column.selectedIndices.clear();
    column.selectionAnchor = -1;
    column.focusIndex = -1;
}

void ApplyColumnFailure(Column& column, SnapshotFailureReason reason, uint64_t generation = 0) {
    if (!column.freshness.Fail(reason, generation)) return;
    if (column.freshness.state == SnapshotViewState::Unavailable) {
        ClearUnavailableColumn(column);
    }
}

} // namespace

Column ColumnView::MakeComputerRootColumn() {
    Column root;
    root.path = L""; // sentinel: not engine-backed, populated directly below

    const DWORD driveMask = GetLogicalDrives();
    for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
        if ((driveMask & (1u << (letter - L'A'))) == 0) {
            continue;
        }
        wchar_t rootPath[] = {letter, L':', L'\\', L'\0'};
        // Keep the synthetic root consistent with the sidebar's logical-drive
        // list. Removable, optical, RAM-disk, and mapped network roots are
        // valid navigation targets too; inaccessible media is surfaced by
        // the normal degraded enumeration error state after activation.
        const UINT driveType = GetDriveTypeW(rootPath);
        if (driveType == DRIVE_UNKNOWN || driveType == DRIVE_NO_ROOT_DIR) {
            continue;
        }
        ColumnItem item;
        item.name = rootPath;
        item.isDirectory = true;
        root.items.push_back(std::move(item));
    }
    return root;
}

void ColumnView::Initialize(EngineClient* engineClient) {
    engineClient_ = engineClient;

    std::lock_guard<std::mutex> lock(columnsMutex_);
    columns_.push_back(MakeComputerRootColumn());
    focusedColumnIndex_ = 0;
}

void ColumnView::TruncateAfter(int columnIndex) {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    auto& columns = ActiveColumns();
    if (columnIndex + 1 < static_cast<int>(columns.size())) {
        columns.resize(columnIndex + 1);
    }
}

void ColumnView::RequestColumn(int /*columnIndex*/, const std::wstring& path) {
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        auto& columns = ActiveColumns();
        int& focusedIndex = ActiveFocusedColumnIndex();
        Column column;
        column.path = path;
        columns.push_back(std::move(column));
        focusedIndex = static_cast<int>(columns.size()) - 1;
        if (activePane_ == 0) verticalScrollOffset_ = 0.0f;
        else verticalScrollOffset2_ = 0.0f;
    }
    if (engineClient_) {
        engineClient_->RequestDirectory(path);
    }
}

void ColumnView::SelectSingle(Column& column, int itemIndex) {
    ApplySelectionClick(column.selectedIndices, column.selectionAnchor, column.focusIndex, itemIndex, false, false);
}

void ColumnView::ToggleSelection(Column& column, int itemIndex) {
    ApplySelectionClick(column.selectedIndices, column.selectionAnchor, column.focusIndex, itemIndex, true, false);
}

void ColumnView::SelectRange(Column& column, int itemIndex) {
    ApplySelectionClick(column.selectedIndices, column.selectionAnchor, column.focusIndex, itemIndex, false, true);
}

void ColumnView::ActivateItem(int columnIndex, int itemIndex, bool control, bool shift) {
    std::wstring childPath;
    bool isDirectory = false;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        auto& columns = ActiveColumns();
        if (columnIndex < 0 || columnIndex >= static_cast<int>(columns.size())) {
            return;
        }
        Column& column = columns[columnIndex];
        if (itemIndex < 0 || itemIndex >= static_cast<int>(column.items.size())) {
            return;
        }
        if (shift) {
            SelectRange(column, itemIndex);
        } else if (control) {
            ToggleSelection(column, itemIndex);
        } else {
            SelectSingle(column, itemIndex);
        }
        isDirectory = column.items[itemIndex].isDirectory;
        childPath = JoinPath(column.path, column.items[itemIndex].name);
    }

    if (control || shift) {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        ActiveFocusedColumnIndex() = columnIndex;
        return;
    }

    TruncateAfter(columnIndex);
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        ActiveFocusedColumnIndex() = columnIndex;
    }

    if (isDirectory) {
        // Overrides focusedColumnIndex_ to the newly created column, which
        // is the correct/deeper focus target.
        RequestColumn(columnIndex + 1, childPath);
    }
}

void ColumnView::OnKeyDown(int virtualKey) {
    // quick-access-home 4.5: the Home pane owns its own keyboard model.
    if (homePane_[activePane_]) {
        HandleHomeKeyDown(virtualKey);
        return;
    }
    int activateColumnIndex = -1;
    int activateItemIndex = -1;

    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        auto& columns = ActiveColumns();
        int& focusedIndex = ActiveFocusedColumnIndex();
        if (columns.empty()) {
            return;
        }
        focusedIndex = std::clamp(focusedIndex, 0, static_cast<int>(columns.size()) - 1);
        Column& focused = columns[focusedIndex];

        switch (virtualKey) {
            case VK_UP:
                if (!focused.items.empty()) {
                    SelectSingle(focused, std::max(0, focused.focusIndex - 1));
                }
                break;
            case VK_DOWN:
                if (!focused.items.empty()) {
                    SelectSingle(focused, std::min(static_cast<int>(focused.items.size()) - 1, focused.focusIndex + 1));
                }
                break;
            case VK_LEFT:
                focusedIndex = std::max(0, focusedIndex - 1);
                break;
            case VK_RIGHT:
                focusedIndex = std::min(static_cast<int>(columns.size()) - 1, focusedIndex + 1);
                break;
            case VK_RETURN:
                activateColumnIndex = focusedIndex;
                activateItemIndex = focused.focusIndex;
                break;
            case 'A':
                if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
                    focused.selectedIndices.clear();
                    for (int index = 0; index < static_cast<int>(focused.items.size()); ++index) {
                        focused.selectedIndices.insert(index);
                    }
                    focused.focusIndex = focused.items.empty() ? -1 : 0;
                    if (!focused.items.empty()) {
                        focused.selectionAnchor = 0;
                    }
                }
                break;
            default:
                break;
        }
    } // release columnsMutex_ before ActivateItem (which re-acquires it) to avoid a self-deadlock

    if (activateColumnIndex >= 0) {
        ActivateItem(activateColumnIndex, activateItemIndex);
    }
}

void ColumnView::OnMouseDown(D2D1_POINT_2F clientPoint, float scrollOffset, float viewportWidth, bool control, bool shift) {
    if (clientPoint.y < kBadgeHeight) {
        // recovery-degraded-browsing D3: the top status chip is the retry
        // affordance for the Engine unavailable state only (modernize-ui-
        // appearance 3.6 kept the chip non-interactive; the Unavailable
        // state now genuinely offers an action).
        if (engineState_.load() == EngineConnectionState::Unavailable && retryEngineCallback_) {
            retryEngineCallback_();
        }
        return;
    }
    
    if (dualPane_) {
        const float paneWidth = viewportWidth / 2.0f;
        const int clickedPane = clientPoint.x < paneWidth ? 0 : 1;
        if (clickedPane != activePane_) {
            ActivatePane(clickedPane);
        }
    }
    
    const int pane = activePane_;
    if (homePane_[pane]) {
        // quick-access-home 4.8: the section-header Clear affordances first
        // (their rects are recorded in viewport coordinates by RenderHomePane).
        const auto& clears = homeClearButtons_[pane];
        const auto inRect = [&clientPoint](const D2D1_RECT_F& rect) {
            return clientPoint.x >= rect.left && clientPoint.x < rect.right &&
                   clientPoint.y >= rect.top && clientPoint.y < rect.bottom;
        };
        if (inRect(clears.recentFolders) || inRect(clears.recentSearches)) {
            if (homeActivateCallback_ != nullptr) {
                HomeActivation activation;
                activation.kind = inRect(clears.recentFolders)
                    ? HomeActionKind::ClearRecentFolders : HomeActionKind::ClearRecentSearches;
                homeActivateCallback_(activation);
            }
            return;
        }
        // quick-access-home 4.4: hit-test the Home surface. `clientPoint` is
        // converted to pane-local coords (pane origin removed) and the vertical
        // scroll offset is folded into the content offset so the Point2F lands
        // in the same coordinate space the layout was drawn in.
        const float paneWidth = dualPane_ ? viewportWidth / 2.0f : viewportWidth;
        const float paneX = (dualPane_ && pane == 1) ? paneWidth : 0.0f;
        const D2D1_POINT_2F localPoint = D2D1::Point2F(clientPoint.x - paneX, clientPoint.y);
        const HomeLayoutResult layout = ComputeHomeLayoutForPane(pane);
        const float vso = pane == 0 ? verticalScrollOffset_ : verticalScrollOffset2_;
        const float contentOffsetY = kBadgeHeight - vso;
        const int hit = ComputeHomeItemHitTest(layout, localPoint, contentOffsetY);
        int& focused = pane == 0 ? homeFocusedIndex_ : homeFocusedIndex2_;
        if (hit >= 0) {
            focused = hit;
            // quick-access-home 4.4: Ctrl (control) => open in new tab; the
            // activation always routes through the callback.
            ActivateHomeItem(pane, hit, control);
        } else {
            focused = -1;
        }
        return;
    }

    const float effectiveScrollOffset = activePane_ == 0 ? scrollOffset : scrollOffset2_;
    const float verticalScrollOffset = activePane_ == 0 ? verticalScrollOffset_ : verticalScrollOffset2_;
    const auto hit = ComputeColumnHitTest(clientPoint, effectiveScrollOffset, verticalScrollOffset,
                                          viewportWidth, dualPane_, activePane_);
    ActivateItem(hit.columnIndex, hit.itemIndex, control, shift);
}

void ColumnView::SetIconCache(IconCache* iconCache) {
    iconCache_ = iconCache;
}

void ColumnView::SetRepaintCallback(std::function<void()> repaint) {
    repaint_ = std::move(repaint);
}

void ColumnView::OnMouseMove(D2D1_POINT_2F clientPoint, float scrollOffset, float viewportWidth) {
    if (clientPoint.y < kBadgeHeight) {
        return; // over the status badge: not hovering any row
    }
    if (dualPane_) {
        const float paneWidth = viewportWidth / 2.0f;
        const int hoveredPane = clientPoint.x < paneWidth ? 0 : 1;
        if (hoveredPane != activePane_) {
            ActivatePane(hoveredPane); // keep hover coords in the active pane, as OnMouseDown does
        }
    }
    const int pane = activePane_;
    if (homePane_[pane]) {
        // quick-access-home 4.4: paint-only hover tracking for the Home surface
        // (no selection change). `hoverColumn_` is set to -1 in Home mode so the
        // column renderer's hover test (which requires a valid column) never
        // matches; the Home draw loop tests `hoverColumn_ == -1` instead.
        const float paneWidth = dualPane_ ? viewportWidth / 2.0f : viewportWidth;
        const float paneX = (dualPane_ && pane == 1) ? paneWidth : 0.0f;
        const D2D1_POINT_2F localPoint = D2D1::Point2F(clientPoint.x - paneX, clientPoint.y);
        const HomeLayoutResult layout = ComputeHomeLayoutForPane(pane);
        const float vso = pane == 0 ? verticalScrollOffset_ : verticalScrollOffset2_;
        const int hit = ComputeHomeItemHitTest(layout, localPoint, kBadgeHeight - vso);
        if (hit != hoverItem_ || hoverColumn_ != -1 || !hoverActive_) {
            hoverItem_ = hit;
            hoverColumn_ = -1;
            hoverActive_ = true;
            hoverEnterMs_ = static_cast<uint64_t>(GetTickCount64());
            hoverOpacity_.AnimateTo(1.0f, kUiAnimationDefaultMs, hoverEnterMs_);
            if (repaint_) {
                repaint_();
            }
        }
        return;
    }

    const float effectiveScrollOffset = activePane_ == 0 ? scrollOffset : scrollOffset2_;
    const float verticalScrollOffset = activePane_ == 0 ? verticalScrollOffset_ : verticalScrollOffset2_;
    const auto hit = ComputeColumnHitTest(clientPoint, effectiveScrollOffset, verticalScrollOffset,
                                          viewportWidth, dualPane_, activePane_);
    hoverColumn_ = hit.columnIndex;
    hoverItem_ = hit.itemIndex;
    hoverActive_ = true;
    hoverEnterMs_ = static_cast<uint64_t>(GetTickCount64());
    hoverOpacity_.AnimateTo(1.0f, kUiAnimationDefaultMs, hoverEnterMs_);
    if (repaint_) {
        repaint_();
    }
}

void ColumnView::OnMouseLeave() {
    hoverActive_ = false;
    hoverLeaveMs_ = static_cast<uint64_t>(GetTickCount64());
    hoverOpacity_.AnimateTo(0.0f, kUiAnimationDefaultMs, hoverLeaveMs_);
    if (repaint_) {
        repaint_();
    }
}

bool ColumnView::HoverAnimating() const {
    return hoverActive_ ||
           (hoverLeaveMs_ != 0 && (GetTickCount64() - hoverLeaveMs_) < static_cast<uint64_t>(kUiAnimationDefaultMs));
}

std::vector<std::wstring> ColumnView::ActiveSelectionPaths() const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    std::vector<std::wstring> paths;
    const auto& columns = ActiveColumns();
    const int& focusedIndex = const_cast<ColumnView*>(this)->ActiveFocusedColumnIndex();
    if (focusedIndex < 0 || focusedIndex >= static_cast<int>(columns.size())) {
        return paths;
    }
    const Column& column = columns[focusedIndex];
    for (int index : column.selectedIndices) {
        if (index >= 0 && index < static_cast<int>(column.items.size())) {
            paths.push_back(JoinPath(column.path, column.items[index].name));
        }
    }
    return paths;
}

std::vector<SelectionItem> ColumnView::ActiveSelectionItems() const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    std::vector<SelectionItem> items;
    const auto& columns = ActiveColumns();
    const int& focusedIndex = const_cast<ColumnView*>(this)->ActiveFocusedColumnIndex();
    if (focusedIndex < 0 || focusedIndex >= static_cast<int>(columns.size())) return items;
    const Column& column = columns[focusedIndex];
    for (int index : column.selectedIndices) {
        if (index >= 0 && index < static_cast<int>(column.items.size())) {
            items.push_back({JoinPath(column.path, column.items[index].name), column.items[index].isDirectory});
        }
    }
    return items;
}

void ColumnView::SelectAll() {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    auto& columns = ActiveColumns();
    int& focusedIndex = ActiveFocusedColumnIndex();
    if (focusedIndex < 0 || focusedIndex >= static_cast<int>(columns.size())) return;
    Column& column = columns[focusedIndex];
    SelectAllItems(column.selectedIndices, column.selectionAnchor, column.focusIndex,
                   static_cast<int>(column.items.size()));
}

void ColumnView::RefreshActiveColumn() {
    const std::wstring path = ActivePanePath();
    if (!path.empty() && engineClient_ != nullptr) {
        MarkPathLoading(path);
        engineClient_->RequestDirectory(path);
    }
}

void ColumnView::RefreshAllColumns() {
    if (engineClient_ == nullptr) return;
    std::vector<std::wstring> paths;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        const auto collect = [&paths](std::vector<Column>& columns) {
            for (const auto& column : columns) {
                if (!column.path.empty()) paths.push_back(column.path);
            }
            for (auto& column : columns) {
                if (!column.path.empty()) column.freshness.BeginRequest();
            }
        };
        collect(columns_);
        collect(columns2_);
    }
    for (const auto& path : paths) engineClient_->RequestDirectory(path);
}

void ColumnView::ScrollVertical(float delta, float viewportHeight) {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    // quick-access-home 4.x: Home clamps against the laid-out content height.
    const int pane = activePane_;
    if (homePane_[pane]) {
        const HomeLayoutResult layout = ComputeHomeLayoutForPane(pane);
        const float visibleHeight = (std::max)(0.0f, viewportHeight - kBadgeHeight);
        const float maximum = (std::max)(0.0f, layout.contentHeight - visibleHeight);
        float& offset = pane == 0 ? verticalScrollOffset_ : verticalScrollOffset2_;
        offset = std::clamp(offset + delta, 0.0f, maximum);
        return;
    }
    auto& columns = ActiveColumns();
    const int focusedIndex = ActiveFocusedColumnIndex();
    const size_t itemCount = focusedIndex >= 0 && focusedIndex < static_cast<int>(columns.size())
        ? columns[focusedIndex].items.size() : 0;
    const float visibleHeight = (std::max)(0.0f, viewportHeight - kBadgeHeight);
    const float maximum = (std::max)(0.0f, static_cast<float>(itemCount) * kRowHeight - visibleHeight);
    float& offset = activePane_ == 0 ? verticalScrollOffset_ : verticalScrollOffset2_;
    offset = std::clamp(offset + delta, 0.0f, maximum);
}

void ColumnView::EnsureFocusedItemVisible(float viewportHeight) {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    // quick-access-home 4.x: Home uses the focused item's rect from the layout.
    const int pane = activePane_;
    if (homePane_[pane]) {
        const int focused = pane == 0 ? homeFocusedIndex_ : homeFocusedIndex2_;
        if (focused < 0) return;
        const HomeLayoutResult layout = ComputeHomeLayoutForPane(pane);
        float itemTop = 0.0f;
        float itemBottom = 0.0f;
        for (const auto& item : layout.items) {
            if (item.globalIndex == focused) {
                itemTop = item.y;
                itemBottom = item.y + item.height;
                break;
            }
        }
        const float visibleHeight = (std::max)(0.0f, viewportHeight - kBadgeHeight);
        float& offset = pane == 0 ? verticalScrollOffset_ : verticalScrollOffset2_;
        if (itemTop < offset) offset = itemTop;
        else if (itemBottom > offset + visibleHeight) offset = itemBottom - visibleHeight;
        const float maximum = (std::max)(0.0f, layout.contentHeight - visibleHeight);
        offset = std::clamp(offset, 0.0f, maximum);
        return;
    }
    auto& columns = ActiveColumns();
    const int focusedIndex = ActiveFocusedColumnIndex();
    if (focusedIndex < 0 || focusedIndex >= static_cast<int>(columns.size())) return;
    const int itemIndex = columns[focusedIndex].focusIndex;
    if (itemIndex < 0) return;
    const float visibleHeight = (std::max)(0.0f, viewportHeight - kBadgeHeight);
    const float top = static_cast<float>(itemIndex) * kRowHeight;
    const float bottom = top + kRowHeight;
    float& offset = activePane_ == 0 ? verticalScrollOffset_ : verticalScrollOffset2_;
    if (top < offset) offset = top;
    else if (bottom > offset + visibleHeight) offset = bottom - visibleHeight;
    const float maximum = (std::max)(0.0f, static_cast<float>(columns[focusedIndex].items.size()) * kRowHeight - visibleHeight);
    offset = std::clamp(offset, 0.0f, maximum);
}

void ColumnView::NavigateToPath(const std::wstring& path, const std::wstring& selectName) {
    if (path.empty()) return;
    const std::vector<std::wstring> paths = BuildNavigationPathChain(path);
    if (paths.empty()) return;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        auto& columns = ActiveColumns();
        int& focusedIndex = ActiveFocusedColumnIndex();
        columns.clear();
        for (const auto& columnPath : paths) {
            Column column;
            column.path = columnPath;
            columns.push_back(std::move(column));
        }
        focusedIndex = static_cast<int>(columns.size()) - 1;
        if (activePane_ == 0) verticalScrollOffset_ = 0.0f;
        else verticalScrollOffset2_ = 0.0f;
        pendingSelectionName_ = selectName;
        pendingSelectionPath_ = selectName.empty() ? std::wstring{} : paths.back();
    }
    if (engineClient_ != nullptr) {
        for (const auto& columnPath : paths) engineClient_->RequestDirectory(columnPath);
    }
}

void ColumnView::NavigateToComputerRoot() {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    auto& columns = ActiveColumns();
    columns.clear();
    columns.push_back(MakeComputerRootColumn());
    ActiveFocusedColumnIndex() = 0;
    if (activePane_ == 0) verticalScrollOffset_ = 0.0f;
    else verticalScrollOffset2_ = 0.0f;
    pendingSelectionName_.clear();
    pendingSelectionPath_.clear();
    hoverColumn_ = -1;
    hoverItem_ = -1;
    hoverActive_ = false;
}

void ColumnView::NavigateToHierarchy(const std::wstring& fullPath, bool isDirectory) {
    if (fullPath.size() < 3) return;
    const size_t slash = fullPath.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return;
    const std::wstring containingPath = fullPath.substr(0, slash);
    const std::wstring selectedName = fullPath.substr(slash + 1);
    std::vector<std::wstring> paths;
    if (containingPath.size() >= 3 && containingPath[1] == L':') {
        std::wstring current = containingPath.substr(0, 3);
        paths.push_back(current);
        size_t start = 3;
        while (start < containingPath.size()) {
            const size_t end = containingPath.find(L'\\', start);
            const std::wstring component = containingPath.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
            if (!component.empty()) {
                if (current.back() != L'\\') current += L'\\';
                current += component;
                paths.push_back(current);
            }
            if (end == std::wstring::npos) break;
            start = end + 1;
        }
    } else {
        paths.push_back(containingPath);
    }
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        auto& columns = ActiveColumns();
        int& focusedIndex = ActiveFocusedColumnIndex();
        columns.clear();
        for (const auto& path : paths) {
            Column column;
            column.path = path;
            columns.push_back(std::move(column));
        }
        focusedIndex = static_cast<int>(columns.size()) - 1;
        pendingSelectionName_ = selectedName;
        pendingSelectionPath_ = paths.empty() ? std::wstring{} : paths.back();
        if (activePane_ == 0) verticalScrollOffset_ = 0.0f;
        else verticalScrollOffset2_ = 0.0f;
    }
    for (const auto& path : paths) if (engineClient_ != nullptr) engineClient_->RequestDirectory(path);
    if (isDirectory && engineClient_ != nullptr) engineClient_->RequestDirectory(fullPath);
}

void ColumnView::NavigateToHierarchy(const std::vector<std::wstring>& segments, bool isDirectory) {
    if (segments.size() < 2) return;
    std::vector<std::wstring> paths;
    std::wstring current = segments.front();
    paths.push_back(current);
    for (size_t index = 1; index + 1 < segments.size(); ++index) {
        if (!current.empty() && current.back() != L'\\') current += L'\\';
        current += segments[index];
        paths.push_back(current);
    }
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        // Search results are opened in the pane that currently owns focus.
        // The old implementation always rebuilt pane 0, so activating a
        // complete result from the right pane silently changed the left pane
        // instead and left the visible right pane unchanged.
        auto& columns = ActiveColumns();
        int& focusedIndex = ActiveFocusedColumnIndex();
        columns.clear();
        for (const auto& path : paths) {
            Column column;
            column.path = path;
            columns.push_back(std::move(column));
        }
        focusedIndex = static_cast<int>(columns.size()) - 1;
        pendingSelectionName_ = segments.back();
        pendingSelectionPath_ = paths.back();
        if (activePane_ == 0) verticalScrollOffset_ = 0.0f;
        else verticalScrollOffset2_ = 0.0f;
    }
    for (const auto& path : paths) if (engineClient_ != nullptr) engineClient_->RequestDirectory(path);
    if (isDirectory && engineClient_ != nullptr) {
        std::wstring folder = paths.back();
        if (folder.back() != L'\\') folder += L'\\';
        engineClient_->RequestDirectory(folder + segments.back());
    }
}

int ColumnView::FocusedItemIndex() const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    const auto& columns = const_cast<ColumnView*>(this)->ActiveColumns();
    const int& focusedIndex = const_cast<ColumnView*>(this)->ActiveFocusedColumnIndex();
    if (focusedIndex < 0 || focusedIndex >= static_cast<int>(columns.size())) return -1;
    return columns[focusedIndex].focusIndex;
}

std::wstring ColumnView::ActivePanePath() const {
    // quick-access-home 4.2: a Home pane has no filesystem path -- returning the
    // empty string keeps every downstream consumer (file ops, reconciliation,
    // address-bar seeding) safe, identical to the "This PC" root.
    if (homePane_[activePane_]) {
        return {};
    }
    std::lock_guard<std::mutex> lock(columnsMutex_);
    const auto& columns = const_cast<ColumnView*>(this)->ActiveColumns();
    const int& focusedIndex = const_cast<ColumnView*>(this)->ActiveFocusedColumnIndex();
    if (focusedIndex < 0 || focusedIndex >= static_cast<int>(columns.size())) {
        return {};
    }
    return columns[focusedIndex].path;
}

// quick-access-home 4.2: data + activation plumbing. These never touch the
// engine; the shell pushes a fresh `HomeData` and owns the activation routing.
void ColumnView::SetHomeData(const HomeData& data) {
    homeData_ = data;
}

void ColumnView::SetHomeActivateCallback(std::function<void(const HomeActivation&)> callback) {
    homeActivateCallback_ = std::move(callback);
}

bool ColumnView::IsPaneHome(int paneIndex) const {
    if (paneIndex < 0 || paneIndex > 1) return false;
    return homePane_[paneIndex];
}

void ColumnView::SetPaneHome(int paneIndex, bool home) {
    if (paneIndex < 0 || paneIndex > 1) return;
    if (home == homePane_[paneIndex]) return;
    homePane_[paneIndex] = home;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        auto& columns = paneIndex == 0 ? columns_ : columns2_;
        int& focusedIndex = paneIndex == 0 ? focusedColumnIndex_ : focusedColumnIndex2_;
        // quick-access-home 4.2: entering Home discards the pane's stale column
        // model so it is never rendered or engine-refreshed while Home is shown.
        columns.clear();
        focusedIndex = 0;
        if (paneIndex == 0) { verticalScrollOffset_ = 0.0f; scrollOffset_ = 0.0f; }
        else { verticalScrollOffset2_ = 0.0f; scrollOffset2_ = 0.0f; }
    }
    if (paneIndex == 0) homeFocusedIndex_ = -1;
    else homeFocusedIndex2_ = -1;
    hoverItem_ = -1;
    hoverColumn_ = -1;
    hoverActive_ = false;
}

void ColumnView::HomeActivateFocused() {
    const int pane = activePane_;
    if (!homePane_[pane]) return;
    const int focused = pane == 0 ? homeFocusedIndex_ : homeFocusedIndex2_;
    if (focused >= 0) {
        ActivateHomeItem(pane, focused, false);
    }
}

// quick-access-home 4.2: rebuild the Home layout for a pane from the current
// `homeData_` and the last known viewport. Pure + cheap; the empty-pinned
// placeholder keeps the Pinned section present (header + hint) so the laid-out
// sections flow below it without colliding and hit-testing stays consistent.
HomeLayoutResult ColumnView::ComputeHomeLayoutForPane(int paneIndex) const {
    // Both panes share identical half-width geometry in dual-pane mode and the
    // full width in single-pane mode, so the layout depends only on the last
    // known viewport, not on which pane index is requested.
    (void)paneIndex;
    HomeLayoutInput input{};
    const float paneWidth = dualPane_ ? (lastViewportWidth_ / 2.0f) : lastViewportWidth_;
    input.viewportWidth = (std::max)(0.0f, paneWidth);
    input.viewportHeight = lastViewportHeight_;
    input.pinnedCount = homeData_.pinned.empty() ? 1 : static_cast<int>(homeData_.pinned.size());
    input.recentFolderCount = static_cast<int>(homeData_.recentFolders.size());
    input.recentSearchCount = static_cast<int>(homeData_.recentSearches.size());
    input.knownFolderCount = static_cast<int>(homeData_.knownFolders.size());
    input.showActions = homeData_.showActions;
    return ComputeHomeLayout(input);
}

// quick-access-home 4.2/4.4: route a Home item activation through the callback.
// The section + position map back to the owning data vector. The empty-pinned
// placeholder (Pinned section, index 0, with `homeData_.pinned` empty) is a
// non-clickable hint and is a no-op here.
void ColumnView::ActivateHomeItem(int paneIndex, int globalIndex, bool openInNewTab) {
    if (homeActivateCallback_ == nullptr) return;
    const HomeLayoutResult layout = ComputeHomeLayoutForPane(paneIndex);
    const HomeItemLayout* found = nullptr;
    for (const auto& item : layout.items) {
        if (item.globalIndex == globalIndex) {
            found = &item;
            break;
        }
    }
    if (found == nullptr) return;

    HomeActivation activation;
    switch (static_cast<HomeSectionKind>(found->sectionIndex)) {
        case HomeSectionKind::Pinned:
            if (homeData_.pinned.empty()) return; // hint placeholder: no-op
            activation.kind = HomeActionKind::Folder;
            activation.value = homeData_.pinned[found->indexInSection].path;
            activation.openInNewTab = openInNewTab;
            break;
        case HomeSectionKind::RecentFolders:
            activation.kind = HomeActionKind::Folder;
            activation.value = homeData_.recentFolders[found->indexInSection];
            activation.openInNewTab = openInNewTab;
            break;
        case HomeSectionKind::RecentSearches:
            activation.kind = HomeActionKind::Search;
            activation.value = homeData_.recentSearches[found->indexInSection];
            activation.openInNewTab = openInNewTab;
            break;
        case HomeSectionKind::CommonLocations:
            activation.kind = HomeActionKind::Folder;
            activation.value = homeData_.knownFolders[found->indexInSection].path;
            activation.openInNewTab = openInNewTab;
            break;
        case HomeSectionKind::QuickActions: {
            // quick-access-home 4.7: New Tab / Search / Open Path / Settings map
            // to existing commands only (design D4).
            static const wchar_t* kCommands[] = {
                L"navigation.new-tab", L"search.focus", L"navigation.focus-path", L"app.settings"};
            if (found->indexInSection < 0 || found->indexInSection >= 4) return;
            activation.kind = HomeActionKind::QuickAction;
            activation.value = kCommands[found->indexInSection];
            break;
        }
    }
    homeActivateCallback_(activation);
}

// quick-access-home 4.5: keyboard model for the Home pane. Up/Down move across
// the flat item list (+-columnCount), Left/Right between the two columns when
// the layout is two-wide, Enter activates (Ctrl+Enter = new tab for
// Folder/Search), Home/End jump to first/last. Escape and all other keys are
// intentionally ignored here so the shell keeps owning them.
void ColumnView::HandleHomeKeyDown(int virtualKey) {
    const int pane = activePane_;
    int& focused = pane == 0 ? homeFocusedIndex_ : homeFocusedIndex2_;
    const HomeLayoutResult layout = ComputeHomeLayoutForPane(pane);
    switch (virtualKey) {
        case VK_UP:
            focused = HomeMoveFocus(layout, focused, 0, -1);
            break;
        case VK_DOWN:
            focused = HomeMoveFocus(layout, focused, 0, 1);
            break;
        case VK_LEFT:
            focused = HomeMoveFocus(layout, focused, -1, 0);
            break;
        case VK_RIGHT:
            focused = HomeMoveFocus(layout, focused, 1, 0);
            break;
        case VK_HOME:
            focused = HomeFirstItem(layout);
            break;
        case VK_END:
            focused = HomeLastItem(layout);
            break;
        case VK_RETURN: {
            const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (focused >= 0) ActivateHomeItem(pane, focused, control);
            break;
        }
        default:
            break;
    }
}

std::wstring ColumnView::RootPath() const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    for (const auto& column : columns_) {
        if (!column.path.empty()) return column.path;
    }
    for (const auto& column : columns2_) {
        if (!column.path.empty()) return column.path;
    }
    return {};
}

void ColumnView::RefreshColumnFromSnapshot(Column& column,
                                           const std::map<std::wstring, ffprotocol::SnapshotDirectory>& snapshot,
                                           uint64_t generation) {
    const auto* directory = FindSnapshotDirectory(snapshot, column.path);
    if (directory == nullptr) {
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                      ffprotocol::DiagnosticCode::FFDIAG_UI_SNAPSHOT_PATH_MISS,
                                      column.path, {}, L"ui-snapshot-path-miss", ERROR_FILE_NOT_FOUND, 0});
        if (column.freshness.HasLastKnownGood()) {
            ApplyColumnFailure(column, SnapshotFailureReason::PathMissing, generation);
        }
        return; // an initial request may not have reached the published view yet
    }
    ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                  ffprotocol::DiagnosticCode::FFDIAG_UI_SNAPSHOT_COLUMN_REFRESH,
                                  column.path, {}, L"ui-snapshot-column-refresh",
                                  0, static_cast<uint64_t>(directory->entries.size())});

    if (directory->status == ffprotocol::DirectoryEnumerationStatus::AccessDenied) {
        ApplyColumnFailure(column, SnapshotFailureReason::AccessDenied, generation);
        return;
    }
    if (directory->status == ffprotocol::DirectoryEnumerationStatus::NotFound) {
        ApplyColumnFailure(column, SnapshotFailureReason::NoLongerAvailable, generation);
        return;
    }
    if (directory->status == ffprotocol::DirectoryEnumerationStatus::IoError) {
        ApplyColumnFailure(column, SnapshotFailureReason::EnumerationFailed, generation);
        return;
    }

    if (!column.freshness.AcceptGeneration(generation)) return;
    std::vector<SelectionIdentity> previousIdentities;
    previousIdentities.reserve(column.items.size());
    for (const auto& item : column.items) {
        previousIdentities.push_back(MakeSelectionIdentity(column.path, item.name, item.volumeRowId,
                                                           item.fileIdLow, item.fileIdHigh));
    }
    const SelectionIndexState previousState{column.selectedIndices, column.selectionAnchor, column.focusIndex};
    const SelectionIdentityState previousSelection =
        CaptureSelectionIdentityState(previousIdentities, previousState);
    const std::wstring pendingName = pendingSelectionName_;
    const std::wstring pendingPath = pendingSelectionPath_;
    bool pendingApplied = false;
    column.items.clear();
    column.items.reserve(directory->entries.size());
    for (const auto& entry : directory->entries) {
        column.items.push_back({entry.name, entry.isDirectory, entry.sizeBytes, entry.attributes,
                                static_cast<uint64_t>(entry.fileIdLow), static_cast<uint64_t>(entry.fileIdHigh),
                                entry.volumeRowId});
    }
    std::sort(column.items.begin(), column.items.end(), [](const ColumnItem& a, const ColumnItem& b) {
        if (a.isDirectory != b.isDirectory) {
            return a.isDirectory > b.isDirectory; // folders first
        }
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    std::vector<SelectionIdentity> refreshedIdentities;
    refreshedIdentities.reserve(column.items.size());
    for (const auto& item : column.items) {
        refreshedIdentities.push_back(MakeSelectionIdentity(column.path, item.name, item.volumeRowId,
                                                            item.fileIdLow, item.fileIdHigh));
    }
    const SelectionIndexState restoredState = RestoreSelectionIndexState(refreshedIdentities, previousSelection);
    column.selectedIndices = restoredState.selectedIndices;
    column.selectionAnchor = restoredState.selectionAnchor;
    column.focusIndex = restoredState.focusIndex;
    if (!pendingName.empty() && MatchesPendingSnapshotPath(column.path, pendingPath)) {
        for (int index = 0; index < static_cast<int>(column.items.size()); ++index) {
            if (_wcsicmp(column.items[index].name.c_str(), pendingName.c_str()) == 0) {
                column.selectedIndices.insert(index);
                column.selectionAnchor = index;
                column.focusIndex = index;
                pendingApplied = true;
                break;
            }
        }
    }
    if (pendingApplied) {
        pendingSelectionName_.clear();
        pendingSelectionPath_.clear();
    }
}

void ColumnView::OnSnapshotUpdated(uint64_t announcedGeneration) {
    if (!engineClient_) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        for (auto& column : columns_) {
            if (!column.path.empty()) column.freshness.AnnounceGeneration(announcedGeneration);
        }
        for (auto& column : columns2_) {
            if (!column.path.empty()) column.freshness.AnnounceGeneration(announcedGeneration);
        }
    }
    auto snapshot = engineClient_->ReadSnapshotWithGeneration();
    if (!snapshot || (announcedGeneration != 0 && snapshot->generation < announcedGeneration)) {
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                      ffprotocol::DiagnosticCode::FFDIAG_UI_SNAPSHOT_READ_FAILED,
                                      {}, {}, L"ui-snapshot-read-failed", ERROR_INVALID_DATA,
                                      announcedGeneration});
        MarkSnapshotLoadFailed(announcedGeneration);
        return;
    }
    std::lock_guard<std::mutex> lock(columnsMutex_);
    for (auto& column : columns_) {
        if (column.path.empty()) {
            continue; // the synthetic root (drive list) column isn't engine-backed
        }
        RefreshColumnFromSnapshot(column, snapshot->directories, snapshot->generation);
    }
    for (auto& column : columns2_) {
        if (column.path.empty()) {
            continue;
        }
        RefreshColumnFromSnapshot(column, snapshot->directories, snapshot->generation);
    }
}

void ColumnView::OnDirectoryError(const std::wstring& path, ffprotocol::DirectoryErrorReason reason) {
    const SnapshotFailureReason failure =
        reason == ffprotocol::DirectoryErrorReason::AccessDenied
            ? SnapshotFailureReason::AccessDenied
            : (reason == ffprotocol::DirectoryErrorReason::PublishFailed
                   ? SnapshotFailureReason::PublishFailed
                   : (reason == ffprotocol::DirectoryErrorReason::EnumerationFailed
                          ? SnapshotFailureReason::EnumerationFailed
                          : SnapshotFailureReason::NoLongerAvailable));
    std::lock_guard<std::mutex> lock(columnsMutex_);
    for (auto& column : columns_) {
        if (ffprotocol::WindowsPathEqual(column.path, path)) {
            ApplyColumnFailure(column, failure);
        }
    }
    for (auto& column : columns2_) {
        if (ffprotocol::WindowsPathEqual(column.path, path)) {
            ApplyColumnFailure(column, failure);
        }
    }
}

void ColumnView::ShowUnavailableLocation(const std::wstring& displayName) {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    Column column;
    column.path = displayName.empty() ? L"Unavailable location" : displayName;
    column.freshness.Fail(SnapshotFailureReason::NoLongerAvailable);
    columns_ = {std::move(column)};
    focusedColumnIndex_ = 0;
    pendingSelectionName_.clear();
    pendingSelectionPath_.clear();
}

void ColumnView::MarkSnapshotLoadFailed(uint64_t generation) {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    for (auto& column : columns_) {
        if (!column.path.empty()) ApplyColumnFailure(column, SnapshotFailureReason::SnapshotLoadFailed, generation);
    }
    for (auto& column : columns2_) {
        if (!column.path.empty()) ApplyColumnFailure(column, SnapshotFailureReason::SnapshotLoadFailed, generation);
    }
}

void ColumnView::MarkPathLoading(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    for (auto& column : columns_) {
        if (ffprotocol::WindowsPathEqual(column.path, path)) column.freshness.BeginRequest();
    }
    for (auto& column : columns2_) {
        if (ffprotocol::WindowsPathEqual(column.path, path)) column.freshness.BeginRequest();
    }
}

void ColumnView::OnEngineConnectionState(EngineConnectionState state) {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    const auto apply = [state](std::vector<Column>& columns) {
        for (auto& column : columns) {
            if (column.path.empty()) continue;
            if (state == EngineConnectionState::Connected) {
                column.freshness.ResetForNewSource();
            } else if (state == EngineConnectionState::Unavailable) {
                ApplyColumnFailure(column, SnapshotFailureReason::Disconnected);
            } else if (column.freshness.HasLastKnownGood()) {
                ApplyColumnFailure(column, SnapshotFailureReason::Disconnected);
            }
        }
    };
    apply(columns_);
    apply(columns2_);
}

void ColumnView::SetEngineStatus(EngineConnectionState state, bool privilegedPathActive) {
    engineState_ = state;
    privilegedPathActive_ = privilegedPathActive;
}

void ColumnView::SetRetryEngineCallback(std::function<void()> retryEngine) {
    retryEngineCallback_ = std::move(retryEngine);
}

void ColumnView::SetDarkTheme(bool dark) {
    if (darkTheme_ != dark) {
        darkTheme_ = dark;
        resourcesCreated_ = false; // recreated together with the next paint
        backgroundBrush_.Reset(); borderBrush_.Reset(); textBrush_.Reset(); textSecondaryBrush_.Reset(); textOnAccentBrush_.Reset();
        selectionBrush_.Reset();
        folderGlyphBrush_.Reset(); fileGlyphBrush_.Reset(); errorBrush_.Reset();
        badgeActiveBrush_.Reset(); badgeActiveTextBrush_.Reset();
        badgeDegradedBrush_.Reset(); badgeDegradedTextBrush_.Reset();
        selectionSoftBrush_.Reset(); dividerBrush_.Reset(); hoverOverlayBrush_.Reset();
        focusStrokeBrush_.Reset(); // quick-access-home 4.3: reset the new brush so it is recreated for the theme
    }
}

float ColumnView::ContentWidth() const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    // quick-access-home 4.x: a Home pane's content fills the pane, not columns.
    if (homePane_[activePane_]) {
        return lastViewportWidth_ > 0.0f
            ? (dualPane_ ? lastViewportWidth_ / 2.0f : lastViewportWidth_)
            : 0.0f;
    }
    const auto& columns = const_cast<ColumnView*>(this)->ActiveColumns();
    return kColumnWidth * static_cast<float>(columns.size());
}

float ColumnView::PaneContentWidth(int paneIndex) const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    if (paneIndex < 0 || paneIndex > 1) return 0.0f;
    // quick-access-home 4.x: a Home pane's content fills the pane, not columns.
    if (homePane_[paneIndex]) {
        return lastViewportWidth_ > 0.0f
            ? (dualPane_ ? lastViewportWidth_ / 2.0f : lastViewportWidth_)
            : 0.0f;
    }
    const auto& columns = paneIndex == 0 ? columns_ : columns2_;
    return kColumnWidth * static_cast<float>(columns.size());
}

int ColumnView::FocusedColumnIndex() const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    const int& focusedIndex = const_cast<ColumnView*>(this)->ActiveFocusedColumnIndex();
    return focusedIndex;
}

std::optional<FileDescriptor> ColumnView::CurrentSelection() const {
    SelectionSummary summary = CurrentSelectionSummary();
    if (summary.items.size() != 1) {
        return std::nullopt;
    }
    return summary.items.front();
}

SelectionSummary ColumnView::CurrentSelectionSummary() const {
    SelectionSummary summary;
    std::lock_guard<std::mutex> lock(columnsMutex_);
    const auto& columns = const_cast<ColumnView*>(this)->ActiveColumns();
    const int& focusedIndex = const_cast<ColumnView*>(this)->ActiveFocusedColumnIndex();
    if (focusedIndex < 0 || focusedIndex >= static_cast<int>(columns.size())) {
        return summary;
    }
    const Column& column = columns[focusedIndex];
    for (int index : column.selectedIndices) {
        if (index < 0 || index >= static_cast<int>(column.items.size())) {
            continue;
        }
        const ColumnItem& item = column.items[index];
        summary.items.push_back({JoinPath(column.path, item.name), item.sizeBytes, item.attributes, item.isDirectory,
                                  item.fileIdLow, item.fileIdHigh, item.volumeRowId});
        if (!item.isDirectory) summary.knownSizeBytes += item.sizeBytes;
    }
    return summary;
}

#ifdef _DEBUG
SelectionDiagnostics ColumnView::CurrentSelectionDiagnostics() const {
    SelectionDiagnostics diagnostics;
    const std::vector<std::wstring> operationTargets = ActiveSelectionPaths();
    if (!operationTargets.empty()) diagnostics.operationTarget = operationTargets.front();
    std::lock_guard<std::mutex> lock(columnsMutex_);
    const auto& columns = const_cast<ColumnView*>(this)->ActiveColumns();
    const int& focusedIndex = const_cast<ColumnView*>(this)->ActiveFocusedColumnIndex();
    if (focusedIndex < 0 || focusedIndex >= static_cast<int>(columns.size())) {
        return diagnostics;
    }

    const Column& column = columns[focusedIndex];
    diagnostics.activePath = column.path;
    diagnostics.visibleItemNames.reserve(column.items.size());
    for (const auto& item : column.items) diagnostics.visibleItemNames.push_back(item.name);
    const auto makeIdentity = [&column](int itemIndex) {
        const ColumnItem& item = column.items[static_cast<size_t>(itemIndex)];
        return MakeSelectionIdentity(column.path, item.name, item.volumeRowId,
                                     item.fileIdLow, item.fileIdHigh);
    };
    for (const int index : column.selectedIndices) {
        if (index >= 0 && index < static_cast<int>(column.items.size())) {
            diagnostics.selectedIndex = index;
            diagnostics.selectedIdentity = makeIdentity(index);
            break;
        }
    }
    if (column.focusIndex >= 0 && column.focusIndex < static_cast<int>(column.items.size())) {
        diagnostics.focusedIndex = column.focusIndex;
        diagnostics.focusedIdentity = makeIdentity(column.focusIndex);
    }
    return diagnostics;
}
#endif

std::wstring ColumnView::CurrentPath() const {
    const std::wstring path = ActivePanePath();
    return path.empty() ? L"Computer" : path;
}

void ColumnView::EnsureCreated(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory) {
    if (resourcesCreated_) {
        return;
    }
    const ffui::UiTheme theme = ffui::GetUiTheme(darkTheme_);
    context->CreateSolidColorBrush(theme.background, &backgroundBrush_);
    context->CreateSolidColorBrush(theme.border, &borderBrush_);
    context->CreateSolidColorBrush(theme.text, &textBrush_);
    context->CreateSolidColorBrush(theme.textSecondary, &textSecondaryBrush_);
    context->CreateSolidColorBrush(theme.textOnAccent, &textOnAccentBrush_);
    context->CreateSolidColorBrush(theme.accent, &selectionBrush_);
    context->CreateSolidColorBrush(theme.folderGlyph, &folderGlyphBrush_);
    context->CreateSolidColorBrush(theme.fileGlyph, &fileGlyphBrush_);
    context->CreateSolidColorBrush(theme.error, &errorBrush_);
    context->CreateSolidColorBrush(theme.badgeActiveBg, &badgeActiveBrush_);
    context->CreateSolidColorBrush(theme.badgeActiveText, &badgeActiveTextBrush_);
    context->CreateSolidColorBrush(theme.badgeDegradedBg, &badgeDegradedBrush_);
    context->CreateSolidColorBrush(theme.badgeDegradedText, &badgeDegradedTextBrush_);
    context->CreateSolidColorBrush(theme.selectionSoft, &selectionSoftBrush_);
    context->CreateSolidColorBrush(theme.dividerSubtle, &dividerBrush_);
    context->CreateSolidColorBrush(theme.hoverOverlay, &hoverOverlayBrush_);
    // quick-access-home 4.3: first consumer of the previously-unused focusStroke token.
    context->CreateSolidColorBrush(theme.focusStroke, &focusStrokeBrush_);

    dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        14.0f, L"en-us", &textFormat_);
    textFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        12.0f, L"en-us", &badgeTextFormat_);

    resourcesCreated_ = true;
}

void ColumnView::Render(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory, D2D1_SIZE_F viewportSize, float scrollOffset, float scrollOffset2) {
    EnsureCreated(context, dwriteFactory);

    // quick-access-home 4.x: remember the viewport so input handlers (which
    // receive no geometry) can rebuild the Home layout for hit-testing / focus.
    lastViewportWidth_ = viewportSize.width;
    lastViewportHeight_ = viewportSize.height;

    const ffui::UiTheme theme = ffui::GetUiTheme(darkTheme_);
    context->Clear(theme.background);

    // recovery-degraded-browsing D3: engine-connection-status text as a
    // rounded chip. Paint-only except for the Unavailable state, where a
    // click retries the engine connection (see OnMouseDown).
    D2D1_RECT_F badgeRect = D2D1::RectF(8.0f, 4.0f, viewportSize.width - 8.0f, kBadgeHeight - 4.0f);
    const EngineConnectionState connectionState = engineState_.load();
    const bool privilegedActive = privilegedPathActive_.load();
    const EngineStatusPresentation statusPresentation =
        GetEngineStatusPresentation(connectionState, privilegedActive);
    ID2D1SolidColorBrush* badgeBrush = badgeDegradedBrush_.Get();
    ID2D1SolidColorBrush* badgeTextBrush = badgeDegradedTextBrush_.Get();
    if (statusPresentation.privilegedPathActive) {
        badgeBrush = badgeActiveBrush_.Get();
        badgeTextBrush = badgeActiveTextBrush_.Get();
    } else if (connectionState == EngineConnectionState::Connected) {
        badgeBrush = badgeDegradedBrush_.Get();
        badgeTextBrush = badgeDegradedTextBrush_.Get();
    }
    ffui::UiFillRoundedRect(context, badgeRect, badgeBrush, ffui::UiMetrics::kRadiusSmall);
    D2D1_RECT_F badgeTextRect = D2D1::RectF(16.0f, 4.0f, viewportSize.width - 16.0f, kBadgeHeight - 4.0f);
    context->DrawText(statusPresentation.text, static_cast<UINT32>(wcslen(statusPresentation.text)),
                      badgeTextFormat_.Get(), badgeTextRect,
                      badgeTextBrush);

    // High Contrast: drive every selection through the system highlight
    // (UiEnsureSolidBrush compares colors and reuses the cached brush) and
    // suppress the token hover overlay in the row loop below.
    const bool highContrast = ffui::UiSystemHighContrast();
    if (highContrast) {
        ffui::UiEnsureSolidBrush(context, ffui::ToD2DColor(GetSysColor(COLOR_HIGHLIGHT)), &selectionBrush_);
    }

    const uint64_t nowMs = static_cast<uint64_t>(GetTickCount64());
    // Task 3.4: there is a single hover target, so the fade advances once per
    // frame; the shell only repaints while HoverAnimating() holds.
    hoverOpacity_.Tick(nowMs);
    // quick-access-home 4.3: hoisted to Render scope so both the column and
    // Home render lambdas can read the lingering fade-out window.
    const bool lingerFade = hoverLeaveMs_ != 0 &&
        (nowMs - hoverLeaveMs_) < static_cast<uint64_t>(kUiAnimationDefaultMs);

    std::lock_guard<std::mutex> lock(columnsMutex_);

    auto RenderPane = [&](const std::vector<Column>& columns, float paneX, float paneWidth, float paneScroll,
                          float verticalScrollOffset, bool isActivePane) {
        for (int i = 0; i < static_cast<int>(columns.size()); ++i) {
            const float x = paneX + i * kColumnWidth - paneScroll;
            if (x + kColumnWidth < paneX || x > paneX + paneWidth) {
                continue; // culled
            }
            const Column& column = columns[i];

            D2D1_RECT_F columnRect = D2D1::RectF(x, kBadgeHeight, x + kColumnWidth, viewportSize.height);
            context->FillRectangle(columnRect, backgroundBrush_.Get());
            // Task 3.6: subtle 1-DIP hairline between columns.
            context->DrawLine(D2D1_POINT_2F{x + kColumnWidth, kBadgeHeight}, D2D1_POINT_2F{x + kColumnWidth, viewportSize.height},
                              dividerBrush_.Get(), 1.0f);

            const auto unavailableMessage = [&column]() {
                switch (column.freshness.failure) {
                    case SnapshotFailureReason::AccessDenied:
                        return L"You don't have permission to view this folder.";
                    case SnapshotFailureReason::PublishFailed:
                        return L"Unable to load this location — snapshot publication failed.";
                    case SnapshotFailureReason::EnumerationFailed:
                        return L"Unable to load this location — the folder could not be read.";
                    case SnapshotFailureReason::SnapshotLoadFailed:
                        return L"Unable to load this location — snapshot data could not be read.";
                    case SnapshotFailureReason::Disconnected:
                        return L"Unable to load this location — the engine is unavailable.";
                    case SnapshotFailureReason::PathMissing:
                    case SnapshotFailureReason::NoLongerAvailable:
                        return L"This folder is no longer available.";
                    case SnapshotFailureReason::None:
                        break;
                }
                return L"Unable to load this location.";
            };
            const auto statusMessage = [&column]() {
                if (column.freshness.state == SnapshotViewState::Loading) return L"Refreshing…";
                if (column.freshness.failure == SnapshotFailureReason::Disconnected) {
                    return L"Showing previous results — engine disconnected.";
                }
                if (column.freshness.failure == SnapshotFailureReason::PublishFailed) {
                    return L"Showing previous results — refresh failed (snapshot publication failed).";
                }
                return L"Showing previous results — refresh failed.";
            };
            if (column.freshness.state == SnapshotViewState::Unavailable) {
                const wchar_t* message = unavailableMessage();
                D2D1_RECT_F messageRect = D2D1::RectF(x + 8, kBadgeHeight + 8, x + kColumnWidth - 8, viewportSize.height - 8);
                context->DrawText(message, static_cast<UINT32>(wcslen(message)), textFormat_.Get(), messageRect, errorBrush_.Get());
                continue;
            }

            float contentTop = kBadgeHeight;
            if (column.freshness.state == SnapshotViewState::Loading ||
                column.freshness.state == SnapshotViewState::StaleWithError) {
                const wchar_t* message = statusMessage();
                D2D1_RECT_F statusRect = D2D1::RectF(x + 8, kBadgeHeight + 4, x + kColumnWidth - 8,
                                                     kBadgeHeight + 24);
                context->DrawText(message, static_cast<UINT32>(wcslen(message)), textFormat_.Get(), statusRect,
                                  column.freshness.state == SnapshotViewState::StaleWithError
                                      ? errorBrush_.Get() : textSecondaryBrush_.Get());
                contentTop += 24.0f;
            }

            if (column.items.empty()) {
                if (column.freshness.state != SnapshotViewState::Current) continue;
                const wchar_t* emptyMessage = L"This folder is empty";
                D2D1_RECT_F messageRect = D2D1::RectF(x + 8, contentTop + 8, x + kColumnWidth - 8, viewportSize.height - 8);
                context->DrawText(emptyMessage, static_cast<UINT32>(wcslen(emptyMessage)), textFormat_.Get(), messageRect, textSecondaryBrush_.Get());
                continue;
            }

            for (int r = 0; r < static_cast<int>(column.items.size()); ++r) {
                const float y = contentTop + r * kRowHeight - verticalScrollOffset;
                if (y + kRowHeight < contentTop || y > viewportSize.height) {
                    continue;
                }

                const ColumnItem& item = column.items[r];
                const bool isSelected = column.selectedIndices.contains(r);
                const bool isFocusedColumn = (i == (activePane_ == 0 ? focusedColumnIndex_ : focusedColumnIndex2_));
                // Task 3.4: hover tracks the active pane only; the lingering
                // fade-out window (150 ms after OnMouseLeave) keeps the pill
                // visible while it dims. Overlay is skipped in High Contrast.
                // (lingerFade is hoisted to Render scope and shared with the
                // Home render lambda.)
                const bool hoverRow = isActivePane && hoverColumn_ == i && hoverItem_ == r && !isSelected && (hoverActive_ || lingerFade);
                if (hoverRow && !highContrast) {
                    const float alpha = hoverOpacity_.Value();
                    if (alpha > 0.001f) {
                        D2D1_RECT_F hoverRect = D2D1::RectF(x + 4, y + 2, x + kColumnWidth - 4, y + kRowHeight - 2);
                        D2D1_COLOR_F transparentOverlay = theme.hoverOverlay;
                        transparentOverlay.a = 0.0f;
                        const D2D1_COLOR_F overlayColor = ffui::UiLerpColor(transparentOverlay, theme.hoverOverlay, alpha);
                        ffui::UiFillHoverOverlay(context, hoverRect, ffui::UiMetrics::kRadiusSmall, overlayColor, &hoverOverlayBrush_);
                    }
                }

                // Task 3.3: rounded selection pill; the focused column uses the
                // full accent (text flips to textOnAccent), elsewhere the soft
                // translucent fill keeps body text legible.
                if (isSelected) {
                    D2D1_RECT_F selectionRect = D2D1::RectF(x + 4, y + 2, x + kColumnWidth - 4, y + kRowHeight - 2);
                    ID2D1SolidColorBrush* selectionBrush = (highContrast || isFocusedColumn) ? selectionBrush_.Get() : selectionSoftBrush_.Get();
                    ffui::UiFillRoundedRect(context, selectionRect, selectionBrush, ffui::UiMetrics::kRadiusSmall);
                }

                // Task 3.2: type icon from the shared cache; the themed glyph
                // rectangle stays as the placeholder until the bitmap arrives.
                std::wstring iconKey = item.isDirectory ? std::wstring(ffui::FolderKey())
                                                        : ffui::IconKeyForExtension(FileExtensionOf(item.name));
                if (iconCache_ != nullptr) {
                    iconCache_->Prefetch(iconKey); // cheap: dedupes against cache/pending/queued
                }
                ID2D1Bitmap1* icon = nullptr;
                if (iconCache_ != nullptr && iconCache_->Get(context, iconKey, &icon) && icon != nullptr) {
                    const float iconY = y + (kRowHeight - ffui::UiMetrics::kIconSize) / 2.0f;
                    context->DrawBitmap(icon,
                                        D2D1::RectF(x + 8, iconY, x + 8 + ffui::UiMetrics::kIconSize, iconY + ffui::UiMetrics::kIconSize),
                                        1.0f, D2D1_INTERPOLATION_MODE_LINEAR);
                } else {
                    D2D1_RECT_F glyphRect = D2D1::RectF(x + 8, y + kRowHeight / 2 - 4, x + 16, y + kRowHeight / 2 + 4);
                    context->FillRectangle(glyphRect, item.isDirectory ? folderGlyphBrush_.Get() : fileGlyphBrush_.Get());
                }

                std::wstring label = item.name;
                if (item.isDirectory && (label.empty() || label.back() != L'\\')) {
                    label += L'\\';
                }
                D2D1_RECT_F textRect = D2D1::RectF(x + 24, y, x + kColumnWidth - 4, y + kRowHeight);
                // White text on the accent selection for contrast in both
                // themes; soft (unfocused) selections keep the normal text
                // brush so body text stays legible on the translucent fill.
                context->DrawText(label.c_str(), static_cast<UINT32>(label.size()), textFormat_.Get(), textRect,
                                  isSelected && isFocusedColumn ? textOnAccentBrush_.Get() : textBrush_.Get());
                // Task 3.3: folder chevron affordance at the right edge.
                if (item.isDirectory) {
                    D2D1_RECT_F chevronRect = D2D1::RectF(x + kColumnWidth - 16, y, x + kColumnWidth - 4, y + kRowHeight);
                    context->DrawText(L"\u203A", 1, textFormat_.Get(), chevronRect, textSecondaryBrush_.Get());
                }
            }
        }
    };

    // quick-access-home 4.3: render the Home surface for one pane. Drawn in the
    // pane's rect (pane-local origin already removed via `paneX`), starting
    // below the status badge like the column surface. The vertical scroll offset
    // is applied so long lists scroll; a clip rect keeps content out of the
    // badge band and off the dual-pane divider. High Contrast suppresses the
    // token hover overlay and the focus ring and uses the system highlight.
    auto RenderHomePane = [&](int paneIndex, float paneX, float paneWidth, float viewportHeight) {
        if (!homePane_[paneIndex]) return;
        const float vso = paneIndex == 0 ? verticalScrollOffset_ : verticalScrollOffset2_;
        const bool isActivePane = (activePane_ == paneIndex);
        const int focusedItem = paneIndex == 0 ? homeFocusedIndex_ : homeFocusedIndex2_;

        HomeLayoutInput input{};
        input.viewportWidth = (std::max)(0.0f, paneWidth);
        input.viewportHeight = viewportHeight;
        // Empty `pinned` is surfaced as one placeholder item so the Pinned
        // section (header + "Pin folders to see them here." hint) still renders
        // before the real sections, keeping hit-testing consistent.
        input.pinnedCount = homeData_.pinned.empty() ? 1 : static_cast<int>(homeData_.pinned.size());
        input.recentFolderCount = static_cast<int>(homeData_.recentFolders.size());
        input.recentSearchCount = static_cast<int>(homeData_.recentSearches.size());
        input.knownFolderCount = static_cast<int>(homeData_.knownFolders.size());
        input.showActions = homeData_.showActions;
        const HomeLayoutResult layout = ComputeHomeLayout(input);

        static const wchar_t* kSectionTitles[] = {
            L"Pinned", L"Recent folders", L"Recent searches", L"Common locations", L"Quick actions"};

        // Clip to the pane minus the badge band.
        D2D1_RECT_F clip = D2D1::RectF(paneX, kBadgeHeight, paneX + paneWidth, viewportSize.height);
        context->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);

        // Section headers (drawn above each section's items). Recent folders /
        // Recent searches additionally expose a right-aligned "Clear"
        // affordance whose hit rect is recorded for OnMouseDown.
        homeClearButtons_[paneIndex] = {};
        for (const auto& section : layout.sections) {
            const float hx = paneX + section.headerX;
            const float hy = kBadgeHeight + section.headerY - vso;
            const wchar_t* title = (section.sectionIndex >= 0 && section.sectionIndex <= 4)
                ? kSectionTitles[section.sectionIndex] : L"";
            D2D1_RECT_F hdrRect = D2D1::RectF(hx, hy, hx + section.headerWidth, hy + section.headerHeight);
            context->DrawText(title, static_cast<UINT32>(wcslen(title)), badgeTextFormat_.Get(), hdrRect,
                              textSecondaryBrush_.Get());
            if (section.sectionIndex == static_cast<int>(HomeSectionKind::RecentFolders) ||
                section.sectionIndex == static_cast<int>(HomeSectionKind::RecentSearches)) {
                const float clearWidth = 44.0f;
                const D2D1_RECT_F clearRect = D2D1::RectF(hx + section.headerWidth - clearWidth, hy,
                                                          hx + section.headerWidth, hy + section.headerHeight);
                context->DrawText(L"Clear", 5, badgeTextFormat_.Get(), clearRect, textSecondaryBrush_.Get());
                if (section.sectionIndex == static_cast<int>(HomeSectionKind::RecentFolders)) {
                    homeClearButtons_[paneIndex].recentFolders = clearRect;
                } else {
                    homeClearButtons_[paneIndex].recentSearches = clearRect;
                }
            }
        }

        for (const auto& item : layout.items) {
            const float x = paneX + item.x;
            const float y = kBadgeHeight + item.y - vso;
            const float w = item.width;
            const float h = item.height;
            const bool isFocused = isActivePane && item.globalIndex == focusedItem;

            std::wstring primary;
            std::wstring secondary;
            std::wstring glyph;
            bool isFolderIcon = false;
            switch (static_cast<HomeSectionKind>(item.sectionIndex)) {
                case HomeSectionKind::Pinned:
                    if (homeData_.pinned.empty()) {
                        primary = L"Pin folders to see them here."; // hint placeholder
                    } else {
                        const auto& b = homeData_.pinned[item.indexInSection];
                        primary = b.displayName.empty() ? b.path : b.displayName;
                        secondary = b.path;
                        isFolderIcon = true;
                    }
                    break;
                case HomeSectionKind::RecentFolders:
                    primary = homeData_.recentFolders[item.indexInSection];
                    isFolderIcon = true;
                    break;
                case HomeSectionKind::RecentSearches:
                    primary = homeData_.recentSearches[item.indexInSection];
                    glyph = L"\uE721"; // magnifier
                    break;
                case HomeSectionKind::CommonLocations: {
                    const auto& b = homeData_.knownFolders[item.indexInSection];
                    primary = b.displayName.empty() ? b.path : b.displayName;
                    secondary = b.path;
                    isFolderIcon = true;
                    break;
                }
                case HomeSectionKind::QuickActions: {
                    static const wchar_t* kLabels[] = {L"New Tab", L"Search", L"Open Path", L"Settings"};
                    static const wchar_t* kGlyphs[] = {L"\uE8A7", L"\uE721", L"\uE8B7", L"\uE713"};
                    if (item.indexInSection >= 0 && item.indexInSection < 4) {
                        primary = kLabels[item.indexInSection];
                        glyph = kGlyphs[item.indexInSection];
                    }
                    break;
                }
            }

            const bool hoverRow = isActivePane && hoverItem_ == item.globalIndex && hoverColumn_ == -1 &&
                                  (hoverActive_ || lingerFade) && !highContrast;

            // Focused fill (selection pill equivalent). High Contrast reuses the
            // system highlight brush set up above; otherwise the soft fill.
            if (isFocused) {
                D2D1_RECT_F fillRect = D2D1::RectF(x + 2.0f, y + 2.0f, x + w - 2.0f, y + h - 2.0f);
                ID2D1SolidColorBrush* fillBrush = highContrast ? selectionBrush_.Get() : selectionSoftBrush_.Get();
                ffui::UiFillRoundedRect(context, fillRect, fillBrush, kHomeRadiusMedium);
            }

            // Hover overlay (token overlay, suppressed in High Contrast).
            if (hoverRow) {
                const float alpha = hoverOpacity_.Value();
                if (alpha > 0.001f) {
                    D2D1_RECT_F hoverRect = D2D1::RectF(x + 2.0f, y + 2.0f, x + w - 2.0f, y + h - 2.0f);
                    D2D1_COLOR_F transparentOverlay = theme.hoverOverlay;
                    transparentOverlay.a = 0.0f;
                    const D2D1_COLOR_F overlayColor = ffui::UiLerpColor(transparentOverlay, theme.hoverOverlay, alpha);
                    ffui::UiFillHoverOverlay(context, hoverRect, kHomeRadiusMedium, overlayColor, &hoverOverlayBrush_);
                }
            }

            // Focus ring: first consumer of the theme.focusStroke token; suppressed
            // in High Contrast (the filled pill already signals focus there).
            if (isFocused && !highContrast && focusStrokeBrush_ != nullptr) {
                D2D1_ROUNDED_RECT fr{D2D1::RectF(x + 1.5f, y + 1.5f, x + w - 1.5f, y + h - 1.5f),
                                     kHomeRadiusMedium, kHomeRadiusMedium};
                context->DrawRoundedRectangle(&fr, focusStrokeBrush_.Get(), 1.0f);
            }

            // Icon / glyph + text. Folder items use the shared IconCache
            // (Prefetch is cheap); everything else is a best-effort glyph char
            // drawn in the existing font (label remains the source of truth).
            float textX = x + 8.0f;
            if (isFolderIcon) {
                if (iconCache_ != nullptr) {
                    iconCache_->Prefetch(ffui::FolderKey());
                }
                ID2D1Bitmap1* icon = nullptr;
                if (iconCache_ != nullptr && iconCache_->Get(context, ffui::FolderKey(), &icon) && icon != nullptr) {
                    const float iconY = y + (h - ffui::UiMetrics::kIconSize) / 2.0f;
                    context->DrawBitmap(icon,
                        D2D1::RectF(x + 8.0f, iconY, x + 8.0f + ffui::UiMetrics::kIconSize, iconY + ffui::UiMetrics::kIconSize),
                        1.0f, D2D1_INTERPOLATION_MODE_LINEAR);
                } else {
                    D2D1_RECT_F glyphRect = D2D1::RectF(x + 8.0f, y + h / 2.0f - 4.0f, x + 16.0f, y + h / 2.0f + 4.0f);
                    context->FillRectangle(glyphRect, folderGlyphBrush_.Get());
                }
                textX = x + 32.0f;
            } else if (!glyph.empty()) {
                D2D1_RECT_F glyphRect = D2D1::RectF(x + 8.0f, y, x + 32.0f, y + h);
                context->DrawText(glyph.c_str(), 1, textFormat_.Get(), glyphRect, textSecondaryBrush_.Get());
                textX = x + 32.0f;
            }

            if (!primary.empty()) {
                const float halfH = y + h / 2.0f;
                D2D1_RECT_F primaryRect = D2D1::RectF(textX, y + 2.0f, x + w - 8.0f,
                                                      secondary.empty() ? y + h - 2.0f : halfH);
                context->DrawText(primary.c_str(), static_cast<UINT32>(primary.size()), textFormat_.Get(),
                                  primaryRect, textBrush_.Get());
            }
            if (!secondary.empty()) {
                D2D1_RECT_F secondaryRect = D2D1::RectF(textX, y + h / 2.0f, x + w - 8.0f, y + h - 2.0f);
                context->DrawText(secondary.c_str(), static_cast<UINT32>(secondary.size()), textFormat_.Get(),
                                  secondaryRect, textSecondaryBrush_.Get());
            }
        }

        context->PopAxisAlignedClip();
    };

    if (dualPane_) {
        const float paneWidth = viewportSize.width / 2.0f;
        if (homePane_[0]) RenderHomePane(0, 0.0f, paneWidth, viewportSize.height);
        else RenderPane(columns_, 0.0f, paneWidth, scrollOffset, verticalScrollOffset_, activePane_ == 0);
        // Keep the dual-pane divider regardless of which surface each pane shows.
        context->DrawLine(D2D1_POINT_2F{paneWidth, kBadgeHeight}, D2D1_POINT_2F{paneWidth, viewportSize.height},
                           borderBrush_.Get(), 2.0f);
        if (homePane_[1]) RenderHomePane(1, paneWidth, paneWidth, viewportSize.height);
        else RenderPane(columns2_, paneWidth, paneWidth, scrollOffset2, verticalScrollOffset2_, activePane_ == 1);
    } else {
        if (homePane_[0]) RenderHomePane(0, 0.0f, viewportSize.width, viewportSize.height);
        else RenderPane(columns_, 0.0f, viewportSize.width, scrollOffset, verticalScrollOffset_, true);
    }
}

void ColumnView::SetDualPane(bool enabled) {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    if (dualPane_ == enabled) return;
    if (enabled) {
        // NavigationWorkspace creates the second context at the current
        // location. Clone the visible column model so the new pane is useful
        // immediately instead of presenting an empty surface until another
        // navigation event occurs.
        // quick-access-home 4.2: if pane 0 is Home, do NOT clone its (empty)
        // column model into pane 2 -- the shell sets pane 2's state right after.
        if (homePane_[0]) {
            columns2_.clear();
        } else {
            columns2_ = columns_;
        }
        focusedColumnIndex2_ = focusedColumnIndex_;
        scrollOffset2_ = scrollOffset_;
        verticalScrollOffset2_ = verticalScrollOffset_;
    }
    dualPane_ = enabled;
    if (!enabled) {
        // NavigationWorkspace keeps whichever pane was active when the user
        // disabled dual-pane mode. Promote that pane's rendered model to the
        // single-pane slot instead of silently reverting to the old left pane.
        if (activePane_ == 1) {
            columns_ = std::move(columns2_);
            focusedColumnIndex_ = focusedColumnIndex2_;
            scrollOffset_ = scrollOffset2_;
            verticalScrollOffset_ = verticalScrollOffset2_;
        }
        columns2_.clear();
        activePane_ = 0;
    }
}

void ColumnView::ActivatePane(int paneIndex) {
    if (paneIndex == 0 || (dualPane_ && paneIndex == 1)) {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        activePane_ = paneIndex;
    }
}

std::vector<Column>& ColumnView::ActiveColumns() {
    return activePane_ == 0 ? columns_ : columns2_;
}

const std::vector<Column>& ColumnView::ActiveColumns() const {
    return activePane_ == 0 ? columns_ : columns2_;
}

int& ColumnView::ActiveFocusedColumnIndex() {
    return activePane_ == 0 ? focusedColumnIndex_ : focusedColumnIndex2_;
}

float& ColumnView::ActiveScrollOffset() {
    return activePane_ == 0 ? scrollOffset_ : scrollOffset2_;
}

void ColumnView::SaveActivePaneState(std::vector<std::wstring>& outColumnPaths, int& outFocusedIndex, float& outScrollOffset) const {
    SavePaneState(activePane_, outColumnPaths, outFocusedIndex, outScrollOffset);
}

void ColumnView::SavePaneState(int paneIndex, std::vector<std::wstring>& outColumnPaths,
                               int& outFocusedIndex, float& outScrollOffset) const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    // quick-access-home 4.2: a Home pane has no column model to persist, so the
    // shell treats an empty result as "re-navigate from currentPath" (which for
    // Home means staying Home). Stale column paths must never persist.
    if (paneIndex < 0 || paneIndex > 1 || (paneIndex == 1 && !dualPane_) || homePane_[paneIndex]) {
        outColumnPaths.clear();
        outFocusedIndex = 0;
        outScrollOffset = 0.0f;
        return;
    }
    const auto& columns = paneIndex == 0 ? columns_ : columns2_;
    const int focusedIndex = paneIndex == 0 ? focusedColumnIndex_ : focusedColumnIndex2_;
    const float scrollOffset = paneIndex == 0 ? scrollOffset_ : scrollOffset2_;
    outColumnPaths.clear();
    outColumnPaths.reserve(columns.size());
    for (const auto& column : columns) {
        outColumnPaths.push_back(column.path);
    }
    outFocusedIndex = focusedIndex;
    outScrollOffset = scrollOffset;
}

void ColumnView::RestoreActivePaneState(const std::vector<std::wstring>& columnPaths, int focusedIndex, float scrollOffset) {
    RestorePaneState(activePane_, columnPaths, focusedIndex, scrollOffset);
}

void ColumnView::RestorePaneState(int paneIndex, const std::vector<std::wstring>& columnPaths,
                                  int focusedIndex, float scrollOffset) {
    if (columnPaths.empty()) return;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        // quick-access-home 4.2: ignore restore for Home panes; the shell sets
        // Home state separately via SetPaneHome, so no stale columns reappear.
        if (paneIndex < 0 || paneIndex > 1 || (paneIndex == 1 && !dualPane_) || homePane_[paneIndex]) return;
        auto& columns = paneIndex == 0 ? columns_ : columns2_;
        int& focusedIndexRef = paneIndex == 0 ? focusedColumnIndex_ : focusedColumnIndex2_;
        float& scrollOffsetRef = paneIndex == 0 ? scrollOffset_ : scrollOffset2_;
        columns.clear();
        for (const auto& path : columnPaths) {
            Column column;
            column.path = path;
            columns.push_back(std::move(column));
        }
        focusedIndexRef = std::clamp(focusedIndex, 0, static_cast<int>(columns.size()) - 1);
        scrollOffsetRef = scrollOffset;
    }
    for (const auto& path : columnPaths) {
        if (!path.empty() && engineClient_ != nullptr) engineClient_->RequestDirectory(path);
    }
}

} // namespace ffui
