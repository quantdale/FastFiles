#include "NavigationWorkspace.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <shlobj.h>
#include <windows.h>

#include "RecentFolders.h"
#include "ffui/DisplayPath.h"

namespace ffui {
namespace {

constexpr size_t kMaxRecentlyClosedTabs = 16;

std::optional<std::wstring> Utf8ToWide(const std::string& value) {
    if (value.empty()) return std::wstring{};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) return std::nullopt;
    std::wstring result(static_cast<size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), length) <= 0) {
        return std::nullopt;
    }
    return result;
}

std::optional<std::string> WideToUtf8(const std::wstring& value) {
    if (value.empty()) return std::string{};
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return std::nullopt;
    std::string result(static_cast<size_t>(length), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), length, nullptr, nullptr) <= 0) {
        return std::nullopt;
    }
    return result;
}

bool IsAbsolutePath(const std::wstring& path) {
    if (path.starts_with(L"\\\\.\\")) {
        // Device namespaces are not filesystem navigation targets. In
        // particular, never turn an address-bar entry into a raw device path.
        return false;
    }
    if (path.starts_with(L"\\\\?\\")) {
        const bool extendedDrive = path.size() >= 7 && std::iswalpha(path[4]) &&
                                    path[5] == L':' && path[6] == L'\\';
        const bool extendedUnc = path.starts_with(L"\\\\?\\UNC\\");
        return extendedDrive || extendedUnc;
    }
    return (path.size() >= 3 && std::iswalpha(path[0]) && path[1] == L':' && path[2] == L'\\') ||
           (path.size() >= 3 && path[0] == L'\\' && path[1] == L'\\' && path[2] != L'\\');
}

