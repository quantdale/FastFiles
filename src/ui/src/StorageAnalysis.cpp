#include "StorageAnalysis.h"

#include "NativeChrome.h"
#include "TreemapView.h"
#include "UiComponents.h"
#include "UITheme.h"
#include "Util.h"
#include "ffui/NativeOverlayLayout.h"
#include "ffui/StorageLayout.h"
#include "ffui/StorageRadioState.h"
#include "ffui/DisplayPath.h"
#include "ffui/SnapshotAggregate.h"
#include "UiaHostQuery.h"
#include "ffsearch/Path.h"

#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>

namespace ffui {
namespace {

constexpr int kListId = 7201;
constexpr int kStatusId = 7202;
constexpr int kBackId = 7203;
constexpr int kUpId = 7204;
constexpr int kOverviewId = 7205;
constexpr int kDrillDownId = 7206;
constexpr int kLargestFoldersId = 7207;
constexpr int kLargestFilesId = 7208;
constexpr int kByCategoryId = 7209;
constexpr int kTreemapId = 7210;
constexpr int kOverviewListId = 7211;
constexpr int kCategoryFilterId = 7212;
constexpr UINT_PTR kStorageButtonSubclassBase = 0x5340;
constexpr UINT_PTR kRadioSubclassBase = 0x5346;
constexpr UINT_PTR kStatusSubclassId = 0x5350;
constexpr size_t kStorageRadioCount = kStorageModeCount;
constexpr UINT_PTR kInactiveSelectionSubclass = 0x5352;

// Scale a DIP metric to physical pixels for Win32 control layout.
int Scaled(int dipValue) {
    return static_cast<int>(ffui::UiScale(static_cast<float>(dipValue)));
}

void DrawInactiveStorageSelection(HWND hwnd, HDC dc, bool darkTheme) {
    if (!dc || UiSystemHighContrast()) return;
    const HWND focused = GetFocus();
    if (focused == hwnd || (focused != nullptr && IsChild(hwnd, focused))) return;
    const int selected = ListView_GetNextItem(hwnd, -1, LVNI_SELECTED);
    if (selected < 0) return;
    RECT row{};
    RECT body{};
    if (!ListView_GetItemRect(hwnd, selected, &row, LVIR_BOUNDS) || !GetClientRect(hwnd, &body)) return;
    const HWND header = ListView_GetHeader(hwnd);
    RECT headerRect{};
    if (header != nullptr && IsWindowVisible(header) && GetWindowRect(header, &headerRect)) {
        MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&headerRect), 2);
        body.top = (std::max)(body.top, headerRect.bottom);
    }
    RECT visible{};
    if (!IntersectRect(&visible, &row, &body)) return;
    const int saved = SaveDC(dc);
    if (saved == 0) return;
    IntersectClipRect(dc, body.left, body.top, body.right, body.bottom);
    const UINT dpi = GetDpiForWindow(hwnd);
    const int stroke = (std::max)(1, MulDiv(1, static_cast<int>(dpi), 96));
    const int strip = (std::max)(stroke, MulDiv(3, static_cast<int>(dpi), 96));
    // Solid selection edges plus a leading strip retain the selected shape;
    // this is not the keyboard focus ring and does not replace native text.
    HBRUSH brush = CreateSolidBrush(ToColorRef(GetUiTheme(darkTheme).accent));
    if (brush != nullptr) {
        const RECT edges[] = {
            {body.left, row.top, body.right, row.top + stroke},
            {body.left, row.bottom - stroke, body.right, row.bottom},
            {body.left, row.top, body.left + strip, row.bottom},
            {body.right - stroke, row.top, body.right, row.bottom}
        };
        for (const RECT& edge : edges) FillRect(dc, &edge, brush);
        DeleteObject(brush);
    }
    RestoreDC(dc, saved);
}

void DrawStorageFocusRing(HDC dc, const RECT& rect, COLORREF color) {
    const int inset = (std::max)(1, Scaled(2));
    const RECT ring{rect.left + inset, rect.top + inset, rect.right - inset, rect.bottom - inset};
    if (ring.right <= ring.left || ring.bottom <= ring.top) return;
    HPEN pen = CreatePen(PS_SOLID, (std::max)(1, Scaled(1)), color);
    if (pen == nullptr) return;
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
    const int radius = (std::max)(1, Scaled(static_cast<int>(UiMetrics::kRadiusSmall)));
    RoundRect(dc, ring.left, ring.top, ring.right, ring.bottom, radius * 2, radius * 2);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void DrawStorageRadioButton(const DRAWITEMSTRUCT* item, bool darkTheme) {
    if (item == nullptr || item->hDC == nullptr) return;
    const bool checked = SendMessageW(item->hwndItem, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const bool pressed = (item->itemState & ODS_SELECTED) != 0;
    const bool focused = (item->itemState & ODS_FOCUS) != 0;
    const bool disabled = (item->itemState & ODS_DISABLED) != 0;
    const bool hot = (item->itemState & ODS_HOTLIGHT) != 0;
    const auto state = ResolveStorageRadioVisualState(checked, pressed, focused, disabled);
    const bool highContrast = UiSystemHighContrast();
    const UiTheme theme = GetUiTheme(darkTheme);

    wchar_t label[256]{};
    GetWindowTextW(item->hwndItem, label, static_cast<int>(std::size(label)));
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(item->hwndItem, WM_GETFONT, 0, 0));
    if (font == nullptr) font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    if (highContrast) {
        // High Contrast keeps a literal radio glyph so the mutually exclusive
        // group remains recognizable when token overlays are suppressed.
        const bool interactionFill = state == StorageRadioVisualState::Pressed ||
                                     state == StorageRadioVisualState::CheckedPressed || hot;
        COLORREF background = interactionFill ? GetSysColor(COLOR_BTNSHADOW) : GetSysColor(COLOR_BTNFACE);
        UiGdiRoundedRect(item->hDC, item->rcItem, background, Scaled(static_cast<int>(UiMetrics::kRadiusSmall)));
        const COLORREF border = GetSysColor(disabled ? COLOR_GRAYTEXT : COLOR_WINDOWTEXT);
        const COLORREF text = GetSysColor(disabled ? COLOR_GRAYTEXT : COLOR_BTNTEXT);
        const COLORREF accent = GetSysColor(COLOR_HIGHLIGHT);
        const int glyphSize = (std::max)(Scaled(16), 12);
        const int glyphLeft = item->rcItem.left + Scaled(static_cast<int>(UiMetrics::kSpaceXs));
        const int glyphTop = (item->rcItem.top + item->rcItem.bottom - glyphSize) / 2;
        const RECT glyph{glyphLeft, glyphTop, glyphLeft + glyphSize, glyphTop + glyphSize};
        HBRUSH glyphBrush = CreateSolidBrush(background);
        HPEN glyphPen = CreatePen(PS_SOLID, (std::max)(1, Scaled(1)), border);
        HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(
            item->hDC, glyphBrush != nullptr ? glyphBrush : GetStockObject(NULL_BRUSH)));
        HPEN oldPen = static_cast<HPEN>(SelectObject(
            item->hDC, glyphPen != nullptr ? glyphPen : GetStockObject(NULL_PEN)));
        Ellipse(item->hDC, glyph.left, glyph.top, glyph.right, glyph.bottom);
        SelectObject(item->hDC, oldBrush);
        SelectObject(item->hDC, oldPen);
        if (glyphBrush != nullptr) DeleteObject(glyphBrush);
        if (glyphPen != nullptr) DeleteObject(glyphPen);
        if (checked) {
            const int inset = (std::max)(2, glyphSize / 4);
            const RECT dot{glyph.left + inset, glyph.top + inset,
                           glyph.right - inset, glyph.bottom - inset};
            HBRUSH dotBrush = CreateSolidBrush(accent);
            if (dotBrush != nullptr) {
                HBRUSH previous = static_cast<HBRUSH>(SelectObject(item->hDC, dotBrush));
                HPEN previousPen = static_cast<HPEN>(SelectObject(item->hDC, GetStockObject(NULL_PEN)));
                Ellipse(item->hDC, dot.left, dot.top, dot.right, dot.bottom);
                SelectObject(item->hDC, previous);
                SelectObject(item->hDC, previousPen);
                DeleteObject(dotBrush);
            }
        }
        RECT textRect = item->rcItem;
        textRect.left = glyph.right + Scaled(static_cast<int>(UiMetrics::kSpaceXs));
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, text);
        HFONT oldFont = static_cast<HFONT>(SelectObject(item->hDC, font));
        DrawTextW(item->hDC, label, -1, &textRect,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(item->hDC, oldFont);
        if (focused) DrawStorageFocusRing(item->hDC, item->rcItem, GetSysColor(COLOR_WINDOWTEXT));
        return;
    }

    // final-product-completion-and-production-readiness 6.2: segmented pills,
    // not native radio dots. Keyboard/UIA stay BS_AUTORADIOBUTTON.
    HBRUSH erase = CreateSolidBrush(ToColorRef(theme.background));
    if (erase != nullptr) {
        FillRect(item->hDC, &item->rcItem, erase);
        DeleteObject(erase);
    }
    COLORREF fill = ToColorRef(theme.surface);
    COLORREF text = ToColorRef(theme.text);
    if (disabled) {
        fill = ToColorRef(theme.background);
        text = ToColorRef(theme.textDisabled);
    } else if (checked) {
        fill = ToColorRef(theme.selectionSoft);
        text = ToColorRef(theme.text);
    } else if (pressed) {
        fill = ToColorRef(theme.surfaceElevated);
    } else if (hot) {
        fill = ToColorRef(theme.surfaceSubtle);
    }
    UiGdiRoundedRect(item->hDC, item->rcItem, fill, Scaled(static_cast<int>(UiMetrics::kRadiusSmall)));
    if (checked && !disabled) {
        RECT bar = item->rcItem;
        const int inset = Scaled(static_cast<int>(UiMetrics::kSpaceXs));
        bar.left += inset;
        bar.right -= inset;
        bar.top = bar.bottom - Scaled(3);
        bar.bottom -= Scaled(1);
        UiGdiRoundedRect(item->hDC, bar, ToColorRef(theme.accent), Scaled(1));
    }
    RECT textRect = item->rcItem;
    const int pad = Scaled(static_cast<int>(UiMetrics::kSpaceXs));
    textRect.left += pad;
    textRect.right -= pad;
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, text);
    HFONT oldFont = static_cast<HFONT>(SelectObject(item->hDC, font));
    DrawTextW(item->hDC, label, -1, &textRect,
              DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(item->hDC, oldFont);
    if (focused) DrawStorageFocusRing(item->hDC, item->rcItem, ToColorRef(theme.focusStroke));
}

void DrawStoragePushButton(HWND hwnd, HDC dc, bool darkTheme) {
    if (hwnd == nullptr || dc == nullptr) return;
    RECT rect{};
    GetClientRect(hwnd, &rect);
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;

    const bool enabled = IsWindowEnabled(hwnd) != FALSE;
    const UINT buttonState = static_cast<UINT>(SendMessageW(hwnd, BM_GETSTATE, 0, 0));
    const bool pressed = (buttonState & BST_PUSHED) != 0;
    POINT cursor{};
    RECT client = rect;
    GetCursorPos(&cursor);
    ScreenToClient(hwnd, &cursor);
    const bool hot = PtInRect(&client, cursor) != FALSE;
    const bool focused = GetFocus() == hwnd;
    const UiTheme theme = GetUiTheme(darkTheme);
    const bool highContrast = UiSystemHighContrast();
    if (highContrast) return;

    COLORREF fill = ToColorRef(theme.surface);
    if (!enabled) {
        fill = ToColorRef(theme.background);
    } else if (pressed) {
        fill = ToColorRef(theme.surfaceElevated);
    } else if (hot) {
        fill = ToColorRef(theme.surfaceSubtle);
    }
    UiGdiRoundedRect(dc, rect, fill, Scaled(static_cast<int>(UiMetrics::kRadiusSmall)));

    DrawStorageFocusRing(dc, rect, ToColorRef(theme.border));
    if (focused) DrawStorageFocusRing(dc, rect, ToColorRef(theme.focusStroke));

    wchar_t label[64]{};
    GetWindowTextW(hwnd, label, static_cast<int>(std::size(label)));
    RECT textRect = rect;
    const int horizontalPadding = Scaled(static_cast<int>(UiMetrics::kSpaceS));
    textRect.left += horizontalPadding;
    textRect.right -= horizontalPadding;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, ToColorRef(enabled ? theme.text : theme.textDisabled));
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
    if (font == nullptr) font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, font));
    DrawTextW(dc, label, -1, &textRect,
              DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, oldFont);
}

// storage-analysis 2.4: a volume that disappeared keeps its last-known
// capacity figures (labeled stale) instead of being silently dropped. A
// volume with no prior figures was never readable -- "stale" would imply
// data that no longer exists, so it is labeled unavailable instead.
constexpr wchar_t kStaleSuffix[] = L" (stale)";
constexpr wchar_t kUnavailableSuffix[] = L" (unavailable)";

void SetSortIndicator(HWND list, int column, bool ascending) {
    HWND header = ListView_GetHeader(list);
    if (header == nullptr) return;
    for (int i = 0; i < 16; ++i) {
        HDITEM hd{};
        hd.mask = HDI_FORMAT;
        if (Header_GetItem(header, i, &hd)) {
            hd.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
            Header_SetItem(header, i, &hd);
        }
    }
    HDITEM hd{};
    hd.mask = HDI_FORMAT;
    if (Header_GetItem(header, column, &hd)) {
        hd.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        hd.fmt |= ascending ? HDF_SORTUP : HDF_SORTDOWN;
        Header_SetItem(header, column, &hd);
    }
}

} // namespace

// ui-v3 storage radio polish: the WindowShell uses a custom fragment root, so
// the default HWND bridge exposes native child buttons as Pane elements. Keep
// the host provider for geometry/runtime identity, but override the radio
// control type and SelectionItem pattern so owner-draw presentation never
// removes keyboard or assistive-technology semantics.
class StorageRadioProvider final : public UiaProviderBase, public ISelectionItemProvider {
public:
    StorageRadioProvider(UiaOwnerStatePtr state, StorageAnalysis* owner, HWND radio)
        : UiaProviderBase(std::move(state)), owner_(owner), radio_(radio) {}

