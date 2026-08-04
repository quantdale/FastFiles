#include "SearchPanel.h"
#include "IconCache.h"
#include "UITheme.h"
#include "Util.h"

#include <algorithm>
#include <commctrl.h>
#include <commoncontrols.h>
#include <cwctype>
#include <d2d1.h>
#include <dwrite.h>
#include <fstream>
#include <shellapi.h>
#include <shlobj.h>
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

// Scale a DIP metric to physical pixels for Win32 control layout.
int Scaled(int dipValue) {
    return static_cast<int>(ffui::UiScale(static_cast<float>(dipValue)));
}

using Microsoft::WRL::ComPtr;

// Returns the extension of a file name including the leading dot (L".txt"),
// or an empty string when there is no extension. Only used to key the system
// icon cache; directories use ffui::FolderKey() instead.
std::wstring FileExtension(const std::wstring& name) {
    const size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos || dot == 0 || dot + 1 >= name.size()) return {};
    return name.substr(dot);
}

// Represents a seen match span within a candidate name (case-insensitive),
// already expanded to never split a surrogate pair.
struct MatchSpan {
    size_t start = std::wstring::npos;
    size_t count = 0;
};

// Surrogate-pair tests (the SDK does not expose IsHighSurrogate/IsLowSurrogate
// as macros in this build, so they are provided here).
bool IsHighSurrogateChar(wchar_t ch) { return ch >= 0xD800 && ch <= 0xDBFF; }
bool IsLowSurrogateChar(wchar_t ch) { return ch >= 0xDC00 && ch <= 0xDFFF; }

// Finds the first case-insensitive occurrence of term in name and expands the
// boundaries so a surrogate pair is never split: if start lands on a low
// surrogate, back it up to include its leading high surrogate; if end lands on
// a high surrogate, advance it to include the trailing low surrogate.
MatchSpan FindMatchSpan(const std::wstring& name, const std::wstring& term) {
    MatchSpan span;
    if (term.empty() || term.size() > name.size()) return span;
    for (size_t i = 0; i + term.size() <= name.size(); ++i) {
        bool matches = true;
        for (size_t j = 0; matches && j < term.size(); ++j) {
            if (towlower(name[i + j]) != towlower(term[j])) matches = false;
        }
        if (!matches) continue;
        size_t start = i;
        size_t end = start + term.size();
        if (start > 0 && IsLowSurrogateChar(name[start])) --start;
        if (end < name.size() && IsHighSurrogateChar(name[end])) ++end;
        span.start = start;
        span.count = end - start;
        return span;
    }
    return span;
}

// Draws a single untruncated line of text at (x, row.top..row.bottom) with the
// given color, returning the consumed width so callers can append subsequent
// match-span segments immediately after the previous one.
int DrawRowText(HDC dc, HFONT font, int x, const RECT& row, const std::wstring& text, COLORREF color) {
    if (text.empty()) return 0;
    SetTextColor(dc, color);
    HGDIOBJ previous = SelectObject(dc, font);
    SIZE size{};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
    RECT textRect{x, row.top, row.right, row.bottom};
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &textRect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(dc, previous);
    return size.cx;
}

LRESULT CALLBACK SearchEditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                UINT_PTR subclassId, DWORD_PTR refData) {
    auto* self = reinterpret_cast<SearchPanel*>(refData);
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT) {
        if (self != nullptr) self->PaintSearchEdit(hwnd);
        return 0;
    }
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS) {
        const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
        if (self != nullptr) self->searchEditFocused_ = (message == WM_SETFOCUS);
        InvalidateRect(hwnd, nullptr, FALSE);
        return result;
    }
    if (message == WM_SETTEXT || message == WM_CLEAR || message == WM_CUT || message == WM_PASTE) {
        const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
        InvalidateRect(hwnd, nullptr, FALSE);
        return result;
    }
    UNREFERENCED_PARAMETER(subclassId);
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

