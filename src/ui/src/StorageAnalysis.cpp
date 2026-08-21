#include "StorageAnalysis.h"

#include "TreemapView.h"
#include "UITheme.h"
#include "Util.h"
#include "ffui/StorageLayout.h"
#include "ffui/SnapshotAggregate.h"
#include "ffsearch/Path.h"

#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>

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

// Scale a DIP metric to physical pixels for Win32 control layout.
int Scaled(int dipValue) {
    return static_cast<int>(ffui::UiScale(static_cast<float>(dipValue)));
}

// storage-analysis 2.4: a volume that disappeared keeps its last-known
// capacity figures (labeled stale) instead of being silently dropped. A
// volume with no prior figures was never readable -- "stale" would imply
// data that no longer exists, so it is labeled unavailable instead.
constexpr wchar_t kStaleSuffix[] = L" (stale)";
constexpr wchar_t kUnavailableSuffix[] = L" (unavailable)";

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

} // namespace

StorageAnalysis::~StorageAnalysis() {
    uiaEvents_.Clear();
    treemapView_.SetUiaEventHandler(nullptr);
    Hide();
}

bool StorageAnalysis::Initialize(HWND owner, EngineClient* engine,
                                 std::function<void(const std::wstring& path, bool isDirectory)> navigate,
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

    const wchar_t* columns[] = {L"Name", L"Type", L"Size", L"Subtree Size", L"% of Parent", L"% of Volume", L"Modified", L"Size on Disk"};
    const int widths[] = {260, 80, 100, 120, 100, 100, 145, 120};
    for (int i = 0; i < 8; ++i) {
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

    // Wire the treemap into the storage panel so it renders the current folder
    // when the Treemap view mode is selected. Treemap navigation updates the
    // panel's current path and rebuilds the tree, then delegates to the shell.
    // Mouse clicks and UIA Invoke funnel through the same NavigateFromTreemap
    // mutation (semantic-treemap-uia 11/28).
    treemapView_.Initialize(owner_, engine_, [this](const std::wstring& path, bool isDirectory) {
        NavigateFromTreemap(path, isDirectory);
    });
    treemapView_.SetUiaEventHandler([this](const UiaEventNotification& notification) {
        uiaEvents_.Notify(notification);
    });
    treemapView_.SetCategoryFilter(&categoryEngine_, activeCategoryId_);
    return true;
}

// semantic-treemap-uia 11/28: the one real navigation mutation shared by the
// treemap mouse-click callback and the UI-thread side of a UIA Invoke.
void StorageAnalysis::NavigateFromTreemap(const std::wstring& path, bool isDirectory) {
    // Files are leaves: they are selected/inspected (the shell's navigate
    // handler opens them) but must never become the panel's scope anchor --
    // a file path in currentPath_ breaks Drill Down/Up/refresh lookups.
    if (isDirectory) {
        currentPath_ = path;
        treemapView_.ShowAndFocus(path);
    }
    if (navigate_) navigate_(path, isDirectory);
}

// semantic-treemap-uia 11.x: a UIA Invoke may arrive on a UIAutomationCore
// worker thread. Never mutate UI state there -- marshal a value-only payload
// to the owning UI thread, which re-validates the stable identity against
// CURRENT state and drops it if the tile changed (generation/scope/identity).
void StorageAnalysis::TreemapActivateItem(const TreemapUiaItemInfo& item) {
    if (!item.invokable) return;
    const DWORD uiThread = GetWindowThreadProcessId(owner_, nullptr);
    if (uiThread != 0 && GetCurrentThreadId() != uiThread) {
        auto payload = std::make_unique<TreemapNavigatePayload>();
        payload->generation = item.generation;
        payload->scopeHash = item.scopeHash;
        payload->identityHash = item.identityHash;
        payload->fullPath = item.fullPath;
        payload->isDirectory = item.isDirectory;
        if (PostMessageW(owner_, WM_APP_TREEMAP_NAVIGATE, 0,
                         reinterpret_cast<LPARAM>(payload.get()))) {
            payload.release();
            return;
        }
        // PostMessage failed (e.g. the window is being torn down): drop the
        // action safely -- never run navigation on this worker thread.
        return;
    }
    // Already on the UI thread: re-validate and run the real mutation.
    TreemapUiaItemInfo current;
    if (!treemapView_.UiaFindTile(item.identityHash, current)) return;
    if (current.generation != item.generation || current.scopeHash != item.scopeHash) return;
    if (!current.invokable) return;
    NavigateFromTreemap(current.fullPath, current.isDirectory);
}

bool StorageAnalysis::HandleTreemapUiaNavigate(LPARAM lParam) {
    std::unique_ptr<TreemapNavigatePayload> payload(reinterpret_cast<TreemapNavigatePayload*>(lParam));
    if (!payload) return true;
    // Re-resolve the stable identity in CURRENT state; a stale queued action
    // (snapshot refresh, category filter, scope navigation, Hide, teardown)
    // must never retarget the new occupant of the same position.
    TreemapUiaItemInfo current;
    if (!treemapView_.UiaFindTile(payload->identityHash, current)) return true;
    if (current.generation != payload->generation || current.scopeHash != payload->scopeHash) return true;
    if (!current.invokable) return true;
    NavigateFromTreemap(current.fullPath, current.isDirectory);
    return true;
}

void StorageAnalysis::SetCategories(const std::vector<std::pair<std::wstring, std::wstring>>& categories) {
    categoryEngine_.LoadFromSettings(categories);
    if (categoryFilter_ != nullptr) {
        PopulateCategoryFilter();
        treemapView_.SetCategoryFilter(&categoryEngine_, activeCategoryId_);
        if (visible_) RefreshData();
    }
}

void StorageAnalysis::PopulateCategoryFilter() {
    if (!categoryFilter_) return;
    ComboBox_ResetContent(categoryFilter_);
    categoryFilterIds_.clear();
    categoryFilterIds_.push_back(L"");
    ComboBox_AddString(categoryFilter_, L"All categories");
    const auto& categories = categoryEngine_.GetCategories();
    for (const auto& cat : categories) {
        categoryFilterIds_.push_back(cat.id);
        ComboBox_AddString(categoryFilter_, const_cast<wchar_t*>(cat.displayName.c_str()));
    }

    int selected = 0;
    for (size_t i = 1; i < categoryFilterIds_.size(); ++i) {
        if (categoryFilterIds_[i] == activeCategoryId_) {
            selected = static_cast<int>(i);
            break;
        }
    }
    if (selected == 0) activeCategoryId_.clear();
    ComboBox_SetCurSel(categoryFilter_, selected);
}

void StorageAnalysis::UpdateCategoryFilterFromControl() {
    activeCategoryId_.clear();
    if (!categoryFilter_) return;
    const int selected = ComboBox_GetCurSel(categoryFilter_);
    if (selected >= 0 && selected < static_cast<int>(categoryFilterIds_.size())) {
        activeCategoryId_ = categoryFilterIds_[static_cast<size_t>(selected)];
    }
}

void StorageAnalysis::ApplyCategoryFilter() {
    // storage-analysis 5.4: scope the displayed items to the selected
    // category, recalculating percentages relative to the filtered scope.
    // Directories stay (they can be drilled into); files whose category does
    // not match the filter are removed from the list.
    if (activeCategoryId_.empty()) return;
    items_.erase(std::remove_if(items_.begin(), items_.end(),
                                [this](const DrillItem& item) {
                                    if (item.isDirectory) return false;
                                    return !categoryEngine_.Matches(item.name, activeCategoryId_);
                                }),
                 items_.end());
}

void StorageAnalysis::ShowAndFocus(const std::wstring& currentPath, bool engineActive,
                                   const ShellLayoutRect& surfaceRect) {
    currentPath_ = currentPath;
    engineActive_ = engineActive;
    visible_ = true;
    // Open on the storage overview (2.1): the volume list is the entry point
    // for capacity figures and per-volume analysis.
    CheckRadioButton(owner_, kOverviewId, kTreemapId, kOverviewId);
    viewMode_ = ViewMode::Overview;
    ApplyModeVisibility();
    RefreshData();
    Reposition(surfaceRect);
    SetFocus(overview_);
}

void StorageAnalysis::Hide() {
    if (!visible_) return;
    visible_ = false;
    ApplyModeVisibility();
    items_.clear();
    volumes_.clear();
    ListView_SetItemCountEx(list_, 0, LVSICF_NOSCROLL);
    ListView_DeleteAllItems(overview_);
}

void StorageAnalysis::Reposition(const ShellLayoutRect& surfaceRect) {
    if (!visible_) return;
    const auto scaledDip = [](float dip) {
        return static_cast<int>(std::lround(ffui::UiScale(dip)));
    };
    const auto layout = ffui::ComputeStorageSurfaceLayout(surfaceRect);
    constexpr UINT kLayoutFlags = SWP_NOACTIVATE;
    const auto place = [scaledDip, kLayoutFlags](HWND control, const ShellLayoutRect& rect) {
        const int left = scaledDip(rect.left);
        const int top = scaledDip(rect.top);
        const int right = scaledDip(rect.right);
        const int bottom = scaledDip(rect.bottom);
        SetWindowPos(control, HWND_TOP, left, top, (std::max)(0, right - left),
                     (std::max)(0, bottom - top), kLayoutFlags);
    };
    place(back_, layout.buttons[0]);
    place(up_, layout.buttons[1]);
    place(overviewButton_, layout.buttons[2]);
    place(drillDown_, layout.buttons[3]);
    place(largestFolders_, layout.buttons[4]);
    place(largestFiles_, layout.buttons[5]);
    place(byCategory_, layout.buttons[6]);
    place(treemap_, layout.buttons[7]);
    place(categoryFilter_, layout.categoryFilter);
    place(status_, layout.status);
    place(list_, layout.list);
    place(overview_, layout.overview);
}

void StorageAnalysis::ApplyModeVisibility() {
    const auto apply = [this](HWND control, StorageControl semanticControl) {
        if (control == nullptr) return;
        const bool show = visible_ && StorageControlVisible(viewMode_, semanticControl);
        ShowWindow(control, show ? SW_SHOW : SW_HIDE);
    };

    apply(back_, StorageControl::Back);
    apply(up_, StorageControl::Up);
    apply(overviewButton_, StorageControl::OverviewButton);
    apply(drillDown_, StorageControl::DrillDownButton);
    apply(largestFolders_, StorageControl::LargestFoldersButton);
    apply(largestFiles_, StorageControl::LargestFilesButton);
    apply(byCategory_, StorageControl::ByCategoryButton);
    apply(treemap_, StorageControl::TreemapButton);
    apply(list_, StorageControl::List);
    apply(overview_, StorageControl::OverviewList);
    apply(categoryFilter_, StorageControl::CategoryFilter);
    apply(status_, StorageControl::Status);

    if (visible_ && StorageControlVisible(viewMode_, StorageControl::TreemapSurface)) {
        if (!treemapView_.Visible()) treemapView_.ShowAndFocus(currentPath_);
    } else {
        treemapView_.Hide();
    }
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
        // "Stale" implies prior figures exist; a volume that was never
        // readable is unavailable, not stale.
        if (vol.unavailable) name += vol.totalBytes > 0 ? kStaleSuffix : kUnavailableSuffix;
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

    // semantic-treemap-uia 22.x: percentage-of-volume denominator from the
    // same OS ground truth as the overview; the treemap metadata uses the
    // identical value.
    UpdateVolumeUsedBytes();
    treemapView_.SetVolumeInfo({volumeUsedBytes_, volumeUsedKnown_});

    // A refresh invalidates all outstanding row requests. Request IDs are
    // monotonic, so a late callback from the previous listing cannot be
    // mistaken for a row in this listing.
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pending_.clear();
    }
    parentAggregateTotal_ = 0;
    parentAggregateKnown_ = false;
    parentAggregatePending_ = false;
    parentRequestId_ = 0;

    // storage-analysis 2.1: the overview is the entry point; list mode views
    // are skipped entirely while it is active.
    if (viewMode_ == ViewMode::Overview) {
        RefreshOverview();
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
        // allocated-size-storage-accuracy 7.1: the entry's own size-on-disk
        // rides the snapshot with an explicit known state.
        item.allocatedSizeBytes = entry.allocatedSizeBytes;
        item.allocatedSizeKnown = entry.allocatedSizeKnown;
        item.attributes = entry.attributes;
        item.creationTime = entry.creationTime;
        item.lastModifiedTime = entry.lastModifiedTime;
        item.volumeRowId = entry.volumeRowId;
        item.fileIdLow = entry.fileIdLow;
        item.fileIdHigh = entry.fileIdHigh;
        item.parentFrnLow = entry.parentIdLow;
        item.parentFrnHigh = entry.parentIdHigh;
        item.calculating = entry.isDirectory && entry.volumeRowId != 0;
        if (entry.isDirectory && entry.volumeRowId == 0) {
            // Degraded snapshots do not have FRNs or a durable projection to
            // answer the aggregate request.  Sum the portion already present
            // in the immutable snapshot instead of leaving every directory in
            // a perpetual "Calculating" state.  Missing descendants remain
            // explicitly partial and therefore cannot produce a misleading
            // percentage or claim complete volume coverage.
            const auto aggregate = ffui::AggregateSnapshotDirectory(
                *snapshot, ffui::JoinSnapshotPath(targetPath, entry.name));
            item.totalSizeBytes = aggregate.totalSizeBytes;
            item.totalAllocatedSizeBytes = aggregate.totalAllocatedSizeBytes;
            item.allocatedKnownCount = aggregate.allocatedBytesKnownCount;
            item.allocatedTotalCount = aggregate.allocatedBytesTotalCount;
            item.calculating = false;
            item.aggregateFailed = !aggregate.complete;
        }
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
            catItem.sizeBytes += item.sizeBytes;
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

    if (directory.volumeRowId == 0) {
        const auto aggregate = ffui::AggregateSnapshotDirectory(*snapshot, targetPath);
        parentAggregateTotal_ = aggregate.totalSizeBytes;
        parentAggregateKnown_ = aggregate.complete;
        parentAggregatePending_ = false;
    } else {
        RequestAggregateForParent(directory);
    }
    ListView_SetItemCountEx(list_, static_cast<int>(items_.size()), LVSICF_NOINVALIDATEALL);
    InvalidateRect(list_, nullptr, FALSE);

    // Request aggregates for all directories
    for (size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].isDirectory && items_[i].volumeRowId != 0) {
            items_[i].requestId = nextRequestId_++;
            RequestAggregateForItem(i);
        }
    }

    SortItems(sortColumn_, sortAscending_);
}

