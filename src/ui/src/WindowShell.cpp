#include "WindowShell.h"

#include "ConflictDialog.h"

#include <commctrl.h>
#include <utility>
#include "QuickActions.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <memory>
#include <shellapi.h>
#include <windowsx.h>
#include <winreg.h>

namespace ffui {

namespace {
constexpr wchar_t kWindowClassName[] = L"FastFilesMainWindow";
constexpr UINT WM_APP_REPAINT = WM_APP + 1;
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
constexpr UINT kForgetDriveCommandBase = 41000;
constexpr size_t kMaxForgetDriveMenuItems = 1000;
constexpr UINT WM_APP_PREVIEW_READY = WM_APP + 3;
constexpr UINT WM_APP_UNAVAILABLE_VOLUMES = WM_APP + 4;
constexpr UINT WM_APP_FORGET_VOLUME_RESULT = WM_APP + 5;
constexpr UINT WM_APP_FOLDER_AGGREGATE = WM_APP + 6;
constexpr UINT WM_APP_ENGINE_STATUS = WM_APP + 7;
constexpr UINT WM_APP_STORAGE_AGGREGATE = WM_APP + 8;
constexpr UINT kContextCommandBase = 20000;
constexpr int kNavigationChromeHeight = 72;
constexpr UINT_PTR kWorkspaceSaveTimer = 0xF1F1;

std::wstring FormatSize(uint64_t bytes) {
    wchar_t buffer[64]{};
    if (bytes >= 1024 * 1024) swprintf_s(buffer, L"%.1f MB", static_cast<double>(bytes) / (1024 * 1024));
    else if (bytes >= 1024) swprintf_s(buffer, L"%.1f KB", static_cast<double>(bytes) / 1024);
    else swprintf_s(buffer, L"%llu bytes", static_cast<unsigned long long>(bytes));
    return buffer;
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
}

WindowShell::WindowShell() : previewController_([this](uint64_t requestId, PreviewResult result) {
    std::lock_guard<std::mutex> lock(previewMutex_);
    if (requestId == activePreviewRequest_) {
        preview_ = std::move(result);
        if (hwnd_ != nullptr) PostMessageW(hwnd_, WM_APP_PREVIEW_READY, 0, 0);
    }
}) {}

void WindowShell::NavigateWorkspace(const std::wstring& path, const std::wstring& selectName) {
    if (path.empty()) return;
    navigationWorkspace_.Navigate(path);
    columnView_.NavigateToPath(path, selectName);
    RefreshSelectionPresentation();
    navigationChrome_.Refresh();
    navigationSidebar_.Refresh();
    RequestRepaint();
}

float WindowShell::NavigationViewportWidth() const {
    RECT client{};
    GetClientRect(hwnd_, &client);
    return static_cast<float>((std::max)(0L, client.right - client.left - navigationSidebar_.Width()));
}

std::filesystem::path WindowShell::ShortcutSettingsPath() const {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return {};
    return std::filesystem::path(buffer.data()) / L"FastFiles" / L"shortcuts.json";
}

CommandContext WindowShell::CurrentCommandContext() const {
    return {ClassifySelection(columnView_.ActiveSelectionItems()), !clipboardPaths_.empty()};
}

bool WindowShell::InitializeCommands() {
    const BaselineHandlers handlers{[this](const std::wstring& commandId) -> CommandHandler {
        return [this, commandId](const std::vector<std::wstring>& paths) {
            const std::wstring activePath = columnView_.ActivePanePath();
            if (!activePath.empty() && activePath != navigationWorkspace_.ActiveContext().currentPath) {
                navigationWorkspace_.Navigate(activePath);
            }
            if (commandId == L"file.copy" || commandId == L"file.cut") {
                clipboardPaths_ = paths;
                clipboardIsCut_ = commandId == L"file.cut";
            } else if (commandId == L"file.paste") {
                const std::wstring destination = columnView_.ActivePanePath();
                if (!clipboardPaths_.empty() && !destination.empty()) {
                    if (QueueTransfer(clipboardPaths_, destination,
                                      clipboardIsCut_ ? FileOperationKind::Move : FileOperationKind::Copy) && clipboardIsCut_) {
                        clipboardPaths_.clear();
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
                const DWORD attributes = GetFileAttributesW(paths.front().c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) NavigateWorkspace(paths.front());
                else if (!OpenWithDefaultApplication(hwnd_, paths.front())) MessageBoxW(hwnd_, L"The item could not be opened.", L"Open", MB_OK | MB_ICONERROR);
            } else if (commandId == L"item.open-with" && paths.size() == 1) {
                if (!ShowOpenWithPicker(hwnd_, paths.front())) MessageBoxW(hwnd_, L"The application picker could not be opened.", L"Open with", MB_OK | MB_ICONERROR);
            } else if (commandId == L"item.copy-path") {
                CopyPathsToClipboard(hwnd_, paths);
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
                CopyPathsToClipboard(hwnd_, relative);
                if (fallback) MessageBoxW(hwnd_, L"A relative path was not possible; the absolute path was copied instead.", L"Copy relative path", MB_OK | MB_ICONINFORMATION);
            } else if (commandId == L"item.open-containing-folder" && paths.size() == 1) {
                const std::filesystem::path selected(paths.front());
                NavigateWorkspace(selected.parent_path().wstring(), selected.filename().wstring());
            } else if (commandId == L"item.open-terminal") {
                const std::wstring target = paths.size() == 1 ? paths.front() : columnView_.ActivePanePath();
                if (!target.empty()) LaunchTerminalHere(hwnd_, target);
            } else if (commandId == L"item.properties") {
                RefreshSelectionPresentation();
                SetWindowTextW(hwnd_, L"FastFiles — Properties");
                RequestRepaint();
            } else if (commandId == L"selection.select-all") {
                columnView_.SelectAll();
                RefreshSelectionPresentation();
                RequestRepaint();
            } else if (commandId == L"navigation.refresh") {
                columnView_.RefreshActiveColumn();
            } else if (commandId == L"search.focus") {
                searchPanel_.ShowAndFocus(columnView_.ActivePanePath(), engineActive_);
            } else if (commandId == L"storage.analyze") {
                storageAnalysis_.ShowAndFocus(columnView_.ActivePanePath(), engineActive_);
                RequestRepaint();
            } else if (commandId == L"app.settings") {
                DrawMenuBar(hwnd_);
                MessageBoxW(hwnd_, L"Use the Settings menu to adjust FastFiles.", L"Settings", MB_OK | MB_ICONINFORMATION);
            } else if (commandId == L"app.command-palette") {
                commandPalette_.Show(CurrentCommandContext());
            } else if (commandId == L"navigation.focus-path") {
                navigationChrome_.FocusAddressBar();
            } else if (commandId == L"navigation.new-tab") {
                navigationWorkspace_.OpenTab();
                columnView_.NavigateToPath(navigationWorkspace_.ActiveContext().currentPath);
                RefreshSelectionPresentation();
                navigationChrome_.Refresh();
                RequestRepaint();
            } else if (commandId == L"navigation.close-tab") {
                if (navigationWorkspace_.CloseActiveTab()) {
                    columnView_.NavigateToPath(navigationWorkspace_.ActiveContext().currentPath);
                    RefreshSelectionPresentation();
                    navigationChrome_.Refresh();
                    RequestRepaint();
                }
            } else if (commandId == L"navigation.next-tab" || commandId == L"navigation.previous-tab") {
                const size_t count = navigationWorkspace_.TabCount();
                if (count > 1) {
                    const size_t current = navigationWorkspace_.ActiveTabIndex();
                    const size_t next = commandId == L"navigation.next-tab"
                        ? (current + 1) % count
                        : (current + count - 1) % count;
                    if (navigationWorkspace_.SwitchTab(next)) {
                        columnView_.NavigateToPath(navigationWorkspace_.ActiveContext().currentPath);
                        RefreshSelectionPresentation();
                        navigationChrome_.Refresh();
                        RequestRepaint();
                    }
                }
            } else if (commandId == L"navigation.reopen-tab") {
                if (navigationWorkspace_.ReopenClosedTab()) {
                    columnView_.NavigateToPath(navigationWorkspace_.ActiveContext().currentPath);
                    RefreshSelectionPresentation();
                    navigationChrome_.Refresh();
                    RequestRepaint();
                }
            } else if (commandId == L"navigation.back" || commandId == L"navigation.forward") {
                const bool moved = commandId == L"navigation.back" ? navigationWorkspace_.GoBack() : navigationWorkspace_.GoForward();
                if (moved) {
                    columnView_.NavigateToPath(navigationWorkspace_.ActiveContext().currentPath);
                    RefreshSelectionPresentation();
                    navigationChrome_.Refresh();
                    RequestRepaint();
                }
            } else if (commandId == L"navigation.toggle-column-view" || commandId == L"navigation.toggle-dual-pane") {
                if (navigationWorkspace_.IsDualPane()) navigationWorkspace_.DisableDualPane();
                else navigationWorkspace_.EnableDualPane();
                navigationChrome_.Refresh();
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
    const auto shortcutPath = ShortcutSettingsPath();
    if (shortcutPath.empty()) shortcuts_.ResetDefaults(commandRegistry_);
    else shortcuts_.Load(shortcutPath, commandRegistry_, [](const std::wstring& message) { OutputDebugStringW((message + L"\n").c_str()); });
    if (!commandPalette_.Initialize(hwnd_, &commandRegistry_, &shortcuts_,
                                    [this](const std::wstring& id) { InvokeCommand(id); })) return false;
    return searchPanel_.Initialize(hwnd_, &engineClient_, [this](const ffsearch::Candidate& candidate,
                                                                  const ffsearch::PathReconstruction& path) {
        const std::wstring navigationPath = candidate.isDirectory
            ? (std::filesystem::path(candidate.folder) / candidate.name).wstring()
            : candidate.folder;
        if (!navigationPath.empty()) navigationWorkspace_.Navigate(navigationPath);
        if (path.complete) columnView_.NavigateToHierarchy(path.segments, candidate.isDirectory);
        else columnView_.NavigateToHierarchy((std::filesystem::path(candidate.folder) / candidate.name).wstring(), candidate.isDirectory);
        RefreshSelectionPresentation();
        navigationChrome_.Refresh();
        RequestRepaint();
    }, settings_.retainSearchHistory);
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
    const int x = navigationSidebar_.Width() + static_cast<int>(columnView_.FocusedColumnIndex() * ColumnView::kColumnWidth - scrollOffset_ + 28.0f);
    const int itemIndex = (std::max)(0, columnView_.FocusedItemIndex());
    const int y = kNavigationChromeHeight + static_cast<int>(ColumnView::kBadgeHeight + itemIndex * ColumnView::kRowHeight);
    inlineRename_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", std::filesystem::path(path).filename().c_str(),
                                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, x, y, 205, 24, hwnd_,
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

bool WindowShell::InvokeCommand(const std::wstring& commandId) {
    const CommandContext context = CurrentCommandContext();
    const auto paths = columnView_.ActiveSelectionPaths();
    if (!commandRegistry_.Invoke(commandId, context, paths)) {
        MessageBoxW(hwnd_, L"That command is not available for the current selection.", L"Command unavailable", MB_OK | MB_ICONINFORMATION);
        return false;
    }
    return true;
}

bool WindowShell::DispatchShortcut(const MSG& message, ShortcutScope scope) {
    if (message.message != WM_KEYDOWN && message.message != WM_SYSKEYDOWN) return false;
    uint8_t modifiers = ModifierNone;
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) modifiers |= ModifierControl;
    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) modifiers |= ModifierShift;
    if ((GetKeyState(VK_MENU) & 0x8000) != 0) modifiers |= ModifierAlt;
    const ShortcutBinding* binding = shortcuts_.FindByChord({static_cast<uint16_t>(message.wParam), modifiers}, scope);
    return binding != nullptr && InvokeCommand(binding->commandId);
}

void WindowShell::ShowContextMenu(POINT screenPoint) {
    const CommandContext context = CurrentCommandContext();
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
        const bool enabled = !command->enabledPredicate || command->enabledPredicate(context);
        const UINT id = kContextCommandBase + static_cast<UINT>(commandIds.size());
        AppendMenuW(menu, MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED), id, command->displayName.c_str());
        commandIds.push_back(command->commandId);
        previousCategory = command->category;
    }
    const UINT selected = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                           screenPoint.x, screenPoint.y, hwnd_, nullptr);
    DestroyMenu(menu);
    if (selected >= kContextCommandBase && selected < kContextCommandBase + commandIds.size()) {
        InvokeCommand(commandIds[selected - kContextCommandBase]);
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
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &WindowShell::WndProcThunk;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr; // Direct2D owns painting
    windowClass.lpszClassName = kWindowClassName;
    if (RegisterClassExW(&windowClass) == 0) {
        return false;
    }

    hwnd_ = CreateWindowExW(
        0, kWindowClassName, L"FastFiles", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1024, 640,
        nullptr, nullptr, instance, this);
    if (hwnd_ == nullptr) {
        return false;
    }

    HMENU menu = CreateMenu();
    HMENU settingsMenu = CreatePopupMenu();
    AppendMenuW(settingsMenu, MF_STRING, kMenuThemeLight, L"Theme: Light");
    AppendMenuW(settingsMenu, MF_STRING, kMenuThemeDark, L"Theme: Dark");
    AppendMenuW(settingsMenu, MF_STRING, kMenuThemeSystem, L"Theme: Follow System");
    AppendMenuW(settingsMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(settingsMenu, MF_STRING, kMenuPreviewEnabled, L"Enable Preview Pane");
    AppendMenuW(settingsMenu, MF_STRING, kMenuPreview1Mb, L"Preview Limit: 1 MB");
    AppendMenuW(settingsMenu, MF_STRING, kMenuPreview16Mb, L"Preview Limit: 16 MB");
    AppendMenuW(settingsMenu, MF_STRING, kMenuPreview64Mb, L"Preview Limit: 64 MB");
    AppendMenuW(settingsMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(settingsMenu, MF_STRING, kMenuForgetUnavailableDrive, L"Forget Unavailable Drive…");
    AppendMenuW(settingsMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(settingsMenu, MF_STRING, kMenuResetSettings, L"Reset Settings to Defaults");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(settingsMenu), L"Settings");
    AppendMenuW(menu, MF_STRING | MF_GRAYED, kMenuOperationDetails, L"Last Operation Errors…");
    SetMenu(hwnd_, menu);

    if (!renderer_.Initialize(hwnd_)) {
        return false;
    }
    settings_ = ffprotocol::LoadSettings();
    ApplyTheme();
    columnView_.Initialize(&engineClient_);
    if (!navigationChrome_.Initialize(hwnd_, &navigationWorkspace_, [this](const std::wstring& path) {
            columnView_.NavigateToPath(path);
            RefreshSelectionPresentation();
            RequestRepaint();
        })) return false;
    if (!navigationSidebar_.Initialize(hwnd_, &navigationWorkspace_, [this](const NavigationSidebar::NavigationTarget& target) {
            if (target.path.empty()) {
                columnView_.ShowUnavailableLocation(target.displayName);
                RefreshSelectionPresentation();
                RequestRepaint();
                return;
            }
            if (target.openInNewTab) navigationWorkspace_.OpenTab(target.path);
            else navigationWorkspace_.Navigate(target.path);
            columnView_.NavigateToPath(target.path);
            RefreshSelectionPresentation();
            navigationChrome_.Refresh();
            navigationSidebar_.Refresh();
            RequestRepaint();
        })) return false;
    if (!fileOperations_.Start(hwnd_)) return false;
    if (!InitializeCommands()) return false;
    if (!storageAnalysis_.Initialize(hwnd_, &engineClient_,
        [this](const std::wstring& path) {
            if (!path.empty()) navigationWorkspace_.Navigate(path);
        },
        [this]() {
            storageAnalysis_.Hide();
            RequestRepaint();
        })) return false;
    IDropTarget* dropTarget = nullptr;
    if (FAILED(CreateFileDropTarget(
            [this] { return columnView_.ActivePanePath(); },
            [this](std::vector<std::wstring> paths, DWORD effect) {
                const std::wstring destination = columnView_.ActivePanePath();
                if (destination.empty()) return;
                if (effect == DROPEFFECT_LINK) {
                    FileOperationRequest request{};
                    request.kind = FileOperationKind::Link;
                    request.sources = std::move(paths);
                    request.destination = destination;
                    const auto exists = [](const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; };
                    for (const auto& source : request.sources) {
                        std::wstring name = std::filesystem::path(source).filename().wstring() + L".lnk";
                        if (exists((std::filesystem::path(destination) / name).wstring())) {
                            name = GenerateKeepBothName(destination, name, exists);
                        }
                        request.transferPlan.push_back({source, name, false});
                    }
                    fileOperations_.Enqueue(std::move(request));
                } else {
                    QueueTransfer(paths, destination, effect == DROPEFFECT_MOVE ? FileOperationKind::Move : FileOperationKind::Copy);
                }
            }, &dropTarget)) || FAILED(RegisterDragDrop(hwnd_, dropTarget))) {
        if (dropTarget != nullptr) dropTarget->Release();
        return false;
    }
    dropTarget_.Attach(dropTarget);

    engineClient_.Start(
        [this] {
            columnView_.OnSnapshotUpdated();
            storageAnalysis_.OnSnapshotUpdated();
            PostMessageW(hwnd_, WM_APP_REPAINT, 0, 0);
        },
        [this](bool active) {
            columnView_.SetEngineStatus(active);
            PostMessageW(hwnd_, WM_APP_ENGINE_STATUS, active ? 1 : 0, 0);
        },
        [this](const std::wstring& path, ffprotocol::DirectoryErrorReason reason) {
            columnView_.OnDirectoryError(path, reason);
            PostMessageW(hwnd_, WM_APP_REPAINT, 0, 0);
        });

    engineClient_.RequestFolderAggregate(0, 0, 0, [this](uint64_t requestId, ffprotocol::FolderAggregateStatus status, uint64_t itemCount, uint64_t totalSizeBytes) {
        auto payload = std::make_unique<ffprotocol::FolderAggregateResultPayload>(ffprotocol::FolderAggregateResultPayload{requestId, status, itemCount, totalSizeBytes});
        const HWND target = hwnd_;
        if (target != nullptr
            && PostMessageW(target, WM_APP_FOLDER_AGGREGATE, 0,
                            reinterpret_cast<LPARAM>(payload.get()))) {
            payload.release();
        }
    });

    ShowWindow(hwnd_, showCommand);
    UpdateWindow(hwnd_);
    SetTimer(hwnd_, kWorkspaceSaveTimer, 500, nullptr);
    return true;
}

int WindowShell::RunMessageLoop() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (commandPalette_.HandleMessage(msg)) continue;
        if (DispatchShortcut(msg, ShortcutScope::Global)) continue;
        if (!commandPalette_.Visible() && msg.hwnd != inlineRename_ && DispatchShortcut(msg, ShortcutScope::ActiveView)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    navigationWorkspace_.FlushState();
    engineClient_.Stop();
    fileOperations_.Stop();
    return static_cast<int>(msg.wParam);
}

void WindowShell::RequestRepaint() {
    InvalidateRect(hwnd_, nullptr, FALSE);
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
    // Theme and DPI/device loss all invalidate device-dependent brushes;
    // ColumnView recreates them lazily in the normal paint path.
    columnView_.SetDarkTheme(dark);
    RequestRepaint();
}

void WindowShell::SaveAndNotifySettings() {
    if (ffprotocol::SaveSettings(settings_)) {
        engineClient_.ReloadIndexingConfig();
    }
}

void WindowShell::Render() {
    RECT clientRect{};
    GetClientRect(hwnd_, &clientRect);
    D2D1_SIZE_F viewportSize = D2D1::SizeF(
        NavigationViewportWidth(),
        static_cast<float>((std::max)(0L, clientRect.bottom - clientRect.top - kNavigationChromeHeight)));

    ID2D1DeviceContext* context = renderer_.BeginFrame();
    context->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(navigationSidebar_.Width()), static_cast<float>(kNavigationChromeHeight)));
    columnView_.Render(context, renderer_.DWriteFactory(), viewportSize, scrollOffset_, columnView_.ScrollOffset2());
    RenderDetails(context, viewportSize);
    context->SetTransform(D2D1::Matrix3x2F::Identity());
    renderer_.EndFrame();
}

void WindowShell::RefreshSelectionPresentation() {
    previewBitmap_.Reset();
    const auto selection = columnView_.CurrentSelection();
    std::lock_guard<std::mutex> lock(previewMutex_);
    preview_ = {};
    if (!settings_.previewEnabled || !selection || selection->isDirectory || selection->sizeBytes > settings_.maxAutoPreviewBytes) {
        previewController_.Clear();
        ++activePreviewRequest_;
        return;
    }
    RequestFileType(selection->path);
    ++activePreviewRequest_;
    previewController_.Request(*selection);
}

void WindowShell::RequestFileType(const std::wstring& path) {
    {
        std::lock_guard<std::mutex> lock(fileTypeMutex_);
        if (fileTypes_.contains(path)) return;
        fileTypes_.emplace(path, L"");
    }
    fileTypeWork_.erase(std::remove_if(fileTypeWork_.begin(), fileTypeWork_.end(), [](std::future<void>& work) {
        return work.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }), fileTypeWork_.end());
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
        if (hwnd_ != nullptr) PostMessageW(hwnd_, WM_APP_REPAINT, 0, 0);
    }));
}

std::wstring WindowShell::FileTypeFor(const std::wstring& path) const {
    const std::wstring extension = ExtensionOf(path);
    const std::wstring fallback = extension.empty() ? std::wstring(L"File") : extension + L" File";
    std::lock_guard<std::mutex> lock(fileTypeMutex_);
    const auto found = fileTypes_.find(path);
    return found == fileTypes_.end() || found->second.empty() ? fallback : found->second;
}

void WindowShell::RenderDetails(ID2D1DeviceContext* context, D2D1_SIZE_F viewportSize) {
    constexpr float kPanelWidth = 300.0f;
    constexpr float kStatusHeight = 26.0f;
    const float left = std::max(0.0f, viewportSize.width - kPanelWidth);
    if (!detailsBrush_) {
        context->CreateSolidColorBrush(D2D1::ColorF(0xF1F3F5), &detailsBrush_);
        context->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &detailsTextBrush_);
        renderer_.DWriteFactory()->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"en-us", &detailsTextFormat_);
        renderer_.DWriteFactory()->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-us", &previewTextFormat_);
        previewTextFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    }
    context->FillRectangle(D2D1::RectF(left, ColumnView::kBadgeHeight, viewportSize.width, viewportSize.height - kStatusHeight), detailsBrush_.Get());
    const SelectionSummary summary = columnView_.CurrentSelectionSummary();
    const auto selection = summary.items.size() == 1 ? std::optional<FileDescriptor>(summary.items.front()) : std::nullopt;
    std::wstring info = L"Properties\n\n";
    if (summary.items.empty()) info += L"No selection";
    else if (summary.items.size() > 1) {
        info += L"Items: " + std::to_wstring(summary.items.size()) + L"\n";
        info += L"Known total size: " + FormatSize(summary.knownSizeBytes);
    }
    else {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        GetFileAttributesExW(selection->path.c_str(), GetFileExInfoStandard, &data);
        const std::wstring extension = ExtensionOf(selection->path);
        const std::wstring extensionLabel = extension.empty() ? std::wstring(L"(none)") : extension;
        const std::wstring typeLabel = FileTypeFor(selection->path);
        info += L"Name: " + FileNameOf(selection->path) + L"\n";
        info += L"Extension: " + extensionLabel + L"\n";
        info += L"Path: " + selection->path + L"\n";
        if (selection->isDirectory) {
            if (selection->volumeRowId != lastAggregateRequestVolumeRowId_ || selection->fileIdLow != lastAggregateRequestFrnLow_ || selection->fileIdHigh != lastAggregateRequestFrnHigh_) {
                lastAggregateRequestVolumeRowId_ = selection->volumeRowId;
                lastAggregateRequestFrnLow_ = selection->fileIdLow;
                lastAggregateRequestFrnHigh_ = selection->fileIdHigh;
                const uint64_t requestId = engineClient_.LastRequestId() + 1;
                pendingAggregateRequestId_ = requestId;
                pendingAggregateStatus_ = ffprotocol::FolderAggregateStatus::Pending;
                pendingAggregateItemCount_ = 0;
                pendingAggregateTotalSize_ = 0;
                engineClient_.RequestFolderAggregate(
                    static_cast<int64_t>(selection->volumeRowId),
                    selection->fileIdLow, selection->fileIdHigh,
                    [this, requestId](uint64_t /*reqId*/, ffprotocol::FolderAggregateStatus status, uint64_t itemCount, uint64_t totalSizeBytes) {
                        auto payload = std::make_unique<ffprotocol::FolderAggregateResultPayload>(ffprotocol::FolderAggregateResultPayload{requestId, status, itemCount, totalSizeBytes});
                        const HWND target = hwnd_;
                        if (target != nullptr
                            && PostMessageW(target, WM_APP_FOLDER_AGGREGATE, 0,
                                            reinterpret_cast<LPARAM>(payload.get()))) {
                            payload.release();
                        }
                    });
            }
            if (pendingAggregateStatus_ == ffprotocol::FolderAggregateStatus::Pending) {
                info += L"Type: Folder\nItems: Calculating…\nTotal size: Calculating…";
            } else if (pendingAggregateStatus_ == ffprotocol::FolderAggregateStatus::Resolved) {
                info += L"Type: Folder\nItems: " + std::to_wstring(pendingAggregateItemCount_) + L"\n";
                info += L"Total size: " + FormatSize(pendingAggregateTotalSize_);
            } else {
                info += L"Type: Folder\nItems: —\nTotal size: —";
            }
        } else {
            info += L"Type: " + typeLabel + L"\n";
            info += L"Size: " + FormatSize(selection->sizeBytes) + L"\n";
            info += L"Created: " + FormatTime(data.ftCreationTime) + L"\n";
            info += L"Modified: " + FormatTime(data.ftLastWriteTime) + L"\n";
            info += L"Accessed: " + FormatTime(data.ftLastAccessTime) + L"\n";
            info += L"Attributes: " + FormatAttributes(selection->attributes);
        }
    }
    context->DrawText(info.c_str(), static_cast<UINT32>(info.size()), detailsTextFormat_.Get(),
        D2D1::RectF(left + 8, ColumnView::kBadgeHeight + 8, viewportSize.width - 8, ColumnView::kBadgeHeight + 120), detailsTextBrush_.Get());
    PreviewResult preview;
    { std::lock_guard<std::mutex> lock(previewMutex_); preview = preview_; }
    const float previewTop = ColumnView::kBadgeHeight + 135;
    if (!settings_.previewEnabled) {
        const std::wstring text = L"Preview disabled";
        context->DrawText(text.c_str(), static_cast<UINT32>(text.size()), previewTextFormat_.Get(),
            D2D1::RectF(left + 8, previewTop, viewportSize.width - 8, viewportSize.height - kStatusHeight - 8), detailsTextBrush_.Get());
    } else if (preview.kind == PreviewKind::Image) {
        if (!previewBitmap_ && !preview.pixels.empty()) {
            const D2D1_BITMAP_PROPERTIES properties = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            context->CreateBitmap(D2D1::SizeU(preview.width, preview.height), preview.pixels.data(), preview.width * 4, &properties, &previewBitmap_);
        }
        if (previewBitmap_) context->DrawBitmap(previewBitmap_.Get(), D2D1::RectF(left + 8, previewTop, viewportSize.width - 8, viewportSize.height - kStatusHeight - 8));
    } else {
        std::wstring text = preview.kind == PreviewKind::Text ? preview.text : (selection && !selection->isDirectory ? L"No preview available" : L"");
        if (preview.truncated) text += L"\n\n[Preview truncated]";
        context->DrawText(text.c_str(), static_cast<UINT32>(text.size()), previewTextFormat_.Get(),
            D2D1::RectF(left + 8, previewTop, viewportSize.width - 8, viewportSize.height - kStatusHeight - 8), detailsTextBrush_.Get());
    }
    const std::wstring status = L"Selected: " + std::to_wstring(summary.items.size()) + L"  •  " +
        FormatSize(summary.knownSizeBytes) + L"  •  " + columnView_.CurrentPath();
    context->FillRectangle(D2D1::RectF(0, viewportSize.height - kStatusHeight, viewportSize.width, viewportSize.height), detailsBrush_.Get());
    context->DrawText(status.c_str(), static_cast<UINT32>(status.size()), detailsTextFormat_.Get(),
        D2D1::RectF(8, viewportSize.height - kStatusHeight, viewportSize.width - 8, viewportSize.height), detailsTextBrush_.Get());
}

