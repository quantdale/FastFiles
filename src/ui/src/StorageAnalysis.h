#pragma once

#include <algorithm>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>
#include <windows.h>
#include <commctrl.h>

#include "CategoryEngine.h"
#include "EngineClient.h"
#include "ffui/ShellLayout.h"
#include "ffui/StorageVisibility.h"
#include "ffui/StoragePercentage.h"
#include "ffui/TreemapSemantics.h"
#include "ffprotocol/SnapshotFormat.h"
#include "ffprotocol/UiProtocol.h"
#include "TreemapView.h"
#include "UiComponents.h"
#include "Util.h"

namespace ffui {

constexpr UINT WM_APP_STORAGE_AGGREGATE = WM_APP + 10;

// semantic-treemap-uia 11.x: value-only payload for marshaling a UIA Invoke to
// the owning UI thread. Never carries pointers: the UI thread re-resolves the
// stable identity against CURRENT state and drops it if the tile changed.
constexpr UINT WM_APP_TREEMAP_NAVIGATE = WM_APP + 54;

struct TreemapNavigatePayload {
    uint64_t generation = 0;
    uint64_t scopeHash = 0;
    uint64_t identityHash = 0;
    std::wstring fullPath;
    bool isDirectory = false;
};

namespace storage_chrome_detail {

inline COLORREF Composite(D2D1_COLOR_F overlay, D2D1_COLOR_F base) {
    const float alpha = std::clamp(overlay.a, 0.0f, 1.0f);
    D2D1_COLOR_F result{};
    result.r = base.r * (1.0f - alpha) + overlay.r * alpha;
    result.g = base.g * (1.0f - alpha) + overlay.g * alpha;
    result.b = base.b * (1.0f - alpha) + overlay.b * alpha;
    result.a = 1.0f;
    return ToColorRef(result);
}

inline void DrawOutline(HDC dc, RECT rect, COLORREF color, int width, int radius) {
    if (dc == nullptr || rect.right <= rect.left || rect.bottom <= rect.top) return;
    HPEN pen = CreatePen(PS_SOLID, (std::max)(1, width), color);
    if (pen == nullptr) return;
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius * 2, radius * 2);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

inline void PaintButton(HWND hwnd, HDC dc) {
    RECT rect{};
    if (hwnd == nullptr || dc == nullptr || !GetClientRect(hwnd, &rect)) return;

    const UiTheme theme = GetUiTheme(gUiDarkTheme);
    const LRESULT state = SendMessageW(hwnd, BM_GETSTATE, 0, 0);
    const bool enabled = IsWindowEnabled(hwnd) != FALSE;
    const bool checked = SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const bool pressed = (state & BST_PUSHED) != 0;
    const bool hot = (state & BST_HOT) != 0;
    const bool focused = GetFocus() == hwnd;

    D2D1_COLOR_F base = theme.surfaceElevated;
    COLORREF fill = ToColorRef(base);
    if (checked) {
        fill = Composite(theme.selectionSoft, base);
    } else if (pressed) {
        fill = Composite(theme.pressOverlay, base);
    } else if (hot) {
        fill = Composite(theme.hoverOverlay, base);
    }

    const int radius = (std::max)(1, static_cast<int>(UiScale(UiMetrics::kRadiusSmall) + 0.5f));
    UiGdiRoundedRect(dc, rect, fill, radius);
    DrawOutline(dc, rect, checked ? ToColorRef(theme.accent) : ToColorRef(theme.border),
                1, radius);

    // Selected storage mode gets a shape cue in addition to color so the
    // current mode remains obvious under reduced color discrimination.
    if (checked) {
        RECT marker = rect;
        marker.left += (std::max)(2, static_cast<int>(UiScale(3.0f)));
        marker.right = marker.left + (std::max)(2, static_cast<int>(UiScale(3.0f)));
        marker.top += (std::max)(4, static_cast<int>(UiScale(6.0f)));
        marker.bottom -= (std::max)(4, static_cast<int>(UiScale(6.0f)));
        HBRUSH markerBrush = CreateSolidBrush(ToColorRef(theme.accent));
        if (markerBrush != nullptr) {
            FillRect(dc, &marker, markerBrush);
            DeleteObject(markerBrush);
        }
    }

    wchar_t label[128]{};
    GetWindowTextW(hwnd, label, static_cast<int>(std::size(label)));
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
    HFONT oldFont = font != nullptr ? static_cast<HFONT>(SelectObject(dc, font)) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, enabled ? ToColorRef(theme.text) : ToColorRef(theme.textDisabled));
    RECT textRect = rect;
    if (checked) textRect.left += (std::max)(6, static_cast<int>(UiScale(8.0f)));
    DrawTextW(dc, label, -1, &textRect,
              DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    if (oldFont != nullptr) SelectObject(dc, oldFont);

    if (focused) {
        RECT focus = rect;
        const int inset = (std::max)(2, static_cast<int>(UiScale(2.0f)));
        InflateRect(&focus, -inset, -inset);
        DrawOutline(dc, focus, ToColorRef(theme.focusStroke),
                    (std::max)(1, static_cast<int>(UiScale(2.0f))), radius);
    }
}

inline LRESULT CALLBACK ButtonProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                   UINT_PTR subclassId, DWORD_PTR refData) {
    UNREFERENCED_PARAMETER(subclassId);
    UNREFERENCED_PARAMETER(refData);

    // High Contrast always keeps native system rendering. The subclass exists
    // only to remove mismatched light chrome from normal light/dark themes.
    if (UiSystemHighContrast()) return DefSubclassProc(hwnd, message, wParam, lParam);

    switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            PaintButton(hwnd, dc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_PRINTCLIENT:
            PaintButton(hwnd, reinterpret_cast<HDC>(wParam));
            return 0;
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
            const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
            InvalidateRect(hwnd, nullptr, FALSE);
            return result;
        }
        case WM_MOUSELEAVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
        case WM_ENABLE:
        case BM_SETCHECK:
        case WM_SETTEXT:
        case WM_THEMECHANGED: {
            const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
            InvalidateRect(hwnd, nullptr, FALSE);
            return result;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, ButtonProc, 0xF572);
            return DefSubclassProc(hwnd, message, wParam, lParam);
        default:
            return DefSubclassProc(hwnd, message, wParam, lParam);
    }
}

} // namespace storage_chrome_detail

