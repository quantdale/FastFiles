#include "ColumnView.h"

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
    if (columnIndex + 1 < static_cast<int>(columns_.size())) {
        columns_.resize(columnIndex + 1);
    }
}

void ColumnView::RequestColumn(int /*columnIndex*/, const std::wstring& path) {
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        Column column;
        column.path = path;
        columns_.push_back(std::move(column));
        focusedColumnIndex_ = static_cast<int>(columns_.size()) - 1;
    }
    if (engineClient_) {
        engineClient_->RequestDirectory(path);
    }
}

void ColumnView::SelectSingle(Column& column, int itemIndex) {
    column.selectedIndices.clear();
    column.selectedIndices.insert(itemIndex);
    column.selectionAnchor = itemIndex;
    column.focusIndex = itemIndex;
}

void ColumnView::ToggleSelection(Column& column, int itemIndex) {
    if (!column.selectedIndices.erase(itemIndex)) {
        column.selectedIndices.insert(itemIndex);
    }
    if (column.selectionAnchor < 0) {
        column.selectionAnchor = itemIndex;
    }
    column.focusIndex = itemIndex;
}

void ColumnView::SelectRange(Column& column, int itemIndex) {
    if (column.selectionAnchor < 0) {
        SelectSingle(column, itemIndex);
        return;
    }
    column.selectedIndices.clear();
    const int first = std::min(column.selectionAnchor, itemIndex);
    const int last = std::max(column.selectionAnchor, itemIndex);
    for (int index = first; index <= last; ++index) {
        column.selectedIndices.insert(index);
    }
    column.focusIndex = itemIndex;
}

void ColumnView::ActivateItem(int columnIndex, int itemIndex, bool control, bool shift) {
    std::wstring childPath;
    bool isDirectory = false;
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        if (columnIndex < 0 || columnIndex >= static_cast<int>(columns_.size())) {
            return;
        }
        Column& column = columns_[columnIndex];
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
        focusedColumnIndex_ = columnIndex;
        return;
    }

    TruncateAfter(columnIndex);
    {
        std::lock_guard<std::mutex> lock(columnsMutex_);
        focusedColumnIndex_ = columnIndex;
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
        if (columns_.empty()) {
            return;
        }
        focusedColumnIndex_ = std::clamp(focusedColumnIndex_, 0, static_cast<int>(columns_.size()) - 1);
        Column& focused = columns_[focusedColumnIndex_];

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
                focusedColumnIndex_ = std::max(0, focusedColumnIndex_ - 1);
                break;
            case VK_RIGHT:
                focusedColumnIndex_ = std::min(static_cast<int>(columns_.size()) - 1, focusedColumnIndex_ + 1);
                break;
            case VK_RETURN:
                activateColumnIndex = focusedColumnIndex_;
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

void ColumnView::OnMouseDown(D2D1_POINT_2F clientPoint, float scrollOffset, bool control, bool shift) {
    if (clientPoint.y < kBadgeHeight) {
        return;
    }
    const int columnIndex = static_cast<int>((clientPoint.x + scrollOffset) / kColumnWidth);
    const int itemIndex = static_cast<int>((clientPoint.y - kBadgeHeight) / kRowHeight);
    ActivateItem(columnIndex, itemIndex, control, shift);
}

std::vector<std::wstring> ColumnView::ActiveSelectionPaths() const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    std::vector<std::wstring> paths;
    if (focusedColumnIndex_ < 0 || focusedColumnIndex_ >= static_cast<int>(columns_.size())) {
        return paths;
    }
    const Column& column = columns_[focusedColumnIndex_];
    for (int index : column.selectedIndices) {
        if (index >= 0 && index < static_cast<int>(column.items.size())) {
            paths.push_back(JoinPath(column.path, column.items[index].name));
        }
    }
    return paths;
}

std::wstring ColumnView::ActivePanePath() const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    if (focusedColumnIndex_ < 0 || focusedColumnIndex_ >= static_cast<int>(columns_.size())) {
        return {};
    }
    return columns_[focusedColumnIndex_].path;
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
        column.items.push_back({entry.name, entry.isDirectory, entry.sizeBytes, entry.attributes});
    }
    std::sort(column.items.begin(), column.items.end(), [](const ColumnItem& a, const ColumnItem& b) {
        if (a.isDirectory != b.isDirectory) {
            return a.isDirectory > b.isDirectory; // folders first
        }
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
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
    return kColumnWidth * static_cast<float>(columns_.size());
}

int ColumnView::FocusedColumnIndex() const {
    std::lock_guard<std::mutex> lock(columnsMutex_);
    return focusedColumnIndex_;
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
    if (focusedColumnIndex_ < 0 || focusedColumnIndex_ >= static_cast<int>(columns_.size())) {
        return summary;
    }
    const Column& column = columns_[focusedColumnIndex_];
    for (int index : column.selectedIndices) {
        if (index < 0 || index >= static_cast<int>(column.items.size())) {
            continue;
        }
        const ColumnItem& item = column.items[index];
        summary.items.push_back({JoinPath(column.path, item.name), item.sizeBytes, item.attributes, item.isDirectory});
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

void ColumnView::Render(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory, D2D1_SIZE_F viewportSize, float scrollOffset) {
    EnsureCreated(context, dwriteFactory);

    context->Clear(D2D1::ColorF(darkTheme_ ? 0x202124 : 0xFFFFFF));

    // Task 5.9: non-modal engine-connection-state status badge.
    D2D1_RECT_F badgeRect = D2D1::RectF(0, 0, viewportSize.width, kBadgeHeight);
    context->FillRectangle(badgeRect, engineActive_ ? badgeActiveBrush_.Get() : badgeDegradedBrush_.Get());
    const wchar_t* badgeText = engineActive_ ? L"Instant search: enabled" : L"Instant search: basic — click to enable";
    D2D1_RECT_F badgeTextRect = D2D1::RectF(10, 0, viewportSize.width - 10, kBadgeHeight);
    context->DrawText(badgeText, static_cast<UINT32>(wcslen(badgeText)), badgeTextFormat_.Get(), badgeTextRect, textBrush_.Get());

    std::lock_guard<std::mutex> lock(columnsMutex_);
    for (int i = 0; i < static_cast<int>(columns_.size()); ++i) {
        const float x = i * kColumnWidth - scrollOffset;
        if (x + kColumnWidth < 0 || x > viewportSize.width) {
            continue; // culled
        }
        const Column& column = columns_[i];

        D2D1_RECT_F columnRect = D2D1::RectF(x, kBadgeHeight, x + kColumnWidth, viewportSize.height);
        context->FillRectangle(columnRect, backgroundBrush_.Get());
        context->DrawLine(D2D1::Point2F(x + kColumnWidth, kBadgeHeight), D2D1::Point2F(x + kColumnWidth, viewportSize.height),
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
                // Task 5.6: the deepest active selection (the focused
                // column) is visually distinct from remembered selections
                // in ancestor columns.
                const float opacity = (i == focusedColumnIndex_) ? 1.0f : 0.35f;
                selectionBrush_->SetOpacity(opacity);
                context->FillRectangle(selectionRect, selectionBrush_.Get());
                selectionBrush_->SetOpacity(1.0f);
            }

            // Task 5.5: files vs folders are visually distinguished by a
            // colored glyph plus a trailing separator on folder names.
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
}

} // namespace ffui