    FFUIA_QI_BEGIN()
        FFUIA_QI_ENTRY(IID_ISelectionItemProvider, ISelectionItemProvider)
    FFUIA_QI_END()
    FFUIA_REFCOUNT()

    STDMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** retVal) noexcept override {
        if (retVal == nullptr) return E_POINTER;
        *retVal = nullptr;
        if (!Alive() || radio_ == nullptr || !IsWindow(radio_)) return UIA_E_ELEMENTNOTAVAILABLE;
        UiaHostQueryGuard guard(radio_);
        return UiaHostProviderFromHwnd(radio_, retVal);
    }

    STDMETHODIMP GetPropertyValue(PROPERTYID propertyId, VARIANT* retVal) noexcept override {
        if (retVal == nullptr) return E_POINTER;
        VariantInit(retVal);
        if (!Alive() || radio_ == nullptr || !IsWindow(radio_)) return UIA_E_ELEMENTNOTAVAILABLE;
        switch (propertyId) {
            case UIA_ControlTypePropertyId:
                SetVariantInt(retVal, UIA_RadioButtonControlTypeId);
                return S_OK;
            case UIA_NamePropertyId: {
                wchar_t text[128]{};
                GetWindowTextW(radio_, text, static_cast<int>(std::size(text)));
                SetVariantString(retVal, text);
                return S_OK;
            }
            case UIA_IsKeyboardFocusablePropertyId:
                SetVariantBool(retVal, IsWindowEnabled(radio_) != FALSE);
                return S_OK;
            case UIA_IsEnabledPropertyId:
                SetVariantBool(retVal, IsWindowEnabled(radio_) != FALSE);
                return S_OK;
            case UIA_HasKeyboardFocusPropertyId:
                SetVariantBool(retVal, GetFocus() == radio_);
                return S_OK;
            case UIA_IsOffscreenPropertyId:
                SetVariantBool(retVal, IsWindowVisible(radio_) == FALSE);
                return S_OK;
            case UIA_SelectionItemIsSelectedPropertyId:
                SetVariantBool(retVal, SendMessageW(radio_, BM_GETCHECK, 0, 0) == BST_CHECKED);
                return S_OK;
            default:
                SetVariantEmpty(retVal);
                return S_OK;
        }
    }

    STDMETHODIMP GetPatternProvider(PATTERNID patternId, IUnknown** retVal) noexcept override {
        if (retVal == nullptr) return E_POINTER;
        *retVal = nullptr;
        if (!Alive()) return UIA_E_ELEMENTNOTAVAILABLE;
        if (patternId == UIA_SelectionItemPatternId) {
            *retVal = static_cast<ISelectionItemProvider*>(this);
            static_cast<IRawElementProviderSimple*>(this)->AddRef();
            return S_OK;
        }
        return UiaProviderBase::GetPatternProvider(patternId, retVal);
    }

    STDMETHODIMP Select() noexcept override {
        return SelectOnUiThread();
    }

    STDMETHODIMP AddToSelection() noexcept override {
        return SelectOnUiThread();
    }

    STDMETHODIMP RemoveFromSelection() noexcept override {
        // A radio group cannot have an unselected active member through the
        // SelectionItem contract; changing mode is the only valid mutation.
        return UIA_E_INVALIDOPERATION;
    }

    STDMETHODIMP get_IsSelected(BOOL* retVal) noexcept override {
        if (retVal == nullptr) return E_POINTER;
        if (!Alive() || radio_ == nullptr || !IsWindow(radio_)) return UIA_E_ELEMENTNOTAVAILABLE;
        *retVal = SendMessageW(radio_, BM_GETCHECK, 0, 0) == BST_CHECKED ? TRUE : FALSE;
        return S_OK;
    }

    STDMETHODIMP get_SelectionContainer(IRawElementProviderSimple** retVal) noexcept override {
        if (retVal == nullptr) return E_POINTER;
        *retVal = nullptr;
        if (!Alive() || owner_ == nullptr || owner_->owner_ == nullptr) return UIA_E_ELEMENTNOTAVAILABLE;
        UiaHostQueryGuard guard(owner_->owner_);
        return UiaHostProviderFromHwnd(owner_->owner_, retVal);
    }

private:
    HRESULT SelectOnUiThread() noexcept {
        if (!Alive() || owner_ == nullptr || radio_ == nullptr || !IsWindow(radio_)) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        return owner_->SelectRadioFromUia(radio_) ? S_OK : UIA_E_INVALIDOPERATION;
    }

    StorageAnalysis* owner_ = nullptr;
    HWND radio_ = nullptr;
};

// original-vision-journey-recovery: query adapters synchronously marshal to
// the list-owning UI thread; provider calls never read items_ concurrently
// with RefreshData/SortItems. Invocation posts a value-owned identity token.
constexpr UINT kStorageListQuery = WM_APP + 80;
constexpr UINT kStorageListAction = WM_APP + 81;
constexpr UINT kStorageListActivate = WM_APP + 82;
constexpr UINT kStorageListChanged = WM_APP + 83;
constexpr UINT_PTR kStorageListSubclass = 0x5351;

struct StorageAnalysis::ListQuery {
    size_t index = SIZE_MAX;
    size_t count = 0;
    UiaSearchResultInfo info;
    RECT rect{};
    RECT headerRect{};
    std::wstring headerName;
    size_t headerCount = 0;
    bool hasRect = false;
    bool hasHeaderRect = false;
    bool focused = false;
    uint64_t headerIdentity = 0;
};

struct StorageAnalysis::ListAction {
    size_t index = 0;
    uint64_t identity = 0;
    unsigned kind = 0; // select/remove/focus/scroll/activate/header sort
};

LRESULT CALLBACK StorageAnalysis::ListSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                                   UINT_PTR subclassId, DWORD_PTR refData) {
    auto* self = reinterpret_cast<StorageAnalysis*>(refData);
    if (!self) return DefSubclassProc(hwnd, message, wParam, lParam);
    if (message == WM_GETOBJECT && static_cast<LONG>(lParam) == UiaRootObjectId && self->UiaSearchVisible()) {
        static thread_local bool serving = false;
        if (serving || UiaHostQueryGuard::Active(hwnd)) return DefSubclassProc(hwnd, message, wParam, lParam);
        serving = true;
        if (!self->listProvider_) {
            self->listUiaState_ = std::make_shared<UiaOwnerState>();
            self->listProvider_ = Microsoft::WRL::ComPtr<SearchUiaProvider>(new SearchUiaProvider(
                self->listUiaState_, self, hwnd, {L"Storage items", L"7201", kUiaNsStorageList}));
            self->listEvents_.Set(&SearchUiaProvider::SinkThunk, self->listProvider_.Get());
        }
        const LRESULT result = UiaReturnRawElementProvider(hwnd, wParam, lParam, self->listProvider_.Get());
        serving = false;
        return result;
    }
    if (message == kStorageListQuery) {
        if (auto* query = reinterpret_cast<ListQuery*>(lParam)) self->ReadListQuery(*query);
        return 0;
    }
    if (message == kStorageListAction) {
        if (const auto* action = reinterpret_cast<const ListAction*>(lParam)) return self->ApplyListAction(*action) ? 1 : 0;
        return 0;
    }
    if (message == kStorageListActivate) {
        std::unique_ptr<ListAction> action(reinterpret_cast<ListAction*>(lParam));
        if (action) self->ApplyListAction(*action);
        return 0;
    }
    if (message == kStorageListChanged) {
        self->listNotificationPending_ = false;
        self->listEvents_.Notify({UiaEventKind::StructureChanged});
        return 0;
    }
    if (message == WM_NCDESTROY) {
        self->listEvents_.Clear();
        if (self->listUiaState_) self->listUiaState_->alive = false;
        self->listProvider_.Reset();
        self->listUiaState_.reset();
        MSG pending{};
        while (PeekMessageW(&pending, hwnd, kStorageListActivate, kStorageListActivate, PM_REMOVE)) {
            delete reinterpret_cast<ListAction*>(pending.lParam);
        }
        RemoveWindowSubclass(hwnd, ListSubclassProc, subclassId);
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

void StorageAnalysis::ReadListQuery(ListQuery& query) const {
    if (!UiaSearchVisible()) return;
    query.count = items_.size();
    query.focused = GetFocus() == list_;
    const HWND header = ListView_GetHeader(list_);
    query.headerCount = static_cast<size_t>((std::max)(0, Header_GetItemCount(header)));
    if (query.index < query.headerCount) {
        wchar_t text[128]{};
        HDITEMW item{};
        item.mask = HDI_TEXT;
        item.pszText = text;
        item.cchTextMax = static_cast<int>(std::size(text));
        if (Header_GetItem(header, static_cast<int>(query.index), &item)) query.headerName = text;
        query.headerIdentity = UiaHashMix(UiaHashWide(currentPath_), static_cast<uint64_t>(query.index) + 1);
        if (Header_GetItemRect(header, static_cast<int>(query.index), &query.headerRect)) {
            MapWindowPoints(header, nullptr, reinterpret_cast<POINT*>(&query.headerRect), 2);
            RECT headerBounds{};
            GetWindowRect(header, &headerBounds);
            IntersectRect(&query.headerRect, &query.headerRect, &headerBounds);
            query.hasHeaderRect = true;
        }
    }
    if (query.index >= items_.size()) return;
    const DrillItem& item = items_[query.index];
    auto& info = query.info;
    info.name = item.name;
    info.folder = item.folder.empty() ? currentPath_ : item.folder;
    info.displayFolder = ffui::NormalizeDisplayPath(info.folder);
    info.isDirectory = item.isDirectory;
    info.sizeBytes = item.totalSizeBytes;
    info.modifiedTime = item.lastModifiedTime;
    info.allocatedSizeBytes = item.isDirectory ? item.totalAllocatedSizeBytes : item.allocatedSizeBytes;
    info.allocatedSizeKnown = item.isDirectory
        ? !item.calculating && !item.aggregateFailed && item.allocatedKnownCount == item.allocatedTotalCount
        : item.allocatedSizeKnown;
    if (!item.categoryId.empty()) {
        info.identityHash = UiaHashMix(UiaHashWide(currentPath_), UiaHashMix(UiaHashWide(L"category"), UiaHashWide(item.categoryId)));
        info.description = L"Category summary, " + ffui::FormatSize(item.totalSizeBytes) + L" in this folder";
        info.displayFolder = L"Category summary";
    } else {
        info.fullPath = StorageRankingItemPath(item.folder, item.name);
        info.identityHash = UiaHashMix(UiaHashWide(currentPath_),
            UiaItemIdentityHash(item.volumeRowId, item.fileIdLow, item.fileIdHigh, info.fullPath));
        // A recursive rank can contain two hard-link paths with the same FRN.
        // Preserve the selected directory entry, not just its shared file ID.
        if (RecursiveRanking()) info.identityHash = UiaHashMix(info.identityHash, UiaHashWide(info.fullPath));
    }
    const UINT state = ListView_GetItemState(list_, static_cast<int>(query.index), LVIS_SELECTED | LVIS_FOCUSED);
    info.selected = (state & LVIS_SELECTED) != 0;
    info.focused = (state & LVIS_FOCUSED) != 0;
    if (ListView_GetItemRect(list_, static_cast<int>(query.index), &query.rect, LVIR_BOUNDS)) {
        MapWindowPoints(list_, nullptr, reinterpret_cast<POINT*>(&query.rect), 2);
        query.hasRect = true;
    }
}

size_t StorageAnalysis::UiaSearchResultCount() const {
    ListQuery query;
    if (list_ && IsWindow(list_)) SendMessageW(list_, kStorageListQuery, 0, reinterpret_cast<LPARAM>(&query));
    return query.count;
}

UiaSearchResultInfo StorageAnalysis::UiaSearchResultInfoOf(size_t index) const {
    ListQuery query;
    query.index = index;
    if (list_ && IsWindow(list_)) SendMessageW(list_, kStorageListQuery, 0, reinterpret_cast<LPARAM>(&query));
    return query.info;
}

bool StorageAnalysis::UiaSearchResultRect(size_t index, RECT& outScreen) const {
    ListQuery query;
    query.index = index;
    if (list_ && IsWindow(list_)) SendMessageW(list_, kStorageListQuery, 0, reinterpret_cast<LPARAM>(&query));
    outScreen = query.rect;
    return query.hasRect;
}

bool StorageAnalysis::UiaListResultRect(size_t index, uint64_t identity, RECT& rect) const {
    ListQuery query;
    query.index = index;
    if (list_ && IsWindow(list_)) SendMessageW(list_, kStorageListQuery, 0, reinterpret_cast<LPARAM>(&query));
    rect = query.rect;
    return query.hasRect && query.info.identityHash == identity;
}

bool StorageAnalysis::UiaListResultOffscreen(size_t index, uint64_t identity) const {
    RECT item{}, clip{};
    if (!UiaListResultRect(index, identity, item) || !GetClientRect(list_, &clip)) return true;
    MapWindowPoints(list_, nullptr, reinterpret_cast<POINT*>(&clip), 2);
    RECT header{};
    if (GetWindowRect(ListView_GetHeader(list_), &header)) clip.top = (std::max)(clip.top, header.bottom);
    RECT intersection{};
    return IntersectRect(&intersection, &item, &clip) == FALSE;
}
bool StorageAnalysis::UiaSearchResultOffscreen(size_t index) const {
    RECT item{}, clip{};
    if (!UiaSearchResultRect(index, item) || !GetClientRect(list_, &clip)) return true;
    MapWindowPoints(list_, nullptr, reinterpret_cast<POINT*>(&clip), 2);
    RECT header{};
    if (GetWindowRect(ListView_GetHeader(list_), &header)) clip.top = (std::max)(clip.top, header.bottom);
    RECT intersection{};
    return IntersectRect(&intersection, &item, &clip) == FALSE;
}

bool StorageAnalysis::UiaSearchVisible() const {
    return list_ != nullptr && IsWindowVisible(list_) != FALSE;
}

bool StorageAnalysis::UiaSearchHasKeyboardFocus() const {
    ListQuery query;
    if (list_ && IsWindow(list_)) SendMessageW(list_, kStorageListQuery, 0, reinterpret_cast<LPARAM>(&query));
    return query.focused;
}

bool StorageAnalysis::DispatchListAction(size_t index, unsigned kind, uint64_t expectedIdentity) {
    if (!UiaSearchVisible() || expectedIdentity == 0) return false;
    ListAction action{index, expectedIdentity, kind};
    if (kind == 4 || kind == 5) {
        auto payload = std::make_unique<ListAction>(action);
        if (!PostMessageW(list_, kStorageListActivate, 0, reinterpret_cast<LPARAM>(payload.get()))) return false;
        payload.release();
        return true;
    }
    return SendMessageW(list_, kStorageListAction, 0, reinterpret_cast<LPARAM>(&action)) != 0;
}

bool StorageAnalysis::ApplyListAction(const ListAction& action) {
    if (!UiaSearchVisible()) return false;
    if (action.kind == 6) {
        SetFocus(list_);
        return GetFocus() == list_;
    }
    ListQuery query;
    query.index = action.index;
    ReadListQuery(query);
    if (action.kind == 5) {
        if (action.index >= query.headerCount || query.headerIdentity != action.identity) return false;
        SortItems(static_cast<int>(action.index),
            static_cast<int>(action.index) == sortColumn_ ? !sortAscending_ : true);
        return true;
    }
    if (query.info.identityHash == 0 || query.info.identityHash != action.identity) return false;
    const int index = static_cast<int>(action.index);
    switch (action.kind) {
    case 0: ListView_SetItemState(list_, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED); break;
    case 1: ListView_SetItemState(list_, index, 0, LVIS_SELECTED); break;
    case 2: SetFocus(list_); ListView_SetItemState(list_, index, LVIS_FOCUSED, LVIS_FOCUSED); break;
    case 3: ListView_EnsureVisible(list_, index, FALSE); break;
    case 4: ActivateListItem(action.index); break;
    default: return false;
    }
    return true;
}

bool StorageAnalysis::UiaListDispatchAction(size_t index, uint64_t expectedIdentity, unsigned action) {
    return DispatchListAction(index, action, expectedIdentity);
}
void StorageAnalysis::UiaListSetFocus() {
    const ListAction action{0, 0, 6};
    if (list_ && IsWindow(list_)) SendMessageW(list_, kStorageListAction, 0, reinterpret_cast<LPARAM>(&action));
}
void StorageAnalysis::UiaSearchSelectItem(size_t index) { DispatchListAction(index, 0, UiaSearchResultInfoOf(index).identityHash); }
void StorageAnalysis::UiaSearchRemoveFromSelection(size_t index) { DispatchListAction(index, 1, UiaSearchResultInfoOf(index).identityHash); }
void StorageAnalysis::UiaSearchSetFocusItem(size_t index) { DispatchListAction(index, 2, UiaSearchResultInfoOf(index).identityHash); }
void StorageAnalysis::UiaSearchScrollIntoView(size_t index) { DispatchListAction(index, 3, UiaSearchResultInfoOf(index).identityHash); }
void StorageAnalysis::UiaSearchActivateItem(size_t index) { DispatchListAction(index, 4, UiaSearchResultInfoOf(index).identityHash); }

size_t StorageAnalysis::UiaListHeaderCount() const {
    ListQuery query;
    if (list_ && IsWindow(list_)) SendMessageW(list_, kStorageListQuery, 0, reinterpret_cast<LPARAM>(&query));
    return query.headerCount;
}

std::wstring StorageAnalysis::UiaListHeaderName(size_t index) const {
    ListQuery query;
    query.index = index;
    if (list_ && IsWindow(list_)) SendMessageW(list_, kStorageListQuery, 0, reinterpret_cast<LPARAM>(&query));
    return query.headerName;
}

bool StorageAnalysis::UiaListHeaderRect(size_t index, RECT& outScreen) const {
    ListQuery query;
    query.index = index;
    if (list_ && IsWindow(list_)) SendMessageW(list_, kStorageListQuery, 0, reinterpret_cast<LPARAM>(&query));
    outScreen = query.headerRect;
    return query.hasHeaderRect;
}

uint64_t StorageAnalysis::UiaListHeaderIdentity(size_t index) const {
    ListQuery query;
    query.index = index;
    if (list_ && IsWindow(list_)) SendMessageW(list_, kStorageListQuery, 0, reinterpret_cast<LPARAM>(&query));
    return query.headerIdentity;
}

bool StorageAnalysis::UiaListDispatchHeader(size_t index, uint64_t identity) {
    return DispatchListAction(index, 5, identity);
}

void StorageAnalysis::UiaListInvokeHeader(size_t index) { DispatchListAction(index, 5, UiaListHeaderIdentity(index)); }
void StorageAnalysis::NotifyListChanged() {
    if (listProvider_ && !listNotificationPending_) {
        listNotificationPending_ = PostMessageW(list_, kStorageListChanged, 0, 0) != FALSE;
    }
}
StorageAnalysis::~StorageAnalysis() {
    StopRanking();
    listEvents_.Clear();
    if (listUiaState_) listUiaState_->alive = false;
    listProvider_.Reset();
    listUiaState_.reset();
    if (list_ != nullptr) {
        MSG pending{};
        while (PeekMessageW(&pending, list_, kStorageListActivate, kStorageListActivate, PM_REMOVE)) {
            delete reinterpret_cast<ListAction*>(pending.lParam);
        }
        RemoveWindowSubclass(list_, ListSubclassProc, kStorageListSubclass);
    }
    for (HWND control : {list_, overview_}) {
        if (control != nullptr) RemoveWindowSubclass(control, InactiveSelectionSubclassProc, kInactiveSelectionSubclass);
    }
    const HWND pushButtons[] = {back_, up_};
    for (size_t i = 0; i < std::size(pushButtons); ++i) {
        if (pushButtons[i] != nullptr) {
            RemoveWindowSubclass(pushButtons[i], StorageButtonSubclassProc,
                                 kStorageButtonSubclassBase + static_cast<UINT_PTR>(i));
        }
    }
    const HWND radios[] = {overviewButton_, drillDown_, largestFolders_, largestFiles_, byCategory_, treemap_};
    for (size_t i = 0; i < kStorageRadioCount; ++i) {
        if (radios[i] != nullptr) {
            RemoveWindowSubclass(radios[i], RadioSubclassProc,
                                 kRadioSubclassBase + static_cast<UINT_PTR>(i));
        }
    }
    if (status_ != nullptr) {
        RemoveWindowSubclass(status_, StatusSubclassProc, kStatusSubclassId);
    }
    uiaEvents_.Clear();
    if (uiaState_) uiaState_->alive = false;
    uiaState_.reset();
    treemapView_.SetUiaEventHandler(nullptr);
    Hide();
}

LRESULT CALLBACK StorageAnalysis::InactiveSelectionSubclassProc(HWND hwnd, UINT message, WPARAM wParam,
                                                               LPARAM lParam, UINT_PTR subclassId, DWORD_PTR refData) {
    auto* self = reinterpret_cast<StorageAnalysis*>(refData);
    if (self == nullptr) return DefSubclassProc(hwnd, message, wParam, lParam);
    const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
    if (message == WM_PAINT) {
        const HDC dc = GetDC(hwnd);
        DrawInactiveStorageSelection(hwnd, dc, self->darkTheme_);
        if (dc != nullptr) ReleaseDC(hwnd, dc);
    } else if (message == WM_PRINTCLIENT) {
        DrawInactiveStorageSelection(hwnd, reinterpret_cast<HDC>(wParam), self->darkTheme_);
    } else if (message == WM_SETFOCUS || message == WM_KILLFOCUS || message == LVM_SETITEMSTATE ||
               message == WM_THEMECHANGED || message == WM_SYSCOLORCHANGE) {
        InvalidateRect(hwnd, nullptr, FALSE);
    } else if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, InactiveSelectionSubclassProc, subclassId);
    }
    return result;
}

