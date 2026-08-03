#include "StorageAnalysis.h"

#include "TreemapView.h"
#include "Util.h"

#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>

namespace ffui {
namespace {

constexpr int kListId = 7201;
constexpr int kStatusId = 7202;
constexpr int kBackId = 7203;
constexpr int kUpId = 7204;
constexpr int kOverviewId = 7205;
constexpr int kDrillDownId = 7206;
constexpr int kLargestFoldersId = 7207;
constexpr int kLargestFilesId = 7208;
constexpr int kByCategoryId = 7209;
constexpr int kTreemapId = 7210;
constexpr int kOverviewListId = 7211;
constexpr int kCategoryFilterId = 7212;
constexpr UINT_PTR kRefreshTimer = 7220;
constexpr int kRefreshDelayMs = 50;

// storage-analysis 2.4: a volume that disappeared keeps its last-known
// capacity figures (labeled stale) instead of being silently dropped.
constexpr wchar_t kStaleSuffix[] = L" (stale)";

void SetSortIndicator(HWND list, int column, bool ascending) {
    HWND header = ListView_GetHeader(list);
    if (header == nullptr) return;
    for (int i = 0; i < 16; ++i) {
        HDITEM hd{};
        hd.mask = HDI_FORMAT;
        if (Header_GetItem(header, i, &hd)) {
            hd.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
            Header_SetItem(header, i, &hd);
        }
    }
    HDITEM hd{};
    hd.mask = HDI_FORMAT;
    if (Header_GetItem(header, column, &hd)) {
        hd.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        hd.fmt |= ascending ? HDF_SORTUP : HDF_SORTDOWN;
        Header_SetItem(header, column, &hd);
    }
}

std::wstring FormatPercent(uint64_t part, uint64_t whole) {
    if (whole == 0) return L"—";
    const double pct = static_cast<double>(part) / static_cast<double>(whole) * 100.0;
    wchar_t buffer[64]{};
    swprintf_s(buffer, L"%.1f%%", pct);
    return buffer;
}

} // namespace

StorageAnalysis::~StorageAnalysis() {
    Hide();
    stopping_ = true;
    currentGeneration_.fetch_add(1);
    workCv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

bool StorageAnalysis::Initialize(HWND owner, EngineClient* engine,
                                 std::function<void(const std::wstring& path)> navigate,
                                 std::function<void()> close,
                                 std::function<void(const std::wstring& commandId, const std::vector<std::wstring>& paths)> invokeCommand) {
    owner_ = owner;
    engine_ = engine;
    navigate_ = std::move(navigate);
    close_ = std::move(close);
    invokeCommand_ = std::move(invokeCommand);

    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&controls);

    back_ = CreateWindowExW(0, L"BUTTON", L"Back", WS_CHILD | BS_PUSHBUTTON,
                            0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBackId)), nullptr, nullptr);
    up_ = CreateWindowExW(0, L"BUTTON", L"Up", WS_CHILD | BS_PUSHBUTTON,
                          0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kUpId)), nullptr, nullptr);
    status_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD,
                              0, 0, 0, 0, owner, nullptr, nullptr, nullptr);
    list_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                            WS_CHILD | LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | WS_TABSTOP,
                            0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kListId)), nullptr, nullptr);
    overview_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                WS_CHILD | LVS_REPORT | LVS_SINGLESEL | WS_TABSTOP,
                                0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOverviewListId)), nullptr, nullptr);
    overviewButton_ = CreateWindowExW(0, L"BUTTON", L"Overview", WS_CHILD | BS_AUTORADIOBUTTON | WS_GROUP,
                                      0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOverviewId)), nullptr, nullptr);
    categoryFilter_ = CreateWindowExW(0, WC_COMBOBOXW, L"",
                                      WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                                      0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCategoryFilterId)), nullptr, nullptr);
    drillDown_ = CreateWindowExW(0, L"BUTTON", L"Drill Down", WS_CHILD | BS_AUTORADIOBUTTON,
                                 0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDrillDownId)), nullptr, nullptr);
    largestFolders_ = CreateWindowExW(0, L"BUTTON", L"Largest Folders", WS_CHILD | BS_AUTORADIOBUTTON,
                                      0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kLargestFoldersId)), nullptr, nullptr);
    largestFiles_ = CreateWindowExW(0, L"BUTTON", L"Largest Files", WS_CHILD | BS_AUTORADIOBUTTON,
                                    0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kLargestFilesId)), nullptr, nullptr);
    byCategory_ = CreateWindowExW(0, L"BUTTON", L"By Category", WS_CHILD | BS_AUTORADIOBUTTON,
                                  0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kByCategoryId)), nullptr, nullptr);
    treemap_ = CreateWindowExW(0, L"BUTTON", L"Treemap", WS_CHILD | BS_AUTORADIOBUTTON,
                                   0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTreemapId)), nullptr, nullptr);

    if (!back_ || !up_ || !status_ || !list_ || !overview_ || !overviewButton_ || !categoryFilter_ ||
        !drillDown_ || !largestFolders_ || !largestFiles_ || !byCategory_ || !treemap_) return false;

    const wchar_t* columns[] = {L"Name", L"Type", L"Size", L"Subtree Size", L"% of Parent", L"Modified"};
    const int widths[] = {260, 80, 100, 120, 100, 145};
    for (int i = 0; i < 6; ++i) {
        LVCOLUMNW column{LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM};
        column.pszText = const_cast<wchar_t*>(columns[i]);
        column.cx = widths[i];
        column.iSubItem = i;
        ListView_InsertColumn(list_, i, &column);
    }
    ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    SetSortIndicator(list_, sortColumn_, sortAscending_);

    // storage-overview columns: volume, capacity trio, coverage, availability.
    const wchar_t* overviewColumns[] = {L"Volume", L"Total", L"Used", L"Free", L"% Used", L"Index Coverage"};
    const int overviewWidths[] = {120, 110, 110, 110, 80, 220};
    for (int i = 0; i < 6; ++i) {
        LVCOLUMNW column{LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM};
        column.pszText = const_cast<wchar_t*>(overviewColumns[i]);
        column.cx = overviewWidths[i];
        column.iSubItem = i;
        ListView_InsertColumn(overview_, i, &column);
    }
    ListView_SetExtendedListViewStyle(overview_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    PopulateCategoryFilter();

    worker_ = std::thread(&StorageAnalysis::WorkerMain, this);
    return true;
}

