#include "WindowShell.h"
#include "IconCache.h"
#include "UITheme.h"

#include "ConflictDialog.h"
#include "Util.h"
#include "ffprotocol/Diagnostics.h"

#include <commctrl.h>
#include <commdlg.h>
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
constexpr UINT kMenuExportDiagnostics = 40011;
constexpr UINT kForgetDriveCommandBase = 41000;
constexpr size_t kMaxForgetDriveMenuItems = 1000;
constexpr UINT WM_APP_PREVIEW_READY = WM_APP + 3;
constexpr UINT WM_APP_UNAVAILABLE_VOLUMES = WM_APP + 4;
constexpr UINT WM_APP_FORGET_VOLUME_RESULT = WM_APP + 5;
constexpr UINT WM_APP_FOLDER_AGGREGATE = WM_APP + 9;
constexpr UINT WM_APP_ENGINE_STATUS = WM_APP + 7;
constexpr UINT kContextCommandBase = 20000;
constexpr int kNavigationChromeHeight = 72;
constexpr UINT_PTR kWorkspaceSaveTimer = 0xF1F1;
constexpr UINT_PTR kUiScrollAnimTimer = 0xF1F2;

// Scale a DIP metric to physical pixels for Win32 control layout.
int Scaled(int dipValue) {
    return static_cast<int>(ffui::UiScale(static_cast<float>(dipValue)));
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
                settingsDialog_.Show();
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
                    std::vector<std::wstring> savedPaths;
                    int savedFocus = 0;
                    float savedScroll = 0.0f;
                    columnView_.SaveActivePaneState(savedPaths, savedFocus, savedScroll);
                    auto& currentContext = navigationWorkspace_.ActiveContext();
                    currentContext.columnPaths = savedPaths;
                    currentContext.columnSelections = {savedFocus};
                    currentContext.columnScrollOffsets = {savedScroll};
                    
                    const size_t next = commandId == L"navigation.next-tab"
                        ? (current + 1) % count
                        : (current + count - 1) % count;
                    if (navigationWorkspace_.SwitchTab(next)) {
                        const auto& newContext = navigationWorkspace_.ActiveContext();
                        if (!newContext.columnPaths.empty()) {
                            columnView_.RestoreActivePaneState(newContext.columnPaths,
                                newContext.columnSelections.empty() ? 0 : newContext.columnSelections[0],
                                newContext.columnScrollOffsets.empty() ? 0.0f : newContext.columnScrollOffsets[0]);
                        } else {
                            columnView_.NavigateToPath(newContext.currentPath);
                        }
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
    const int x = static_cast<int>(ffui::UiScale(navigationSidebar_.Width() + columnView_.FocusedColumnIndex() * ColumnView::kColumnWidth - scrollOffset_ + 28.0f));
    const int itemIndex = (std::max)(0, columnView_.FocusedItemIndex());
    const int y = static_cast<int>(ffui::UiScale(kNavigationChromeHeight + ColumnView::kBadgeHeight + itemIndex * ColumnView::kRowHeight));
    inlineRename_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", std::filesystem::path(path).filename().c_str(),
                                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, x, y, Scaled(205), Scaled(24), hwnd_,
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
    AppendMenuW(settingsMenu, MF_STRING, kMenuExportDiagnostics, L"Export Diagnostics Bundle…");
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
    // The icon cache posts WM_APP_ICON_READY to hwnd_ when a type icon resolves;
    // the shell repaints on that message. The column view draws the cached
    // bitmap (16 DIP) with a themed glyph placeholder until it arrives.
    iconCache_ = std::make_unique<ffui::IconCache>(hwnd_);
    columnView_.SetIconCache(iconCache_.get());
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
        },
        [this](const std::wstring& commandId, const std::vector<std::wstring>& /*paths*/) {
            InvokeCommand(commandId);
        })) return false;
    if (!settingsDialog_.Initialize(hwnd_, &settings_, &navigationWorkspace_,
        [this]() {
            SaveAndNotifySettings();
            ApplyTheme();
            navigationChrome_.Refresh();
            RequestRepaint();
        })) return false;
    settingsDialog_.SetEngineClient(&engineClient_);
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
        ffui::PostFolderAggregateResult(hwnd_, WM_APP_FOLDER_AGGREGATE, requestId, status, itemCount, totalSizeBytes);
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
        if (settingsDialog_.HandleMessage(msg)) continue;
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
    ffui::gUiDarkTheme = dark;
    darkTheme_ = dark;
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
    detailsTextFormat_.Reset();
    previewTextFormat_.Reset();
    dividerBrush_.Reset();
    progressBrush_.Reset();
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
    if (ffprotocol::SaveSettings(settings_)) {
        engineClient_.ReloadIndexingConfig();
    }
}

