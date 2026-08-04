#include "ColumnView.h"
#include "SelectionModel.h"
#include "UITheme.h"
#include "UiAnimation.h"
#include "UiStyle.h"
#include "IconCache.h"

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

} // namespace

void ColumnView::Initialize(EngineClient* engineClient) {
    engineClient_ = engineClient;

    std::lock_guard<std::mutex> lock(columnsMutex_);
    Column root;
    root.path = L""; // sentinel: not engine-backed, populated directly below

    const DWORD driveMask = GetLogicalDrives();
    for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
        if ((driveMask & (1u << (letter - L'A'))) == 0) {
            continue;
        }
        wchar_t rootPath[] = {letter, L':', L'\\', L'\0'};
        if (GetDriveTypeW(rootPath) != DRIVE_FIXED) {
            continue;
        }
        ColumnItem item;
        item.name = rootPath;
        item.isDirectory = true;
        root.items.push_back(std::move(item));
    }

    columns_.push_back(std::move(root));
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
        return;
    }
    
    if (dualPane_) {
        const float paneWidth = viewportWidth / 2.0f;
        const int clickedPane = clientPoint.x < paneWidth ? 0 : 1;
        if (clickedPane != activePane_) {
            ActivatePane(clickedPane);
        }
    }
    
    const float effectiveScrollOffset = activePane_ == 0 ? scrollOffset : scrollOffset2_;
    const int columnIndex = static_cast<int>((clientPoint.x + effectiveScrollOffset) / kColumnWidth);
    const int itemIndex = static_cast<int>((clientPoint.y - kBadgeHeight) / kRowHeight);
    ActivateItem(columnIndex, itemIndex, control, shift);
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
    const float effectiveScrollOffset = activePane_ == 0 ? scrollOffset : scrollOffset2_;
    hoverColumn_ = static_cast<int>((clientPoint.x + effectiveScrollOffset) / kColumnWidth);
    hoverItem_ = static_cast<int>((clientPoint.y - kBadgeHeight) / kRowHeight);
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
    if (!path.empty() && engineClient_ != nullptr) engineClient_->RequestDirectory(path);
}

void ColumnView::NavigateToPath(const std::wstring& path, const std::wstring& selectName) {
    if (path.empty()) return;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        auto& columns = ActiveColumns();
        int& focusedIndex = ActiveFocusedColumnIndex();
        columns.clear();
        Column column;
        column.path = path;
        columns.push_back(std::move(column));
        focusedIndex = 0;
        pendingSelectionName_ = selectName;
    }
    if (engineClient_ != nullptr) engineClient_->RequestDirectory(path);
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
        columns_.clear();
        for (const auto& path : paths) {
            Column column;
            column.path = path;
            columns_.push_back(std::move(column));
        }
        focusedColumnIndex_ = static_cast<int>(columns_.size()) - 1;
        pendingSelectionName_ = segments.back();
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
    std::lock_guard<std::mutex> lock(columnsMutex_);
    const auto& columns = const_cast<ColumnView*>(this)->ActiveColumns();
    const int& focusedIndex = const_cast<ColumnView*>(this)->ActiveFocusedColumnIndex();
    if (focusedIndex < 0 || focusedIndex >= static_cast<int>(columns.size())) {
        return {};
    }
    return columns[focusedIndex].path;
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

