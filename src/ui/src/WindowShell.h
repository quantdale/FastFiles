#pragma once
#include <windows.h>

#include "ColumnView.h"
#include "EngineClient.h"
#include "Renderer.h"

namespace ffui {

// Task 5.1: the window shell -- HWND creation, resize handling, and the
// basic message loop -- plus wiring mouse/keyboard input and repaint
// requests to ColumnView and the engine connection.
class WindowShell {
public:
    bool Initialize(HINSTANCE instance, int showCommand);
    int RunMessageLoop();

private:
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void Render();
    void EnsureColumnVisible(int columnIndex, float viewportWidth);
    void RequestRepaint();

    HWND hwnd_ = nullptr;
    Renderer renderer_;
    ColumnView columnView_;
    EngineClient engineClient_;
    float scrollOffset_ = 0.0f;
};

} // namespace ffui
