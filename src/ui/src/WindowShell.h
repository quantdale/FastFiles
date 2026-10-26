#pragma once
#include <atomic>
#include <windows.h>
#include <filesystem>
#include <functional>
#include <future>
#include <map>

#include "ffui/DetailsLayout.h"
#include "ffui/DetailsAggregateState.h"
#include "ffui/ShellLayout.h"
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
#include "Gallery.h"
#include "IconCache.h"
#include "UiAnimation.h"
#include "UiStyle.h"
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
    void MarkDeviceDirty();
    void HandleAnimationTimer(bool ensureRunning);
    bool IsSystemDark() const;
    void SaveAndNotifySettings();
    void RefreshSelectionPresentation();
    // ui-overhaul phase 3: push the current cut/move intent to the browse
    // surface so cut rows render ghosted; clears on paste, copy, external
    // clipboard changes, and window re-activation.
    void SyncCutGhost();
    // Transient, non-modal status-line message (copy failures, degraded-mode
    // explanations). Auto-expires; never blocks input.
    void ShowStatusNotice(const std::wstring& message);
    void UpdatePreviewLimitMenuChecks();
    void RenderDetails(ID2D1DeviceContext* context, const ffui::DetailsLayout& layout);
    void RequestFileType(const std::wstring& path);
    std::wstring FileTypeFor(const std::wstring& path) const;
    bool InitializeCommands();
    CommandContext CurrentCommandContext() const;
    std::vector<SelectionItem> CurrentCommandSelection() const;
    bool InvokeCommand(const std::wstring& commandId);
    bool DispatchShortcut(const MSG& message, ShortcutScope scope, bool nativeEditFocused);
    bool QueueTransfer(const std::vector<std::wstring>& paths, const std::wstring& destination, FileOperationKind kind);
    void BeginInlineRename(const std::wstring& path);
    void FinishInlineRename(bool commit);
    void ShowContextMenu(POINT screenPoint);
    void NavigateWorkspace(const std::wstring& path, const std::wstring& selectName = {});
    void NavigateColumnViewTo(const std::wstring& path);
    // quick-access-home 5.4: the single Home-aware navigation entry. Keeps the
    // model, the per-pane ColumnView Home kind, and the Home data snapshot in
    // lockstep for both Home and filesystem destinations. A filesystem caller
    // may supply its reconstructed hierarchy; the callback runs synchronously.
    void NavigateToLocation(const NavigationLocation& location,
                            const std::function<void()>& populateFileSystem = {});
    // quick-access-home 5.5: rebuild the Home snapshot from live workspace /
    // search state and push it into ColumnView (no polling).
    void RefreshHomeData();
    void SaveActiveTabViewState();
    void RestoreActiveTabViewState();
    void HideToolWorkspaces();
    void OpenSearchWorkspace();
    void OpenStorageWorkspace();
    void SyncWorkspaceChrome();
    float NavigationViewportWidth() const;
    ffui::MainShellLayout CurrentShellLayout() const;

    // Workers publish completion while WM_DESTROY retires the shell. Atomic
    // publication removes the C++ data race; it does not extend HWND lifetime.
    std::atomic<HWND> hwnd_{nullptr};
    bool uiaReady_ = false;
    Renderer renderer_;
    // Owns the bounded type-icon cache; the column view resolves icons through
    // it (SetIconCache) and the shell repaints on WM_APP_ICON_READY. Created in
    // Initialize once hwnd_ exists because the cache posts to its owner HWND.
    std::unique_ptr<ffui::IconCache> iconCache_;
    // The initial ColumnView surface is the synthetic "This PC" drive root,
    // not C:\\ itself. Keep the workspace sentinel aligned with that model so
    // the first breadcrumb/address state cannot claim a directory that is not
    // the surface currently being rendered. A persisted or configured real
    // path replaces this sentinel during Initialize().
    NavigationWorkspace navigationWorkspace_{L""};
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
    std::vector<std::wstring> paletteTargets_;
    bool paletteStorage_ = false;
    bool paletteSearch_ = false;
    SearchPanel searchPanel_;
    StorageAnalysis storageAnalysis_;
    SettingsDialog settingsDialog_;
    bool engineActive_ = false;
    std::atomic<bool> snapshotRefreshPosted_{false};
    std::atomic<uint64_t> pendingSnapshotGeneration_{0};
    std::vector<std::wstring> clipboardPaths_;
    std::vector<FileOperationFailure> lastOperationFailures_;
    bool clipboardIsCut_ = false;
    // GetClipboardSequenceNumber binds the app-local cut fallback to the exact
    // clipboard publication made by FastFiles. If a clipboard manager strips
    // Preferred DropEffect, a same-sequence path list still preserves Cut;
    // an external clipboard replacement can never inherit that move intent.
    DWORD clipboardSequenceNumber_ = 0;
    bool dragArmed_ = false;
    POINT dragOrigin_{};
    // complete-ole-drag-source-move-effect: set by this window's own drop
    // handler while a drag is in flight, so an intra-app MOVE (already queued
    // through QueueTransfer) is never re-completed as an external source delete.
    bool dragDropHandledInWindow_ = false;
    HWND inlineRename_ = nullptr;
    std::wstring inlineRenamePath_;
    float scrollOffset_ = 0.0f;
    // Animated scroll offsets: the FloatAnimation value IS the authoritative
    // offset; the render/hit-test paths sync from them (see Render).
    ffui::FloatAnimation scrollAnim_;
    ffui::FloatAnimation scrollAnim2_;
    uint64_t lastAnimTickMs_ = 0;
    ffprotocol::Settings settings_;
    std::mutex previewMutex_;
    // Published by the preview worker as an immutable snapshot; RenderDetails
    // only takes the shared_ptr under the lock (never a deep pixel copy).
    std::shared_ptr<const PreviewResult> preview_;
    uint64_t activePreviewRequest_ = 0;
    PreviewController previewController_;
    mutable std::mutex fileTypeMutex_;
    std::map<std::wstring, std::wstring> fileTypes_;
    // original-vision-journey-recovery 4.8: retain unfinished async states;
    // final destruction joins before fileTypes_ and fileTypeMutex_ die.
    std::vector<std::shared_future<void>> fileTypeWork_;
    // Details-pane metadata is fetched once per selection change (not per
    // paint frame); a failed fetch renders "—" rather than epoch-zero times.
    std::wstring attributesCachePath_;
    WIN32_FILE_ATTRIBUTE_DATA attributesCache_{};
    bool attributesCacheValid_ = false;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> previewBitmap_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> detailsBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> detailsTextBrush_;
    // ui-overhaul phase 7: secondary metadata label/brush pair so the details
    // pane carries a real hierarchy (title > values > labels/path).
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> detailsSecondaryBrush_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> detailsTextFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> detailsTitleFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> previewTextFormat_;
    // ui-v3 details: caption format + shared brush cache for the deliberate
    // preview-state panel (DrawUiStatePanel) so non-Ready previews never paint
    // as flat top-left text or empty boxes.
    Microsoft::WRL::ComPtr<IDWriteTextFormat> detailsCaptionFormat_;
    ffui::UiBrushCache detailsStateBrushes_;
    // ui-v3 drag/drop: presentation-only drop-destination emphasis state,
    // driven by the IDropTarget feedback sink and cleared on leave/drop.
    bool dropFeedbackActive_ = false;
    bool dropFeedbackValid_ = false;
    float dropFeedbackXDip = 0.0f;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> dropStrokeBrush_;
    void SetDropFeedback(bool active, POINTL point);
    void DrawDropTargetFeedback(ID2D1DeviceContext* context, const ffui::MainShellLayout& shellLayout);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> dividerBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> progressBrush_;
    bool darkTheme_ = false;
    // The details surface is user-toggleable.  When hidden the navigation
    // viewport receives the reclaimed width; narrow windows may still
    // collapse it to a disclosure rail via DetailsLayout.
    bool detailsPaneVisible_ = true;
    bool galleryMode_ = false;
    bool frameRendered_ = false;
    bool crossFadeActive_ = false;
    uint64_t crossFadeStartMs_ = 0;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> crossFadeBitmap_;
    bool fileOpInProgress_ = false;
    float fileOpProgress_ = 0.0f;
    ffui::FolderAggregateRequestState aggregateRequest_;
    ffprotocol::FolderAggregateStatus pendingAggregateStatus_ = ffprotocol::FolderAggregateStatus::Resolved;
    uint64_t pendingAggregateItemCount_ = 0;
    uint64_t pendingAggregateTotalSize_ = 0;
    // allocated-size-storage-accuracy 6.3: the V2 aggregate payload's
    // size-on-disk fields, carried through to the details pane.
    uint64_t pendingAggregateAllocatedSize_ = 0;
    uint64_t pendingAggregateAllocatedKnownCount_ = 0;
    uint64_t pendingAggregateAllocatedTotalCount_ = 0;
    // Transient status-line notice (message + expiry tick).
    std::wstring statusNotice_;
    uint64_t statusNoticeUntilMs_ = 0;
    // True between a Queued/Started file-operation event and its
    // Completed/Cancelled event; gates the Escape-to-cancel path so an idle
    // Escape never claims an operation is being cancelled.
    bool fileOpOutstanding_ = false;
    // semantic-uia-core-surfaces 2.5: bumped on every geometry push so the
    // provider layer can cheaply validate its cached view.
    uint64_t uiaGeometryRevision_ = 0;
};

} // namespace ffui
