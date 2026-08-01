#pragma once
#include <windows.h>
#include <future>
#include <map>

#include "ColumnView.h"
#include "EngineClient.h"
#include "FileOperations.h"
#include "Renderer.h"
#include "ffprotocol/Settings.h"
#include "Preview.h"

namespace ffui {

// Task 5.1: the window shell -- HWND creation, resize handling, and the
// basic message loop -- plus wiring mouse/keyboard input and repaint
// requests to ColumnView and the engine connection.
class WindowShell {
public:
    WindowShell();
    bool Initialize(HINSTANCE instance, int showCommand);
    int RunMessageLoop();

private:
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void Render();
    void EnsureColumnVisible(int columnIndex, float viewportWidth);
    void RequestRepaint();
    void ApplyTheme();
    bool IsSystemDark() const;
    void SaveAndNotifySettings();
    void RefreshSelectionPresentation();
    void RenderDetails(ID2D1DeviceContext* context, D2D1_SIZE_F viewportSize);
    void RequestFileType(const std::wstring& path);
    std::wstring FileTypeFor(const std::wstring& path) const;

    HWND hwnd_ = nullptr;
    Renderer renderer_;
    ColumnView columnView_;
    EngineClient engineClient_;
    FileOperations fileOperations_;
    std::vector<std::wstring> clipboardPaths_;
    std::vector<FileOperationFailure> lastOperationFailures_;
    bool clipboardIsCut_ = false;
    float scrollOffset_ = 0.0f;
    ffprotocol::Settings settings_;
    std::mutex previewMutex_;
    PreviewResult preview_;
    uint64_t activePreviewRequest_ = 0;
    PreviewController previewController_;
    mutable std::mutex fileTypeMutex_;
    std::map<std::wstring, std::wstring> fileTypes_;
    std::vector<std::future<void>> fileTypeWork_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> previewBitmap_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> detailsBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> detailsTextBrush_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> detailsTextFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> previewTextFormat_;
};

} // namespace ffui