LRESULT CALLBACK StorageAnalysis::StorageButtonSubclassProc(HWND hwnd, UINT message, WPARAM wParam,
                                                            LPARAM lParam, UINT_PTR subclassId,
                                                            DWORD_PTR refData) {
    auto* self = reinterpret_cast<StorageAnalysis*>(refData);
    if (self == nullptr) return DefSubclassProc(hwnd, message, wParam, lParam);
    if (UiSystemHighContrast()) {
        if (message == WM_NCDESTROY) {
            RemoveWindowSubclass(hwnd, StorageButtonSubclassProc, subclassId);
        }
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }

    const auto invalidate = [hwnd]() { InvalidateRect(hwnd, nullptr, FALSE); };
    switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            const HDC dc = BeginPaint(hwnd, &paint);
            if (dc != nullptr) DrawStoragePushButton(hwnd, dc, self->darkTheme_);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_PRINTCLIENT:
            DrawStoragePushButton(hwnd, reinterpret_cast<HDC>(wParam), self->darkTheme_);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
            const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
            invalidate();
            return result;
        }
        case WM_MOUSELEAVE:
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
        case WM_ENABLE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_KEYDOWN:
        case WM_KEYUP:
        case BM_SETSTATE:
        case WM_SETTEXT:
        case WM_THEMECHANGED: {
            const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
            invalidate();
            return result;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, StorageButtonSubclassProc, subclassId);
            break;
        default:
            break;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK StorageAnalysis::RadioSubclassProc(HWND hwnd, UINT message, WPARAM wParam,
                                                    LPARAM lParam, UINT_PTR subclassId,
                                                    DWORD_PTR refData) {
    auto* self = reinterpret_cast<StorageAnalysis*>(refData);
    if (self == nullptr) return DefSubclassProc(hwnd, message, wParam, lParam);

    if (message == WM_GETOBJECT && static_cast<LONG>(lParam) == UiaRootObjectId &&
        !UiaHostQueryGuard::Active(hwnd)) {
        Microsoft::WRL::ComPtr<IRawElementProviderSimple> fixup = self->UiaRadioFixupProvider(hwnd);
        if (fixup != nullptr) {
            return UiaReturnRawElementProvider(hwnd, wParam, lParam, fixup.Get());
        }
    }

    const auto invalidate = [hwnd]() { InvalidateRect(hwnd, nullptr, FALSE); };
    switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            const HDC dc = BeginPaint(hwnd, &paint);
            if (dc != nullptr) {
                DRAWITEMSTRUCT item{};
                item.CtlType = ODT_BUTTON;
                item.CtlID = static_cast<UINT>(GetDlgCtrlID(hwnd));
                item.itemID = item.CtlID;
                item.hwndItem = hwnd;
                item.hDC = dc;
                GetClientRect(hwnd, &item.rcItem);
                if ((static_cast<UINT>(SendMessageW(hwnd, BM_GETSTATE, 0, 0)) & BST_PUSHED) != 0) {
                    item.itemState |= ODS_SELECTED;
                }
                if (GetFocus() == hwnd) item.itemState |= ODS_FOCUS;
                if (!IsWindowEnabled(hwnd)) item.itemState |= ODS_DISABLED;
                POINT cursor{};
                RECT client{};
                GetClientRect(hwnd, &client);
                GetCursorPos(&cursor);
                ScreenToClient(hwnd, &cursor);
                if (PtInRect(&client, cursor)) item.itemState |= ODS_HOTLIGHT;
                DrawStorageRadioButton(&item, self->darkTheme_);
                EndPaint(hwnd, &paint);
            }
            return 0;
        }
        case WM_PRINTCLIENT: {
            const HDC dc = reinterpret_cast<HDC>(wParam);
            if (dc == nullptr) return 0;
            DRAWITEMSTRUCT item{};
            item.CtlType = ODT_BUTTON;
            item.CtlID = static_cast<UINT>(GetDlgCtrlID(hwnd));
            item.itemID = item.CtlID;
            item.hwndItem = hwnd;
            item.hDC = dc;
            GetClientRect(hwnd, &item.rcItem);
            if ((static_cast<UINT>(SendMessageW(hwnd, BM_GETSTATE, 0, 0)) & BST_PUSHED) != 0) {
                item.itemState |= ODS_SELECTED;
            }
            if (GetFocus() == hwnd) item.itemState |= ODS_FOCUS;
            if (!IsWindowEnabled(hwnd)) item.itemState |= ODS_DISABLED;
            DrawStorageRadioButton(&item, self->darkTheme_);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
            const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
            invalidate();
            return result;
        }
        case WM_MOUSELEAVE:
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
        case WM_ENABLE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_KEYDOWN:
        case WM_KEYUP:
        case BM_SETCHECK: {
            const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
            invalidate();
            return result;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, RadioSubclassProc, subclassId);
            break;
        default:
            break;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK StorageAnalysis::StatusSubclassProc(HWND hwnd, UINT message, WPARAM wParam,
                                                     LPARAM lParam, UINT_PTR subclassId,
                                                     DWORD_PTR refData) {
    auto* self = reinterpret_cast<StorageAnalysis*>(refData);
    if (self == nullptr) return DefSubclassProc(hwnd, message, wParam, lParam);
    switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            const HDC dc = BeginPaint(hwnd, &paint);
            if (dc != nullptr) {
                RECT rect{};
                GetClientRect(hwnd, &rect);
                const UiTheme theme = GetUiTheme(self->darkTheme_);
                const bool highContrast = UiSystemHighContrast();
                const COLORREF fill = highContrast ? GetSysColor(COLOR_BTNFACE) : ToColorRef(theme.background);
                const COLORREF text = highContrast ? GetSysColor(COLOR_WINDOWTEXT) : ToColorRef(theme.textSecondary);
                HBRUSH brush = CreateSolidBrush(fill);
                if (brush != nullptr) {
                    FillRect(dc, &rect, brush);
                    DeleteObject(brush);
                }
                wchar_t label[256]{};
                GetWindowTextW(hwnd, label, static_cast<int>(std::size(label)));
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, text);
                HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
                if (font == nullptr) font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
                HFONT oldFont = static_cast<HFONT>(SelectObject(dc, font));
                DrawTextW(dc, label, -1, &rect,
                          DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX | DT_END_ELLIPSIS);
                SelectObject(dc, oldFont);
            }
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_SETTEXT: {
            const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
            InvalidateRect(hwnd, nullptr, FALSE);
            return result;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, StatusSubclassProc, subclassId);
            break;
        default:
            break;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

