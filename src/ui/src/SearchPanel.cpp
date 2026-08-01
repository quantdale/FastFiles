#include "SearchPanel.h"

#include <algorithm>
#include <commctrl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <fstream>
#include <unordered_map>
#include <wrl/client.h>

namespace ffui {
namespace {
constexpr int kQueryId = 7101;
constexpr int kScopeId = 7102;
constexpr int kSortId = 7103;
constexpr int kListId = 7104;
constexpr int kClearHistoryId = 7105;
constexpr int kSortDirectionId = 7107;
constexpr UINT_PTR kDebounceTimer = 7106;
constexpr UINT kDebounceMilliseconds = 125;

using Microsoft::WRL::ComPtr;

void PaintSearchEdit(HWND hwnd) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    if (!dc) return;

    RECT client{};
    GetClientRect(hwnd, &client);
    ComPtr<ID2D1Factory> factory;
    ComPtr<ID2D1DCRenderTarget> target;
    ComPtr<IDWriteFactory> writeFactory;
    ComPtr<IDWriteTextFormat> format;
    if (SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&factory)))) {
        D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            0.0f, 0.0f, D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);
        if (SUCCEEDED(factory->CreateDCRenderTarget(&properties, &target)) &&
            SUCCEEDED(target->BindDC(dc, &client)) &&
            SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                           reinterpret_cast<IUnknown**>(writeFactory.GetAddressOf()))) &&
            SUCCEEDED(writeFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                                      DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                      14.0f, L"en-us", &format))) {
            target->BeginDraw();
            target->Clear(D2D1::ColorF(D2D1::ColorF::White));
            ComPtr<ID2D1SolidColorBrush> border;
            ComPtr<ID2D1SolidColorBrush> textBrush;
            target->CreateSolidColorBrush(D2D1::ColorF(0x7A8AA0), &border);
            target->CreateSolidColorBrush(D2D1::ColorF(0x1B2430), &textBrush);
            const auto bounds = D2D1::RectF(0.5f, 0.5f,
                                             static_cast<float>(client.right) - 0.5f,
                                             static_cast<float>(client.bottom) - 0.5f);
            if (border) target->DrawRoundedRectangle(D2D1::RoundedRect(bounds, 4.0f, 4.0f), border.Get(), 1.0f);

            int length = GetWindowTextLengthW(hwnd);
            std::wstring text(static_cast<size_t>(length) + 1, L'\0');
            GetWindowTextW(hwnd, text.data(), length + 1);
            text.resize(static_cast<size_t>(length));
            if (text.empty()) {
                text = L"Search indexed locations…";
                textBrush.Reset();
                target->CreateSolidColorBrush(D2D1::ColorF(0x6B7785), &textBrush);
            }
            const auto textRect = D2D1::RectF(10.0f, 3.0f,
                                              static_cast<float>(client.right) - 10.0f,
                                              static_cast<float>(client.bottom) - 3.0f);
            target->DrawText(text.c_str(), static_cast<UINT32>(text.size()), format.Get(), textRect,
                             textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            target->EndDraw();
        }
    }
    EndPaint(hwnd, &paint);
}

LRESULT CALLBACK SearchEditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                UINT_PTR subclassId, DWORD_PTR refData) {
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(lParam);
    UNREFERENCED_PARAMETER(subclassId);
    UNREFERENCED_PARAMETER(refData);
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT) {
        PaintSearchEdit(hwnd);
        return 0;
    }
    if (message == WM_SETTEXT || message == WM_CLEAR || message == WM_CUT || message == WM_PASTE) {
        const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
        InvalidateRect(hwnd, nullptr, FALSE);
        return result;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

std::wstring JoinPath(const std::wstring& folder, const std::wstring& name) {
    if (folder.empty()) return name;
    return folder.back() == L'\\' ? folder + name : folder + L"\\" + name;
}

std::wstring LeafName(const std::wstring& path) {
    if (path.size() == 3 && path[1] == L':') return path;
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring ParentPath(const std::wstring& path) {
    if (path.size() <= 3) return {};
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash);
}

int64_t VolumeFor(const std::wstring& path) {
    if (path.size() >= 2 && path[1] == L':') return static_cast<int64_t>(towupper(path[0]));
    return static_cast<int64_t>(std::hash<std::wstring>{}(path.substr(0, path.find(L'\\', 2))));
}

std::wstring FormatSize(uint64_t bytes) {
    if (bytes >= 1024 * 1024) return std::to_wstring(bytes / (1024 * 1024)) + L" MB";
    if (bytes >= 1024) return std::to_wstring(bytes / 1024) + L" KB";
    return std::to_wstring(bytes) + L" B";
}

std::wstring FormatFileTime(uint64_t ticks) {
    if (ticks == 0) return L"—";
    ULARGE_INTEGER value{};
    value.QuadPart = ticks;
    FILETIME fileTime{value.LowPart, value.HighPart};
    SYSTEMTIME time{};
    if (!FileTimeToSystemTime(&fileTime, &time)) return L"—";
    wchar_t text[32]{};
    swprintf_s(text, L"%04u-%02u-%02u %02u:%02u", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute);
    return text;
}
}

