#include "WindowShell.h"
#include "ShutdownDiagnostics.h"
#include "ffui/AsyncWorkAdmission.h"
#include "IconCache.h"
#include "ThemeToggle.h"
#include "UITheme.h"
#include "UiComponents.h" // ui-v3: shared state-panel presentation for details

#include "ConflictDialog.h"
#include "Util.h"
#include "ffprotocol/Diagnostics.h"
#include "ffui/DropFeedbackLayout.h" // ui-v3 drag/drop: pure feedback-geometry seam
#include "ffui/DisplayPath.h"
#include "ffui/StorageLayout.h"
#include "ffui/ColumnVisibility.h"
#include "UiaHostQuery.h"

#include <commctrl.h>
#include <commdlg.h>
#include <io.h>
#include <utility>
#include "QuickActions.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <limits>
#include <memory>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>
#include <winreg.h>

#ifdef _DEBUG
#include "ffui/SelectionDiagnostics.h"
#endif

namespace ffui {

namespace {
constexpr wchar_t kWindowClassName[] = L"FastFilesMainWindow";
constexpr UINT WM_APP_REPAINT = WM_APP + 1;
constexpr UINT WM_APP_SNAPSHOT_UPDATED = WM_APP + 11;
constexpr UINT kMenuCopy = 1001;
constexpr UINT kMenuCut = 1002;
constexpr UINT kMenuPaste = 1003;
constexpr UINT kMenuDelete = 1004;
constexpr UINT kMenuPermanentDelete = 1005;
constexpr UINT kMenuNewFolder = 1006;
constexpr UINT kMenuThemeLight = 40001;
constexpr UINT kMenuThemeDark = 40002;
constexpr UINT kMenuThemeSystem = 40003;
constexpr UINT kMenuResetSettings = 40004;
constexpr UINT kMenuPreviewEnabled = 40005;
constexpr UINT kMenuPreview1Mb = 40006;
constexpr UINT kMenuPreview16Mb = 40007;
constexpr UINT kMenuPreview64Mb = 40008;
constexpr UINT kMenuOperationDetails = 40009;
constexpr UINT kMenuForgetUnavailableDrive = 40010;
constexpr UINT kMenuExportDiagnostics = 40011;
constexpr UINT kForgetDriveCommandBase = 41000;
constexpr size_t kMaxForgetDriveMenuItems = 1000;
constexpr UINT WM_APP_PREVIEW_READY = WM_APP + 3;
constexpr UINT WM_APP_UNAVAILABLE_VOLUMES = WM_APP + 4;
constexpr UINT WM_APP_FORGET_VOLUME_RESULT = WM_APP + 5;
constexpr UINT WM_APP_FOLDER_AGGREGATE = WM_APP + 9;
constexpr UINT WM_APP_ENGINE_STATUS = WM_APP + 7;
constexpr UINT kContextCommandBase = 20000;
constexpr UINT_PTR kWorkspaceSaveTimer = 0xF1F1;
constexpr UINT_PTR kUiScrollAnimTimer = 0xF1F2;
// Bounds for the per-path type-name cache and its in-flight lookup list.
constexpr size_t kMaxFileTypeCacheEntries = 4096;
constexpr size_t kMaxInFlightFileTypeLookups = 32;

// Scale a DIP metric to physical pixels for Win32 control layout.
int Scaled(int dipValue) {
    return static_cast<int>(ffui::UiScale(static_cast<float>(dipValue)));
}

float CurrentDpiScale() {
    return (std::max)(0.01f, ffui::UiScale(1.0f));
}

bool IsNativeTextInput(HWND hwnd) {
    if (hwnd == nullptr) return false;
    wchar_t className[32]{};
    const int length = GetClassNameW(hwnd, className, static_cast<int>(std::size(className)));
    return length > 0 && (_wcsicmp(className, L"Edit") == 0 || _wcsicmp(className, L"RichEdit20W") == 0);
}

bool UiInteractionDiagnosticsEnabled() {
    wchar_t value[8]{};
    const DWORD length = GetEnvironmentVariableW(L"FASTFILES_UIA_DIAGNOSTICS", value,
                                                  static_cast<DWORD>(std::size(value)));
    return length == 1 && value[0] == L'1';
}

void AppendUiCommandDiagnostic(const std::wstring& commandId, bool accepted,
                               size_t workspacePaneCountBefore, size_t workspacePaneCountAfter,
                               size_t activePaneBefore, size_t activePaneAfter,
                               bool viewDualBefore, bool viewDualAfter) {
    if (!UiInteractionDiagnosticsEnabled()) return;
    ffprotocol::DiagnosticEvent event{};
    event.category = ffprotocol::DiagnosticCategory::UiInteraction;
    event.code = ffprotocol::DiagnosticCode::FFDIAG_UI_COMMAND_DISPATCHED;
    event.state = L"command=" + commandId + L";accepted=" + (accepted ? L"true" : L"false") +
        L";workspacePaneCountBefore=" + std::to_wstring(workspacePaneCountBefore) +
        L";workspacePaneCountAfter=" + std::to_wstring(workspacePaneCountAfter) +
        L";activePaneBefore=" + std::to_wstring(activePaneBefore) +
        L";activePaneAfter=" + std::to_wstring(activePaneAfter) +
        L";viewDualBefore=" + (viewDualBefore ? L"true" : L"false") +
        L";viewDualAfter=" + (viewDualAfter ? L"true" : L"false");
    event.outcome = accepted ? L"accepted" : L"rejected";
    ffprotocol::AppendDiagnostic(event, ffprotocol::DiagnosticFormat::Json);
}

std::wstring FileNameOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring ExtensionOf(const std::wstring& path) {
    const std::wstring name = FileNameOf(path);
    const size_t dot = name.find_last_of(L'.');
    return dot == std::wstring::npos ? L"" : name.substr(dot);
}

std::wstring FormatTime(const FILETIME& time) {
    // A zero FILETIME is "no timestamp available" (e.g. a failed attribute
    // fetch), not the year 1601 -- render it as an em-dash.
    if (time.dwHighDateTime == 0 && time.dwLowDateTime == 0) return L"\u2014";
    FILETIME localFileTime{};
    SYSTEMTIME local{};
    if (!FileTimeToLocalFileTime(&time, &localFileTime) || !FileTimeToSystemTime(&localFileTime, &local)) return L"Unavailable";
    wchar_t buffer[64]{};
    swprintf_s(buffer, L"%04u-%02u-%02u %02u:%02u", local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute);
    return buffer;
}

std::wstring FormatAttributes(uint32_t attributes) {
    std::wstring result;
    const auto add = [&result](const wchar_t* name) { if (!result.empty()) result += L", "; result += name; };
    if ((attributes & FILE_ATTRIBUTE_READONLY) != 0) add(L"Read-only");
    if ((attributes & FILE_ATTRIBUTE_HIDDEN) != 0) add(L"Hidden");
    if ((attributes & FILE_ATTRIBUTE_SYSTEM) != 0) add(L"System");
    if ((attributes & FILE_ATTRIBUTE_ARCHIVE) != 0) add(L"Archive");
    return result.empty() ? L"Normal" : result;
}

std::wstring FormatUnavailableVolume(const ffprotocol::UnavailableVolumeRecord& record) {
    GUID guid{};
    static_assert(sizeof(guid) == sizeof(record.volumeGuid));
    std::memcpy(&guid, record.volumeGuid, sizeof(guid));
    wchar_t guidText[40]{};
    if (StringFromGUID2(guid, guidText, static_cast<int>(std::size(guidText))) == 0) {
        wcscpy_s(guidText, L"{unknown-volume-guid}");
    }
    wchar_t serialText[16]{};
    swprintf_s(serialText, L"%08X", record.serialNumber);
    return std::wstring(guidText) + L"  (serial " + serialText + L", "
        + std::to_wstring(record.entryCount) + L" indexed entries)";
}

#ifdef _DEBUG
std::wstring FormatSelectionProbeIdentity(const std::optional<SelectionIdentity>& identity) {
    if (!identity) return {};
    if (!identity->hasStableFileId) return L"path:" + identity->path;
    return L"indexed:" + std::to_wstring(identity->volumeRowId) + L":" +
           std::to_wstring(identity->fileIdHigh) + L":" + std::to_wstring(identity->fileIdLow);
}

std::wstring FormatSelectionProbeState(const SelectionDiagnostics& diagnostics) {
    const std::wstring selectedPath = diagnostics.selectedIdentity
        ? diagnostics.selectedIdentity->path : std::wstring{};
    const std::wstring focusedIdentity = FormatSelectionProbeIdentity(diagnostics.focusedIdentity);
    const std::wstring selectedIdentity = FormatSelectionProbeIdentity(diagnostics.selectedIdentity);
    std::wstring visibleItems;
    for (const auto& item : diagnostics.visibleItemNames) {
        if (!visibleItems.empty()) visibleItems += L"|";
        visibleItems += item;
    }
    return L"ActivePath=" + diagnostics.activePath + L"\n" +
           L"VisibleItems=" + visibleItems + L"\n" +
           L"SelectedPath=" + selectedPath + L"\n" +
           L"SelectedIdentity=" + selectedIdentity + L"\n" +
           L"SelectedIndex=" + std::to_wstring(diagnostics.selectedIndex) + L"\n" +
           L"FocusedIdentity=" + focusedIdentity + L"\n" +
           L"FocusedIndex=" + std::to_wstring(diagnostics.focusedIndex) + L"\n" +
           L"OperationTarget=" + diagnostics.operationTarget + L"\n";
}
#endif
}

WindowShell::WindowShell() : previewController_([this](uint64_t requestId, PreviewResult result) {
    // Publish immutably: RenderDetails takes only this shared_ptr under the
    // lock, so a large decoded image is never deep-copied per paint frame.
    const auto state = result.state;
    auto payload = std::make_shared<const PreviewResult>(std::move(result));
    std::lock_guard<std::mutex> lock(previewMutex_);
    if (requestId == activePreviewRequest_) {
        preview_ = std::move(payload);
        switch (state) {
            case PreviewState::Ready: columnView_.SetUiaDetailsStatus(L"Preview: Ready"); break;
            case PreviewState::TooLarge: columnView_.SetUiaDetailsStatus(L"Preview: Too large"); break;
            case PreviewState::PermissionDenied: columnView_.SetUiaDetailsStatus(L"Preview: Permission denied"); break;
            case PreviewState::Unavailable: columnView_.SetUiaDetailsStatus(L"Preview: Unavailable"); break;
            case PreviewState::Disabled: columnView_.SetUiaDetailsStatus(L"Preview: Disabled"); break;
            case PreviewState::Loading: columnView_.SetUiaDetailsStatus(L"Preview: Loading"); break;
            case PreviewState::Unsupported: columnView_.SetUiaDetailsStatus(L"Preview: Unsupported"); break;
        }
        const HWND target = hwnd_.load();
        if (target != nullptr) PostMessageW(target, WM_APP_PREVIEW_READY, 0, 0);
    }
}) {}

void WindowShell::NavigateWorkspace(const std::wstring& path, const std::wstring& selectName) {
    if (path.empty()) return;
    // quick-access-home 5.4: delegate the model + view sync to the single
    // Home-aware entry, then re-apply the requested selection (NavigateToLocation
    // performs a plain path navigation that cannot carry it).
    NavigateToLocation(NavigationLocation::FromPath(path));
    if (!selectName.empty()) {
        columnView_.NavigateToPath(path, selectName);
        // ui-v3 hardening V3-03: a rebuilt column chain can leave the new leaf
        // off-screen; reveal it with the same animated ensure-visible path the
        // click/keyboard flows use.
        EnsureColumnVisible(columnView_.FocusedColumnIndex(), NavigationViewportWidth());
        RefreshSelectionPresentation();
        RequestRepaint();
    }
}

void WindowShell::NavigateColumnViewTo(const std::wstring& path) {
    // View-only filesystem navigation (the model is already in sync -- used by
    // back/forward where the model moved first). quick-access-home 5.4: clear
    // the pane's Home kind; an empty path is "This PC", not Home.
    columnView_.SetPaneHome(static_cast<int>(navigationWorkspace_.ActivePaneIndex()), false);
    if (path.empty()) columnView_.NavigateToComputerRoot();
    else columnView_.NavigateToPath(path);
    // ui-v3 hardening V3-03: back/forward rebuilds the whole chain too.
    EnsureColumnVisible(columnView_.FocusedColumnIndex(), NavigationViewportWidth());
}

void WindowShell::HideToolWorkspaces() {
    searchPanel_.Hide();
    storageAnalysis_.Hide();
    navigationChrome_.SetSearchActive(false);
    SyncWorkspaceChrome();
    SetFocus(hwnd_);
}

void WindowShell::OpenSearchWorkspace() {
    storageAnalysis_.Hide();
    searchPanel_.ShowAndFocus(columnView_.ActivePanePath(), engineActive_,
                              CurrentShellLayout().contentRect);
    navigationChrome_.SetSearchActive(true);
    SyncWorkspaceChrome();
    RequestRepaint();
}

void WindowShell::OpenStorageWorkspace() {
    searchPanel_.Hide();
    navigationChrome_.SetSearchActive(false);
    const std::wstring scope = columnView_.ActivePanePath();
    storageAnalysis_.ShowAndFocus(scope, engineActive_, CurrentShellLayout().contentRect);
    SyncWorkspaceChrome();
    RequestRepaint();
}

void WindowShell::SyncWorkspaceChrome() {
    columnView_.SetBrowseContentActive(!searchPanel_.Visible() && !storageAnalysis_.Visible());
    NavigationSidebar::ActiveWorkspace workspace = NavigationSidebar::ActiveWorkspace::Browse;
    if (searchPanel_.Visible()) workspace = NavigationSidebar::ActiveWorkspace::Search;
    else if (storageAnalysis_.Visible()) workspace = NavigationSidebar::ActiveWorkspace::Storage;
    else if (navigationWorkspace_.ActiveContext().isHomeLocation) {
        workspace = NavigationSidebar::ActiveWorkspace::Home;
    }
    navigationSidebar_.SetActiveWorkspace(workspace);
    navigationSidebar_.Refresh();
    navigationChrome_.Refresh();
}

void WindowShell::NavigateToLocation(const NavigationLocation& location,
                                     const std::function<void()>& populateFileSystem) {
    HideToolWorkspaces();
    // quick-access-home 5.4: Home and filesystem destinations take separate
    // model + view paths. `Navigate` no-ops when the model is already at the
    // target location, so callers that moved the model first (back/forward,
    // address/drive/breadcrumb commits) stay history-safe here.
    const int activePane = static_cast<int>(navigationWorkspace_.ActivePaneIndex());
    if (location.IsHome()) {
        navigationWorkspace_.Navigate(NavigationLocation::Home());
        columnView_.SetPaneHome(activePane, true);
    } else {
        navigationWorkspace_.Navigate(location.path);
        columnView_.SetPaneHome(activePane, false);
        if (populateFileSystem) populateFileSystem();
        else if (location.path.empty()) columnView_.NavigateToComputerRoot();
        else columnView_.NavigateToPath(location.path);
        // ui-v3 hardening V3-03: address-bar/breadcrumb/drive/sidebar commits
        // rebuild the column chain from scratch, which used to keep the
        // horizontal scroll anchored at the old position and left the new leaf
        // column off-screen (the viewport kept showing stale root columns).
        EnsureColumnVisible(columnView_.FocusedColumnIndex(), NavigationViewportWidth());
    }
    RefreshSelectionPresentation();
    // original-vision-journey-recovery 3.1: workspace chrome must observe the
    // new location, after Home/filesystem state and the columns agree.
    SyncWorkspaceChrome();
    RefreshHomeData();
    RequestRepaint();
}

void WindowShell::RefreshHomeData() {
    // quick-access-home 5.5: rebuild the Home snapshot from live state. Called
    // from every data-mutation refresh point (navigation, back/forward, tabs,
    // panes, bookmarks, recents, search history, known-folder re-resolve).
    HomeData data;
    data.pinned = navigationWorkspace_.State().bookmarks;
    data.recentFolders = navigationWorkspace_.RecentFoldersForHome(10);
    data.recentSearches = searchPanel_.RecentQueries(10);
    data.knownFolders = navigationWorkspace_.EnumerateKnownFolders();
    data.showActions = true;
    columnView_.SetHomeData(data);
    RequestRepaint();
}

void WindowShell::SaveActiveTabViewState() {
    // A mouse click updates the workspace pane after ColumnView hit-testing;
    // keep the model's active-pane choice authoritative before persisting.
    navigationWorkspace_.ActivatePane(static_cast<size_t>(columnView_.ActivePane()));
    const size_t paneCount = navigationWorkspace_.ActivePaneCount();
    for (size_t pane = 0; pane < paneCount; ++pane) {
        auto& context = navigationWorkspace_.PaneContext(pane);
        std::vector<std::wstring> columnPaths;
        int focusedIndex = 0;
        float scrollOffset = 0.0f;
        columnView_.SavePaneState(static_cast<int>(pane), columnPaths, focusedIndex, scrollOffset);
        context.columnPaths = std::move(columnPaths);
        context.columnSelections = {focusedIndex};
        context.columnScrollOffsets = {scrollOffset};
        if (focusedIndex >= 0 && focusedIndex < static_cast<int>(context.columnPaths.size())) {
            context.currentPath = context.columnPaths[static_cast<size_t>(focusedIndex)];
        }
    }
}

void WindowShell::RestoreActiveTabViewState() {
    HideToolWorkspaces();
    const bool dualPane = navigationWorkspace_.ActivePaneCount() == 2;
    columnView_.SetDualPane(dualPane);
    for (size_t pane = 0; pane < navigationWorkspace_.ActivePaneCount(); ++pane) {
        const auto& context = navigationWorkspace_.PaneContext(pane);
        columnView_.ActivatePane(static_cast<int>(pane));
        if (context.isHomeLocation) {
            // quick-access-home 5.4: a Home pane carries no column state; keep
            // the pane in Home mode and skip column restoration entirely.
            columnView_.SetPaneHome(static_cast<int>(pane), true);
            continue;
        }
        columnView_.SetPaneHome(static_cast<int>(pane), false);
        if (context.columnPaths.empty() ||
            (context.columnPaths.size() == 1 && context.columnPaths.front().empty())) {
            if (context.currentPath.empty()) columnView_.NavigateToComputerRoot();
            else columnView_.NavigateToPath(context.currentPath);
            // ui-v3 hardening V3-03: same reveal for restored fallback chains.
            EnsureColumnVisible(columnView_.FocusedColumnIndex(), NavigationViewportWidth());
            continue;
        }
        columnView_.RestorePaneState(
            static_cast<int>(pane), context.columnPaths,
            context.columnSelections.empty() ? 0 : context.columnSelections.front(),
            context.columnScrollOffsets.empty() ? 0.0f : context.columnScrollOffsets.front());
    }
    columnView_.ActivatePane(static_cast<int>(navigationWorkspace_.ActivePaneIndex()));
}

float WindowShell::NavigationViewportWidth() const {
    return CurrentShellLayout().details.availableColumnWidthDip;
}

ffui::MainShellLayout WindowShell::CurrentShellLayout() const {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const float scale = CurrentDpiScale();
    // Search and Storage are alternate workspace surfaces.  They replace the
    // browse/details board, so their native controls and treemap receive the
    // full post-navigation content rect rather than reserving a stale details
    // pane underneath them.
    const bool reserveDetails = detailsPaneVisible_ &&
        !searchPanel_.Visible() && !storageAnalysis_.Visible();
    return ffui::ComputeMainShellLayout(
        static_cast<float>((std::max)(0L, client.right - client.left)),
        static_cast<float>((std::max)(0L, client.bottom - client.top)),
        static_cast<float>(navigationSidebar_.Width()),
        ffui::UiMetrics::kChromeHeight,
        reserveDetails,
        scale,
        columnView_.IsDualPane());
}

CommandContext WindowShell::CurrentCommandContext() const {
    return {ClassifySelection(CurrentCommandSelection()),
            !clipboardPaths_.empty() || HasFilesOnClipboard(hwnd_),
            !storageAnalysis_.Visible() && !searchPanel_.Visible(),
            fileOperations_.HasOutstandingWork()};
}

std::vector<SelectionItem> WindowShell::CurrentCommandSelection() const {
    if (storageAnalysis_.Visible()) return storageAnalysis_.ActiveSelectionItems();
    if (searchPanel_.Visible()) return searchPanel_.ActiveSelectionItems();
    return columnView_.ActiveSelectionItems();
}

bool WindowShell::InitializeCommands() {
    const BaselineHandlers handlers{[this](const std::wstring& commandId) -> CommandHandler {
        return [this, commandId](const std::vector<std::wstring>& paths) {
            if (commandId == L"file.cancel-operations") {
                CancelFileOperations();
                return;
            }
            const std::wstring activePath = columnView_.ActivePanePath();
            if (!activePath.empty() && activePath != navigationWorkspace_.ActiveContext().currentPath) {
                navigationWorkspace_.Navigate(activePath);
                RefreshHomeData(); // quick-access-home 5.5: navigation records recents
            }
            if (commandId == L"file.copy" || commandId == L"file.cut") {
                clipboardPaths_ = paths;
                clipboardIsCut_ = commandId == L"file.cut";
                // Keep the app-local vector as a fallback for a clipboard that
                // is temporarily owned by another process, but publish the
                // normal shell format whenever possible so Explorer and other
                // file managers can paste this selection.
                const bool published = CopyFilesToClipboard(hwnd_, paths, clipboardIsCut_);
                clipboardSequenceNumber_ = published ? GetClipboardSequenceNumber() : 0;
                SyncCutGhost();
            } else if (commandId == L"file.paste") {
                const std::wstring destination = columnView_.ActivePanePath();
                // The shell clipboard is authoritative: read it fresh at paste
                // time so content copied elsewhere after our last app-local
                // copy wins. The app-local vector is only a fallback for a
                // clipboard another process temporarily owns or has cleared --
                // it must never shadow fresher clipboard content.
                bool externalCut = false;
                std::vector<std::wstring> pathsToPaste = ReadFilesFromClipboard(hwnd_, externalCut);
                // Preferred DropEffect is the interoperable source of truth,
                // but clipboard managers are allowed to preserve CF_HDROP and
                // drop that auxiliary format. Recover the app's own Cut only
                // while the sequence number and path list still match; this
                // cannot turn a later external clipboard copy into a move.
                const bool localClipboardCut = CanUseLocalCutClipboardFallback(
                    pathsToPaste, clipboardPaths_, externalCut, clipboardIsCut_,
                    GetClipboardSequenceNumber(), clipboardSequenceNumber_);
                bool cut = externalCut || localClipboardCut;
                if (pathsToPaste.empty() && !clipboardPaths_.empty()) {
                    pathsToPaste = clipboardPaths_;
                    cut = clipboardIsCut_;
                }
                if (!pathsToPaste.empty() && !destination.empty()) {
                    if (QueueTransfer(pathsToPaste, destination,
                                      cut ? FileOperationKind::Move : FileOperationKind::Copy)) {
                        if (cut) {
                            // A move consumes the source selection: drop both the
                            // app-local vector and the shell clipboard content so
                            // a second paste cannot duplicate the operation.
                            clipboardPaths_.clear();
                            clipboardIsCut_ = false;
                            clipboardSequenceNumber_ = 0;
                            SyncCutGhost();
                            if (OpenClipboard(hwnd_)) {
                                EmptyClipboard();
                                CloseClipboard();
                            }
                        }
                    }
                }
            } else if (commandId == L"file.new-folder") {
                const std::wstring destination = columnView_.ActivePanePath();
                if (!destination.empty()) {
                    const auto exists = [](const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; };
                    const std::wstring suggested = exists((std::filesystem::path(destination) / L"New folder").wstring())
                        ? GenerateKeepBothName(destination, L"New folder", exists) : L"New folder";
                    fileOperations_.Enqueue({FileOperationKind::CreateFolder, {}, destination, suggested, true});
                }
            } else if (commandId == L"file.new-file") {
                const std::wstring destination = columnView_.ActivePanePath();
                if (!destination.empty()) {
                    const auto exists = [](const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; };
                    const std::wstring suggested = exists((std::filesystem::path(destination) / L"New file.txt").wstring())
                        ? GenerateKeepBothName(destination, L"New file.txt", exists) : L"New file.txt";
                    fileOperations_.Enqueue({FileOperationKind::CreateFile, {}, destination, suggested, true});
                }
            } else if (commandId == L"file.delete" || commandId == L"file.delete-permanently") {
                const bool permanent = commandId == L"file.delete-permanently";
                if (paths.empty()) return;
                if (permanent) {
                    std::wstring prompt = L"Permanently delete " + std::to_wstring(paths.size()) + L" item(s)? This cannot be undone.\n\n" + paths.front();
                    if (MessageBoxW(hwnd_, prompt.c_str(), L"Delete permanently", MB_OKCANCEL | MB_DEFBUTTON2 | MB_ICONWARNING) != IDOK) return;
                }
                fileOperations_.Enqueue({FileOperationKind::Delete, paths, {}, {}, !permanent});
            } else if (commandId == L"file.undo") {
                const auto undo = operationHistory_.Pop();
                if (!undo) {
                    MessageBoxW(hwnd_, L"There is no reversible file operation to undo in this session.", L"Undo", MB_OK | MB_ICONINFORMATION);
                    return;
                }
                if (undo->kind == ReversibleOperationKind::RecycleDelete) {
                    FileOperationRequest request{};
                    request.kind = FileOperationKind::Restore;
                    request.restorePaths = undo->paths;
                    request.recordHistory = false;
                    for (const auto& path : undo->paths) request.sources.push_back(path.originalPath);
                    fileOperations_.Enqueue(std::move(request));
                } else if (undo->kind == ReversibleOperationKind::Rename && undo->paths.size() == 1) {
                    FileOperationRequest request{};
                    request.kind = FileOperationKind::Rename;
                    request.sources = {undo->paths.front().resultingPath};
                    request.newName = std::filesystem::path(undo->paths.front().originalPath).filename().wstring();
                    request.recordHistory = false;
                    fileOperations_.Enqueue(std::move(request));
                } else {
                    for (const auto& path : undo->paths) {
                        FileOperationRequest request{};
                        request.kind = FileOperationKind::Move;
                        request.sources = {path.resultingPath};
                        request.destination = std::filesystem::path(path.originalPath).parent_path().wstring();
                        request.transferPlan = {{path.resultingPath, std::filesystem::path(path.originalPath).filename().wstring(), false}};
                        request.recordHistory = false;
                        fileOperations_.Enqueue(std::move(request));
                    }
                }
            } else if (commandId == L"item.open" && paths.size() == 1) {
                if (searchPanel_.Visible()) {
                    searchPanel_.ActivateSelection();
                    return;
                }
                const DWORD attributes = GetFileAttributesW(paths.front().c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) NavigateWorkspace(paths.front());
                else if (!OpenWithDefaultApplication(hwnd_, paths.front())) MessageBoxW(hwnd_, L"The item could not be opened.", L"Open", MB_OK | MB_ICONERROR);
            } else if (commandId == L"item.open-with" && paths.size() == 1) {
                if (!ShowOpenWithPicker(hwnd_, paths.front())) MessageBoxW(hwnd_, L"The application picker could not be opened.", L"Open with", MB_OK | MB_ICONERROR);
            } else if (commandId == L"item.copy-path") {
                if (!CopyPathsToClipboard(hwnd_, paths)) {
                    ShowStatusNotice(L"Could not copy the path to the clipboard.");
                }
            } else if (commandId == L"item.copy-relative-path") {
                // Base folder: the other pane's current location when dual-pane
                // mode is active, else the current view's root (leftmost)
                // column location (spec, "Copy Relative Path Action").
                // PathsRelativeTo falls back to the absolute path when no
                // relative path exists (cross-volume), surfaced to the user
                // via the notification below.
                const std::optional<std::wstring> otherPane = navigationWorkspace_.OtherPanePath();
                const std::wstring base = otherPane ? *otherPane : columnView_.RootPath();
                bool fallback = false;
                const auto relative = PathsRelativeTo(paths, base, fallback);
                if (!CopyPathsToClipboard(hwnd_, relative)) {
                    ShowStatusNotice(L"Could not copy the path to the clipboard.");
                }
                if (fallback) MessageBoxW(hwnd_, L"A relative path was not possible; the absolute path was copied instead.", L"Copy relative path", MB_OK | MB_ICONINFORMATION);
            } else if (commandId == L"item.open-containing-folder" && paths.size() == 1) {
                const std::filesystem::path selected(paths.front());
                NavigateWorkspace(selected.parent_path().wstring(), selected.filename().wstring());
            } else if (commandId == L"item.open-terminal") {
                const std::wstring target = paths.size() == 1 ? paths.front() : columnView_.ActivePanePath();
                if (!target.empty()) LaunchTerminalHere(hwnd_, target);
            } else if (commandId == L"item.properties") {
                if (paths.size() == 1 &&
                    !SHObjectProperties(hwnd_, SHOP_FILEPATH, paths.front().c_str(), nullptr)) {
                    MessageBoxW(hwnd_, L"Windows could not open the item properties.",
                                L"Properties", MB_OK | MB_ICONERROR);
                }
            } else if (commandId == L"selection.select-all") {
                columnView_.SelectAll();
                RefreshSelectionPresentation();
                RequestRepaint();
            } else if (commandId == L"navigation.up") {
                // quick-access-home 5.4: from Home the active pane reports an
                // empty path, so ParentNavigationPath returns nullopt and Up is
                // a no-op -- Home has no filesystem parent.
                const auto parentPath = ParentNavigationPath(columnView_.ActivePanePath());
                if (parentPath.has_value()) {
                    if (parentPath->empty()) {
                        HideToolWorkspaces();
                        navigationWorkspace_.Navigate(std::wstring{});
                        columnView_.NavigateToComputerRoot();
                        RefreshSelectionPresentation();
                        navigationChrome_.Refresh();
                        navigationSidebar_.Refresh();
                        RefreshHomeData(); // quick-access-home 5.5: navigation records recents
                        RequestRepaint();
                    } else {
                        NavigateWorkspace(*parentPath);
                    }
                }
            } else if (commandId == L"navigation.refresh") {
                columnView_.RefreshActiveColumn();
            } else if (commandId == L"search.focus") {
                OpenSearchWorkspace();
            } else if (commandId == L"storage.analyze") {
                OpenStorageWorkspace();
            } else if (commandId == L"app.settings") {
                settingsDialog_.Show();
            } else if (commandId == L"app.export-diagnostics") {
                SendMessageW(hwnd_, WM_COMMAND, kMenuExportDiagnostics, 0);
            } else if (commandId == L"app.forget-unavailable-drive") {
                SendMessageW(hwnd_, WM_COMMAND, kMenuForgetUnavailableDrive, 0);
            } else if (commandId == L"app.last-operation-errors") {
                SendMessageW(hwnd_, WM_COMMAND, kMenuOperationDetails, 0);
            } else if (commandId == L"app.command-palette") {
                paletteTargets_ = SelectionPaths(CurrentCommandSelection());
                paletteStorage_ = storageAnalysis_.Visible();
                paletteSearch_ = searchPanel_.Visible();
                commandPalette_.Show(CurrentCommandContext());
            } else if (commandId == L"navigation.focus-path") {
                navigationChrome_.FocusAddressBar();
            } else if (commandId == L"navigation.add-bookmark") {
                const std::wstring path = columnView_.ActivePanePath();
                if (!path.empty()) {
                    navigationWorkspace_.AddBookmark(path);
                    navigationWorkspace_.FlushState();
                    navigationSidebar_.Refresh();
                    RefreshHomeData(); // quick-access-home 5.5: keep Home Pinned in sync
                    RequestRepaint();
                }
            } else if (commandId == L"navigation.new-tab") {
                SaveActiveTabViewState();
                navigationWorkspace_.OpenTab(); // quick-access-home D7: Home default
                RestoreActiveTabViewState();
                RefreshSelectionPresentation();
                navigationChrome_.Refresh();
                RefreshHomeData();
                RequestRepaint();
            } else if (commandId == L"navigation.close-tab") {
                SaveActiveTabViewState();
                if (navigationWorkspace_.CloseActiveTab()) {
                    RestoreActiveTabViewState();
                    RefreshSelectionPresentation();
                    navigationChrome_.Refresh();
                    RefreshHomeData();
                    RequestRepaint();
                }
            } else if (commandId == L"navigation.next-tab" || commandId == L"navigation.previous-tab") {
                const size_t count = navigationWorkspace_.TabCount();
                if (count > 1) {
                    const size_t current = navigationWorkspace_.ActiveTabIndex();
                    SaveActiveTabViewState();
                    const size_t next = commandId == L"navigation.next-tab"
                        ? (current + 1) % count
                        : (current + count - 1) % count;
                    if (navigationWorkspace_.SwitchTab(next)) {
                        RestoreActiveTabViewState();
                        RefreshSelectionPresentation();
                        navigationChrome_.Refresh();
                        RefreshHomeData();
                        RequestRepaint();
                    }
                }
            } else if (commandId == L"navigation.reopen-tab") {
                if (navigationWorkspace_.ReopenClosedTab()) {
                    RestoreActiveTabViewState();
                    RefreshSelectionPresentation();
                    navigationChrome_.Refresh();
                    RefreshHomeData();
                    RequestRepaint();
                }
            } else if (commandId == L"navigation.back" || commandId == L"navigation.forward") {
                const bool moved = commandId == L"navigation.back" ? navigationWorkspace_.GoBack() : navigationWorkspace_.GoForward();
                if (moved) {
                    // quick-access-home 5.4: GoBack/GoForward already moved the
                    // model, so calling Navigate here would push a duplicate
                    // history entry. Sync the view only, restoring the pane's
                    // Home/FS kind from the active context.
                    const auto& context = navigationWorkspace_.ActiveContext();
                    const int activePane = static_cast<int>(navigationWorkspace_.ActivePaneIndex());
                    if (context.isHomeLocation) {
                        columnView_.SetPaneHome(activePane, true);
                    } else {
                        columnView_.SetPaneHome(activePane, false);
                        NavigateColumnViewTo(context.currentPath);
                    }
                    RefreshSelectionPresentation();
                    navigationChrome_.Refresh();
                    navigationSidebar_.Refresh();
                    RefreshHomeData();
                    RequestRepaint();
                }
            } else if (commandId == L"navigation.home") {
                // quick-access-home 5.3: Alt+Home navigates the active context
                // to the Home surface.
                NavigateToLocation(NavigationLocation::Home());
            } else if (commandId == L"navigation.toggle-column-view" || commandId == L"navigation.toggle-dual-pane") {
                const bool enabled = !navigationWorkspace_.IsDualPane();
                if (enabled) navigationWorkspace_.EnableDualPane();
                else navigationWorkspace_.DisableDualPane();
                // Keep the rendered ColumnView model in lockstep with the
                // workspace state. Previously the command only changed the
                // model, leaving ColumnView's dual-pane renderer disabled.
                columnView_.SetDualPane(enabled);
                // quick-access-home 5.4: after a pane-count change, sync each
                // pane's Home kind so a Home pane stays Home and an FS pane
                // renders columns.
                for (size_t pane = 0; pane < navigationWorkspace_.ActivePaneCount(); ++pane) {
                    columnView_.SetPaneHome(static_cast<int>(pane),
                                            navigationWorkspace_.PaneContext(pane).isHomeLocation);
                }
                RefreshHomeData();
                navigationChrome_.Refresh();
                RequestRepaint();
            } else if (commandId == L"view.toggle-details-pane") {
                detailsPaneVisible_ = !detailsPaneVisible_;
                RequestRepaint();
            } else if (commandId == L"file.rename" && paths.size() == 1) {
                const std::filesystem::path source(paths.front());
                const auto name = PromptForLeafName(hwnd_, L"Rename", source.filename().wstring());
                if (name && !name->empty() && *name != source.filename().wstring()) {
                    std::wstring reason;
                    const auto sibling = source.parent_path() / *name;
                    if (!IsValidFileName(*name, &reason)) {
                        MessageBoxW(hwnd_, reason.c_str(), L"Invalid name", MB_OK | MB_ICONWARNING);
                    } else if (GetFileAttributesW(sibling.c_str()) != INVALID_FILE_ATTRIBUTES) {
                        MessageBoxW(hwnd_, L"An item with that name already exists in this folder.", L"Rename", MB_OK | MB_ICONWARNING);
                    } else {
                        fileOperations_.Enqueue({FileOperationKind::Rename, paths, {}, *name, true});
                    }
                }
            }
        };
    }};
    if (!RegisterBaselineCommands(commandRegistry_, handlers)) return false;
    const auto shortcutPath = ffprotocol::ShortcutSettingsPath();
    std::error_code shortcutError;
    const bool canonicalExists = !shortcutPath.empty() && std::filesystem::exists(shortcutPath, shortcutError);
    if (canonicalExists) {
        shortcuts_.Load(shortcutPath, commandRegistry_, [](const std::wstring& message) {
            OutputDebugStringW((message + L"\n").c_str());
        });
    } else {
        shortcuts_.ResetDefaults(commandRegistry_);
        const ffprotocol::LegacyShortcutSettings legacy = ffprotocol::LoadLegacyShortcutSettings();
        if (legacy.sectionPresent) {
            if (!legacy.valid) {
                OutputDebugStringW(L"Legacy shortcut settings were malformed; defaults retained.\n");
            } else {
                shortcuts_.ImportLegacy(legacy.bindings, commandRegistry_, [](const std::wstring& message) {
                    OutputDebugStringW((message + L"\n").c_str());
                });
            }
            // Persist the migration result once. From this point forward the
            // general settings shortcut section is never consulted again.
            if (!shortcutPath.empty()) shortcuts_.Save(shortcutPath);
        }
    }
    if (!commandPalette_.Initialize(hwnd_, &commandRegistry_, &shortcuts_,
        [this](const std::wstring& id) {
            const auto* command = commandRegistry_.Find(id);
            if (command != nullptr && CommandUsesSelection(*command) &&
                (paletteStorage_ != storageAnalysis_.Visible() || paletteSearch_ != searchPanel_.Visible() ||
                 paletteTargets_ != SelectionPaths(CurrentCommandSelection()))) {
                ShowStatusNotice(L"The selection changed. Select the item again to run that command.");
                return;
            }
            InvokeCommand(id);
        })) return false;
    if (!searchPanel_.Initialize(hwnd_, &engineClient_, [this](const ffsearch::Candidate& candidate,
                                                                  const ffsearch::PathReconstruction& path) {
        const std::wstring navigationPath = candidate.isDirectory
            ? (std::filesystem::path(candidate.folder) / candidate.name).wstring()
            : candidate.folder;
        if (navigationPath.empty()) {
            SyncWorkspaceChrome();
            return;
        }
        // original-vision-journey-recovery 3.1: use the Home-aware shell
        // transition, retaining the engine's reconstructed ancestry without
        // first rebuilding/enumerating a second, path-only column chain.
        NavigateToLocation(NavigationLocation::FromPath(navigationPath), [this, &candidate, &path]() {
            if (path.complete) columnView_.NavigateToHierarchy(path.segments, candidate.isDirectory);
            else columnView_.NavigateToHierarchy(
                (std::filesystem::path(candidate.folder) / candidate.name).wstring(), candidate.isDirectory);
        });
    }, settings_.retainSearchHistory)) return false;
    searchPanel_.SetDefaultScope(settings_.defaultSearchScope);
    // quick-access-home 5.6: search history changes refresh Home's Recent
    // searches section.
    searchPanel_.SetHistoryChangedCallback([this] { RefreshHomeData(); });
    return true;
}

bool WindowShell::QueueTransfer(const std::vector<std::wstring>& paths, const std::wstring& destination,
                                FileOperationKind kind) {
    if (paths.empty() || destination.empty()) return false;
    fileOperations_.Enqueue({kind, paths, destination, {}, true});
    return true;
}

LRESULT CALLBACK WindowShell::InlineRenameProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                                UINT_PTR, DWORD_PTR referenceData) {
    auto* shell = reinterpret_cast<WindowShell*>(referenceData);
    if (message == WM_KEYDOWN && shell != nullptr) {
        if (wParam == VK_RETURN) {
            shell->FinishInlineRename(true);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            shell->FinishInlineRename(false);
            return 0;
        }
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

void WindowShell::BeginInlineRename(const std::wstring& path) {
    FinishInlineRename(false);
    inlineRenamePath_ = path;
    const auto shellLayout = CurrentShellLayout();
    // Position the edit from the same row-rect math the renderer uses
    // (ColumnView::Render): pane x-offset + column x - horizontal scroll for
    // the left edge, and the badge strip + row index * row height - the
    // pane's vertical scroll offset for the top. Ignoring the vertical scroll
    // used to place the edit over the wrong row once the column was scrolled.
    const int pane = columnView_.ActivePane();
    const float viewportWidth = NavigationViewportWidth();
    const float paneWidth = columnView_.IsDualPane() ? viewportWidth / 2.0f : viewportWidth;
    const float paneX = columnView_.IsDualPane() ? pane * paneWidth : 0.0f;
    const float paneScroll = pane == 0 ? scrollAnim_.Value() : columnView_.ScrollOffset2();
    const float verticalScroll = columnView_.VerticalScrollOffset(pane);
    const int x = static_cast<int>(ffui::UiScale(shellLayout.contentRect.left + paneX +
                                                 columnView_.FocusedColumnIndex() * ColumnView::kColumnWidth -
                                                 paneScroll + 24.0f));
    const int itemIndex = (std::max)(0, columnView_.FocusedItemIndex());
    const float rowHeight = BrowseRowHeight();
    const int y = static_cast<int>(ffui::UiScale(shellLayout.contentRect.top +
                                                 ColumnView::kBadgeHeight + static_cast<float>(itemIndex) * rowHeight -
                                                 verticalScroll));
    const int editHeight = Scaled(static_cast<int>(ffui::GetUiDensity(ffui::gUiDensityProfile).controlHeight));
    inlineRename_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", std::filesystem::path(path).filename().c_str(),
                                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, x, y, Scaled(205), editHeight, hwnd_,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(7201)), nullptr, nullptr);
    if (inlineRename_ == nullptr) {
        inlineRenamePath_.clear();
        return;
    }
    SendMessageW(inlineRename_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SetWindowSubclass(inlineRename_, InlineRenameProc, 1, reinterpret_cast<DWORD_PTR>(this));
    SendMessageW(inlineRename_, EM_SETSEL, 0, static_cast<LPARAM>(-1));
    SetFocus(inlineRename_);
}

void WindowShell::FinishInlineRename(bool commit) {
    if (inlineRename_ == nullptr) return;
    const HWND edit = inlineRename_;
    inlineRename_ = nullptr;
    RemoveWindowSubclass(edit, InlineRenameProc, 1);
    std::wstring value(static_cast<size_t>(GetWindowTextLengthW(edit)) + 1, L'\0');
    GetWindowTextW(edit, value.data(), static_cast<int>(value.size()));
    value.resize(wcslen(value.c_str()));
    DestroyWindow(edit);
    const std::wstring path = std::exchange(inlineRenamePath_, {});
    SetFocus(hwnd_);
    if (!commit || value == std::filesystem::path(path).filename().wstring()) return;
    std::wstring reason;
    const auto sibling = std::filesystem::path(path).parent_path() / value;
    if (!IsValidFileName(value, &reason)) {
        MessageBoxW(hwnd_, reason.c_str(), L"Invalid name", MB_OK | MB_ICONWARNING);
        BeginInlineRename(path);
        return;
    }
    if (GetFileAttributesW(sibling.c_str()) != INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(hwnd_, L"An item with that name already exists in this folder.", L"Rename", MB_OK | MB_ICONWARNING);
        BeginInlineRename(path);
        return;
    }
    fileOperations_.Enqueue({FileOperationKind::Rename, {path}, {}, value, true});
}

void WindowShell::CancelFileOperations() {
    if (!fileOperations_.HasOutstandingWork()) return;
    fileOperations_.CancelCurrent();
    // original-vision-journey-recovery 4.7: a terminal event can reach the UI
    // before the worker clears executing_. Request cancellation from live worker
    // state, but never overwrite an already presented terminal caption.
    if (fileOpOutstanding_) SetWindowTextW(hwnd_, L"FastFiles \u2014 cancelling operation\u2026");
}

bool WindowShell::InvokeCommand(const std::wstring& commandId) {
    const size_t workspacePaneCountBefore = navigationWorkspace_.ActivePaneCount();
    const size_t activePaneBefore = navigationWorkspace_.ActivePaneIndex();
    const bool viewDualBefore = columnView_.IsDualPane();
    auto selection = CurrentCommandSelection();
    const auto* descriptor = commandRegistry_.Find(commandId);
    const bool usesSelection = descriptor != nullptr && CommandUsesSelection(*descriptor);
    const auto paths = usesSelection ? SelectionPaths(selection) : std::vector<std::wstring>{};
    const bool browseWorkspace = !storageAnalysis_.Visible() && !searchPanel_.Visible();
    // Alternate-workspace rows may have become stale since rendering. Never
    // recover by borrowing a selection from the hidden browsing surface.
    const auto liveSelection = browseWorkspace || !usesSelection
        ? std::optional{selection} : ResolveExistingSelection(paths);
    if (!liveSelection) {
        ShowStatusNotice(L"The selected item is no longer available. Refresh its location and try again.");
        return false;
    }
    const CommandContext context{ClassifySelection(*liveSelection),
        !clipboardPaths_.empty() || HasFilesOnClipboard(hwnd_), browseWorkspace,
        fileOperations_.HasOutstandingWork()};
    const bool accepted = commandRegistry_.Invoke(commandId, context, paths);
    AppendUiCommandDiagnostic(commandId, accepted, workspacePaneCountBefore,
                              navigationWorkspace_.ActivePaneCount(), activePaneBefore,
                              navigationWorkspace_.ActivePaneIndex(), viewDualBefore,
                              columnView_.IsDualPane());
    if (!accepted) {
        MessageBoxW(hwnd_, L"That command is not available for the current selection.", L"Command unavailable", MB_OK | MB_ICONINFORMATION);
        return false;
    }
    return true;
}

bool WindowShell::DispatchShortcut(const MSG& message, ShortcutScope scope, bool nativeEditFocused) {
    if (message.message != WM_KEYDOWN && message.message != WM_SYSKEYDOWN) return false;
    if (!IsShortcutDispatchAllowed(scope, nativeEditFocused)) return false;
    uint8_t modifiers = ModifierNone;
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) modifiers |= ModifierControl;
    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) modifiers |= ModifierShift;
    if ((GetKeyState(VK_MENU) & 0x8000) != 0) modifiers |= ModifierAlt;
    const ShortcutBinding* binding = shortcuts_.FindByChord({static_cast<uint16_t>(message.wParam), modifiers}, scope);
    return binding != nullptr && InvokeCommand(binding->commandId);
}