// HWND-compatible wrapper that automatically applies the Storage chrome
// subclass at creation time. It keeps the existing StorageAnalysis.cpp control
// construction/command routing untouched while making the last native-light
// buttons consume FastFiles theme tokens. The underlying windows remain real
// BUTTON controls, preserving native radio grouping, keyboard, focus and UIA.
class ThemedStorageButton {
public:
    ThemedStorageButton() = default;
    ThemedStorageButton(const ThemedStorageButton&) = delete;
    ThemedStorageButton& operator=(const ThemedStorageButton&) = delete;

    ThemedStorageButton& operator=(HWND hwnd) {
        if (value_ != nullptr && IsWindow(value_)) {
            RemoveWindowSubclass(value_, storage_chrome_detail::ButtonProc, 0xF572);
        }
        value_ = hwnd;
        if (value_ != nullptr) {
            SetWindowSubclass(value_, storage_chrome_detail::ButtonProc, 0xF572, 0);
        }
        return *this;
    }

    operator HWND() const noexcept { return value_; }
    explicit operator bool() const noexcept { return value_ != nullptr; }
    bool operator!() const noexcept { return value_ == nullptr; }

private:
    HWND value_ = nullptr;
};

class StorageAnalysis : public TreemapUiaSurface {
public:
    StorageAnalysis() = default;
    ~StorageAnalysis();
    bool Initialize(HWND owner, EngineClient* engine,
                    std::function<void(const std::wstring& path, bool isDirectory)> navigate,
                    std::function<void()> close,
                    std::function<void(const std::wstring& commandId, const std::vector<std::wstring>& paths)> invokeCommand);
    // Applies persisted category definitions to the category view and its
    // filter. Safe before or after controls are created.
    void SetCategories(const std::vector<std::pair<std::wstring, std::wstring>>& categories);
    void ShowAndFocus(const std::wstring& currentPath, bool engineActive,
                      const ShellLayoutRect& surfaceRect);
    void Hide();
    bool Visible() const { return visible_; }
    void Reposition(const ShellLayoutRect& surfaceRect);
    bool HandleOwnerCommand(WPARAM wParam, LPARAM lParam);
    bool HandleNotify(LPARAM lParam);
    // ui-v3 hardening: NM_CUSTOMDRAW for the overview/drill-down list headers
    // so dark mode never shows system-white SysHeader32 bands. Writes the
    // CDRF_* value the WM_NOTIFY handler must return; false keeps the native
    // header (High Contrast or non-header notifications).
    bool HandleListHeaderCustomDraw(LPARAM lParam, LRESULT* result);
    bool HandleContextMenu(WPARAM wParam, LPARAM lParam);
    bool HandleCompletion(LPARAM lParam);
    bool HandleTreemapCompletion(LPARAM lParam);
    bool HandleTreemapUiaNavigate(LPARAM lParam);
    bool HandleMouseMove(WPARAM wParam, LPARAM lParam);
    bool HandleLButtonDown(WPARAM wParam, LPARAM lParam);
    void SetEngineActive(bool active);
    void SetDarkTheme(bool dark);
    void OnSnapshotUpdated();
    void RenderTreemap(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory, D2D1_SIZE_F viewportSize, float offsetX, float offsetY);