SearchPanel::~SearchPanel() {
    if (queryEdit_) RemoveWindowSubclass(queryEdit_, SearchEditProc, 1);
    stopping_ = true;
    currentGeneration_.fetch_add(1);
    workCv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

bool SearchPanel::Initialize(HWND owner, EngineClient* engine,
                             std::function<void(const ffsearch::Candidate&, const ffsearch::PathReconstruction&)> navigate,
                             bool retainHistory) {
    owner_ = owner;
    engine_ = engine;
    navigate_ = std::move(navigate);
    retainHistory_ = retainHistory;
    historyStore_.SetRecordingEnabled(retainHistory);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&controls);
    query_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWN | WS_VSCROLL,
                             0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kQueryId)), nullptr, nullptr);
    scope_ = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
                             0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kScopeId)), nullptr, nullptr);
    sort_ = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST,
                            0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSortId)), nullptr, nullptr);
    list_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                            WS_CHILD | LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | WS_TABSTOP,
                            0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kListId)), nullptr, nullptr);
    status_ = CreateWindowExW(0, L"STATIC", L"Ready", WS_CHILD, 0, 0, 0, 0, owner, nullptr, nullptr, nullptr);
    clearHistory_ = CreateWindowExW(0, L"BUTTON", L"Clear history", WS_CHILD,
                                    0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kClearHistoryId)), nullptr, nullptr);
    sortDirection_ = CreateWindowExW(0, L"BUTTON", L"Ascending", WS_CHILD,
                                     0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSortDirectionId)), nullptr, nullptr);
    if (!query_ || !scope_ || !sort_ || !list_ || !status_ || !clearHistory_ || !sortDirection_) return false;
    COMBOBOXINFO comboInfo{sizeof(comboInfo)};
    if (GetComboBoxInfo(query_, &comboInfo) && comboInfo.hwndItem) {
        queryEdit_ = comboInfo.hwndItem;
        SetWindowSubclass(queryEdit_, SearchEditProc, 1, reinterpret_cast<DWORD_PTR>(this));
    }
    for (const wchar_t* text : {L"Current Folder", L"Current Folder + Subfolders", L"Current Drive", L"All Indexed Locations"})
        SendMessageW(scope_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    for (const wchar_t* text : {L"Relevance", L"Filename", L"Path", L"Size", L"Modified", L"Created"})
        SendMessageW(sort_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    SendMessageW(sort_, CB_SETCURSEL, 0, 0);
    const wchar_t* columns[] = {L"Name", L"Type", L"Location", L"Size", L"Modified"};
    const int widths[] = {220, 80, 360, 100, 145};
    for (int index = 0; index < 5; ++index) {
        LVCOLUMNW column{LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM};
        column.pszText = const_cast<wchar_t*>(columns[index]);
        column.cx = widths[index];
        column.iSubItem = index;
        ListView_InsertColumn(list_, index, &column);
    }
    ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    LoadHistory();
    worker_ = std::thread(&SearchPanel::WorkerMain, this);
    return true;
}

void SearchPanel::ShowAndFocus(const std::wstring& currentPath, bool engineActive) {
    currentPath_ = currentPath;
    engineActive_ = engineActive;
    visible_ = true;
    for (HWND control : {query_, scope_, sort_, sortDirection_, list_, status_, clearHistory_}) ShowWindow(control, SW_SHOW);
    UpdateScopeAvailability(false);
    Reposition();
    SetFocus(GetWindow(query_, GW_CHILD));
}

void SearchPanel::Hide() {
    visible_ = false;
    KillTimer(owner_, kDebounceTimer);
    for (HWND control : {query_, scope_, sort_, sortDirection_, list_, status_, clearHistory_}) ShowWindow(control, SW_HIDE);
}

void SearchPanel::Reposition() {
    if (!visible_) return;
    RECT client{}; GetClientRect(owner_, &client);
    const int width = static_cast<int>(client.right);
    const int height = static_cast<int>(client.bottom);
    SetWindowPos(query_, HWND_TOP, 12, 44, (std::max)(180, width - 700), 300, SWP_SHOWWINDOW);
    SetWindowPos(scope_, HWND_TOP, (std::max)(194, width - 508), 44, 210, 300, SWP_SHOWWINDOW);
    SetWindowPos(sort_, HWND_TOP, (std::max)(406, width - 296), 44, 126, 300, SWP_SHOWWINDOW);
    SetWindowPos(sortDirection_, HWND_TOP, width - 158, 44, 146, 26, SWP_SHOWWINDOW);
    SetWindowPos(status_, HWND_TOP, 12, 76, width - 150, 24, SWP_SHOWWINDOW);
    SetWindowPos(clearHistory_, HWND_TOP, width - 126, 72, 114, 26, SWP_SHOWWINDOW);
    SetWindowPos(list_, HWND_TOP, 12, 104, width - 24, (std::max)(80, height - 116), SWP_SHOWWINDOW);
}

bool SearchPanel::HandleOwnerCommand(WPARAM wParam, LPARAM) {
    if (!visible_) return false;
    const int id = LOWORD(wParam), notification = HIWORD(wParam);
    if (id == kQueryId && notification == CBN_EDITCHANGE) {
        if (updatingHistory_) return true;
        ++generation_;
        currentGeneration_ = generation_;
        KillTimer(owner_, kDebounceTimer);
        SetTimer(owner_, kDebounceTimer, kDebounceMilliseconds, nullptr);
        UpdateStatus(L"Waiting for typing to pause…");
        return true;
    }
    if (id == kScopeId && notification == CBN_SELCHANGE) {
        if (!engineActive_ && SendMessageW(scope_, CB_GETCURSEL, 0, 0) >= 2) {
            SendMessageW(scope_, CB_SETCURSEL, 1, 0);
            UpdateStatus(L"Current Drive and All Indexed Locations require the active index service.");
        }
        Dispatch();
        return true;
    }
    if (id == kSortId && notification == CBN_SELCHANGE) {
        Dispatch();
        return true;
    }
    if (id == kSortDirectionId && notification == BN_CLICKED) {
        descending_ = !descending_;
        SetWindowTextW(sortDirection_, descending_ ? L"Descending" : L"Ascending");
        Dispatch();
        return true;
    }
    if (id == kClearHistoryId && notification == BN_CLICKED) {
        ClearHistory();
        return true;
    }
    return false;
}

bool SearchPanel::HandleTimer(UINT_PTR timerId) {
    if (!visible_ || timerId != kDebounceTimer) return false;
    KillTimer(owner_, kDebounceTimer);
    Dispatch();
    return true;
}

void SearchPanel::Dispatch() {
    wchar_t queryText[1024]{};
    GetWindowTextW(query_, queryText, static_cast<int>(std::size(queryText)));
    ++generation_;
    currentGeneration_ = generation_;
    if (queryText[0] == L'\0') {
        results_.clear();
        ListView_SetItemCountEx(list_, 0, LVSICF_NOSCROLL);
        UpdateStatus(history_.empty() ? L"Type to search" : L"Choose a previous query or type to search");
        return;
    }
    Work work;
    work.generation = generation_;
    PopulateCandidates(work.candidates);
    work.request.query = ffsearch::ParseQuery(queryText, ffsearch::FilterRegistry::WithDefaults());
    work.request.scope = static_cast<ffsearch::SearchScope>((std::max)(0, static_cast<int>(SendMessageW(scope_, CB_GETCURSEL, 0, 0))));
    work.request.scopePath = currentPath_;
    work.request.sortField = static_cast<ffsearch::SortField>((std::max)(0, static_cast<int>(SendMessageW(sort_, CB_GETCURSEL, 0, 0))));
    work.request.descending = descending_;
    UpdateStatus(L"Searching…");
    RecordHistory(queryText);
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        pendingWork_ = std::move(work);
    }
    workCv_.notify_one();
}

