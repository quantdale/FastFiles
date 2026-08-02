#include "StorageAnalysis.h"

#include <commctrl.h>
#include <algorithm>

namespace ffui {
namespace {

constexpr int kListId = 7201;
constexpr int kStatusId = 7202;
constexpr int kBackId = 7203;
constexpr int kUpId = 7204;
constexpr UINT_PTR kRefreshTimer = 7205;
constexpr int kRefreshDelayMs = 50;

std::wstring FormatSize(uint64_t bytes) {
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        wchar_t buffer[64]{};
        swprintf_s(buffer, L"%.1f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
        return buffer;
    }
    if (bytes >= 1024ULL * 1024ULL) {
        wchar_t buffer[64]{};
        swprintf_s(buffer, L"%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
        return buffer;
    }
    if (bytes >= 1024ULL) {
        wchar_t buffer[64]{};
        swprintf_s(buffer, L"%.1f KB", static_cast<double>(bytes) / 1024.0);
        return buffer;
    }
    return std::to_wstring(bytes) + L" B";
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
                                 std::function<void()> close) {
    owner_ = owner;
    engine_ = engine;
    navigate_ = std::move(navigate);
    close_ = std::move(close);

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

    if (!back_ || !up_ || !status_ || !list_) return false;

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

    worker_ = std::thread(&StorageAnalysis::WorkerMain, this);
    return true;
}

void StorageAnalysis::ShowAndFocus(const std::wstring& currentPath, bool engineActive) {
    currentPath_ = currentPath;
    engineActive_ = engineActive;
    visible_ = true;
    for (HWND c : {back_, up_, list_, status_}) ShowWindow(c, SW_SHOW);
    RefreshData();
    Reposition();
    SetFocus(list_);
}

void StorageAnalysis::Hide() {
    if (!visible_) return;
    visible_ = false;
    KillTimer(owner_, kRefreshTimer);
    for (HWND c : {back_, up_, list_, status_}) ShowWindow(c, SW_HIDE);
    items_.clear();
    ListView_SetItemCountEx(list_, 0, LVSICF_NOSCROLL);
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
    SetWindowPos(status_, HWND_TOP, 180, top + 4, width - 200, 20, SWP_SHOWWINDOW);
    SetWindowPos(list_, HWND_TOP, 12, top + buttonHeight + 8, width - 24,
                 std::max(80, height - top - buttonHeight - 20), SWP_SHOWWINDOW);
}

void StorageAnalysis::RefreshData() {
    if (!visible_ || !engine_) return;
    const auto snapshot = engine_->ReadSnapshot();
    if (!snapshot) {
        SetWindowTextW(status_, L"Waiting for index data…");
        return;
    }

    std::wstring targetPath = currentPath_;
    if (targetPath.empty()) targetPath = L"";

    auto it = snapshot->find(targetPath);
    if (it == snapshot->end()) {
        SetWindowTextW(status_, L"Folder not yet indexed.");
        items_.clear();
        ListView_SetItemCountEx(list_, 0, LVSICF_NOSCROLL);
        return;
    }

    const auto& directory = it->second;
    if (directory.status != ffprotocol::DirectoryEnumerationStatus::Success) {
        SetWindowTextW(status_, L"This folder is not accessible.");
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

    SetWindowTextW(status_, std::to_wstring(items_.size()) + L" items");
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

    // Sort: folders first, then by name
    std::sort(items_.begin(), items_.end(), [](const DrillItem& a, const DrillItem& b) {
        if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    ListView_SetItemCountEx(list_, static_cast<int>(items_.size()), LVSICF_NOINVALIDATEALL);
    InvalidateRect(list_, nullptr, FALSE);
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
            auto payload = std::make_unique<ffprotocol::FolderAggregateResultPayload>(
                ffprotocol::FolderAggregateResultPayload{requestId, status, itemCount, totalSizeBytes});
            const HWND target = owner_;
            if (target != nullptr
                && PostMessageW(target, WM_APP_STORAGE_AGGREGATE, 0,
                                reinterpret_cast<LPARAM>(payload.get()))) {
                payload.release();
            }
        });
}

void StorageAnalysis::HandleAggregateResult(uint64_t requestId,
                                            ffprotocol::FolderAggregateStatus status,
                                            uint64_t itemCount, uint64_t totalSizeBytes) {
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
}

bool StorageAnalysis::HandleOwnerCommand(WPARAM wParam, LPARAM) {
    if (!visible_) return false;
    const int id = LOWORD(wParam);
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
    return false;
}

bool StorageAnalysis::HandleNotify(LPARAM lParam) {
    if (!visible_) return false;
    auto* header = reinterpret_cast<NMHDR*>(lParam);
    if (header == nullptr || header->hwndFrom != list_) return false;

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
                text = item.isDirectory ? L"—" : FormatSize(item.sizeBytes);
                break;
            case 3:
                if (item.isDirectory) {
                    text = item.calculating ? L"Calculating…" : FormatSize(item.totalSizeBytes);
                } else {
                    text = FormatSize(item.totalSizeBytes);
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
                std::wstring childPath = currentPath_;
                if (!childPath.empty() && childPath.back() != L'\\') childPath += L'\\';
                childPath += item.name;
                currentPath_ = childPath;
                RefreshData();
                if (navigate_) navigate_(currentPath_);
            } else if (navigate_) {
                std::wstring filePath = currentPath_;
                if (!filePath.empty() && filePath.back() != L'\\') filePath += L'\\';
                filePath += item.name;
                navigate_(filePath);
            }
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

} // namespace ffui