void WindowShell::Render() {
    RECT clientRect{};
    GetClientRect(hwnd_, &clientRect);
    const float clientWidth = static_cast<float>((std::max)(0L, clientRect.right - clientRect.left));
    const float clientHeight = static_cast<float>((std::max)(0L, clientRect.bottom - clientRect.top));
    D2D1_SIZE_F viewportSize = D2D1::SizeF(
        NavigationViewportWidth(),
        static_cast<float>((std::max)(0L, clientRect.bottom - clientRect.top - kNavigationChromeHeight)));

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

    // The eased scroll animations are the authoritative offsets; sync the values
    // the render/hit-test paths read so paints always reflect the eased position.
    // Pane 1's offset lives in ColumnView and is only touched mid-flight so its
    // idle state (tab restore, direct writes) is preserved untouched.
    scrollOffset_ = scrollAnim_.Value();
    if (scrollAnim2_.IsAnimating()) {
        columnView_.SetScrollOffset2(scrollAnim2_.Value());
    }

    context->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(navigationSidebar_.Width()), static_cast<float>(kNavigationChromeHeight)));
    if (storageAnalysis_.Visible() && storageAnalysis_.GetViewMode() == StorageAnalysis::ViewMode::Treemap) {
        storageAnalysis_.RenderTreemap(context, renderer_.DWriteFactory(), viewportSize,
                                       static_cast<float>(navigationSidebar_.Width()),
                                       static_cast<float>(kNavigationChromeHeight));
    } else {
        columnView_.Render(context, renderer_.DWriteFactory(), viewportSize, scrollOffset_, columnView_.ScrollOffset2());
    }
    RenderDetails(context, viewportSize);
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
    constexpr float kProgressHeight = 3.0f;
    const float left = std::max(0.0f, viewportSize.width - kPanelWidth);
    // Elevated details card: the pane floats ~8 DIP inside the viewport as a
    // rounded surfaceElevated card; the status bar stays a full-width strip
    // below it, separated by a subtle divider.
    const float inset = ffui::UiMetrics::kSpaceS;
    const float statusTop = viewportSize.height - kStatusHeight;
    const float cardLeft = left + inset;
    const float cardRight = viewportSize.width - inset;
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
            DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"en-us", &detailsTextFormat_);
        renderer_.DWriteFactory()->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-us", &previewTextFormat_);
        previewTextFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    }
    if (highContrast) {
        // High contrast: suppress the rounded-token styling and fall back to
        // system colors so the user's accessibility palette is never overridden.
        ffui::UiEnsureSolidBrush(context, ffui::ToD2DColor(GetSysColor(COLOR_WINDOW)), &detailsBrush_);
        ffui::UiEnsureSolidBrush(context, ffui::ToD2DColor(GetSysColor(COLOR_WINDOWTEXT)), &detailsTextBrush_);
        context->FillRectangle(D2D1::RectF(cardLeft, cardTop, cardRight, cardBottom), detailsBrush_.Get());
    } else {
        ffui::UiEnsureSolidBrush(context, theme.surfaceElevated, &detailsBrush_);
        ffui::UiEnsureSolidBrush(context, theme.text, &detailsTextBrush_);
        ffui::UiEnsureSolidBrush(context, theme.dividerSubtle, &dividerBrush_);
        ffui::UiFillRoundedRect(context, D2D1::RectF(cardLeft, cardTop, cardRight, cardBottom), detailsBrush_.Get(), ffui::UiMetrics::kRadiusMedium);
    }
    const SelectionSummary summary = columnView_.CurrentSelectionSummary();
    const auto selection = summary.items.size() == 1 ? std::optional<FileDescriptor>(summary.items.front()) : std::nullopt;
    std::wstring info = L"Properties\n\n";
    if (summary.items.empty()) info += L"No selection";
    else if (summary.items.size() > 1) {
        info += L"Items: " + std::to_wstring(summary.items.size()) + L"\n";
        info += L"Known total size: " + ffui::FormatSize(summary.knownSizeBytes);
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
                info += L"Total size: " + ffui::FormatSize(pendingAggregateTotalSize_);
            } else {
                info += L"Type: Folder\nItems: —\nTotal size: —";
            }
        } else {
            info += L"Type: " + typeLabel + L"\n";
            info += L"Size: " + ffui::FormatSize(selection->sizeBytes) + L"\n";
            info += L"Created: " + FormatTime(data.ftCreationTime) + L"\n";
            info += L"Modified: " + FormatTime(data.ftLastWriteTime) + L"\n";
            info += L"Accessed: " + FormatTime(data.ftLastAccessTime) + L"\n";
            info += L"Attributes: " + FormatAttributes(selection->attributes);
        }
    }
    context->DrawText(info.c_str(), static_cast<UINT32>(info.size()), detailsTextFormat_.Get(),
        D2D1::RectF(cardLeft + pad, cardTop + pad, cardRight - pad, cardTop + 120), detailsTextBrush_.Get());
    PreviewResult preview;
    { std::lock_guard<std::mutex> lock(previewMutex_); preview = preview_; }
    const float previewTop = cardTop + 135;
    if (!settings_.previewEnabled) {
        const std::wstring text = L"Preview disabled";
        context->DrawText(text.c_str(), static_cast<UINT32>(text.size()), previewTextFormat_.Get(),
            D2D1::RectF(cardLeft + pad, previewTop, cardRight - pad, cardBottom - pad), detailsTextBrush_.Get());
    } else if (preview.kind == PreviewKind::Image) {
        if (!previewBitmap_ && !preview.pixels.empty()) {
            const D2D1_BITMAP_PROPERTIES properties = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            context->CreateBitmap(D2D1::SizeU(preview.width, preview.height), preview.pixels.data(), preview.width * 4, &properties, &previewBitmap_);
        }
        if (previewBitmap_) context->DrawBitmap(previewBitmap_.Get(), D2D1::RectF(cardLeft + pad, previewTop, cardRight - pad, cardBottom - pad));
    } else {
        std::wstring text = preview.kind == PreviewKind::Text ? preview.text : (selection && !selection->isDirectory ? L"No preview available" : L"");
        if (preview.truncated) text += L"\n\n[Preview truncated]";
        context->DrawText(text.c_str(), static_cast<UINT32>(text.size()), previewTextFormat_.Get(),
            D2D1::RectF(cardLeft + pad, previewTop, cardRight - pad, cardBottom - pad), detailsTextBrush_.Get());
    }
    const std::wstring status = L"Selected: " + std::to_wstring(summary.items.size()) + L"  •  " +
        ffui::FormatSize(summary.knownSizeBytes) + L"  •  " + columnView_.CurrentPath();
    // Status bar: full-width strip under the card, separated by a subtle
    // divider and carrying the slim file-operation progress bar (the primary
    // progress surface; the window title keeps its textual progress too).
    context->FillRectangle(D2D1::RectF(0, statusTop, viewportSize.width, viewportSize.height), detailsBrush_.Get());
    if (!highContrast) {
        context->DrawLine(D2D1::Point2F(0, statusTop), D2D1::Point2F(viewportSize.width, statusTop), dividerBrush_.Get(), 1.0f);
    }
    if (fileOpInProgress_) {
        const float progressWidth = viewportSize.width * std::clamp(fileOpProgress_, 0.0f, 1.0f);
        const D2D1_RECT_F progressRect = D2D1::RectF(0, statusTop + kStatusHeight - kProgressHeight, progressWidth, statusTop + kStatusHeight);
        if (highContrast) {
            ffui::UiEnsureSolidBrush(context, ffui::ToD2DColor(GetSysColor(COLOR_HIGHLIGHT)), &progressBrush_);
            context->FillRectangle(progressRect, progressBrush_.Get());
        } else {
            ffui::UiEnsureSolidBrush(context, theme.accent, &progressBrush_);
            ffui::UiFillRoundedRect(context, progressRect, progressBrush_.Get(), ffui::UiMetrics::kRadiusSmall);
        }
    }
    context->DrawText(status.c_str(), static_cast<UINT32>(status.size()), detailsTextFormat_.Get(),
        D2D1::RectF(8, statusTop, viewportSize.width - 8, viewportSize.height), detailsTextBrush_.Get());
}

