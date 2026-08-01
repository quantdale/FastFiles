#include "NavigationWorkspace.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <shlobj.h>
#include <windows.h>

namespace ffui {
namespace {

constexpr size_t kMaxRecentlyClosedTabs = 16;

bool IsAbsolutePath(const std::wstring& path) {
    return (path.size() >= 3 && std::iswalpha(path[0]) && path[1] == L':' && path[2] == L'\\') ||
           (path.size() >= 3 && path[0] == L'\\' && path[1] == L'\\' && path[2] != L'\\');
}

std::wstring DefaultBookmarkName(const std::wstring& path) {
    const size_t end = path.find_last_not_of(L'\\');
    const size_t slash = path.find_last_of(L'\\', end);
    return slash == std::wstring::npos ? path : path.substr(slash + 1, end - slash);
}

std::wstring EscapeJson(const std::wstring& value) {
    std::wstring result;
    for (wchar_t ch : value) {
        if (ch == L'"' || ch == L'\\') result += L'\\';
        if (ch == L'\n') result += L"\\n";
        else if (ch == L'\r') result += L"\\r";
        else if (ch == L'\t') result += L"\\t";
        else result += ch;
    }
    return result;
}

std::wstring UnescapeJson(const std::wstring& value) {
    std::wstring result;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] != L'\\' || i + 1 == value.size()) {
            result += value[i];
            continue;
        }
        const wchar_t escaped = value[++i];
        result += escaped == L'n' ? L'\n' : escaped == L'r' ? L'\r' : escaped == L't' ? L'\t' : escaped;
    }
    return result;
}

bool ReadBoolean(const std::wstring& json, const wchar_t* name) {
    const std::wstring marker = std::wstring(L"\"") + name + L"\"";
    const size_t key = json.find(marker);
    if (key == std::wstring::npos) return false;
    const size_t colon = json.find(L':', key + marker.size());
    if (colon == std::wstring::npos) return false;
    const size_t value = json.find_first_not_of(L" \t\r\n", colon + 1);
    return value != std::wstring::npos && json.compare(value, 4, L"true") == 0;
}

bool ParseQuoted(const std::wstring& json, size_t quote, std::wstring& value, size_t& next) {
    if (quote >= json.size() || json[quote] != L'"') return false;
    size_t end = quote + 1;
    while (end < json.size()) {
        if (json[end] == L'"' && (end == quote + 1 || json[end - 1] != L'\\')) break;
        ++end;
    }
    if (end >= json.size()) return false;
    value = UnescapeJson(json.substr(quote + 1, end - quote - 1));
    next = end + 1;
    return true;
}

bool ExtractJsonString(const std::wstring& json, const wchar_t* name, size_t from,
                       std::wstring& value, size_t& next) {
    const std::wstring marker = std::wstring(L"\"") + name + L"\"";
    const size_t key = json.find(marker, from);
    if (key == std::wstring::npos) return false;
    const size_t colon = json.find(L':', key + marker.size());
    if (colon == std::wstring::npos) return false;
    const size_t quote = json.find(L'"', colon + 1);
    return quote != std::wstring::npos && ParseQuoted(json, quote, value, next);
}

} // namespace

NavigationContext NavigationWorkspace::MakeContext(const std::wstring& path) {
    NavigationContext result;
    result.currentPath = path;
    result.history.push_back(path);
    return result;
}

NavigationWorkspace::NavigationWorkspace(std::wstring defaultPath) : defaultPath_(std::move(defaultPath)) {
    tabs_.push_back({{MakeContext(defaultPath_)}, 0});
    LoadState(state_);
}

NavigationWorkspace::Tab& NavigationWorkspace::ActiveTab() { return tabs_[activeTab_]; }
const NavigationWorkspace::Tab& NavigationWorkspace::ActiveTab() const { return tabs_[activeTab_]; }
NavigationContext& NavigationWorkspace::ActiveContext() { return ActiveTab().panes[ActiveTab().activePane]; }
const NavigationContext& NavigationWorkspace::ActiveContext() const { return ActiveTab().panes[ActiveTab().activePane]; }

std::wstring NavigationWorkspace::TabPath(size_t index) const {
    if (index >= tabs_.size() || tabs_[index].panes.empty()) return {};
    const auto& tab = tabs_[index];
    const size_t pane = (std::min)(tab.activePane, tab.panes.size() - 1);
    return tab.panes[pane].currentPath;
}