void StorageAnalysis::PopulateCategoryFilter() {
    if (!categoryFilter_) return;
    ComboBox_ResetContent(categoryFilter_);
    // storage-analysis 5.4: "All categories" plus one entry per shipped/user
    // category definition; directories are never filtered out.
    ComboBox_AddString(categoryFilter_, L"All categories");
    const auto& categories = categoryEngine_.GetCategories();
    for (const auto& cat : categories) {
        ComboBox_AddString(categoryFilter_, const_cast<wchar_t*>(cat.displayName.c_str()));
    }
    ComboBox_SetCurSel(categoryFilter_, 0);
}

void StorageAnalysis::ApplyCategoryFilter() {
    // storage-analysis 5.4: scope the displayed items to the selected
    // category, recalculating percentages relative to the filtered scope.
    // Directories stay (they can be drilled into); files whose category does
    // not match the filter are removed from the list.
    if (!categoryFilter_) return;
    const int sel = ComboBox_GetCurSel(categoryFilter_);
    if (sel <= 0) return; // index 0 = "All categories"
    wchar_t buffer[128]{};
    ComboBox_GetLBText(categoryFilter_, sel, buffer);
    const std::wstring catName = buffer;
    items_.erase(std::remove_if(items_.begin(), items_.end(),
                                [this, &catName](const DrillItem& item) {
                                    if (item.isDirectory) return false;
                                    auto match = categoryEngine_.Match(item.name);
                                    return match.matched ? match.categoryName != catName : catName != L"Other";
                                }),
                 items_.end());
}

void StorageAnalysis::ShowAndFocus(const std::wstring& currentPath, bool engineActive) {
    currentPath_ = currentPath;
    engineActive_ = engineActive;
    visible_ = true;
    for (HWND c : {back_, up_, list_, overview_, status_, overviewButton_, categoryFilter_, drillDown_,
                   largestFolders_, largestFiles_, byCategory_, treemap_}) ShowWindow(c, SW_SHOW);
    // Open on the storage overview (2.1): the volume list is the entry point
    // for capacity figures and per-volume analysis.
    CheckRadioButton(owner_, kOverviewId, kTreemapId, kOverviewId);
    viewMode_ = ViewMode::Overview;
    RefreshData();
    Reposition();
    SetFocus(overview_);
}

