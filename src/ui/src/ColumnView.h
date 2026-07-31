#pragma once
#include <atomic>
#include <d2d1_1.h>
#include <dwrite.h>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <wrl/client.h>

#include "ffprotocol/SnapshotFormat.h"
#include "ffprotocol/UiProtocol.h"

#include "EngineClient.h"

namespace ffui {

struct ColumnItem {
    std::wstring name;
    bool isDirectory = false;
};

enum class ColumnErrorState {
    None,
    AccessDenied,
    NoLongerAvailable,
};

struct Column {
    std::wstring path;
    std::vector<ColumnItem> items;
    int selectedIndex = -1;
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
    void ActivateItem(int columnIndex, int itemIndex);

    // Task 5.10: keyboard navigation. Up/Down move the visual selection
    // within the focused column only (no column population); Left/Right
    // move keyboard focus across existing columns; Enter activates the
    // focused column's current selection (equivalent to ActivateItem).
    void OnKeyDown(int virtualKey);

    // Mouse hit-testing entry point (translates a client-area point into
    // a column/item index and calls ActivateItem).
    void OnMouseDown(D2D1_POINT_2F clientPoint, float scrollOffset);

    void OnSnapshotUpdated();
    void OnDirectoryError(const std::wstring& path, ffprotocol::DirectoryErrorReason reason);
    void SetEngineStatus(bool active);

    void Render(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory, D2D1_SIZE_F viewportSize, float scrollOffset);

    float ContentWidth() const;
    int FocusedColumnIndex() const;
    static constexpr float kColumnWidth = 240.0f;
    static constexpr float kRowHeight = 24.0f;
    static constexpr float kBadgeHeight = 28.0f;

private:
    void TruncateAfter(int columnIndex);
    void RequestColumn(int columnIndex, const std::wstring& path);
    void RefreshColumnFromSnapshot(Column& column, const std::map<std::wstring, ffprotocol::SnapshotDirectory>& snapshot);
    void EnsureCreated(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory);

    EngineClient* engineClient_ = nullptr;
    mutable std::mutex columnsMutex_;
    std::vector<Column> columns_;
    int focusedColumnIndex_ = 0;
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
};

} // namespace ffui
