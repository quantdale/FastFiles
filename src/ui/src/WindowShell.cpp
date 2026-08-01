#include "WindowShell.h"

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
    if (!fileOperations_.Start(hwnd_)) return false;

    engineClient_.Start(
        [this] {
            columnView_.OnSnapshotUpdated();
            PostMessageW(hwnd_, WM_APP_REPAINT, 0, 0);
        },
        [this](bool active) {
            columnView_.SetEngineStatus(active);
            PostMessageW(hwnd_, WM_APP_REPAINT, 0, 0);
        },
        [this](const std::wstring& path, ffprotocol::DirectoryErrorReason reason) {
            columnView_.OnDirectoryError(path, reason);
            PostMessageW(hwnd_, WM_APP_REPAINT, 0, 0);
        });

    ShowWindow(hwnd_, showCommand);
    UpdateWindow(hwnd_);
    return true;
}

int WindowShell::RunMessageLoop() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
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
        static_cast<float>(clientRect.right - clientRect.left),
        static_cast<float>(clientRect.bottom - clientRect.top));

    ID2D1DeviceContext* context = renderer_.BeginFrame();
    columnView_.Render(context, renderer_.DWriteFactory(), viewportSize, scrollOffset_);
    RenderDetails(context, viewportSize);
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
            info += L"Type: Folder\nItems: Calculating…\nTotal size: Calculating…";
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
    if (columnLeft < scrollOffset_) {
        scrollOffset_ = columnLeft;
    } else if (columnRight > scrollOffset_ + viewportWidth) {
        scrollOffset_ = columnRight - viewportWidth;
    }
    const float maxScroll = std::max(0.0f, columnView_.ContentWidth() - viewportWidth);
    scrollOffset_ = std::clamp(scrollOffset_, 0.0f, maxScroll);
}