void StorageAnalysis::Hide() {
    if (!visible_) return;
    visible_ = false;
    KillTimer(owner_, kRefreshTimer);
    for (HWND c : {back_, up_, list_, overview_, status_, overviewButton_, categoryFilter_, drillDown_,
                   largestFolders_, largestFiles_, byCategory_, treemap_}) ShowWindow(c, SW_HIDE);
    items_.clear();
    volumes_.clear();
    ListView_SetItemCountEx(list_, 0, LVSICF_NOSCROLL);
    ListView_DeleteAllItems(overview_);
}

void StorageAnalysis::Reposition() {
    if (!visible_) return;
    RECT client{};
    GetClientRect(owner_, &client);
    const int width = static_cast<int>(client.right);
    const int height = static_cast<int>(client.bottom);
    const int top = 72; // below navigation chrome
    const int buttonHeight = 28;
    SetWindowPos(back_, HWND_TOP, 12, top, 80, buttonHeight, SWP_SHOWWINDOW);
    SetWindowPos(up_, HWND_TOP, 96, top, 80, buttonHeight, SWP_SHOWWINDOW);
    SetWindowPos(overviewButton_, HWND_TOP, 180, top, 100, buttonHeight, SWP_SHOWWINDOW);
    SetWindowPos(drillDown_, HWND_TOP, 284, top, 110, buttonHeight, SWP_SHOWWINDOW);
    SetWindowPos(largestFolders_, HWND_TOP, 398, top, 120, buttonHeight, SWP_SHOWWINDOW);
    SetWindowPos(largestFiles_, HWND_TOP, 522, top, 110, buttonHeight, SWP_SHOWWINDOW);
    SetWindowPos(byCategory_, HWND_TOP, 636, top, 110, buttonHeight, SWP_SHOWWINDOW);
    SetWindowPos(treemap_, HWND_TOP, 750, top, 90, buttonHeight, SWP_SHOWWINDOW);
    SetWindowPos(categoryFilter_, HWND_TOP, 844, top, (std::max)(140, width - 1084), buttonHeight, SWP_SHOWWINDOW);
    SetWindowPos(status_, HWND_TOP, (std::max)(988, width - 200), top + 4, (std::max)(100, width - 992), 20, SWP_SHOWWINDOW);
    const int listTop = top + buttonHeight + 8;
    const int listHeight = (std::max)(80, height - top - buttonHeight - 20);
    SetWindowPos(list_, HWND_TOP, 12, listTop, width - 24, listHeight, SWP_SHOWWINDOW);
    SetWindowPos(overview_, HWND_TOP, 12, listTop, width - 24, listHeight, SWP_SHOWWINDOW);
}