    // semantic-treemap-uia 30.x: the provider controller subscribes here;
    // notifications are pushed after the view released its locks.
    void SetUiaEventSink(UiaEventSink sink, void* context) { uiaEvents_.Set(sink, context); }

    using ViewMode = StorageViewMode;
    void SetViewMode(ViewMode mode);
    ViewMode GetViewMode() const { return viewMode_; }

    // --- TreemapUiaSurface (semantic-treemap-uia 6.x): the provider consumes
    // the same bounded semantic projection the renderer and hit-tester use. ---
    bool TreemapActive() const override { return treemapView_.UiaActive(); }
    uint64_t TreemapGeneration() const override { return treemapView_.UiaGeneration(); }
    uint64_t TreemapScopeHash() const override { return treemapView_.UiaScopeHash(); }
    std::wstring TreemapScopePath() const override { return treemapView_.UiaScopePath(); }
    int TreemapTileCount() const override { return treemapView_.UiaTileCount(); }
    TreemapUiaItemInfo TreemapTileInfo(int index) const override { return treemapView_.UiaTileInfo(index); }
    int TreemapTileIndex(uint64_t identityHash) const override { return treemapView_.UiaTileIndex(identityHash); }
    bool TreemapFindTile(uint64_t identityHash, TreemapUiaItemInfo& out) const override {
        return treemapView_.UiaFindTile(identityHash, out);
    }
    TreemapUiaItemInfo TreemapTileHitTest(float normalizedX, float normalizedY) const override {
        return treemapView_.UiaTileHitTest(normalizedX, normalizedY);
    }
    void TreemapActivateItem(const TreemapUiaItemInfo& item) override;
    UiaEventRouter* TreemapEvents() override { return &uiaEvents_; }

private:
    struct DrillItem {
        std::wstring name;
        bool isDirectory = false;
        uint64_t sizeBytes = 0;
        uint64_t totalSizeBytes = 0; // subtree aggregate when known
        // allocated-size-storage-accuracy 7.1: the item's own size-on-disk
        // (files) plus the subtree's allocated aggregate with its explicit
        // completeness pair (known/total counts) for directories.
        uint64_t allocatedSizeBytes = 0;
        bool allocatedSizeKnown = false;
        uint64_t totalAllocatedSizeBytes = 0;
        uint64_t allocatedKnownCount = 0;
        uint64_t allocatedTotalCount = 0;
        uint32_t attributes = 0;
        uint64_t creationTime = 0;
        uint64_t lastModifiedTime = 0;
        int64_t volumeRowId = 0;
        uint64_t fileIdLow = 0;
        uint64_t fileIdHigh = 0;
        uint64_t parentFrnLow = 0;
        uint64_t parentFrnHigh = 0;
        bool calculating = false;
        bool aggregateFailed = false;
        uint64_t requestId = 0;
    };