void StorageAnalysis::RequestAggregateForParent(const ffprotocol::SnapshotDirectory& directory) {
    if (!engine_ || directory.volumeRowId == 0) return;
    const uint64_t requestId = nextRequestId_++;
    parentRequestId_ = requestId;
    parentAggregatePending_ = true;
    engine_->RequestFolderAggregate(
        directory.volumeRowId, directory.directoryIdLow, directory.directoryIdHigh,
        [this, requestId](uint64_t /*reqId*/, ffprotocol::FolderAggregateStatus status,
               uint64_t itemCount, uint64_t totalSizeBytes, uint64_t totalAllocatedSizeBytes,
               uint64_t allocatedBytesKnownCount, uint64_t allocatedBytesTotalCount) {
            ffui::PostFolderAggregateResult(owner_, WM_APP_STORAGE_AGGREGATE,
                                             requestId, status,
                                             itemCount, totalSizeBytes, totalAllocatedSizeBytes,
                                             allocatedBytesKnownCount, allocatedBytesTotalCount);
        });
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
        [this, requestId](uint64_t /*reqId*/, ffprotocol::FolderAggregateStatus status,
                          uint64_t itemCount, uint64_t totalSizeBytes, uint64_t totalAllocatedSizeBytes,
                          uint64_t allocatedBytesKnownCount, uint64_t allocatedBytesTotalCount) {
            // EngineClient's callback is already scoped to this request.  The
            // storage view intentionally uses its own compact request IDs for
            // ListView bookkeeping, so comparing the engine's global ID here
            // would discard every aggregate result after the first request.
            ffui::PostFolderAggregateResult(owner_, WM_APP_STORAGE_AGGREGATE, requestId, status, itemCount,
                                            totalSizeBytes, totalAllocatedSizeBytes,
                                            allocatedBytesKnownCount, allocatedBytesTotalCount);
        });
}