void StorageAnalysis::RefreshOverview() {
    std::vector<VolumeItem> prior = std::move(volumes_);
    volumes_.clear();

    // Storage-overview 2.1: fixed local volume enumeration, independent of
    // the filesystem index (GetDiskFreeSpaceEx needs no privileged path).
    const DWORD driveMask = GetLogicalDrives();
    const auto snapshot = engine_ ? engine_->ReadSnapshot() : std::nullopt;

    for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
        if ((driveMask & (1u << (letter - L'A'))) == 0) continue;
        wchar_t rootPath[] = {letter, L':', L'\\', L'\0'};
        if (GetDriveTypeW(rootPath) != DRIVE_FIXED) continue;

        VolumeItem vol;
        vol.rootPath = rootPath;

        // 2.2: total/used/free/percentage from the OS's own reporting.
        ULARGE_INTEGER total{}, freeAvail{};
        if (GetDiskFreeSpaceExW(rootPath, &freeAvail, &total, nullptr)) {
            vol.totalBytes = total.QuadPart;
            vol.freeBytes = freeAvail.QuadPart;
            vol.usedBytes = total.QuadPart > freeAvail.QuadPart
                                ? total.QuadPart - freeAvail.QuadPart
                                : 0;
        }

        // 2.3: indexed-coverage indicator -- whole-volume analysis available
        // only when the engine's snapshot covers the volume root.
        vol.fullyIndexed = false;
        if (engineActive_ && snapshot) {
            auto it = snapshot->find(rootPath);
            if (it != snapshot->end() &&
                it->second.status == ffprotocol::DirectoryEnumerationStatus::Success) {
                vol.fullyIndexed = true;
            }
        }

        // 2.4: stale/unavailable handling -- a volume that fails capacity
        // retrieval keeps its last-known figures from the previous pass,
        // clearly labeled stale, with live drill-down disabled.
        if (vol.totalBytes == 0 && vol.freeBytes == 0) {
            vol.unavailable = true;
            for (const auto& p : prior) {
                if (p.rootPath == vol.rootPath && p.totalBytes > 0) {
                    vol.totalBytes = p.totalBytes;
                    vol.freeBytes = p.freeBytes;
                    vol.usedBytes = p.usedBytes;
                    break;
                }
            }
        }

        volumes_.push_back(std::move(vol));
    }

    // A volume that has vanished from GetLogicalDrives entirely is retained
    // from the previous pass, labeled stale, with live drill-down disabled.
    for (const auto& p : prior) {
        if (p.totalBytes == 0) continue;
        bool stillPresent = false;
        for (const auto& vol : volumes_) {
            if (vol.rootPath == p.rootPath) { stillPresent = true; break; }
        }
        if (!stillPresent) {
            VolumeItem stale = p;
            stale.unavailable = true;
            volumes_.push_back(std::move(stale));
        }
    }

    ListView_DeleteAllItems(overview_);
    for (size_t i = 0; i < volumes_.size(); ++i) {
        const VolumeItem& vol = volumes_[i];
        LVITEMW item{};
        item.iItem = static_cast<int>(i);
        item.mask = LVIF_TEXT;
        std::wstring name = vol.rootPath;
        if (vol.unavailable) name += kStaleSuffix;
        item.pszText = const_cast<wchar_t*>(name.c_str());
        ListView_InsertItem(overview_, &item);

        wchar_t buffer[64]{};
        const std::wstring total = vol.unavailable && vol.totalBytes == 0
                                       ? L"—"
                                       : ffui::FormatSize(vol.totalBytes);
        const std::wstring used = vol.unavailable && vol.usedBytes == 0
                                      ? L"—"
                                      : ffui::FormatSize(vol.usedBytes);
        const std::wstring free = vol.unavailable && vol.freeBytes == 0
                                      ? L"—"
                                      : ffui::FormatSize(vol.freeBytes);
        std::wstring pct = L"—";
        if (vol.totalBytes > 0) {
            swprintf_s(buffer, L"%.1f%%",
                       static_cast<double>(vol.usedBytes) / static_cast<double>(vol.totalBytes) * 100.0);
            pct = buffer;
        }
        ListView_SetItemText(overview_, static_cast<int>(i), 1, const_cast<wchar_t*>(total.c_str()));
        ListView_SetItemText(overview_, static_cast<int>(i), 2, const_cast<wchar_t*>(used.c_str()));
        ListView_SetItemText(overview_, static_cast<int>(i), 3, const_cast<wchar_t*>(free.c_str()));
        ListView_SetItemText(overview_, static_cast<int>(i), 4, const_cast<wchar_t*>(pct.c_str()));
        ListView_SetItemText(overview_, static_cast<int>(i), 5,
                             const_cast<wchar_t*>(vol.fullyIndexed ? L"Fully indexed — whole-volume analysis" : L"Partial — browsed/pinned only"));
    }

    std::wstring statusText = std::to_wstring(volumes_.size()) + L" fixed volumes";
    if (!engineActive_) statusText += L" (degraded mode — capacity from OS only, coverage partial)";
    SetWindowTextW(status_, statusText.c_str());
}