bool StorageAnalysis::Initialize(HWND owner, EngineClient* engine,
                                 std::function<void(const std::wstring& path, bool isDirectory)> navigate,
                                 std::function<void()> close,
                                 std::function<void(const std::wstring& commandId, const std::vector<std::wstring>& paths)> invokeCommand) {
    owner_ = owner;
    engine_ = engine;
    navigate_ = std::move(navigate);
    close_ = std::move(close);
    invokeCommand_ = std::move(invokeCommand);

    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&controls);

    back_ = CreateWindowExW(0, L"BUTTON", L"Back", StoragePushButtonStyle(),
                            0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBackId)), nullptr, nullptr);
    up_ = CreateWindowExW(0, L"BUTTON", L"Up", StoragePushButtonStyle(),
                          0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kUpId)), nullptr, nullptr);
    status_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_ENDELLIPSIS | SS_LEFTNOWORDWRAP,
                              0, 0, 0, 0, owner, nullptr, nullptr, nullptr);
    list_ = CreateWindowExW(0, WC_LISTVIEWW, L"",
                            WS_CHILD | LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP,
                            0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kListId)), nullptr, nullptr);
    overview_ = CreateWindowExW(0, WC_LISTVIEWW, L"",
                                WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP,
                                0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOverviewListId)), nullptr, nullptr);
    overviewButton_ = CreateWindowExW(0, L"BUTTON", L"Overview", StorageModeRadioStyle(0),
                                      0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOverviewId)), nullptr, nullptr);
    categoryFilter_ = CreateWindowExW(0, WC_COMBOBOXW, L"",
                                      WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                                      0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCategoryFilterId)), nullptr, nullptr);
    drillDown_ = CreateWindowExW(0, L"BUTTON", L"Drill Down", StorageModeRadioStyle(1),
                                 0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDrillDownId)), nullptr, nullptr);
    largestFolders_ = CreateWindowExW(0, L"BUTTON", L"Largest Folders", StorageModeRadioStyle(2),
                                      0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kLargestFoldersId)), nullptr, nullptr);
    largestFiles_ = CreateWindowExW(0, L"BUTTON", L"Largest Files", StorageModeRadioStyle(3),
                                    0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kLargestFilesId)), nullptr, nullptr);
    byCategory_ = CreateWindowExW(0, L"BUTTON", L"By Category", StorageModeRadioStyle(4),
                                  0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kByCategoryId)), nullptr, nullptr);
    treemap_ = CreateWindowExW(0, L"BUTTON", L"Treemap", StorageModeRadioStyle(5),
                                   0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTreemapId)), nullptr, nullptr);

    if (!back_ || !up_ || !status_ || !list_ || !overview_ || !overviewButton_ || !categoryFilter_ ||
        !drillDown_ || !largestFolders_ || !largestFiles_ || !byCategory_ || !treemap_) return false;

    const HWND pushButtons[] = {back_, up_};
    for (size_t i = 0; i < std::size(pushButtons); ++i) {
        SetWindowSubclass(pushButtons[i], StorageButtonSubclassProc,
                          kStorageButtonSubclassBase + static_cast<UINT_PTR>(i),
                          reinterpret_cast<DWORD_PTR>(this));
    }
    const HWND radios[] = {overviewButton_, drillDown_, largestFolders_, largestFiles_, byCategory_, treemap_};
    for (size_t i = 0; i < kStorageRadioCount; ++i) {
        SetWindowSubclass(radios[i], RadioSubclassProc, kRadioSubclassBase + static_cast<UINT_PTR>(i),
                          reinterpret_cast<DWORD_PTR>(this));
    }
    SetWindowSubclass(status_, StatusSubclassProc, kStatusSubclassId, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(list_, ListSubclassProc, kStorageListSubclass, reinterpret_cast<DWORD_PTR>(this));
    for (HWND control : {list_, overview_}) {
        SetWindowSubclass(control, InactiveSelectionSubclassProc, kInactiveSelectionSubclass, reinterpret_cast<DWORD_PTR>(this));
    }

    const wchar_t* columns[] = {L"Name", L"Type", L"Size", L"Subtree Size", L"% of Parent", L"% of Volume", L"Modified", L"Size on Disk", L"Location"};
    const int widths[] = {260, 80, 100, 120, 100, 100, 145, 120, 0};
    for (int i = 0; i < 9; ++i) {
        LVCOLUMNW column{LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM};
        column.pszText = const_cast<wchar_t*>(columns[i]);
        column.cx = widths[i];
        column.iSubItem = i;
        ListView_InsertColumn(list_, i, &column);
    }
    ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    SetSortIndicator(list_, sortColumn_, sortAscending_);

    // storage-overview columns: volume, capacity trio, coverage, availability.
    const wchar_t* overviewColumns[] = {L"Volume", L"Total", L"Used", L"Free", L"% Used", L"Index Coverage"};
    const int overviewWidths[] = {120, 110, 110, 110, 80, 220};
    for (int i = 0; i < 6; ++i) {
        LVCOLUMNW column{LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM};
        column.pszText = const_cast<wchar_t*>(overviewColumns[i]);
        column.cx = overviewWidths[i];
        column.iSubItem = i;
        ListView_InsertColumn(overview_, i, &column);
    }
    ListView_SetExtendedListViewStyle(overview_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    // SysHeader32 NM_CUSTOMDRAW is delivered to the ListView, not the shell
    // owner. Subclass the headers and owner-paint tokens after columns exist.
    AttachThemedListHeader(list_);
    AttachThemedListHeader(overview_);
    PopulateCategoryFilter();

    // Wire the treemap into the storage panel so it renders the current folder
    // when the Treemap view mode is selected. Treemap navigation updates the
    // panel's current path and rebuilds the tree, then delegates to the shell.
    // Mouse clicks and UIA Invoke funnel through the same NavigateFromTreemap
    // mutation (semantic-treemap-uia 11/28).
    treemapView_.Initialize(owner_, engine_, [this](const std::wstring& path, bool isDirectory) {
        NavigateFromTreemap(path, isDirectory);
    });
    treemapView_.SetUiaEventHandler([this](const UiaEventNotification& notification) {
        uiaEvents_.Notify(notification);
    });
    treemapView_.SetCategoryFilter(&categoryEngine_, activeCategoryId_);
    // ui-v3 hardening: the panel initializes lazily on first open, after the
    // startup ApplyTheme already ran, so apply the live theme here or the two
    // report lists keep system-white backgrounds in dark mode.
    SetDarkTheme(ffui::gUiDarkTheme);
    UpdateNavigationButtonState();
    rankingCallbackState_ = std::make_shared<RankingCallbackState>();
    rankingCallbackState_->owner = owner_;
    rankingWorker_ = std::thread(&StorageAnalysis::RankingWorkerMain, this);
    return true;
}

// semantic-treemap-uia 11/28: the one real navigation mutation shared by the
// treemap mouse-click callback and the UI-thread side of a UIA Invoke.
void StorageAnalysis::NavigateFromTreemap(const std::wstring& path, bool isDirectory) {
    // Files are leaves: they are selected/inspected (the shell's navigate
    // handler opens them) but must never become the panel's scope anchor --
    // a file path in currentPath_ breaks Drill Down/Up/refresh lookups.
    if (isDirectory) {
        currentPath_ = path;
        UpdateNavigationButtonState();
        treemapView_.ShowAndFocus(path);
    }
    if (navigate_) navigate_(path, isDirectory);
}

// semantic-treemap-uia 11.x: a UIA Invoke may arrive on a UIAutomationCore
// worker thread. Never mutate UI state there -- marshal a value-only payload
// to the owning UI thread, which re-validates the stable identity against
// CURRENT state and drops it if the tile changed (generation/scope/identity).
void StorageAnalysis::TreemapActivateItem(const TreemapUiaItemInfo& item) {
    if (!item.invokable) return;
    const DWORD uiThread = GetWindowThreadProcessId(owner_, nullptr);
    if (uiThread != 0 && GetCurrentThreadId() != uiThread) {
        auto payload = std::make_unique<TreemapNavigatePayload>();
        payload->generation = item.generation;
        payload->scopeHash = item.scopeHash;
        payload->identityHash = item.identityHash;
        payload->fullPath = item.fullPath;
        payload->isDirectory = item.isDirectory;
        if (PostMessageW(owner_, WM_APP_TREEMAP_NAVIGATE, 0,
                         reinterpret_cast<LPARAM>(payload.get()))) {
            payload.release();
            return;
        }
        // PostMessage failed (e.g. the window is being torn down): drop the
        // action safely -- never run navigation on this worker thread.
        return;
    }
    // Already on the UI thread: re-validate and run the real mutation.
    TreemapUiaItemInfo current;
    if (!treemapView_.UiaFindTile(item.identityHash, current)) return;
    if (current.generation != item.generation || current.scopeHash != item.scopeHash) return;
    if (!current.invokable) return;
    NavigateFromTreemap(current.fullPath, current.isDirectory);
}

bool StorageAnalysis::HandleTreemapUiaNavigate(LPARAM lParam) {
    std::unique_ptr<TreemapNavigatePayload> payload(reinterpret_cast<TreemapNavigatePayload*>(lParam));
    if (!payload) return true;
    // Re-resolve the stable identity in CURRENT state; a stale queued action
    // (snapshot refresh, category filter, scope navigation, Hide, teardown)
    // must never retarget the new occupant of the same position.
    TreemapUiaItemInfo current;
    if (!treemapView_.UiaFindTile(payload->identityHash, current)) return true;
    if (current.generation != payload->generation || current.scopeHash != payload->scopeHash) return true;
    if (!current.invokable) return true;
    NavigateFromTreemap(current.fullPath, current.isDirectory);
    return true;
}

void StorageAnalysis::SetCategories(const std::vector<std::pair<std::wstring, std::wstring>>& categories) {
    categoryEngine_.LoadFromSettings(categories);
    if (categoryFilter_ != nullptr) {
        PopulateCategoryFilter();
        if (visible_) RefreshData();
        treemapView_.SetCategoryFilter(&categoryEngine_, activeCategoryId_);
    }
}

void StorageAnalysis::PopulateCategoryFilter() {
    if (!categoryFilter_) return;
    ComboBox_ResetContent(categoryFilter_);
    categoryFilterIds_.clear();
    categoryFilterIds_.push_back(L"");
    ComboBox_AddString(categoryFilter_, L"All categories");
    const auto& categories = categoryEngine_.GetCategories();
    for (const auto& cat : categories) {
        categoryFilterIds_.push_back(cat.id);
        ComboBox_AddString(categoryFilter_, const_cast<wchar_t*>(cat.displayName.c_str()));
    }
    categoryFilterIds_.push_back(categoryEngine_.UncategorizedFilterId());
    ComboBox_AddString(categoryFilter_, L"Other (uncategorized)");

    int selected = 0;
    for (size_t i = 1; i < categoryFilterIds_.size(); ++i) {
        if (categoryFilterIds_[i] == activeCategoryId_) {
            selected = static_cast<int>(i);
            break;
        }
    }
    if (selected == 0) activeCategoryId_.clear();
    ComboBox_SetCurSel(categoryFilter_, selected);
}

void StorageAnalysis::UpdateCategoryFilterFromControl() {
    activeCategoryId_.clear();
    if (!categoryFilter_) return;
    const int selected = ComboBox_GetCurSel(categoryFilter_);
    if (selected >= 0 && selected < static_cast<int>(categoryFilterIds_.size())) {
        activeCategoryId_ = categoryFilterIds_[static_cast<size_t>(selected)];
    }
}

void StorageAnalysis::ApplyCategoryFilter() {
    // storage-analysis 5.4: scope the displayed items to the selected
    // category, recalculating percentages relative to the filtered scope.
    // Directories stay (they can be drilled into); files whose category does
    // not match the filter are removed from the list.
    if (activeCategoryId_.empty()) return;
    items_.erase(std::remove_if(items_.begin(), items_.end(),
                                [this](const DrillItem& item) {
                                    if (item.isDirectory) return false;
                                    return !categoryEngine_.Matches(item.name, activeCategoryId_);
                                }),
                 items_.end());
}

void StorageAnalysis::ShowAndFocus(const std::wstring& currentPath, bool engineActive,
                                   const ShellLayoutRect& surfaceRect) {
    currentPath_ = currentPath;
    engineActive_ = engineActive;
    visible_ = true;
    UpdateNavigationButtonState();
    // Open on the storage overview (2.1): the volume list is the entry point
    // for capacity figures and per-volume analysis.
    CheckRadioButton(owner_, kOverviewId, kTreemapId, kOverviewId);
    viewMode_ = ViewMode::Overview;
    RefreshData();
    ApplyModeVisibility();
    Reposition(surfaceRect);
    SetFocus(overview_);
}

void StorageAnalysis::Hide() {
    if (!visible_) return;
    CancelRanking();
    HWND currentFocus = GetFocus();
    const bool storageHadFocus = (currentFocus != nullptr &&
                                  (currentFocus == list_ || currentFocus == overview_ ||
                                   currentFocus == categoryFilter_ || currentFocus == back_ ||
                                   currentFocus == up_ || currentFocus == overviewButton_ ||
                                   currentFocus == drillDown_ || currentFocus == largestFolders_ ||
                                   currentFocus == largestFiles_ || currentFocus == byCategory_ ||
                                   currentFocus == treemap_ ||
                                   IsChild(list_, currentFocus) || IsChild(overview_, currentFocus) ||
                                   IsChild(categoryFilter_, currentFocus)));
    visible_ = false;
    ApplyModeVisibility();
    items_.clear();
    volumes_.clear();
    ListView_SetItemCountEx(list_, 0, LVSICF_NOSCROLL);
    ListView_DeleteAllItems(overview_);
    if (storageHadFocus && owner_ != nullptr && IsWindow(owner_)) {
        SetFocus(owner_);
    }
}

void StorageAnalysis::Reposition(const ShellLayoutRect& surfaceRect) {
    lastSurfaceRect_ = surfaceRect;
    if (!visible_) return;
    const auto scaledDip = [](float dip) {
        return static_cast<int>(std::lround(ffui::UiScale(dip)));
    };
    const auto layout = ffui::ComputeStorageSurfaceLayout(surfaceRect);
    const auto place = [scaledDip](HWND control, const ShellLayoutRect& rect) {
        const int left = scaledDip(rect.left);
        const int top = scaledDip(rect.top);
        const int right = scaledDip(rect.right);
        const int bottom = scaledDip(rect.bottom);
        SetWindowPos(control, nullptr, left, top, (std::max)(0, right - left),
                     (std::max)(0, bottom - top), kNativeWorkspaceLayoutFlags);
    };
    place(back_, layout.buttons[0]);
    place(up_, layout.buttons[1]);
    place(overviewButton_, layout.buttons[2]);
    place(drillDown_, layout.buttons[3]);
    place(largestFolders_, layout.buttons[4]);
    place(largestFiles_, layout.buttons[5]);
    place(byCategory_, layout.buttons[6]);
    place(treemap_, layout.buttons[7]);
    place(categoryFilter_, layout.categoryFilter);
    const auto statusRect = StorageStatusRect(layout, viewMode_ == ViewMode::Treemap);
    place(status_, statusRect);
    place(list_, layout.list);
    place(overview_, layout.overview);
}

void StorageAnalysis::ApplyModeVisibility() {
    const auto apply = [this](HWND control, StorageControl semanticControl) {
        if (control == nullptr) return;
        const bool show = visible_ && StorageControlVisible(viewMode_, semanticControl);
        // ShowWindow(SW_SHOW) raises z-order and would cover the command palette.
        SetWindowPos(control, nullptr, 0, 0, 0, 0,
                     kNativeWorkspaceLayoutFlags | SWP_NOMOVE | SWP_NOSIZE |
                         (show ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    };

    apply(back_, StorageControl::Back);
    apply(up_, StorageControl::Up);
    apply(overviewButton_, StorageControl::OverviewButton);
    apply(drillDown_, StorageControl::DrillDownButton);
    apply(largestFolders_, StorageControl::LargestFoldersButton);
    apply(largestFiles_, StorageControl::LargestFilesButton);
    apply(byCategory_, StorageControl::ByCategoryButton);
    apply(treemap_, StorageControl::TreemapButton);
    apply(list_, StorageControl::List);
    apply(overview_, StorageControl::OverviewList);
    apply(categoryFilter_, StorageControl::CategoryFilter);
    apply(status_, StorageControl::Status);

    if (visible_ && StorageControlVisible(viewMode_, StorageControl::TreemapSurface)) {
        if (!treemapView_.Visible()) treemapView_.ShowAndFocus(currentPath_);
    } else {
        treemapView_.Hide();
    }
}

void StorageAnalysis::UpdateNavigationButtonState() {
    if (back_ != nullptr) EnableWindow(back_, close_ != nullptr ? TRUE : FALSE);
    if (up_ != nullptr) {
        const bool canNavigateUp = !currentPath_.empty() && !ffsearch::ParentPath(currentPath_).empty();
        EnableWindow(up_, canNavigateUp ? TRUE : FALSE);
    }
}

void StorageAnalysis::RefreshOverview() {
    std::vector<VolumeItem> prior = std::move(volumes_);
    volumes_.clear();

    // Storage-overview 2.1: fixed local volume enumeration, independent of
    // the filesystem index (GetDiskFreeSpaceEx needs no privileged path).
    const DWORD driveMask = GetLogicalDrives();
    const auto snapshot = engine_ ? engine_->ReadSnapshot() : std::nullopt;

    for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
        if ((driveMask & (1u << (letter - L'A'))) == 0) continue;
        wchar_t rootPath[] = {letter, L':', L'\\', L'\0'};
        if (GetDriveTypeW(rootPath) != DRIVE_FIXED) continue;

        VolumeItem vol;
        vol.rootPath = rootPath;

        // 2.2: total/used/free/percentage from the OS's own reporting.
        ULARGE_INTEGER total{}, freeAvail{};
        if (GetDiskFreeSpaceExW(rootPath, &freeAvail, &total, nullptr)) {
            vol.totalBytes = total.QuadPart;
            vol.freeBytes = freeAvail.QuadPart;
            vol.usedBytes = total.QuadPart > freeAvail.QuadPart
                                ? total.QuadPart - freeAvail.QuadPart
                                : 0;
        }

        // A successful root listing proves no recursive coverage. The UI
        // snapshot carries no whole-volume completeness certificate.
        vol.rootAvailable = false;
        if (engineActive_ && snapshot) {
            auto it = snapshot->find(rootPath);
            if (it != snapshot->end() &&
                it->second.status == ffprotocol::DirectoryEnumerationStatus::Success) {
                vol.rootAvailable = true;
            }
        }

        // 2.4: stale/unavailable handling -- a volume that fails capacity
        // retrieval keeps its last-known figures from the previous pass,
        // clearly labeled stale, with live drill-down disabled.
        if (vol.totalBytes == 0 && vol.freeBytes == 0) {
            vol.unavailable = true;
            for (const auto& p : prior) {
                if (p.rootPath == vol.rootPath && p.totalBytes > 0) {
                    vol.totalBytes = p.totalBytes;
                    vol.freeBytes = p.freeBytes;
                    vol.usedBytes = p.usedBytes;
                    break;
                }
            }
        }

        volumes_.push_back(std::move(vol));
    }

    // A volume that has vanished from GetLogicalDrives entirely is retained
    // from the previous pass, labeled stale, with live drill-down disabled.
    for (const auto& p : prior) {
        if (p.totalBytes == 0) continue;
        bool stillPresent = false;
        for (const auto& vol : volumes_) {
            if (vol.rootPath == p.rootPath) { stillPresent = true; break; }
        }
        if (!stillPresent) {
            VolumeItem stale = p;
            stale.unavailable = true;
            volumes_.push_back(std::move(stale));
        }
    }

    ListView_DeleteAllItems(overview_);
    for (size_t i = 0; i < volumes_.size(); ++i) {
        const VolumeItem& vol = volumes_[i];
        LVITEMW item{};
        item.iItem = static_cast<int>(i);
        item.mask = LVIF_TEXT;
        std::wstring name = vol.rootPath;
        // "Stale" implies prior figures exist; a volume that was never
        // readable is unavailable, not stale.
        if (vol.unavailable) name += vol.totalBytes > 0 ? kStaleSuffix : kUnavailableSuffix;
        item.pszText = const_cast<wchar_t*>(name.c_str());
        ListView_InsertItem(overview_, &item);

        wchar_t buffer[64]{};
        const std::wstring total = vol.unavailable && vol.totalBytes == 0
                                       ? L"—"
                                       : ffui::FormatSize(vol.totalBytes);
        const std::wstring used = vol.unavailable && vol.usedBytes == 0
                                      ? L"—"
                                      : ffui::FormatSize(vol.usedBytes);
        const std::wstring free = vol.unavailable && vol.freeBytes == 0
                                      ? L"—"
                                      : ffui::FormatSize(vol.freeBytes);
        std::wstring pct = L"—";
        if (vol.totalBytes > 0) {
            swprintf_s(buffer, L"%.1f%%",
                       static_cast<double>(vol.usedBytes) / static_cast<double>(vol.totalBytes) * 100.0);
            pct = buffer;
        }
        ListView_SetItemText(overview_, static_cast<int>(i), 1, const_cast<wchar_t*>(total.c_str()));
        ListView_SetItemText(overview_, static_cast<int>(i), 2, const_cast<wchar_t*>(used.c_str()));
        ListView_SetItemText(overview_, static_cast<int>(i), 3, const_cast<wchar_t*>(free.c_str()));
        ListView_SetItemText(overview_, static_cast<int>(i), 4, const_cast<wchar_t*>(pct.c_str()));
        ListView_SetItemText(overview_, static_cast<int>(i), 5,
                             const_cast<wchar_t*>(StorageCoverageText(vol.rootAvailable)));
    }

    const std::wstring statusText = StorageOverviewStatusText(volumes_.size(), engineActive_);
    SetWindowTextW(status_, statusText.c_str());
}

void StorageAnalysis::RefreshData() {
    CancelRanking();
    ConfigureListColumns();
    NotifyListChanged();
    const auto selected = displayedPath_ == currentPath_ ? CaptureSelectedItem() : std::nullopt;
    if (RecursiveRanking()) {
        if (rankingSelectionScope_ != currentPath_ || selected || !items_.empty()) rankingSelected_ = selected;
        rankingSelectionScope_ = currentPath_;
    } else {
        rankingSelected_.reset();
        rankingSelectionScope_.clear();
    }
    ListView_SetItemState(list_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    displayedPath_ = currentPath_;
    if (!visible_) return;
    UpdateNavigationButtonState();

    // semantic-treemap-uia 22.x: percentage-of-volume denominator from the
    // same OS ground truth as the overview; the treemap metadata uses the
    // identical value.
    UpdateVolumeUsedBytes();
    treemapView_.SetVolumeInfo({volumeUsedBytes_, volumeUsedKnown_});

    // A refresh invalidates all outstanding row requests. Request IDs are
    // monotonic, so a late callback from the previous listing cannot be
    // mistaken for a row in this listing.
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pending_.clear();
    }
    parentAggregateTotal_ = 0;
    parentAggregateKnown_ = false;
    parentAggregatePending_ = false;
    parentRequestId_ = 0;

    // storage-analysis 2.1: the overview is the entry point; list mode views
    // are skipped entirely while it is active.
    if (viewMode_ == ViewMode::Overview) {
        RefreshOverview();
        return;
    }
    if (RecursiveRanking()) {
        RefreshRanking();
        return;
    }
    if (!engine_) {
        items_.clear();
        ListView_SetItemCountEx(list_, 0, LVSICF_NOSCROLL);
        SetWindowTextW(status_, StorageWaitingStatusText(engineActive_));
        return;
    }
    // ui-v3 hardening V3-07: a scope (re)load starts in the calculating state.
    treemapView_.SetEmptyStateText(L"Calculating folder sizes…");
    const auto snapshot = engine_->ReadSnapshot();
    if (!snapshot) {
        items_.clear();
        ListView_SetItemCountEx(list_, 0, LVSICF_NOSCROLL);
        SetWindowTextW(status_, StorageWaitingStatusText(engineActive_));
        return;
    }

    std::wstring targetPath = currentPath_;
    if (targetPath.empty()) targetPath = L"";

    auto it = snapshot->find(targetPath);
    if (it == snapshot->end()) {
        SetWindowTextW(status_, StorageNotIndexedStatusText(engineActive_));
        items_.clear();
        ListView_SetItemCountEx(list_, 0, LVSICF_NOSCROLL);
        return;
    }

    const auto& directory = it->second;
    if (directory.status != ffprotocol::DirectoryEnumerationStatus::Success) {
        SetWindowTextW(status_, StorageUnavailableStatusText(engineActive_));
        items_.clear();
        ListView_SetItemCountEx(list_, 0, LVSICF_NOSCROLL);
        return;
    }

    items_.clear();
    items_.reserve(directory.entries.size());
    for (const auto& entry : directory.entries) {
        DrillItem item;
        item.name = entry.name;
        item.folder = targetPath;
        item.isDirectory = entry.isDirectory;
        item.sizeBytes = entry.sizeBytes;
        item.totalSizeBytes = entry.isDirectory ? 0 : entry.sizeBytes;
        // allocated-size-storage-accuracy 7.1: the entry's own size-on-disk
        // rides the snapshot with an explicit known state.
        item.allocatedSizeBytes = entry.allocatedSizeBytes;
        item.allocatedSizeKnown = entry.allocatedSizeKnown;
        item.attributes = entry.attributes;
        item.creationTime = entry.creationTime;
        item.lastModifiedTime = entry.lastModifiedTime;
        item.volumeRowId = entry.volumeRowId;
        item.fileIdLow = entry.fileIdLow;
        item.fileIdHigh = entry.fileIdHigh;
        item.parentFrnLow = entry.parentIdLow;
        item.parentFrnHigh = entry.parentIdHigh;
        item.calculating = entry.isDirectory && entry.volumeRowId != 0;
        if (entry.isDirectory && entry.volumeRowId == 0) {
            // Degraded snapshots do not have FRNs or a durable projection to
            // answer the aggregate request.  Sum the portion already present
            // in the immutable snapshot instead of leaving every directory in
            // a perpetual "Calculating" state.  Missing descendants remain
            // explicitly partial and therefore cannot produce a misleading
            // percentage or claim complete volume coverage.
            const auto aggregate = ffui::AggregateSnapshotDirectory(
                *snapshot, ffui::JoinSnapshotPath(targetPath, entry.name));
            item.totalSizeBytes = aggregate.totalSizeBytes;
            item.totalAllocatedSizeBytes = aggregate.totalAllocatedSizeBytes;
            item.allocatedKnownCount = aggregate.allocatedBytesKnownCount;
            item.allocatedTotalCount = aggregate.allocatedBytesTotalCount;
            item.calculating = false;
            item.aggregateFailed = !aggregate.complete;
        }
        items_.push_back(item);
    }

    const std::wstring statusText = StorageItemsStatusText(items_.size(), engineActive_) +
        (viewMode_ == ViewMode::LargestFiles && !activeCategoryId_.empty()
            ? L" — selected category, immediate files only" : L"");
    SetWindowTextW(status_, viewMode_ == ViewMode::Treemap ? StorageTreemapStatusText() : statusText.c_str());

    // Filter based on view mode
    if (viewMode_ == ViewMode::LargestFolders) {
        items_.erase(std::remove_if(items_.begin(), items_.end(),
                                    [](const DrillItem& item) { return !item.isDirectory; }),
                     items_.end());
    } else if (viewMode_ == ViewMode::LargestFiles) {
        items_.erase(std::remove_if(items_.begin(), items_.end(),
                                    [](const DrillItem& item) { return item.isDirectory; }),
                     items_.end());
    } else if (viewMode_ == ViewMode::ByCategory) {
        std::map<std::wstring, DrillItem> categoryMap;
        for (const auto& item : items_) {
            if (item.isDirectory) continue;
            auto match = categoryEngine_.Match(item.name);
            std::wstring catName = match.matched ? match.categoryName : L"Other (uncategorized)";
            const auto categoryId = match.matched ? match.categoryId : categoryEngine_.UncategorizedFilterId();
            auto& catItem = categoryMap[categoryId];
            catItem.name = catName;
            catItem.categoryId = categoryId;
            catItem.isDirectory = false;
            catItem.sizeBytes += item.sizeBytes;
            catItem.totalSizeBytes += item.sizeBytes;
            catItem.calculating = false;
        }
        items_.clear();
        for (auto& [name, item] : categoryMap) {
            items_.push_back(std::move(item));
        }
    }

    // storage-analysis 5.4: category filter applies to the drill-down,
    // largest-folders, and largest-files listings (not the aggregate
    // by-category view, which already groups by category).
    if (viewMode_ != ViewMode::ByCategory) {
        ApplyCategoryFilter();
    }

    if (directory.volumeRowId == 0) {
        const auto aggregate = ffui::AggregateSnapshotDirectory(*snapshot, targetPath);
        parentAggregateTotal_ = aggregate.totalSizeBytes;
        parentAggregateKnown_ = aggregate.complete;
        parentAggregatePending_ = false;
    } else {
        RequestAggregateForParent(directory);
    }
    ListView_SetItemCountEx(list_, static_cast<int>(items_.size()), LVSICF_NOINVALIDATEALL);
    InvalidateRect(list_, nullptr, FALSE);

    // Request aggregates for all directories
    for (size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].isDirectory && items_[i].volumeRowId != 0) {
            items_[i].requestId = nextRequestId_++;
            RequestAggregateForItem(i);
        }
    }

    SortItems(sortColumn_, sortAscending_);
    RestoreSelectedItem(selected);
}