std::pair<uint64_t, bool> StorageAnalysis::CurrentParentTotals() const {
    // storage-analysis 5.4: with a category filter active, "% of Parent" is
    // recalculated relative to the FILTERED scope (matching the treemap's
    // filtered root total), so the denominator comes from the retained rows
    // instead of the engine's unfiltered aggregate.
    if (!activeCategoryId_.empty()) {
        uint64_t total = 0;
        bool known = true;
        for (const auto& item : items_) {
            if (item.isDirectory && (item.calculating || item.aggregateFailed)) {
                known = false;
            }
            if (UINT64_MAX - total < item.totalSizeBytes) {
                // A representationally impossible parent total must not wrap
                // and produce a plausible-looking percentage.
                return {0, false};
            }
            total += item.totalSizeBytes;
        }
        return {total, known};
    }
    if (parentAggregateKnown_) {
        return {parentAggregateTotal_, true};
    }
    if (parentAggregatePending_) {
        return {0, false};
    }
    uint64_t total = 0;
    bool known = true;
    for (const auto& item : items_) {
        if (item.isDirectory && (item.calculating || item.aggregateFailed)) {
            known = false;
        }
        if (UINT64_MAX - total < item.totalSizeBytes) {
            // A representationally impossible parent total must not wrap and
            // produce a plausible-looking percentage.
            return {0, false};
        }
        total += item.totalSizeBytes;
    }
    return {total, known};
}