bool NavigationWorkspace::IsDualPane() const { return ActiveTab().panes.size() == 2; }
bool NavigationWorkspace::CanGoBack() const { return ActiveContext().historyIndex > 0; }
bool NavigationWorkspace::CanGoForward() const {
    return ActiveContext().historyIndex + 1 < ActiveContext().history.size();
}

void NavigationWorkspace::Navigate(const std::wstring& path) {
    auto& context = ActiveContext();
    if (path == context.currentPath) return;
    context.history.resize(context.historyIndex + 1);
    context.history.push_back(path);
    context.historyIndex = context.history.size() - 1;
    context.currentPath = path;
    context.addressBarMode = AddressBarMode::Breadcrumb;
    context.addressBarError.clear();
}

bool NavigationWorkspace::GoBack() {
    if (!CanGoBack()) return false;
    auto& context = ActiveContext();
    context.currentPath = context.history[--context.historyIndex];
    return true;
}

bool NavigationWorkspace::GoForward() {
    if (!CanGoForward()) return false;
    auto& context = ActiveContext();
    context.currentPath = context.history[++context.historyIndex];
    return true;
}

std::vector<BreadcrumbSegment> NavigationWorkspace::Breadcrumbs() const {
    const std::wstring& path = ActiveContext().currentPath;
    std::vector<BreadcrumbSegment> result{{L"This PC", L""}};
    if (path.size() >= 3 && path[1] == L':' && path[2] == L'\\') {
        std::wstring accumulated = path.substr(0, 3);
        result.push_back({path.substr(0, 2), accumulated});
        size_t start = 3;
        while (start < path.size()) {
            const size_t end = path.find(L'\\', start);
            const std::wstring component = path.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
            if (!component.empty()) {
                if (accumulated.back() != L'\\') accumulated += L'\\';
                accumulated += component;
                result.push_back({component, accumulated});
            }
            if (end == std::wstring::npos) break;
            start = end + 1;
        }
        return result;
    }
    if (path.rfind(L"\\\\", 0) == 0) {
        std::wstring accumulated = L"\\\\";
        size_t start = 2;
        while (start < path.size()) {
            const size_t end = path.find(L'\\', start);
            const std::wstring component = path.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
            if (!component.empty()) {
                accumulated += component;
                result.push_back({component, accumulated});
                accumulated += L'\\';
            }
            if (end == std::wstring::npos) break;
            start = end + 1;
        }
    }
    return result;
}

void NavigationWorkspace::BeginAddressBarEdit() {
    auto& context = ActiveContext();
    context.addressBarMode = AddressBarMode::EditableText;
    context.addressBarText = context.currentPath;
    context.addressBarError.clear();
}

void NavigationWorkspace::CancelAddressBarEdit() {
    auto& context = ActiveContext();
    context.addressBarMode = AddressBarMode::Breadcrumb;
    context.addressBarText.clear();
    context.addressBarError.clear();
}

bool NavigationWorkspace::NavigateBreadcrumb(size_t segmentIndex) {
    const auto segments = Breadcrumbs();
    if (segmentIndex >= segments.size() || segments[segmentIndex].path.empty()) return false;
    Navigate(segments[segmentIndex].path);
    return true;
}

void NavigationWorkspace::OpenTab(const std::optional<std::wstring>& path) {
    tabs_.push_back({{MakeContext(path.value_or(ActiveContext().currentPath))}, 0});
    activeTab_ = tabs_.size() - 1;
}

bool NavigationWorkspace::CloseActiveTab() {
    if (tabs_.size() == 1) return false;
    state_.recentlyClosedTabs.push_front(ActiveContext().currentPath);
    if (state_.recentlyClosedTabs.size() > kMaxRecentlyClosedTabs) state_.recentlyClosedTabs.pop_back();
    tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(activeTab_));
    activeTab_ = std::min(activeTab_, tabs_.size() - 1);
    stateDirty_ = true;
    return true;
}

bool NavigationWorkspace::SwitchTab(size_t index) {
    if (index >= tabs_.size()) return false;
    activeTab_ = index;
    return true;
}

bool NavigationWorkspace::ReopenClosedTab() {
    if (state_.recentlyClosedTabs.empty()) return false;
    const auto path = state_.recentlyClosedTabs.front();
    state_.recentlyClosedTabs.pop_front();
    OpenTab(path);
    stateDirty_ = true;
    return true;
}

void NavigationWorkspace::EnableDualPane() {
    if (!IsDualPane()) ActiveTab().panes.push_back(MakeContext(ActiveContext().currentPath));
}

void NavigationWorkspace::DisableDualPane() {
    auto& tab = ActiveTab();
    if (!IsDualPane()) return;
    auto kept = tab.panes[tab.activePane];
    tab.panes = {std::move(kept)};
    tab.activePane = 0;
}

