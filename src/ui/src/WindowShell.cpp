#include "WindowShell.h"

#include <algorithm>
#include <windowsx.h>

namespace ffui {

namespace {
constexpr wchar_t kWindowClassName[] = L"FastFilesMainWindow";
constexpr UINT WM_APP_REPAINT = WM_APP + 1;
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

    if (!renderer_.Initialize(hwnd_)) {
        return false;
    }
    columnView_.Initialize(&engineClient_);

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
    return static_cast<int>(msg.wParam);
}

void WindowShell::RequestRepaint() {
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void WindowShell::Render() {
    RECT clientRect{};
    GetClientRect(hwnd_, &clientRect);
    D2D1_SIZE_F viewportSize = D2D1::SizeF(
        static_cast<float>(clientRect.right - clientRect.left),
        static_cast<float>(clientRect.bottom - clientRect.top));

    ID2D1DeviceContext* context = renderer_.BeginFrame();
    columnView_.Render(context, renderer_.DWriteFactory(), viewportSize, scrollOffset_);
    renderer_.EndFrame();
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

        case WM_LBUTTONDOWN: {
            D2D1_POINT_2F point = D2D1::Point2F(
                static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)));
            columnView_.OnMouseDown(point, scrollOffset_);
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

        case WM_KEYDOWN: {
            columnView_.OnKeyDown(static_cast<int>(wParam));
            if (wParam == VK_LEFT || wParam == VK_RIGHT || wParam == VK_RETURN) {
                RECT clientRect{};
                GetClientRect(hwnd, &clientRect);
                const float viewportWidth = static_cast<float>(clientRect.right - clientRect.left);
                EnsureColumnVisible(columnView_.FocusedColumnIndex(), viewportWidth);
            }
            RequestRepaint();
            return 0;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

} // namespace ffui