// Small subclassed STATIC that paints its text with the themed status color
// (searching = accent, no-results = textSecondary, otherwise primary text) so
// the status line is visually distinct regardless of the shell theme.
LRESULT CALLBACK StatusProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                            UINT_PTR subclassId, DWORD_PTR refData) {
    auto* self = reinterpret_cast<SearchPanel*>(refData);
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT) {
        if (self != nullptr) self->PaintStatus(hwnd);
        return 0;
    }
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(lParam);
    UNREFERENCED_PARAMETER(subclassId);
    return DefSubclassProc(hwnd, message, wParam, lParam);
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
}  // namespace

SearchPanel::~SearchPanel() {
    if (queryEdit_) RemoveWindowSubclass(queryEdit_, SearchEditProc, 1);
    if (status_) RemoveWindowSubclass(status_, StatusProc, 2);
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
    sort_ = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
                            0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSortId)), nullptr, nullptr);
    list_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                            WS_CHILD | LVS_REPORT | LVS_OWNERDATA | LVS_OWNERDRAWFIXED | LVS_SINGLESEL | WS_TABSTOP,
                            0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kListId)), nullptr, nullptr);
    status_ = CreateWindowExW(0, L"STATIC", L"Ready", WS_CHILD, 0, 0, 0, 0, owner, nullptr, nullptr, nullptr);
    clearHistory_ = CreateWindowExW(0, L"BUTTON", L"Clear history", WS_CHILD,
                                    0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kClearHistoryId)), nullptr, nullptr);
    sortDirection_ = CreateWindowExW(0, L"BUTTON", L"Ascending", WS_CHILD | BS_OWNERDRAW,
                                     0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSortDirectionId)), nullptr, nullptr);
    if (!query_ || !scope_ || !sort_ || !list_ || !status_ || !clearHistory_ || !sortDirection_) return false;
    COMBOBOXINFO comboInfo{sizeof(comboInfo)};
    if (GetComboBoxInfo(query_, &comboInfo) && comboInfo.hwndItem) {
        queryEdit_ = comboInfo.hwndItem;
        SetWindowSubclass(queryEdit_, SearchEditProc, 1, reinterpret_cast<DWORD_PTR>(this));
    }
    SetWindowSubclass(status_, StatusProc, 2, reinterpret_cast<DWORD_PTR>(this));
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
    // The owner-drawn result rows draw a unified icon + name + location line, so
    // the column headers are meaningless and would render as a system-light strip
    // in dark mode. Hide the header (column widths still drive scroll extent).
    if (HWND header = ListView_GetHeader(list_)) ShowWindow(header, SW_HIDE);
    SetDarkTheme(ffui::gUiDarkTheme);
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

void SearchPanel::SetDarkTheme(bool dark) {
    if (query_) InvalidateRect(query_, nullptr, FALSE);
    if (list_) {
        const ffui::UiTheme theme = ffui::GetUiTheme(dark);
        ListView_SetBkColor(list_, ffui::ToColorRef(theme.background));
        ListView_SetTextColor(list_, ffui::ToColorRef(theme.text));
        ListView_SetTextBkColor(list_, ffui::ToColorRef(theme.background));
        InvalidateRect(list_, nullptr, FALSE);
    }
    if (scope_) InvalidateRect(scope_, nullptr, TRUE);
    if (sort_) InvalidateRect(sort_, nullptr, TRUE);
    if (sortDirection_) InvalidateRect(sortDirection_, nullptr, TRUE);
    if (status_) InvalidateRect(status_, nullptr, TRUE);
    // Force the query-edit paint resources to rebuild on the next paint so a
    // theme change cannot leave stale colors cached in the DC render target.
    searchResourcesReady_ = false;
    lastSearchDpi_ = -1.0f;
}