    // storage-analysis 2.1-2.4: one row of the storage overview.
    struct VolumeItem {
        std::wstring rootPath;
        bool unavailable = false;
        bool fullyIndexed = false;
        uint64_t totalBytes = 0;
        uint64_t freeBytes = 0;
        uint64_t usedBytes = 0;
    };

    struct PendingRequest {
        uint64_t requestId = 0;
        size_t itemIndex = 0;
    };

    void RefreshData();
    void RefreshOverview();
    void ApplyModeVisibility();
    void RequestAggregateForItem(size_t index);
    void RequestAggregateForParent(const ffprotocol::SnapshotDirectory& directory);
    std::pair<uint64_t, bool> CurrentParentTotals() const;
    void HandleAggregateResult(uint64_t requestId, ffprotocol::FolderAggregateStatus status,
                               uint64_t itemCount, uint64_t totalSizeBytes,
                               uint64_t totalAllocatedSizeBytes,
                               uint64_t allocatedBytesKnownCount,
                               uint64_t allocatedBytesTotalCount);
    void SortItems(int column, bool ascending);
    void PopulateCategoryFilter();
    void UpdateCategoryFilterFromControl();
    void ApplyCategoryFilter();
    void UpdateVolumeUsedBytes();
    // semantic-treemap-uia 11/28: the single real navigation mutation shared
    // by the mouse-click callback and the UI-thread side of a UIA Invoke.
    void NavigateFromTreemap(const std::wstring& path, bool isDirectory);

    HWND owner_ = nullptr;
    HWND list_ = nullptr;
    HWND listHeader_ = nullptr;
    HWND overviewHeader_ = nullptr;
    HWND overview_ = nullptr;
    HWND categoryFilter_ = nullptr;
    HWND status_ = nullptr;
    ThemedStorageButton back_;
    ThemedStorageButton up_;
    ThemedStorageButton overviewButton_;
    ThemedStorageButton drillDown_;
    ThemedStorageButton largestFolders_;
    ThemedStorageButton largestFiles_;
    ThemedStorageButton byCategory_;
    ThemedStorageButton treemap_;

    EngineClient* engine_ = nullptr;
    std::function<void(const std::wstring& path, bool isDirectory)> navigate_;
    std::function<void()> close_;
    std::function<void(const std::wstring& commandId, const std::vector<std::wstring>& paths)> invokeCommand_;
    CategoryEngine categoryEngine_;
    TreemapView treemapView_;
    std::vector<std::wstring> categoryFilterIds_;
    std::wstring activeCategoryId_;

    std::vector<DrillItem> items_;
    std::vector<VolumeItem> volumes_;
    std::wstring currentPath_;
    bool visible_ = false;
    bool engineActive_ = false;
    bool darkTheme_ = false;
    ViewMode viewMode_ = ViewMode::Overview;

    uint64_t volumeUsedBytes_ = 0;
    bool volumeUsedKnown_ = false;

    UiaEventRouter uiaEvents_;

    int sortColumn_ = 3;
    bool sortAscending_ = false;

    std::vector<PendingRequest> pending_;
    std::mutex pendingMutex_;
    uint64_t nextRequestId_ = 1;
    uint64_t parentAggregateTotal_ = 0;
    bool parentAggregateKnown_ = false;
    bool parentAggregatePending_ = false;
    uint64_t parentRequestId_ = 0;
};

} // namespace ffui