void WindowShell::EnsureColumnVisible(int columnIndex, float viewportWidth) {
    const float columnLeft = columnIndex * ColumnView::kColumnWidth;
    const float columnRight = columnLeft + ColumnView::kColumnWidth;
    if (columnView_.IsDualPane()) {
        const float paneWidth = viewportWidth / 2.0f;
        const int pane = columnView_.ActivePane();
        const float paneScroll = pane == 0 ? scrollOffset_ : columnView_.ScrollOffset2();
        if (columnLeft < paneScroll) {
            if (pane == 0) scrollOffset_ = columnLeft;
            else columnView_.SetScrollOffset2(columnLeft);
        } else if (columnRight > paneScroll + paneWidth) {
            const float newScroll = columnRight - paneWidth;
            if (pane == 0) scrollOffset_ = newScroll;
            else columnView_.SetScrollOffset2(newScroll);
        }
        const float maxScroll = std::max(0.0f, columnView_.PaneContentWidth(pane) - paneWidth);
        const float clamped = std::clamp(pane == 0 ? scrollOffset_ : columnView_.ScrollOffset2(), 0.0f, maxScroll);
        if (pane == 0) scrollOffset_ = clamped;
        else columnView_.SetScrollOffset2(clamped);
    } else {
        if (columnLeft < scrollOffset_) {
            scrollOffset_ = columnLeft;
        } else if (columnRight > scrollOffset_ + viewportWidth) {
            scrollOffset_ = columnRight - viewportWidth;
        }
        const float maxScroll = std::max(0.0f, columnView_.ContentWidth() - viewportWidth);
        scrollOffset_ = std::clamp(scrollOffset_, 0.0f, maxScroll);
    }
}