void ColumnView::RefreshColumnFromSnapshot(Column& column, const std::map<std::wstring, ffprotocol::SnapshotDirectory>& snapshot) {
    auto it = snapshot.find(column.path);
    if (it == snapshot.end()) {
        return; // not published yet
    }
    const auto& directory = it->second;

    if (directory.status == ffprotocol::DirectoryEnumerationStatus::AccessDenied) {
        column.error = ColumnErrorState::AccessDenied;
        column.items.clear();
        return;
    }
    if (directory.status == ffprotocol::DirectoryEnumerationStatus::NotFound) {
        column.error = ColumnErrorState::NoLongerAvailable;
        column.items.clear();
        return;
    }

    column.error = ColumnErrorState::None;
    const std::set<int> oldSelection = column.selectedIndices;
    column.items.clear();
    column.items.reserve(directory.entries.size());
    for (const auto& entry : directory.entries) {
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
    if (!pendingSelectionName_.empty()) {
        for (int index = 0; index < static_cast<int>(column.items.size()); ++index) {
            if (_wcsicmp(column.items[index].name.c_str(), pendingSelectionName_.c_str()) == 0) {
                SelectSingle(column, index);
                break;
            }
        }
        pendingSelectionName_.clear();
    }
    column.selectedIndices.clear();
    for (int index : oldSelection) {
        if (index >= 0 && index < static_cast<int>(column.items.size())) {
            column.selectedIndices.insert(index);
        }
    }
    if (column.focusIndex >= static_cast<int>(column.items.size())) {
        column.focusIndex = -1;
    }
}

void ColumnView::OnSnapshotUpdated() {
    if (!engineClient_) {
        return;
    }
    auto snapshot = engineClient_->ReadSnapshot();
    if (!snapshot) {
        return;
    }
    std::lock_guard<std::mutex> lock(columnsMutex_);
    for (auto& column : columns_) {
        if (column.path.empty()) {
            continue; // the synthetic root (drive list) column isn't engine-backed
        }
        RefreshColumnFromSnapshot(column, *snapshot);
    }
    for (auto& column : columns2_) {
        if (column.path.empty()) {
            continue;
        }
        RefreshColumnFromSnapshot(column, *snapshot);
    }
}

void ColumnView::OnDirectoryError(const std::wstring& path, ffprotocol::DirectoryErrorReason reason) {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    for (auto& column : columns_) {
        if (column.path == path) {
            column.error = reason == ffprotocol::DirectoryErrorReason::AccessDenied
                ? ColumnErrorState::AccessDenied
                : ColumnErrorState::NoLongerAvailable;
            column.items.clear();
        }
    }
    for (auto& column : columns2_) {
        if (column.path == path) {
            column.error = reason == ffprotocol::DirectoryErrorReason::AccessDenied
                ? ColumnErrorState::AccessDenied
                : ColumnErrorState::NoLongerAvailable;
            column.items.clear();
        }
    }
}

void ColumnView::ShowUnavailableLocation(const std::wstring& displayName) {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    Column column;
    column.path = displayName.empty() ? L"Unavailable location" : displayName;
    column.error = ColumnErrorState::NoLongerAvailable;
    columns_ = {std::move(column)};
    focusedColumnIndex_ = 0;
    pendingSelectionName_.clear();
}

void ColumnView::SetEngineStatus(bool active) {
    engineActive_ = active;
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
    }
}

float ColumnView::ContentWidth() const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    const auto& columns = const_cast<ColumnView*>(this)->ActiveColumns();
    return kColumnWidth * static_cast<float>(columns.size());
}