void WindowShell::EnsureColumnVisible(int columnIndex, float viewportWidth) {
    const float columnLeft = columnIndex * ColumnView::kColumnWidth;
    const float columnRight = columnLeft + ColumnView::kColumnWidth;
    const uint64_t now = GetTickCount64();
    if (columnView_.IsDualPane()) {
        const float paneWidth = viewportWidth / 2.0f;
        const int pane = columnView_.ActivePane();
        const float paneScroll = pane == 0 ? scrollAnim_.Value()
            : (scrollAnim2_.IsAnimating() ? scrollAnim2_.Value() : columnView_.ScrollOffset2());
        float newScroll = paneScroll;
        if (columnLeft < paneScroll) {
            newScroll = columnLeft;
        } else if (columnRight > paneScroll + paneWidth) {
            newScroll = columnRight - paneWidth;
        }
        const float maxScroll = std::max(0.0f, columnView_.PaneContentWidth(pane) - paneWidth);
        newScroll = std::clamp(newScroll, 0.0f, maxScroll);
        if (pane == 0) {
            scrollAnim_.AnimateTo(newScroll, kUiAnimationDefaultMs, now);
        } else {
            if (!scrollAnim2_.IsAnimating()) scrollAnim2_.Snap(columnView_.ScrollOffset2());
            scrollAnim2_.AnimateTo(newScroll, kUiAnimationDefaultMs, now);
        }
        HandleAnimationTimer(true);
    } else {
        const float paneScroll = scrollAnim_.Value();
        float newScroll = paneScroll;
        if (columnLeft < paneScroll) {
            newScroll = columnLeft;
        } else if (columnRight > paneScroll + viewportWidth) {
            newScroll = columnRight - viewportWidth;
        }
        const float maxScroll = std::max(0.0f, columnView_.ContentWidth() - viewportWidth);
        scrollAnim_.AnimateTo(std::clamp(newScroll, 0.0f, maxScroll), kUiAnimationDefaultMs, now);
        HandleAnimationTimer(true);
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
            // Type icons are bitmap-resolution-dependent; drop the cached
            // bitmaps so they re-resolve at the new DPI scale on the next paint.
            if (iconCache_) {
                iconCache_->Flush();
            }
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
            settingsDialog_.SetEngineActive(engineActive_);
            storageAnalysis_.SetEngineActive(engineActive_);
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

        case WM_NOTIFY:
            if (searchPanel_.HandleNotify(lParam)) return 0;
            return DefWindowProcW(hwnd, message, wParam, lParam);

        case WM_DRAWITEM:
            if (searchPanel_.HandleDrawItem(lParam)) return TRUE;
            return DefWindowProcW(hwnd, message, wParam, lParam);

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
                fileOpInProgress_ = true;
                fileOpProgress_ = 0.0f;
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
                lastOperationFailures_ = event->failures;
                EnableMenuItem(GetMenu(hwnd_), kMenuOperationDetails,
                               MF_BYCOMMAND | (lastOperationFailures_.empty() ? MF_GRAYED : MF_ENABLED));
                fileOpProgress_ = std::clamp(static_cast<float>(event->percent) / 100.0f, 0.0f, 1.0f);
                RequestRepaint();
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
                fileOpInProgress_ = false;
                columnView_.OnSnapshotUpdated();
                navigationChrome_.Refresh();
                RequestRepaint();
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            if (storageAnalysis_.Visible() && storageAnalysis_.GetViewMode() == StorageAnalysis::ViewMode::Treemap) {
                const int x = GET_X_LPARAM(lParam);
                const int y = GET_Y_LPARAM(lParam);
                if (storageAnalysis_.HandleLButtonDown(wParam, MAKELPARAM(x, y))) {
                    return 0;
                }
            }
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

        case WM_MOUSEMOVE: {
            const bool inTreemap = storageAnalysis_.Visible() && storageAnalysis_.GetViewMode() == StorageAnalysis::ViewMode::Treemap;
            if (inTreemap) {
                const int x = GET_X_LPARAM(lParam);
                const int y = GET_Y_LPARAM(lParam);
                if (storageAnalysis_.HandleMouseMove(wParam, MAKELPARAM(x, y))) {
                    RequestRepaint();
                    return 0;
                }
            }
            if (dragArmed_ && (wParam & MK_LBUTTON) != 0 &&
                (std::abs(GET_X_LPARAM(lParam) - dragOrigin_.x) >= GetSystemMetrics(SM_CXDRAG) ||
                 std::abs(GET_Y_LPARAM(lParam) - dragOrigin_.y) >= GetSystemMetrics(SM_CYDRAG))) {
                dragArmed_ = false;
                BeginFileDrag(columnView_.ActiveSelectionPaths());
                return 0;
            }
            // Route hover into the column view (paint-only; hit-testing is
            // unchanged) so rows can animate a hover overlay. TME_LEAVE is
            // re-armed so the leave that must cancel the hover is observed.
            if (!inTreemap) {
                D2D1_POINT_2F point = D2D1::Point2F(
                    static_cast<float>(GET_X_LPARAM(lParam) - navigationSidebar_.Width()),
                    static_cast<float>(GET_Y_LPARAM(lParam) - kNavigationChromeHeight));
                columnView_.OnMouseMove(point, scrollOffset_, NavigationViewportWidth());
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
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (point.x == -1 && point.y == -1) {
                const int column = columnView_.FocusedColumnIndex();
                const int item = columnView_.FocusedItemIndex();
                point = {static_cast<LONG>(ffui::UiScale(navigationSidebar_.Width() + column * ColumnView::kColumnWidth - scrollOffset_ + 24.0f)),
                         static_cast<LONG>(ffui::UiScale(kNavigationChromeHeight + ColumnView::kBadgeHeight + (std::max)(0, item) * ColumnView::kRowHeight + ColumnView::kRowHeight))};
                ClientToScreen(hwnd_, &point);
            }
            if (storageAnalysis_.Visible() && storageAnalysis_.GetViewMode() != StorageAnalysis::ViewMode::Treemap) {
                ScreenToClient(hwnd_, &point);
                if (storageAnalysis_.HandleContextMenu(wParam, MAKELPARAM(point.x, point.y))) {
                    return 0;
                }
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
                    MessageBoxW(hwnd_, ok ? L"Diagnostic bundle exported." : L"Export failed: no diagnostic log found.",
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
            ApplyTheme();
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd_, kWorkspaceSaveTimer);
            KillTimer(hwnd_, kUiScrollAnimTimer);
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