void StorageAnalysis::HandleAggregateResult(uint64_t requestId,
                                            ffprotocol::FolderAggregateStatus status,
                                            uint64_t /*itemCount*/, uint64_t totalSizeBytes,
                                            uint64_t totalAllocatedSizeBytes,
                                            uint64_t allocatedBytesKnownCount,
                                            uint64_t allocatedBytesTotalCount) {
    if (requestId == parentRequestId_) {
        parentAggregatePending_ = false;
        parentAggregateKnown_ = status == ffprotocol::FolderAggregateStatus::Resolved;
        parentAggregateTotal_ = parentAggregateKnown_ ? totalSizeBytes : 0;
        // The percentage and size sort keys depend on the parent aggregate.
        // Re-sort after the asynchronous parent result arrives so an owner-
        // data list does not keep the provisional order indefinitely.
        SortItems(sortColumn_, sortAscending_);
        ListView_SetItemCountEx(list_, static_cast<int>(items_.size()), LVSICF_NOINVALIDATEALL);
        InvalidateRect(list_, nullptr, FALSE);
        return;
    }
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
        // allocated-size-storage-accuracy 7.1: store the aggregate's
        // allocated subtotal plus the completeness pair; the rendering
        // layer decides exact / partial / unknown from the counts.
        itemIt->totalAllocatedSizeBytes = totalAllocatedSizeBytes;
        itemIt->allocatedKnownCount = allocatedBytesKnownCount;
        itemIt->allocatedTotalCount = allocatedBytesTotalCount;
        itemIt->calculating = false;
        itemIt->aggregateFailed = false;
    } else if (status == ffprotocol::FolderAggregateStatus::NotFound) {
        itemIt->totalSizeBytes = 0;
        itemIt->totalAllocatedSizeBytes = 0;
        itemIt->allocatedKnownCount = 0;
        itemIt->allocatedTotalCount = 0;
        itemIt->calculating = false;
        itemIt->aggregateFailed = true;
    }

    // Folder size and percentage columns are sortable; the completed value can
    // change the item's relative position, so refresh the ordering before the
    // owner-data list repaints.
    SortItems(sortColumn_, sortAscending_);
    ListView_SetItemCountEx(list_, static_cast<int>(items_.size()), LVSICF_NOINVALIDATEALL);
    InvalidateRect(list_, nullptr, FALSE);
}