float ColumnView::PaneContentWidth(int paneIndex) const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
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

    const ffui::UiTheme theme = ffui::GetUiTheme(darkTheme_);
    context->Clear(theme.background);

    // Task 3.6: engine-connection-status text as a rounded chip (paint-only,
    // deliberately non-interactive).
    D2D1_RECT_F badgeRect = D2D1::RectF(8.0f, 4.0f, viewportSize.width - 8.0f, kBadgeHeight - 4.0f);
    ffui::UiFillRoundedRect(context, badgeRect,
                            engineActive_ ? badgeActiveBrush_.Get() : badgeDegradedBrush_.Get(),
                            ffui::UiMetrics::kRadiusSmall);
    const wchar_t* badgeText = engineActive_ ? L"Instant search: enabled" : L"Instant search: basic — click to enable";
    D2D1_RECT_F badgeTextRect = D2D1::RectF(16.0f, 4.0f, viewportSize.width - 16.0f, kBadgeHeight - 4.0f);
    context->DrawText(badgeText, static_cast<UINT32>(wcslen(badgeText)), badgeTextFormat_.Get(), badgeTextRect,
                      engineActive_ ? badgeActiveTextBrush_.Get() : badgeDegradedTextBrush_.Get());

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

    std::lock_guard<std::mutex> lock(columnsMutex_);

    auto RenderPane = [&](const std::vector<Column>& columns, float paneX, float paneWidth, float paneScroll, bool isActivePane) {
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

            if (column.error != ColumnErrorState::None) {
                const wchar_t* message = column.error == ColumnErrorState::AccessDenied
                    ? L"You don't have permission to view this folder."
                    : L"This folder is no longer available.";
                D2D1_RECT_F messageRect = D2D1::RectF(x + 8, kBadgeHeight + 8, x + kColumnWidth - 8, viewportSize.height - 8);
                context->DrawText(message, static_cast<UINT32>(wcslen(message)), textFormat_.Get(), messageRect, errorBrush_.Get());
                continue;
            }

            if (column.items.empty()) {
                const wchar_t* emptyMessage = L"This folder is empty";
                D2D1_RECT_F messageRect = D2D1::RectF(x + 8, kBadgeHeight + 8, x + kColumnWidth - 8, viewportSize.height - 8);
                context->DrawText(emptyMessage, static_cast<UINT32>(wcslen(emptyMessage)), textFormat_.Get(), messageRect, textSecondaryBrush_.Get());
                continue;
            }

            for (int r = 0; r < static_cast<int>(column.items.size()); ++r) {
                const float y = kBadgeHeight + r * kRowHeight;
                if (y + kRowHeight < kBadgeHeight || y > viewportSize.height) {
                    continue;
                }

                const ColumnItem& item = column.items[r];
                const bool isSelected = column.selectedIndices.contains(r);
                const bool isFocusedColumn = (i == (activePane_ == 0 ? focusedColumnIndex_ : focusedColumnIndex2_));
                // Task 3.4: hover tracks the active pane only; the lingering
                // fade-out window (150 ms after OnMouseLeave) keeps the pill
                // visible while it dims. Overlay is skipped in High Contrast.
                const bool lingerFade = hoverLeaveMs_ != 0 && (nowMs - hoverLeaveMs_) < static_cast<uint64_t>(kUiAnimationDefaultMs);
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

    if (dualPane_) {
        const float paneWidth = viewportSize.width / 2.0f;
        RenderPane(columns_, 0.0f, paneWidth, scrollOffset, activePane_ == 0);
        context->DrawLine(D2D1_POINT_2F{paneWidth, kBadgeHeight}, D2D1_POINT_2F{paneWidth, viewportSize.height},
                           borderBrush_.Get(), 2.0f);
        RenderPane(columns2_, paneWidth, paneWidth, scrollOffset2, activePane_ == 1);
    } else {
        RenderPane(columns_, 0.0f, viewportSize.width, scrollOffset, true);
    }
}

void ColumnView::SetDualPane(bool enabled) {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    dualPane_ = enabled;
    if (!enabled) {
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
    std::lock_guard<std::mutex> lock(columnsMutex_);
    const auto& columns = ActiveColumns();
    const int& focusedIndex = const_cast<ColumnView*>(this)->ActiveFocusedColumnIndex();
    const float& scrollOffset = const_cast<ColumnView*>(this)->ActiveScrollOffset();
    outColumnPaths.clear();
    outColumnPaths.reserve(columns.size());
    for (const auto& column : columns) {
        outColumnPaths.push_back(column.path);
    }
    outFocusedIndex = focusedIndex;
    outScrollOffset = scrollOffset;
}

void ColumnView::RestoreActivePaneState(const std::vector<std::wstring>& columnPaths, int focusedIndex, float scrollOffset) {
    if (columnPaths.empty()) return;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        auto& columns = ActiveColumns();
        int& focusedIndexRef = ActiveFocusedColumnIndex();
        float& scrollOffsetRef = ActiveScrollOffset();
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
        if (engineClient_ != nullptr) engineClient_->RequestDirectory(path);
    }
}

} // namespace ffui