void StorageAnalysis::CancelRanking() {
    rankingGeneration_.fetch_add(1, std::memory_order_acq_rel);
    {
        std::lock_guard lock(rankingWorkMutex_);
        pendingRanking_.reset();
    }
    if (engine_ && indexedRankingRequestId_ != 0) engine_->CancelIndexedSearch(indexedRankingRequestId_);
    indexedRankingRequestId_ = 0;
    rankingWorkCv_.notify_all();
}

void StorageAnalysis::StopRanking() {
    if (rankingStopping_.exchange(true)) return;
    CancelRanking();
    if (rankingCallbackState_) {
        std::lock_guard lock(rankingCallbackState_->mutex);
        rankingCallbackState_->alive = false;
        rankingCallbackState_->owner = nullptr;
    }
    rankingWorkCv_.notify_all();
    if (rankingWorker_.joinable()) rankingWorker_.join();
    if (owner_ != nullptr) {
        MSG message{};
        while (PeekMessageW(&message, owner_, WM_APP_STORAGE_RANKING, WM_APP_STORAGE_RANKING, PM_REMOVE)) {
            delete reinterpret_cast<RankingCompletion*>(message.lParam);
        }
    }
}

void StorageAnalysis::ConfigureListColumns() {
    if (!list_) return;
    const bool recursive = RecursiveRanking();
    if (recursiveColumns_ && *recursiveColumns_ == recursive && columnsDpi_ == UiDpiScale()) return;
    recursiveColumns_ = recursive;
    columnsDpi_ = UiDpiScale();
    if (!recursive && sortColumn_ == 8) {
        sortColumn_ = 3;
        sortAscending_ = false;
        SetSortIndicator(list_, sortColumn_, sortAscending_);
    }
    const int normalWidths[] = {260, 80, 100, 120, 100, 100, 145, 120, 0};
    const int rankingWidths[] = {220, 0, 100, 0, 0, 0, 145, 120, 300};
    int normalOrder[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    int rankingOrder[] = {0, 2, 8, 7, 6, 1, 3, 4, 5};
    for (int index = 0; index < 9; ++index) {
        ListView_SetColumnWidth(list_, index, Scaled(recursive ? rankingWidths[index] : normalWidths[index]));
    }
    ListView_SetColumnOrderArray(list_, 9, recursive ? rankingOrder : normalOrder);
}

void StorageAnalysis::RefreshRanking() {
    items_.clear();
    ListView_SetItemCountEx(list_, 0, LVSICF_NOSCROLL);
    InvalidateRect(list_, nullptr, FALSE);
    const auto scope = NormalizeStorageRankingScope(currentPath_);
    if (!scope || !engine_ || rankingStopping_) {
        SetWindowTextW(status_, L"Choose an absolute folder to rank its known files and subfolders.");
        return;
    }
    const uint64_t generation = rankingGeneration_.load(std::memory_order_acquire);
    if (!scope->indexedSupported) {
        QueueSnapshotRanking(generation, *scope);
        return;
    }
    SetWindowTextW(status_, L"Finding largest indexed known files in this folder and subfolders…");
    const auto weak = std::weak_ptr<RankingCallbackState>(rankingCallbackState_);
    indexedRankingRequestId_ = engine_->RequestIndexedSearch(L"kind:file", 1, scope->matchPath, 3, true,
        static_cast<uint32_t>(kStorageRankingLimit),
        [weak, generation, scope = *scope](ffprotocol::IndexedSearchResponse response) {
            auto state = weak.lock();
            if (!state) return;
            auto completion = std::make_unique<RankingCompletion>();
            completion->generation = generation;
            completion->scope = scope;
            completion->indexed = true;
            completion->available = response.status == ffprotocol::IndexedSearchStatus::Completed &&
                response.candidates.size() <= kStorageRankingLimit;
            completion->truncated = response.truncated;
            for (auto& candidate : response.candidates) {
                if (candidate.isDirectory || !candidate.persistentIndexed || !StorageRankingContains(scope, candidate.folder) ||
                    StorageRankingItemPath(candidate.folder, candidate.name).empty()) {
                    completion->available = false;
                    break;
                }
                ffsearch::Candidate item;
                item.name = std::move(candidate.name);
                item.folder = std::move(candidate.folder);
                item.sizeBytes = candidate.sizeBytes;
                item.modifiedTime = candidate.modifiedTime;
                item.createdTime = candidate.createdTime;
                item.volumeId = candidate.volumeId;
                item.id = candidate.id;
                item.parentId = candidate.parentId;
                item.persistentIndexed = candidate.persistentIndexed;
                item.allocatedSizeBytes = candidate.allocatedSizeBytes;
                item.allocatedSizeKnown = candidate.allocatedSizeKnown;
                completion->candidates.push_back(std::move(item));
            }
            std::lock_guard lock(state->mutex);
            if (state->alive && state->owner != nullptr && PostMessageW(state->owner, WM_APP_STORAGE_RANKING,
                    0, reinterpret_cast<LPARAM>(completion.get()))) completion.release();
        });
}

void StorageAnalysis::QueueSnapshotRanking(uint64_t generation, StorageRankingScope scope) {
    if (rankingStopping_ || generation != rankingGeneration_.load(std::memory_order_acquire)) return;
    SetWindowTextW(status_, L"Ranking browsed/published subfolders only — coverage partial…");
    {
        std::lock_guard lock(rankingWorkMutex_);
        pendingRanking_ = RankingWork{generation, std::move(scope)};
    }
    rankingWorkCv_.notify_one();
}

void StorageAnalysis::RankingWorkerMain() {
    while (!rankingStopping_) {
        RankingWork work;
        {
            std::unique_lock lock(rankingWorkMutex_);
            rankingWorkCv_.wait(lock, [this] { return rankingStopping_ || pendingRanking_.has_value(); });
            if (rankingStopping_) break;
            work = std::move(*pendingRanking_);
            pendingRanking_.reset();
        }
        const auto cancelled = [this, generation = work.generation] {
            return rankingStopping_ || generation != rankingGeneration_.load(std::memory_order_acquire);
        };
        const auto snapshot = engine_->ReadSnapshot();
        if (cancelled()) continue;
        auto completion = std::make_unique<RankingCompletion>();
        completion->generation = work.generation;
        completion->scope = work.scope;
        completion->available = snapshot.has_value();
        if (snapshot) {
            auto ranked = RankKnownStorageSnapshot(*snapshot, work.scope, cancelled);
            if (ranked.cancelled || cancelled()) continue;
            completion->truncated = ranked.truncated;
            for (auto& result : ranked.results) completion->candidates.push_back(std::move(result.candidate));
        }
        if (cancelled()) continue;
        std::lock_guard lock(rankingCallbackState_->mutex);
        if (rankingCallbackState_->alive && rankingCallbackState_->owner != nullptr &&
            PostMessageW(rankingCallbackState_->owner, WM_APP_STORAGE_RANKING, 0,
                         reinterpret_cast<LPARAM>(completion.get()))) completion.release();
    }
}

bool StorageAnalysis::HandleRankingCompletion(LPARAM lParam) {
    std::unique_ptr<RankingCompletion> completion(reinterpret_cast<RankingCompletion*>(lParam));
    if (!completion || rankingStopping_ || !StorageRankingCompletionMatches(completion->generation,
        rankingGeneration_.load(std::memory_order_acquire), currentPath_, completion->scope,
        visible_, RecursiveRanking())) return true;
    indexedRankingRequestId_ = 0;
    // An empty durable result must not hide useful degraded/browsed records.
    // The fallback is always labelled published/browsed partial coverage.
    if (completion->indexed && (!completion->available || completion->candidates.empty())) {
        QueueSnapshotRanking(completion->generation, completion->scope);
        return true;
    }
    NotifyListChanged();
    const auto selected = rankingSelected_ ? rankingSelected_ : CaptureSelectedItem();
    ListView_SetItemState(list_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    items_.clear();
    for (auto& candidate : completion->candidates) {
        DrillItem item;
        item.name = std::move(candidate.name);
        item.folder = std::move(candidate.folder);
        item.sizeBytes = item.totalSizeBytes = candidate.sizeBytes;
        item.lastModifiedTime = candidate.modifiedTime;
        item.creationTime = candidate.createdTime;
        item.volumeRowId = candidate.volumeId;
        item.fileIdLow = candidate.id;
        item.parentFrnLow = candidate.parentId;
        item.allocatedSizeBytes = candidate.allocatedSizeBytes;
        item.allocatedSizeKnown = candidate.allocatedSizeKnown;
        items_.push_back(std::move(item));
    }
    ListView_SetItemCountEx(list_, static_cast<int>(items_.size()), LVSICF_NOINVALIDATEALL);
    SetWindowTextW(status_, completion->available
        ? StorageRankingStatusText(items_.size(), completion->indexed, completion->truncated).c_str()
        : L"No published snapshot available — subfolder coverage unknown. Browse folders or retry.");
    SortItems(sortColumn_, sortAscending_);
    RestoreSelectedItem(selected);
    InvalidateRect(list_, nullptr, FALSE);
    return true;
}

void StorageAnalysis::RequestAggregateForParent(const ffprotocol::SnapshotDirectory& directory) {
    if (!engine_ || directory.volumeRowId == 0) return;
    const uint64_t requestId = nextRequestId_++;
    parentRequestId_ = requestId;
    parentAggregatePending_ = true;
    engine_->RequestFolderAggregate(
        directory.volumeRowId, directory.directoryIdLow, directory.directoryIdHigh,
        [this, requestId](uint64_t /*reqId*/, ffprotocol::FolderAggregateStatus status,
               uint64_t itemCount, uint64_t totalSizeBytes, uint64_t totalAllocatedSizeBytes,
               uint64_t allocatedBytesKnownCount, uint64_t allocatedBytesTotalCount) {
            ffui::PostFolderAggregateResult(owner_, WM_APP_STORAGE_AGGREGATE,
                                             requestId, status,
                                             itemCount, totalSizeBytes, totalAllocatedSizeBytes,
                                             allocatedBytesKnownCount, allocatedBytesTotalCount);
        });
}

void StorageAnalysis::RequestAggregateForItem(size_t index) {
    if (!engine_ || index >= items_.size()) return;
    const DrillItem& item = items_[index];
    if (!item.isDirectory || item.volumeRowId == 0) return;

    uint64_t requestId = item.requestId;
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pending_.push_back({requestId, static_cast<uint32_t>(index)});
    }

    engine_->RequestFolderAggregate(
        item.volumeRowId, item.fileIdLow, item.fileIdHigh,
        [this, requestId](uint64_t /*reqId*/, ffprotocol::FolderAggregateStatus status,
                          uint64_t itemCount, uint64_t totalSizeBytes, uint64_t totalAllocatedSizeBytes,
                          uint64_t allocatedBytesKnownCount, uint64_t allocatedBytesTotalCount) {
            // EngineClient's callback is already scoped to this request.  The
            // storage view intentionally uses its own compact request IDs for
            // ListView bookkeeping, so comparing the engine's global ID here
            // would discard every aggregate result after the first request.
            ffui::PostFolderAggregateResult(owner_, WM_APP_STORAGE_AGGREGATE, requestId, status, itemCount,
                                            totalSizeBytes, totalAllocatedSizeBytes,
                                            allocatedBytesKnownCount, allocatedBytesTotalCount);
        });
}

std::pair<uint64_t, bool> StorageAnalysis::CurrentParentTotals() const {
    // storage-analysis 5.4: with a category filter active, "% of Parent" is
    // recalculated relative to the FILTERED scope (matching the treemap's
    // filtered root total), so the denominator comes from the retained rows
    // instead of the engine's unfiltered aggregate.
    if (!activeCategoryId_.empty()) {
        uint64_t total = 0;
        bool known = true;
        for (const auto& item : items_) {
            if (item.isDirectory && (item.calculating || item.aggregateFailed)) {
                known = false;
            }
            if (UINT64_MAX - total < item.totalSizeBytes) {
                // A representationally impossible parent total must not wrap
                // and produce a plausible-looking percentage.
                return {0, false};
            }
            total += item.totalSizeBytes;
        }
        return {total, known};
    }
    if (parentAggregateKnown_) {
        return {parentAggregateTotal_, true};
    }
    if (parentAggregatePending_) {
        return {0, false};
    }
    uint64_t total = 0;
    bool known = true;
    for (const auto& item : items_) {
        if (item.isDirectory && (item.calculating || item.aggregateFailed)) {
            known = false;
        }
        if (UINT64_MAX - total < item.totalSizeBytes) {
            // A representationally impossible parent total must not wrap and
            // produce a plausible-looking percentage.
            return {0, false};
        }
        total += item.totalSizeBytes;
    }
    return {total, known};
}

void StorageAnalysis::HandleAggregateResult(uint64_t requestId,
                                            ffprotocol::FolderAggregateStatus status,
                                            uint64_t /*itemCount*/, uint64_t totalSizeBytes,
                                            uint64_t totalAllocatedSizeBytes,
                                            uint64_t allocatedBytesKnownCount,
                                            uint64_t allocatedBytesTotalCount) {
    if (requestId == parentRequestId_) {
        parentAggregatePending_ = false;
        parentAggregateKnown_ = status == ffprotocol::FolderAggregateStatus::Resolved;
        parentAggregateTotal_ = parentAggregateKnown_ ? totalSizeBytes : 0;
        // The percentage and size sort keys depend on the parent aggregate.
        // Re-sort after the asynchronous parent result arrives so an owner-
        // data list does not keep the provisional order indefinitely.
        SortItems(sortColumn_, sortAscending_);
        ListView_SetItemCountEx(list_, static_cast<int>(items_.size()), LVSICF_NOINVALIDATEALL);
        InvalidateRect(list_, nullptr, FALSE);
        return;
    }
    std::lock_guard<std::mutex> lock(pendingMutex_);
    auto it = std::find_if(pending_.begin(), pending_.end(),
                           [requestId](const PendingRequest& r) { return r.requestId == requestId; });
    if (it == pending_.end()) return;
    pending_.erase(it);

    auto itemIt = std::find_if(items_.begin(), items_.end(),
                               [requestId](const DrillItem& item) { return item.requestId == requestId; });
    if (itemIt == items_.end()) return;

    if (status == ffprotocol::FolderAggregateStatus::Resolved) {
        itemIt->totalSizeBytes = totalSizeBytes;
        // allocated-size-storage-accuracy 7.1: store the aggregate's
        // allocated subtotal plus the completeness pair; the rendering
        // layer decides exact / partial / unknown from the counts.
        itemIt->totalAllocatedSizeBytes = totalAllocatedSizeBytes;
        itemIt->allocatedKnownCount = allocatedBytesKnownCount;
        itemIt->allocatedTotalCount = allocatedBytesTotalCount;
        itemIt->calculating = false;
        itemIt->aggregateFailed = false;
    } else if (status == ffprotocol::FolderAggregateStatus::NotFound) {
        itemIt->totalSizeBytes = 0;
        itemIt->totalAllocatedSizeBytes = 0;
        itemIt->allocatedKnownCount = 0;
        itemIt->allocatedTotalCount = 0;
        itemIt->calculating = false;
        itemIt->aggregateFailed = true;
    }

    // Folder size and percentage columns are sortable; the completed value can
    // change the item's relative position, so refresh the ordering before the
    // owner-data list repaints.
    SortItems(sortColumn_, sortAscending_);
    ListView_SetItemCountEx(list_, static_cast<int>(items_.size()), LVSICF_NOINVALIDATEALL);
    InvalidateRect(list_, nullptr, FALSE);
}

// semantic-treemap-uia 22.x: the "overall volume" denominator for
// percentage-of-volume. Same GetDiskFreeSpaceExW ground truth as the storage
// overview (RefreshOverview); the volume hosting the current scope.
void StorageAnalysis::UpdateVolumeUsedBytes() {
    volumeUsedKnown_ = false;
    volumeUsedBytes_ = 0;
    if (currentPath_.empty() || currentPath_[0] == L'\0') return;
    wchar_t root[4] = {currentPath_[0], L':', L'\\', L'\0'};
    if (currentPath_.size() >= 2 && currentPath_[1] != L':') return;
    ULARGE_INTEGER total{}, freeAvail{};
    if (GetDiskFreeSpaceExW(root, &freeAvail, &total, nullptr)) {
        volumeUsedBytes_ = total.QuadPart > freeAvail.QuadPart ? total.QuadPart - freeAvail.QuadPart : 0;
        volumeUsedKnown_ = true;
    }
}

void StorageAnalysis::SortItems(int column, bool ascending) {
    if (column < 0 || column >= 9) return;
    NotifyListChanged();
    const auto selected = CaptureSelectedItem();
    ListView_SetItemState(list_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    sortColumn_ = column;
    sortAscending_ = ascending;
    SetSortIndicator(list_, column, ascending);

    if (RecursiveRanking()) {
        const auto ordinal = [](const std::wstring& left, const std::wstring& right) {
            return CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()), right.c_str(),
                                        static_cast<int>(right.size()), TRUE) - CSTR_EQUAL;
        };
        std::sort(items_.begin(), items_.end(), [column, ascending, ordinal](const DrillItem& left, const DrillItem& right) {
            int order = 0;
            if (column == 7 && left.allocatedSizeKnown != right.allocatedSizeKnown) return left.allocatedSizeKnown;
            if (column == 2 || column == 3 || column == 6 || column == 7) {
                const uint64_t a = column == 6 ? left.lastModifiedTime : column == 7 ? left.allocatedSizeBytes : left.sizeBytes;
                const uint64_t b = column == 6 ? right.lastModifiedTime : column == 7 ? right.allocatedSizeBytes : right.sizeBytes;
                order = a == b ? 0 : a < b ? -1 : 1;
            } else if (column == 8) {
                order = ordinal(left.folder, right.folder);
            }
            if (order == 0) order = ordinal(left.name, right.name);
            if (order == 0) order = ordinal(left.folder, right.folder);
            if (order == 0 && left.volumeRowId != right.volumeRowId) order = left.volumeRowId < right.volumeRowId ? -1 : 1;
            if (order == 0 && left.fileIdLow != right.fileIdLow) order = left.fileIdLow < right.fileIdLow ? -1 : 1;
            return ascending ? order < 0 : order > 0;
        });
        ListView_SetItemCountEx(list_, static_cast<int>(items_.size()), LVSICF_NOINVALIDATEALL);
        RestoreSelectedItem(selected);
        InvalidateRect(list_, nullptr, FALSE);
        return;
    }

    struct SortKey {
        int group = 0;
        bool known = true;
        double value = 0.0;
    };
    const auto [parentTotal, parentKnown] = CurrentParentTotals();
    auto getSortKey = [this, parentTotal, parentKnown](const DrillItem& item, int col) -> SortKey {
        switch (col) {
            case 0: return {item.isDirectory ? 0 : 1, true, 0.0}; // name: folders first, then name
            case 1: return {item.isDirectory ? 0 : 1, true, 0.0}; // type: folders first
            case 2: return {item.isDirectory ? 0 : 1, true, static_cast<double>(item.sizeBytes)}; // size
            case 3: return {item.isDirectory ? 0 : 1, !item.calculating && !item.aggregateFailed,
                                   static_cast<double>(item.totalSizeBytes)}; // subtree size
            case 4: {
                const auto percentage = ComputeStoragePercentage(
                    item.totalSizeBytes, parentTotal, parentKnown &&
                    (!item.isDirectory || (!item.calculating && !item.aggregateFailed)));
                return {item.isDirectory ? 0 : 1, percentage.known, percentage.sortKey};
            }
            case 5: {
                // semantic-treemap-uia 22.x: percentage of the overall volume
                // (OS used bytes). Unknown denominators/items sort last.
                const auto percentage = ComputeStoragePercentage(
                    item.totalSizeBytes, volumeUsedBytes_, volumeUsedKnown_ &&
                    (!item.isDirectory || (!item.calculating && !item.aggregateFailed)));
                return {item.isDirectory ? 0 : 1, percentage.known, percentage.sortKey};
            }
            case 6: return {0, true, static_cast<double>(item.lastModifiedTime)}; // modified
            case 7:
                // allocated-size-storage-accuracy 7.1: size-on-disk column --
                // files sort on their own allocated size (known first),
                // directories on the aggregate's known subtotal.
                if (item.isDirectory) {
                    return {item.isDirectory ? 0 : 1,
                            !item.calculating && !item.aggregateFailed && item.allocatedKnownCount > 0,
                            static_cast<double>(item.totalAllocatedSizeBytes)};
                }
                return {1, item.allocatedSizeKnown, static_cast<double>(item.allocatedSizeBytes)};
            default: return {0, true, 0.0};
        }
    };

    std::sort(items_.begin(), items_.end(), [this, column, ascending, getSortKey](const DrillItem& a, const DrillItem& b) {
        const auto keyA = getSortKey(a, column);
        const auto keyB = getSortKey(b, column);
        if (keyA.group != keyB.group) return ascending ? keyA.group < keyB.group : keyA.group > keyB.group;
        if (keyA.known != keyB.known) return ascending ? keyA.known > keyB.known : keyA.known < keyB.known;
        const bool equalValue = (column == 4 || column == 5)
            ? StoragePercentageNearlyEqual(keyA.value, keyB.value)
            : keyA.value == keyB.value;
        if (!equalValue) {
            return ascending ? keyA.value < keyB.value : keyA.value > keyB.value;
        }
        // tie-break on name
        return ascending ? _wcsicmp(a.name.c_str(), b.name.c_str()) < 0 : _wcsicmp(a.name.c_str(), b.name.c_str()) > 0;
    });

    ListView_SetItemCountEx(list_, static_cast<int>(items_.size()), LVSICF_NOINVALIDATEALL);
    RestoreSelectedItem(selected);
    InvalidateRect(list_, nullptr, FALSE);
}