void SearchPanel::Reposition() {
    if (!visible_) return;
    RECT client{}; GetClientRect(owner_, &client);
    const int width = static_cast<int>(client.right);
    const int height = static_cast<int>(client.bottom);
    const int top = Scaled(static_cast<int>(ffui::UiMetrics::kChromeHeight)); // below navigation chrome
    SetWindowPos(query_, HWND_TOP, Scaled(12), top, (std::max)(Scaled(180), width - Scaled(700)), Scaled(300), SWP_SHOWWINDOW);
    SetWindowPos(scope_, HWND_TOP, (std::max)(Scaled(194), width - Scaled(508)), top, Scaled(210), Scaled(300), SWP_SHOWWINDOW);
    SetWindowPos(sort_, HWND_TOP, (std::max)(Scaled(406), width - Scaled(296)), top, Scaled(126), Scaled(300), SWP_SHOWWINDOW);
    SetWindowPos(sortDirection_, HWND_TOP, width - Scaled(158), top, Scaled(146), Scaled(26), SWP_SHOWWINDOW);
    SetWindowPos(status_, HWND_TOP, Scaled(12), top + Scaled(32), width - Scaled(150), Scaled(24), SWP_SHOWWINDOW);
    SetWindowPos(clearHistory_, HWND_TOP, width - Scaled(126), top + Scaled(28), Scaled(114), Scaled(26), SWP_SHOWWINDOW);
    SetWindowPos(list_, HWND_TOP, Scaled(12), top + Scaled(60), width - Scaled(24), (std::max)(Scaled(80), height - top - Scaled(72)), SWP_SHOWWINDOW);
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
        currentPrimaryTerm_.clear();
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
    currentPrimaryTerm_ = work.request.query.primaryTerm;
    UpdateStatus(L"Searching…", StatusKind::Searching);
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
    UpdateStatus(results_.empty() ? L"No results" : std::to_wstring(results_.size()) + L" results",
                 results_.empty() ? StatusKind::NoResults : StatusKind::Info);
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
            case 3: text = candidate.isDirectory ? L"—" : ffui::FormatSize(candidate.sizeBytes); break;
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
    if (item == nullptr) return false;
    if (item->hwndItem == list_) {
        DrawSearchResultRow(item);
        return true;
    }
    if (item->hwndItem == scope_ || item->hwndItem == sort_) {
        if (item->itemID == static_cast<UINT>(-1)) return false;
        DrawComboItem(item);
        return true;
    }
    if (item->hwndItem == sortDirection_) {
        DrawDirectionButton(item);
        return true;
    }
    return false;
}