// semantic-treemap-uia 22.x: the "overall volume" denominator for
// percentage-of-volume. Same GetDiskFreeSpaceExW ground truth as the storage
// overview (RefreshOverview); the volume hosting the current scope.
void StorageAnalysis::UpdateVolumeUsedBytes() {
    volumeUsedKnown_ = false;
    volumeUsedBytes_ = 0;
    if (currentPath_.empty() || currentPath_[0] == L'\0') return;
    wchar_t root[4] = {currentPath_[0], L':', L'\\', L'\0'};
    if (currentPath_.size() >= 2 && currentPath_[1] != L':') return;
    ULARGE_INTEGER total{}, freeAvail{};
    if (GetDiskFreeSpaceExW(root, &freeAvail, &total, nullptr)) {
        volumeUsedBytes_ = total.QuadPart > freeAvail.QuadPart ? total.QuadPart - freeAvail.QuadPart : 0;
        volumeUsedKnown_ = true;
    }
}

void StorageAnalysis::SortItems(int column, bool ascending) {
    if (column < 0 || column >= 8) return;
    sortColumn_ = column;
    sortAscending_ = ascending;
    SetSortIndicator(list_, column, ascending);

    struct SortKey {
        int group = 0;
        bool known = true;
        double value = 0.0;
    };
    const auto [parentTotal, parentKnown] = CurrentParentTotals();
    auto getSortKey = [this, parentTotal, parentKnown](const DrillItem& item, int col) -> SortKey {
        switch (col) {
            case 0: return {item.isDirectory ? 0 : 1, true, 0.0}; // name: folders first, then name
            case 1: return {item.isDirectory ? 0 : 1, true, 0.0}; // type: folders first
            case 2: return {item.isDirectory ? 0 : 1, true, static_cast<double>(item.sizeBytes)}; // size
            case 3: return {item.isDirectory ? 0 : 1, !item.calculating && !item.aggregateFailed,
                                   static_cast<double>(item.totalSizeBytes)}; // subtree size
            case 4: {
                const auto percentage = ComputeStoragePercentage(
                    item.totalSizeBytes, parentTotal, parentKnown &&
                    (!item.isDirectory || (!item.calculating && !item.aggregateFailed)));
                return {item.isDirectory ? 0 : 1, percentage.known, percentage.sortKey};
            }
            case 5: {
                // semantic-treemap-uia 22.x: percentage of the overall volume
                // (OS used bytes). Unknown denominators/items sort last.
                const auto percentage = ComputeStoragePercentage(
                    item.totalSizeBytes, volumeUsedBytes_, volumeUsedKnown_ &&
                    (!item.isDirectory || (!item.calculating && !item.aggregateFailed)));
                return {item.isDirectory ? 0 : 1, percentage.known, percentage.sortKey};
            }
            case 6: return {0, true, static_cast<double>(item.lastModifiedTime)}; // modified
            case 7:
                // allocated-size-storage-accuracy 7.1: size-on-disk column --
                // files sort on their own allocated size (known first),
                // directories on the aggregate's known subtotal.
                if (item.isDirectory) {
                    return {item.isDirectory ? 0 : 1,
                            !item.calculating && !item.aggregateFailed && item.allocatedKnownCount > 0,
                            static_cast<double>(item.totalAllocatedSizeBytes)};
                }
                return {1, item.allocatedSizeKnown, static_cast<double>(item.allocatedSizeBytes)};
            default: return {0, true, 0.0};
        }
    };

    std::sort(items_.begin(), items_.end(), [this, column, ascending, getSortKey](const DrillItem& a, const DrillItem& b) {
        const auto keyA = getSortKey(a, column);
        const auto keyB = getSortKey(b, column);
        if (keyA.group != keyB.group) return ascending ? keyA.group < keyB.group : keyA.group > keyB.group;
        if (keyA.known != keyB.known) return ascending ? keyA.known > keyB.known : keyA.known < keyB.known;
        const bool equalValue = (column == 4 || column == 5)
            ? StoragePercentageNearlyEqual(keyA.value, keyB.value)
            : keyA.value == keyB.value;
        if (!equalValue) {
            return ascending ? keyA.value < keyB.value : keyA.value > keyB.value;
        }
        // tie-break on name
        return ascending ? _wcsicmp(a.name.c_str(), b.name.c_str()) < 0 : _wcsicmp(a.name.c_str(), b.name.c_str()) > 0;
    });

    ListView_SetItemCountEx(list_, static_cast<int>(items_.size()), LVSICF_NOINVALIDATEALL);
    InvalidateRect(list_, nullptr, FALSE);
}