std::optional<StorageAnalysis::DrillItem> StorageAnalysis::CaptureSelectedItem() const {
    const int selected = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
    if (selected < 0 || static_cast<size_t>(selected) >= items_.size()) return std::nullopt;
    return items_[static_cast<size_t>(selected)];
}

void StorageAnalysis::RestoreSelectedItem(const std::optional<DrillItem>& selected) {
    if (!selected) return;
    for (size_t index = 0; index < items_.size(); ++index) {
        const auto& item = items_[index];
        if (item.name != selected->name || item.folder != selected->folder || item.categoryId != selected->categoryId ||
            item.isDirectory != selected->isDirectory || item.volumeRowId != selected->volumeRowId ||
            item.fileIdLow != selected->fileIdLow || item.fileIdHigh != selected->fileIdHigh) continue;
        ListView_SetItemState(list_, static_cast<int>(index), LVIS_SELECTED | LVIS_FOCUSED,
                             LVIS_SELECTED | LVIS_FOCUSED);
        break;
    }
}

void StorageAnalysis::OnSnapshotUpdated() {
    if (!visible_) return;
    RefreshData();
    treemapView_.OnSnapshotUpdated();
}

void StorageAnalysis::SetTreemapGeometry(const UiaViewGeometry& geometry) {
    std::lock_guard<std::mutex> lock(treemapGeometryMutex_);
    treemapGeometry_ = geometry;
}