LRESULT WindowShell::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_SIZE: {
            const UINT width = LOWORD(lParam);
            const UINT height = HIWORD(lParam);
            renderer_.Resize(width, height);
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
                // The existing UI-to-engine control pipe has no blocking
                // request/reply path here.  Requesting affected directories
                // is deliberately best effort: it refreshes any live
                // listing while an unavailable engine cannot delay the UI.
                for (const auto& affected : event->affectedPaths) {
                    const std::wstring parent = std::filesystem::path(affected).parent_path().wstring();
                    if (!parent.empty()) engineClient_.RequestDirectory(parent);
                }
                columnView_.OnSnapshotUpdated();
                RequestRepaint();
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            D2D1_POINT_2F point = D2D1::Point2F(
                static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)));
            columnView_.OnMouseDown(
                point, scrollOffset_, (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            RefreshSelectionPresentation();
            RECT clientRect{};
            GetClientRect(hwnd, &clientRect);
            EnsureColumnVisible(columnView_.FocusedColumnIndex(), static_cast<float>(clientRect.right - clientRect.left));
            RequestRepaint();
            return 0;
        }

        case WM_MOUSEWHEEL: {
            const short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            RECT clientRect{};
            GetClientRect(hwnd, &clientRect);
            const float viewportWidth = static_cast<float>(clientRect.right - clientRect.left);
            const float maxScroll = std::max(0.0f, columnView_.ContentWidth() - viewportWidth);
            scrollOffset_ = std::clamp(scrollOffset_ - static_cast<float>(delta) / 2.0f, 0.0f, maxScroll);
            RequestRepaint();
            return 0;
        }

        case WM_CONTEXTMENU: {
            HMENU menu = CreatePopupMenu();
            if (!menu) return 0;
            AppendMenuW(menu, MF_STRING, kMenuCopy, L"Copy");
            AppendMenuW(menu, MF_STRING, kMenuCut, L"Cut");
            AppendMenuW(menu, MF_STRING, kMenuPaste, L"Paste");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, kMenuNewFolder, L"New folder");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, kMenuDelete, L"Delete");
            AppendMenuW(menu, MF_STRING, kMenuPermanentDelete, L"Delete permanently");
            const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                                GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), 0, hwnd_, nullptr);
            DestroyMenu(menu);
            const auto paths = columnView_.ActiveSelectionPaths();
            if (command == kMenuCopy || command == kMenuCut) {
                clipboardPaths_ = paths;
                clipboardIsCut_ = command == kMenuCut;
            } else if (command == kMenuPaste && !clipboardPaths_.empty()) {
                fileOperations_.Enqueue({clipboardIsCut_ ? FileOperationKind::Move : FileOperationKind::Copy,
                                         clipboardPaths_, columnView_.ActivePanePath(), {}, true});
                if (clipboardIsCut_) clipboardPaths_.clear();
            } else if (command == kMenuNewFolder && !columnView_.ActivePanePath().empty()) {
                fileOperations_.Enqueue({FileOperationKind::CreateFolder, {}, columnView_.ActivePanePath(), L"New folder", true});
            } else if (command == kMenuDelete && !paths.empty()) {
                fileOperations_.Enqueue({FileOperationKind::Delete, paths, {}, {}, true});
            } else if (command == kMenuPermanentDelete && !paths.empty()) {
                std::wstring prompt = L"Permanently delete " + std::to_wstring(paths.size()) + L" item(s)? This cannot be undone.\n\n" + paths.front();
                if (paths.size() > 1) prompt += L"\n…";
                if (MessageBoxW(hwnd_, prompt.c_str(), L"Delete permanently", MB_OKCANCEL | MB_DEFBUTTON2 | MB_ICONWARNING) == IDOK) {
                    fileOperations_.Enqueue({FileOperationKind::Delete, paths, {}, {}, false});
                }
            }
            return 0;
        }

        case WM_KEYDOWN: {
            if (wParam == VK_F9) {
                settings_.theme = settings_.theme == ffprotocol::ThemePreference::Light ? ffprotocol::ThemePreference::Dark : ffprotocol::ThemePreference::Light;
                SaveAndNotifySettings();
                ApplyTheme();
                return 0;
            }
            const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (wParam == VK_ESCAPE) {
                fileOperations_.CancelCurrent();
                SetWindowTextW(hwnd_, L"FastFiles — cancelling operation…");
                return 0;
            }
            if (control && (wParam == 'C' || wParam == 'X')) {
                clipboardPaths_ = columnView_.ActiveSelectionPaths();
                clipboardIsCut_ = wParam == 'X';
                return 0;
            }
            if (control && wParam == 'V' && !clipboardPaths_.empty()) {
                fileOperations_.Enqueue({clipboardIsCut_ ? FileOperationKind::Move : FileOperationKind::Copy,
                                         clipboardPaths_, columnView_.ActivePanePath(), {}, true});
                if (clipboardIsCut_) clipboardPaths_.clear();
                return 0;
            }
            if (wParam == VK_DELETE) {
                const auto paths = columnView_.ActiveSelectionPaths();
                std::wstring prompt = L"Permanently delete " + std::to_wstring(paths.size()) + L" item(s)? This cannot be undone.";
                if (!paths.empty()) prompt += L"\n\n" + paths.front() + (paths.size() > 1 ? L"\n…" : L"");
                if (!paths.empty() && (!shift || MessageBoxW(hwnd_, prompt.c_str(),
                    L"Delete permanently", MB_OKCANCEL | MB_DEFBUTTON2 | MB_ICONWARNING) == IDOK)) {
                    fileOperations_.Enqueue({FileOperationKind::Delete, paths, {}, {}, !shift});
                }
                return 0;
            }
            if (control && shift && wParam == 'N') {
                const auto destination = columnView_.ActivePanePath();
                if (!destination.empty()) fileOperations_.Enqueue({FileOperationKind::CreateFolder, {}, destination, L"New folder", true});
                return 0;
            }
            columnView_.OnKeyDown(static_cast<int>(wParam));
            RefreshSelectionPresentation();
            if (wParam == VK_LEFT || wParam == VK_RIGHT || wParam == VK_RETURN) {
                RECT clientRect{};
                GetClientRect(hwnd, &clientRect);
                const float viewportWidth = static_cast<float>(clientRect.right - clientRect.left);
                EnsureColumnVisible(columnView_.FocusedColumnIndex(), viewportWidth);
            }
            RequestRepaint();
            return 0;
        }

        case WM_COMMAND:
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