std::wstring DefaultBookmarkName(const std::wstring& path) {
    return ffui::DisplayPathLeaf(path);
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

bool HasSupportedVersion(const std::wstring& json) {
    const std::wstring marker = L"\"version\"";
    const size_t key = json.find(marker);
    if (key == std::wstring::npos) return false;
    const size_t colon = json.find(L':', key + marker.size());
    if (colon == std::wstring::npos) return false;
    const size_t value = json.find_first_not_of(L" \t\r\n", colon + 1);
    if (value == std::wstring::npos || json[value] != L'1') return false;
    const size_t after = value + 1;
    return after == json.size() || std::iswspace(json[after]) ||
           json[after] == L',' || json[after] == L'}';
}

bool ParseQuoted(const std::wstring& json, size_t quote, std::wstring& value, size_t& next) {
    if (quote >= json.size() || json[quote] != L'"') return false;
    size_t end = quote + 1;
    while (end < json.size()) {
        if (json[end] == L'"') {
            size_t slashCount = 0;
            for (size_t cursor = end; cursor > quote && json[cursor - 1] == L'\\'; --cursor) {
                ++slashCount;
            }
            if ((slashCount % 2) == 0) break;
        }
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

NavigationContext NavigationWorkspace::MakeContext(const NavigationLocation& location) {
    NavigationContext result;
    result.currentPath = location.IsFileSystem() ? location.path : std::wstring{};
    result.isHomeLocation = location.IsHome();
    result.history.push_back(location);
    return result;
}

NavigationWorkspace::NavigationWorkspace(std::wstring defaultPath) : defaultPath_(std::move(defaultPath)) {
    tabs_.push_back({{MakeContext(NavigationLocation::FromPath(defaultPath_))}, 0});
    LoadState(state_);
}

NavigationWorkspace::Tab& NavigationWorkspace::ActiveTab() { return tabs_[activeTab_]; }
const NavigationWorkspace::Tab& NavigationWorkspace::ActiveTab() const { return tabs_[activeTab_]; }
NavigationContext& NavigationWorkspace::ActiveContext() { return ActiveTab().panes[ActiveTab().activePane]; }
const NavigationContext& NavigationWorkspace::ActiveContext() const { return ActiveTab().panes[ActiveTab().activePane]; }
NavigationContext& NavigationWorkspace::PaneContext(size_t paneIndex) { return ActiveTab().panes.at(paneIndex); }
const NavigationContext& NavigationWorkspace::PaneContext(size_t paneIndex) const { return ActiveTab().panes.at(paneIndex); }
size_t NavigationWorkspace::ActivePaneIndex() const { return ActiveTab().activePane; }
size_t NavigationWorkspace::ActivePaneCount() const { return ActiveTab().panes.size(); }

std::wstring NavigationWorkspace::TabPath(size_t index) const {
    if (index >= tabs_.size() || tabs_[index].panes.empty()) return {};
    const auto& tab = tabs_[index];
    const size_t pane = (std::min)(tab.activePane, tab.panes.size() - 1);
    return tab.panes[pane].currentPath;
}

NavigationLocation NavigationWorkspace::TabLocation(size_t index) const {
    if (index >= tabs_.size() || tabs_[index].panes.empty()) return NavigationLocation::FromPath({});
    const auto& tab = tabs_[index];
    const size_t pane = (std::min)(tab.activePane, tab.panes.size() - 1);
    // quick-access-home hardening: shared typed extraction -- a Home tab keeps
    // its Home kind, This PC stays FileSystem(empty), never Home.
    return LocationOf(tab.panes[pane]);
}

std::vector<std::wstring> NavigationWorkspace::RecentFoldersForHome(size_t maxCount) const {
    return RecentFolderHistory::RecentForHome(state_.recentFolders, maxCount);
}

void NavigationWorkspace::ClearRecentFolders() {
    // quick-access-home 2.3: clearing must persist, so mark the state dirty.
    RecentFolderHistory::Clear(state_.recentFolders);
    MarkStateDirty();
}

bool NavigationWorkspace::IsDualPane() const { return ActiveTab().panes.size() == 2; }
bool NavigationWorkspace::CanGoBack() const { return ActiveContext().historyIndex > 0; }
bool NavigationWorkspace::CanGoForward() const {
    return ActiveContext().historyIndex + 1 < ActiveContext().history.size();
}

void NavigationWorkspace::Navigate(const std::wstring& path) {
    Navigate(NavigationLocation::FromPath(path));
}

void NavigationWorkspace::Navigate(const NavigationLocation& location) {
    auto& context = ActiveContext();
    // quick-access-home 1.3: kind-aware same-location no-op. Two Home
    // locations are always "the same" (Home has no path); a filesystem
    // navigation is a no-op only when the active context is already that exact
    // filesystem path. Navigating to a filesystem path while Home is active, or
    // to Home while a filesystem path is active, must NOT be treated as a
    // no-op. Combined with the recording guard below, this also excludes
    // selection/scroll churn from the history.
    const bool sameLocation = location.IsHome()
        ? context.isHomeLocation
        : (!context.isHomeLocation && location.path == context.currentPath);
    if (sameLocation) return;
    // Truncate any forward history without default-constructing entries.
    if (context.history.size() > context.historyIndex + 1) {
        context.history.erase(context.history.begin() + static_cast<std::ptrdiff_t>(context.historyIndex + 1),
                              context.history.end());
    }
    context.history.push_back(location);
    context.historyIndex = context.history.size() - 1;
    context.currentPath = location.IsFileSystem() ? location.path : std::wstring{};
    context.isHomeLocation = location.IsHome();
    context.addressBarMode = AddressBarMode::Breadcrumb;
    context.addressBarError.clear();
    // quick-access-home 2.3: record only real filesystem folder visits. This
    // automatically excludes Home (kind != FileSystem) and "This PC" (empty
    // path); the no-op above already excludes non-changing navigations.
    if (location.IsFileSystem() && !location.path.empty()) {
        RecentFolderHistory::Record(state_.recentFolders, location.path);
        MarkStateDirty();
    }
}

bool NavigationWorkspace::GoBack() {
    if (!CanGoBack()) return false;
    auto& context = ActiveContext();
    const NavigationLocation& location = context.history[--context.historyIndex];
    context.currentPath = location.IsFileSystem() ? location.path : std::wstring{};
    context.isHomeLocation = location.IsHome();
    // A history move is a successful navigation: drop any stale address-bar
    // error so it cannot paint over the new location's breadcrumbs.
    context.addressBarMode = AddressBarMode::Breadcrumb;
    context.addressBarError.clear();
    return true;
}

bool NavigationWorkspace::GoForward() {
    if (!CanGoForward()) return false;
    auto& context = ActiveContext();
    const NavigationLocation& location = context.history[++context.historyIndex];
    context.currentPath = location.IsFileSystem() ? location.path : std::wstring{};
    context.isHomeLocation = location.IsHome();
    context.addressBarMode = AddressBarMode::Breadcrumb;
    context.addressBarError.clear();
    return true;
}

std::vector<BreadcrumbSegment> NavigationWorkspace::Breadcrumbs() const {
    // quick-access-home 1.4: a Home context exposes a single, non-clickable
    // "Home" segment (its path is empty, like the "This PC" first segment).
    if (ActiveContext().isHomeLocation) {
        return {{L"Home", L""}};
    }
    const std::wstring& path = ActiveContext().currentPath;
    std::vector<BreadcrumbSegment> result{{L"This PC", L"", true}};
    const auto applyDisplayLabels = [&](std::vector<BreadcrumbSegment> segments) {
        const std::wstring displayPath = ffui::NormalizeDisplayPath(path);
        if (displayPath.empty() || segments.size() <= 1) return segments;

        std::vector<std::wstring> labels{L"This PC"};
        if (displayPath.size() >= 3 && displayPath[1] == L':' && displayPath[2] == L'\\') {
            labels.push_back(displayPath.substr(0, 2));
            size_t start = 3;
            while (start < displayPath.size()) {
                const size_t end = displayPath.find(L'\\', start);
                const std::wstring component = displayPath.substr(
                    start, end == std::wstring::npos ? std::wstring::npos : end - start);
                if (!component.empty()) labels.push_back(component);
                if (end == std::wstring::npos) break;
                start = end + 1;
            }
        } else if (displayPath.rfind(L"\\\\", 0) == 0) {
            size_t start = 2;
            size_t componentsSeen = 0;
            while (start < displayPath.size()) {
                const size_t end = displayPath.find(L'\\', start);
                const std::wstring component = displayPath.substr(
                    start, end == std::wstring::npos ? std::wstring::npos : end - start);
                if (!component.empty()) {
                    ++componentsSeen;
                    if (componentsSeen >= 2) labels.push_back(component);
                }
                if (end == std::wstring::npos) break;
                start = end + 1;
            }
        }
        const size_t count = (std::min)(segments.size(), labels.size());
        for (size_t index = 1; index < count; ++index) segments[index].label = labels[index];
        return segments;
    };
    if (path.starts_with(L"\\\\?\\") && path.size() >= 7 &&
        std::iswalpha(path[4]) && path[5] == L':' && path[6] == L'\\') {
        std::wstring accumulated = path.substr(0, 7);
        result.push_back({path.substr(4, 2), accumulated});
        size_t start = 7;
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
        return applyDisplayLabels(std::move(result));
    }
    if (path.starts_with(L"\\\\?\\UNC\\")) {
        std::wstring accumulated = L"\\\\?\\UNC\\";
        size_t start = accumulated.size();
        // The server and share merge into one segment: bare `\\?\UNC\server`
        // is not an enumerable navigation target, only the share root is.
        size_t componentsSeen = 0;
        while (start < path.size()) {
            const size_t end = path.find(L'\\', start);
            const std::wstring component = path.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
            if (!component.empty()) {
                accumulated += component;
                ++componentsSeen;
                // First component is the server, second the share: emit them
                // together as a single share-root segment.
                if (componentsSeen >= 2) {
                    result.push_back({component, accumulated});
                }
                accumulated += L'\\';
            }
            if (end == std::wstring::npos) break;
            start = end + 1;
        }
        return applyDisplayLabels(std::move(result));
    }
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
        return applyDisplayLabels(std::move(result));
    }
    if (path.rfind(L"\\\\", 0) == 0) {
        std::wstring accumulated = L"\\\\";
        size_t start = 2;
        // The server and share merge into one segment: bare `\\server` is not
        // an enumerable navigation target, only the share root is.
        size_t componentsSeen = 0;
        while (start < path.size()) {
            const size_t end = path.find(L'\\', start);
            const std::wstring component = path.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
            if (!component.empty()) {
                accumulated += component;
                ++componentsSeen;
                // First component is the server, second the share: emit them
                // together as a single share-root segment.
                if (componentsSeen >= 2) {
                    result.push_back({component, accumulated});
                }
                accumulated += L'\\';
            }
            if (end == std::wstring::npos) break;
            start = end + 1;
        }
    }
    return applyDisplayLabels(std::move(result));
}

void NavigationWorkspace::BeginAddressBarEdit() {
    auto& context = ActiveContext();
    context.addressBarMode = AddressBarMode::EditableText;
    // The editable field is the path identity surface. Display-only prefix
    // stripping and 8.3 expansion belong to breadcrumbs/tabs, never here.
    context.addressBarText = context.currentPath.empty() ? L"This PC" : context.currentPath;
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
    if (segmentIndex >= segments.size()) return false;
    // The synthetic "This PC" root is a real target even though its path is
    // empty; everything else must carry a concrete path.
    if (!segments[segmentIndex].syntheticRoot && segments[segmentIndex].path.empty()) return false;
    Navigate(segments[segmentIndex].path);
    return true;
}

void NavigationWorkspace::OpenTab() {
    // quick-access-home 1.3 / D7: a new tab opens Home by default.
    OpenTab(NavigationLocation::Home());
}

void NavigationWorkspace::OpenTab(const NavigationLocation& location) {
    // quick-access-home hardening (HOME-POST-01/04): tabs are created from the
    // typed location with a fresh {location} history.
    tabs_.push_back({{MakeContext(location)}, 0});
    activeTab_ = tabs_.size() - 1;
}

void NavigationWorkspace::OpenTab(const std::wstring& path) {
    OpenTab(NavigationLocation::FromPath(path));
}

bool NavigationWorkspace::CloseActiveTab() {
    if (tabs_.size() == 1) return false;
    // quick-access-home hardening (HOME-POST-01): remember the TYPED location.
    // A closed Home tab (empty path) must reopen as Home, never as an empty
    // filesystem path ("This PC").
    state_.recentlyClosedTabs.push_front(LocationOf(ActiveContext()));
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
    // quick-access-home hardening (HOME-POST-01): reopen restores the typed
    // location (Home stays Home; FileSystem keeps its path; This PC stays
    // FileSystem(empty)).
    const auto location = state_.recentlyClosedTabs.front();
    state_.recentlyClosedTabs.pop_front();
    OpenTab(location);
    stateDirty_ = true;
    return true;
}

void NavigationWorkspace::EnableDualPane() {
    // quick-access-home hardening (HOME-POST-04): clone the ACTIVE TYPED
    // location instead of the bare path. From Home the new pane starts Home
    // with history {Home}; from a filesystem context it starts at the same
    // folder with a fresh {folder} history; This PC stays FileSystem(empty),
    // never Home.
    if (!IsDualPane()) ActiveTab().panes.push_back(MakeContext(LocationOf(ActiveContext())));
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

std::optional<std::wstring> NavigationWorkspace::OtherPanePath() const {
    const auto& tab = ActiveTab();
    if (tab.panes.size() != 2) return std::nullopt;
    // activePane is always < panes.size() (ActivatePane clamps), so the
    // inactive pane index is 0 or 1.
    return tab.panes[1 - tab.activePane].currentPath;
}

void NavigationWorkspace::AddBookmark(const std::wstring& path, std::wstring name) {
    if (path.empty()) return;
    const bool alreadyPresent = std::any_of(state_.bookmarks.begin(), state_.bookmarks.end(),
        [&path](const Bookmark& bookmark) {
            return CompareStringOrdinal(bookmark.path.data(), static_cast<int>(bookmark.path.size()),
                                        path.data(), static_cast<int>(path.size()), TRUE) == CSTR_EQUAL;
        });
    if (alreadyPresent) return;
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
    if (knownFoldersDirty_) {
        knownFoldersCache_.clear();
        struct KnownFolder { const wchar_t* name; const KNOWNFOLDERID* id; };
        const KnownFolder folders[] = {
            {L"Desktop", &FOLDERID_Desktop}, {L"Documents", &FOLDERID_Documents},
            {L"Downloads", &FOLDERID_Downloads}, {L"Pictures", &FOLDERID_Pictures},
            {L"Videos", &FOLDERID_Videos}, {L"Music", &FOLDERID_Music}};
        for (const auto& folder : folders) {
            PWSTR path = nullptr;
            const HRESULT status = SHGetKnownFolderPath(*folder.id, KF_FLAG_DEFAULT, nullptr, &path);
            knownFoldersCache_.push_back({status == S_OK && path != nullptr ? path : L"", folder.name});
            if (path != nullptr) CoTaskMemFree(path);
        }
        knownFoldersDirty_ = false;
    }
    return knownFoldersCache_;
}

void NavigationWorkspace::ReResolveKnownFolders() {
    knownFoldersDirty_ = true;
    EnumerateKnownFolders(); // refresh the cache immediately
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
    std::ifstream input(file, std::ios::binary);
    if (!input) return true;
    const std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (input.bad()) return true;
    const auto decoded = Utf8ToWide(bytes);
    if (!decoded) return true;
    const std::wstring& json = *decoded;
    if (!HasSupportedVersion(json)) return true;
    return DeserializeState(json, state);
}

bool DeserializeState(const std::wstring& json, WorkspaceState& out) {
    out = {};
    out.sidebarCollapsed = ReadBoolean(json, L"sidebarCollapsed");
    out.drivesCollapsed = ReadBoolean(json, L"drivesCollapsed");
    out.knownFoldersCollapsed = ReadBoolean(json, L"knownFoldersCollapsed");
    out.bookmarksCollapsed = ReadBoolean(json, L"bookmarksCollapsed");
    size_t lastLocationCursor = 0;
    ExtractJsonString(json, L"lastLocation", 0, out.lastLocation, lastLocationCursor);
    // quick-access-home: additive typed kind for the persisted last location
    // (older documents simply load as filesystem).
    out.lastLocationIsHome = ReadBoolean(json, L"lastLocationIsHome");

    size_t cursor = 0;
    std::wstring path;
    while (ExtractJsonString(json, L"path", cursor, path, cursor)) {
        std::wstring displayName;
        size_t displayCursor = cursor;
        if (!ExtractJsonString(json, L"displayName", displayCursor, displayName, displayCursor)) break;
        out.bookmarks.push_back({std::move(path), std::move(displayName)});
        cursor = displayCursor;
    }

    // quick-access-home hardening (HOME-POST-01): recently-closed tabs are
    // typed. Legacy documents store bare path strings; those load as
    // FileSystem locations ("This PC" when the string is empty). Home entries
    // are stored as {"kind": "home"} objects (and {"kind": "filesystem",
    // "path": ...} objects are accepted for forward robustness). Malformed
    // entries are skipped without failing the whole document.
    const size_t closedKey = json.find(L"\"recentlyClosedTabs\"");
    const size_t open = closedKey == std::wstring::npos ? std::wstring::npos : json.find(L'[', closedKey);
    const size_t close = open == std::wstring::npos ? std::wstring::npos : json.find(L']', open + 1);
    size_t closedCursor = open == std::wstring::npos ? 0 : open + 1;
    while (open != std::wstring::npos && close != std::wstring::npos && closedCursor < close &&
           out.recentlyClosedTabs.size() < kMaxRecentlyClosedTabs) {
        closedCursor = json.find_first_not_of(L" \t\r\n", closedCursor);
        if (closedCursor == std::wstring::npos || closedCursor >= close) break;
        if (json[closedCursor] == L'"') {
            std::wstring closedPath;
            size_t next = closedCursor;
            if (!ParseQuoted(json, closedCursor, closedPath, next)) break;
            out.recentlyClosedTabs.push_back(NavigationLocation::FromPath(std::move(closedPath)));
            closedCursor = next;
        } else if (json[closedCursor] == L'{') {
            const size_t braceEnd = json.find(L'}', closedCursor + 1);
            if (braceEnd == std::wstring::npos || braceEnd > close) break;
            const std::wstring object = json.substr(closedCursor, braceEnd - closedCursor + 1);
            std::wstring kind;
            size_t kindNext = 0;
            std::wstring closedPath;
            size_t pathNext = 0;
            if (ExtractJsonString(object, L"kind", 0, kind, kindNext)) {
                const bool hasPath = ExtractJsonString(object, L"path", kindNext, closedPath, pathNext);
                if (kind == L"home") {
                    out.recentlyClosedTabs.push_back(NavigationLocation::Home());
                } else {
                    out.recentlyClosedTabs.push_back(
                        NavigationLocation::FromPath(hasPath ? std::move(closedPath) : std::wstring{}));
                }
            }
            closedCursor = braceEnd + 1;
        } else {
            // Not a string or object entry (e.g. a number or null): skip it.
            const size_t commaOrClose = json.find_first_of(L",]", closedCursor);
            if (commaOrClose == std::wstring::npos) break;
            if (json[commaOrClose] == L']') break;
            closedCursor = commaOrClose + 1;
        }
    }

    // quick-access-home 2.4: recentFolders is additive and optional. Missing
    // field -> empty list; malformed/over-long individual entries are skipped
    // without failing the whole document.
    const size_t foldersKey = json.find(L"\"recentFolders\"");
    const size_t fOpen = foldersKey == std::wstring::npos ? std::wstring::npos : json.find(L'[', foldersKey);
    const size_t fClose = fOpen == std::wstring::npos ? std::wstring::npos : json.find(L']', fOpen + 1);
    size_t fCursor = fOpen == std::wstring::npos ? 0 : fOpen + 1;
    while (fOpen != std::wstring::npos && fClose != std::wstring::npos && fCursor < fClose &&
           out.recentFolders.size() < RecentFolderHistory::kMaxRecentFoldersPersisted) {
        fCursor = json.find_first_not_of(L" \t\r\n", fCursor);
        if (fCursor == std::wstring::npos || fCursor >= fClose) break;
        if (json[fCursor] != L'"') {
            // Not a string entry (e.g. a number, object, or null): skip it.
            const size_t commaOrClose = json.find_first_of(L",]", fCursor);
            if (commaOrClose == std::wstring::npos) break;
            if (json[commaOrClose] == L']') break;
            fCursor = commaOrClose + 1;
            continue;
        }
        std::wstring folderPath;
        size_t next = fCursor;
        if (!ParseQuoted(json, fCursor, folderPath, next)) break;
        out.recentFolders.push_back(std::move(folderPath));
        fCursor = next;
    }
    return true;
}

std::wstring SerializeState(const WorkspaceState& state) {
    std::wostringstream document;
    document << L"{\n  \"version\": 1,\n  \"lastLocation\": \"" << EscapeJson(state.lastLocation)
            << L"\",\n  \"lastLocationIsHome\": " << (state.lastLocationIsHome ? L"true" : L"false")
            << L",\n  \"sidebarCollapsed\": "
            << (state.sidebarCollapsed ? L"true" : L"false") << L",\n  \"drivesCollapsed\": "
            << (state.drivesCollapsed ? L"true" : L"false") << L",\n  \"knownFoldersCollapsed\": "
            << (state.knownFoldersCollapsed ? L"true" : L"false") << L",\n  \"bookmarksCollapsed\": "
            << (state.bookmarksCollapsed ? L"true" : L"false") << L",\n  \"bookmarks\": [";
    for (size_t i = 0; i < state.bookmarks.size(); ++i) {
        const auto& bookmark = state.bookmarks[i];
        document << (i ? L"," : L"") << L"\n    {\"path\": \"" << EscapeJson(bookmark.path)
                << L"\", \"displayName\": \"" << EscapeJson(bookmark.displayName) << L"\"}";
    }
    document << L"\n  ],\n  \"recentlyClosedTabs\": [";
    for (size_t i = 0; i < state.recentlyClosedTabs.size(); ++i) {
        const auto& entry = state.recentlyClosedTabs[i];
        document << (i ? L", " : L"");
        // quick-access-home hardening (HOME-POST-01): filesystem entries keep
        // the legacy bare-string form so older binaries still read them; only
        // Home entries use the typed {"kind": "home"} object form.
        if (entry.IsHome()) {
            document << L"{\"kind\": \"home\"}";
        } else {
            document << L"\"" << EscapeJson(entry.path) << L"\"";
        }
    }
    document << L"],\n  \"recentFolders\": [";
    for (size_t i = 0; i < state.recentFolders.size(); ++i) {
        document << (i ? L", " : L"") << L"\"" << EscapeJson(state.recentFolders[i]) << L"\"";
    }
    document << L"]\n}\n";
    return document.str();
}

bool NavigationWorkspace::SaveState(const WorkspaceState& state) {
    const std::wstring file = StateFilePath();
    if (file.empty()) return false;
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(file).parent_path(), error);
    if (error) return false;
    const std::wstring temporary = file + L".tmp";
    const auto document = SerializeState(state);
    const auto encoded = WideToUtf8(document);
    if (!encoded) return false;
    std::ofstream output(temporary, std::ios::trunc | std::ios::binary);
    if (!output) return false;
    output.write(encoded->data(), static_cast<std::streamsize>(encoded->size()));
    output.close();
    if (!output) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    if (!MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

bool NavigationWorkspace::FlushState() {
    const std::wstring currentPath = ActiveContext().currentPath;
    const bool isHome = ActiveContext().isHomeLocation;
    // quick-access-home: a session that ends on Home must restore as Home, so
    // the typed kind is persisted alongside the (empty) Home path.
    if (state_.lastLocation != currentPath || state_.lastLocationIsHome != isHome) {
        state_.lastLocation = currentPath;
        state_.lastLocationIsHome = isHome;
        stateDirty_ = true;
    }
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
    if (_wcsicmp(value.c_str(), L"This PC") == 0) return {{}, {}};
    std::replace(value.begin(), value.end(), L'/', L'\\');
    const DWORD needed = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
    if (needed != 0) {
        std::wstring expanded(needed, L'\0');
        ExpandEnvironmentStringsW(value.c_str(), expanded.data(), needed);
        expanded.pop_back();
        value = std::move(expanded);
    }
    const bool extendedPath = value.starts_with(L"\\\\?\\");
    if (value.starts_with(L"\\\\.\\")) return {{}, L"Device paths are not valid navigation targets."};
    if (!IsAbsolutePath(value)) return {{}, L"Paths must begin with a drive or UNC root."};
    const size_t reservedSearchStart = extendedPath ? 4 : 0;
    if (value.find_first_of(L"<>|?*", reservedSearchStart) != std::wstring::npos) {
        return {{}, L"The path contains reserved characters."};
    }
    std::wstring full(32768, L'\0');
    const DWORD size = GetFullPathNameW(value.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
    if (size == 0 || size >= full.size()) return {{}, L"The path is invalid."};
    full.resize(size);
    // Keep the trailing separator on an extended drive root (`\\?\\C:\\`),
    // where trimming it would turn a root into the invalid `\\?\\C:` form.
    if (!extendedPath) {
        while (full.size() > 3 && full.back() == L'\\') full.pop_back();
    }
    return {std::move(full), {}};
}

std::wstring ResolveInitialNavigationPath(bool restorePreviousSession,
                                          const std::wstring& lastLocation,
                                          const std::wstring& startupLocation) {
    if (restorePreviousSession && !lastLocation.empty()) {
        const auto parsedLastLocation = ParseNavigationPath(lastLocation);
        if (parsedLastLocation.error.empty() && !parsedLastLocation.path.empty()) {
            const DWORD attributes = GetFileAttributesW(parsedLastLocation.path.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES &&
                (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                return parsedLastLocation.path;
            }
            const DWORD error = GetLastError();
            if (attributes == INVALID_FILE_ATTRIBUTES &&
                (error == ERROR_ACCESS_DENIED || error == ERROR_PRIVILEGE_NOT_HELD)) {
                return parsedLastLocation.path;
            }
        }
    }

    if (!startupLocation.empty()) {
        const auto parsedStartup = ParseNavigationPath(startupLocation);
        if (parsedStartup.error.empty()) return parsedStartup.path;
    }
    return {};
}

NavigationLocation ResolveInitialNavigationLocation(bool restorePreviousSession,
                                                    const WorkspaceState& state,
                                                    const std::wstring& startupLocation) {
    // A session that ended on Home restores as Home; Home never reaches any
    // filesystem API, so no existence check applies.
    if (restorePreviousSession && state.lastLocationIsHome) {
        return NavigationLocation::Home();
    }
    return NavigationLocation::FromPath(
        ResolveInitialNavigationPath(restorePreviousSession, state.lastLocation, startupLocation));
}

PathCommitResult CommitAddressBarPath(NavigationWorkspace& workspace, const std::wstring& input) {
    const auto parsed = ParseNavigationPath(input);
    auto& context = workspace.ActiveContext();
    if (!parsed.error.empty()) {
        context.addressBarError = parsed.error;
        context.addressBarMode = AddressBarMode::EditableText;
        return PathCommitResult::Invalid;
    }
    if (parsed.path.empty()) {
        workspace.Navigate(std::wstring{});
        return PathCommitResult::Navigated;
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

std::optional<std::wstring> ParentNavigationPath(const std::wstring& path) {
    if (path.empty()) {
        return std::nullopt;
    }

    std::wstring normalized = path;
    const bool extendedDrive = normalized.size() >= 7 && normalized.starts_with(L"\\\\?\\") &&
                               std::iswalpha(normalized[4]) && normalized[5] == L':' && normalized[6] == L'\\';
    if (extendedDrive) {
        while (normalized.size() > 7 && normalized.back() == L'\\') normalized.pop_back();
        if (normalized.size() == 7) return std::wstring{};
        const size_t slash = normalized.find_last_of(L'\\');
        if (slash < 6) return std::wstring{};
        return normalized.substr(0, slash == 6 ? 7 : slash);
    }

    while (normalized.size() > 3 && (normalized.back() == L'\\' || normalized.back() == L'/')) {
        normalized.pop_back();
    }

    if (normalized.starts_with(L"\\\\?\\UNC\\")) {
        const size_t serverEnd = normalized.find(L'\\', 8);
        if (serverEnd == std::wstring::npos) return std::wstring{};
        const size_t shareEnd = normalized.find(L'\\', serverEnd + 1);
        if (shareEnd == std::wstring::npos || normalized.size() == shareEnd) return std::wstring{};
        const size_t slash = normalized.find_last_of(L'\\');
        if (slash < shareEnd) return std::wstring{};
        return normalized.substr(0, slash);
    }

    // A drive root's parent is the synthetic This-PC root, not itself.
    if (normalized.size() == 3 && std::iswalpha(normalized[0]) && normalized[1] == L':' && normalized[2] == L'\\') {
        return std::wstring{};
    }

    // Treat a UNC server and share root as a single navigable root. The
    // standard filesystem parent operation is not consistent about whether
    // `\\server\share` is its own parent across Windows library versions.
    if (normalized.rfind(L"\\\\", 0) == 0) {
        const size_t serverEnd = normalized.find(L'\\', 2);
        if (serverEnd == std::wstring::npos) {
            return std::wstring{};
        }
        const size_t shareEnd = normalized.find(L'\\', serverEnd + 1);
        if (shareEnd == std::wstring::npos) {
            return std::wstring{};
        }
        if (normalized.size() == shareEnd) {
            return std::wstring{};
        }
    }

    const std::wstring parent = std::filesystem::path(normalized).parent_path().wstring();
    if (parent.empty() || parent == normalized) {
        return std::nullopt;
    }
    return parent;
}

} // namespace ffui