void SearchPanel::WorkerMain() {
    while (!stopping_) {
        Work work;
        {
            std::unique_lock<std::mutex> lock(workMutex_);
            workCv_.wait(lock, [this] { return stopping_ || pendingWork_.has_value(); });
            if (stopping_) break;
            work = std::move(*pendingWork_);
            pendingWork_.reset();
        }
        auto response = ffsearch::ExecuteSearch(work.candidates, work.request, [this, generation = work.generation] {
            return stopping_ || currentGeneration_.load() != generation;
        });
        if (response.cancelled) continue;
        auto completion = std::make_unique<Completion>(Completion{work.generation, std::move(response), std::move(work.candidates)});
        if (PostMessageW(owner_, WM_APP_SEARCH_COMPLETE, 0, reinterpret_cast<LPARAM>(completion.get()))) completion.release();
    }
}

bool SearchPanel::HandleCompletion(LPARAM lParam) {
    std::unique_ptr<Completion> completion(reinterpret_cast<Completion*>(lParam));
    if (!completion || completion->generation != currentGeneration_) return true;
    results_ = std::move(completion->response.results);
    candidates_ = std::move(completion->candidates);
    ListView_SetItemCountEx(list_, static_cast<int>(results_.size()), LVSICF_NOINVALIDATEALL);
    UpdateStatus(results_.empty() ? L"No results" : std::to_wstring(results_.size()) + L" results");
    InvalidateRect(list_, nullptr, FALSE);
    return true;
}