void StorageAnalysis::RefreshData() {
    if (!visible_) return;

    // storage-analysis 2.1: the overview is the entry point; list mode views
    // are skipped entirely while it is active.
    if (viewMode_ == ViewMode::Overview) {
        RefreshOverview();
        ShowWindow(list_, SW_HIDE);
        ShowWindow(overview_, SW_SHOW);
        ShowWindow(categoryFilter_, SW_HIDE);
        ShowWindow(status_, SW_SHOW);
        return;
    }
    if (!engine_) return;
    const auto snapshot = engine_->ReadSnapshot();
    if (!snapshot) {
        SetWindowTextW(status_, engineActive_ ? L"Waiting for index data…" : L"Degraded mode — browsing via FindFirstFileEx");
        return;
    }

    std::wstring targetPath = currentPath_;
    if (targetPath.empty()) targetPath = L"";

    auto it = snapshot->find(targetPath);
    if (it == snapshot->end()) {
        SetWindowTextW(status_, engineActive_ ? L"Folder not yet indexed." : L"Degraded mode — folder not yet browsed");
        items_.clear();
        ListView_SetItemCountEx(list_, 0, LVSICF_NOSCROLL);
        return;
    }

    const auto& directory = it->second;
    if (directory.status != ffprotocol::DirectoryEnumerationStatus::Success) {
        SetWindowTextW(status_, engineActive_ ? L"This folder is not accessible." : L"Degraded mode — folder not accessible");
        items_.clear();
        ListView_SetItemCountEx(list_, 0, LVSICF_NOSCROLL);
        return;
    }

    items_.clear();
    items_.reserve(directory.entries.size());
    for (const auto& entry : directory.entries) {
        DrillItem item;
        item.name = entry.name;
        item.isDirectory = entry.isDirectory;
        item.sizeBytes = entry.sizeBytes;
        item.totalSizeBytes = entry.isDirectory ? 0 : entry.sizeBytes;
        item.attributes = entry.attributes;
        item.creationTime = entry.creationTime;
        item.lastModifiedTime = entry.lastModifiedTime;
        item.volumeRowId = entry.volumeRowId;
        item.fileIdLow = entry.fileIdLow;
        item.fileIdHigh = entry.fileIdHigh;
        item.parentFrnLow = entry.parentIdLow;
        item.parentFrnHigh = entry.parentIdHigh;
        item.calculating = entry.isDirectory;
        items_.push_back(item);
    }

    std::wstring statusText = std::to_wstring(items_.size()) + L" items";
    if (!engineActive_) {
        statusText += L" (degraded mode — partial coverage)";
    }
    SetWindowTextW(status_, statusText.c_str());

    // Filter based on view mode
    if (viewMode_ == ViewMode::LargestFolders) {
        items_.erase(std::remove_if(items_.begin(), items_.end(),
                                    [](const DrillItem& item) { return !item.isDirectory; }),
                     items_.end());
    } else if (viewMode_ == ViewMode::LargestFiles) {
        items_.erase(std::remove_if(items_.begin(), items_.end(),
                                    [](const DrillItem& item) { return item.isDirectory; }),
                     items_.end());
    } else if (viewMode_ == ViewMode::ByCategory) {
        std::map<std::wstring, DrillItem> categoryMap;
        for (const auto& item : items_) {
            if (item.isDirectory) continue;
            auto match = categoryEngine_.Match(item.name);
            std::wstring catName = match.matched ? match.categoryName : L"Other";
            auto& catItem = categoryMap[catName];
            catItem.name = catName;
            catItem.isDirectory = false;
            catItem.totalSizeBytes += item.sizeBytes;
            catItem.calculating = false;
        }
        items_.clear();
        for (auto& [name, item] : categoryMap) {
            items_.push_back(std::move(item));
        }
    }

    // storage-analysis 5.4: category filter applies to the drill-down,
    // largest-folders, and largest-files listings (not the aggregate
    // by-category view, which already groups by category).
    if (viewMode_ != ViewMode::ByCategory) {
        ApplyCategoryFilter();
    }

    const bool showList = viewMode_ != ViewMode::Treemap;
    ShowWindow(list_, showList ? SW_SHOW : SW_HIDE);
    ShowWindow(overview_, SW_HIDE);
    ShowWindow(status_, showList ? SW_SHOW : SW_HIDE);
    ListView_SetItemCountEx(list_, static_cast<int>(items_.size()), LVSICF_NOINVALIDATEALL);
    InvalidateRect(list_, nullptr, FALSE);

    // Request aggregates for all directories
    uint64_t requestId = 1;
    for (size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].isDirectory && items_[i].volumeRowId != 0) {
            items_[i].requestId = requestId++;
            RequestAggregateForItem(i);
        }
    }

    SortItems(sortColumn_, sortAscending_);
}

void StorageAnalysis::RequestAggregateForItem(size_t index) {
    if (!engine_ || index >= items_.size()) return;
    const DrillItem& item = items_[index];
    if (!item.isDirectory || item.volumeRowId == 0) return;

    uint64_t requestId = item.requestId;
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pending_.push_back({requestId, static_cast<uint32_t>(index)});
    }

    engine_->RequestFolderAggregate(
        item.volumeRowId, item.fileIdLow, item.fileIdHigh,
        [this, requestId](uint64_t reqId, ffprotocol::FolderAggregateStatus status,
                          uint64_t itemCount, uint64_t totalSizeBytes) {
            if (reqId != requestId) return;
            ffui::PostFolderAggregateResult(owner_, WM_APP_STORAGE_AGGREGATE, requestId, status, itemCount, totalSizeBytes);
        });
}