void WindowShell::ShowContextMenu(POINT screenPoint) {
    const CommandContext context = CurrentCommandContext();
    const auto expectedTargets = SelectionPaths(CurrentCommandSelection());
    const bool expectedStorage = storageAnalysis_.Visible();
    const bool expectedSearch = searchPanel_.Visible();
    std::vector<const CommandDescriptor*> commands;
    for (const auto* command : commandRegistry_.Query(context)) {
        if (command->category == L"File" || command->category == L"Item") commands.push_back(command);
    }
    if (commands.empty()) return;
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return;
    std::vector<std::wstring> commandIds;
    std::wstring previousCategory;
    for (const auto* command : commands) {
        if (!previousCategory.empty() && previousCategory != command->category) AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        const bool enabled = CommandEnabled(*command, context);
        const UINT id = kContextCommandBase + static_cast<UINT>(commandIds.size());
        AppendMenuW(menu, MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED), id, command->displayName.c_str());
        commandIds.push_back(command->commandId);
        previousCategory = command->category;
    }
    const UINT selected = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                           screenPoint.x, screenPoint.y, hwnd_, nullptr);
    DestroyMenu(menu);
    if (selected >= kContextCommandBase && selected < kContextCommandBase + commandIds.size()) {
        const auto& id = commandIds[selected - kContextCommandBase];
        const auto* command = commandRegistry_.Find(id);
        if (command != nullptr && CommandUsesSelection(*command) &&
            (expectedStorage != storageAnalysis_.Visible() || expectedSearch != searchPanel_.Visible() ||
             expectedTargets != SelectionPaths(CurrentCommandSelection()))) {
            ShowStatusNotice(L"The selection changed. Select the item again to run that command.");
            return;
        }
        InvokeCommand(id);
    }
}

