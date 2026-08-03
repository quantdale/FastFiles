#include "ColumnView.h"
#include "SelectionModel.h"

#include <algorithm>
#include <cwchar>
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
        backgroundBrush_.Reset(); borderBrush_.Reset(); textBrush_.Reset(); selectionBrush_.Reset();
        folderGlyphBrush_.Reset(); fileGlyphBrush_.Reset(); errorBrush_.Reset();
        badgeActiveBrush_.Reset(); badgeDegradedBrush_.Reset();
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
    context->CreateSolidColorBrush(D2D1::ColorF(darkTheme_ ? 0x202124 : 0xFFFFFF), &backgroundBrush_);
    context->CreateSolidColorBrush(D2D1::ColorF(darkTheme_ ? 0x50535A : 0xD8D8D8), &borderBrush_);
    context->CreateSolidColorBrush(D2D1::ColorF(darkTheme_ ? 0xF1F3F4 : 0x000000), &textBrush_);
    context->CreateSolidColorBrush(D2D1::ColorF(0x2B6CDA), &selectionBrush_);
    context->CreateSolidColorBrush(D2D1::ColorF(0x5B8FE0), &folderGlyphBrush_);
    context->CreateSolidColorBrush(D2D1::ColorF(0x9AA0A6), &fileGlyphBrush_);
    context->CreateSolidColorBrush(D2D1::ColorF(0xB00020), &errorBrush_);
    context->CreateSolidColorBrush(D2D1::ColorF(0xDDEFDD), &badgeActiveBrush_);
    context->CreateSolidColorBrush(D2D1::ColorF(0xFFF3CD), &badgeDegradedBrush_);

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

    context->Clear(D2D1::ColorF(darkTheme_ ? 0x202124 : 0xFFFFFF));

    // Task 5.9: non-modal engine-connection-state status badge.
    D2D1_RECT_F badgeRect = D2D1::RectF(0, 0, viewportSize.width, kBadgeHeight);
    context->FillRectangle(badgeRect, engineActive_ ? badgeActiveBrush_.Get() : badgeDegradedBrush_.Get());
    const wchar_t* badgeText = engineActive_ ? L"Instant search: enabled" : L"Instant search: basic — click to enable";
    D2D1_RECT_F badgeTextRect = D2D1::RectF(10, 0, viewportSize.width - 10, kBadgeHeight);
    context->DrawText(badgeText, static_cast<UINT32>(wcslen(badgeText)), badgeTextFormat_.Get(), badgeTextRect, textBrush_.Get());

    std::lock_guard<std::mutex> lock(columnsMutex_);

    auto RenderPane = [&](const std::vector<Column>& columns, float paneX, float paneWidth, float paneScroll) {
        for (int i = 0; i < static_cast<int>(columns.size()); ++i) {
            const float x = paneX + i * kColumnWidth - paneScroll;
            if (x + kColumnWidth < paneX || x > paneX + paneWidth) {
                continue; // culled
            }
            const Column& column = columns[i];

            D2D1_RECT_F columnRect = D2D1::RectF(x, kBadgeHeight, x + kColumnWidth, viewportSize.height);
            context->FillRectangle(columnRect, backgroundBrush_.Get());
            context->DrawLine(D2D1_POINT_2F{x + kColumnWidth, kBadgeHeight}, D2D1_POINT_2F{x + kColumnWidth, viewportSize.height},
                               borderBrush_.Get(), 1.0f);

            if (column.error != ColumnErrorState::None) {
                const wchar_t* message = column.error == ColumnErrorState::AccessDenied
                    ? L"You don't have permission to view this folder."
                    : L"This folder is no longer available.";
                D2D1_RECT_F messageRect = D2D1::RectF(x + 8, kBadgeHeight + 8, x + kColumnWidth - 8, viewportSize.height - 8);
                context->DrawText(message, static_cast<UINT32>(wcslen(message)), textFormat_.Get(), messageRect, errorBrush_.Get());
                continue;
            }

            for (int r = 0; r < static_cast<int>(column.items.size()); ++r) {
                const float y = kBadgeHeight + r * kRowHeight;
                if (y + kRowHeight < kBadgeHeight || y > viewportSize.height) {
                    continue;
                }

                if (column.selectedIndices.contains(r)) {
                    D2D1_RECT_F selectionRect = D2D1::RectF(x, y, x + kColumnWidth, y + kRowHeight);
                    const float opacity = (i == (activePane_ == 0 ? focusedColumnIndex_ : focusedColumnIndex2_)) ? 1.0f : 0.35f;
                    selectionBrush_->SetOpacity(opacity);
                    context->FillRectangle(selectionRect, selectionBrush_.Get());
                    selectionBrush_->SetOpacity(1.0f);
                }

                D2D1_RECT_F glyphRect = D2D1::RectF(x + 8, y + kRowHeight / 2 - 4, x + 16, y + kRowHeight / 2 + 4);
                context->FillRectangle(glyphRect, column.items[r].isDirectory ? folderGlyphBrush_.Get() : fileGlyphBrush_.Get());

                std::wstring label = column.items[r].name;
                if (column.items[r].isDirectory && (label.empty() || label.back() != L'\\')) {
                    label += L'\\';
                }
                D2D1_RECT_F textRect = D2D1::RectF(x + 24, y, x + kColumnWidth - 4, y + kRowHeight);
                context->DrawText(label.c_str(), static_cast<UINT32>(label.size()), textFormat_.Get(), textRect, textBrush_.Get());
            }
        }
    };

    if (dualPane_) {
        const float paneWidth = viewportSize.width / 2.0f;
        RenderPane(columns_, 0.0f, paneWidth, scrollOffset);
        context->DrawLine(D2D1_POINT_2F{paneWidth, kBadgeHeight}, D2D1_POINT_2F{paneWidth, viewportSize.height},
                           borderBrush_.Get(), 2.0f);
        RenderPane(columns2_, paneWidth, paneWidth, scrollOffset2);
    } else {
        RenderPane(columns_, 0.0f, viewportSize.width, scrollOffset);
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