void StorageAnalysis::HandleAggregateResult(uint64_t requestId,
                                            ffprotocol::FolderAggregateStatus status,
                                            uint64_t /*itemCount*/, uint64_t totalSizeBytes) {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    auto it = std::find_if(pending_.begin(), pending_.end(),
                           [requestId](const PendingRequest& r) { return r.requestId == requestId; });
    if (it == pending_.end()) return;
    pending_.erase(it);

    auto itemIt = std::find_if(items_.begin(), items_.end(),
                               [requestId](const DrillItem& item) { return item.requestId == requestId; });
    if (itemIt == items_.end()) return;

    if (status == ffprotocol::FolderAggregateStatus::Resolved) {
        itemIt->totalSizeBytes = totalSizeBytes;
        itemIt->calculating = false;
    } else if (status == ffprotocol::FolderAggregateStatus::NotFound) {
        itemIt->totalSizeBytes = 0;
        itemIt->calculating = false;
    }

    ListView_SetItemCountEx(list_, static_cast<int>(items_.size()), LVSICF_NOINVALIDATEALL);
    InvalidateRect(list_, nullptr, FALSE);
}

void StorageAnalysis::SortItems(int column, bool ascending) {
    if (column < 0 || column >= 6) return;
    sortColumn_ = column;
    sortAscending_ = ascending;
    SetSortIndicator(list_, column, ascending);

    auto getSortKey = [this](const DrillItem& item, int col) -> std::pair<int, uint64_t> {
        switch (col) {
            case 0: return {item.isDirectory ? 0 : 1, 0}; // name: folders first, then name
            case 1: return {item.isDirectory ? 0 : 1, 0}; // type: folders first
            case 2: return {item.isDirectory ? 0 : 1, item.sizeBytes}; // size
            case 3: return {item.isDirectory ? 0 : 1, item.totalSizeBytes}; // subtree size
            case 4: {
                uint64_t parentTotal = 0;
                for (const auto& sibling : items_) {
                    if (sibling.isDirectory) parentTotal += sibling.totalSizeBytes;
                }
                return {item.isDirectory ? 0 : 1, parentTotal > 0 ? (item.totalSizeBytes * 1000000ULL / parentTotal) : 0};
            }
            case 5: return {0, item.lastModifiedTime}; // modified
            default: return {0, 0};
        }
    };

    std::sort(items_.begin(), items_.end(), [this, column, ascending, getSortKey](const DrillItem& a, const DrillItem& b) {
        const auto keyA = getSortKey(a, column);
        const auto keyB = getSortKey(b, column);
        if (keyA.first != keyB.first) return ascending ? keyA.first < keyB.first : keyA.first > keyB.first;
        if (keyA.second != keyB.second) return ascending ? keyA.second < keyB.second : keyA.second > keyB.second;
        // tie-break on name
        return ascending ? _wcsicmp(a.name.c_str(), b.name.c_str()) < 0 : _wcsicmp(a.name.c_str(), b.name.c_str()) > 0;
    });

    ListView_SetItemCountEx(list_, static_cast<int>(items_.size()), LVSICF_NOINVALIDATEALL);
    InvalidateRect(list_, nullptr, FALSE);
}

void StorageAnalysis::WorkerMain() {
    while (!stopping_) {
        std::unique_lock<std::mutex> lock(workMutex_);
        workCv_.wait(lock, [this] { return stopping_ || !currentPath_.empty(); });
        if (stopping_) break;
    }
}

void StorageAnalysis::OnSnapshotUpdated() {
    if (!visible_) return;
    ++generation_;
    currentGeneration_ = generation_;
    RefreshData();
    treemapView_.OnSnapshotUpdated();
}

void StorageAnalysis::RenderTreemap(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory, D2D1_SIZE_F viewportSize) {
    if (viewMode_ != ViewMode::Treemap) return;
    treemapView_.EnsureCreated(context, dwriteFactory);
    treemapView_.SetDarkTheme(engineActive_);
    treemapView_.Render(context, dwriteFactory, viewportSize);
}