void StorageAnalysis::OnSnapshotUpdated() {
    if (!visible_) return;
    RefreshData();
    treemapView_.OnSnapshotUpdated();
}

void StorageAnalysis::RenderTreemap(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory, D2D1_SIZE_F viewportSize, float offsetX, float offsetY) {
    if (viewMode_ != ViewMode::Treemap) return;
    treemapView_.EnsureCreated(context, dwriteFactory);
    treemapView_.Render(context, dwriteFactory, viewportSize, offsetX, offsetY);
}

bool StorageAnalysis::HandleOwnerCommand(WPARAM wParam, LPARAM) {
    if (!visible_) return false;
    const int id = LOWORD(wParam);
    if (id == kCategoryFilterId && HIWORD(wParam) == CBN_SELCHANGE) {
        UpdateCategoryFilterFromControl();
        treemapView_.SetCategoryFilter(&categoryEngine_, activeCategoryId_);
        RefreshData();
        return true;
    }
    if (id == kBackId) {
        if (close_) close_();
        return true;
    }
    if (id == kUpId) {
        if (!currentPath_.empty()) {
            const std::wstring parent = ffsearch::ParentPath(currentPath_);
            if (!parent.empty()) {
                currentPath_ = parent;
                RefreshData();
                if (navigate_) navigate_(currentPath_, true);
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
    if (viewMode_ == mode) {
        ApplyModeVisibility();
        return;
    }
    viewMode_ = mode;
    CheckRadioButton(owner_, kOverviewId, kTreemapId,
                     mode == ViewMode::Overview ? kOverviewId :
                     mode == ViewMode::DrillDown ? kDrillDownId :
                     mode == ViewMode::LargestFolders ? kLargestFoldersId :
                     mode == ViewMode::LargestFiles ? kLargestFilesId :
                     mode == ViewMode::ByCategory ? kByCategoryId : kTreemapId);
    ApplyModeVisibility();
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
                if (navigate_) navigate_(currentPath_, true);
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
                    if (item.calculating) text = L"Calculating…";
                    else if (item.aggregateFailed) text = L"Partial";
                    else text = ffui::FormatSize(item.totalSizeBytes);
                } else {
                    text = ffui::FormatSize(item.totalSizeBytes);
                }
                break;
            case 4: {
                const auto [parentTotal, parentKnown] = CurrentParentTotals();
                const auto percentage = ComputeStoragePercentage(
                    item.totalSizeBytes, parentTotal, parentKnown &&
                    (!item.isDirectory || (!item.calculating && !item.aggregateFailed)));
                text = percentage.display;
                break;
            }
            case 5: {
                // semantic-treemap-uia 22.x: % of Volume -- the same
                // ComputeStoragePercentage helper and the same OS used-bytes
                // denominator the treemap metadata uses (one percentage
                // source, never a second implementation).
                const auto percentage = ComputeStoragePercentage(
                    item.totalSizeBytes, volumeUsedBytes_, volumeUsedKnown_ &&
                    (!item.isDirectory || (!item.calculating && !item.aggregateFailed)));
                text = percentage.display;
                break;
            }
            case 6:
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
            case 7: {
                // allocated-size-storage-accuracy 7.1: Size on Disk column.
                // Unknown is never rendered as 0 B; a partial directory
                // aggregate is labeled with its known/total qualifier and a
                // lower-bound marker -- never presented as an exact total.
                if (item.isDirectory) {
                    if (item.calculating) {
                        text = L"Calculating…";
                    } else if (item.allocatedTotalCount == 0) {
                        text = ffui::FormatSize(0); // empty subtree: exactly zero on disk
                    } else if (item.allocatedKnownCount == item.allocatedTotalCount) {
                        text = ffui::FormatSize(item.totalAllocatedSizeBytes);
                    } else if (item.allocatedKnownCount == 0) {
                        text = L"—"; // descendants exist but none has a known size-on-disk
                    } else {
                        text = L"≥ " + ffui::FormatSize(item.totalAllocatedSizeBytes) + L" (" +
                               std::to_wstring(item.allocatedKnownCount) + L"/" +
                               std::to_wstring(item.allocatedTotalCount) + L")";
                    }
                } else {
                    text = item.allocatedSizeKnown ? ffui::FormatSize(item.allocatedSizeBytes) : L"—";
                }
                break;
            }
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
                if (navigate_) navigate_(currentPath_, true);
            } else if (navigate_) {
                std::wstring filePath = ffui::JoinPath(currentPath_, item.name);
                navigate_(filePath, false);
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

bool StorageAnalysis::HandleCompletion(LPARAM lParam) {
    // allocated-size-storage-accuracy 6.3: the posted payload is the
    // additive V2 shape (logical + allocated + completeness pair).
    auto* payload = reinterpret_cast<ffprotocol::FolderAggregateResultV2Payload*>(lParam);
    if (!payload) return true;
    HandleAggregateResult(payload->requestId, payload->status, payload->itemCount, payload->totalSizeBytes,
                          payload->totalAllocatedSizeBytes, payload->allocatedBytesKnownCount,
                          payload->allocatedBytesTotalCount);
    delete payload;
    return true;
}

bool StorageAnalysis::HandleTreemapCompletion(LPARAM lParam) {
    return treemapView_.HandleAggregateCompletion(lParam);
}

void StorageAnalysis::SetEngineActive(bool active) {
    engineActive_ = active;
}

void StorageAnalysis::SetDarkTheme(bool dark) {
    darkTheme_ = dark;
    treemapView_.SetDarkTheme(dark);
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
    AppendMenuW(menu, MF_STRING, 4, L"Cut");
    if (!item.isDirectory) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, 3, L"Delete");
    }

    // Keyboard invocation (Shift+F10 / menu key) passes lParam == -1: anchor
    // the menu at the selected item's on-screen rect instead of (-1,-1).
    POINT menuPos{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    if (lParam == static_cast<LPARAM>(-1)) {
        RECT itemRect{};
        if (ListView_GetItemRect(list_, selected, &itemRect, LVIR_BOUNDS)) {
            menuPos = POINT{itemRect.left, itemRect.bottom};
            ClientToScreen(list_, &menuPos);
        } else {
            GetCursorPos(&menuPos);
        }
    }

    const int cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                      menuPos.x, menuPos.y, owner_, nullptr);
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