bool SearchPanel::HandleNotify(LPARAM lParam) {
    if (!visible_) return false;
    auto* header = reinterpret_cast<NMHDR*>(lParam);
    if (header == nullptr || header->hwndFrom != list_) return false;
    if (header->code == LVN_GETDISPINFOW) {
        auto* info = reinterpret_cast<NMLVDISPINFOW*>(lParam);
        if (info->item.iItem < 0 || static_cast<size_t>(info->item.iItem) >= results_.size() ||
            (info->item.mask & LVIF_TEXT) == 0) return true;
        const auto& candidate = results_[static_cast<size_t>(info->item.iItem)].candidate;
        std::wstring text;
        switch (info->item.iSubItem) {
            case 0: text = candidate.name; break;
            case 1: text = candidate.isDirectory ? L"Folder" : L"File"; break;
            case 2: text = candidate.folder; break;
            case 3: text = candidate.isDirectory ? L"—" : FormatSize(candidate.sizeBytes); break;
            case 4: text = FormatFileTime(candidate.modifiedTime); break;
        }
        wcsncpy_s(info->item.pszText, static_cast<size_t>(info->item.cchTextMax), text.c_str(), _TRUNCATE);
        return true;
    }
    if (header->code == NM_DBLCLK || header->code == LVN_ITEMACTIVATE) {
        const int selected = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
        if (selected >= 0 && static_cast<size_t>(selected) < results_.size()) {
            const auto candidate = results_[static_cast<size_t>(selected)].candidate;
            const auto found = std::find_if(candidates_.begin(), candidates_.end(), [&](const ffsearch::Candidate& entry) {
                return entry.volumeId == candidate.volumeId && entry.id == candidate.id;
            });
            const auto reconstruction = found == candidates_.end()
                ? ffsearch::PathReconstruction{}
                : ffsearch::ReconstructPath(candidates_, static_cast<size_t>(found - candidates_.begin()));
            Hide();
            navigate_(candidate, reconstruction);
        }
        return true;
    }
    return false;
}