UiaViewGeometry StorageAnalysis::TreemapGeometry() const {
    std::lock_guard<std::mutex> lock(treemapGeometryMutex_);
    return treemapGeometry_;
}

void StorageAnalysis::RenderTreemap(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory,
                                    const ShellLayoutRect& surfaceRect) {
    if (viewMode_ != ViewMode::Treemap) return;
    const auto layout = ComputeStorageSurfaceLayout(surfaceRect);
    const ShellLayoutRect& treemapRect = layout.list;
    D2D1_MATRIX_3X2_F priorTransform{};
    context->GetTransform(&priorTransform);
    // The shell enters this branch translated to the content origin. The
    // native Storage controls use global DIPs, so render the D2D canvas into
    // the same list rectangle and restore the shell transform for overlays.
    context->SetTransform(D2D1::Matrix3x2F::Translation(treemapRect.left, treemapRect.top));
    treemapView_.EnsureCreated(context, dwriteFactory);
    treemapView_.Render(context, dwriteFactory,
                        D2D1::SizeF(treemapRect.Width(), treemapRect.Height()),
                        treemapRect.left, treemapRect.top);
    context->SetTransform(priorTransform);
}

bool StorageAnalysis::HandleOwnerCommand(WPARAM wParam, LPARAM) {
    if (!visible_) return false;
    const int id = LOWORD(wParam);
    if (id == kCategoryFilterId && HIWORD(wParam) == CBN_SELCHANGE) {
        UpdateCategoryFilterFromControl();
        RefreshData();
        treemapView_.SetCategoryFilter(&categoryEngine_, activeCategoryId_);
        return true;
    }
    if (id == kBackId) {
        if (close_) close_();
        return true;
    }
    if (id == kUpId) {
        if (!currentPath_.empty()) {
            const std::wstring parent = ffsearch::ParentPath(currentPath_);
            if (!parent.empty()) {
                currentPath_ = parent;
                UpdateNavigationButtonState();
                RefreshData();
                if (viewMode_ == ViewMode::Treemap) {
                    treemapView_.ShowAndFocus(currentPath_);
                }
                if (navigate_) navigate_(currentPath_, true);
            }
        }
        return true;
    }
    if (id == kOverviewId) {
        SetViewMode(ViewMode::Overview);
        return true;
    }
    if (id == kDrillDownId) {
        SetViewMode(ViewMode::DrillDown);
        return true;
    }
    if (id == kLargestFoldersId) {
        SetViewMode(ViewMode::LargestFolders);
        return true;
    }
    if (id == kLargestFilesId) {
        SetViewMode(ViewMode::LargestFiles);
        return true;
    }
    if (id == kByCategoryId) {
        SetViewMode(ViewMode::ByCategory);
        return true;
    }
    if (id == kTreemapId) {
        SetViewMode(ViewMode::Treemap);
        return true;
    }
    return false;
}

bool StorageAnalysis::HandleDrawItem(LPARAM lParam) {
    const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
    if (item == nullptr) return false;
    if (item->hwndItem != overviewButton_ && item->hwndItem != drillDown_ &&
        item->hwndItem != largestFolders_ && item->hwndItem != largestFiles_ &&
        item->hwndItem != byCategory_ && item->hwndItem != treemap_) {
        return false;
    }
    DrawStorageRadioButton(item, darkTheme_);
    return true;
}

void StorageAnalysis::SetViewMode(ViewMode mode) {
    if (viewMode_ == mode) {
        ApplyModeVisibility();
        Reposition(lastSurfaceRect_);
        return;
    }
    viewMode_ = mode;
    if (RecursiveRanking()) {
        sortColumn_ = 2;
        sortAscending_ = false;
        SetSortIndicator(list_, sortColumn_, sortAscending_);
    }
    CheckRadioButton(owner_, kOverviewId, kTreemapId,
                     mode == ViewMode::Overview ? kOverviewId :
                     mode == ViewMode::DrillDown ? kDrillDownId :
                     mode == ViewMode::LargestFolders ? kLargestFoldersId :
                     mode == ViewMode::LargestFiles ? kLargestFilesId :
                     mode == ViewMode::ByCategory ? kByCategoryId : kTreemapId);
    for (HWND control : {overviewButton_, drillDown_, largestFolders_, largestFiles_, byCategory_, treemap_}) {
        if (control != nullptr) InvalidateRect(control, nullptr, FALSE);
    }
    RefreshData();
    ApplyModeVisibility();
    Reposition(lastSurfaceRect_);
}