bool StorageAnalysis::HandleOwnerCommand(WPARAM wParam, LPARAM) {
    if (!visible_) return false;
    const int id = LOWORD(wParam);
    if (id == kCategoryFilterId && HIWORD(wParam) == CBN_SELCHANGE) {
        RefreshData();
        return true;
    }
    if (id == kBackId) {
        if (close_) close_();
        return true;
    }
    if (id == kUpId) {
        if (!currentPath_.empty()) {
            size_t slash = currentPath_.find_last_of(L"\\/");
            if (slash != std::wstring::npos && slash >= 3) {
                std::wstring parent = currentPath_.substr(0, slash);
                if (parent.size() == 2 && parent[1] == L':') parent += L"\\";
                currentPath_ = parent;
                RefreshData();
                if (navigate_) navigate_(currentPath_);
            }
        }
        return true;
    }
    if (id == kOverviewId) {
        SetViewMode(ViewMode::Overview);
        return true;
    }
    if (id == kDrillDownId) {
        SetViewMode(ViewMode::DrillDown);
        return true;
    }
    if (id == kLargestFoldersId) {
        SetViewMode(ViewMode::LargestFolders);
        return true;
    }
    if (id == kLargestFilesId) {
        SetViewMode(ViewMode::LargestFiles);
        return true;
    }
    if (id == kByCategoryId) {
        SetViewMode(ViewMode::ByCategory);
        return true;
    }
    if (id == kTreemapId) {
        SetViewMode(ViewMode::Treemap);
        return true;
    }
    return false;
}

void StorageAnalysis::SetViewMode(ViewMode mode) {
    if (viewMode_ == mode) return;
    viewMode_ = mode;
    CheckRadioButton(owner_, kOverviewId, kTreemapId,
                     mode == ViewMode::Overview ? kOverviewId :
                     mode == ViewMode::DrillDown ? kDrillDownId :
                     mode == ViewMode::LargestFolders ? kLargestFoldersId :
                     mode == ViewMode::LargestFiles ? kLargestFilesId :
                     mode == ViewMode::ByCategory ? kByCategoryId : kTreemapId);
    RefreshData();
}

bool StorageAnalysis::HandleNotify(LPARAM lParam) {
    if (!visible_) return false;
    auto* header = reinterpret_cast<NMHDR*>(lParam);
    if (header == nullptr) return false;

    // storage-analysis 2.5: selecting a volume from the overview opens the
    // drill-down scoped to that volume's root; an unavailable (stale)
    // volume keeps live drill-down disabled.
    if (header->hwndFrom == overview_) {
        if (header->code == NM_DBLCLK || header->code == LVN_ITEMACTIVATE) {
            const int selected = ListView_GetNextItem(overview_, -1, LVNI_SELECTED);
            if (selected >= 0 && static_cast<size_t>(selected) < volumes_.size()) {
                const VolumeItem& vol = volumes_[static_cast<size_t>(selected)];
                if (vol.unavailable) {
                    SetWindowTextW(status_, L"Volume unavailable — capacity figures are stale; drill-down disabled until it reconnects.");
                    return true;
                }
                currentPath_ = vol.rootPath;
                SetViewMode(ViewMode::DrillDown);
                if (navigate_) navigate_(currentPath_);
            }
            return true;
        }
        return false;
    }
    if (header->hwndFrom != list_) return false;

    if (header->code == LVN_GETDISPINFOW) {
        auto* info = reinterpret_cast<NMLVDISPINFOW*>(lParam);
        if (info->item.iItem < 0 || static_cast<size_t>(info->item.iItem) >= items_.size() ||
            (info->item.mask & LVIF_TEXT) == 0) return true;
        const DrillItem& item = items_[static_cast<size_t>(info->item.iItem)];
        std::wstring text;
        switch (info->item.iSubItem) {
            case 0:
                text = item.name;
                if (item.isDirectory && !text.empty() && text.back() != L'\\') text += L'\\';
                break;
            case 1:
                text = item.isDirectory ? L"Folder" : L"File";
                break;
            case 2:
                text = item.isDirectory ? L"—" : ffui::FormatSize(item.sizeBytes);
                break;
            case 3:
                if (item.isDirectory) {
                    text = item.calculating ? L"Calculating…" : ffui::FormatSize(item.totalSizeBytes);
                } else {
                    text = ffui::FormatSize(item.totalSizeBytes);
                }
                break;
            case 4: {
                if (item.isDirectory && !item.calculating && item.totalSizeBytes > 0) {
                    uint64_t parentTotal = 0;
                    for (const auto& sibling : items_) {
                        if (sibling.isDirectory) parentTotal += sibling.totalSizeBytes;
                    }
                    text = FormatPercent(item.totalSizeBytes, parentTotal);
                } else {
                    text = L"—";
                }
                break;
            }
            case 5:
                if (item.lastModifiedTime == 0) {
                    text = L"—";
                } else {
                    ULARGE_INTEGER value{};
                    value.QuadPart = item.lastModifiedTime;
                    FILETIME ft{value.LowPart, value.HighPart};
                    SYSTEMTIME time{};
                    if (FileTimeToSystemTime(&ft, &time)) {
                        wchar_t buffer[32]{};
                        swprintf_s(buffer, L"%04u-%02u-%02u %02u:%02u",
                                   time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute);
                        text = buffer;
                    } else {
                        text = L"—";
                    }
                }
                break;
        }
        wcsncpy_s(info->item.pszText, static_cast<size_t>(info->item.cchTextMax), text.c_str(), _TRUNCATE);
        return true;
    }

    if (header->code == NM_DBLCLK || header->code == LVN_ITEMACTIVATE) {
        const int selected = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
        if (selected >= 0 && static_cast<size_t>(selected) < items_.size()) {
            const DrillItem& item = items_[static_cast<size_t>(selected)];
            if (item.isDirectory) {
                std::wstring childPath = ffui::JoinPath(currentPath_, item.name);
                currentPath_ = childPath;
                RefreshData();
                if (navigate_) navigate_(currentPath_);
            } else if (navigate_) {
                std::wstring filePath = ffui::JoinPath(currentPath_, item.name);
                navigate_(filePath);
            }
        }
        return true;
    }

    if (header->code == LVN_COLUMNCLICK) {
        auto* info = reinterpret_cast<NMLISTVIEW*>(lParam);
        if (info->iSubItem == sortColumn_) {
            SortItems(sortColumn_, !sortAscending_);
        } else {
            SortItems(info->iSubItem, true);
        }
        return true;
    }
    return false;
}