bool SearchPanel::HandleDrawItem(LPARAM lParam) {
    const auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
    if (item == nullptr || item->hwndItem != scope_ || item->itemID == static_cast<UINT>(-1)) return false;
    wchar_t text[128]{};
    SendMessageW(scope_, CB_GETLBTEXT, item->itemID, reinterpret_cast<LPARAM>(text));
    const bool disabled = !engineActive_ && item->itemID >= 2;
    const bool selected = (item->itemState & ODS_SELECTED) != 0 && !disabled;
    FillRect(item->hDC, &item->rcItem, GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW));
    SetTextColor(item->hDC, GetSysColor(disabled ? COLOR_GRAYTEXT : selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
    SetBkMode(item->hDC, TRANSPARENT);
    RECT textRect = item->rcItem;
    textRect.left += 4;
    DrawTextW(item->hDC, text, -1, &textRect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    if ((item->itemState & ODS_FOCUS) != 0) DrawFocusRect(item->hDC, &item->rcItem);
    return true;
}

void SearchPanel::PopulateCandidates(std::vector<ffsearch::Candidate>& candidates) const {
    const auto snapshot = engine_->ReadSnapshot();
    if (!snapshot) return;
    std::unordered_map<std::wstring, uint64_t> directoryIds;
    uint64_t nextId = 1;
    for (const auto& [path, directory] : *snapshot) {
        if (directory.status == ffprotocol::DirectoryEnumerationStatus::Success)
            directoryIds.emplace(path, directory.directoryIdLow != 0 ? directory.directoryIdLow : nextId++);
    }
    std::unordered_map<std::wstring, size_t> directoryIndexes;
    for (const auto& [path, id] : directoryIds) {
        const std::wstring parent = ParentPath(path);
        const auto parentId = directoryIds.find(parent);
        const auto& directory = snapshot->at(path);
        candidates.push_back({LeafName(path), parent, 0, 0, true, 0,
                              directory.volumeRowId != 0 ? directory.volumeRowId : VolumeFor(path), id,
                              directory.parentIdLow != 0 ? directory.parentIdLow : (parentId == directoryIds.end() ? id : parentId->second)});
        directoryIndexes.emplace(path, candidates.size() - 1);
    }
    for (const auto& [path, directory] : *snapshot) {
        if (directory.status != ffprotocol::DirectoryEnumerationStatus::Success) continue;
        const uint64_t parentId = directoryIds.contains(path) ? directoryIds.at(path) : 0;
        for (const auto& entry : directory.entries) {
            const std::wstring fullPath = JoinPath(path, entry.name);
            if (entry.isDirectory && directoryIndexes.contains(fullPath)) {
                auto& candidate = candidates[directoryIndexes.at(fullPath)];
                candidate.sizeBytes = entry.sizeBytes;
                candidate.createdTime = entry.creationTime;
                candidate.modifiedTime = entry.lastModifiedTime;
                continue;
            }
            const uint64_t id = entry.isDirectory && directoryIds.contains(fullPath) ? directoryIds.at(fullPath) : nextId++;
            candidates.push_back({entry.name, path, entry.sizeBytes, entry.lastModifiedTime, entry.isDirectory,
                                  entry.creationTime, entry.volumeRowId != 0 ? entry.volumeRowId : VolumeFor(path),
                                  entry.fileIdLow != 0 ? entry.fileIdLow : id,
                                  entry.parentIdLow != 0 ? entry.parentIdLow : parentId});
        }
    }
}

void SearchPanel::SetEngineActive(bool active) {
    const bool lostBroadScope = engineActive_ && !active && SendMessageW(scope_, CB_GETCURSEL, 0, 0) >= 2;
    engineActive_ = active;
    if (visible_) UpdateScopeAvailability(lostBroadScope);
}

void SearchPanel::UpdateScopeAvailability(bool showFallbackNotice) {
    if (!engineActive_) {
        const int selected = static_cast<int>(SendMessageW(scope_, CB_GETCURSEL, 0, 0));
        if (selected < 0 || selected >= 2) SendMessageW(scope_, CB_SETCURSEL, 1, 0);
        if (showFallbackNotice) UpdateStatus(L"Index service disconnected; scope narrowed to Current Folder + Subfolders.");
    } else if (SendMessageW(scope_, CB_GETCURSEL, 0, 0) < 0) {
        SendMessageW(scope_, CB_SETCURSEL, 3, 0);
    }
    InvalidateRect(scope_, nullptr, TRUE);
}

void SearchPanel::UpdateStatus(const std::wstring& text) { SetWindowTextW(status_, text.c_str()); }

std::filesystem::path SearchPanel::HistoryPath() {
    std::vector<wchar_t> localAppData(32768);
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData.data(), static_cast<DWORD>(localAppData.size()));
    if (length == 0 || length >= localAppData.size()) return {};
    return std::filesystem::path(localAppData.data()) / L"FastFiles" / L"search-history.txt";
}

void SearchPanel::LoadHistory() {
    historyStore_.Load(HistoryPath());
    history_ = historyStore_.Queries();
    for (const auto& query : history_) SendMessageW(query_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(query.c_str()));
}

void SearchPanel::SaveHistory() const {
    historyStore_.Save(HistoryPath());
}

void SearchPanel::RecordHistory(const std::wstring& query) {
    if (!retainHistory_ || query.empty()) return;
    historyStore_.Record(query, HistoryPath());
    history_ = historyStore_.Queries();
    updatingHistory_ = true;
    SendMessageW(query_, CB_RESETCONTENT, 0, 0);
    for (const auto& item : history_) SendMessageW(query_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
    SetWindowTextW(query_, query.c_str());
    updatingHistory_ = false;
}

void SearchPanel::ClearHistory() {
    history_.clear();
    SendMessageW(query_, CB_RESETCONTENT, 0, 0);
    historyStore_.Clear(HistoryPath());
    UpdateStatus(L"Search history cleared.");
}

} // namespace ffui