LRESULT CALLBACK WindowShell::WndProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    WindowShell* self = nullptr;
    if (message == WM_NCCREATE) {
        auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<WindowShell*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<WindowShell*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self != nullptr) {
        return self->HandleMessage(hwnd, message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool WindowShell::Initialize(HINSTANCE instance, int showCommand) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    // Request the native double-click message sequence so a file's ordinary
    // file-manager gesture (select, then double-click to open) is distinct
    // from the single-click folder activation used by Column View.
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    windowClass.lpfnWndProc = &WindowShell::WndProcThunk;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr; // Direct2D owns painting
    windowClass.lpszClassName = kWindowClassName;
    if (RegisterClassExW(&windowClass) == 0) {
        return false;
    }

    galleryMode_ = ffui::IsGalleryAllowed();
    const wchar_t* windowTitle = galleryMode_ ? L"FastFiles \u2014 Gallery (debug)" : L"FastFiles";
    hwnd_ = CreateWindowExW(
        0, kWindowClassName, windowTitle, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1024, 640,
        nullptr, nullptr, instance, this);
    if (hwnd_ == nullptr) {
        return false;
    }

    // final-product-completion-and-production-readiness: no native menubar.
    // Settings, diagnostics, and last-operation errors live in the Settings
    // dialog, command palette, and chrome gear — native menus stayed
    // system-light on dark D2D chrome.
    if (!renderer_.Initialize(hwnd_)) {
        return false;
    }
    settings_ = ffprotocol::LoadSettings();
    // quick-access-home: typed startup resolution -- a session that ended on
    // Home restores as Home instead of silently falling back to the default.
    const NavigationLocation initialLocation = ResolveInitialNavigationLocation(
        settings_.restorePreviousSession, navigationWorkspace_.State(), settings_.startupLocation);
    if (!initialLocation.IsHome() && !initialLocation.path.empty() &&
        initialLocation.path != navigationWorkspace_.ActiveContext().currentPath) {
        navigationWorkspace_.Navigate(initialLocation.path);
    }
    ApplyTheme();
    columnView_.Initialize(&engineClient_);
    // semantic-uia-core-surfaces 2.5: pane-0 horizontal scrolling from the UIA
    // Scroll pattern drives the same animated offset Shift+wheel uses.
    columnView_.SetUiaHorizontalScrollWriter([this](float offset) {
        const uint64_t now = GetTickCount64();
        scrollAnim_.Snap(scrollAnim_.Value());
        scrollAnim_.AnimateTo(offset, kUiAnimationDefaultMs, now);
        HandleAnimationTimer(true);
        RequestRepaint();
    });
    // selection-model-interactions click fix: pane-1 UIA Scroll-pattern writes
    // mutate ColumnView state directly (no animation bridge), so ColumnView
    // needs this repaint bridge to keep the painted frame, the pushed UIA
    // geometry, and the hit-test offsets in the same generation.
    columnView_.SetRepaintCallback([this] { RequestRepaint(); });
    // The icon cache posts WM_APP_ICON_READY to hwnd_ when a type icon resolves;
    // the shell repaints on that message. The column view draws the cached
    // bitmap (16 DIP) with a themed glyph placeholder until it arrives.
    iconCache_ = std::make_unique<ffui::IconCache>(hwnd_);
    columnView_.SetIconCache(iconCache_.get());
    if (!navigationChrome_.Initialize(hwnd_, &navigationWorkspace_, [this](const ffui::NavigationLocation& location, bool restoreTabState) {
            // quick-access-home 5.2: restoreTabState restores a target tab's
            // saved view (Home-safe); otherwise route through the single
            // Home-aware navigation entry.
            if (restoreTabState) RestoreActiveTabViewState();
            else NavigateToLocation(location);
            RefreshSelectionPresentation();
            RequestRepaint();
        }, [this] { SaveActiveTabViewState(); },
        [this](const std::wstring& commandId) { InvokeCommand(commandId); })) return false;
    if (!navigationSidebar_.Initialize(hwnd_, &navigationWorkspace_, [this](const NavigationSidebar::NavigationTarget& target) {
            if (target.isSearch) {
                OpenSearchWorkspace();
                return;
            }
            if (target.isStorage) {
                OpenStorageWorkspace();
                return;
            }
            // quick-access-home 5.1: a Home target routes to Home navigation
            // (or a fresh Home tab on middle-click) before any filesystem
            // handling -- its path is empty and must not reach ShowUnavailableLocation.
            if (target.isHome) {
                if (target.openInNewTab) {
                    SaveActiveTabViewState();
                    navigationWorkspace_.OpenTab(); // Home default
                    RestoreActiveTabViewState();
                } else {
                    NavigateToLocation(NavigationLocation::Home());
                }
                RefreshSelectionPresentation();
                navigationChrome_.Refresh();
                navigationSidebar_.Refresh();
                RefreshHomeData();
                RequestRepaint();
                return;
            }
            HideToolWorkspaces();
            if (target.path.empty()) {
                columnView_.ShowUnavailableLocation(target.displayName);
                RefreshSelectionPresentation();
                RequestRepaint();
                return;
            }
            if (target.openInNewTab) {
                SaveActiveTabViewState();
                navigationWorkspace_.OpenTab(target.path);
                RestoreActiveTabViewState();
            }
            else navigationWorkspace_.Navigate(target.path);
            if (!target.openInNewTab) columnView_.NavigateToPath(target.path);
            RefreshSelectionPresentation();
            navigationChrome_.Refresh();
            navigationSidebar_.Refresh();
            RequestRepaint();
        })) return false;
    // quick-access-home 5.5: sidebar bookmark edits (context-menu rename/remove/
    // reorder) change Home's Pinned section; refresh the Home surface.
    navigationSidebar_.SetBookmarksChangedCallback([this] { RefreshHomeData(); });
    // quick-access-home 5.4: route Home item activation from ColumnView. Folder
    // -> navigate (new tab when flagged); Search -> open search and run the
    // query; QuickAction -> the named command; the two Clear* kinds empty the
    // corresponding history.
    columnView_.SetHomeActivateCallback([this](const HomeActivation& activation) {
        switch (activation.kind) {
            case HomeActionKind::Folder:
                // quick-access-home hardening (HOME-POST-02): defensive shell
                // boundary -- a missing folder destination must NEVER collapse
                // into the "This PC" root (which is only reachable through an
                // explicit This PC action). ColumnView already refuses to emit
                // empty Folder activations; this guards any other producer.
                if (activation.value.empty()) return;
                if (activation.openInNewTab) {
                    SaveActiveTabViewState();
                    navigationWorkspace_.OpenTab(activation.value);
                    RestoreActiveTabViewState();
                    RefreshSelectionPresentation();
                    navigationChrome_.Refresh();
                    navigationSidebar_.Refresh();
                    RefreshHomeData();
                    RequestRepaint();
                } else {
                    NavigateToLocation(NavigationLocation::FromPath(activation.value));
                }
                break;
            case HomeActionKind::Search:
                if (activation.openInNewTab) {
                    SaveActiveTabViewState();
                    navigationWorkspace_.OpenTab(); // Home default
                    RestoreActiveTabViewState();
                }
                // Scope stays the user's persisted default (SetDefaultScope at
                // init); from Home there is no folder context to fabricate, so
                // ShowAndFocus passes an empty path and the existing selection
                // governs the scope.
                storageAnalysis_.Hide();
                searchPanel_.ShowAndFocus(L"", engineActive_, CurrentShellLayout().contentRect);
                searchPanel_.SetQueryAndDispatch(activation.value);
                navigationChrome_.SetSearchActive(true);
                SyncWorkspaceChrome();
                RefreshSelectionPresentation();
                navigationChrome_.Refresh();
                navigationSidebar_.Refresh();
                RefreshHomeData();
                RequestRepaint();
                break;
            case HomeActionKind::QuickAction:
                InvokeCommand(activation.value);
                break;
            case HomeActionKind::ClearRecentFolders:
                navigationWorkspace_.ClearRecentFolders();
                // quick-access-home hardening: the spec's explicit Clear action
                // persists immediately (safe atomic flush). Normal navigation
                // recording still rides the 500 ms debounced writer.
                navigationWorkspace_.FlushState();
                RefreshHomeData();
                break;
            case HomeActionKind::ClearRecentSearches:
                searchPanel_.ClearHistory();
                RefreshHomeData();
                break;
        }
    });
    if (!fileOperations_.Start(hwnd_)) return false;
    if (!InitializeCommands()) return false;
    if (!storageAnalysis_.Initialize(hwnd_, &engineClient_,
        [this](const std::wstring& path, bool isDirectory) {
            if (!path.empty()) {
                if (!isDirectory) {
                    // semantic-treemap-uia 29.x: a file tile activation opens
                    // the standard Column View at the file's CONTAINING
                    // FOLDER with the file selected (canonical
                    // storage-drilldown-and-treemap contract). The old
                    // OpenWithDefaultApplication behavior launched the file,
                    // contradicting the spec's navigate/select semantics.
                    const std::filesystem::path selected(path);
                    storageAnalysis_.Hide();
                    NavigateWorkspace(selected.parent_path().wstring(),
                                      selected.filename().wstring());
                    return;
                }
                navigationWorkspace_.Navigate(path);
                columnView_.SetPaneHome(static_cast<int>(navigationWorkspace_.ActivePaneIndex()), false);
                columnView_.NavigateToPath(path);
                EnsureColumnVisible(columnView_.FocusedColumnIndex(), NavigationViewportWidth());
                RefreshSelectionPresentation();
                navigationChrome_.Refresh();
                navigationSidebar_.Refresh();
                RefreshHomeData(); // quick-access-home 5.5: navigation records recents
                RequestRepaint();
            }
        },
        [this]() {
            HideToolWorkspaces();
            RequestRepaint();
        },
        [this](const std::wstring& commandId, const std::vector<std::wstring>& paths) {
            // Selection may change in TrackPopupMenu's nested message loop.
            // Reject stale menu targets rather than retargeting another row.
            if (paths != SelectionPaths(storageAnalysis_.ActiveSelectionItems())) return;
            InvokeCommand(commandId);
        })) return false;
    // semantic-treemap-uia 10.x: attach the Storage Analysis treemap surface
    // to the Column View fragment root so the UIA tree describes the treemap
    // while it is the visible workspace.
    columnView_.SetUiaTreemapSurface(&storageAnalysis_);
    storageAnalysis_.SetCategories(settings_.storageCategories);
    if (!settingsDialog_.Initialize(hwnd_, &settings_, &commandRegistry_, &shortcuts_, &navigationWorkspace_,
        [this]() {
            SaveAndNotifySettings();
            ApplyTheme();
            navigationChrome_.Refresh();
            RequestRepaint();
        })) return false;
    settingsDialog_.SetEngineClient(&engineClient_);
    IDropTarget* dropTarget = nullptr;
    if (FAILED(CreateFileDropTarget(
            [this](POINTL point) {
                // stabilize-live-uia-e2e: resolve the drop destination from
                // the DROP POINT's pane (dual-pane split by content width),
                // not the active pane: the drag's own mouse-down activates
                // the source pane, which would make every cross-pane drag
                // land back in the source folder. The conversion inverts
                // UiaRectToScreen exactly: screen = (dip + originDip) * scale
                // + clientOrigin (ClientToScreen of the client origin).
                const auto shellLayout = CurrentShellLayout();
                const float scale = CurrentDpiScale();
                POINT clientOrigin{};
                ClientToScreen(hwnd_, &clientOrigin);
                const float clientX = DropPointClientXDip(point, clientOrigin, scale, shellLayout.contentRect.left);
                const float viewportWidth = shellLayout.details.availableColumnWidthDip;
                if (clientX < 0.0f || clientX >= viewportWidth) {
                    return columnView_.ActivePanePath();
                }
                return columnView_.PanePath(PaneIndexForDropPoint(clientX, viewportWidth));
            },
            [this](std::vector<std::wstring> paths, DWORD effect, std::wstring destination) {
                // stabilize-live-uia-e2e: destination comes from the drop
                // point (the pane the user dropped on), NOT from the active
                // pane: the drag's own mouse-down activated the source pane.
                if (destination.empty()) return;
                // complete-ole-drag-source-move-effect: an intra-app drop queues
                // its own transfer here; the drag source must not additionally
                // treat the returned MOVE as an external copy-then-delete.
                dragDropHandledInWindow_ = true;
                if (effect == DROPEFFECT_LINK) {
                    FileOperationRequest request{};
                    request.kind = FileOperationKind::Link;
                    request.sources = std::move(paths);
                    request.destination = destination;
                    // complete-ole-drag-source-move-effect D3: reserve planned
                    // destinations within the batch so two same-basename sources
                    // get distinct .lnk names instead of one overwriting the other.
                    const auto exists = [](const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; };
                    request.transferPlan = BuildLinkPlan(request.sources, destination, exists);
                    fileOperations_.Enqueue(std::move(request));
                } else {
                    QueueTransfer(paths, destination, effect == DROPEFFECT_MOVE ? FileOperationKind::Move : FileOperationKind::Copy);
                }
            // ui-v3 drag/drop: presentation-only feedback — the shell paints
            // destination emphasis while the drag is over the workspace and
            // clears it on leave/drop. No operation decisions flow through it.
            },
            [this](bool active, POINTL point, DWORD) { SetDropFeedback(active, point); },
            &dropTarget)) || FAILED(RegisterDragDrop(hwnd_, dropTarget))) {
        if (dropTarget != nullptr) dropTarget->Release();
        return false;
    }
    dropTarget_.Attach(dropTarget);

    engineClient_.Start(
        [this](uint64_t generation) {
            // ReaderLoop must stay dedicated to draining the pipe. Snapshot
            // parsing and owner-window/ListView work can be expensive and
            // must run on the UI thread; doing it inline here can fill the
            // synchronous pipe and prevent the next directory request from
            // ever reaching UiServer.
            uint64_t pending = pendingSnapshotGeneration_.load();
            while (generation > pending &&
                   !pendingSnapshotGeneration_.compare_exchange_weak(pending, generation)) {
            }
            if (!snapshotRefreshPosted_.exchange(true)) {
                const HWND target = hwnd_.load();
                if (target != nullptr) PostMessageW(target, WM_APP_SNAPSHOT_UPDATED, 0, 0);
            }
        },
        [this](EngineConnectionState state, bool privilegedPathActive) {
            columnView_.SetEngineStatus(state, privilegedPathActive);
            const HWND target = hwnd_.load();
            if (target != nullptr) PostMessageW(target, WM_APP_ENGINE_STATUS, static_cast<WPARAM>(state), privilegedPathActive ? 1 : 0);
        },
        [this](const std::wstring& path, ffprotocol::DirectoryErrorReason reason) {
            columnView_.OnDirectoryError(path, reason);
            const HWND target = hwnd_.load();
            if (target != nullptr) PostMessageW(target, WM_APP_REPAINT, 0, 0);
        });

    // overnight-recovery-campaign: EngineClient drops directory requests until
    // Start publishes its running state. Restore the initial path only after
    // Start so a cold launch cannot strand every restored column on
    // "Refreshing…" until the user touches the address bar.
    if (initialLocation.IsHome()) {
        // original-vision-journey-recovery: resolving a typed Home location
        // must also restore its real pane/model/chrome presentation.
        NavigateToLocation(initialLocation);
    } else if (!initialLocation.path.empty()) {
        columnView_.NavigateToPath(initialLocation.path);
        // ui-overhaul phase 3: a deep startup location must reveal its leaf
        // column immediately, exactly like an address/sidebar navigation. The
        // window already owns its initial client size here, so the reveal uses
        // the real viewport instead of leaving the user stranded at the root
        // whenever the path chain is wider than the workspace.
        EnsureColumnVisible(columnView_.FocusedColumnIndex(), NavigationViewportWidth());
    }

    // recovery-degraded-browsing 3.2: the status chip's retry affordance for
    // the Engine unavailable state.
    columnView_.SetRetryEngineCallback([this] { engineClient_.RetryConnect(); });

    // selection-input-hardening: Enter over a focused non-directory opens the
    // item through the same command seam as double-click (ColumnView consumes
    // VK_RETURN before shortcut dispatch, so the shell must supply the open
    // action explicitly).
    columnView_.SetOpenFocusedItemCallback([this] {
        if (ShouldOpenSelectionOnDoubleClick(columnView_.ActiveSelectionItems())) {
            InvokeCommand(L"item.open");
        }
    });

    uiaReady_ = true;
    ShowWindow(hwnd_, showCommand);
    UpdateWindow(hwnd_);
    UpdatePreviewLimitMenuChecks();
    SetTimer(hwnd_, kWorkspaceSaveTimer, 500, nullptr);
    return true;
}

int WindowShell::RunMessageLoop() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        const HWND focused = GetFocus();
        const bool nativeEditFocused = IsNativeTextInput(focused) || IsNativeTextInput(msg.hwnd) ||
            focused == inlineRename_ || msg.hwnd == inlineRename_;
        // Global shortcuts are checked before child-control routing. A
        // successful dispatch consumes this message exactly once, so it can
        // never fall through to a second command or the native edit.
        if (DispatchShortcut(msg, ShortcutScope::Global, nativeEditFocused)) continue;
        if (commandPalette_.HandleMessage(msg)) continue;
        if (settingsDialog_.HandleMessage(msg)) continue;
        // Escape closes the search panel even while keyboard focus sits in the
        // query box or the result list (native child controls would otherwise
        // swallow the key before the main window's WM_KEYDOWN sees it). The
        // command palette and settings dialog above keep precedence.
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE && searchPanel_.Visible()) {
            HideToolWorkspaces();
            RequestRepaint();
            continue;
        }
        // original-vision-journey-recovery 4.6: native child controls do not
        // bubble WM_KEYDOWN to the shell. Preserve modal/search/edit Escape
        // ownership, then route a running operation's cancellation through the
        // same shell handler even when a navigation/preview child has focus.
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE &&
            fileOperations_.HasOutstandingWork() && !nativeEditFocused &&
            msg.hwnd != hwnd_ && IsChild(hwnd_, msg.hwnd)) {
            SendMessageW(hwnd_, WM_KEYDOWN, msg.wParam, msg.lParam);
            continue;
        }
        if (!commandPalette_.Visible() && DispatchShortcut(msg, ShortcutScope::ActiveView, nativeEditFocused)) continue;
        // scan-generation-integrity-and-residual-certification-closure 6.4:
        // the shell is not a dialog and its message loop does not call
        // IsDialogMessage, so forward Tab focus movement to native chrome.
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_TAB && !nativeEditFocused &&
            navigationChrome_.HandleTabNavigation((GetKeyState(VK_SHIFT) & 0x8000) != 0)) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    AppendShutdownPhase(L"message-loop-exit", L"observed");
    navigationWorkspace_.FlushState();
    AppendShutdownPhase(L"engine-client-stop", L"begin");
    engineClient_.Stop();
    AppendShutdownPhase(L"engine-client-stop", L"end");
    fileOperations_.Stop();
    return static_cast<int>(msg.wParam);
}