void NavigationWorkspace::ActivatePane(size_t index) {
    if (index < ActiveTab().panes.size()) ActiveTab().activePane = index;
}

void NavigationWorkspace::AddBookmark(const std::wstring& path, std::wstring name) {
    state_.bookmarks.push_back({path, name.empty() ? DefaultBookmarkName(path) : std::move(name)});
    stateDirty_ = true;
}

bool NavigationWorkspace::RenameBookmark(size_t index, const std::wstring& name) {
    if (index >= state_.bookmarks.size() || name.empty()) return false;
    state_.bookmarks[index].displayName = name;
    stateDirty_ = true;
    return true;
}

bool NavigationWorkspace::ReorderBookmark(size_t from, size_t to) {
    if (from >= state_.bookmarks.size() || to >= state_.bookmarks.size()) return false;
    auto bookmark = state_.bookmarks[from];
    state_.bookmarks.erase(state_.bookmarks.begin() + static_cast<std::ptrdiff_t>(from));
    state_.bookmarks.insert(state_.bookmarks.begin() + static_cast<std::ptrdiff_t>(to), std::move(bookmark));
    stateDirty_ = true;
    return true;
}

bool NavigationWorkspace::RemoveBookmark(size_t index) {
    if (index >= state_.bookmarks.size()) return false;
    state_.bookmarks.erase(state_.bookmarks.begin() + static_cast<std::ptrdiff_t>(index));
    stateDirty_ = true;
    return true;
}

std::vector<std::wstring> NavigationWorkspace::EnumerateDrives() {
    std::vector<std::wstring> result;
    const DWORD mask = GetLogicalDrives();
    for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
        if ((mask & (1u << (letter - L'A'))) != 0) result.push_back(std::wstring(1, letter) + L":\\");
    }
    return result;
}

std::vector<Bookmark> NavigationWorkspace::EnumerateKnownFolders() {
    struct KnownFolder { const wchar_t* name; const KNOWNFOLDERID* id; };
    const KnownFolder folders[] = {
        {L"Desktop", &FOLDERID_Desktop}, {L"Documents", &FOLDERID_Documents},
        {L"Downloads", &FOLDERID_Downloads}, {L"Pictures", &FOLDERID_Pictures},
        {L"Videos", &FOLDERID_Videos}, {L"Music", &FOLDERID_Music}};
    std::vector<Bookmark> result;
    for (const auto& folder : folders) {
        PWSTR path = nullptr;
        const HRESULT status = SHGetKnownFolderPath(*folder.id, KF_FLAG_DEFAULT, nullptr, &path);
        result.push_back({status == S_OK && path != nullptr ? path : L"", folder.name});
        if (path != nullptr) CoTaskMemFree(path);
    }
    return result;
}

std::wstring NavigationWorkspace::StateFilePath() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));
    return length == 0 || length >= buffer.size() ? L"" : std::wstring(buffer.data()) + L"\\FastFiles\\workspace-state.json";
}

bool NavigationWorkspace::LoadState(WorkspaceState& state) {
    state = {};
    const std::wstring file = StateFilePath();
    if (file.empty()) return false;
    std::wifstream input(file);
    if (!input) return true;
    std::wstringstream buffer;
    buffer << input.rdbuf();
    const std::wstring json = buffer.str();
    const size_t version = json.find(L"\"version\"");
    if (version == std::wstring::npos || json.find(L'1', version) == std::wstring::npos) return true;
    state.sidebarCollapsed = ReadBoolean(json, L"sidebarCollapsed");
    state.drivesCollapsed = ReadBoolean(json, L"drivesCollapsed");
    state.knownFoldersCollapsed = ReadBoolean(json, L"knownFoldersCollapsed");
    state.bookmarksCollapsed = ReadBoolean(json, L"bookmarksCollapsed");

    size_t cursor = 0;
    std::wstring path;
    while (ExtractJsonString(json, L"path", cursor, path, cursor)) {
        std::wstring displayName;
        size_t displayCursor = cursor;
        if (!ExtractJsonString(json, L"displayName", displayCursor, displayName, displayCursor)) break;
        state.bookmarks.push_back({std::move(path), std::move(displayName)});
        cursor = displayCursor;
    }

    const size_t closedKey = json.find(L"\"recentlyClosedTabs\"");
    const size_t open = closedKey == std::wstring::npos ? std::wstring::npos : json.find(L'[', closedKey);
    const size_t close = open == std::wstring::npos ? std::wstring::npos : json.find(L']', open + 1);
    cursor = open == std::wstring::npos ? 0 : open + 1;
    while (open != std::wstring::npos && close != std::wstring::npos && cursor < close) {
        const size_t quote = json.find(L'"', cursor);
        if (quote == std::wstring::npos || quote >= close) break;
        std::wstring closedPath;
        size_t next = quote;
        if (!ParseQuoted(json, quote, closedPath, next)) break;
        state.recentlyClosedTabs.push_back(std::move(closedPath));
        cursor = next;
    }
    return true;
}