LRESULT WindowShell::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_SIZE: {
            const UINT width = LOWORD(lParam);
            const UINT height = HIWORD(lParam);
            renderer_.Resize(width, height);
            navigationChrome_.Reposition();
            navigationSidebar_.Reposition();
            commandPalette_.Reposition();
            searchPanel_.Reposition();
            storageAnalysis_.Reposition();
            RequestRepaint();
            return 0;
        }

        case WM_DPICHANGED: {
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            renderer_.Resize(static_cast<UINT>(suggested->right - suggested->left), static_cast<UINT>(suggested->bottom - suggested->top));
            RequestRepaint();
            return 0;
        }

        case WM_SETTINGCHANGE:
            if (settings_.theme == ffprotocol::ThemePreference::FollowSystem) ApplyTheme();
            navigationWorkspace_.ReResolveKnownFolders();
            navigationSidebar_.Refresh();
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

        case WM_APP_ENGINE_STATUS:
            engineActive_ = wParam != 0;
            searchPanel_.SetEngineActive(engineActive_);
            RequestRepaint();
            return 0;

        case WM_APP_SEARCH_COMPLETE:
            searchPanel_.HandleCompletion(lParam);
            return 0;

        case WM_TIMER:
            if (wParam == kWorkspaceSaveTimer) {
                navigationWorkspace_.FlushState();
                return 0;
            }
            if (searchPanel_.HandleTimer(wParam)) return 0;
            return DefWindowProcW(hwnd, message, wParam, lParam);

        case WM_NOTIFY:
            if (searchPanel_.HandleNotify(lParam)) return 0;
            return DefWindowProcW(hwnd, message, wParam, lParam);

        case WM_DRAWITEM:
            if (searchPanel_.HandleDrawItem(lParam)) return TRUE;
            return DefWindowProcW(hwnd, message, wParam, lParam);

        case WM_APP_PREVIEW_READY:
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
            auto* payload = reinterpret_cast<ffprotocol::FolderAggregateResultPayload*>(lParam);
            if (!payload) {
                return 0;
            }
            // Stale-result gating: only accept the response that matches the
            // request the UI still considers current.
            if (payload->requestId == pendingAggregateRequestId_) {
                pendingAggregateStatus_ = payload->status;
                pendingAggregateItemCount_ = payload->itemCount;
                pendingAggregateTotalSize_ = payload->totalSizeBytes;
                RequestRepaint();
            }
            delete payload;
            return 0;
        }

        case WM_APP_STORAGE_AGGREGATE: {
            storageAnalysis_.HandleCompletion(lParam);
            return 0;
        }

        case WM_APP_FILE_OPERATION_CONFLICT: {
            auto* question = reinterpret_cast<FileOperationConflictQuestion*>(lParam);
            if (question == nullptr) return 0;
            const ConflictDecision decision = ShowConflictDialog(hwnd_, question->source, question->destination);
            {
                std::lock_guard lock(question->mutex);
                question->decision = decision;
                question->answered = true;
            }
            question->answeredCondition.notify_one();
            return 0;
        }

        case WM_APP_FILE_OPERATION_EVENT: {
            std::unique_ptr<FileOperationEvent> event(reinterpret_cast<FileOperationEvent*>(lParam));
            if (!event) return 0;
            if (event->kind == FileOperationEventKind::Queued || event->kind == FileOperationEventKind::Started) {
                SetWindowTextW(hwnd_, event->kind == FileOperationEventKind::Queued ? L"FastFiles — operation queued" : L"FastFiles — operation in progress");
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
                lastOperationFailures_ = event->failures;
                EnableMenuItem(GetMenu(hwnd_), kMenuOperationDetails,
                               MF_BYCOMMAND | (lastOperationFailures_.empty() ? MF_GRAYED : MF_ENABLED));
            } else if (event->kind == FileOperationEventKind::Completed || event->kind == FileOperationEventKind::Cancelled) {
                std::wstring title = event->kind == FileOperationEventKind::Cancelled ? L"FastFiles — cancelled" : L"FastFiles — complete";
                title += L" — " + std::to_wstring(event->completed) + L" of " + std::to_wstring(event->total);
                title += L" — " + std::to_wstring(event->failures.size()) + L" failed";
                if (!event->failures.empty()) {
                    title += L" (first: " + event->failures.front().path + L")";
                }
                SetWindowTextW(hwnd_, title.c_str());
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
                    MessageBoxW(hwnd_, L"The deleted item can no longer be restored. It may have been removed from the Recycle Bin.",
                                L"Undo unavailable", MB_OK | MB_ICONWARNING);
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
                columnView_.OnSnapshotUpdated();
                navigationChrome_.Refresh();
                RequestRepaint();
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            D2D1_POINT_2F point = D2D1::Point2F(
                static_cast<float>(GET_X_LPARAM(lParam) - navigationSidebar_.Width()),
                static_cast<float>(GET_Y_LPARAM(lParam) - kNavigationChromeHeight));
            columnView_.OnMouseDown(
                point, scrollOffset_, NavigationViewportWidth(),
                (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            dragOrigin_ = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            dragArmed_ = !columnView_.ActiveSelectionPaths().empty();
            RefreshSelectionPresentation();
            EnsureColumnVisible(columnView_.FocusedColumnIndex(), NavigationViewportWidth());
            const std::wstring activePath = columnView_.ActivePanePath();
            if (!activePath.empty() && activePath != navigationWorkspace_.ActiveContext().currentPath) {
                navigationWorkspace_.Navigate(activePath);
            }
            navigationChrome_.Refresh();
            RequestRepaint();
            return 0;
        }

        case WM_MOUSEMOVE:
            if (dragArmed_ && (wParam & MK_LBUTTON) != 0 &&
                (std::abs(GET_X_LPARAM(lParam) - dragOrigin_.x) >= GetSystemMetrics(SM_CXDRAG) ||
                 std::abs(GET_Y_LPARAM(lParam) - dragOrigin_.y) >= GetSystemMetrics(SM_CYDRAG))) {
                dragArmed_ = false;
                BeginFileDrag(columnView_.ActiveSelectionPaths());
                return 0;
            }
            return DefWindowProcW(hwnd, message, wParam, lParam);

        case WM_LBUTTONUP:
            dragArmed_ = false;
            return 0;

        case WM_RBUTTONDOWN: {
            D2D1_POINT_2F point = D2D1::Point2F(
                static_cast<float>(GET_X_LPARAM(lParam) - navigationSidebar_.Width()),
                static_cast<float>(GET_Y_LPARAM(lParam) - kNavigationChromeHeight));
            columnView_.OnMouseDown(point, scrollOffset_, NavigationViewportWidth(), false, false);
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
            const float viewportWidth = NavigationViewportWidth();
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd_, &pt);
            const float effectiveX = pt.x - static_cast<float>(navigationSidebar_.Width());
            
            if (columnView_.IsDualPane()) {
                const float paneWidth = viewportWidth / 2.0f;
                const int pane = effectiveX < paneWidth ? 0 : 1;
                const float paneScroll = pane == 0 ? scrollOffset_ : columnView_.ScrollOffset2();
                const float maxScroll = std::max(0.0f, columnView_.PaneContentWidth(pane) - paneWidth);
                const float newScroll = std::clamp(paneScroll - static_cast<float>(delta) / 2.0f, 0.0f, maxScroll);
                if (pane == 0) {
                    scrollOffset_ = newScroll;
                } else {
                    columnView_.SetScrollOffset2(newScroll);
                }
            } else {
                const float maxScroll = std::max(0.0f, columnView_.ContentWidth() - viewportWidth);
                scrollOffset_ = std::clamp(scrollOffset_ - static_cast<float>(delta) / 2.0f, 0.0f, maxScroll);
            }
            RequestRepaint();
            return 0;
        }

        case WM_CONTEXTMENU: {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (point.x == -1 && point.y == -1) {
                const int column = columnView_.FocusedColumnIndex();
                const int item = columnView_.FocusedItemIndex();
                point = {static_cast<LONG>(navigationSidebar_.Width() + column * ColumnView::kColumnWidth - scrollOffset_ + 24),
                         static_cast<LONG>(kNavigationChromeHeight + ColumnView::kBadgeHeight + (std::max)(0, item) * ColumnView::kRowHeight + ColumnView::kRowHeight)};
                ClientToScreen(hwnd_, &point);
            }
            ShowContextMenu(point);
            return 0;
        }

        case WM_KEYDOWN: {
            if (wParam == VK_F9) {
                settings_.theme = settings_.theme == ffprotocol::ThemePreference::Light ? ffprotocol::ThemePreference::Dark : ffprotocol::ThemePreference::Light;
                SaveAndNotifySettings();
                ApplyTheme();
                return 0;
            }
            if (wParam == VK_ESCAPE) {
                if (searchPanel_.Visible()) {
                    searchPanel_.Hide();
                    SetFocus(hwnd_);
                    return 0;
                }
                fileOperations_.CancelCurrent();
                SetWindowTextW(hwnd_, L"FastFiles — cancelling operation…");
                return 0;
            }
            columnView_.OnKeyDown(static_cast<int>(wParam));
            RefreshSelectionPresentation();
            const std::wstring activePath = columnView_.ActivePanePath();
            if (!activePath.empty() && activePath != navigationWorkspace_.ActiveContext().currentPath) {
                navigationWorkspace_.Navigate(activePath);
            }
            navigationChrome_.Refresh();
            if (wParam == VK_LEFT || wParam == VK_RIGHT || wParam == VK_RETURN) {
                EnsureColumnVisible(columnView_.FocusedColumnIndex(), NavigationViewportWidth());
            }
            RequestRepaint();
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
                case kMenuPreview64Mb: settings_.maxAutoPreviewBytes = 64ULL * 1024ULL * 1024ULL; break;
                case kMenuForgetUnavailableDrive:
                    engineClient_.RequestUnavailableVolumes([this](auto records) {
                        auto owned = std::make_unique<std::vector<ffprotocol::UnavailableVolumeRecord>>(
                            std::move(records));
                        const HWND target = hwnd_;
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
                        if (!details.empty()) details += L"\n";
                        wchar_t error[16]{};
                        swprintf_s(error, L"0x%08X", static_cast<unsigned int>(failure.error));
                        details += (failure.path.empty() ? L"(operation)" : failure.path) + std::wstring(L" — ") + error;
                    }
                    if (!details.empty()) MessageBoxW(hwnd_, details.c_str(), L"Last operation errors", MB_OK | MB_ICONERROR);
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
            ApplyTheme();
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd_, kWorkspaceSaveTimer);
            navigationWorkspace_.FlushState();
            if (inlineRename_ != nullptr) FinishInlineRename(false);
            RevokeDragDrop(hwnd);
            dropTarget_.Reset();
            fileOperations_.Stop();
            previewController_.Clear();
            hwnd_ = nullptr;
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

} // namespace ffui