void WindowShell::RequestRepaint() {
    const HWND target = hwnd_.load();
    if (target != nullptr) InvalidateRect(target, nullptr, FALSE);
}

bool WindowShell::IsSystemDark() const {
    DWORD value = 1, size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value == 0;
}

void WindowShell::ApplyTheme() {
    const bool dark = settings_.theme == ffprotocol::ThemePreference::Dark ||
        (settings_.theme == ffprotocol::ThemePreference::FollowSystem && IsSystemDark());
    ffui::gUiDarkTheme = dark;
    darkTheme_ = dark;
    ffui::gUiDensityProfile = settings_.density == ffprotocol::DensityPreference::Comfortable
        ? ffui::UiDensityProfile::Comfortable
        : ffui::UiDensityProfile::Compact;
    // Theme and device-loss both invalidate device-dependent resources; the
    // MarkDeviceDirty fan-out marks every surface dirty so each recreates its
    // brushes/text-formats lazily in its own paint path. settings-and-appearance
    // 6.2 posture: the transition is non-blocking -- ApplyTheme never waits,
    // input stays responsive throughout; the chrome cross-fade below is the only
    // animation and it is gated on the Windows "Show animations" setting.
    MarkDeviceDirty();
    // Win11 immersive dark mode for the title bar / window chrome; harmless no-op
    // on systems without the attribute.
    if (HMODULE dwmapi = GetModuleHandleW(L"dwmapi.dll")) {
        using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        auto dwmSetAttribute = reinterpret_cast<DwmSetWindowAttributeFn>(GetProcAddress(dwmapi, "DwmSetWindowAttribute"));
        if (dwmSetAttribute) {
            const BOOL useDark = dark ? TRUE : FALSE;
            dwmSetAttribute(hwnd_, 20, &useDark, sizeof(useDark));  // DWMWA_USE_IMMERSIVE_DARK_MODE (20)
            // Win11 rounded corners + Mica backdrop; attribute ids are unsupported
            // on Windows 10, where the HRESULT failure is simply ignored.
            const DWORD cornerPreference = 2;  // DWMWCP_ROUND
            dwmSetAttribute(hwnd_, 33, &cornerPreference, sizeof(cornerPreference));  // DWMWA_WINDOW_CORNER_PREFERENCE (33)
            const DWORD backdropType = 2;      // DWMWA_SYSTEMBACKDROP_TYPE_MICA
            dwmSetAttribute(hwnd_, 38, &backdropType, sizeof(backdropType));  // DWMWA_SYSTEMBACKDROP_TYPE (38)
        }
    }
    // Cross-fade the pre-change frame out over the newly themed scene (~150 ms).
    // Skipped entirely when system animations are off or before the first frame
    // exists (first launch paints the theme directly instead of fading from a
    // blank canvas). ApplyTheme never blocks; the animation timer drives the
    // repaints and stops itself once the fade completes.
    if (ffui::SystemAnimationsEnabled() && frameRendered_) {
        crossFadeActive_ = true;
        crossFadeStartMs_ = GetTickCount64();
        HandleAnimationTimer(true);
    }
    RequestRepaint();
}

void WindowShell::MarkDeviceDirty() {
    // Same dirty-marking as a theme change, using the current theme so no
    // re-theme occurs. Each surface recreates its device-dependent resources
    // lazily in its own paint path (the existing EnsureCreated pattern).
    columnView_.SetDarkTheme(darkTheme_);
    storageAnalysis_.SetDarkTheme(darkTheme_);
    searchPanel_.SetDarkTheme(darkTheme_);
    navigationSidebar_.SetDarkTheme(darkTheme_);
    navigationChrome_.SetDarkTheme(darkTheme_);
    commandPalette_.SetDarkTheme(darkTheme_);
    settingsDialog_.SetDarkTheme(darkTheme_);
    // Details-pane resources are (re)created lazily in RenderDetails; the text
    // formats are device-independent but reset for symmetry with ApplyTheme.
    detailsBrush_.Reset();
    detailsTextBrush_.Reset();
    detailsSecondaryBrush_.Reset();
    detailsTextFormat_.Reset();
    statusLineLayout_.Reset();
    detailsTitleFormat_.Reset();
    previewTextFormat_.Reset();
    // ui-v3 details: the caption format and state-panel brush cache follow the
    // same device/theme reset ladder as the other details resources.
    detailsCaptionFormat_.Reset();
    detailsStateBrushes_.Reset();
    dividerBrush_.Reset();
    progressBrush_.Reset();
    // Preview bitmaps are device-dependent: a bitmap created on the old
    // device cannot be drawn by the recreated context, so drop it and let
    // RenderDetails re-decode from the still-valid pixel snapshot.
    previewBitmap_.Reset();
    // ui-v3 drag/drop: the emphasis brush is device-dependent like the rest.
    dropStrokeBrush_.Reset();
    // Any in-flight cross-fade captured buffers on the old device; cancel it so
    // the next paint renders the current theme directly.
    crossFadeActive_ = false;
    crossFadeBitmap_.Reset();
    // Icon bitmaps are device-dependent; re-resolve them on demand after a
    // device-loss recovery (design D5) rather than reusing stale bitmaps.
    if (iconCache_) {
        iconCache_->Flush();
    }
}

void WindowShell::HandleAnimationTimer(bool ensureRunning) {
    if (ensureRunning) {
        SetTimer(hwnd_, kUiScrollAnimTimer, 16, nullptr);
    } else {
        KillTimer(hwnd_, kUiScrollAnimTimer);
    }
}

void WindowShell::SaveAndNotifySettings() {
    const bool settingsSaved = ffprotocol::SaveSettings(settings_);
    const auto shortcutPath = ffprotocol::ShortcutSettingsPath();
    const bool shortcutsSaved = !shortcutPath.empty() && shortcuts_.Save(shortcutPath);
    // Index-dependent consumers follow the general settings alone; a shortcut
    // save failure must not suppress indexing config reloads, and is surfaced
    // independently instead of silently swallowed.
    if (settingsSaved) {
        storageAnalysis_.SetCategories(settings_.storageCategories);
        engineClient_.ReloadIndexingConfig();
    }
    if (!shortcutsSaved) {
        ShowStatusNotice(L"Settings saved, but keyboard shortcuts could not be persisted.");
    }
}

void WindowShell::ShowStatusNotice(const std::wstring& message) {
    statusNotice_ = message;
    statusNoticeUntilMs_ = GetTickCount64() + 8000;
    RequestRepaint();
}

void WindowShell::SyncCutGhost() {
    // ui-overhaul phase 3: the browse surface ghosts its own cut selection until
    // the move is pasted. An external clipboard publication (different sequence
    // number) supersedes the app-local cut intent, so the ghost clears with it.
    // A copy/cut keyboard command changes no other visible state, so the ghost
    // transition itself must schedule the repaint; otherwise the dimmed rows
    // and dimmed icon would stay stale until the next unrelated invalidation.
    bool ghost = clipboardIsCut_ && !clipboardPaths_.empty();
    if (ghost && clipboardSequenceNumber_ != 0 &&
        GetClipboardSequenceNumber() != clipboardSequenceNumber_) {
        ghost = false;
    }
    const bool changed = ghost ? columnView_.SetCutPaths(clipboardPaths_) : columnView_.SetCutPaths({});
    if (changed) RequestRepaint();
}

void WindowShell::UpdatePreviewLimitMenuChecks() {
    HMENU menu = GetMenu(hwnd_);
    if (menu == nullptr) return;
    const auto checked = [](bool on) { return MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED); };
    CheckMenuItem(menu, kMenuPreviewEnabled, checked(settings_.previewEnabled));
    const unsigned long long limit = settings_.maxAutoPreviewBytes;
    CheckMenuItem(menu, kMenuPreview1Mb, checked(limit == 1ULL * 1024ULL * 1024ULL));
    CheckMenuItem(menu, kMenuPreview16Mb, checked(limit == 16ULL * 1024ULL * 1024ULL));
    CheckMenuItem(menu, kMenuPreview64Mb, checked(limit == 64ULL * 1024ULL * 1024ULL));
}