void SearchPanel::DrawSearchResultRow(const DRAWITEMSTRUCT* item) {
    HDC dc = item->hDC;
    const RECT row = item->rcItem;
    const bool selected = (item->itemState & ODS_SELECTED) != 0;
    const bool highContrast = ffui::UiSystemHighContrast();
    const ffui::UiTheme theme = ffui::GetUiTheme(ffui::gUiDarkTheme);

    COLORREF background = selected ? ffui::ToColorRef(theme.accent) : ffui::ToColorRef(theme.background);
    COLORREF nameColor = selected ? ffui::ToColorRef(theme.textOnAccent) : ffui::ToColorRef(theme.text);
    COLORREF matchColor = selected ? ffui::ToColorRef(theme.textSecondary) : ffui::ToColorRef(theme.accent);
    COLORREF locationColor = ffui::ToColorRef(theme.textSecondary);
    if (highContrast) {
        background = GetSysColor(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW);
        nameColor = GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT);
        matchColor = nameColor;
        locationColor = nameColor;
    }

    HBRUSH backgroundBrush = CreateSolidBrush(background);
    FillRect(dc, &row, backgroundBrush);
    DeleteObject(backgroundBrush);

    if (static_cast<size_t>(item->itemID) >= results_.size()) {
        if ((item->itemState & ODS_FOCUS) != 0) DrawFocusRect(dc, &row);
        return;
    }
    const auto& candidate = results_[static_cast<size_t>(item->itemID)].candidate;

    HFONT font = reinterpret_cast<HFONT>(SendMessageW(list_, WM_GETFONT, 0, 0));
    if (font == nullptr) font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    // Icon (system SHIL_SMALL image list), vertically centered in the row.
    IImageList* imageList = nullptr;
    if (SUCCEEDED(SHGetImageList(SHIL_SMALL, IID_PPV_ARGS(&imageList)))) {
        int iconWidth = 0;
        int iconHeight = 0;
        if (ImageList_GetIconSize(IImageListToHIMAGELIST(imageList), &iconWidth, &iconHeight) && iconWidth > 0 && iconHeight > 0) {
            const int iconLeft = row.left + Scaled(static_cast<int>(ffui::UiMetrics::kSpaceS));
            const int iconTop = row.top + (static_cast<int>(row.bottom - row.top) - iconHeight) / 2;
            const int iconIndex = SystemIconIndex(candidate.isDirectory ? ffui::FolderKey()
                                                                        : ffui::IconKeyForExtension(FileExtension(candidate.name)));
            if (iconIndex >= 0) {
                ImageList_Draw(IImageListToHIMAGELIST(imageList), iconIndex, dc, iconLeft, iconTop, ILD_TRANSPARENT);
            }
        }
    }

    const int textLeft = row.left + Scaled(static_cast<int>(ffui::UiMetrics::kSpaceS)) +
                         Scaled(static_cast<int>(ffui::UiMetrics::kIconSize)) + Scaled(static_cast<int>(ffui::UiMetrics::kSpaceXs));

    // Reserve the right side for the location path (folder), capped at 50% of
    // the row so a long name still has room.
    const int rowWidth = static_cast<int>(row.right - row.left);
    int locationWidth = 0;
    if (!candidate.folder.empty()) {
        SetTextColor(dc, locationColor);
        HGDIOBJ previous = SelectObject(dc, font);
        SIZE locationSize{};
        GetTextExtentPoint32W(dc, candidate.folder.c_str(), static_cast<int>(candidate.folder.size()), &locationSize);
        SelectObject(dc, previous);
        locationWidth = (std::min)(static_cast<int>(locationSize.cx) + Scaled(static_cast<int>(ffui::UiMetrics::kSpaceM)),
                                   rowWidth / 2);
    }
    const int nameRight = row.right - locationWidth;

    // Draw the name with the matched span highlighted; the span is expanded to
    // never split a surrogate pair.
    const MatchSpan span = FindMatchSpan(candidate.name, currentPrimaryTerm_);
    int x = textLeft;
    if (span.count == 0 || span.start >= candidate.name.size()) {
        x += DrawRowText(dc, font, x, row, candidate.name, nameColor);
    } else {
        x += DrawRowText(dc, font, x, row, candidate.name.substr(0, span.start), nameColor);
        x += DrawRowText(dc, font, x, row, candidate.name.substr(span.start, span.count), matchColor);
        DrawRowText(dc, font, x, row, candidate.name.substr(span.start + span.count), nameColor);
    }

    // Location path, right-aligned and ellipsized.
    if (!candidate.folder.empty() && locationWidth > 0) {
        SetTextColor(dc, locationColor);
        HGDIOBJ previous = SelectObject(dc, font);
        RECT locationRect{nameRight, row.top, row.right, row.bottom};
        DrawTextW(dc, candidate.folder.c_str(), static_cast<int>(candidate.folder.size()), &locationRect,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_RIGHT | DT_END_ELLIPSIS);
        SelectObject(dc, previous);
    }

    if ((item->itemState & ODS_FOCUS) != 0) DrawFocusRect(dc, &row);
}

int SearchPanel::SystemIconIndex(const std::wstring& key) {
    const auto cached = iconIndexCache_.find(key);
    if (cached != iconIndexCache_.end()) return cached->second;

    int index = -1;
    if (key == ffui::FolderKey()) {
        SHSTOCKICONINFO stock{};
        stock.cbSize = static_cast<DWORD>(sizeof(stock));
        // SHGSI_SYSICONINDEX fills iSysImageIndex with the system image-list
        // index (this SDK ships no SHGSI_ICONINDEX flag).
        if (SUCCEEDED(SHGetStockIconInfo(SIID_FOLDER, SHGSI_SYSICONINDEX, &stock))) index = stock.iSysImageIndex;
    } else {
        std::wstring pseudoPath = L"file";
        if (!key.empty()) {
            if (key[0] == L'.') {
                pseudoPath += key;
            } else {
                pseudoPath += L".";
                pseudoPath += key;
            }
        }
        SHFILEINFOW info{};
        if (SHGetFileInfoW(pseudoPath.c_str(), FILE_ATTRIBUTE_NORMAL, &info, static_cast<UINT>(sizeof(info)),
                           SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES) != 0) {
            index = info.iIcon;
        }
    }
    iconIndexCache_[key] = index;
    return index;
}