bool NavigationWorkspace::SaveState(const WorkspaceState& state) {
    const std::wstring file = StateFilePath();
    if (file.empty()) return false;
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(file).parent_path(), error);
    if (error) return false;
    std::wofstream output(file, std::ios::trunc);
    if (!output) return false;
    output << L"{\n  \"version\": 1,\n  \"sidebarCollapsed\": "
            << (state.sidebarCollapsed ? L"true" : L"false") << L",\n  \"drivesCollapsed\": "
            << (state.drivesCollapsed ? L"true" : L"false") << L",\n  \"knownFoldersCollapsed\": "
            << (state.knownFoldersCollapsed ? L"true" : L"false") << L",\n  \"bookmarksCollapsed\": "
            << (state.bookmarksCollapsed ? L"true" : L"false") << L",\n  \"bookmarks\": [";
    for (size_t i = 0; i < state.bookmarks.size(); ++i) {
        const auto& bookmark = state.bookmarks[i];
        output << (i ? L"," : L"") << L"\n    {\"path\": \"" << EscapeJson(bookmark.path)
                << L"\", \"displayName\": \"" << EscapeJson(bookmark.displayName) << L"\"}";
    }
    output << L"\n  ],\n  \"recentlyClosedTabs\": [";
    for (size_t i = 0; i < state.recentlyClosedTabs.size(); ++i) {
        output << (i ? L", " : L"") << L"\"" << EscapeJson(state.recentlyClosedTabs[i]) << L"\"";
    }
    output << L"]\n}\n";
    return static_cast<bool>(output);
}

bool NavigationWorkspace::FlushState() {
    if (!stateDirty_) return true;
    const bool saved = SaveState(state_);
    if (saved) stateDirty_ = false;
    return saved;
}

ParsedPath ParseNavigationPath(const std::wstring& input) {
    const auto first = input.find_first_not_of(L" \t\r\n");
    const auto last = input.find_last_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {{}, L"Enter an absolute path."};
    std::wstring value = input.substr(first, last - first + 1);
    if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"') value = value.substr(1, value.size() - 2);
    std::replace(value.begin(), value.end(), L'/', L'\\');
    const DWORD needed = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
    if (needed != 0) {
        std::wstring expanded(needed, L'\0');
        ExpandEnvironmentStringsW(value.c_str(), expanded.data(), needed);
        expanded.pop_back();
        value = std::move(expanded);
    }
    if (!IsAbsolutePath(value)) return {{}, L"Paths must begin with a drive or UNC root."};
    if (value.find_first_of(L"<>|?*") != std::wstring::npos) return {{}, L"The path contains reserved characters."};
    std::wstring full(32768, L'\0');
    const DWORD size = GetFullPathNameW(value.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
    if (size == 0 || size >= full.size()) return {{}, L"The path is invalid."};
    full.resize(size);
    while (full.size() > 3 && full.back() == L'\\') full.pop_back();
    return {std::move(full), {}};
}

PathCommitResult CommitAddressBarPath(NavigationWorkspace& workspace, const std::wstring& input) {
    const auto parsed = ParseNavigationPath(input);
    auto& context = workspace.ActiveContext();
    if (!parsed.error.empty()) {
        context.addressBarError = parsed.error;
        context.addressBarMode = AddressBarMode::EditableText;
        return PathCommitResult::Invalid;
    }
    const DWORD attributes = GetFileAttributesW(parsed.path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        if (error == ERROR_ACCESS_DENIED || error == ERROR_PRIVILEGE_NOT_HELD) {
            // The destination may exist but be unreadable. Let the bound
            // column surface its permission-denied state rather than turning
            // an authorization failure into a misleading "not found" error.
            workspace.Navigate(parsed.path);
            return PathCommitResult::Navigated;
        }
        context.addressBarError = L"This location no longer exists.";
        context.addressBarMode = AddressBarMode::EditableText;
        return PathCommitResult::NotFound;
    }
    workspace.Navigate(parsed.path);
    return PathCommitResult::Navigated;
}

} // namespace ffui