void WindowShell::Render() {
    RECT clientRect{};
    GetClientRect(hwnd_, &clientRect);
    const float clientWidth = static_cast<float>((std::max)(0L, clientRect.right - clientRect.left));
    const float clientHeight = static_cast<float>((std::max)(0L, clientRect.bottom - clientRect.top));
    const auto shellLayout = CurrentShellLayout();
    const auto& detailsLayout = shellLayout.details;
    D2D1_SIZE_F viewportSize = D2D1::SizeF(
        detailsLayout.availableColumnWidthDip,
        detailsLayout.viewportHeightDip);

    // The eased scroll animations are the authoritative offsets; sync the values
    // the render/hit-test paths read so paints always reflect the eased position.
    // Pane 1's offset lives in ColumnView and is only touched mid-flight so its
    // idle state (tab restore, direct writes) is preserved untouched.
    scrollOffset_ = scrollAnim_.Value();
    if (scrollAnim2_.IsAnimating()) {
        columnView_.SetScrollOffset2(scrollAnim2_.Value());
    }
    const float pane1FrameScroll = columnView_.ScrollOffset2();

    // semantic-uia-core-surfaces 2.5/1.4: push the live geometry the UIA
    // providers convert to screen coordinates. Updated every render (plain
    // floats) so bounds always track the eased scroll offsets and layout.
    {
        UiaViewGeometry geometry;
        geometry.originXDip = shellLayout.contentRect.left;
        geometry.originYDip = shellLayout.contentRect.top;
        geometry.widthDip = detailsLayout.availableColumnWidthDip;
        geometry.heightDip = detailsLayout.viewportHeightDip;
        geometry.dpiScale = CurrentDpiScale();
        geometry.hScroll0Dip = scrollOffset_;
        geometry.hScroll1Dip = pane1FrameScroll;
        geometry.hwnd = hwnd_;
        geometry.keyboardFocus = GetFocus() == hwnd_;
        geometry.revision = ++uiaGeometryRevision_;
        columnView_.SetUiaGeometry(geometry);
    }
    if (storageAnalysis_.Visible() && storageAnalysis_.GetViewMode() == StorageAnalysis::ViewMode::Treemap) {
        const auto storageLayout = ffui::ComputeStorageSurfaceLayout(shellLayout.contentRect);
        const auto& treemapRect = storageLayout.list;
        UiaViewGeometry treemapGeometry;
        treemapGeometry.originXDip = treemapRect.left;
        treemapGeometry.originYDip = treemapRect.top;
        treemapGeometry.widthDip = treemapRect.Width();
        treemapGeometry.heightDip = treemapRect.Height();
        treemapGeometry.dpiScale = CurrentDpiScale();
        treemapGeometry.hwnd = hwnd_;
        treemapGeometry.keyboardFocus = GetFocus() == hwnd_;
        treemapGeometry.revision = uiaGeometryRevision_;
        storageAnalysis_.SetTreemapGeometry(treemapGeometry);
    }

    ID2D1DeviceContext* context = renderer_.BeginFrame();

    // Cross-fade capture: on the first frame after a theme change the swap-chain
    // back buffer still holds the previous frame (the D2D command list is only
    // flushed at EndDraw). Snapshot it before drawing the new theme so the fade
    // can composite the old frame over the new one. Any failure degrades to an
    // instant theme switch (the fade simply does not run).
    if (crossFadeActive_ && !crossFadeBitmap_) {
        if (SUCCEEDED(context->CreateBitmap(
                D2D1::SizeU(static_cast<UINT32>(clientWidth), static_cast<UINT32>(clientHeight)), nullptr, 0,
                D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
                &crossFadeBitmap_))) {
            if (FAILED(crossFadeBitmap_->CopyFromRenderTarget(nullptr, context, nullptr))) {
                crossFadeBitmap_.Reset();
                crossFadeActive_ = false;
            }
        } else {
            crossFadeBitmap_.Reset();
            crossFadeActive_ = false;
        }
    }


    context->SetTransform(D2D1::Matrix3x2F::Translation(shellLayout.contentRect.left,
                                                        shellLayout.contentRect.top));
    if (galleryMode_) {
        RECT gClient{};
        GetClientRect(hwnd_, &gClient);
        const D2D1_RECT_F galleryRect = D2D1::RectF(0.0f, 0.0f,
            static_cast<float>((std::max)(0L, gClient.right - gClient.left)),
            static_cast<float>((std::max)(0L, gClient.bottom - gClient.top)));
        if (!detailsTextBrush_) {
            context->CreateSolidColorBrush(ffui::GetUiTheme(ffui::gUiDarkTheme).text, &detailsTextBrush_);
        }
        if (!detailsBrush_) {
            context->CreateSolidColorBrush(ffui::GetUiTheme(ffui::gUiDarkTheme).textSecondary, &detailsBrush_);
        }
        if (!detailsTextFormat_) {
            renderer_.DWriteFactory()->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                ffui::UiTypeSize(ffui::UiTypeRole::Body), L"en-us", &detailsTextFormat_);
        }
        if (!previewTextFormat_) {
            renderer_.DWriteFactory()->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                ffui::UiTypeSize(ffui::UiTypeRole::Subtitle), L"en-us", &previewTextFormat_);
        }
        if (!detailsCaptionFormat_) {
            renderer_.DWriteFactory()->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                ffui::UiTypeSize(ffui::UiTypeRole::Caption), L"en-us", &detailsCaptionFormat_);
        }
        ffui::RenderGallery(context, detailsTextBrush_.Get(), detailsBrush_.Get(),
            previewTextFormat_.Get(), detailsTextFormat_.Get(), detailsCaptionFormat_.Get(),
            galleryRect, ffui::gUiDarkTheme, CurrentDpiScale());
    } else if (storageAnalysis_.Visible() && storageAnalysis_.GetViewMode() == StorageAnalysis::ViewMode::Treemap) {
        storageAnalysis_.RenderTreemap(context, renderer_.DWriteFactory(), shellLayout.contentRect);
    } else if (!columnView_.BrowseContentActive()) {
        // ui-v3 hardening V3-01: search/storage own the workspace through native
        // child controls separated by inset gutters. Rendering browse pixels
        // beneath them leaked stale columns/headings through every gap between
        // those controls (left gutter, row bands, right sliver, bottom strip).
        // The alternate workspace surface paints nothing but the theme
        // background; Clear covers the full target regardless of transform.
        context->Clear(ffui::GetUiTheme(ffui::gUiDarkTheme).background);
    } else {
        columnView_.Render(context, renderer_.DWriteFactory(), viewportSize, scrollOffset_, pane1FrameScroll);
    }
    // ui-v3 drag/drop: destination emphasis paints over the browse surface but
    // under the details card, so a drag over the workspace reads as an accent
    // strip on the target pane split without obscuring file rows.
    DrawDropTargetFeedback(context, shellLayout);
    // Search and Storage own the alternate workspace surface, including its
    // status/control region.  Do not leave the browse Details/status layer
    // underneath their native children; it would create a stale overlapping
    // strip at the bottom of their lists.
    // quick-access-home 5.4: Home has no selection, so the details card must
    // not draw over it. ui-overhaul phase 12: the debug component gallery owns
    // the whole workspace and must not be overlaid by the details surface.
    if (!searchPanel_.Visible() && !storageAnalysis_.Visible() && !galleryMode_ &&
        !navigationWorkspace_.ActiveContext().isHomeLocation) {
        RenderDetails(context, detailsLayout);
    }
    context->SetTransform(D2D1::Matrix3x2F::Identity());

    // Cross-fade composite: draw the captured previous frame over the freshly
    // themed scene, fading it out over ~150 ms.
    if (crossFadeActive_ && crossFadeBitmap_) {
        const float t = std::clamp(static_cast<float>(GetTickCount64() - crossFadeStartMs_) / kUiAnimationDefaultMs, 0.0f, 1.0f);
        if (t >= 1.0f) {
            crossFadeActive_ = false;
            crossFadeBitmap_.Reset();
        } else {
            context->DrawBitmap(crossFadeBitmap_.Get(),
                D2D1::RectF(0.0f, 0.0f, clientWidth, clientHeight),
                1.0f - t, D2D1_INTERPOLATION_MODE_LINEAR);
        }
    }

    renderer_.EndFrame();
    frameRendered_ = true;

    // Device-loss fan-out: the renderer recreated its swap chain / target
    // bitmap during this frame; mark every surface dirty so the next paint
    // recreates device-dependent resources lazily (the same fan-out as a theme
    // change, without re-theming).
    if (renderer_.ConsumeDeviceRecreated()) {
        MarkDeviceDirty();
        RequestRepaint();
    }
}

void WindowShell::RefreshSelectionPresentation() {
    previewBitmap_.Reset();
    attributesCachePath_.clear();
    attributesCacheValid_ = false;
    const auto selection = columnView_.CurrentSelection();
    // Details aggregates are keyed by folder identity, but a cleared/file
    // selection must also invalidate the request so reselecting that folder
    // cannot reuse an old result after the selection context changed.
    if (!selection || !selection->isDirectory) {
        aggregateRequest_.Invalidate();
    }
    const bool canPreview = settings_.previewEnabled && selection && !selection->isDirectory &&
                            selection->sizeBytes <= settings_.maxAutoPreviewBytes;
    {
        std::lock_guard<std::mutex> lock(previewMutex_);
        preview_ = nullptr;
        ++activePreviewRequest_;
    }
    // Do not hold previewMutex_ while cancelling or queueing work. A tiny text
    // file can complete immediately on a worker; keeping the lock across
    // Request would make that otherwise-fast completion wait behind the UI
    // thread and amplify selection churn into visible loading.
    if (!canPreview) {
        if (!settings_.previewEnabled) {
            columnView_.SetUiaDetailsStatus(L"Preview: Disabled");
        } else if (!selection) {
            columnView_.SetUiaDetailsStatus(L"Preview: No selection");
        } else if (selection->isDirectory) {
            columnView_.SetUiaDetailsStatus(L"Preview: Select a file");
        } else {
            columnView_.SetUiaDetailsStatus(L"Preview: Too large");
        }
        previewController_.Clear();
        return;
    }
    columnView_.SetUiaDetailsStatus(L"Preview: Loading");
    RequestFileType(selection->path);
    // Forward the configured Preview Limit so text truncation follows the
    // advertised control, not just the auto-preview gate.
    FileDescriptor descriptor = *selection;
    descriptor.maxBytes = settings_.maxAutoPreviewBytes;
    previewController_.Request(descriptor);
}

void WindowShell::RequestFileType(const std::wstring& path) {
    if (!AdmitAsyncWork(fileTypeWork_, kMaxInFlightFileTypeLookups)) return;
    {
        std::lock_guard<std::mutex> lock(fileTypeMutex_);
        if (fileTypes_.contains(path)) return;
        // Bound the cache: a long session browsing many unique paths must not
        // grow it without limit. Entries are cheap to re-resolve, so a plain
        // clear at the cap is sufficient (no LRU bookkeeping).
        if (fileTypes_.size() >= kMaxFileTypeCacheEntries) {
            fileTypes_.clear();
        }
        fileTypes_.emplace(path, L"");
    }
    fileTypeWork_.push_back(std::async(std::launch::async, [this, path] {
        SHFILEINFOW info{};
        std::wstring description;
        if (SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), SHGFI_TYPENAME) != 0 && info.szTypeName[0] != L'\0') {
            description = info.szTypeName;
        }
        {
            std::lock_guard<std::mutex> lock(fileTypeMutex_);
            fileTypes_[path] = std::move(description);
        }
        const HWND target = hwnd_.load();
        if (target != nullptr) PostMessageW(target, WM_APP_REPAINT, 0, 0);
    }).share());
}

std::wstring WindowShell::FileTypeFor(const std::wstring& path) const {
    const std::wstring extension = ExtensionOf(path);
    const std::wstring fallback = extension.empty() ? std::wstring(L"File") : extension + L" File";
    std::lock_guard<std::mutex> lock(fileTypeMutex_);
    const auto found = fileTypes_.find(path);
    return found == fileTypes_.end() || found->second.empty() ? fallback : found->second;
}

// ui-v3 drag/drop: presentation-only feedback state from the IDropTarget
// sink. Converts the screen drop point to content-space DIP exactly like the
// destination provider so the emphasis lands on the pane the drop would use.
void WindowShell::SetDropFeedback(bool active, POINTL point) {
    if (!active) {
        if (!dropFeedbackActive_) return;
        dropFeedbackActive_ = false;
        dropFeedbackValid_ = false;
        RequestRepaint();
        return;
    }
    const auto shellLayout = CurrentShellLayout();
    const float scale = CurrentDpiScale();
    POINT clientOrigin{};
    ClientToScreen(hwnd_, &clientOrigin);
    const float clientX =
        DropPointClientXDip(point, clientOrigin, scale, shellLayout.contentRect.left);
    const float viewportWidth = shellLayout.details.availableColumnWidthDip;
    dropFeedbackXDip = clientX;
    dropFeedbackValid_ = clientX >= 0.0f && clientX < viewportWidth;
    dropFeedbackActive_ = true;
    RequestRepaint();
}

// ui-v3 drag/drop: canonical destination affordance using the shared
// dropTargetStroke token (shape + stroke redundancy; High Contrast falls back
// to the system highlight). Dual-pane draws a strip on the pane split at the
// drop point's x; single-pane outlines the whole column viewport. Shape
// decisions come from the headless DropFeedbackLayout seam so they stay
// unit-tested without a renderer.
void WindowShell::DrawDropTargetFeedback(ID2D1DeviceContext* context,
                                         const ffui::MainShellLayout& shellLayout) {
    if (!dropFeedbackActive_ || !dropFeedbackValid_ || context == nullptr) {
        return;
    }
    ffui::DropFeedbackInput input;
    input.shellLayout = shellLayout;
    input.dropXDip = dropFeedbackXDip;
    input.activePaneCount = navigationWorkspace_.ActivePaneCount();
    input.active = true;
    input.valid = true;
    const ffui::DropFeedbackGeometry shape = ffui::ComputeDropFeedbackGeometry(input);
    if (shape.kind == ffui::DropFeedbackKind::None) {
        return;
    }
    const ffui::UiTheme theme = GetUiTheme(darkTheme_);
    D2D1_COLOR_F strokeColor =
        ffui::UiSystemHighContrast() ? ffui::ToD2DColor(GetSysColor(COLOR_HIGHLIGHT))
                                     : theme.dropTargetStroke;
    if (FAILED(ffui::UiEnsureSolidBrush(context, strokeColor, &dropStrokeBrush_))) {
        return;
    }
    if (shape.kind == ffui::DropFeedbackKind::PaneSplitStrip) {
        context->FillRectangle(
            D2D1::RectF(shape.left, shape.top, shape.right, shape.bottom),
            dropStrokeBrush_.Get());
    } else {
        ffui::UiDrawRoundedRect(context,
                                D2D1::RectF(shape.left, shape.top, shape.right, shape.bottom),
                                dropStrokeBrush_.Get(), shape.cornerRadius,
                                shape.strokeWidth);
    }
}