bool StorageAnalysis::HandleTimer(UINT_PTR timerId) {
    if (!visible_ || timerId != kRefreshTimer) return false;
    KillTimer(owner_, kRefreshTimer);
    RefreshData();
    return true;
}

bool StorageAnalysis::HandleCompletion(LPARAM lParam) {
    auto* payload = reinterpret_cast<ffprotocol::FolderAggregateResultPayload*>(lParam);
    if (!payload) return true;
    HandleAggregateResult(payload->requestId, payload->status, payload->itemCount, payload->totalSizeBytes);
    delete payload;
    return true;
}

void StorageAnalysis::SetEngineActive(bool active) {
    engineActive_ = active;
}

bool StorageAnalysis::HandleContextMenu(WPARAM /*wParam*/, LPARAM lParam) {
    if (!visible_ || !invokeCommand_) return false;
    const int selected = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
    if (selected < 0 || static_cast<size_t>(selected) >= items_.size()) return false;

    const DrillItem& item = items_[static_cast<size_t>(selected)];
    std::vector<std::wstring> paths;
    paths.push_back(ffui::JoinPath(currentPath_, item.name));

    HMENU menu = CreatePopupMenu();
    if (!menu) return false;

    AppendMenuW(menu, MF_STRING, 1, item.isDirectory ? L"Open" : L"Open");
    AppendMenuW(menu, MF_STRING, 2, L"Copy Path");
    AppendMenuW(menu, MF_STRING, 4, L"Move…");
    if (!item.isDirectory) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, 3, L"Delete");
    }

    const int cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                      GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), owner_, nullptr);
    DestroyMenu(menu);

    switch (cmd) {
        case 1:
            invokeCommand_(L"item.open", paths);
            return true;
        case 2:
            invokeCommand_(L"item.copy-path", paths);
            return true;
        case 3:
            invokeCommand_(L"file.delete", paths);
            return true;
        case 4:
            invokeCommand_(L"file.cut", paths);
            return true;
    }
    return false;
}

bool StorageAnalysis::HandleMouseMove(WPARAM wParam, LPARAM lParam) {
    (void)wParam;
    return treemapView_.HandleMouseMove(wParam, lParam);
}

bool StorageAnalysis::HandleLButtonDown(WPARAM wParam, LPARAM lParam) {
    (void)wParam;
    return treemapView_.HandleLButtonDown(wParam, lParam);
}

} // namespace ffui
