#pragma once
#include <atomic>
#include <d2d1_1.h>
#include <dwrite.h>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <wrl/client.h>

#include "ffprotocol/SnapshotFormat.h"
#include "ffprotocol/UiProtocol.h"

#include "EngineClient.h"
#include "Preview.h"
#include "CommandSystem.h"

namespace ffui {

struct ColumnItem {
    std::wstring name;
    bool isDirectory = false;
    uint64_t sizeBytes = 0;
    uint32_t attributes = 0;
    uint64_t fileIdLow = 0;
    uint64_t fileIdHigh = 0;
    int64_t volumeRowId = 0;
};

struct SelectionSummary {
    std::vector<FileDescriptor> items;
    uint64_t knownSizeBytes = 0;
};

enum class ColumnErrorState {
    None,
    AccessDenied,
    NoLongerAvailable,
};

struct Column {
    std::wstring path;
    std::vector<ColumnItem> items;
    std::set<int> selectedIndices;
    int selectionAnchor = -1;
    int focusIndex = -1;
    ColumnErrorState error = ColumnErrorState::None;
};

// Tasks 5.3-5.10: the Finder-style multi-column navigation model and its
// Direct2D/DirectWrite rendering, reading directory contents through
// EngineClient's degraded-mode-backed snapshot (design.md D5).
class ColumnView {
public:
    void Initialize(EngineClient* engineClient);

    // Task 5.3/5.4/5.5: selecting an item in a column. Folders truncate
    // and (re)populate the next column; files never create a new column.
    // This is the "full" selection used by mouse clicks and Enter.
    void ActivateItem(int columnIndex, int itemIndex, bool control = false, bool shift = false);

    // Task 5.10: keyboard navigation. Up/Down move the visual selection
    // within the focused column only (no column population); Left/Right
    // move keyboard focus across existing columns; Enter activates the
    // focused column's current selection (equivalent to ActivateItem).
    void OnKeyDown(int virtualKey);

    // Mouse hit-testing entry point (translates a client-area point into
    // a column/item index and calls ActivateItem).
    void OnMouseDown(D2D1_POINT_2F clientPoint, float scrollOffset, float viewportWidth, bool control, bool shift);

    // Dual-pane (task 6.1/6.4): enable/disable the second navigation
    // surface and switch active pane. When disabled, only pane 0 is used.
    void SetDualPane(bool enabled);
    bool IsDualPane() const { return dualPane_; }
    void ActivatePane(int paneIndex);
    int ActivePane() const { return activePane_; }
    const std::vector<Column>& ActiveColumns() const;
    std::vector<Column>& ActiveColumns();

    // Real filesystem paths for the currently active pane selection.  File
    // operations and drag start consume this single selection contract.
    std::vector<std::wstring> ActiveSelectionPaths() const;
    std::vector<SelectionItem> ActiveSelectionItems() const;
    std::wstring ActivePanePath() const;
    std::wstring RootPath() const;
    void SelectAll();
    void RefreshActiveColumn();
    void NavigateToPath(const std::wstring& path, const std::wstring& selectName = {});
    void NavigateToHierarchy(const std::wstring& fullPath, bool isDirectory);
    void NavigateToHierarchy(const std::vector<std::wstring>& segments, bool isDirectory);
    int FocusedItemIndex() const;

    void OnSnapshotUpdated();
    void OnDirectoryError(const std::wstring& path, ffprotocol::DirectoryErrorReason reason);
    void ShowUnavailableLocation(const std::wstring& displayName);
    void SetEngineStatus(bool active);
    void SetDarkTheme(bool dark);
    void SetScrollOffset2(float offset) { scrollOffset2_ = offset; }
    float ScrollOffset2() const { return scrollOffset2_; }

    // Dual-pane support
    void SetDualPane(bool enabled);
    bool IsDualPane() const { return dualPane_; }
    void ActivatePane(int paneIndex);
    int ActivePane() const { return activePane_; }
    std::vector<Column>& ActiveColumns();
    const std::vector<Column>& ActiveColumns() const;
    int& ActiveFocusedColumnIndex();
    float& ActiveScrollOffset();

    void Render(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory, D2D1_SIZE_F viewportSize, float scrollOffset, float scrollOffset2 = 0.0f);

    float ContentWidth() const;
    float PaneContentWidth(int paneIndex) const;
    int FocusedColumnIndex() const;
    std::optional<FileDescriptor> CurrentSelection() const;
    SelectionSummary CurrentSelectionSummary() const;
    std::wstring CurrentPath() const;
    static constexpr float kColumnWidth = 240.0f;
    static constexpr float kRowHeight = 24.0f;
    static constexpr float kBadgeHeight = 28.0f;

private:
    void TruncateAfter(int columnIndex);
    void RequestColumn(int columnIndex, const std::wstring& path);
    void RefreshColumnFromSnapshot(Column& column, const std::map<std::wstring, ffprotocol::SnapshotDirectory>& snapshot);
    static void SelectSingle(Column& column, int itemIndex);
    static void ToggleSelection(Column& column, int itemIndex);
    static void SelectRange(Column& column, int itemIndex);
    void EnsureCreated(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory);

    EngineClient* engineClient_ = nullptr;
    mutable std::mutex columnsMutex_;
    std::vector<Column> columns_;
    std::vector<Column> columns2_;
    int focusedColumnIndex_ = 0;
    int focusedColumnIndex2_ = 0;
    int activePane_ = 0;
    bool dualPane_ = false;
    std::atomic<bool> engineActive_{false}; // written from EngineClient's background callback, read by Render on the UI thread

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> backgroundBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> borderBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> selectionBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> folderGlyphBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fileGlyphBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> errorBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> badgeActiveBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> badgeDegradedBrush_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> badgeTextFormat_;
    bool resourcesCreated_ = false;
    bool darkTheme_ = false;
    std::wstring pendingSelectionName_;
    float scrollOffset2_ = 0.0f;
};

} // namespace ffui