void SearchPanel::DrawComboItem(const DRAWITEMSTRUCT* item) {
    HDC dc = item->hDC;
    const RECT rect = item->rcItem;
    const bool highContrast = ffui::UiSystemHighContrast();
    const ffui::UiTheme theme = ffui::GetUiTheme(ffui::gUiDarkTheme);
    const bool disabled = (item->hwndItem == scope_) && !engineActive_ && item->itemID >= 2;
    const bool selected = (item->itemState & ODS_SELECTED) != 0 && !disabled;

    COLORREF background = selected ? ffui::ToColorRef(theme.accent) : ffui::ToColorRef(theme.background);
    COLORREF text = selected ? ffui::ToColorRef(theme.textOnAccent) : ffui::ToColorRef(theme.text);
    if (disabled) text = ffui::ToColorRef(theme.textSecondary);
    if (highContrast) {
        background = GetSysColor(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW);
        text = GetSysColor(disabled ? COLOR_GRAYTEXT : selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT);
    }

    HBRUSH backgroundBrush = CreateSolidBrush(background);
    FillRect(dc, &rect, backgroundBrush);
    DeleteObject(backgroundBrush);

    wchar_t textBuffer[128]{};
    SendMessageW(item->hwndItem, CB_GETLBTEXT, item->itemID, reinterpret_cast<LPARAM>(textBuffer));
    SetTextColor(dc, text);
    SetBkMode(dc, TRANSPARENT);
    RECT textRect = rect;
    textRect.left += Scaled(static_cast<int>(ffui::UiMetrics::kSpaceXs));
    DrawTextW(dc, textBuffer, -1, &textRect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    if ((item->itemState & ODS_FOCUS) != 0) DrawFocusRect(dc, &rect);
}

void SearchPanel::DrawDirectionButton(const DRAWITEMSTRUCT* item) {
    HDC dc = item->hDC;
    RECT rect = item->rcItem;
    const bool highContrast = ffui::UiSystemHighContrast();
    const ffui::UiTheme theme = ffui::GetUiTheme(ffui::gUiDarkTheme);
    const bool pressed = (item->itemState & ODS_SELECTED) != 0;
    const bool hasFocus = (item->itemState & ODS_FOCUS) != 0;

    COLORREF background = pressed ? ffui::ToColorRef(theme.surfaceSubtle) : ffui::ToColorRef(theme.surface);
    COLORREF text = ffui::ToColorRef(theme.text);
    if (highContrast) {
        background = GetSysColor(pressed ? COLOR_BTNSHADOW : COLOR_BTNFACE);
        text = GetSysColor(COLOR_BTNTEXT);
    }

    HBRUSH backgroundBrush = CreateSolidBrush(background);
    FillRect(dc, &rect, backgroundBrush);
    DeleteObject(backgroundBrush);

    HBRUSH borderBrush = CreateSolidBrush(ffui::ToColorRef(theme.border));
    FrameRect(dc, &rect, borderBrush);
    DeleteObject(borderBrush);

    wchar_t textBuffer[128]{};
    GetWindowTextW(sortDirection_, textBuffer, static_cast<int>(std::size(textBuffer)));
    SetTextColor(dc, text);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, textBuffer, -1, &rect, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    if (hasFocus) DrawFocusRect(dc, &rect);
}

void SearchPanel::PaintSearchEdit(HWND hwnd) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    if (!dc) return;

    const float dpi = ffui::UiDpiScale();
    if (!searchResourcesReady_ || dpi != lastSearchDpi_) {
        searchFactory_.Reset();
        searchTarget_.Reset();
        searchWriteFactory_.Reset();
        searchFormat_.Reset();
        lastSearchDpi_ = dpi;
        searchResourcesReady_ = false;
        if (SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&searchFactory_)))) {
            D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
                0.0f, 0.0f, D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);
            if (SUCCEEDED(searchFactory_->CreateDCRenderTarget(&properties, &searchTarget_)) &&
                SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                              reinterpret_cast<IUnknown**>(searchWriteFactory_.GetAddressOf()))) &&
                SUCCEEDED(searchWriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                                                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                                14.0f, L"en-us", &searchFormat_))) {
                searchResourcesReady_ = true;
            }
        }
    }

    if (!searchResourcesReady_) {
        EndPaint(hwnd, &paint);
        return;
    }

    RECT client{};
    GetClientRect(hwnd, &client);
    if (FAILED(searchTarget_->BindDC(dc, &client))) {
        searchResourcesReady_ = false;
        EndPaint(hwnd, &paint);
        return;
    }

    const ffui::UiTheme theme = ffui::GetUiTheme(ffui::gUiDarkTheme);
    searchTarget_->BeginDraw();
    searchTarget_->Clear(theme.background);
    ComPtr<ID2D1SolidColorBrush> border;
    ComPtr<ID2D1SolidColorBrush> textBrush;
    searchTarget_->CreateSolidColorBrush(searchEditFocused_ ? theme.accent : theme.searchBorder, &border);
    searchTarget_->CreateSolidColorBrush(theme.searchText, &textBrush);
    const auto bounds = D2D1::RectF(0.5f, 0.5f,
                                    static_cast<float>(client.right) - 0.5f,
                                    static_cast<float>(client.bottom) - 0.5f);
    if (border) searchTarget_->DrawRoundedRectangle(D2D1::RoundedRect(bounds, 4.0f, 4.0f), border.Get(), 1.0f);

    // Fluent focus accent: a 2-DIP accent underline along the bottom edge (and
    // the border color flips to accent above, so the focused edit reads as a
    // contained field with an accent baseline).
    if (searchEditFocused_ && border) {
        const auto accentLine = D2D1::RectF(0.5f, static_cast<float>(client.bottom) - 2.0f,
                                            static_cast<float>(client.right) - 0.5f,
                                            static_cast<float>(client.bottom) - 0.5f);
        searchTarget_->FillRectangle(accentLine, border.Get());
    }

    int length = GetWindowTextLengthW(hwnd);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(hwnd, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    if (text.empty()) {
        text = L"Search indexed locations…";
        textBrush.Reset();
        searchTarget_->CreateSolidColorBrush(theme.searchPlaceholder, &textBrush);
    }
    const auto textRect = D2D1::RectF(10.0f, 3.0f,
                                      static_cast<float>(client.right) - 10.0f,
                                      static_cast<float>(client.bottom) - 3.0f);
    if (textBrush) {
        searchTarget_->DrawText(text.c_str(), static_cast<UINT32>(text.size()), searchFormat_.Get(), textRect,
                                textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    const HRESULT drawResult = searchTarget_->EndDraw();
    if (FAILED(drawResult) && drawResult == D2DERR_RECREATE_TARGET) {
        searchResourcesReady_ = false;
    }
    EndPaint(hwnd, &paint);
}

void SearchPanel::PaintStatus(HWND hwnd) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    if (!dc) return;

    const ffui::UiTheme theme = ffui::GetUiTheme(ffui::gUiDarkTheme);
    COLORREF color = ffui::ToColorRef(theme.text);
    if (statusKind_ == StatusKind::Searching) color = ffui::ToColorRef(theme.accent);
    else if (statusKind_ == StatusKind::NoResults) color = ffui::ToColorRef(theme.textSecondary);

    RECT client{};
    GetClientRect(hwnd, &client);
    HBRUSH backgroundBrush = CreateSolidBrush(ffui::ToColorRef(theme.background));
    FillRect(dc, &client, backgroundBrush);
    DeleteObject(backgroundBrush);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    int length = GetWindowTextLengthW(hwnd);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(hwnd, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &client,
              DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    EndPaint(hwnd, &paint);
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

void SearchPanel::UpdateStatus(const std::wstring& text, StatusKind kind) {
    statusKind_ = kind;
    SetWindowTextW(status_, text.c_str());
    if (status_) InvalidateRect(status_, nullptr, TRUE);
}

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