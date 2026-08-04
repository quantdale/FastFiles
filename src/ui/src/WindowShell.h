#pragma once
#include <windows.h>
#include <filesystem>
#include <future>
#include <map>

#include "ColumnView.h"
#include "CommandPalette.h"
#include "CommandSystem.h"
#include "EngineClient.h"
#include "FileOperations.h"
#include "Renderer.h"
#include "SearchPanel.h"
#include "OleDragDrop.h"
#include "ffprotocol/Settings.h"
#include "Preview.h"
#include "NavigationWorkspace.h"
#include "NavigationChrome.h"
#include "NavigationSidebar.h"
#include "StorageAnalysis.h"
#include "SettingsDialog.h"
#include "Util.h"

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
    static LRESULT CALLBACK InlineRenameProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                             UINT_PTR subclassId, DWORD_PTR referenceData);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void Render();
    void EnsureColumnVisible(int columnIndex, float viewportWidth);
    void RequestRepaint();
    void ApplyTheme();
    bool IsSystemDark() const;
    // settings-and-appearance 6.2: "Show animations in Windows" gate --
    // theme changes apply instantly (the minimal no-animation transition);
    // when system animations are enabled a short non-blocking cross-fade on
    // top-level chrome *may* run, and is skipped entirely when disabled.
    bool SystemAnimationsEnabled() const;
    void SaveAndNotifySettings();
    void RefreshSelectionPresentation();
    void RenderDetails(ID2D1DeviceContext* context, D2D1_SIZE_F viewportSize);
    void RequestFileType(const std::wstring& path);
    std::wstring FileTypeFor(const std::wstring& path) const;
    bool InitializeCommands();
    CommandContext CurrentCommandContext() const;
    bool InvokeCommand(const std::wstring& commandId);
    bool DispatchShortcut(const MSG& message, ShortcutScope scope);
    bool QueueTransfer(const std::vector<std::wstring>& paths, const std::wstring& destination, FileOperationKind kind);
    void BeginInlineRename(const std::wstring& path);
    void FinishInlineRename(bool commit);
    void ShowContextMenu(POINT screenPoint);
    void NavigateWorkspace(const std::wstring& path, const std::wstring& selectName = {});
    float NavigationViewportWidth() const;
    std::filesystem::path ShortcutSettingsPath() const;

    HWND hwnd_ = nullptr;
    Renderer renderer_;
    NavigationWorkspace navigationWorkspace_{L"C:\\"};
    NavigationChrome navigationChrome_;
    NavigationSidebar navigationSidebar_;
    ColumnView columnView_;
    EngineClient engineClient_;
    FileOperations fileOperations_;
    OperationHistory operationHistory_;
    Microsoft::WRL::ComPtr<IDropTarget> dropTarget_;
    CommandRegistry commandRegistry_;
    ShortcutMap shortcuts_;
    CommandPalette commandPalette_;
    SearchPanel searchPanel_;
    StorageAnalysis storageAnalysis_;
    SettingsDialog settingsDialog_;
    bool engineActive_ = false;
    std::vector<std::wstring> clipboardPaths_;
    std::vector<FileOperationFailure> lastOperationFailures_;
    bool clipboardIsCut_ = false;
    bool dragArmed_ = false;
    POINT dragOrigin_{};
    HWND inlineRename_ = nullptr;
    std::wstring inlineRenamePath_;
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
    bool darkTheme_ = false;
    uint64_t pendingAggregateRequestId_ = 0;
    ffprotocol::FolderAggregateStatus pendingAggregateStatus_ = ffprotocol::FolderAggregateStatus::Resolved;
    uint64_t pendingAggregateItemCount_ = 0;
    uint64_t pendingAggregateTotalSize_ = 0;
    int64_t lastAggregateRequestVolumeRowId_ = 0;
    uint64_t lastAggregateRequestFrnLow_ = 0;
    uint64_t lastAggregateRequestFrnHigh_ = 0;
};

} // namespace ffui