void WindowShell::RenderDetails(ID2D1DeviceContext* context, const ffui::DetailsLayout& layout) {
    constexpr float kStatusHeight = 26.0f;
    constexpr float kProgressHeight = 3.0f;
    const float sceneWidth = layout.viewportWidthDip;
    const float sceneHeight = layout.viewportHeightDip;
    const bool drawFullPane = layout.paneVisible && !layout.paneCollapsed &&
                              layout.detailsRect.right > layout.detailsRect.left;
    const float left = layout.detailsRect.left;
    // Elevated details card: the pane floats ~8 DIP inside the viewport as a
    // rounded surfaceElevated card; the status bar stays a full-width strip
    // below it, separated by a subtle divider.
    const float inset = ffui::UiMetrics::kSpaceS;
    const float statusTop = sceneHeight - kStatusHeight;
    const float cardLeft = left + inset;
    const float cardRight = layout.detailsRect.right - inset;
    const float cardTop = ColumnView::kBadgeHeight + inset;
    const float cardBottom = statusTop - inset;
    const float pad = inset;
    const ffui::UiTheme theme = ffui::GetUiTheme(darkTheme_);
    const bool highContrast = ffui::UiSystemHighContrast();
    // Text formats are device-independent; recreate only after ApplyTheme reset
    // them (brushes are handled by UiEnsureSolidBrush below, which also tracks
    // theme/device changes without an explicit reset).
    if (!detailsTextFormat_) {
        renderer_.DWriteFactory()->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, ffui::UiTypeSize(ffui::UiTypeRole::Body),
            L"en-us", &detailsTextFormat_);
        renderer_.DWriteFactory()->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-us", &previewTextFormat_);
        previewTextFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        // ui-v3 details: caption-size format for preview-state explanations.
        renderer_.DWriteFactory()->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, ffui::UiMetrics::kFontSizeCaption, L"en-us", &detailsCaptionFormat_);
        // ui-overhaul phase 7: the selected item name is the pane's title and
        // must outrank every metadata row.
        renderer_.DWriteFactory()->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            ffui::UiTypeSize(ffui::UiTypeRole::Subtitle), L"en-us", &detailsTitleFormat_);
    }
    if (highContrast) {
        // High contrast: suppress the rounded-token styling and fall back to
        // system colors so the user's accessibility palette is never overridden.
        ffui::UiEnsureSolidBrush(context, ffui::ToD2DColor(GetSysColor(COLOR_WINDOW)), &detailsBrush_);
        ffui::UiEnsureSolidBrush(context, ffui::ToD2DColor(GetSysColor(COLOR_WINDOWTEXT)), &detailsTextBrush_);
        ffui::UiEnsureSolidBrush(context, ffui::ToD2DColor(GetSysColor(COLOR_WINDOWTEXT)), &detailsSecondaryBrush_);
    } else {
        ffui::UiEnsureSolidBrush(context, theme.surfaceElevated, &detailsBrush_);
        ffui::UiEnsureSolidBrush(context, theme.text, &detailsTextBrush_);
        ffui::UiEnsureSolidBrush(context, theme.textSecondary, &detailsSecondaryBrush_);
        ffui::UiEnsureSolidBrush(context, theme.dividerSubtle, &dividerBrush_);
    }
    if (drawFullPane) {
        if (highContrast) {
            context->FillRectangle(D2D1::RectF(cardLeft, cardTop, cardRight, cardBottom), detailsBrush_.Get());
        } else {
            ffui::UiFillRoundedRect(context, D2D1::RectF(cardLeft, cardTop, cardRight, cardBottom), detailsBrush_.Get(), ffui::UiMetrics::kRadiusMedium);
        }
    } else if (layout.paneCollapsed) {
        context->FillRectangle(D2D1::RectF(layout.detailsRect.left, 0.0f,
                                           layout.detailsRect.right, sceneHeight), detailsBrush_.Get());
        // ui-overhaul phase 7: the rail is a real disclosure control. A
        // trailing chevron marks it as interactive; clicking it collapses the
        // details surface so the workspace reclaims the rail width (the
        // View > Toggle Details Pane command brings it back).
        const D2D1_RECT_F rail = D2D1::RectF(layout.detailsRect.left, cardTop,
                                             layout.detailsRect.right, cardBottom);
        ffui::UiDrawTextCentered(context, detailsCaptionFormat_.Get(), L"\u203A", rail,
                                 highContrast ? detailsTextBrush_.Get() : detailsSecondaryBrush_.Get());
    }
    const SelectionSummary summary = columnView_.CurrentSelectionSummary();
    const bool singleSelection = summary.items.size() == 1;
    const auto selection = singleSelection ? std::optional<FileDescriptor>(summary.items.front()) : std::nullopt;
    const bool previewAllowed = summary.items.size() == 1;
    if (drawFullPane) {
        // ui-overhaul phase 7: the details pane is an information hierarchy, not
        // a property dump. Item name (title) > type/size summary > the 4-6
        // highest-value metadata rows > path/attributes as quiet secondary
        // text. Every row is painted with the semantic text/caption formats so
        // the eye lands on identification first.
        ID2D1SolidColorBrush* primary = detailsTextBrush_.Get();
        ID2D1SolidColorBrush* secondary =
            detailsSecondaryBrush_ != nullptr ? detailsSecondaryBrush_.Get() : detailsTextBrush_.Get();
        IDWriteTextFormat* const titleFormat =
            detailsTitleFormat_ != nullptr ? detailsTitleFormat_.Get() : detailsTextFormat_.Get();
        const float contentLeft = cardLeft + pad;
        const float contentRight = cardRight - pad;
        const float contentWidth = (std::max)(0.0f, contentRight - contentLeft);
        const float captionHeight = ffui::UiMetrics::kFontSizeCaption * 1.5f;
        const float rowHeight = 20.0f;
        const float labelWidth = (std::min)(92.0f, contentWidth * 0.42f);
        float y = cardTop + pad;

        if (summary.items.empty()) {
            // native-precision-workspace: keep the no-selection prompt near
            // the top of the details card so it stays concise instead of
            // turning a quiet state into a centered empty card.
            const float stateBottom = (std::min)(cardBottom - pad, cardTop + pad + 96.0f);
            ffui::DrawUiStatePanel(
                context, D2D1::RectF(contentLeft, cardTop + pad, contentRight, stateBottom),
                titleFormat, L"Nothing selected", detailsCaptionFormat_.Get(),
                L"Select a file or folder to see its details and preview.", theme, highContrast,
                &detailsStateBrushes_);
        } else if (!singleSelection) {
            // Multi-selection aggregate: count + total size. No preview promise
            // beyond an honest one-line explanation.
            const std::wstring title = std::to_wstring(summary.items.size()) + L" items selected";
            context->DrawTextW(title.c_str(), static_cast<UINT32>(title.size()), titleFormat,
                               D2D1::RectF(contentLeft, y, contentRight, y + 24.0f), primary);
            y += 28.0f;
            const auto drawRow = [&](const wchar_t* label, const std::wstring& value) {
                context->DrawTextW(label, static_cast<UINT32>(wcslen(label)), detailsCaptionFormat_.Get(),
                                   D2D1::RectF(contentLeft, y, contentLeft + labelWidth, y + rowHeight),
                                   secondary);
                context->DrawTextW(value.c_str(), static_cast<UINT32>(value.size()), detailsTextFormat_.Get(),
                                   D2D1::RectF(contentLeft + labelWidth, y, contentRight, y + rowHeight),
                                   primary);
                y += rowHeight;
            };
            drawRow(L"Total size", ffui::FormatSize(summary.knownSizeBytes));
            y += 6.0f;
            const wchar_t* hint = L"Preview is available for a single selection.";
            context->DrawTextW(hint, static_cast<UINT32>(wcslen(hint)), detailsCaptionFormat_.Get(),
                               D2D1::RectF(contentLeft, y, contentRight, y + captionHeight * 2.0f), secondary);
            y += captionHeight * 2.0f;
        } else {
            const std::wstring name = FileNameOf(selection->path);
            // Title: one bounded, character-trimmed layout per paint.
            const float nameHeight = 24.0f;
            Microsoft::WRL::ComPtr<IDWriteTextLayout> nameLayout;
            if (titleFormat != nullptr && contentWidth > 0.0f &&
                SUCCEEDED(renderer_.DWriteFactory()->CreateTextLayout(
                    name.c_str(), static_cast<UINT32>(name.size()), titleFormat, contentWidth,
                    nameHeight, &nameLayout)) &&
                nameLayout != nullptr) {
                // ui-overhaul phase 7: the layout needs the factory's ellipsis
                // trimming sign; a bare DWRITE_TRIMMING delimiter clips silently.
                Microsoft::WRL::ComPtr<IDWriteInlineObject> ellipsis;
                const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
                if (SUCCEEDED(renderer_.DWriteFactory()->CreateEllipsisTrimmingSign(nameLayout.Get(), &ellipsis)) &&
                    SUCCEEDED(nameLayout->SetTrimming(&trimming, ellipsis.Get()))) {
                    context->DrawTextLayout(D2D1::Point2F(contentLeft, y), nameLayout.Get(), primary,
                                            D2D1_DRAW_TEXT_OPTIONS_CLIP);
                }
            }
            y += nameHeight + 2.0f;

            // Type + size summary: the one line a user reads before the grid.
            std::wstring summaryLine = selection->isDirectory ? std::wstring(L"Folder")
                                                              : FileTypeFor(selection->path);
            if (!selection->isDirectory) {
                summaryLine += L"  \u2022  " + ffui::FormatSize(selection->sizeBytes);
            }
            context->DrawTextW(summaryLine.c_str(), static_cast<UINT32>(summaryLine.size()),
                               detailsCaptionFormat_.Get(),
                               D2D1::RectF(contentLeft, y, contentRight, y + captionHeight), secondary);
            y += captionHeight + 6.0f;
            if (!highContrast) {
                context->DrawLine(D2D1::Point2F(contentLeft, y), D2D1::Point2F(contentRight, y),
                                  dividerBrush_.Get(), 1.0f);
            }
            y += 8.0f;

            const auto drawRow = [&](const wchar_t* label, const std::wstring& value) {
                context->DrawTextW(label, static_cast<UINT32>(wcslen(label)), detailsCaptionFormat_.Get(),
                                   D2D1::RectF(contentLeft, y, contentLeft + labelWidth, y + rowHeight),
                                   secondary);
                context->DrawTextW(value.c_str(), static_cast<UINT32>(value.size()), detailsTextFormat_.Get(),
                                   D2D1::RectF(contentLeft + labelWidth, y, contentRight, y + rowHeight),
                                   primary);
                y += rowHeight;
            };

            // Attributes are fetched once per selection change, never per paint
            // frame: a blocking GetFileAttributesExW on an unreachable network
            // share would otherwise freeze the UI on every repaint.
            if (!attributesCacheValid_ || attributesCachePath_ != selection->path) {
                attributesCachePath_ = selection->path;
                attributesCacheValid_ =
                    GetFileAttributesExW(selection->path.c_str(), GetFileExInfoStandard, &attributesCache_) != FALSE;
            }
            const bool haveAttributes = attributesCacheValid_;

            if (selection->isDirectory) {
                const ffui::FolderAggregateKey aggregateKey{
                    static_cast<int64_t>(selection->volumeRowId), selection->fileIdLow, selection->fileIdHigh};
                if (aggregateRequest_.NeedsRequest(aggregateKey)) {
                    aggregateRequest_.Begin(aggregateKey);
                    uint64_t requestId = 0;
                    pendingAggregateStatus_ = ffprotocol::FolderAggregateStatus::Pending;
                    pendingAggregateItemCount_ = 0;
                    pendingAggregateTotalSize_ = 0;
                    pendingAggregateAllocatedSize_ = 0;
                    pendingAggregateAllocatedKnownCount_ = 0;
                    pendingAggregateAllocatedTotalCount_ = 0;
                    requestId = engineClient_.RequestFolderAggregate(
                        static_cast<int64_t>(selection->volumeRowId),
                        selection->fileIdLow, selection->fileIdHigh,
                        [this](uint64_t engineRequestId, ffprotocol::FolderAggregateStatus status, uint64_t itemCount, uint64_t totalSizeBytes,
                               uint64_t totalAllocatedSizeBytes, uint64_t allocatedBytesKnownCount, uint64_t allocatedBytesTotalCount) {
                            auto payload = std::make_unique<ffprotocol::FolderAggregateResultV2Payload>(
                                ffprotocol::FolderAggregateResultV2Payload{engineRequestId, status, itemCount, totalSizeBytes,
                                                                           totalAllocatedSizeBytes, allocatedBytesKnownCount,
                                                                           allocatedBytesTotalCount});
                            const HWND target = hwnd_.load();
                            if (target != nullptr
                                && PostMessageW(target, WM_APP_FOLDER_AGGREGATE, 0,
                                                reinterpret_cast<LPARAM>(payload.get()))) {
                                payload.release();
                            }
                        });
                    aggregateRequest_.SetRequestId(requestId);
                }
                if (pendingAggregateStatus_ == ffprotocol::FolderAggregateStatus::Pending) {
                    drawRow(L"Items", L"Calculating\u2026");
                    drawRow(L"Total size", L"Calculating\u2026");
                } else if (pendingAggregateStatus_ == ffprotocol::FolderAggregateStatus::Resolved) {
                    drawRow(L"Items", std::to_wstring(pendingAggregateItemCount_));
                    drawRow(L"Total size", ffui::FormatSize(pendingAggregateTotalSize_));
                    // allocated-size-storage-accuracy 6.3: size-on-disk is shown
                    // when known; unknown stays honestly absent, never 0 B.
                    if (pendingAggregateAllocatedKnownCount_ > 0) {
                        drawRow(L"Size on disk", ffui::FormatSize(pendingAggregateAllocatedSize_));
                    }
                } else {
                    drawRow(L"Items", L"\u2014");
                    drawRow(L"Total size", L"\u2014");
                }
            } else {
                drawRow(L"Size", ffui::FormatSize(selection->sizeBytes));
                if (selection->allocatedSizeKnown) {
                    drawRow(L"Size on disk", ffui::FormatSize(selection->allocatedSizeBytes));
                }
            }
            // A failed attribute fetch renders em-dashes (FormatTime maps a
            // zero FILETIME to "—"), never epoch-zero 1601 timestamps.
            drawRow(L"Modified", haveAttributes ? FormatTime(attributesCache_.ftLastWriteTime) : L"\u2014");
            drawRow(L"Created", haveAttributes ? FormatTime(attributesCache_.ftCreationTime) : L"\u2014");
            if (!selection->isDirectory) {
                drawRow(L"Accessed", haveAttributes ? FormatTime(attributesCache_.ftLastAccessTime) : L"\u2014");
            }

            // Secondary block: the path (wrapped, bounded to three caption
            // lines) then extension/attributes. Progressively disclosed and
            // deliberately quieter than the grid above.
            y += 4.0f;
            const std::wstring displayPath = ffui::NormalizeDisplayPath(selection->path);
            float pathHeight = captionHeight;
            Microsoft::WRL::ComPtr<IDWriteTextLayout> pathLayout;
            if (detailsCaptionFormat_ != nullptr && contentWidth > 0.0f &&
                SUCCEEDED(renderer_.DWriteFactory()->CreateTextLayout(
                    displayPath.c_str(), static_cast<UINT32>(displayPath.size()),
                    detailsCaptionFormat_.Get(), contentWidth, captionHeight * 3.0f, &pathLayout)) &&
                pathLayout != nullptr) {
                Microsoft::WRL::ComPtr<IDWriteInlineObject> ellipsis;
                const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
                if (SUCCEEDED(renderer_.DWriteFactory()->CreateEllipsisTrimmingSign(pathLayout.Get(), &ellipsis)) &&
                    SUCCEEDED(pathLayout->SetTrimming(&trimming, ellipsis.Get()))) {
                    DWRITE_TEXT_METRICS pathMetrics{};
                    if (SUCCEEDED(pathLayout->GetMetrics(&pathMetrics))) {
                        pathHeight = (std::min)(pathMetrics.height, captionHeight * 3.0f);
                    }
                    context->DrawTextLayout(D2D1::Point2F(contentLeft, y), pathLayout.Get(), secondary,
                                            D2D1_DRAW_TEXT_OPTIONS_CLIP);
                }
            }
            y += pathHeight + 4.0f;
            const std::wstring extension = ExtensionOf(selection->path);
            std::wstring advanced = L"Extension: " + (extension.empty() ? std::wstring(L"(none)") : extension);
            advanced += L"  \u2022  Attributes: " + FormatAttributes(selection->attributes);
            context->DrawTextW(advanced.c_str(), static_cast<UINT32>(advanced.size()), detailsCaptionFormat_.Get(),
                               D2D1::RectF(contentLeft, y, contentRight, y + captionHeight * 2.0f), secondary);
            y += captionHeight * 2.0f;
        }

        // Preview occupies whatever remains beneath the metadata block; it never
        // pushes the metadata off the card.
        const float previewTop = (std::min)(cardBottom - pad, (std::max)(y + 6.0f, cardTop + 150.0f));
        const float previewBottom = cardBottom - pad;
        // ui-v3 details: non-Ready previews render through the shared state panel
        // (centered title + one-line explanation) instead of flat corner text —
        // deliberate states, never unexplained empty boxes.
        const auto drawPreviewStatePanel = [&](ffui::PreviewState state) {
            if (previewTop >= previewBottom - 8.0f || !detailsTextFormat_ || !detailsCaptionFormat_) {
                return;
            }
            const ffui::PreviewStatePresentation presentation =
                ffui::ResolvePreviewStatePresentation(state);
            if (presentation.title == nullptr) {
                return;
            }
            ffui::DrawUiStatePanel(context,
                                   D2D1::RectF(cardLeft + pad, previewTop, cardRight - pad, previewBottom),
                                   detailsTextFormat_.Get(), presentation.title,
                                   detailsCaptionFormat_.Get(), presentation.message, theme,
                                   highContrast, &detailsStateBrushes_);
        };
        if (!previewAllowed || previewTop >= previewBottom - 8.0f) {
            // Empty/multi selection has no preview surface; the aggregate block
            // above already fills the pane, so nothing more is drawn.
        } else {
            std::shared_ptr<const PreviewResult> preview;
            { std::lock_guard<std::mutex> lock(previewMutex_); preview = preview_; }
            if (!settings_.previewEnabled) {
                drawPreviewStatePanel(ffui::PreviewState::Disabled);
            } else if (preview && preview->kind == PreviewKind::Image) {
                if (!previewBitmap_ && !preview->pixels.empty()) {
                    const D2D1_BITMAP_PROPERTIES properties = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
                    context->CreateBitmap(D2D1::SizeU(preview->width, preview->height), preview->pixels.data(), preview->width * 4, &properties, &previewBitmap_);
                }
                if (previewBitmap_) {
                    // Letterbox: uniform scale, centered -- never stretch to the
                    // pane rect (which would distort portraits/panoramas).
                    const D2D1_RECT_F dest = ffui::ComputeLetterboxRect(
                        D2D1::RectF(cardLeft + pad, previewTop, cardRight - pad, previewBottom),
                        static_cast<float>(preview->width), static_cast<float>(preview->height));
                    context->DrawBitmap(previewBitmap_.Get(), dest);
                }
            } else if (preview && preview->kind == PreviewKind::Text) {
                std::wstring text = preview->text;
                if (preview->truncated) text += L"\n\n[Preview truncated]";
                context->DrawText(text.c_str(), static_cast<UINT32>(text.size()), previewTextFormat_.Get(),
                    D2D1::RectF(cardLeft + pad, previewTop, cardRight - pad, previewBottom), detailsTextBrush_.Get());
            } else {
                // No content: resolve the sharpest known cause. A
                // provider-supplied failure wins; a pending request shows
                // Loading; folders legitimately have no preview and stay silent
                // rather than nagging.
                bool haveState = true;
                ffui::PreviewState state = ffui::PreviewState::Unsupported;
                if (preview) {
                    state = preview->state;
                } else if (selection && !selection->isDirectory) {
                    state = ffui::PreviewState::Loading;
                } else {
                    haveState = false;
                }
                if (haveState) {
                    drawPreviewStatePanel(state);
                }
            }
        }
    }
    const std::wstring status = L"Selected: " + std::to_wstring(summary.items.size()) + L"  \u2022  " +
        ffui::FormatSize(summary.knownSizeBytes) + L"  \u2022  " + columnView_.CurrentPath();
    // Status bar: full-width strip under the card, separated by a subtle
    // divider and carrying the slim file-operation progress bar (the primary
    // progress surface; the window title keeps its textual progress too).
    context->FillRectangle(D2D1::RectF(0, statusTop, sceneWidth, sceneHeight), detailsBrush_.Get());
    if (!highContrast) {
        context->DrawLine(D2D1::Point2F(0, statusTop), D2D1::Point2F(sceneWidth, statusTop), dividerBrush_.Get(), 1.0f);
    }
    if (fileOpInProgress_) {
        if (fileOpProgress_ > 0.001f) {
            const float progressWidth = sceneWidth * std::clamp(fileOpProgress_, 0.0f, 1.0f);
            const D2D1_RECT_F progressRect = D2D1::RectF(0, statusTop + kStatusHeight - kProgressHeight, progressWidth, statusTop + kStatusHeight);
            if (highContrast) {
                ffui::UiEnsureSolidBrush(context, ffui::ToD2DColor(GetSysColor(COLOR_HIGHLIGHT)), &progressBrush_);
                context->FillRectangle(progressRect, progressBrush_.Get());
            } else {
                ffui::UiEnsureSolidBrush(context, theme.accent, &progressBrush_);
                ffui::UiFillRoundedRect(context, progressRect, progressBrush_.Get(), ffui::UiMetrics::kRadiusSmall);
            }
        } else {
            // ui-overhaul phase 8: an operation that has not reported a
            // percentage yet (queued, scanning, first item) shows the canonical
            // indeterminate band instead of an invisible zero-width bar. The
            // phase advances as the worker's progress events repaint.
            const float phase = static_cast<float>((GetTickCount64() % 1200) / 1200.0);
            ffui::DrawUiProgressSurface(
                context,
                D2D1::RectF(0, statusTop + kStatusHeight - kProgressHeight, sceneWidth, statusTop + kStatusHeight),
                -1.0f, phase, theme, highContrast, &detailsStateBrushes_);
        }
    }
    // Transient non-modal notice (copy failures, degraded-mode explanation)
    // rides the status line until it expires.
    std::wstring statusText = status;
    if (!statusNotice_.empty() && GetTickCount64() < statusNoticeUntilMs_) {
        // Keep recovery/completion feedback ahead of a potentially long path.
        statusText = statusNotice_ + L"  \u2022  " + status;
    }
    const D2D1_RECT_F statusRect = D2D1::RectF(8, statusTop, sceneWidth - 8, sceneHeight);
    const float statusWidth = statusRect.right - statusRect.left;
    if (statusLineLayout_ == nullptr || statusLineText_ != statusText || statusLineWidth_ != statusWidth) {
        UiCreateSingleLineTextLayout(renderer_.DWriteFactory(), detailsTextFormat_.Get(), statusText.c_str(),
            static_cast<UINT32>(statusText.size()), statusWidth, statusRect.bottom - statusRect.top,
            &statusLineLayout_);
        statusLineText_ = statusText;
        statusLineWidth_ = statusWidth;
    }
    if (statusLineLayout_) {
        context->DrawTextLayout(D2D1::Point2F(statusRect.left, statusRect.top), statusLineLayout_.Get(),
            detailsTextBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
}


void WindowShell::EnsureColumnVisible(int columnIndex, float viewportWidth) {
    EnsurePaneColumnVisible(columnView_.ActivePane(), columnIndex, viewportWidth);
}

void WindowShell::EnsurePaneColumnVisible(int pane, int columnIndex, float viewportWidth) {
    if (viewportWidth <= 0.0f || columnIndex < 0 || pane < 0 ||
        pane >= (columnView_.IsDualPane() ? 2 : 1) || columnView_.IsPaneHome(pane)) return;
    const uint64_t now = GetTickCount64();
    if (columnView_.IsDualPane()) {
        const float paneWidth = viewportWidth / 2.0f;
        const float paneScroll = pane == 0 ? scrollAnim_.Value()
            : (scrollAnim2_.IsAnimating() ? scrollAnim2_.Value() : columnView_.ScrollOffset2());
        const float target = ColumnRevealScrollOffset(columnIndex, ColumnView::kColumnWidth,
            columnView_.PaneContentWidth(pane), paneWidth, paneScroll);
        if (pane == 0) scrollAnim_.AnimateTo(target, kUiAnimationDefaultMs, now);
        else {
            if (!scrollAnim2_.IsAnimating()) scrollAnim2_.Snap(columnView_.ScrollOffset2());
            scrollAnim2_.AnimateTo(target, kUiAnimationDefaultMs, now);
        }
    } else {
        const float target = ColumnRevealScrollOffset(columnIndex, ColumnView::kColumnWidth,
            columnView_.ContentWidth(), viewportWidth, scrollAnim_.Value());
        scrollAnim_.AnimateTo(target, kUiAnimationDefaultMs, now);
    }
    HandleAnimationTimer(true);
}

void WindowShell::ReconcileBrowseInteraction(bool revealFocusedColumn) {
    if (!columnView_.UiaBrowseContentActive()) return;
    navigationWorkspace_.ActivatePane(static_cast<size_t>(columnView_.ActivePane()));
    RefreshSelectionPresentation();
    // Home has no filesystem columns and its empty path never reaches Navigate.
    if (revealFocusedColumn && !columnView_.IsPaneHome(columnView_.ActivePane())) {
        EnsureColumnVisible(columnView_.FocusedColumnIndex(), NavigationViewportWidth());
    }
    const std::wstring activePath = columnView_.ActivePanePath();
    if (!activePath.empty() && activePath != navigationWorkspace_.ActiveContext().currentPath) {
        navigationWorkspace_.Navigate(activePath);
    }
    navigationChrome_.Refresh();
    RequestRepaint();
}

LRESULT WindowShell::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_PARENTNOTIFY:
            if (LOWORD(wParam) == WM_CREATE) {
                const HWND child = reinterpret_cast<HWND>(lParam);
                if (child != nullptr && GetParent(child) == hwnd) {
                    // original-vision-journey-recovery: stable z-order alone
                    // does not clip native workspace paints under the palette.
                    // Apply the sibling paint contract to future children too.
                    const LONG_PTR style = GetWindowLongPtrW(child, GWL_STYLE);
                    SetWindowLongPtrW(child, GWL_STYLE, style | WS_CLIPSIBLINGS);
                }
            }
            return DefWindowProcW(hwnd, message, wParam, lParam);

#ifdef _DEBUG
        case WM_COPYDATA: {
            const auto* request = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
            if (request == nullptr || request->dwData != ffui::kSelectionDiagnosticsQuery ||
                wParam == 0 || request->cbData != 0) {
                return DefWindowProcW(hwnd, message, wParam, lParam);
            }
            const std::wstring payload = FormatSelectionProbeState(columnView_.CurrentSelectionDiagnostics());
            COPYDATASTRUCT response{};
            response.dwData = ffui::kSelectionDiagnosticsResponse;
            response.cbData = static_cast<DWORD>((payload.size() + 1) * sizeof(wchar_t));
            response.lpData = const_cast<wchar_t*>(payload.c_str());
            SendMessageW(reinterpret_cast<HWND>(wParam), WM_COPYDATA,
                         reinterpret_cast<WPARAM>(hwnd), reinterpret_cast<LPARAM>(&response));
            return TRUE;
        }
#endif
        case WM_SIZE: {
            const UINT width = LOWORD(lParam);
            const UINT height = HIWORD(lParam);
            renderer_.Resize(width, height);
            navigationChrome_.Reposition();
            navigationSidebar_.Reposition();
            const auto shellLayout = CurrentShellLayout();
            searchPanel_.Reposition(shellLayout.contentRect);
            storageAnalysis_.Reposition(shellLayout.contentRect);
            // Palette last so its HWND_TOP overlay stays above Search/Storage.
            commandPalette_.Reposition();
            RequestRepaint();
            return 0;
        }

        case WM_DPICHANGED: {
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            // Size the swap chain from the new CLIENT rect, never from the
            // suggested window rect: the window rect includes non-client
            // chrome, so a composition swap chain sized from it overflows the
            // client area after a DPI change and clips the bottom of the
            // scene (status bar) off the visible surface. SetWindowPos above
            // raises WM_SIZE with this same client size; the explicit read
            // keeps the resize correct regardless of message coalescing.
            RECT clientAfterDpi{};
            GetClientRect(hwnd, &clientAfterDpi);
            renderer_.Resize(static_cast<UINT>((std::max)(0L, clientAfterDpi.right - clientAfterDpi.left)),
                             static_cast<UINT>((std::max)(0L, clientAfterDpi.bottom - clientAfterDpi.top)));
            navigationChrome_.Reposition();
            navigationSidebar_.Reposition();
            const auto shellLayout = CurrentShellLayout();
            searchPanel_.Reposition(shellLayout.contentRect);
            storageAnalysis_.Reposition(shellLayout.contentRect);
            commandPalette_.Reposition();
            // Type icons are bitmap-resolution-dependent; drop the cached
            // bitmaps so they re-resolve at the new DPI scale on the next paint.
            if (iconCache_) {
                iconCache_->Flush();
            }
            RequestRepaint();
            return 0;
        }

        // semantic-uia-core-surfaces 2.5: WM_GETOBJECT integration -- the
        // custom Column View / Home surface answers UIAutomationCore's
        // provider request with its fragment root (design D2).
        case WM_GETOBJECT: {
            if (static_cast<LONG>(lParam) == UiaRootObjectId) {
                static thread_local int serving = 0;
                if (!uiaReady_ || serving > 0 || UiaHostQueryGuard::Active(hwnd)) {
                    return DefWindowProcW(hwnd, message, wParam, lParam);
                }
                ++serving;
                LRESULT result = 0;
                Microsoft::WRL::ComPtr<IRawElementProviderFragmentRoot> root =
                    columnView_.UiaRootProvider(hwnd);
                if (root != nullptr) {
                    Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;
                    if (SUCCEEDED(root.As(&simple)) && simple != nullptr) {
                        result = UiaReturnRawElementProvider(hwnd, wParam, lParam, simple.Get());
                    }
                }
                --serving;
                return result;
            }
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        // semantic-uia-core-surfaces 2.5: real keyboard-focus ownership of the
        // main window, projected into HasKeyboardFocus by the providers.
        case WM_SETFOCUS: {
            columnView_.SetUiaKeyboardFocusOwned(true);
            // ui-overhaul phase 3: re-check the clipboard on activation so a cut
            // ghost left over an external clipboard change clears immediately.
            SyncCutGhost();
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        case WM_KILLFOCUS: {
            columnView_.SetUiaKeyboardFocusOwned(false);
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        case WM_SETTINGCHANGE:
            if (settings_.theme == ffprotocol::ThemePreference::FollowSystem) ApplyTheme();
            navigationWorkspace_.ReResolveKnownFolders();
            navigationSidebar_.Refresh();
            RefreshHomeData(); // quick-access-home 5.5: known folders may have changed
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT paint{};
            BeginPaint(hwnd, &paint);
            Render();
            EndPaint(hwnd, &paint);
            return 0;
        }

        case WM_APP_REPAINT:
            RequestRepaint();
            return 0;

        case WM_APP_SNAPSHOT_UPDATED: {
            snapshotRefreshPosted_ = false;
            // Folder aggregates are derived from the published projection,
            // so a new snapshot generation must not reuse the old Details
            // result even when the selected folder keeps the same identity.
            aggregateRequest_.Invalidate();
            const auto selectionBeforeSnapshot = columnView_.CurrentSelection();
            {
                const uint64_t generation = pendingSnapshotGeneration_.exchange(0);
                columnView_.OnSnapshotUpdated(generation);
            }
            // A path activation can install a pending selection before the
            // asynchronous directory snapshot arrives.  OnSnapshotUpdated
            // materializes that row, but no pointer/keyboard event follows to
            // refresh Details; detect that model transition here so preview
            // requests cannot remain in the shell-resolved Loading state.
            if (!PreviewSelectionEquivalent(selectionBeforeSnapshot, columnView_.CurrentSelection())) {
                RefreshSelectionPresentation();
            }
            searchPanel_.OnSnapshotUpdated();
            storageAnalysis_.OnSnapshotUpdated();
            RequestRepaint();
            if (pendingSnapshotGeneration_.load() != 0 && !snapshotRefreshPosted_.exchange(true)) {
                PostMessageW(hwnd_, WM_APP_SNAPSHOT_UPDATED, 0, 0);
            }
            return 0;
        }

        case WM_APP_ENGINE_STATUS: {
            // wParam carries the EngineConnectionState; lParam is the
            // privileged-path-active flag fanned out to the index-dependent
            // surfaces. Keep all ColumnView model work on the owner thread:
            // this message is posted by EngineClient's management thread.
            const EngineConnectionState state = static_cast<EngineConnectionState>(wParam);
            columnView_.OnEngineConnectionState(state);
            searchPanel_.OnEngineConnectionState(state);
            if (state == EngineConnectionState::Connected) {
                // A restarted engine owns a fresh snapshot mapping. Replay
                // the visible columns so reconnects cannot leave the browser
                // showing empty/stale columns indefinitely.
                columnView_.RefreshAllColumns();
            }
            engineActive_ = lParam != 0;
            searchPanel_.SetEngineActive(engineActive_);
            settingsDialog_.SetEngineActive(engineActive_);
            storageAnalysis_.SetEngineActive(engineActive_);
            if (state == EngineConnectionState::Unavailable) {
                // First-run clarity: a rejected/unlaunchable engine (e.g. the
                // fail-closed unsigned-engine rejection in default builds) must
                // not read as a perpetual silent "Refreshing…". Messaging only
                // -- the fail-closed security boundary is never weakened.
                ShowStatusNotice(
                    L"Indexing enhancement unavailable in this configuration — browsing still works.");
            }
            RequestRepaint();
            return 0;
        }

        case WM_APP_SEARCH_COMPLETE:
            searchPanel_.HandleCompletion(lParam);
            return 0;

        case WM_APP_COLUMNVIEW_BROWSE_ACTION: {
            if (lParam == 0) return 0;
            const auto& request = *reinterpret_cast<const UiaBrowseActionRequest*>(lParam);
            if (!columnView_.HandleUiaBrowseAction(request)) return 0;
            // column-uia-identity-dispatch: owner-thread mutations must also
            // run the same visible navigation/preview/reveal boundary as input.
            if (request.kind == UiaBrowseActionKind::ScrollItem) {
                EnsurePaneColumnVisible(request.pane, request.column, NavigationViewportWidth());
                RequestRepaint();
            } else if (request.kind != UiaBrowseActionKind::FocusRoot) {
                if (request.kind == UiaBrowseActionKind::FocusItem) {
                    columnView_.EnsureFocusedItemVisible(CurrentShellLayout().details.viewportHeightDip);
                }
                ReconcileBrowseInteraction(true);
            }
            return 1;
        }

        case WM_APP_COLUMNVIEW_HOME_ACTION:
            return lParam != 0 && columnView_.HandleUiaHomeAction(
                *reinterpret_cast<const UiaHomeActionRequest*>(lParam)) ? 1 : 0;

        case WM_APP_INDEXED_SEARCH_COMPLETE:
            searchPanel_.HandleIndexedCompletion(lParam);
            return 0;

        case WM_TIMER:
            if (wParam == kWorkspaceSaveTimer) {
                navigationWorkspace_.FlushState();
                // Transient status/operation notices carry a wall-clock deadline;
                // without a repaint at expiry the text would linger on screen
                // until the next unrelated invalidation. This timer already
                // ticks at 500 ms, so piggyback instead of adding a new timer.
                if (!statusNotice_.empty() && GetTickCount64() >= statusNoticeUntilMs_) {
                    statusNotice_.clear();
                    statusNoticeUntilMs_ = 0;
                    RequestRepaint();
                }
                return 0;
            }
            if (wParam == kUiScrollAnimTimer) {
                const uint64_t now = GetTickCount64();
                lastAnimTickMs_ = now;
                scrollAnim_.Tick(now);
                const bool wasAnimating2 = scrollAnim2_.IsAnimating();
                scrollAnim2_.Tick(now);
                // Flush the settled pane-1 offset exactly once; Render only
                // syncs ColumnView's offset mid-flight so idle/restored state
                // survives without an animation running.
                if (wasAnimating2 && !scrollAnim2_.IsAnimating()) {
                    columnView_.SetScrollOffset2(scrollAnim2_.Value());
                }
                RequestRepaint();
                // Keep the timer alive only while an animation is in flight
                // (scroll, hover, or the theme cross-fade); once everything
                // settles it must not run while idle.
                if (!crossFadeActive_ && !scrollAnim_.IsAnimating() && !scrollAnim2_.IsAnimating() && !columnView_.HoverAnimating()) {
                    HandleAnimationTimer(false);
                }
                return 0;
            }
            if (searchPanel_.HandleTimer(wParam)) return 0;
            return DefWindowProcW(hwnd, message, wParam, lParam);

        case WM_NOTIFY: {
            if (searchPanel_.HandleNotify(lParam)) return 0;
            if (storageAnalysis_.HandleNotify(lParam)) return 0;
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        case WM_DRAWITEM:
            if (storageAnalysis_.HandleDrawItem(lParam)) return TRUE;
            if (commandPalette_.HandleDrawItem(lParam)) return TRUE;
            if (searchPanel_.HandleDrawItem(lParam)) return TRUE;
            return DefWindowProcW(hwnd, message, wParam, lParam);

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            const LRESULT paletteBrush = commandPalette_.HandleCtlColor(message, wParam, lParam);
            if (paletteBrush != 0) return paletteBrush;
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        case WM_APP_PREVIEW_READY:
            RequestRepaint();
            return 0;

        case WM_APP_ICON_READY:
            RequestRepaint();
            return 0;

        case WM_APP_UNAVAILABLE_VOLUMES: {
            std::unique_ptr<std::vector<ffprotocol::UnavailableVolumeRecord>> records(
                reinterpret_cast<std::vector<ffprotocol::UnavailableVolumeRecord>*>(lParam));
            if (!records || records->empty()) {
                MessageBoxW(hwnd_, L"There are no unavailable drives with retained index data.",
                            L"Forget unavailable drive", MB_OK | MB_ICONINFORMATION);
                return 0;
            }

            HMENU choices = CreatePopupMenu();
            if (choices == nullptr) {
                return 0;
            }
            const size_t visibleCount = (std::min)(records->size(), kMaxForgetDriveMenuItems);
            for (size_t i = 0; i < visibleCount; ++i) {
                const std::wstring label = FormatUnavailableVolume((*records)[i]);
                AppendMenuW(choices, MF_STRING, kForgetDriveCommandBase + static_cast<UINT>(i), label.c_str());
            }
            RECT windowRect{};
            GetWindowRect(hwnd_, &windowRect);
            const UINT command = TrackPopupMenu(
                choices, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                (windowRect.left + windowRect.right) / 2, (windowRect.top + windowRect.bottom) / 2,
                0, hwnd_, nullptr);
            DestroyMenu(choices);
            if (command < kForgetDriveCommandBase
                || command >= kForgetDriveCommandBase + static_cast<UINT>(visibleCount)) {
                return 0;
            }

            const auto selected = (*records)[static_cast<size_t>(command - kForgetDriveCommandBase)];
            const std::wstring prompt = L"Permanently remove this unavailable drive's retained index data?\n\n"
                + FormatUnavailableVolume(selected) + L"\n\nThe drive itself will not be modified.";
            if (MessageBoxW(hwnd_, prompt.c_str(), L"Forget unavailable drive",
                            MB_OKCANCEL | MB_DEFBUTTON2 | MB_ICONWARNING) != IDOK) {
                return 0;
            }
            engineClient_.ForgetUnavailableVolume(selected.volumeRowId, [this](auto result) {
                auto owned = std::make_unique<ffprotocol::ForgetUnavailableVolumeResultPayload>(result);
                const HWND target = hwnd_;
                if (target != nullptr
                    && PostMessageW(target, WM_APP_FORGET_VOLUME_RESULT, 0,
                                    reinterpret_cast<LPARAM>(owned.get()))) {
                    owned.release();
                }
            });
            return 0;
        }

        case WM_APP_FORGET_VOLUME_RESULT: {
            std::unique_ptr<ffprotocol::ForgetUnavailableVolumeResultPayload> result(
                reinterpret_cast<ffprotocol::ForgetUnavailableVolumeResultPayload*>(lParam));
            if (!result) {
                return 0;
            }
            const wchar_t* messageText = L"The retained index data could not be removed.";
            UINT icon = MB_ICONERROR;
            switch (result->status) {
                case ffprotocol::ForgetUnavailableVolumeStatus::Removed:
                    messageText = L"The unavailable drive's retained index data was removed.";
                    icon = MB_ICONINFORMATION;
                    break;
                case ffprotocol::ForgetUnavailableVolumeStatus::NotFound:
                    messageText = L"That unavailable drive is no longer present in the index.";
                    icon = MB_ICONINFORMATION;
                    break;
                case ffprotocol::ForgetUnavailableVolumeStatus::VolumeAvailable:
                    messageText = L"The drive is currently available and cannot be forgotten.";
                    icon = MB_ICONWARNING;
                    break;
                case ffprotocol::ForgetUnavailableVolumeStatus::Failed:
                    break;
            }
            MessageBoxW(hwnd_, messageText, L"Forget unavailable drive", MB_OK | icon);
            return 0;
        }

        case WM_APP_FOLDER_AGGREGATE: {
            // allocated-size-storage-accuracy 6.3: the posted payload is the
            // additive V2 shape (logical + allocated + completeness pair).
            auto* payload = reinterpret_cast<ffprotocol::FolderAggregateResultV2Payload*>(lParam);
            if (!payload) {
                return 0;
            }
            // Stale-result gating: only accept the response that matches the
            // request the UI still considers current.
            if (aggregateRequest_.Accepts(payload->requestId)) {
                pendingAggregateStatus_ = payload->status;
                pendingAggregateItemCount_ = payload->itemCount;
                pendingAggregateTotalSize_ = payload->totalSizeBytes;
                pendingAggregateAllocatedSize_ = payload->totalAllocatedSizeBytes;
                pendingAggregateAllocatedKnownCount_ = payload->allocatedBytesKnownCount;
                pendingAggregateAllocatedTotalCount_ = payload->allocatedBytesTotalCount;
                RequestRepaint();
            }
            delete payload;
            return 0;
        }

        case WM_APP_STORAGE_AGGREGATE: {
            storageAnalysis_.HandleCompletion(lParam);
            return 0;
        }

        case WM_APP_STORAGE_RANKING:
            storageAnalysis_.HandleRankingCompletion(lParam);
            return 0;

        case WM_APP_TREEMAP_AGGREGATE:
            storageAnalysis_.HandleTreemapCompletion(lParam);
            return 0;

        case WM_APP_TREEMAP_NAVIGATE:
            // semantic-treemap-uia 11.x: a UIA Invoke marshaled from a
            // UIAutomationCore worker thread; the value-only payload is
            // re-validated against current state on this (UI) thread.
            storageAnalysis_.HandleTreemapUiaNavigate(lParam);
            return 0;

        case WM_APP_FILE_OPERATION_CONFLICT: {
            // The worker posts a heap shared_ptr so the question stays alive
            // even when Stop()/WM_DESTROY runs inside the dialog's nested
            // message pump (close-file-ops-conflict-lifetime-and-outcome-truth D1).
            std::unique_ptr<FileOperationConflictQuestionRef> posted(
                reinterpret_cast<FileOperationConflictQuestionRef*>(lParam));
            if (!posted || !*posted) return 0;
            const FileOperationConflictQuestionRef question = *posted;
            const ConflictDecision decision = ShowConflictDialog(hwnd_, question->source, question->destination);
            {
                std::lock_guard lock(question->mutex);
                question->decision = decision;
                question->answered = true;
            }
            question->answeredCondition.notify_one();
            return 0;
        }

        case WM_APP_FILE_OPERATION_AVAILABILITY:
            if (commandPalette_.Visible()) commandPalette_.UpdateContext(CurrentCommandContext());
            return 0;

        case WM_APP_FILE_OPERATION_EVENT: {
            std::unique_ptr<FileOperationEvent> event(reinterpret_cast<FileOperationEvent*>(lParam));
            if (!event) return 0;
            // Progress cannot change availability; avoid rebuilding a potentially
            // large selection and clipboard context for every presentation tick.
            if (event->kind != FileOperationEventKind::Progress && commandPalette_.Visible()) {
                commandPalette_.UpdateContext(CurrentCommandContext());
            }
            if (event->kind == FileOperationEventKind::Queued || event->kind == FileOperationEventKind::Started) {
                SetWindowTextW(hwnd_, event->kind == FileOperationEventKind::Queued ? L"FastFiles — operation queued" : L"FastFiles — operation in progress");
                fileOpInProgress_ = true;
                fileOpProgress_ = 0.0f;
                // An operation is genuinely outstanding: Escape may cancel it.
                fileOpOutstanding_ = true;
                RequestRepaint();
            } else if (event->kind == FileOperationEventKind::Progress) {
                std::wstring title = L"FastFiles — ";
                if (!event->currentItem.empty()) title += event->currentItem + L" — ";
                title += std::to_wstring(event->percent) + L"%";
                if (event->workUnitsPerSecond > 0.0) {
                    title += L" — " + std::to_wstring(static_cast<unsigned long long>(event->workUnitsPerSecond)) + L" units/s";
                    title += L" — ETA " + std::to_wstring(static_cast<unsigned long long>(event->etaSeconds)) + L" s";
                } else {
                    title += L" — speed/ETA estimating…";
                }
                SetWindowTextW(hwnd_, title.c_str());
                // file-operations-core: failure detail is retained from the
                // terminal event below; a Progress tick never overwrites it.
                fileOpProgress_ = std::clamp(static_cast<float>(event->percent) / 100.0f, 0.0f, 1.0f);
                RequestRepaint();
            } else if (event->kind == FileOperationEventKind::Completed || event->kind == FileOperationEventKind::Cancelled) {
                // The operation is no longer outstanding; Escape must not claim
                // a cancellation is in flight when nothing is running.
                fileOpOutstanding_ = false;
                RetainLastOperationFailures(lastOperationFailures_, *event);
                std::wstring title = event->kind == FileOperationEventKind::Cancelled ? L"FastFiles — cancelled" : L"FastFiles — complete";
                if (!event->failures.empty()) {
                    title = event->kind == FileOperationEventKind::Cancelled
                        ? L"FastFiles — stopped with errors" : L"FastFiles — complete with errors";
                }
                title += L" — " + std::to_wstring(event->completed) + L" of " + std::to_wstring(event->total);
                if (event->skipped > 0) title += L" — " + std::to_wstring(event->skipped) + L" skipped";
                title += L" — " + std::to_wstring(event->failures.size()) + L" failed";
                if (!event->failures.empty()) {
                    title += L" (first: " + event->failures.front().path + L")";
                }
                SetWindowTextW(hwnd_, title.c_str());
                // ui-overhaul phase 8: completion, cancellation, and failure
                // also ride the non-modal status line, so an outcome is never
                // visible only in the window title. Failures stay actionable
                // through the existing last-operation-errors surface.
                if (!event->failures.empty()) {
                    ShowStatusNotice(std::to_wstring(event->failures.size()) + L" of " +
                                     std::to_wstring(event->total) +
                                     L" item(s) failed. " +
                                     (event->kind == FileOperationEventKind::Cancelled ? L"Operation stopped. " : L"") +
                                     L"Right-click for last operation errors.");
                } else if (event->kind == FileOperationEventKind::Cancelled) {
                    ShowStatusNotice(L"Cancelled after " + std::to_wstring(event->completed) + L" of " +
                                     std::to_wstring(event->total) + L" item(s).");
                } else if (event->skipped > 0) {
                    // Skipped items are a user decision, not a failure: an
                    // all-skip batch must not read as "0 item(s) completed".
                    ShowStatusNotice(std::to_wstring(event->completed) + L" item(s) completed, " +
                                     std::to_wstring(event->skipped) + L" skipped.");
                } else {
                    ShowStatusNotice(std::to_wstring(event->completed) + L" item(s) completed.");
                }
                if (event->reversibleOperation) {
                    auto& reversible = *event->reversibleOperation;
                    if (reversible.kind == ReversibleOperationKind::Rename && reversible.paths.size() == 1) {
                        operationHistory_.PushRename(std::move(reversible.paths.front().originalPath),
                                                     std::move(reversible.paths.front().resultingPath));
                    } else if (reversible.kind == ReversibleOperationKind::Move) {
                        operationHistory_.PushMove(std::move(reversible.paths));
                    } else if (reversible.kind == ReversibleOperationKind::RecycleDelete) {
                        operationHistory_.PushRecycleDelete(std::move(reversible.paths));
                    }
                }
                if (event->operation == FileOperationKind::Restore && !event->failures.empty()) {
                    // Reflect partial success accurately: some items restored
                    // is a different message from "nothing could be restored".
                    if (event->completed > 0) {
                        MessageBoxW(hwnd_,
                                    (L"Restored " + std::to_wstring(event->completed) + L" of " +
                                     std::to_wstring(event->total) + L" item(s). " +
                                     std::to_wstring(event->failures.size()) +
                                     L" item(s) could no longer be restored from the Recycle Bin.").c_str(),
                                    L"Undo partially completed", MB_OK | MB_ICONWARNING);
                    } else {
                        MessageBoxW(hwnd_, L"The deleted item can no longer be restored. It may have been removed from the Recycle Bin.",
                                    L"Undo unavailable", MB_OK | MB_ICONWARNING);
                    }
                }
                if (!event->createdPaths.empty()) {
                    const std::filesystem::path created(event->createdPaths.front());
                    NavigateWorkspace(created.parent_path().wstring(), created.filename().wstring());
                    BeginInlineRename(created.wstring());
                }
                // The existing UI-to-engine control pipe has no blocking
                // request/reply path here.  Requesting affected directories
                // is deliberately best effort: it refreshes any live
                // listing while an unavailable engine cannot delay the UI.
                for (const auto& affected : event->affectedPaths) {
                    const std::wstring parent = std::filesystem::path(affected).parent_path().wstring();
                    if (!parent.empty()) engineClient_.RequestDirectory(parent);
                }
                fileOpInProgress_ = false;
                columnView_.OnSnapshotUpdated();
                // ui-overhaul phase 3: a completed move/delete invalidates the
                // cut ghost for paths that no longer exist in place.
                SyncCutGhost();
                navigationChrome_.Refresh();
                RequestRepaint();
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            // Custom-rendered content handles the click itself and therefore
            // does not reach DefWindowProc's default focus activation. Keep
            // keyboard commands (F2, Delete, Enter, and pane shortcuts)
            // owned by the shell after a real pointer selection.
            SetFocus(hwnd_);
            if (storageAnalysis_.Visible() && storageAnalysis_.GetViewMode() == StorageAnalysis::ViewMode::Treemap) {
                // Treemap layout and hit-testing use DIPs, while WM mouse
                // coordinates are physical pixels.  Convert at the same
                // boundary as ColumnView so hover/click targets remain
                // aligned at 125%/150% DPI.
                const float scale = CurrentDpiScale();
                const int x = static_cast<int>(std::lround(static_cast<float>(GET_X_LPARAM(lParam)) / scale));
                const int y = static_cast<int>(std::lround(static_cast<float>(GET_Y_LPARAM(lParam)) / scale));
                if (storageAnalysis_.HandleLButtonDown(wParam, MAKELPARAM(x, y))) {
                    return 0;
                }
            }
            const auto shellLayout = CurrentShellLayout();
            const float scale = CurrentDpiScale();
            const float clientX = static_cast<float>(GET_X_LPARAM(lParam)) / scale;
            const float clientY = static_cast<float>(GET_Y_LPARAM(lParam)) / scale;
            // ui-overhaul phase 7: the collapsed details disclosure rail is a
            // real control; clicking anywhere on its strip collapses the
            // details surface entirely.
            if (shellLayout.details.paneCollapsed && !shellLayout.detailsRect.Empty() &&
                clientX >= shellLayout.detailsRect.left && clientX <= shellLayout.detailsRect.right &&
                clientY >= shellLayout.contentRect.top && clientY <= shellLayout.contentRect.bottom) {
                detailsPaneVisible_ = false;
                RequestRepaint();
                return 0;
            }
            D2D1_POINT_2F point = D2D1::Point2F(
                static_cast<float>(GET_X_LPARAM(lParam)) / scale - shellLayout.contentRect.left,
                static_cast<float>(GET_Y_LPARAM(lParam)) / scale - shellLayout.contentRect.top);
            const float viewportWidth = shellLayout.details.availableColumnWidthDip;
            if (point.x < 0.0f || point.x >= viewportWidth || point.y < 0.0f) {
                return 0;
            }
            columnView_.OnMouseDown(
                point, scrollOffset_, viewportWidth,
                (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            navigationWorkspace_.ActivatePane(static_cast<size_t>(columnView_.ActivePane()));
            dragOrigin_ = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            dragArmed_ = !columnView_.ActiveSelectionPaths().empty();
            ReconcileBrowseInteraction(true);
            return 0;
        }

        case WM_LBUTTONDBLCLK: {
            // The first click in the native double-click sequence has already
            // selected the item (and, for a folder, populated the next
            // column).  Only a selected file should be opened here; folders
            // intentionally retain Finder-style single-click navigation and
            // must not be activated a second time against a newly focused
            // column.
            const auto selection = columnView_.ActiveSelectionItems();
            if (ShouldOpenSelectionOnDoubleClick(selection)) {
                InvokeCommand(L"item.open");
            }
            dragArmed_ = false;
            return 0;
        }

        case WM_MOUSEMOVE: {
            const bool inTreemap = storageAnalysis_.Visible() && storageAnalysis_.GetViewMode() == StorageAnalysis::ViewMode::Treemap;
            if (inTreemap) {
                const float scale = CurrentDpiScale();
                const int x = static_cast<int>(std::lround(static_cast<float>(GET_X_LPARAM(lParam)) / scale));
                const int y = static_cast<int>(std::lround(static_cast<float>(GET_Y_LPARAM(lParam)) / scale));
                if (storageAnalysis_.HandleMouseMove(wParam, MAKELPARAM(x, y))) {
                    RequestRepaint();
                    return 0;
                }
            }
            if (dragArmed_ && (wParam & MK_LBUTTON) != 0 &&
                (std::abs(GET_X_LPARAM(lParam) - dragOrigin_.x) >= GetSystemMetrics(SM_CXDRAG) ||
                 std::abs(GET_Y_LPARAM(lParam) - dragOrigin_.y) >= GetSystemMetrics(SM_CYDRAG))) {
                dragArmed_ = false;
                const std::vector<std::wstring> selection = columnView_.ActiveSelectionPaths();
                // complete-ole-drag-source-move-effect D1: consume the negotiated
                // drag effect. A MOVE the target did not perform itself (sources
                // still exist) completes as a Recycle-Bin delete of those sources
                // through the same worker as the menu Delete; COPY/LINK never
                // delete, and a target that already moved the files is left alone.
                const DWORD dragEffect = BeginFileDrag(selection, static_cast<DWORD>(wParam));
                auto remainingSources = SourcePathsToDeleteAfterDrag(dragEffect, selection,
                    [](const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; });
                if (!remainingSources.empty()) {
                    fileOperations_.Enqueue({FileOperationKind::Delete, std::move(remainingSources), {}, {}, true});
                }
                return 0;
            }
            // Route hover into the column view (paint-only; hit-testing is
            // unchanged) so rows can animate a hover overlay. TME_LEAVE is
            // re-armed so the leave that must cancel the hover is observed.
            if (!inTreemap) {
                const auto shellLayout = CurrentShellLayout();
                const float scale = CurrentDpiScale();
                D2D1_POINT_2F point = D2D1::Point2F(
                    static_cast<float>(GET_X_LPARAM(lParam)) / scale - shellLayout.contentRect.left,
                    static_cast<float>(GET_Y_LPARAM(lParam)) / scale - shellLayout.contentRect.top);
                const float viewportWidth = shellLayout.details.availableColumnWidthDip;
                if (point.x < 0.0f || point.x >= viewportWidth || point.y < 0.0f) {
                    columnView_.OnMouseLeave();
                    return 0;
                }
                columnView_.OnMouseMove(point, scrollOffset_, viewportWidth);
                if (columnView_.HoverAnimating()) {
                    HandleAnimationTimer(true);
                }
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&tme);
                RequestRepaint();
            }
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        case WM_MOUSELEAVE: {
            columnView_.OnMouseLeave();
            if (columnView_.HoverAnimating()) {
                HandleAnimationTimer(true);
            }
            RequestRepaint();
            return 0;
        }

        case WM_LBUTTONUP:
            dragArmed_ = false;
            return 0;

        case WM_RBUTTONDOWN: {
            SetFocus(hwnd_);
            const auto shellLayout = CurrentShellLayout();
            const float scale = CurrentDpiScale();
            D2D1_POINT_2F point = D2D1::Point2F(
                static_cast<float>(GET_X_LPARAM(lParam)) / scale - shellLayout.contentRect.left,
                static_cast<float>(GET_Y_LPARAM(lParam)) / scale - shellLayout.contentRect.top);
            columnView_.OnMouseDown(point, scrollOffset_, shellLayout.details.availableColumnWidthDip, false, false);
            navigationWorkspace_.ActivatePane(static_cast<size_t>(columnView_.ActivePane()));
            const std::wstring activePath = columnView_.ActivePanePath();
            if (!activePath.empty() && activePath != navigationWorkspace_.ActiveContext().currentPath) {
                navigationWorkspace_.Navigate(activePath);
            }
            navigationChrome_.Refresh();
            RefreshSelectionPresentation();
            RequestRepaint();
            return 0;
        }

        case WM_MOUSEWHEEL: {
            const short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            const auto shellLayout = CurrentShellLayout();
            const float viewportWidth = shellLayout.details.availableColumnWidthDip;
            const float viewportHeight = shellLayout.details.viewportHeightDip;
            if ((GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT) == 0) {
                // Normal wheel input scrolls rows. Shift+wheel retains the
                // Finder-style horizontal column scrolling path below.
                columnView_.ScrollVertical(-static_cast<float>(delta) / 2.0f, viewportHeight);
                RequestRepaint();
                return 0;
            }
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd_, &pt);
            const float effectiveX = static_cast<float>(pt.x) / CurrentDpiScale() -
                                     shellLayout.contentRect.left;
            const uint64_t now = GetTickCount64();

            if (columnView_.IsDualPane()) {
                const float paneWidth = viewportWidth / 2.0f;
                const int pane = effectiveX < paneWidth ? 0 : 1;
                // Base the target on the currently displayed offset (the eased
                // value mid-flight, the stored offset when idle) so re-targets
                // never jump.
                const float paneScroll = pane == 0 ? scrollAnim_.Value()
                    : (scrollAnim2_.IsAnimating() ? scrollAnim2_.Value() : columnView_.ScrollOffset2());
                const float maxScroll = std::max(0.0f, columnView_.PaneContentWidth(pane) - paneWidth);
                const float newScroll = std::clamp(paneScroll - static_cast<float>(delta) / 2.0f, 0.0f, maxScroll);
                if (pane == 0) {
                    scrollAnim_.AnimateTo(newScroll, kUiAnimationDefaultMs, now);
                } else {
                    if (!scrollAnim2_.IsAnimating()) scrollAnim2_.Snap(columnView_.ScrollOffset2());
                    scrollAnim2_.AnimateTo(newScroll, kUiAnimationDefaultMs, now);
                }
            } else {
                const float maxScroll = std::max(0.0f, columnView_.ContentWidth() - viewportWidth);
                const float newScroll = std::clamp(scrollAnim_.Value() - static_cast<float>(delta) / 2.0f, 0.0f, maxScroll);
                scrollAnim_.AnimateTo(newScroll, kUiAnimationDefaultMs, now);
            }
            HandleAnimationTimer(true);
            RequestRepaint();
            return 0;
        }

        case WM_CONTEXTMENU: {
            if (storageAnalysis_.Visible()) {
                // Preserve WM_CONTEXTMENU's screen coordinates and keyboard
                // sentinel. Cancel/empty/summary clicks never open a hidden
                // browsing menu as a fallback.
                storageAnalysis_.HandleContextMenu(wParam, lParam);
                return 0;
            }
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (point.x == -1 && point.y == -1 && searchPanel_.Visible()) {
                RECT selected{};
                if (!searchPanel_.ActiveSelectionRect(selected)) return 0;
                point = {selected.left, selected.bottom};
            } else if (point.x == -1 && point.y == -1) {
                const auto shellLayout = CurrentShellLayout();
                const int column = columnView_.FocusedColumnIndex();
                const int item = columnView_.FocusedItemIndex();
                const float rowHeight = BrowseRowHeight();
                point = {static_cast<LONG>(ffui::UiScale(shellLayout.contentRect.left +
                                                        column * ColumnView::kColumnWidth - scrollOffset_ + 24.0f)),
                         static_cast<LONG>(ffui::UiScale(shellLayout.contentRect.top +
                                                        ColumnView::kBadgeHeight +
                                                        static_cast<float>((std::max)(0, item)) * rowHeight + rowHeight))};
                ClientToScreen(hwnd_, &point);
            }
            ShowContextMenu(point);
            return 0;
        }

        case WM_KEYDOWN: {
            if (wParam == VK_F9) {
                // theme-toggle-follow-system: resolve Follow-System at press
                // time and flip the RENDERED theme (the historical inline map
                // made the first press a visible no-op on light systems).
                settings_.theme = ffui::NextThemeTogglePreference(settings_.theme, IsSystemDark());
                SaveAndNotifySettings();
                ApplyTheme();
                return 0;
            }
            if (wParam == VK_ESCAPE) {
                if (searchPanel_.Visible()) {
                    HideToolWorkspaces();
                    return 0;
                }
                // Only cancel when an operation is actually outstanding; an
                // idle Escape must not claim "cancelling operation…".
                CancelFileOperations();
                return 0;
            }
            if (searchPanel_.Visible() || storageAnalysis_.Visible()) return 0;
            columnView_.OnKeyDown(static_cast<int>(wParam));
            if (wParam == VK_UP || wParam == VK_DOWN) {
                columnView_.EnsureFocusedItemVisible(CurrentShellLayout().details.viewportHeightDip);
            }
            ReconcileBrowseInteraction(wParam == VK_LEFT || wParam == VK_RIGHT || wParam == VK_RETURN);
            return 0;
        }

        case WM_COMMAND:
            if (searchPanel_.HandleOwnerCommand(wParam, lParam)) return 0;
            if (storageAnalysis_.HandleOwnerCommand(wParam, lParam)) return 0;
            if (commandPalette_.HandleOwnerCommand(wParam, lParam)) return 0;
            switch (LOWORD(wParam)) {
                case kMenuThemeLight: settings_.theme = ffprotocol::ThemePreference::Light; break;
                case kMenuThemeDark: settings_.theme = ffprotocol::ThemePreference::Dark; break;
                case kMenuThemeSystem: settings_.theme = ffprotocol::ThemePreference::FollowSystem; break;
                case kMenuPreviewEnabled: settings_.previewEnabled = !settings_.previewEnabled; break;
                case kMenuPreview1Mb: settings_.maxAutoPreviewBytes = 1ULL * 1024ULL * 1024ULL; break;
                case kMenuPreview16Mb: settings_.maxAutoPreviewBytes = 16ULL * 1024ULL * 1024ULL; break;
                case kMenuPreview64Mb: settings_.maxAutoPreviewBytes = 64ULL * 1024ULL * 1024ULL; break;                case kMenuForgetUnavailableDrive:
                    engineClient_.RequestUnavailableVolumes([this](auto records) {
                        auto owned = std::make_unique<std::vector<ffprotocol::UnavailableVolumeRecord>>(
                            std::move(records));
                        const HWND target = hwnd_.load();
                        if (target != nullptr
                            && PostMessageW(target, WM_APP_UNAVAILABLE_VOLUMES, 0,
                                            reinterpret_cast<LPARAM>(owned.get()))) {
                            owned.release();
                        }
                    });
                    return 0;
                case kMenuOperationDetails: {
                    std::wstring details;
                    for (const auto& failure : lastOperationFailures_) {
                        if (!details.empty()) details += L"\n\n";
                        details += (failure.path.empty() ? std::wstring(L"(operation)")
                                                         : ffui::NormalizeDisplayPath(failure.path)) +
                                    L"\n" + ffui::DescribeFileOperationFailure(failure.error);
                        if (FAILED(failure.cleanupError)) {
                            details += L"\nThe incomplete new file could not be removed: " +
                                       ffui::DescribeFileOperationFailure(failure.cleanupError);
                        }
                    }
                    if (!details.empty()) {
                        MessageBoxW(hwnd_, details.c_str(), L"Last operation errors", MB_OK | MB_ICONERROR);
                    } else {
                        ShowStatusNotice(L"No recent file-operation errors.");
                    }
                    return 0;
                }
                case kMenuExportDiagnostics: {
                    // settings-and-appearance 8.4: literal paths require an
                    // explicit opt-in at export time; the default bundle is
                    // aggregated/redacted. File content is excluded in both
                    // modes (the log never holds content).
                    const int optIn = MessageBoxW(hwnd_,
                        L"Include literal paths and filenames in the exported bundle?\n\n"
                        L"Yes: include literal paths (opt-in, redacted by default otherwise)\n"
                        L"No: aggregated/redacted bundle (recommended)\n\n"
                        L"File content is never included in either mode.",
                        L"Export Diagnostics Bundle", MB_YESNO | MB_ICONQUESTION);
                    const bool includeLiteralPaths = optIn == IDYES;
                    wchar_t destination[MAX_PATH]{};
                    const wchar_t* filter = L"Diagnostic bundle (*.txt)\0*.txt\0All files (*.*)\0*.*\0";
                    OPENFILENAMEW ofn{};
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hwnd_;
                    ofn.lpstrFilter = filter;
                    ofn.lpstrFile = destination;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.lpstrDefExt = L"txt";
                    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
                    if (!GetSaveFileNameW(&ofn)) return 0;
                    const bool ok = ffprotocol::ExportDiagnosticBundle(destination, includeLiteralPaths);
                    // Distinguish an unwritable destination from a missing
                    // diagnostic log instead of reporting every failure as
                    // "no diagnostic log found".
                    std::wstring failureMessage;
                    if (!ok) {
                        if (_waccess(destination, 02) != 0) {
                            failureMessage = L"Export failed: the destination file could not be written.";
                        } else if (!ffprotocol::DiagnosticLogPath().empty() &&
                                   std::filesystem::exists(ffprotocol::DiagnosticLogPath())) {
                            failureMessage = L"Export failed: the diagnostic bundle could not be written.";
                        } else {
                            failureMessage = L"Export failed: no diagnostic log was available to export.";
                        }
                    }
                    MessageBoxW(hwnd_, ok ? L"Diagnostic bundle exported." : failureMessage.c_str(),
                                L"Export Diagnostics Bundle", MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR));
                    return 0;
                }
                case kMenuResetSettings:
                    settings_ = ffprotocol::DefaultSettings();
                    SaveAndNotifySettings();
                    ApplyTheme();
                    return 0;
                default: return DefWindowProcW(hwnd, message, wParam, lParam);
            }
            SaveAndNotifySettings();
            UpdatePreviewLimitMenuChecks();
            // A changed Preview Limit must take effect on the current
            // selection immediately, not only on the next selection change.
            RefreshSelectionPresentation();
            ApplyTheme();
            return 0;

        case WM_DESTROY:
            AppendShutdownPhase(L"wm-destroy", L"begin");
            storageAnalysis_.StopRanking();
            KillTimer(hwnd_, kWorkspaceSaveTimer);
            KillTimer(hwnd_, kUiScrollAnimTimer);
            navigationWorkspace_.FlushState();
            if (inlineRename_ != nullptr) FinishInlineRename(false);
            RevokeDragDrop(hwnd);
            dropTarget_.Reset();
            fileOperations_.Stop();
            AppendShutdownPhase(L"wm-destroy-file-operations-stop", L"end");
            previewController_.Clear();
            AppendShutdownPhase(L"wm-destroy-preview-clear", L"end");
            // semantic-uia-core-surfaces 2.5: release the app's provider
            // references; client-held elements become safe zombies via the
            // owner-lifetime token before the HWND is destroyed.
            columnView_.ReleaseUiaProvider();
            searchPanel_.ReleaseUiaProvider();
            AppendShutdownPhase(L"wm-destroy-providers-release", L"end");
            hwnd_ = nullptr;
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

} // namespace ffui