bool StorageAnalysis::HandleNotify(LPARAM lParam) {
    if (!visible_) return false;
    auto* header = reinterpret_cast<NMHDR*>(lParam);
    if (header == nullptr) return false;

    // storage-analysis 2.5: selecting a volume from the overview opens the
    // drill-down scoped to that volume's root; an unavailable (stale)
    // volume keeps live drill-down disabled.
    if (header->hwndFrom == overview_) {
        if (header->code == NM_DBLCLK || header->code == LVN_ITEMACTIVATE) {
            const int selected = ListView_GetNextItem(overview_, -1, LVNI_SELECTED);
            if (selected >= 0 && static_cast<size_t>(selected) < volumes_.size()) {
                const VolumeItem& vol = volumes_[static_cast<size_t>(selected)];
                if (vol.unavailable) {
                    SetWindowTextW(status_, StorageUnavailableVolumeStatusText());
                    return true;
                }
                currentPath_ = vol.rootPath;
                UpdateNavigationButtonState();
                SetViewMode(ViewMode::DrillDown);
                if (navigate_) navigate_(currentPath_, true);
            }
            return true;
        }
        return false;
    }
    if (header->hwndFrom != list_) return false;

    if (header->code == LVN_GETDISPINFOW) {
        auto* info = reinterpret_cast<NMLVDISPINFOW*>(lParam);
        if (info->item.iItem < 0 || static_cast<size_t>(info->item.iItem) >= items_.size() ||
            (info->item.mask & LVIF_TEXT) == 0) return true;
        const DrillItem& item = items_[static_cast<size_t>(info->item.iItem)];
        std::wstring text;
        switch (info->item.iSubItem) {
            case 0:
                text = item.name;
                if (item.isDirectory && !text.empty() && text.back() != L'\\') text += L'\\';
                break;
            case 1:
                text = item.isDirectory ? L"Folder" : L"File";
                break;
            case 2:
                text = item.isDirectory ? L"—" : ffui::FormatSize(item.sizeBytes);
                break;
            case 3:
                if (item.isDirectory) {
                    if (item.calculating) text = L"Calculating…";
                    else if (item.aggregateFailed) text = L"Partial";
                    else text = ffui::FormatSize(item.totalSizeBytes);
                } else {
                    text = ffui::FormatSize(item.totalSizeBytes);
                }
                break;
            case 4: {
                const auto [parentTotal, parentKnown] = CurrentParentTotals();
                const auto percentage = ComputeStoragePercentage(
                    item.totalSizeBytes, parentTotal, parentKnown &&
                    (!item.isDirectory || (!item.calculating && !item.aggregateFailed)));
                text = percentage.display;
                break;
            }
            case 5: {
                // semantic-treemap-uia 22.x: % of Volume -- the same
                // ComputeStoragePercentage helper and the same OS used-bytes
                // denominator the treemap metadata uses (one percentage
                // source, never a second implementation).
                const auto percentage = ComputeStoragePercentage(
                    item.totalSizeBytes, volumeUsedBytes_, volumeUsedKnown_ &&
                    (!item.isDirectory || (!item.calculating && !item.aggregateFailed)));
                text = percentage.display;
                break;
            }
            case 6:
                if (item.lastModifiedTime == 0) {
                    text = L"—";
                } else {
                    ULARGE_INTEGER value{};
                    value.QuadPart = item.lastModifiedTime;
                    FILETIME ft{value.LowPart, value.HighPart};
                    SYSTEMTIME time{};
                    if (FileTimeToSystemTime(&ft, &time)) {
                        wchar_t buffer[32]{};
                        swprintf_s(buffer, L"%04u-%02u-%02u %02u:%02u",
                                   time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute);
                        text = buffer;
                    } else {
                        text = L"—";
                    }
                }
                break;
            case 7: {
                // allocated-size-storage-accuracy 7.1: Size on Disk column.
                // Unknown is never rendered as 0 B; a partial directory
                // aggregate is labeled with its known/total qualifier and a
                // lower-bound marker -- never presented as an exact total.
                if (item.isDirectory) {
                    if (item.calculating) {
                        text = L"Calculating…";
                    } else if (item.allocatedTotalCount == 0) {
                        text = ffui::FormatSize(0); // empty subtree: exactly zero on disk
                    } else if (item.allocatedKnownCount == item.allocatedTotalCount) {
                        text = ffui::FormatSize(item.totalAllocatedSizeBytes);
                    } else if (item.allocatedKnownCount == 0) {
                        text = L"—"; // descendants exist but none has a known size-on-disk
                    } else {
                        text = L"≥ " + ffui::FormatSize(item.totalAllocatedSizeBytes) + L" (" +
                               std::to_wstring(item.allocatedKnownCount) + L"/" +
                               std::to_wstring(item.allocatedTotalCount) + L")";
                    }
                } else {
                    text = item.allocatedSizeKnown ? ffui::FormatSize(item.allocatedSizeBytes) : L"—";
                }
                break;
            }
            case 8:
                text = RecursiveRanking() ? StorageRankingLocationText(currentPath_, item.folder)
                                          : ffui::NormalizeDisplayPath(item.folder);
                break;
        }
        wcsncpy_s(info->item.pszText, static_cast<size_t>(info->item.cchTextMax), text.c_str(), _TRUNCATE);
        return true;
    }

    if (header->code == NM_DBLCLK || header->code == LVN_ITEMACTIVATE) {
        const int selected = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
        if (selected >= 0) ActivateListItem(static_cast<size_t>(selected));
        return true;
    }

    if (header->code == LVN_ITEMCHANGED && listProvider_) {
        const auto* change = reinterpret_cast<NMLISTVIEW*>(lParam);
        if ((change->uChanged & LVIF_STATE) != 0) {
            listEvents_.Notify({UiaEventKind::SelectionInvalidated});
            if ((change->uNewState & LVIS_FOCUSED) != 0) listEvents_.Notify({UiaEventKind::FocusChanged});
        }
    }

    if (header->code == LVN_COLUMNCLICK) {
        auto* info = reinterpret_cast<NMLISTVIEW*>(lParam);
        if (info->iSubItem == sortColumn_) {
            SortItems(sortColumn_, !sortAscending_);
        } else {
            SortItems(info->iSubItem, true);
        }
        return true;
    }
    return false;
}

void StorageAnalysis::ActivateListItem(size_t index) {
    if (!visible_ || index >= items_.size()) return;
    const DrillItem item = items_[index];
    if (!item.categoryId.empty()) {
        activeCategoryId_ = item.categoryId;
        PopulateCategoryFilter();
        treemapView_.SetCategoryFilter(&categoryEngine_, activeCategoryId_);
        SetViewMode(ViewMode::LargestFiles);
    } else if (item.isDirectory) {
        currentPath_ = StorageRankingItemPath(item.folder, item.name);
        UpdateNavigationButtonState();
        RefreshData();
        if (navigate_) navigate_(currentPath_, true);
    } else if (navigate_) {
        navigate_(StorageRankingItemPath(item.folder, item.name), false);
    }
}

bool StorageAnalysis::HandleCompletion(LPARAM lParam) {
    // allocated-size-storage-accuracy 6.3: the posted payload is the
    // additive V2 shape (logical + allocated + completeness pair).
    auto* payload = reinterpret_cast<ffprotocol::FolderAggregateResultV2Payload*>(lParam);
    if (!payload) return true;
    HandleAggregateResult(payload->requestId, payload->status, payload->itemCount, payload->totalSizeBytes,
                          payload->totalAllocatedSizeBytes, payload->allocatedBytesKnownCount,
                          payload->allocatedBytesTotalCount);
    delete payload;
    return true;
}

bool StorageAnalysis::HandleTreemapCompletion(LPARAM lParam) {
    const bool handled = treemapView_.HandleAggregateCompletion(lParam);
    // ui-v3 hardening V3-07: a finished aggregate that still leaves the treemap
    // empty is honest "no data", not an endless calculating state.
    if (handled && treemapView_.UiaTileCount() == 0) {
        treemapView_.SetEmptyStateText(L"No sized data for this scope.");
        InvalidateRect(owner_, nullptr, FALSE);
    }
    return handled;
}

void StorageAnalysis::SetEngineActive(bool active) {
    // A connection/source transition can keep the same degraded/privileged
    // flag. Still invalidate old ranking completions at that boundary.
    engineActive_ = active;
    if (visible_) RefreshData();
}

bool StorageAnalysis::SelectRadioFromUia(HWND radio) {
    if (!visible_ || owner_ == nullptr || radio == nullptr || !IsWindow(radio)) return false;
    const int id = GetDlgCtrlID(radio);
    if (id < kOverviewId || id > kTreemapId) return false;
    return PostMessageW(owner_, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), reinterpret_cast<LPARAM>(radio)) != FALSE;
}

Microsoft::WRL::ComPtr<IRawElementProviderSimple> StorageAnalysis::UiaRadioFixupProvider(HWND radio) {
    if (radio == nullptr || !IsWindow(radio)) return nullptr;
    if (!uiaState_) uiaState_ = std::make_shared<UiaOwnerState>();
    return Microsoft::WRL::ComPtr<IRawElementProviderSimple>(
        new StorageRadioProvider(uiaState_, this, radio));
}

void StorageAnalysis::SetDarkTheme(bool dark) {
    darkTheme_ = dark;
    treemapView_.SetDarkTheme(dark);
    // ui-v3 hardening: theme the two report lists (they previously kept
    // system-white backgrounds in dark mode) and repaint their headers.
    const ffui::UiTheme theme = ffui::GetUiTheme(dark);
    ApplyNativeExplorerTheme(list_, dark);
    ApplyNativeExplorerTheme(overview_, dark);
    ApplyNativeExplorerTheme(categoryFilter_, dark, NativeExplorerTheme::Combo);
    AttachThemedListHeader(list_);
    AttachThemedListHeader(overview_);
    for (HWND list : {list_, overview_}) {
        if (list == nullptr) continue;
        ListView_SetBkColor(list, ffui::ToColorRef(theme.background));
        ListView_SetTextColor(list, ffui::ToColorRef(theme.text));
        ListView_SetTextBkColor(list, ffui::ToColorRef(theme.background));
        InvalidateRect(list, nullptr, FALSE);
    }
    for (HWND control : {overviewButton_, drillDown_, largestFolders_, largestFiles_, byCategory_, treemap_}) {
        if (control != nullptr) InvalidateRect(control, nullptr, FALSE);
    }
    for (HWND control : {back_, up_}) {
        if (control != nullptr) InvalidateRect(control, nullptr, FALSE);
    }
    if (status_ != nullptr) InvalidateRect(status_, nullptr, FALSE);
}

std::vector<SelectionItem> StorageAnalysis::ActiveSelectionItems() const {
    if (!visible_ || !StorageRowsAreFiles(viewMode_) || displayedPath_ != currentPath_ ||
        currentPath_.empty() || list_ == nullptr) return {};
    const int selected = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
    if (selected < 0 || static_cast<size_t>(selected) >= items_.size()) return {};
    const DrillItem& item = items_[static_cast<size_t>(selected)];
    if (!item.categoryId.empty()) return {};
    const auto path = StorageRankingItemPath(item.folder, item.name);
    return path.empty() ? std::vector<SelectionItem>{} : std::vector<SelectionItem>{{path, item.isDirectory}};
}

bool StorageAnalysis::HandleContextMenu(WPARAM wParam, LPARAM lParam) {
    if (!visible_ || !invokeCommand_ || reinterpret_cast<HWND>(wParam) != list_) return false;
    const auto selection = ActiveSelectionItems();
    if (selection.empty()) return false;
    const int selected = ListView_GetNextItem(list_, -1, LVNI_SELECTED);

    const auto paths = SelectionPaths(selection);

    HMENU menu = CreatePopupMenu();
    if (!menu) return false;

    AppendMenuW(menu, MF_STRING, 1, L"Open");
    if (!selection.front().isDirectory) AppendMenuW(menu, MF_STRING, 5, L"Open containing folder");
    AppendMenuW(menu, MF_STRING, 2, L"Copy Path");
    AppendMenuW(menu, MF_STRING, 4, L"Cut");
    if (!selection.front().isDirectory) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, 3, L"Delete");
    }

    // Keyboard invocation (Shift+F10 / menu key) passes lParam == -1: anchor
    // the menu at the selected item's on-screen rect instead of (-1,-1).
    POINT menuPos{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    if (lParam == static_cast<LPARAM>(-1)) {
        RECT itemRect{};
        if (ListView_GetItemRect(list_, selected, &itemRect, LVIR_BOUNDS)) {
            menuPos = POINT{itemRect.left, itemRect.bottom};
            ClientToScreen(list_, &menuPos);
        } else {
            GetCursorPos(&menuPos);
        }
    }

    const int cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                      menuPos.x, menuPos.y, owner_, nullptr);
    DestroyMenu(menu);

    switch (cmd) {
        case 1:
            invokeCommand_(L"item.open", paths);
            return true;
        case 2:
            invokeCommand_(L"item.copy-path", paths);
            return true;
        case 3:
            invokeCommand_(L"file.delete", paths);
            return true;
        case 4:
            invokeCommand_(L"file.cut", paths);
            return true;
        case 5:
            invokeCommand_(L"item.open-containing-folder", paths);
            return true;
    }
    return false;
}

bool StorageAnalysis::HandleMouseMove(WPARAM wParam, LPARAM lParam) {
    (void)wParam;
    return treemapView_.HandleMouseMove(wParam, lParam);
}

bool StorageAnalysis::HandleLButtonDown(WPARAM wParam, LPARAM lParam) {
    (void)wParam;
    return treemapView_.HandleLButtonDown(wParam, lParam);
}

} // namespace ffui
