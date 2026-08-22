#include "SearchPanel.h"


#include "ffsearch/Path.h"
#include "IconCache.h"
#include "UITheme.h"
#include "UiComponents.h" // ui-v3 search: shared chip/pill/focus GDI grammar
#include "Util.h"
#include "ffui/SearchResultMetadata.h" // indexed-search-allocated-size D7: shared Size-on-Disk wording
#include "ffui/SearchTabOrder.h" // ui-v3 search a11y hardening: headless Tab-order seam
#include "SearchUiaProvider.h" // semantic-uia-core-surfaces 4.2
#include "UiaProviderBase.h" // ui-v3 search a11y hardening: shared COM provider foundation

#include <array>
#include <algorithm>
#include <commctrl.h>
#include <commoncontrols.h>
#include <cmath>
#include <cwctype>
#include <d2d1.h>
#include <dwrite.h>
#include <fstream>
#include <shellapi.h>
#include <shlobj.h>
#include <unordered_map>
#include <windowsx.h>
#include <wrl/client.h>
#include <wrl/implements.h> // Microsoft::WRL::Make

namespace ffui {
namespace {
constexpr int kQueryId = 7101;
constexpr int kScopeId = 7102;
constexpr int kSortId = 7103;
constexpr int kListId = 7104;
constexpr int kClearHistoryId = 7105;
constexpr int kSortDirectionId = 7107;
// ui-v3 search a11y: the Filters disclosure is a real button so it stays in
// the UIA tree (and keyboard order) even while the row it reveals is hidden.
constexpr int kFiltersToggleId = 7110;
constexpr UINT_PTR kDebounceTimer = 7106;
constexpr UINT kDebounceMilliseconds = 125;
// Coalesces search-history disk flushes: recording only marks the store dirty
// and arms this timer, so a burst of recorded queries costs one write.
constexpr UINT_PTR kHistorySaveTimer = 7108;
constexpr UINT kHistorySaveCoalesceMilliseconds = 1500;
// Minimum interval between snapshot-generation-triggered re-dispatches while
// the panel is open; faster generations only mark a pending refresh.
constexpr UINT kSnapshotRefreshIntervalMs = 2000;
constexpr size_t kMaxUiSearchResults = 10000;

// ui-v3 search: indeterminate progress cue while a query runs. The timer is
// armed only for the duration of the search (UpdateStatus) and killed on
// completion/hide — no idle animation ever runs.
constexpr UINT_PTR kProgressTimer = 7109;
constexpr UINT kProgressTimerMilliseconds = 33;
constexpr float kProgressSweepSeconds = 1.4f;
// ui-v3 search: query-first surface geometry (DIP). Collapsed rows reclaim
// the old permanent-filter band for results; expanding adds one combo row.
constexpr float kSurfaceInsetDip = 12.0f;
constexpr float kQueryRowHeightDip = 26.0f;
constexpr float kFilterBarHeightDip = 24.0f;
constexpr float kFilterRowHeightDip = 26.0f;   // expanded scope/sort/direction row
constexpr float kRowGapDip = 4.0f;             // UiMetrics::kSpaceXs
constexpr float kListBottomMarginDip = 12.0f;
constexpr float kCompactBreakWidthDip = 698.0f; // same break as the previous layout
constexpr float kScopeWidthDip = 210.0f;
constexpr float kSortWidthDip = 126.0f;
constexpr float kDirectionWidthDip = 146.0f;
constexpr float kClearHistoryWidthDip = 126.0f;
// ui-v3 search a11y: shared geometry for the real Filters disclosure button.
// Reposition places the child control with these values (owner coordinates)
// and PaintFilterBar reserves the same span before the first chip (bar-client
// coordinates), so the painted row and the placed control never drift.
constexpr float kFiltersToggleInsetDip = UiMetrics::kSpaceXs; // inset from the bar's left edge
constexpr float kFiltersToggleWidthDip = 64.0f;               // chevron + label at a comfortable target width
constexpr float kFiltersToggleGapDip = UiMetrics::kSpaceS;    // gap between the toggle and the first chip
constexpr float kComboDropdownHeightPx = 300.0f; // closed height is font-driven
constexpr float kSpaceClearArrowMarginDip = 20.0f; // keeps the affordance clear of the combo arrow
// ui-v3 search: two-line result rows (name primary, location/metadata caption).
constexpr float kResultRowHeightDip = 38.0f;
// ui-v3 search: filter-bar hit regions.
constexpr int kChipNone = -1;
// ui-v3 search: the disclosure pill became a real child button; only the
// scope/sort chips remain filter-bar hit-test regions.
constexpr int kChipScope = 1;
constexpr int kChipSort = 2;

// Scale a DIP metric to physical pixels for Win32 control layout. Exact
// int/float overloads so theme-token floats never narrow implicitly at /W4.
int Scaled(int dipValue) {
    return static_cast<int>(ffui::UiScale(static_cast<float>(dipValue)) + 0.5f);
}

int Scaled(float dipValue) {
    return static_cast<int>(ffui::UiScale(dipValue) + 0.5f);
}

// Alpha-composites a translucent theme token over an opaque base color so GDI
// (which has no alpha channel) renders the result Direct2D would.
COLORREF CompositeOver(D2D1_COLOR_F overlay, COLORREF base) {
    const float alpha = overlay.a;
    const auto blend = [alpha](float foreground, BYTE backgroundByte) -> BYTE {
        const float background = static_cast<float>(backgroundByte) / 255.0f;
        const float composited = foreground * alpha + background * (1.0f - alpha);
        return static_cast<BYTE>(static_cast<UINT>(composited * 255.0f + 0.5f));
    };
    return RGB(blend(overlay.r, GetRValue(base)), blend(overlay.g, GetGValue(base)),
               blend(overlay.b, GetBValue(base)));
}

// Quiet disclosure chevron drawn with lines so it never depends on glyph-font
// availability (same grammar as NavigationSidebar).
void DrawChevron(HDC dc, POINT center, int halfWidth, bool expanded, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    if (pen == nullptr) return;
    const int halfHeight = (std::max)(1, halfWidth / 2);
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
    if (expanded) {
        MoveToEx(dc, center.x - halfWidth, center.y - halfHeight, nullptr);
        LineTo(dc, center.x, center.y + halfHeight);
        LineTo(dc, center.x + halfWidth, center.y - halfHeight);
    } else {
        MoveToEx(dc, center.x - halfHeight, center.y - halfWidth, nullptr);
        LineTo(dc, center.x + halfHeight, center.y);
        LineTo(dc, center.x - halfHeight, center.y + halfWidth);
    }
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

// ui-v3 search: close/clear affordance drawn with two strokes so it never
// depends on a glyph font being installed.
void DrawCloseGlyph(HDC dc, POINT center, int halfExtent, COLORREF color, int penWidth) {
    HPEN pen = CreatePen(PS_SOLID, penWidth, color);
    if (pen == nullptr) return;
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
    MoveToEx(dc, center.x - halfExtent, center.y - halfExtent, nullptr);
    LineTo(dc, center.x + halfExtent, center.y + halfExtent);
    MoveToEx(dc, center.x + halfExtent, center.y - halfExtent, nullptr);
    LineTo(dc, center.x - halfExtent, center.y + halfExtent);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

// Inset keyboard-focus outline, independent of hover/selection (the GDI twin
// of ffui::DrawUiFocusIndicator, matching NavigationSidebar's ring grammar).
void DrawGdiFocusRing(HDC dc, const RECT& rect, int radius, COLORREF color) {
    const int inset = Scaled(UiMetrics::kSpaceXxs) + 1;
    const RECT ring{rect.left + inset, rect.top + inset, rect.right - inset, rect.bottom - inset};
    const int width = ring.right - ring.left;
    const int height = ring.bottom - ring.top;
    if (width <= 0 || height <= 0) return;
    radius = std::clamp(radius, 0, std::min(width, height) / 2);
    HPEN pen = CreatePen(PS_SOLID, (std::max)(1, Scaled(UiMetrics::kSpaceXxs)), color);
    if (pen == nullptr) return;
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
    RoundRect(dc, ring.left, ring.top, ring.right + 1, ring.bottom + 1, radius * 2, radius * 2);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

// ui-v3 search: GDI mirror of ffui::ResolveUiRowPalette so result rows share
// the browse-row state grammar (rest/hover/selected-focused/selected-unfocused
// identical in spirit). High Contrast falls back to system colors; translucent
// overlays composite over the list background.
struct SearchRowPaint {
    COLORREF fill = 0;
    bool hasFill = false;
    COLORREF text = 0;
    COLORREF secondary = 0;   // location/path
    COLORREF tertiary = 0;    // metadata block
};

SearchRowPaint ResolveSearchRowPaint(const UiTheme& theme, UiVisualState state, bool highContrast,
                                     COLORREF background) {
    SearchRowPaint paint{};
    if (highContrast) {
        // Accessibility fallback: system highlight colors, never token overlays.
        switch (state) {
            case UiVisualState::SelectedFocused:
            case UiVisualState::SelectedUnfocused:
                paint.fill = GetSysColor(COLOR_HIGHLIGHT);
                paint.hasFill = true;
                paint.text = GetSysColor(COLOR_HIGHLIGHTTEXT);
                break;
            case UiVisualState::Hover:
            case UiVisualState::Pressed:
                paint.fill = GetSysColor(COLOR_MENUHILIGHT);
                paint.hasFill = true;
                paint.text = GetSysColor(COLOR_HIGHLIGHTTEXT);
                break;
            default:
                paint.text = GetSysColor(COLOR_WINDOWTEXT);
                break;
        }
        paint.secondary = paint.text;
        paint.tertiary = paint.text;
        return paint;
    }
    paint.text = ToColorRef(theme.text);
    paint.secondary = ToColorRef(theme.textSecondary);
    paint.tertiary = ToColorRef(theme.textTertiary);
    switch (state) {
        case UiVisualState::Hover:
            paint.fill = CompositeOver(theme.hoverOverlay, background);
            paint.hasFill = true;
            break;
        case UiVisualState::Pressed:
            paint.fill = CompositeOver(theme.pressOverlay, background);
            paint.hasFill = true;
            break;
        case UiVisualState::SelectedUnfocused:
            paint.fill = CompositeOver(theme.selectionSoft, background);
            paint.hasFill = true;
            break;
        case UiVisualState::SelectedFocused:
            paint.fill = ToColorRef(theme.accent);
            paint.hasFill = true;
            paint.text = ToColorRef(theme.textOnAccent);
            // Secondary text on the accent fill stays readable by leaning back
            // toward textOnAccent instead of dropping to muted grays.
            paint.secondary = ToColorRef(UiLerpColor(theme.textOnAccent, theme.accent, 0.22f));
            paint.tertiary = ToColorRef(UiLerpColor(theme.textOnAccent, theme.accent, 0.38f));
            break;
        case UiVisualState::Disabled:
            paint.text = ToColorRef(theme.textDisabled);
            paint.secondary = paint.text;
            paint.tertiary = paint.text;
            break;
        case UiVisualState::Busy:
        case UiVisualState::Focused:
        case UiVisualState::DropTarget:
        case UiVisualState::Rest:
            break;
    }
    return paint;
}

// Short scope/sort names shared by the chips and the empty-state panel so the
// summary can never drift from what the combos offer.
const wchar_t* ScopeShortName(int index) {
    switch (index) {
        case 0: return L"Current Folder";
        case 1: return L"Current Folder + Subfolders";
        case 2: return L"Current Drive";
        case 3: return L"All Locations";
        default: return L"All Locations";
    }
}

const wchar_t* SortFieldName(int index) {
    switch (index) {
        case 0: return L"Relevance";
        case 1: return L"Filename";
        case 2: return L"Path";
        case 3: return L"Size";
        case 4: return L"Modified";
        case 5: return L"Created";
        case 6: return L"Size on Disk";
        default: return L"Relevance";
    }
}

using Microsoft::WRL::ComPtr;

// Returns the extension of a file name including the leading dot (L".txt"),
// or an empty string when there is no extension. Only used to key the system
// icon cache; directories use ffui::FolderKey() instead.
std::wstring FileExtension(const std::wstring& name) {
    const size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos || dot == 0 || dot + 1 >= name.size()) return {};
    return name.substr(dot);
}

// Represents a seen match span within a candidate name (case-insensitive),
// already expanded to never split a surrogate pair.
struct MatchSpan {
    size_t start = std::wstring::npos;
    size_t count = 0;
};

// Surrogate-pair tests (the SDK does not expose IsHighSurrogate/IsLowSurrogate
// as macros in this build, so they are provided here).
bool IsHighSurrogateChar(wchar_t ch) { return ch >= 0xD800 && ch <= 0xDBFF; }
bool IsLowSurrogateChar(wchar_t ch) { return ch >= 0xDC00 && ch <= 0xDFFF; }

// Finds the first case-insensitive occurrence of term in name and expands the
// boundaries so a surrogate pair is never split: if start lands on a low
// surrogate, back it up to include its leading high surrogate; if end lands on
// a high surrogate, advance it to include the trailing low surrogate.
MatchSpan FindMatchSpan(const std::wstring& name, const std::wstring& term) {
    MatchSpan span;
    if (term.empty() || term.size() > name.size()) return span;
    for (size_t i = 0; i + term.size() <= name.size(); ++i) {
        bool matches = true;
        for (size_t j = 0; matches && j < term.size(); ++j) {
            if (towlower(name[i + j]) != towlower(term[j])) matches = false;
        }
        if (!matches) continue;
        size_t start = i;
        size_t end = start + term.size();
        if (start > 0 && IsLowSurrogateChar(name[start])) --start;
        if (end < name.size() && IsHighSurrogateChar(name[end])) ++end;
        span.start = start;
        span.count = end - start;
        return span;
    }
    return span;
}

// Draws a single untruncated line of text at (x, row.top..row.bottom) with the
// given color, returning the consumed width so callers can append subsequent
// match-span segments immediately after the previous one.
int DrawRowText(HDC dc, HFONT font, int x, const RECT& row, const std::wstring& text, COLORREF color) {
    if (text.empty()) return 0;
    SetTextColor(dc, color);
    HGDIOBJ previous = SelectObject(dc, font);
    SIZE size{};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
    RECT textRect{x, row.top, row.right, row.bottom};
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &textRect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(dc, previous);
    return size.cx;
}

LRESULT CALLBACK SearchEditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                UINT_PTR subclassId, DWORD_PTR refData) {
    auto* self = reinterpret_cast<SearchPanel*>(refData);
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT) {
        if (self != nullptr) self->PaintSearchEdit(hwnd);
        return 0;
    }
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS) {
        const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
        if (self != nullptr) self->searchEditFocused_ = (message == WM_SETFOCUS);
        InvalidateRect(hwnd, nullptr, FALSE);
        return result;
    }
    if (message == WM_SETTEXT || message == WM_CLEAR || message == WM_CUT || message == WM_PASTE) {
        const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
        InvalidateRect(hwnd, nullptr, FALSE);
        return result;
    }
    // ui-v3 hardening: keyboard-driven text changes repaint the native edit
    // directly and left stale system-light frames over the custom paint when
    // the queued WM_PAINT raced the input batch. Force our paint after every
    // content-affecting key so the field never shows a native flash.
    if ((message == WM_KEYDOWN || message == WM_CHAR) && self != nullptr) {
        const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
        InvalidateRect(hwnd, nullptr, FALSE);
        return result;
    }
    // ui-v3 search a11y hardening: no dialog manager owns this window, so Tab
    // would die in the edit control; walk the panel's explicit focus order
    // (query -> Filters -> ... -> list -> query) instead. Shift+Tab reverses.
    if (message == WM_KEYDOWN && wParam == VK_TAB && self != nullptr) {
        const bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        self->CycleSearchFocus(hwnd, !shiftHeld);
        return 0;
    }
    UNREFERENCED_PARAMETER(subclassId);
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

// ui-v3 search: the filter/disclosure bar replaces the old status STATIC. It
// custom-paints the Filters disclosure pill, active-filter chips, and the live
// status line; hover/click stay inside the subclass so no owner WM_COMMAND
// routing changes.
LRESULT CALLBACK FilterBarProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                               UINT_PTR subclassId, DWORD_PTR refData) {
    auto* self = reinterpret_cast<SearchPanel*>(refData);
    if (self == nullptr) return DefSubclassProc(hwnd, message, wParam, lParam);
    switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            self->PaintFilterBar(hwnd);
            return 0;
        case WM_MOUSEMOVE:
            self->HandleFilterBarMouseMove(lParam);
            {
                TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&track);
            }
            break;
        case WM_MOUSELEAVE:
            self->HandleFilterBarMouseLeave();
            break;
        case WM_LBUTTONUP:
            self->HandleFilterBarClick(lParam);
            break;
        default:
            break;
    }
    UNREFERENCED_PARAMETER(subclassId);
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

// ui-v3 search: in-field clear-query affordance. A small STATIC floating over
// the right edge of the query combo; visible only while the edit has text.
LRESULT CALLBACK ClearQueryProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                UINT_PTR subclassId, DWORD_PTR refData) {
    auto* self = reinterpret_cast<SearchPanel*>(refData);
    if (self == nullptr) return DefSubclassProc(hwnd, message, wParam, lParam);
    switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            self->PaintClearQueryButton(hwnd);
            return 0;
        case WM_MOUSEMOVE: {
            if (!self->ClearQueryHot()) {
                self->SetClearQueryHot(true);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
            return 0;
        }
        case WM_MOUSELEAVE:
            self->SetClearQueryHot(false);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP:
            self->ClearQueryText();
            return 0;
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
        default:
            break;
    }
    UNREFERENCED_PARAMETER(subclassId);
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

// ui-v3 search: hover tracking for the owner-draw push buttons. Native buttons
// report pressed/focus via DRAWITEMSTRUCT but not hover, so the shared proc
// keeps a per-button hot flag keyed by HWND.
// ui-v3 search a11y hardening additions in this proc: WM_GETOBJECT answers
// with the button fixup provider (Button control type + Invoke pattern +
// honest keyboard focusability), Tab walks the panel focus order, and Enter
// activates like a native Space press (no dialog manager owns the window).
LRESULT CALLBACK ButtonHoverProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                 UINT_PTR subclassId, DWORD_PTR refData) {
    auto* self = reinterpret_cast<SearchPanel*>(refData);
    if (self != nullptr && message == WM_MOUSEMOVE) {
        self->SetButtonHot(hwnd, true);
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&track);
    }
    if (self != nullptr && message == WM_MOUSELEAVE) {
        self->SetButtonHot(hwnd, false);
    }
    if (message == WM_GETOBJECT && static_cast<LONG>(lParam) == UiaRootObjectId && self != nullptr) {
        Microsoft::WRL::ComPtr<IRawElementProviderSimple> fixup = self->UiaButtonFixupProvider(hwnd);
        if (fixup != nullptr) {
            return UiaReturnRawElementProvider(hwnd, wParam, lParam, fixup.Get());
        }
        // Provider unavailable: fall through so the native MSAA/UIA fallback
        // answers instead of swallowing the query.
    }
    if (self != nullptr && message == WM_KEYDOWN && wParam == VK_TAB) {
        const bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        self->CycleSearchFocus(hwnd, !shiftHeld);
        return 0;
    }
    if (self != nullptr && message == WM_KEYDOWN && wParam == VK_RETURN) {
        SendMessageW(hwnd, BM_CLICK, 0, 0); // same path a native Space press takes
        return 0;
    }
    UNREFERENCED_PARAMETER(subclassId);
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

std::wstring FormatFileTime(uint64_t ticks) {
    if (ticks == 0) return L"—";
    ULARGE_INTEGER value{};
    value.QuadPart = ticks;
    FILETIME fileTime{value.LowPart, value.HighPart};
    SYSTEMTIME time{};
    if (!FileTimeToSystemTime(&fileTime, &time)) return L"—";
    wchar_t text[32]{};
    swprintf_s(text, L"%04u-%02u-%02u %02u:%02u", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute);
    return text;
}

constexpr HRESULT kElementNotAvailable = UIA_E_ELEMENTNOTAVAILABLE;

// ui-v3 search a11y hardening: property/pattern fixup for the panel's
// owner-draw push buttons (Filters disclosure, sort direction, clear history).
// Under the custom fragment root these HWNDs fall through to UIA's default
// MSAA proxy, which reports them as ControlType=Pane without an Invoke
// pattern and with IsKeyboardFocusable=false -- contradicting what they are
// (real push buttons) and locking keyboard/assistive-tech users out of the
// Filters disclosure. This provider keeps the default HWND host provider as
// its fallback (get_HostRawElementProvider -> UiaHostProviderFromHwnd), so
// tree shape, geometry, runtime id, navigation, and every property/pattern we
// do not override keep coming from the host bridge while ControlType, Invoke,
// and keyboard-focusability come from here. Lifetime follows the panel's
// shared UiaOwnerState token: once the panel dies, calls fail with
// UIA_E_ELEMENTNOTAVAILABLE instead of dereferencing a stale HWND.
class SearchButtonInvokeProvider final : public UiaProviderBase, public IInvokeProvider {
public:
    SearchButtonInvokeProvider(UiaOwnerStatePtr state, HWND button)
        : UiaProviderBase(std::move(state)), button_(button) {}

    FFUIA_QI_BEGIN()
        FFUIA_QI_ENTRY(IID_IInvokeProvider, IInvokeProvider)
    FFUIA_QI_END()
    FFUIA_REFCOUNT()

    // IRawElementProviderSimple overrides.
    STDMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** retVal) noexcept override {
        if (retVal == nullptr) return E_POINTER;
        *retVal = nullptr;
        if (!Alive()) return kElementNotAvailable;
        return UiaHostProviderFromHwnd(button_, retVal);
    }
    STDMETHODIMP GetPropertyValue(PROPERTYID propertyId, VARIANT* retVal) noexcept override {
        if (retVal == nullptr) return E_POINTER;
        VariantInit(retVal);
        if (!Alive() || button_ == nullptr || !IsWindow(button_)) return kElementNotAvailable;
        switch (propertyId) {
            case UIA_ControlTypePropertyId:
                SetVariantInt(retVal, UIA_ButtonControlTypeId);
                return S_OK;
            case UIA_NamePropertyId: {
                wchar_t text[128]{};
                GetWindowTextW(button_, text, static_cast<int>(std::size(text)));
                SetVariantString(retVal, text);
                return S_OK;
            }
            case UIA_IsKeyboardFocusablePropertyId:
                // Honest: Tab traversal reaches this button through the
                // panel's explicit focus cycle (ffui::SearchTabOrder).
                SetVariantBool(retVal, true);
                return S_OK;
            case UIA_IsEnabledPropertyId:
                SetVariantBool(retVal, IsWindowEnabled(button_) != FALSE);
                return S_OK;
            case UIA_HasKeyboardFocusPropertyId:
                SetVariantBool(retVal, GetFocus() == button_);
                return S_OK;
            case UIA_IsOffscreenPropertyId:
                SetVariantBool(retVal, IsWindowVisible(button_) == FALSE);
                return S_OK;
            default:
                break;
        }
        SetVariantEmpty(retVal); // fall back to the host provider
        return S_OK;
    }
    STDMETHODIMP GetPatternProvider(PATTERNID patternId, IUnknown** retVal) noexcept override {
        if (retVal == nullptr) return E_POINTER;
        *retVal = nullptr;
        if (!Alive()) return kElementNotAvailable;
        if (patternId == UIA_InvokePatternId) {
            *retVal = static_cast<IInvokeProvider*>(this);
            static_cast<IRawElementProviderSimple*>(this)->AddRef();
            return S_OK;
        }
        return UiaProviderBase::GetPatternProvider(patternId, retVal); // host supplies the rest
    }

    // IInvokeProvider: route through BM_CLICK exactly like a native press so
    // BN_CLICKED -> HandleOwnerCommand runs on the UI thread. Post, never
    // send: UIA invokes arrive on a UIAutomationCore worker thread and must
    // never block it against the UI thread (same posture as treemap Invoke).
    STDMETHODIMP Invoke() noexcept override {
        if (!Alive() || button_ == nullptr || !IsWindow(button_)) return kElementNotAvailable;
        PostMessageW(button_, BM_CLICK, 0, 0);
        return S_OK;
    }

private:
    HWND button_;
};

// semantic-uia-core-surfaces 4.2: the result list's subclassed window
// procedure. WM_GETOBJECT (UiaRootObjectId) returns the search fragment
// root; every other message -- including OBJID_CLIENT MSAA queries -- falls
// through to the native ListView, so input, painting, and virtualization
// are untouched.
// ui-v3 search additions (presentation only): a deliberate no-results /
// searching panel replaces the blank list while the result set is empty, and
// row hover is tracked for the shared row-state grammar.
// ui-v3 search a11y hardening: Tab traversal for the expanded-row combos.
// The native ComboBox proc consumes VK_TAB (stranding focus), so the shared
// subclass routes it through the panel's explicit focus-order seam; every
// other key (arrows, F4/Alt+Down, typing) stays native.
LRESULT CALLBACK ComboKeyProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                              UINT_PTR subclassId, DWORD_PTR refData) {
    auto* self = reinterpret_cast<SearchPanel*>(refData);
    if (self != nullptr && message == WM_KEYDOWN && wParam == VK_TAB) {
        const bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        self->CycleSearchFocus(hwnd, !shiftHeld);
        return 0;
    }
    UNREFERENCED_PARAMETER(subclassId);
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK SearchListProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                UINT_PTR subclassId, DWORD_PTR refData) {
    auto* self = reinterpret_cast<SearchPanel*>(refData);
    if (message == WM_GETOBJECT && static_cast<LONG>(lParam) == UiaRootObjectId && self != nullptr) {
        Microsoft::WRL::ComPtr<IRawElementProviderFragmentRoot> root = self->UiaRootProvider(hwnd);
        if (root != nullptr) {
            Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;
            if (SUCCEEDED(root.As(&simple)) && simple != nullptr) {
                return UiaReturnRawElementProvider(hwnd, wParam, lParam, simple.Get());
            }
        }
        // Provider unavailable: fall through to DefSubclassProc so the native
        // MSAA/UIA fallback answers instead of swallowing the query.
    }
    if (self != nullptr) {
        // ui-v3 search: deliberate state surface while there is nothing to
        // list; the provider reports the same (zero) count, so automation and
        // pixels stay consistent.
        if (message == WM_PAINT && self->ShouldPaintListEmptyState()) {
            self->PaintListEmptyState(hwnd);
            return 0;
        }
        if (message == WM_MOUSEMOVE) {
            self->HandleListMouseMove(lParam);
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
        }
        if (message == WM_MOUSELEAVE) {
            self->HandleListMouseLeave();
        }
        // ui-v3 search a11y hardening: Tab leaves the list back to the query
        // field (Shift+Tab reaches the last filter control first). The list
        // keeps sole ownership of arrows/PageUp/PageDown/Home/End.
        if (message == WM_KEYDOWN && wParam == VK_TAB) {
            const bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            self->CycleSearchFocus(hwnd, !shiftHeld);
            return 0;
        }
        if (message == WM_NCDESTROY) {
            // The row-height image list is owned by the panel; release it while
            // the list can no longer reference it.
            self->ReleaseListImageList();
        }
    }
    UNREFERENCED_PARAMETER(subclassId);
    return DefSubclassProc(hwnd, message, wParam, lParam);
}
}  // namespace

SearchPanel::~SearchPanel() {
    KillTimer(owner_, kDebounceTimer);
    KillTimer(owner_, kHistorySaveTimer);
    KillTimer(owner_, kProgressTimer);
    // Flush a coalesced history save that never fired; teardown is the one
    // place a synchronous write is acceptable (the UI thread is going away).
    if (historyDirty_) {
        historyDirty_ = false;
        SaveHistory();
    }
    // semantic-uia-core-surfaces 4.x: drop the list subclass and flip the
    // owner-lifetime token before releasing the provider so client-held
    // elements become safe zombies.
    if (list_) RemoveWindowSubclass(list_, SearchListProc, 3);
    ReleaseUiaProvider();
    if (queryEdit_) RemoveWindowSubclass(queryEdit_, SearchEditProc, 1);
    // ui-v3 search: drop the presentation subclasses before the windows die.
    if (filterBar_) RemoveWindowSubclass(filterBar_, FilterBarProc, 4);
    if (clearQuery_) RemoveWindowSubclass(clearQuery_, ClearQueryProc, 5);
    if (sortDirection_) RemoveWindowSubclass(sortDirection_, ButtonHoverProc, 6);
    if (clearHistory_) RemoveWindowSubclass(clearHistory_, ButtonHoverProc, 7);
    if (filtersToggle_) RemoveWindowSubclass(filtersToggle_, ButtonHoverProc, 8);
    if (scope_) RemoveWindowSubclass(scope_, ComboKeyProc, 9);
    if (sort_) RemoveWindowSubclass(sort_, ComboKeyProc, 10);
    ReleaseListImageList();
    // ui-v3 search: DPI-aware row/bar fonts are panel-owned GDI objects.
    if (resultNameFont_) DeleteObject(resultNameFont_);
    if (resultMatchFont_) DeleteObject(resultMatchFont_);
    if (resultCaptionFont_) DeleteObject(resultCaptionFont_);
    stopping_.store(true, std::memory_order_release);
    {
        std::lock_guard callbackLock(indexedCallbackState_->mutex);
        indexedCallbackState_->alive = false;
    }
    const uint64_t indexedRequestId = indexedRequestId_.exchange(0);
    if (engine_ != nullptr && indexedRequestId != 0) {
        engine_->CancelIndexedSearch(indexedRequestId);
    }
    currentGeneration_.fetch_add(1);
    workCv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

bool SearchPanel::Initialize(HWND owner, EngineClient* engine,
                             std::function<void(const ffsearch::Candidate&, const ffsearch::PathReconstruction&)> navigate,
                             bool retainHistory) {
    owner_ = owner;
    engine_ = engine;
    navigate_ = std::move(navigate);
    retainHistory_ = retainHistory;
    historyStore_.SetRecordingEnabled(retainHistory);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&controls);
    query_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWN | WS_VSCROLL,
                             0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kQueryId)), nullptr, nullptr);
    scope_ = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
                             0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kScopeId)), nullptr, nullptr);
    sort_ = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
                            0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSortId)), nullptr, nullptr);
    list_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                            WS_CHILD | LVS_REPORT | LVS_OWNERDATA | LVS_OWNERDRAWFIXED | LVS_SINGLESEL |
                            // ui-v3 hardening V3-02: owner-drawn rows carry name +
                            // location themselves, so the column header strip is
                            // meaningless — and as a native child it rendered
                            // system-light in dark mode. NOCOLUMNHEADER keeps column
                            // widths driving scroll extent without ever creating a
                            // visible header (a plain SW_HIDE raced with the list's
                            // own first layout and re-showed it).
                            LVS_NOCOLUMNHEADER | WS_TABSTOP,
                            0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kListId)), nullptr, nullptr);
    // ui-v3 search: one custom-painted bar replaces the old status STATIC —
    // Filters disclosure, active-filter chips, and the live status line share
    // a single quiet row so permanent chrome stays minimal.
    filterBar_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 0, 0, owner, nullptr, nullptr, nullptr);
    // ui-v3 search: in-field clear affordance; created after query_ so it sits
    // above the combo and receives the clicks inside its small rect.
    clearQuery_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 0, 0, owner, nullptr, nullptr, nullptr);
    // BS_OWNERDRAW keeps the button inside the themed token set instead of
    // native light chrome breaking the dark panel.
    clearHistory_ = CreateWindowExW(0, L"BUTTON", L"Clear history", WS_CHILD | BS_OWNERDRAW,
                                    0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kClearHistoryId)), nullptr, nullptr);
    // Accessible Name contract: the plain direction word ("Ascending") —
    // DrawTextButton renders the direction glyph so the visible label keeps
    // the arrow affordance without leaking it into the UIA Name.
    sortDirection_ = CreateWindowExW(0, L"BUTTON", L"Ascending", WS_CHILD | BS_OWNERDRAW,
                                     0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSortDirectionId)), nullptr, nullptr);
    // ui-v3 search a11y: the disclosure pill as a real owner-draw button.
    filtersToggle_ = CreateWindowExW(0, L"BUTTON", L"Filters", WS_CHILD | BS_OWNERDRAW,
                                     0, 0, 0, 0, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFiltersToggleId)), nullptr, nullptr);
    if (!query_ || !scope_ || !sort_ || !list_ || !filterBar_ || !clearQuery_ || !clearHistory_ || !sortDirection_ || !filtersToggle_) return false;
    COMBOBOXINFO comboInfo{sizeof(comboInfo)};
    if (GetComboBoxInfo(query_, &comboInfo) && comboInfo.hwndItem) {
        queryEdit_ = comboInfo.hwndItem;
        SendMessageW(queryEdit_, EM_LIMITTEXT,
                     static_cast<WPARAM>(ffprotocol::kMaxIndexedSearchQueryLengthChars), 0);
        SetWindowSubclass(queryEdit_, SearchEditProc, 1, reinterpret_cast<DWORD_PTR>(this));
    }
    SetWindowSubclass(filterBar_, FilterBarProc, 4, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(clearQuery_, ClearQueryProc, 5, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(sortDirection_, ButtonHoverProc, 6, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(clearHistory_, ButtonHoverProc, 7, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(filtersToggle_, ButtonHoverProc, 8, reinterpret_cast<DWORD_PTR>(this));
    // ui-v3 search a11y hardening: the expanded-row combos must participate
    // in the panel's explicit Tab order; without this their native proc
    // swallows VK_TAB and strands focus.
    SetWindowSubclass(scope_, ComboKeyProc, 9, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(sort_, ComboKeyProc, 10, reinterpret_cast<DWORD_PTR>(this));
    // semantic-uia-core-surfaces 4.2: subclass the result list so WM_GETOBJECT
    // returns the search fragment root (native virtual lists have no UIA item
    // provider; see the change design D1/D15).
    SetWindowSubclass(list_, SearchListProc, 3, reinterpret_cast<DWORD_PTR>(this));
    RefreshScopeLabels();
    for (const wchar_t* text : {L"Relevance", L"Filename", L"Path", L"Size", L"Modified", L"Created", L"Size on Disk"})
        SendMessageW(sort_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    SendMessageW(sort_, CB_SETCURSEL, 0, 0);
    const wchar_t* columns[] = {L"Name", L"Type", L"Location", L"Size", L"Modified", L"Size on Disk"};
    const int widths[] = {220, 80, 360, 100, 145, 110};
    for (int index = 0; index < 6; ++index) {
        LVCOLUMNW column{LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM};
        column.pszText = const_cast<wchar_t*>(columns[index]);
        column.cx = widths[index];
        column.iSubItem = index;
        ListView_InsertColumn(list_, index, &column);
    }
    ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    // ui-v3 search: two-line result rows. A report-mode ListView derives its
    // row height from the small image list, so a one-entry list at the row
    // height sets the metrics; the panel owns and releases it.
    EnsureRowMetrics();
    SetDarkTheme(ffui::gUiDarkTheme);
    LoadHistory();
    UpdateFilterSummary();
    UpdateClearAffordance();
    worker_ = std::thread(&SearchPanel::WorkerMain, this);
    return true;
}

void SearchPanel::SetDefaultScope(const std::wstring& scopeName) {
    if (!scope_) return;
    int index = 3; // "Everywhere" maps to all locations present in the snapshot.
    if (scopeName == L"Current Folder") index = 0;
    else if (scopeName == L"Current Folder + Subfolders") index = 1;
    else if (scopeName == L"Current Drive") index = 2;
    else if (scopeName == L"All Indexed Locations" || scopeName == L"Everywhere") index = 3;
    SendMessageW(scope_, CB_SETCURSEL, index, 0);
    UpdateFilterSummary(); // ui-v3 search: chips must reflect the restored scope
}

void SearchPanel::ShowAndFocus(const std::wstring& currentPath, bool engineActive,
                               const ShellLayoutRect& surfaceRect) {
    currentPath_ = currentPath;
    engineActive_ = engineActive;
    visible_ = true;
    for (HWND control : {query_, filterBar_, list_}) ShowWindow(control, SW_SHOW);
    if (filtersToggle_ != nullptr) ShowWindow(filtersToggle_, SW_SHOW); // disclosure always reachable while open
    UpdateScopeAvailability(false);
    Reposition(surfaceRect);
    // Consume a coalesced snapshot refresh that was deferred while hidden.
    if (snapshotRefreshPending_ && query_ != nullptr && GetWindowTextLengthW(query_) > 0) {
        Dispatch(false);
    }
    if (query_ != nullptr) {
        SetFocus(GetWindow(query_, GW_CHILD));
    }
}

void SearchPanel::Hide() {
    // Commitment point for history: a query still present when the panel
    // closes counts as an executed search. Intermediate debounce ticks never
    // record, so prefixes like "docu"/"documen" stay out of Recent searches.
    if (visible_ && query_ != nullptr) {
        std::array<wchar_t, static_cast<size_t>(ffprotocol::kMaxIndexedSearchQueryLengthChars) + 1> text{};
        GetWindowTextW(query_, text.data(), static_cast<int>(text.size()));
        if (text[0] != L'\0') RecordHistory(text.data());
    }
    visible_ = false;
    KillTimer(owner_, kDebounceTimer);
    // ui-v3 search: no progress animation while hidden.
    KillTimer(owner_, kProgressTimer);
    // The coalesced history-save timer keeps running so a recorded query is
    // flushed even after the panel hides.
    for (HWND control : {query_, filterBar_, clearQuery_, list_, clearHistory_}) ShowWindow(control, SW_HIDE);
    for (HWND control : {scope_, sort_, sortDirection_}) ShowWindow(control, SW_HIDE);
    if (filtersToggle_ != nullptr) ShowWindow(filtersToggle_, SW_HIDE);
}

void SearchPanel::SetDarkTheme(bool dark) {
    if (query_) InvalidateRect(query_, nullptr, FALSE);
    if (list_) {
        const ffui::UiTheme theme = ffui::GetUiTheme(dark);
        ListView_SetBkColor(list_, ffui::ToColorRef(theme.background));
        ListView_SetTextColor(list_, ffui::ToColorRef(theme.text));
        ListView_SetTextBkColor(list_, ffui::ToColorRef(theme.background));
        InvalidateRect(list_, nullptr, FALSE);
    }
    if (scope_) InvalidateRect(scope_, nullptr, TRUE);
    if (sort_) InvalidateRect(sort_, nullptr, TRUE);
    if (sortDirection_) InvalidateRect(sortDirection_, nullptr, TRUE);
    if (clearHistory_) InvalidateRect(clearHistory_, nullptr, TRUE);
    if (filtersToggle_) InvalidateRect(filtersToggle_, nullptr, TRUE);
    // ui-v3 search: repaint the custom-painted bar and clear affordance.
    if (filterBar_) InvalidateRect(filterBar_, nullptr, TRUE);
    if (clearQuery_) InvalidateRect(clearQuery_, nullptr, TRUE);
    // Force the query-edit paint resources to rebuild on the next paint so a
    // theme change cannot leave stale colors cached in the DC render target.
    searchResourcesReady_ = false;
    lastSearchDpi_ = -1.0f;
}

void SearchPanel::Reposition(const ShellLayoutRect& surfaceRect) {
    if (!visible_) return;
    lastSurfaceRect_ = surfaceRect;
    EnsureRowMetrics(); // ui-v3 search: follow DPI changes without a rebuild

    // ui-v3 search: query-first geometry. Row 1 is the prominent full-width
    // query field; row 2 is the compact filter/status bar; the scope/sort/
    // direction combos only occupy a row while the disclosure is expanded.
    // Collapsed, results start exactly where the old permanent-chrome layout
    // placed them, so collapsing never costs result space.
    const float left = surfaceRect.left + kSurfaceInsetDip;
    const float right = (std::max)(left, surfaceRect.right - kSurfaceInsetDip);
    const float width = surfaceRect.Width();
    const bool wide = width >= kCompactBreakWidthDip;

    const float queryTop = surfaceRect.top;
    const float queryBottom = queryTop + kQueryRowHeightDip;
    const float barTop = queryBottom + kRowGapDip;
    const float barBottom = barTop + kFilterBarHeightDip;
    const float filterTop = barBottom + kRowGapDip;
    const float filterBottom = filterTop + kFilterRowHeightDip;
    const float compactSecondTop = filterBottom + kRowGapDip;
    const float compactSecondBottom = compactSecondTop + kFilterRowHeightDip;

    const float listTop = filtersExpanded_ ? (wide ? filterBottom : compactSecondBottom) + kRowGapDip
                                           : barBottom + kRowGapDip + UiMetrics::kSpaceXxs;
    const ShellLayoutRect queryRect{left, queryTop, right, queryBottom};
    const ShellLayoutRect barRect{left, barTop, right, barBottom};
    const ShellLayoutRect listRect{left, listTop, right,
                                   (std::max)(listTop, surfaceRect.bottom - kListBottomMarginDip)};

    const auto scaledDip = [](float dip) {
        return static_cast<int>(std::lround(ffui::UiScale(dip)));
    };
    // Moves a control into DIP-space rect; visibility is applied after the
    // move so a shown control never flashes at its stale position.
    const auto place = [scaledDip](HWND control, const ShellLayoutRect& rect, float heightDip,
                                   bool show) {
        const int pxLeft = scaledDip(rect.left);
        const int pxTop = scaledDip(rect.top);
        const int pxRight = scaledDip(rect.right);
        const int pxHeight = Scaled(heightDip);
        SetWindowPos(control, HWND_TOP, pxLeft, pxTop, (std::max)(0, pxRight - pxLeft), pxHeight,
                     show ? SWP_NOACTIVATE | SWP_SHOWWINDOW : SWP_NOACTIVATE);
        if (!show) ShowWindow(control, SW_HIDE);
    };

    // Combo parents keep their historical 300-DIP dropdown height; the closed
    // control height is font-driven, so the row geometry above governs layout.
    place(query_, queryRect, kComboDropdownHeightPx, true);
    place(filterBar_, barRect, kFilterBarHeightDip, true);
    // ui-v3 search a11y: the Filters disclosure is always visible while the
    // panel is open (it is the only way to reach the collapsed scope/sort
    // row), placed over the bar's left inset with the shared toggle geometry.
    place(filtersToggle_, {left + kFiltersToggleInsetDip, barTop,
                           left + kFiltersToggleInsetDip + kFiltersToggleWidthDip, barBottom},
          kFilterBarHeightDip, true);
    // The clear affordance floats inside the query field's right edge, clear of
    // the combo's dropdown arrow.
    if (clearQuery_ != nullptr) {
        const int buttonPx = Scaled(UiMetrics::kTargetMinSize - UiMetrics::kSpaceXxs);
        const int fieldRight = scaledDip(queryRect.right);
        const int fieldTop = scaledDip(queryRect.top);
        const int fieldBottom = scaledDip(queryRect.bottom);
        SetWindowPos(clearQuery_, HWND_TOP,
                     (std::max)(scaledDip(queryRect.left), fieldRight - buttonPx - Scaled(kSpaceClearArrowMarginDip)),
                     fieldTop + (std::max)(0, (fieldBottom - fieldTop) - buttonPx) / 2,
                     buttonPx, buttonPx, SWP_NOACTIVATE);
    }

    // Expanded filter row(s). Wide packs one row; the narrow fallback keeps a
    // two-row stack so no control gets a negative width.
    if (wide) {
        const float clearLeft = (std::max)(left, right - kClearHistoryWidthDip);
        const float directionLeft = (std::max)(left, clearLeft - kRowGapDip - kDirectionWidthDip);
        const float sortLeft = (std::max)(left, directionLeft - kRowGapDip - kSortWidthDip);
        const float scopeLeft = (std::max)(left, sortLeft - kRowGapDip - kScopeWidthDip);
        place(scope_, {scopeLeft, filterTop, (std::min)(scopeLeft + kScopeWidthDip, sortLeft - kRowGapDip), filterBottom},
              kComboDropdownHeightPx, filtersExpanded_);
        place(sort_, {sortLeft, filterTop, directionLeft - kRowGapDip, filterBottom},
              kComboDropdownHeightPx, filtersExpanded_);
        place(sortDirection_, {directionLeft, filterTop, clearLeft - kRowGapDip, filterBottom},
              kFilterRowHeightDip, filtersExpanded_);
        place(clearHistory_, {clearLeft, filterTop, right, filterBottom}, kFilterRowHeightDip, filtersExpanded_);
    } else {
        const float mid = left + (right - left) / 2.0f;
        place(scope_, {left, filterTop, mid - kRowGapDip / 2.0f, filterBottom}, kComboDropdownHeightPx, filtersExpanded_);
        place(sort_, {mid + kRowGapDip / 2.0f, filterTop, right, filterBottom}, kComboDropdownHeightPx, filtersExpanded_);
        place(sortDirection_, {left, compactSecondTop, mid - kRowGapDip / 2.0f, compactSecondBottom},
              kFilterRowHeightDip, filtersExpanded_);
        place(clearHistory_, {mid + kRowGapDip / 2.0f, compactSecondTop, right, compactSecondBottom},
              kFilterRowHeightDip, filtersExpanded_);
    }
    place(list_, listRect, listRect.Height(), true);

    UpdateClearAffordance(); // visibility follows the freshly placed geometry
}

bool SearchPanel::HandleOwnerCommand(WPARAM wParam, LPARAM) {
    if (!visible_) return false;
    const int id = LOWORD(wParam), notification = HIWORD(wParam);
    if (id == kQueryId && notification == CBN_EDITCHANGE) {
        UpdateClearAffordance(); // ui-v3 search: clear affordance follows text
        if (updatingHistory_) return true;
        ++generation_;
        currentGeneration_ = generation_;
        KillTimer(owner_, kDebounceTimer);
        SetTimer(owner_, kDebounceTimer, kDebounceMilliseconds, nullptr);
        UpdateStatus(L"Waiting for typing to pause…");
        return true;
    }
    if (id == kScopeId && notification == CBN_SELCHANGE) {
        UpdateFilterSummary(); // ui-v3 search: scope chip tracks non-default state
        Dispatch(false);
        return true;
    }
    if (id == kSortId && notification == CBN_SELCHANGE) {
        UpdateFilterSummary();
        Dispatch(false);
        return true;
    }
    if (id == kSortDirectionId && notification == BN_CLICKED) {
        descending_ = !descending_;
        SetWindowTextW(sortDirection_, descending_ ? L"Descending" : L"Ascending");
        UpdateFilterSummary();
        Dispatch(false);
        return true;
    }
    if (id == kFiltersToggleId && notification == BN_CLICKED) {
        ToggleFiltersExpanded();
        return true;
    }
    if (id == kClearHistoryId && notification == BN_CLICKED) {
        ClearHistory();
        return true;
    }
    return false;
}

bool SearchPanel::HandleTimer(UINT_PTR timerId) {
    if (timerId == kHistorySaveTimer) {
        // The coalesced history flush runs even while the panel is hidden so a
        // recorded query is never stranded dirty until teardown.
        KillTimer(owner_, kHistorySaveTimer);
        if (historyDirty_) {
            historyDirty_ = false;
            SaveHistory();
        }
        return true;
    }
    // ui-v3 search: indeterminate progress band phase, advanced only while a
    // query is actually running and the panel is visible.
    if (timerId == kProgressTimer) {
        if (statusKind_ != StatusKind::Searching || !visible_) {
            KillTimer(owner_, kProgressTimer);
            return true;
        }
        progressPhase_ += static_cast<float>(kProgressTimerMilliseconds) / 1000.0f / kProgressSweepSeconds;
        if (progressPhase_ >= 1.0f) progressPhase_ -= 1.0f;
        if (filterBar_) InvalidateRect(filterBar_, nullptr, FALSE);
        return true;
    }
    if (!visible_ || timerId != kDebounceTimer) return false;
    KillTimer(owner_, kDebounceTimer);
    // A debounced keystroke is not a committed search: history is recorded
    // when the panel closes or the query is explicitly invoked.
    Dispatch(false);
    return true;
}

void SearchPanel::Dispatch(bool recordHistory, bool forceSnapshot) {
    std::array<wchar_t, static_cast<size_t>(ffprotocol::kMaxIndexedSearchQueryLengthChars) + 1> queryText{};
    GetWindowTextW(query_, queryText.data(), static_cast<int>(queryText.size()));
    UpdateClearAffordance(); // ui-v3 search: covers programmatic text changes too
    ++generation_;
    currentGeneration_ = generation_;
    if (queryText[0] == L'\0') {
        results_.clear();
        currentPrimaryTerm_.clear();
        statusWarning_.clear();
        ListView_SetItemCountEx(list_, 0, LVSICF_NOSCROLL);
        UpdateStatus(history_.empty() ? L"Type to search" : L"Choose a previous query or type to search");
        return;
    }
    lastSnapshotRefresh_ = std::chrono::steady_clock::now();
    snapshotRefreshPending_ = false;
    Work work;
    work.generation = generation_;
    work.request.query = ffsearch::ParseQuery(queryText.data(), ffsearch::FilterRegistry::WithDefaults());
    // Surface parse diagnostics instead of silently degrading the query: the
    // warning is prefixed to the next status update (searching/results).
    {
        std::vector<std::wstring> notices;
        for (const auto& key : work.request.query.invalidFilters) notices.push_back(L"Ignored invalid filter: " + key + L":");
        for (const auto& key : work.request.query.unrecognizedKeys) notices.push_back(L"Unknown filter: " + key + L":");
        std::wstring warning;
        for (const auto& notice : notices) {
            if (!warning.empty()) warning += L"  ";
            warning += notice;
        }
        statusWarning_ = std::move(warning);
    }
    work.request.scope = static_cast<ffsearch::SearchScope>((std::max)(0, static_cast<int>(SendMessageW(scope_, CB_GETCURSEL, 0, 0))));
    work.request.scopePath = currentPath_;
    work.request.sortField = static_cast<ffsearch::SortField>((std::max)(0, static_cast<int>(SendMessageW(sort_, CB_GETCURSEL, 0, 0))));
    work.request.descending = descending_;
    work.request.maxResults = kMaxUiSearchResults;
    // Once the snapshot contains durable index rows, every scope whose label
    // promises indexed descendants must exclude browsed-only records. The
    // current-folder scope deliberately remains an "available data" view so
    // degraded browsing is still useful when the folder has no indexed rows.
    work.request.persistentOnly = persistentCoverage_ &&
        static_cast<int>(work.request.scope) >= static_cast<int>(ffsearch::SearchScope::CurrentFolderAndSubfolders);
    currentPrimaryTerm_ = work.request.query.primaryTerm;
    if (recordHistory) RecordHistory(queryText.data());

    const bool broadIndexedScope = static_cast<int>(work.request.scope) >=
        static_cast<int>(ffsearch::SearchScope::CurrentFolderAndSubfolders);
    if (!forceSnapshot && persistentCoverage_ && broadIndexedScope && engine_ != nullptr) {
        snapshotFallback_ = false;
        UpdateStatus(L"Searching indexed locations…", StatusKind::Searching);
        ffprotocol::IndexedSearchRequest indexedRequest;
        indexedRequest.scope = static_cast<uint8_t>(work.request.scope);
        indexedRequest.sortField = static_cast<uint8_t>(work.request.sortField);
        indexedRequest.descending = work.request.descending;
        indexedRequest.maxResults = static_cast<uint32_t>(work.request.maxResults);
        indexedRequest.query = queryText.data();
        indexedRequest.scopePath = currentPath_;
        const uint64_t previousIndexedRequestId = indexedRequestId_.load();
        if (previousIndexedRequestId != 0) {
            engine_->CancelIndexedSearch(previousIndexedRequestId);
        }
        indexedRequestId_.store(engine_->RequestIndexedSearch(
            indexedRequest.query, indexedRequest.scope, indexedRequest.scopePath,
            indexedRequest.sortField, indexedRequest.descending, indexedRequest.maxResults,
            [this, generation = work.generation,
             alive = std::weak_ptr<IndexedCallbackState>(indexedCallbackState_)](
                ffprotocol::IndexedSearchResponse response) {
                const auto callbackState = alive.lock();
                if (!callbackState) return;
                std::lock_guard callbackLock(callbackState->mutex);
                if (!callbackState->alive || stopping_.load(std::memory_order_acquire)) return;
                uint64_t expected = response.requestId;
                indexedRequestId_.compare_exchange_strong(expected, 0);
                auto completion = std::make_unique<IndexedCompletion>(
                    IndexedCompletion{generation, std::move(response)});
                if (PostMessageW(owner_, WM_APP_INDEXED_SEARCH_COMPLETE, 0,
                                 reinterpret_cast<LPARAM>(completion.get()))) {
                    completion.release();
                }
            }));
        return;
    }

    snapshotFallback_ = forceSnapshot && persistentCoverage_ && broadIndexedScope;
    UpdateStatus(snapshotFallback_ ? L"Indexed search unavailable; searching browsed snapshot…"
                                   : L"Searching…", StatusKind::Searching);
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        pendingWork_ = std::move(work);
    }
    workCv_.notify_one();
}

void SearchPanel::WorkerMain() {
    while (!stopping_) {
        Work work;
        {
            std::unique_lock<std::mutex> lock(workMutex_);
            workCv_.wait(lock, [this] { return stopping_ || pendingWork_.has_value(); });
            if (stopping_) break;
            work = std::move(*pendingWork_);
            pendingWork_.reset();
        }
        // Snapshot traversal can be large in degraded mode. Keep candidate
        // construction off the UI thread just like query execution so a
        // keystroke never stalls painting or navigation.
        PopulateCandidates(work.candidates);
        if (stopping_ || currentGeneration_.load() != work.generation) continue;
        auto response = ffsearch::ExecuteSearch(work.candidates, work.request, [this, generation = work.generation] {
            return stopping_ || currentGeneration_.load() != generation;
        });
        if (response.cancelled) continue;
        auto completion = std::make_unique<Completion>(Completion{work.generation, std::move(response), std::move(work.candidates)});
        if (PostMessageW(owner_, WM_APP_SEARCH_COMPLETE, 0, reinterpret_cast<LPARAM>(completion.get()))) completion.release();
    }
}

bool SearchPanel::HandleCompletion(LPARAM lParam) {
    std::unique_ptr<Completion> completion(reinterpret_cast<Completion*>(lParam));
    if (!completion || completion->generation != currentGeneration_) return true;
    results_ = std::move(completion->response.results);
    candidates_ = std::move(completion->candidates);
    ListView_SetItemCountEx(list_, static_cast<int>(results_.size()), LVSICF_NOINVALIDATEALL);
    if (completion->response.truncated) {
        const std::wstring prefix = snapshotFallback_ ? L"Browsed snapshot: " : L"";
        UpdateStatus(statusWarning_ + prefix + L"showing the first " + std::to_wstring(results_.size()) + L" results", StatusKind::Info);
    } else {
        const std::wstring prefix = snapshotFallback_ ? L"Browsed snapshot: " : L"";
        UpdateStatus(statusWarning_ + (results_.empty() ? prefix + L"no results" : prefix + std::to_wstring(results_.size()) + L" results"),
                     results_.empty() ? StatusKind::NoResults : StatusKind::Info);
    }
    InvalidateRect(list_, nullptr, FALSE);
    // semantic-uia-core-surfaces 4.4: the result set was replaced.
    NotifyUia({UiaEventKind::StructureBulkRemoved, -1, -1, -1, -1, -1, false, false, /*search=*/true});
    NotifyUia({UiaEventKind::StructureBulkAdded, -1, -1, -1, -1, -1, false, false, /*search=*/true});
    return true;
}

bool SearchPanel::HandleIndexedCompletion(LPARAM lParam) {
    std::unique_ptr<IndexedCompletion> completion(reinterpret_cast<IndexedCompletion*>(lParam));
    if (!completion || completion->generation != currentGeneration_) return true;
    if (completion->response.status != ffprotocol::IndexedSearchStatus::Completed) {
        // A disconnected/unavailable index must not strand the user's query.
        // Re-enter the existing worker path once, with an explicit source
        // notice; the broad scope remains labelled according to its data.
        Dispatch(false, true);
        return true;
    }

    snapshotFallback_ = false;
    candidates_.clear();
    candidates_.reserve(completion->response.candidates.size());
    results_.clear();
    results_.reserve(completion->response.candidates.size());
    for (auto& candidate : completion->response.candidates) {
        ffsearch::Candidate searchCandidate{};
        searchCandidate.name = candidate.name;
        searchCandidate.folder = candidate.folder;
        searchCandidate.sizeBytes = candidate.sizeBytes;
        searchCandidate.modifiedTime = candidate.modifiedTime;
        searchCandidate.isDirectory = candidate.isDirectory;
        searchCandidate.createdTime = candidate.createdTime;
        searchCandidate.volumeId = candidate.volumeId;
        searchCandidate.id = candidate.id;
        searchCandidate.parentId = candidate.parentId;
        searchCandidate.persistentIndexed = candidate.persistentIndexed;
        // indexed-search-allocated-size D1: carry on-disk size + knownness from
        // the V2 response into the Search row model verbatim.
        searchCandidate.allocatedSizeBytes = candidate.allocatedSizeBytes;
        searchCandidate.allocatedSizeKnown = candidate.allocatedSizeKnown;
        candidates_.push_back(searchCandidate);
        results_.push_back({searchCandidate, ffsearch::ClassifyMatch(searchCandidate, currentPrimaryTerm_)});
    }
    ListView_SetItemCountEx(list_, static_cast<int>(results_.size()), LVSICF_NOINVALIDATEALL);
    if (completion->response.truncated) {
        UpdateStatus(statusWarning_ + L"Showing the first " + std::to_wstring(results_.size()) + L" indexed results", StatusKind::Info);
    } else {
        UpdateStatus(statusWarning_ + (results_.empty() ? L"No indexed results" :
                     std::to_wstring(results_.size()) + L" indexed results"),
                     results_.empty() ? StatusKind::NoResults : StatusKind::Info);
    }
    InvalidateRect(list_, nullptr, FALSE);
    // semantic-uia-core-surfaces 4.4: the indexed result set was replaced.
    NotifyUia({UiaEventKind::StructureBulkRemoved, -1, -1, -1, -1, -1, false, false, /*search=*/true});
    NotifyUia({UiaEventKind::StructureBulkAdded, -1, -1, -1, -1, -1, false, false, /*search=*/true});
    return true;
}

bool SearchPanel::HandleNotify(LPARAM lParam) {
    if (!visible_) return false;
    auto* header = reinterpret_cast<NMHDR*>(lParam);
    if (header == nullptr || header->hwndFrom != list_) return false;
    if (header->code == LVN_GETDISPINFOW) {
        auto* info = reinterpret_cast<NMLVDISPINFOW*>(lParam);
        if (info->item.iItem < 0 || static_cast<size_t>(info->item.iItem) >= results_.size() ||
            (info->item.mask & LVIF_TEXT) == 0) return true;
        const auto& candidate = results_[static_cast<size_t>(info->item.iItem)].candidate;
        std::wstring text;
        switch (info->item.iSubItem) {
            case 0: text = candidate.name; break;
            case 1: text = candidate.isDirectory ? L"Folder" : L"File"; break;
            case 2: text = candidate.folder; break;
            case 3: text = candidate.isDirectory ? L"—" : ffui::FormatSize(candidate.sizeBytes); break;
            case 4: text = FormatFileTime(candidate.modifiedTime); break;
            // indexed-search-allocated-size D7: Size on Disk column mirrors the
            // shared wording (known bytes, known zero, or explicit unknown).
            case 5: text = ffui::SearchAllocatedSizeText(candidate.allocatedSizeBytes,
                                                         candidate.allocatedSizeKnown, ffui::FormatSize);
                    break;
        }
        wcsncpy_s(info->item.pszText, static_cast<size_t>(info->item.cchTextMax), text.c_str(), _TRUNCATE);
        return true;
    }
    if (header->code == NM_DBLCLK || header->code == LVN_ITEMACTIVATE) {
        const int selected = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
        if (selected >= 0 && static_cast<size_t>(selected) < results_.size()) {
            const auto candidate = results_[static_cast<size_t>(selected)].candidate;
            const auto found = std::find_if(candidates_.begin(), candidates_.end(), [&](const ffsearch::Candidate& entry) {
                return entry.volumeId == candidate.volumeId && entry.id == candidate.id;
            });
            const auto reconstruction = found == candidates_.end()
                ? ffsearch::PathReconstruction{}
                : ffsearch::ReconstructPath(candidates_, static_cast<size_t>(found - candidates_.begin()));
            Hide();
            navigate_(candidate, reconstruction);
        }
        return true;
    }
    if (header->code == LVN_ITEMCHANGED) {
        // semantic-uia-core-surfaces 4.3/4.4: native selection/focus changes
        // (mouse + keyboard) become UIA selection/focus events.
        const auto* change = reinterpret_cast<NMLISTVIEW*>(lParam);
        if ((change->uChanged & LVIF_STATE) != 0) {
            const int oldSelected = (change->uOldState & LVIS_SELECTED) != 0 ? change->iItem : -1;
            const int newSelected = (change->uNewState & LVIS_SELECTED) != 0 ? change->iItem : -1;
            if (oldSelected != newSelected) {
                NotifyUia({UiaEventKind::SelectionInvalidated, -1, -1, -1, -1, -1,
                           false, false, /*search=*/true, static_cast<size_t>(change->iItem)});
                if (newSelected >= 0) {
                    NotifyUia({UiaEventKind::ElementAddedToSelection, -1, -1, -1, -1, -1,
                               false, false, /*search=*/true, static_cast<size_t>(newSelected)});
                }
                if (oldSelected >= 0) {
                    NotifyUia({UiaEventKind::ElementRemovedFromSelection, -1, -1, -1, -1, -1,
                               false, false, /*search=*/true, static_cast<size_t>(oldSelected)});
                }
            }
            const bool oldFocused = (change->uOldState & LVIS_FOCUSED) != 0;
            const bool newFocused = (change->uNewState & LVIS_FOCUSED) != 0;
            if (oldFocused != newFocused && newFocused && change->iItem >= 0) {
                NotifyUia({UiaEventKind::FocusChanged, -1, -1, -1, -1, -1,
                           false, false, /*search=*/true, static_cast<size_t>(change->iItem)});
            }
        }
        return true;
    }
    return false;
}

bool SearchPanel::HandleDrawItem(LPARAM lParam) {
    const auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
    if (item == nullptr) return false;
    if (item->hwndItem == list_) {
        DrawSearchResultRow(item);
        return true;
    }
    if (item->hwndItem == scope_ || item->hwndItem == sort_) {
        if (item->itemID == static_cast<UINT>(-1)) return false;
        DrawComboItem(item);
        return true;
    }
    if (item->hwndItem == sortDirection_ || item->hwndItem == clearHistory_) {
        DrawTextButton(item->hwndItem, item);
        return true;
    }
    if (item->hwndItem == filtersToggle_) {
        DrawFiltersButton(item);
        return true;
    }
    return false;
}

void SearchPanel::DrawSearchResultRow(const DRAWITEMSTRUCT* item) {
    HDC dc = item->hDC;
    const RECT row = item->rcItem;
    const bool selected = (item->itemState & ODS_SELECTED) != 0;
    const bool focused = (item->itemState & ODS_FOCUS) != 0;
    const bool highContrast = ffui::UiSystemHighContrast();
    const ffui::UiTheme theme = ffui::GetUiTheme(ffui::gUiDarkTheme);
    EnsureResultFonts();

    // ui-v3 search: canonical row states through the shared grammar, mirroring
    // ResolveUiRowPalette/ResolveNavPaint. Selection/focus identity stays with
    // the native ListView; only the presentation is resolved here.
    const COLORREF baseBackground = highContrast ? GetSysColor(COLOR_WINDOW)
                                                 : ffui::ToColorRef(theme.background);
    UiVisualState state = UiVisualState::Rest;
    if (selected) {
        state = focused ? UiVisualState::SelectedFocused : UiVisualState::SelectedUnfocused;
    } else if (static_cast<int>(item->itemID) == hoveredResultRow_) {
        state = UiVisualState::Hover;
    }
    const SearchRowPaint paint = ResolveSearchRowPaint(theme, state, highContrast, baseBackground);

    HBRUSH backgroundBrush = CreateSolidBrush(baseBackground);
    FillRect(dc, &row, backgroundBrush);
    DeleteObject(backgroundBrush);
    if (paint.hasFill) {
        UiGdiRoundedRect(dc, row, paint.fill, Scaled(UiMetrics::kRadiusSmall));
    }

    if (static_cast<size_t>(item->itemID) >= results_.size()) {
        if (focused) DrawGdiFocusRing(dc, row, Scaled(UiMetrics::kRadiusSmall),
                                      highContrast ? GetSysColor(COLOR_WINDOWTEXT)
                                                   : ffui::ToColorRef(theme.focusStroke));
        return;
    }
    const auto& candidate = results_[static_cast<size_t>(item->itemID)].candidate;

    HFONT fallbackFont = reinterpret_cast<HFONT>(SendMessageW(list_, WM_GETFONT, 0, 0));
    if (fallbackFont == nullptr) fallbackFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HFONT nameFont = resultNameFont_ != nullptr ? resultNameFont_ : fallbackFont;
    HFONT matchFont = resultMatchFont_ != nullptr ? resultMatchFont_ : nameFont;
    HFONT captionFont = resultCaptionFont_ != nullptr ? resultCaptionFont_ : fallbackFont;

    // ui-v3 search: two-line hierarchy — the file name is primary (Body), the
    // location path and metadata secondary (Caption, muted tokens).
    const RECT line1{row.left, row.top + Scaled(3.0f), row.right, row.top + Scaled(21.0f)};
    const RECT line2{row.left, row.bottom - Scaled(17.0f), row.right, row.bottom - Scaled(2.0f)};

    // Icon (system SHIL_SMALL image list), vertically centered on the name line.
    IImageList* imageList = nullptr;
    if (SUCCEEDED(SHGetImageList(SHIL_SMALL, IID_PPV_ARGS(&imageList)))) {
        int iconWidth = 0;
        int iconHeight = 0;
        if (ImageList_GetIconSize(IImageListToHIMAGELIST(imageList), &iconWidth, &iconHeight) && iconWidth > 0 && iconHeight > 0) {
            const int iconLeft = row.left + Scaled(UiMetrics::kSpaceS);
            const int iconTop = line1.top + ((line1.bottom - line1.top) - iconHeight) / 2;
            const int iconIndex = SystemIconIndex(candidate.isDirectory ? ffui::FolderKey()
                                                                        : ffui::IconKeyForExtension(FileExtension(candidate.name)));
            if (iconIndex >= 0) {
                ImageList_Draw(IImageListToHIMAGELIST(imageList), iconIndex, dc, iconLeft, iconTop, ILD_TRANSPARENT);
            }
        }
    }

    const int textLeft = row.left + Scaled(UiMetrics::kSpaceS) +
                         Scaled(UiMetrics::kIconSize) + Scaled(UiMetrics::kSpaceXs);

    // indexed-search-allocated-size D7: a right-aligned metadata block renders
    // "logical • Size on Disk • modified" so the two sizes stay visibly distinct
    // without redesigning the result surface. Directories show a dash for logical
    // size (matching the Size column); the on-disk wording comes from the shared
    // helper so the row and the UIA projection cannot drift in meaning.
    const std::wstring metaLogical = candidate.isDirectory ? L"—" : ffui::FormatSize(candidate.sizeBytes);
    const std::wstring metaOnDisk = ffui::SearchAllocatedSizeText(candidate.allocatedSizeBytes,
                                                                  candidate.allocatedSizeKnown, ffui::FormatSize);
    const std::wstring metaModified = FormatFileTime(candidate.modifiedTime);
    const std::wstring metaText = metaLogical + L" • " + metaOnDisk + L" • " + metaModified;

    // Name line: matched span emphasized in an accent-derived color that keeps
    // contrast under every row state (accent on rest/hover/soft-selection,
    // textOnAccent over the filled SelectedFocused state; semibold carries a
    // non-color cue so emphasis never rides on hue alone).
    const COLORREF matchColor =
        (state == UiVisualState::SelectedFocused || highContrast) ? paint.text : ffui::ToColorRef(theme.accent);
    const MatchSpan span = FindMatchSpan(candidate.name, currentPrimaryTerm_);
    int x = textLeft;
    if (span.count == 0 || span.start >= candidate.name.size()) {
        x += DrawRowText(dc, nameFont, x, line1, candidate.name, paint.text);
    } else {
        x += DrawRowText(dc, nameFont, x, line1, candidate.name.substr(0, span.start), paint.text);
        x += DrawRowText(dc, matchFont, x, line1, candidate.name.substr(span.start, span.count), matchColor);
        DrawRowText(dc, nameFont, x, line1, candidate.name.substr(span.start + span.count), paint.text);
    }

    // Second line: location path on the left (capped so long names keep room),
    // metadata block right-aligned; both ellipsized independently.
    if (!candidate.folder.empty()) {
        const int folderMaxRight = row.left + Scaled(0.55f * static_cast<float>(row.right - row.left));
        SetTextColor(dc, paint.secondary);
        HGDIOBJ previous = SelectObject(dc, captionFont);
        RECT folderRect{textLeft, line2.top, (std::max)(textLeft, folderMaxRight), line2.bottom};
        DrawTextW(dc, candidate.folder.c_str(), static_cast<int>(candidate.folder.size()), &folderRect,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(dc, previous);
    }
    {
        SetTextColor(dc, paint.tertiary);
        HGDIOBJ previous = SelectObject(dc, captionFont);
        SIZE metaSize{};
        GetTextExtentPoint32W(dc, metaText.c_str(), static_cast<int>(metaText.size()), &metaSize);
        const int metaLeft = (std::max)(textLeft, static_cast<int>(row.right) - static_cast<int>(metaSize.cx) -
                                                           Scaled(UiMetrics::kSpaceS));
        RECT metaRect{metaLeft, line2.top, row.right - Scaled(UiMetrics::kSpaceXs), line2.bottom};
        DrawTextW(dc, metaText.c_str(), static_cast<int>(metaText.size()), &metaRect,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(dc, previous);
    }

    // Keyboard-focus indicator, independent of hover/selection.
    if (focused) {
        DrawGdiFocusRing(dc, row, Scaled(UiMetrics::kRadiusSmall),
                         highContrast ? GetSysColor(COLOR_WINDOWTEXT)
                                      : ffui::ToColorRef(theme.focusStroke));
    }
}

int SearchPanel::SystemIconIndex(const std::wstring& key) {
    const auto cached = iconIndexCache_.find(key);
    if (cached != iconIndexCache_.end()) return cached->second;

    int index = -1;
    if (key == ffui::FolderKey()) {
        SHSTOCKICONINFO stock{};
        stock.cbSize = static_cast<DWORD>(sizeof(stock));
        // SHGSI_SYSICONINDEX fills iSysImageIndex with the system image-list
        // index (this SDK ships no SHGSI_ICONINDEX flag).
        if (SUCCEEDED(SHGetStockIconInfo(SIID_FOLDER, SHGSI_SYSICONINDEX, &stock))) index = stock.iSysImageIndex;
    } else {
        std::wstring pseudoPath = L"file";
        if (!key.empty()) {
            if (key[0] == L'.') {
                pseudoPath += key;
            } else {
                pseudoPath += L".";
                pseudoPath += key;
            }
        }
        SHFILEINFOW info{};
        if (SHGetFileInfoW(pseudoPath.c_str(), FILE_ATTRIBUTE_NORMAL, &info, static_cast<UINT>(sizeof(info)),
                           SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES) != 0) {
            index = info.iIcon;
        }
    }
    iconIndexCache_[key] = index;
    return index;
}

void SearchPanel::DrawComboItem(const DRAWITEMSTRUCT* item) {
    HDC dc = item->hDC;
    const RECT rect = item->rcItem;
    const bool highContrast = ffui::UiSystemHighContrast();
    const ffui::UiTheme theme = ffui::GetUiTheme(ffui::gUiDarkTheme);
    const bool selected = (item->itemState & ODS_SELECTED) != 0;

    // ui-v3 search: dropdown rows share the row-state grammar — a rounded
    // selection pill instead of a full-square accent band.
    COLORREF background = ffui::ToColorRef(theme.background);
    if (selected) background = ffui::ToColorRef(theme.accent);
    COLORREF text = selected ? ffui::ToColorRef(theme.textOnAccent) : ffui::ToColorRef(theme.text);
    if (highContrast) {
        background = GetSysColor(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW);
        text = GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT);
    }

    HBRUSH backgroundBrush = CreateSolidBrush(background);
    FillRect(dc, &rect, backgroundBrush);
    DeleteObject(backgroundBrush);
    if (selected) {
        RECT pill = rect;
        pill.left += 1;
        pill.right -= 1;
        pill.top += 1;
        pill.bottom -= 1;
        UiGdiRoundedRect(dc, pill, background, Scaled(UiMetrics::kRadiusSmall));
    }

    // Size the buffer from CB_GETLBTEXTLEN so any future longer label cannot
    // overflow the fixed read (native UIA proxies read these same strings).
    const int textLength = static_cast<int>(
        SendMessageW(item->hwndItem, CB_GETLBTEXTLEN, item->itemID, 0));
    if (textLength < 0) return;
    std::wstring itemText(static_cast<size_t>(textLength) + 1, L'\0');
    SendMessageW(item->hwndItem, CB_GETLBTEXT, item->itemID,
                 reinterpret_cast<LPARAM>(itemText.data()));
    SetTextColor(dc, text);
    SetBkMode(dc, TRANSPARENT);
    RECT textRect = rect;
    textRect.left += Scaled(UiMetrics::kSpaceXs);
    DrawTextW(dc, itemText.c_str(), -1, &textRect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    if ((item->itemState & ODS_FOCUS) != 0) {
        DrawGdiFocusRing(dc, rect, Scaled(UiMetrics::kRadiusSmall),
                         highContrast ? GetSysColor(COLOR_WINDOWTEXT)
                                      : ffui::ToColorRef(theme.focusStroke));
    }
}

void SearchPanel::DrawTextButton(HWND source, const DRAWITEMSTRUCT* item) {
    HDC dc = item->hDC;
    RECT rect = item->rcItem;
    const bool highContrast = ffui::UiSystemHighContrast();
    const ffui::UiTheme theme = ffui::GetUiTheme(ffui::gUiDarkTheme);
    const bool pressed = (item->itemState & ODS_SELECTED) != 0;
    const bool hasFocus = (item->itemState & ODS_FOCUS) != 0;
    // ui-v3 search: quiet secondary buttons with tracked hover (native buttons
    // report pressed/focus but never hover).
    const bool hot = source == sortDirection_ ? directionHot_
                                              : clearHistoryHot_;
    const int radius = Scaled(UiMetrics::kRadiusSmall);

    COLORREF background = pressed ? CompositeOver(theme.pressOverlay, ToColorRef(theme.surfaceElevated))
                     : hot       ? CompositeOver(theme.hoverOverlay, ToColorRef(theme.surfaceElevated))
                                 : ToColorRef(theme.surfaceElevated);
    COLORREF text = ffui::ToColorRef(theme.text);
    COLORREF border = ffui::ToColorRef(theme.border);
    if (highContrast) {
        background = GetSysColor(pressed ? COLOR_BTNSHADOW : COLOR_BTNFACE);
        text = GetSysColor(COLOR_BTNTEXT);
        border = GetSysColor(COLOR_WINDOWTEXT);
    }

    UiGdiRoundedRect(dc, rect, background, radius);
    HPEN borderPen = CreatePen(PS_SOLID, 1, border);
    if (borderPen != nullptr) {
        HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
        HPEN oldPen = static_cast<HPEN>(SelectObject(dc, borderPen));
        RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius * 2, radius * 2);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(borderPen);
    }

    wchar_t textBuffer[128]{};
    GetWindowTextW(source, textBuffer, static_cast<int>(std::size(textBuffer)));
    SetTextColor(dc, text);
    SetBkMode(dc, TRANSPARENT);
    if (source == sortDirection_) {
        // ui-v3 search: direction glyph beside the label. The window text stays
        // the plain direction word so the UIA Name contract remains stable;
        // the arrow affordance is paint-only.
        HFONT guiFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        HFONT oldFont = static_cast<HFONT>(SelectObject(dc, guiFont));
        const int length = static_cast<int>(lstrlenW(textBuffer));
        SIZE textSize{};
        GetTextExtentPoint32W(dc, textBuffer, length, &textSize);
        const int arrowWidth = Scaled(9);
        const int gap = Scaled(4);
        const int contentWidth = arrowWidth + gap + static_cast<int>(textSize.cx);
        const int contentLeft = static_cast<int>(rect.left) +
                                (std::max)(0, (static_cast<int>(rect.right) - static_cast<int>(rect.left) - contentWidth) / 2);
        const POINT center{contentLeft + arrowWidth / 2, (rect.top + rect.bottom) / 2};
        const int halfWidth = Scaled(3);
        const int halfHeight = Scaled(2);
        HBRUSH fillBrush = CreateSolidBrush(text);
        HPEN outlinePen = CreatePen(PS_SOLID, 1, text);
        if (fillBrush != nullptr && outlinePen != nullptr) {
            HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, fillBrush));
            HPEN oldPen = static_cast<HPEN>(SelectObject(dc, outlinePen));
            POINT triangle[3];
            if (!descending_) {
                triangle[0] = POINT{center.x, center.y - halfHeight};
                triangle[1] = POINT{center.x - halfWidth, center.y + halfHeight};
                triangle[2] = POINT{center.x + halfWidth, center.y + halfHeight};
            } else {
                triangle[0] = POINT{center.x, center.y + halfHeight};
                triangle[1] = POINT{center.x - halfWidth, center.y - halfHeight};
                triangle[2] = POINT{center.x + halfWidth, center.y - halfHeight};
            }
            Polygon(dc, triangle, 3);
            SelectObject(dc, oldBrush);
            SelectObject(dc, oldPen);
            DeleteObject(fillBrush);
            DeleteObject(outlinePen);
        }
        RECT labelRect{contentLeft + arrowWidth + gap, rect.top, rect.right, rect.bottom};
        DrawTextW(dc, textBuffer, length, &labelRect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(dc, oldFont);
    } else {
        DrawTextW(dc, textBuffer, -1, &rect, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    }
    if (hasFocus) {
        DrawGdiFocusRing(dc, rect, radius,
                         highContrast ? GetSysColor(COLOR_WINDOWTEXT)
                                      : ffui::ToColorRef(theme.focusStroke));
    }
}

// ui-v3 search a11y: owner-draw for the Filters disclosure button. Renders as
// the same quiet pill the bar used to paint (chevron + caption label, muted
// text, hover/press overlays over the shared background token) while being a
// real BUTTON so UIA exposes an actionable element and keyboard focus rings
// work. High Contrast falls back to system colors like every sibling control.
void SearchPanel::DrawFiltersButton(const DRAWITEMSTRUCT* item) {
    HDC dc = item->hDC;
    const RECT rect = item->rcItem;
    const bool highContrast = ffui::UiSystemHighContrast();
    const ffui::UiTheme theme = ffui::GetUiTheme(ffui::gUiDarkTheme);
    const bool pressed = (item->itemState & ODS_SELECTED) != 0;
    const bool hasFocus = (item->itemState & ODS_FOCUS) != 0;

    COLORREF fill = pressed ? CompositeOver(theme.pressOverlay, ToColorRef(theme.background))
                    : filtersHot_
                        ? CompositeOver(theme.hoverOverlay, ToColorRef(theme.background))
                        : ToColorRef(theme.background);
    COLORREF fg = highContrast ? GetSysColor(COLOR_WINDOWTEXT)
                                 : ffui::ToColorRef(theme.textSecondary);
    if (highContrast) {
        fill = pressed ? GetSysColor(COLOR_BTNSHADOW)
                       : filtersHot_ ? GetSysColor(COLOR_BTNFACE) : GetSysColor(COLOR_WINDOW);
    }
    const int radius = Scaled(UiMetrics::kRadiusSmall);
    UiGdiRoundedRect(dc, rect, fill, radius);

    EnsureResultFonts();
    HFONT captionFont = resultCaptionFont_ != nullptr
                            ? resultCaptionFont_
                            : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    wchar_t label[32]{};
    GetWindowTextW(item->hwndItem, label, static_cast<int>(std::size(label)));
    const int chevronWidth = Scaled(8);
    SIZE labelSize{};
    SelectObject(dc, captionFont);
    GetTextExtentPoint32W(dc, label, static_cast<int>(lstrlenW(label)), &labelSize);
    // Content block centered in the pill: chevron, small gap, then the label.
    const int contentWidth = chevronWidth + Scaled(UiMetrics::kSpaceXxs) + static_cast<int>(labelSize.cx);
    const int contentLeft = static_cast<int>(rect.left) +
                            (std::max)(0, (static_cast<int>(rect.right) - static_cast<int>(rect.left) - contentWidth) / 2);
    const POINT chevronCenter{contentLeft + chevronWidth / 2, (rect.top + rect.bottom) / 2};
    DrawChevron(dc, chevronCenter, chevronWidth / 2, /*expanded=*/filtersExpanded_, fg);
    SetTextColor(dc, fg);
    SetBkMode(dc, TRANSPARENT);
    RECT labelRect{contentLeft + chevronWidth + Scaled(UiMetrics::kSpaceXxs), rect.top,
                   rect.right, rect.bottom};
    DrawTextW(dc, label, -1, &labelRect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    if (hasFocus) {
        DrawGdiFocusRing(dc, rect, radius,
                         highContrast ? GetSysColor(COLOR_WINDOWTEXT)
                                      : ffui::ToColorRef(theme.focusStroke));
    }
}

void SearchPanel::PaintSearchEdit(HWND hwnd) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    if (!dc) return;

    const float dpi = ffui::UiDpiScale();
    if (!searchResourcesReady_ || dpi != lastSearchDpi_) {
        searchFactory_.Reset();
        searchTarget_.Reset();
        searchWriteFactory_.Reset();
        searchFormat_.Reset();
        lastSearchDpi_ = dpi;
        searchResourcesReady_ = false;
        if (SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&searchFactory_)))) {
            D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
                0.0f, 0.0f, D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);
            if (SUCCEEDED(searchFactory_->CreateDCRenderTarget(&properties, &searchTarget_)) &&
                SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                              reinterpret_cast<IUnknown**>(searchWriteFactory_.GetAddressOf()))) &&
                SUCCEEDED(UiCreateTextFormat(searchWriteFactory_.Get(), UiTypeSize(UiTypeRole::Body),
                                             DWRITE_FONT_WEIGHT_NORMAL, L"Segoe UI", &searchFormat_))) {
                // ui-v3 search: optional leading search glyph; if the glyph font
                // is unavailable the field simply renders without it.
                Microsoft::WRL::ComPtr<IDWriteTextFormat> glyphFormat;
                if (SUCCEEDED(UiCreateTextFormat(searchWriteFactory_.Get(), UiTypeSize(UiTypeRole::Body),
                                                 DWRITE_FONT_WEIGHT_NORMAL, L"Segoe MDL2 Assets",
                                                 &glyphFormat))) {
                    searchGlyphFormat_ = std::move(glyphFormat);
                }
                searchResourcesReady_ = true;
            }
        }
    }

    if (!searchResourcesReady_) {
        EndPaint(hwnd, &paint);
        return;
    }

    RECT client{};
    GetClientRect(hwnd, &client);
    if (FAILED(searchTarget_->BindDC(dc, &client))) {
        searchResourcesReady_ = false;
        EndPaint(hwnd, &paint);
        return;
    }

    const ffui::UiTheme theme = ffui::GetUiTheme(ffui::gUiDarkTheme);
    searchTarget_->BeginDraw();
    searchTarget_->Clear(theme.background);
    // ui-v3 search: obvious focus treatment — the border flips to the shared
    // focusStroke token and thickens slightly while the field has focus.
    const D2D1_COLOR_F borderColor = searchEditFocused_ ? theme.focusStroke : theme.searchBorder;
    ComPtr<ID2D1SolidColorBrush> border;
    ComPtr<ID2D1SolidColorBrush> textBrush;
    searchTarget_->CreateSolidColorBrush(borderColor, &border);
    searchTarget_->CreateSolidColorBrush(theme.searchText, &textBrush);
    const auto bounds = D2D1::RectF(0.5f, 0.5f,
                                    static_cast<float>(client.right) - 0.5f,
                                    static_cast<float>(client.bottom) - 0.5f);
    if (border) {
        searchTarget_->DrawRoundedRectangle(D2D1::RoundedRect(bounds, UiMetrics::kRadiusField, UiMetrics::kRadiusField),
                                            border.Get(), searchEditFocused_ ? 1.5f : 1.0f);
    }

    // Fluent focus accent: a restrained accent baseline along the bottom edge
    // so the focused field reads as contained and active.
    if (searchEditFocused_ && border) {
        const auto accentLine = D2D1::RectF(0.5f, static_cast<float>(client.bottom) - 2.0f,
                                            static_cast<float>(client.right) - 0.5f,
                                            static_cast<float>(client.bottom) - 0.5f);
        searchTarget_->FillRectangle(accentLine, border.Get());
    }

    int length = GetWindowTextLengthW(hwnd);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(hwnd, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    if (text.empty()) {
        // The placeholder must reflect actual capability: degraded mode has no
        // privileged index, only the browsed-folder snapshot.
        text = persistentCoverage_ ? L"Search indexed locations…" : L"Search browsed folders…";
        textBrush.Reset();
        searchTarget_->CreateSolidColorBrush(theme.searchPlaceholder, &textBrush);
    }
    // ui-v3 search: leading magnifier glyph anchors the field's purpose; the
    // text block yields on the right to the clear affordance when present.
    float textLeftDip = 10.0f;
    float rightPadDip = length > 0 ? kSpaceClearArrowMarginDip + 18.0f : 10.0f;
    if (searchGlyphFormat_) {
        const auto glyphRect = D2D1::RectF(8.0f, 0.0f, 8.0f + UiMetrics::kIconSizeLg,
                                           static_cast<float>(client.bottom));
        if (textBrush) {
            searchTarget_->DrawText(L"\uE721", 1, searchGlyphFormat_.Get(), glyphRect,
                                    textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        textLeftDip = 8.0f + UiMetrics::kIconSizeLg + UiMetrics::kSpaceS;
    }
    const auto textRect = D2D1::RectF(textLeftDip, 3.0f,
                                      (std::max)(textLeftDip + 20.0f,
                                                 static_cast<float>(client.right) - rightPadDip),
                                      static_cast<float>(client.bottom) - 3.0f);
    if (textBrush) {
        searchTarget_->DrawText(text.c_str(), static_cast<UINT32>(text.size()), searchFormat_.Get(), textRect,
                                textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    const HRESULT drawResult = searchTarget_->EndDraw();
    if (FAILED(drawResult) && drawResult == D2DERR_RECREATE_TARGET) {
        searchResourcesReady_ = false;
    }
    EndPaint(hwnd, &paint);
}

// ---------------------------------------------------------------------------
// ui-v3 search: custom-painted surfaces. The filter/disclosure bar carries the
// Filters disclosure pill, active-filter chips, the live status line, and the
// searching progress band; the query field hosts an in-field clear affordance;
// an empty result list renders a deliberate explanatory state instead of blank
// canvas. All colors come from GetUiTheme tokens (High Contrast falls back to
// system colors); no filesystem access happens on any of these paths.
// ---------------------------------------------------------------------------

void SearchPanel::PaintFilterBar(HWND hwnd) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    if (!dc) return;

    const ffui::UiTheme theme = ffui::GetUiTheme(ffui::gUiDarkTheme);
    const bool highContrast = ffui::UiSystemHighContrast();
    RECT client{};
    GetClientRect(hwnd, &client);
    const COLORREF background = highContrast ? GetSysColor(COLOR_WINDOW) : ffui::ToColorRef(theme.background);
    HBRUSH backgroundBrush = CreateSolidBrush(background);
    FillRect(dc, &client, backgroundBrush);
    DeleteObject(backgroundBrush);

    EnsureResultFonts();
    HFONT fallbackFont = reinterpret_cast<HFONT>(SendMessageW(list_, WM_GETFONT, 0, 0));
    if (fallbackFont == nullptr) fallbackFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HFONT captionFont = resultCaptionFont_ != nullptr ? resultCaptionFont_ : fallbackFont;
    const COLORREF textMuted = highContrast ? GetSysColor(COLOR_GRAYTEXT) : ffui::ToColorRef(theme.textSecondary);

    const int barHeight = client.bottom - client.top;
    const int chipHeight = (std::min)((std::max)(2, barHeight - 2), Scaled(18));
    const int chipTop = client.top + (std::max)(0, barHeight - chipHeight) / 2;
    const int chipBottom = chipTop + chipHeight;
    const int chipRadius = chipHeight / 2;
    int x = client.left + Scaled(UiMetrics::kSpaceXs);

    // --- Filters disclosure ----------------------------------------------
    // ui-v3 search a11y: the disclosure is a real owner-draw BUTTON placed by
    // Reposition over this spot; painting here only reserves its span so the
    // chips/status flow around the placed control.
    {
        const int toggleLeft = client.left + Scaled(kFiltersToggleInsetDip);
        const int toggleRight =
            (std::min)(static_cast<int>(client.right), toggleLeft + Scaled(kFiltersToggleWidthDip));
        x = toggleRight + Scaled(kFiltersToggleGapDip);
    }

    // --- Active-filter chips (Info pills; click clears the filter) -------
    const auto drawChip = [&](RECT rect, const std::wstring& label, bool dismissible, bool hovered,
                              COLORREF fill, COLORREF fg) {
        if (rect.right <= rect.left) return;
        UiGdiRoundedRect(dc, rect, hovered ? CompositeOver(theme.hoverOverlay, fill) : fill, chipRadius);
        SIZE labelSize{};
        SelectObject(dc, captionFont);
        GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()), &labelSize);
        SetTextColor(dc, fg);
        SetBkMode(dc, TRANSPARENT);
        const int closeSlot = dismissible ? Scaled(14) : 0;
        RECT labelRect{rect.left + Scaled(UiMetrics::kSpaceS), rect.top,
                       (std::max)(rect.left, rect.right - closeSlot - Scaled(UiMetrics::kSpaceXxs)),
                       rect.bottom};
        DrawTextW(dc, label.c_str(), static_cast<int>(label.size()), &labelRect,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        if (dismissible) {
            const POINT center{rect.right - Scaled(7), (rect.top + rect.bottom) / 2};
            DrawCloseGlyph(dc, center, Scaled(2.5f), fg, 1);
        }
    };
    const COLORREF chipFill = highContrast ? GetSysColor(COLOR_BTNFACE)
                                           : CompositeOver(theme.selectionSoft, background);
    const COLORREF chipFg = highContrast ? GetSysColor(COLOR_WINDOWTEXT) : ffui::ToColorRef(theme.info);
    if (scopeChipActive_ && !scopeChipText_.empty()) {
        SIZE labelSize{};
        SelectObject(dc, captionFont);
        GetTextExtentPoint32W(dc, scopeChipText_.c_str(), static_cast<int>(scopeChipText_.size()), &labelSize);
        const int width = Scaled(UiMetrics::kSpaceS) * 2 + Scaled(14) + static_cast<int>(labelSize.cx);
        scopeChipRect_ = RECT{x, chipTop, (std::min)(static_cast<int>(client.right), x + width), chipBottom};
        drawChip(scopeChipRect_, scopeChipText_, /*dismissible=*/true, hoveredChip_ == kChipScope,
                 chipFill, chipFg);
        x = scopeChipRect_.right + Scaled(UiMetrics::kSpaceXs);
    } else {
        scopeChipRect_ = RECT{};
    }
    if (sortChipActive_ && !sortChipText_.empty()) {
        SIZE labelSize{};
        SelectObject(dc, captionFont);
        GetTextExtentPoint32W(dc, sortChipText_.c_str(), static_cast<int>(sortChipText_.size()), &labelSize);
        const int width = Scaled(UiMetrics::kSpaceS) * 2 + Scaled(14) + static_cast<int>(labelSize.cx);
        sortChipRect_ = RECT{x, chipTop, (std::min)(static_cast<int>(client.right), x + width), chipBottom};
        drawChip(sortChipRect_, sortChipText_, /*dismissible=*/true, hoveredChip_ == kChipSort,
                 chipFill, chipFg);
        x = sortChipRect_.right + Scaled(UiMetrics::kSpaceXs);
    } else {
        sortChipRect_ = RECT{};
    }
    if (!engineActive_) {
        // ui-v3 search: degraded engine gets its own distinct chip (paired with
        // the status wording so meaning never rides on color alone).
        const wchar_t* label = L"Browsed snapshot";
        SIZE labelSize{};
        SelectObject(dc, captionFont);
        GetTextExtentPoint32W(dc, label, static_cast<int>(wcslen(label)), &labelSize);
        const int width = Scaled(UiMetrics::kSpaceS) * 2 + static_cast<int>(labelSize.cx);
        RECT degradedRect{x, chipTop, (std::min)(static_cast<int>(client.right), x + width), chipBottom};
        drawChip(degradedRect, label, false, false,
                 highContrast ? GetSysColor(COLOR_BTNFACE) : ffui::ToColorRef(theme.badgeDegradedBg),
                 highContrast ? GetSysColor(COLOR_WINDOWTEXT) : ffui::ToColorRef(theme.badgeDegradedText));
        x = degradedRect.right + Scaled(UiMetrics::kSpaceXs);
    }

    // --- Live status line (right-aligned in the remaining space) ---------
    if (x < client.right - Scaled(UiMetrics::kSpaceS)) {
        COLORREF statusColor = textMuted;
        if (!statusWarning_.empty()) {
            statusColor = highContrast ? GetSysColor(COLOR_WINDOWTEXT) : ffui::ToColorRef(theme.warning);
        } else if (statusKind_ == StatusKind::Searching) {
            statusColor = highContrast ? GetSysColor(COLOR_WINDOWTEXT) : ffui::ToColorRef(theme.accent);
        } else if (statusKind_ == StatusKind::NoResults) {
            statusColor = textMuted;
        }
        SetTextColor(dc, statusColor);
        SetBkMode(dc, TRANSPARENT);
        RECT statusRect{x + Scaled(UiMetrics::kSpaceXs), client.top,
                        client.right - Scaled(UiMetrics::kSpaceXs), client.bottom - Scaled(3)};
        DrawTextW(dc, statusText_.c_str(), static_cast<int>(statusText_.size()), &statusRect,
                  DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    // --- Searching progress cue ------------------------------------------
    if (statusKind_ == StatusKind::Searching && client.right > client.left) {
        const RECT bandRect{client.left, (std::max)(client.top, client.bottom - Scaled(3)),
                            client.right, client.bottom};
        UiGdiRoundedRect(dc, bandRect,
                         highContrast ? GetSysColor(COLOR_BTNFACE) : CompositeOver(theme.dividerSubtle, background),
                         Scaled(1.5f));
        const float travel = static_cast<float>(bandRect.right - bandRect.left) * 0.75f;
        const int bandLeft = bandRect.left + static_cast<int>(travel * progressPhase_);
        RECT indicator{bandLeft, bandRect.top,
                       (std::min)(static_cast<int>(bandRect.right), bandLeft + Scaled(36)), bandRect.bottom};
        if (indicator.right > indicator.left) {
            UiGdiRoundedRect(dc, indicator,
                             highContrast ? GetSysColor(COLOR_HIGHLIGHT) : ffui::ToColorRef(theme.accent),
                             Scaled(1.5f));
        }
    }
    EndPaint(hwnd, &paint);
}

void SearchPanel::PaintClearQueryButton(HWND hwnd) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    if (!dc) return;

    // ui-v3 search: circular clear affordance matching the field's token set;
    // hover lifts the ring to focusStroke so the target reads as interactive.
    const ffui::UiTheme theme = ffui::GetUiTheme(ffui::gUiDarkTheme);
    const bool highContrast = ffui::UiSystemHighContrast();
    RECT client{};
    GetClientRect(hwnd, &client);
    HBRUSH backgroundBrush = CreateSolidBrush(highContrast ? GetSysColor(COLOR_WINDOW)
                                                           : ffui::ToColorRef(theme.background));
    FillRect(dc, &client, backgroundBrush);
    DeleteObject(backgroundBrush);

    const COLORREF ringColor = clearQueryHot_
        ? (highContrast ? GetSysColor(COLOR_WINDOWTEXT) : ffui::ToColorRef(theme.focusStroke))
        : (highContrast ? GetSysColor(COLOR_GRAYTEXT) : ffui::ToColorRef(theme.searchBorder));
    HPEN pen = CreatePen(PS_SOLID, 1, ringColor);
    if (pen != nullptr) {
        const RECT ellipse{client.left + 1, client.top + 1, client.right - 1, client.bottom - 1};
        HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
        HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
        Ellipse(dc, ellipse.left, ellipse.top, ellipse.right, ellipse.bottom);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }
    const POINT center{(client.left + client.right) / 2, (client.top + client.bottom) / 2};
    DrawCloseGlyph(dc, center, Scaled(4),
                   highContrast ? GetSysColor(COLOR_WINDOWTEXT) : ffui::ToColorRef(theme.searchText),
                   clearQueryHot_ ? 2 : 1);
    EndPaint(hwnd, &paint);
}

bool SearchPanel::ShouldPaintListEmptyState() const {
    return visible_ && list_ != nullptr && results_.empty() && query_ != nullptr &&
           GetWindowTextLengthW(query_) > 0;
}

void SearchPanel::PaintListEmptyState(HWND hwnd) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    if (!dc) return;

    const ffui::UiTheme theme = ffui::GetUiTheme(ffui::gUiDarkTheme);
    const bool highContrast = ffui::UiSystemHighContrast();
    RECT client{};
    GetClientRect(hwnd, &client);
    HBRUSH backgroundBrush = CreateSolidBrush(highContrast ? GetSysColor(COLOR_WINDOW)
                                                           : ffui::ToColorRef(theme.background));
    FillRect(dc, &client, backgroundBrush);
    DeleteObject(backgroundBrush);
    EnsureResultFonts();

    // ui-v3 search: deliberate states, never blank — searching shows what is
    // being searched; no-results explains scope/filters and offers recovery.
    const wchar_t* title = statusKind_ == StatusKind::Searching ? L"Searching\u2026" : L"No matches";
    std::wstring body;
    if (statusKind_ == StatusKind::Searching) {
        body = L"Looking in ";
        body += ScopeDisplayName();
        body += L"\u2026";
    } else {
        body = L"No files match";
        if (!currentPrimaryTerm_.empty()) {
            body += L" \u201C";
            body += currentPrimaryTerm_;
            body += L"\u201D";
        }
        body += L" in ";
        body += ScopeDisplayName();
        body += L".";
    }
    if (!engineActive_) {
        body += L" The index service is offline \u2014 results come from browsed folders.";
    } else if (!persistentCoverage_) {
        body += L" Only browsed folders are available.";
    }
    if (!statusWarning_.empty()) {
        body += L" ";
        body += statusWarning_;
    }
    body += L" Try a shorter name or widen the scope under Filters.";

    HFONT fallbackFont = reinterpret_cast<HFONT>(SendMessageW(list_, WM_GETFONT, 0, 0));
    if (fallbackFont == nullptr) fallbackFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HFONT titleFont = resultMatchFont_ != nullptr ? resultMatchFont_ : fallbackFont;
    HFONT bodyFont = resultCaptionFont_ != nullptr ? resultCaptionFont_ : fallbackFont;

    const COLORREF titleColor = highContrast ? GetSysColor(COLOR_WINDOWTEXT) : ffui::ToColorRef(theme.text);
    const COLORREF bodyColor = highContrast ? GetSysColor(COLOR_GRAYTEXT) : ffui::ToColorRef(theme.textSecondary);
    const int blockWidth = (std::min)(static_cast<int>(client.right - client.left) - Scaled(UiMetrics::kSpaceL),
                                      Scaled(520));

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, titleColor);
    RECT titleRect{0, 0, blockWidth, Scaled(56)};
    SelectObject(dc, titleFont);
    DrawTextW(dc, title, -1, &titleRect, DT_CALCRECT | DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    SetTextColor(dc, bodyColor);
    RECT bodyRect{0, 0, blockWidth, 0};
    SelectObject(dc, bodyFont);
    DrawTextW(dc, body.c_str(), static_cast<int>(body.size()), &bodyRect,
              DT_CALCRECT | DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);

    const int blockHeight = (titleRect.bottom - titleRect.top) + Scaled(UiMetrics::kSpaceS) +
                            (bodyRect.bottom - bodyRect.top);
    int y = client.top + (std::max)(0, static_cast<int>(client.bottom - client.top) - blockHeight) / 2;
    const int x = client.left + (std::max)(0, static_cast<int>(client.right - client.left) - blockWidth) / 2;

    RECT titleOut{x, y, x + blockWidth, y + (titleRect.bottom - titleRect.top)};
    SelectObject(dc, titleFont);
    SetTextColor(dc, titleColor);
    DrawTextW(dc, title, -1, &titleOut, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    y += (titleRect.bottom - titleRect.top) + Scaled(UiMetrics::kSpaceS);
    RECT bodyOut{x, y, x + blockWidth, y + (bodyRect.bottom - bodyRect.top)};
    SelectObject(dc, bodyFont);
    SetTextColor(dc, bodyColor);
    DrawTextW(dc, body.c_str(), static_cast<int>(body.size()), &bodyOut,
              DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    EndPaint(hwnd, &paint);
}

// --- Filter-bar interaction (presentation-level hover/click routing) -------

void SearchPanel::HandleFilterBarMouseMove(LPARAM lParam) {
    const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    int chip = kChipNone;
    if (scopeChipActive_ && PtInRect(&scopeChipRect_, point)) chip = kChipScope;
    else if (sortChipActive_ && PtInRect(&sortChipRect_, point)) chip = kChipSort;
    if (chip != hoveredChip_) {
        hoveredChip_ = chip;
        if (filterBar_) InvalidateRect(filterBar_, nullptr, FALSE);
    }
}

void SearchPanel::HandleFilterBarMouseLeave() {
    if (hoveredChip_ != kChipNone) {
        hoveredChip_ = kChipNone;
        if (filterBar_) InvalidateRect(filterBar_, nullptr, FALSE);
    }
}

void SearchPanel::HandleFilterBarClick(LPARAM lParam) {
    const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    // The Filters disclosure is a real child button; expansion is driven from
    // its BN_CLICKED path, not by hit-testing the bar.
    // ui-v3 search: clicking a chip clears that filter through the exact same
    // control/model path the combo itself uses, then re-runs the query.
    if (scopeChipActive_ && PtInRect(&scopeChipRect_, point)) {
        if (scope_ != nullptr) {
            SendMessageW(scope_, CB_SETCURSEL, 3, 0);
            UpdateFilterSummary();
            Dispatch(false);
        }
        return;
    }
    if (sortChipActive_ && PtInRect(&sortChipRect_, point)) {
        if (sort_ != nullptr) {
            SendMessageW(sort_, CB_SETCURSEL, 0, 0);
            descending_ = false;
            if (sortDirection_ != nullptr) SetWindowTextW(sortDirection_, L"Ascending");
            UpdateFilterSummary();
            Dispatch(false);
        }
        return;
    }
}

void SearchPanel::ToggleFiltersExpanded() {
    filtersExpanded_ = !filtersExpanded_;
    if (filterBar_) InvalidateRect(filterBar_, nullptr, FALSE);
    if (visible_) Reposition(lastSurfaceRect_);
    // ui-v3 search a11y/query-first: after a disclosure toggle (mouse or UIA
    // invoke), typing continues in the query field -- the panel's primary
    // surface -- instead of stranding focus on the toggle button.
    if (visible_ && queryEdit_ != nullptr) SetFocus(queryEdit_);
}

void SearchPanel::SetButtonHot(HWND source, bool hot) {
    bool* flag = source == filtersToggle_ ? &filtersHot_
               : source == sortDirection_ ? &directionHot_
               : source == clearHistory_ ? &clearHistoryHot_
                                         : nullptr;
    if (flag != nullptr && *flag != hot) {
        *flag = hot;
        InvalidateRect(source, nullptr, FALSE);
    }
}

// ui-v3 search: clears the query through the edit control (so selection,
// undo state, and the history combo stay consistent), cancels any pending
// debounce tick, and re-dispatches the now-empty query.
void SearchPanel::ClearQueryText() {
    if (queryEdit_ == nullptr) return;
    updatingHistory_ = true;
    SetWindowTextW(queryEdit_, L"");
    updatingHistory_ = false;
    KillTimer(owner_, kDebounceTimer);
    clearQueryHot_ = false;
    UpdateClearAffordance();
    InvalidateRect(query_, nullptr, FALSE);
    SetFocus(queryEdit_);
    Dispatch(false);
}

// ui-v3 search: row hover tracking on the native list. Presentation only —
// the ListView keeps sole ownership of selection and keyboard focus.
void SearchPanel::HandleListMouseMove(LPARAM lParam) {
    if (list_ == nullptr || results_.empty()) return;
    LVHITTESTINFO hit{};
    hit.pt = POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    const int item = ListView_HitTest(list_, &hit);
    if (item != hoveredResultRow_) {
        RECT previous{};
        if (hoveredResultRow_ >= 0 &&
            ListView_GetItemRect(list_, hoveredResultRow_, &previous, LVIR_BOUNDS)) {
            InvalidateRect(list_, &previous, FALSE);
        }
        hoveredResultRow_ = item >= 0 && static_cast<size_t>(item) < results_.size() ? item : -1;
        RECT current{};
        if (hoveredResultRow_ >= 0 &&
            ListView_GetItemRect(list_, hoveredResultRow_, &current, LVIR_BOUNDS)) {
            InvalidateRect(list_, &current, FALSE);
        }
    }
}

void SearchPanel::HandleListMouseLeave() {
    if (hoveredResultRow_ < 0) return;
    RECT previous{};
    if (list_ != nullptr && ListView_GetItemRect(list_, hoveredResultRow_, &previous, LVIR_BOUNDS)) {
        InvalidateRect(list_, &previous, FALSE);
    }
    hoveredResultRow_ = -1;
}

// ui-v3 search: DPI-aware fonts for result rows and the filter bar; recreated
// only when the system DPI scale changes.
void SearchPanel::EnsureResultFonts() {
    const float scale = ffui::UiDpiScale();
    if (resultNameFont_ != nullptr && scale == resultFontScale_) return;
    if (resultNameFont_) DeleteObject(resultNameFont_);
    if (resultMatchFont_) DeleteObject(resultMatchFont_);
    if (resultCaptionFont_) DeleteObject(resultCaptionFont_);
    resultFontScale_ = scale;
    resultNameFont_ = CreateFontW(-Scaled(UiTypeSize(UiTypeRole::Body)), 0, 0, 0, FW_NORMAL,
                                  FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    resultMatchFont_ = CreateFontW(-Scaled(UiTypeSize(UiTypeRole::Body)), 0, 0, 0, FW_SEMIBOLD,
                                   FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                   CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    resultCaptionFont_ = CreateFontW(-Scaled(UiTypeSize(UiTypeRole::Caption)), 0, 0, 0, FW_NORMAL,
                                     FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                     CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

// ui-v3 search: two-line rows need explicit metrics; a report-mode ListView
// derives its row height from the small image list, so a one-entry image list
// at the row height sets it without touching virtualization.
void SearchPanel::EnsureRowMetrics() {
    const int desired = Scaled(kResultRowHeightDip);
    if (resultListImages_ != nullptr && desired == resultListRowHeightPx_) return;
    ReleaseListImageList();
    if (desired <= 0) return;
    resultListImages_ = ImageList_Create((std::max)(1, Scaled(static_cast<int>(UiMetrics::kIconSize))),
                                         desired, ILC_COLOR32, 1, 1);
    if (resultListImages_ != nullptr && list_ != nullptr) {
        ListView_SetImageList(list_, resultListImages_, LVSIL_SMALL);
        resultListRowHeightPx_ = desired;
    } else {
        ReleaseListImageList();
    }
}

void SearchPanel::ReleaseListImageList() {
    if (resultListImages_ != nullptr) {
        HIMAGELIST images = resultListImages_;
        resultListImages_ = nullptr; // detach before destroy (list may still be alive)
        ImageList_Destroy(images);
    }
    resultListRowHeightPx_ = 0;
}

// ui-v3 search: cached chip model — rebuilt only when a scope/sort/direction/
// engine input changes, never per paint.
void SearchPanel::UpdateFilterSummary() {
    const int scopeSel = scope_ != nullptr ? static_cast<int>(SendMessageW(scope_, CB_GETCURSEL, 0, 0)) : -1;
    const int sortSel = sort_ != nullptr ? static_cast<int>(SendMessageW(sort_, CB_GETCURSEL, 0, 0)) : 0;
    scopeChipActive_ = scopeSel >= 0 && scopeSel != 3; // 3 = All Locations (default)
    scopeChipText_ = scopeChipActive_ ? std::wstring(L"Scope: ") + ScopeShortName(scopeSel) : std::wstring();
    sortChipActive_ = sortSel > 0 || descending_; // Relevance ascending is the default
    if (sortChipActive_) {
        sortChipText_ = std::wstring(L"Sort: ") + SortFieldName(sortSel);
        sortChipText_ += descending_ ? L" \u2193" : L" \u2191";
    } else {
        sortChipText_.clear();
    }
    if (filterBar_ != nullptr) InvalidateRect(filterBar_, nullptr, FALSE);
}

void SearchPanel::UpdateClearAffordance() {
    if (clearQuery_ == nullptr || query_ == nullptr) return;
    const bool show = visible_ && GetWindowTextLengthW(query_) > 0;
    ShowWindow(clearQuery_, show ? SW_SHOWNOACTIVATE : SW_HIDE);
}

std::wstring SearchPanel::ScopeDisplayName() const {
    if (scope_ == nullptr) return L"All Locations";
    const int selection = static_cast<int>(SendMessageW(scope_, CB_GETCURSEL, 0, 0));
    return ScopeShortName(selection);
}

void SearchPanel::OnSnapshotUpdated() {
    // Invalidate an in-flight query as well as its materialized candidates.
    // Otherwise a worker that started against the previous mapping can post a
    // result after the new generation is already visible in the browser.
    ++generation_;
    currentGeneration_ = generation_;
    candidateCacheEpoch_.fetch_add(1, std::memory_order_acq_rel);
    {
        std::lock_guard<std::mutex> lock(candidateCacheMutex_);
        candidateCache_.reset();
    }
    bool hasPersistentCoverage = false;
    if (engine_ != nullptr) {
        const auto snapshot = engine_->ReadSnapshot();
        if (snapshot) {
            for (const auto& [path, directory] : *snapshot) {
                if (directory.volumeRowId != 0) {
                    hasPersistentCoverage = true;
                    break;
                }
            }
        }
    }
    if (persistentCoverage_ != hasPersistentCoverage) {
        persistentCoverage_ = hasPersistentCoverage;
        RefreshScopeLabels();
    }
    // A visible search should follow the new snapshot generation without
    // requiring the user to type another character — but generations can
    // publish every few seconds during active indexing, so re-dispatches are
    // coalesced: within the refresh interval only a pending flag is set and
    // the refresh happens on the next debounce/visibility change. Suppress
    // history writes: this is a data refresh, not a new query execution.
    if (visible_ && query_ != nullptr && GetWindowTextLengthW(query_) > 0) {
        const auto now = std::chrono::steady_clock::now();
        if (now - lastSnapshotRefresh_ >= std::chrono::milliseconds(kSnapshotRefreshIntervalMs)) {
            Dispatch(false);
        } else {
            snapshotRefreshPending_ = true;
        }
    }
}

void SearchPanel::RefreshScopeLabels() {
    if (scope_ == nullptr) return;
    const int selected = static_cast<int>(SendMessageW(scope_, CB_GETCURSEL, 0, 0));
    SendMessageW(scope_, CB_RESETCONTENT, 0, 0);
    if (persistentCoverage_) {
        for (const wchar_t* text : {L"Current Folder — Available Data",
                                    L"Current Folder + Indexed Subfolders",
                                    L"Current Drive — Indexed Coverage",
                                    L"All Indexed Locations"})
            SendMessageW(scope_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    } else {
        for (const wchar_t* text : {L"Current Folder", L"Current Folder + Browsed Subfolders",
                                    L"Current Drive — Browsed Folders Only", L"All Browsed Locations"})
            SendMessageW(scope_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    }
    const int count = static_cast<int>(SendMessageW(scope_, CB_GETCOUNT, 0, 0));
    const int next = selected >= 0 && selected < count ? selected : 3;
    SendMessageW(scope_, CB_SETCURSEL, next, 0);
    InvalidateRect(scope_, nullptr, TRUE);
    UpdateFilterSummary(); // ui-v3 search: chip labels follow the relabelled scope set
}

void SearchPanel::PopulateCandidates(std::vector<ffsearch::Candidate>& candidates) {
    {
        std::lock_guard<std::mutex> lock(candidateCacheMutex_);
        if (candidateCache_) {
            candidates = *candidateCache_;
            return;
        }
    }

    const uint64_t cacheEpoch = candidateCacheEpoch_.load(std::memory_order_acquire);
    std::vector<ffsearch::Candidate> built;
    const auto snapshot = engine_->ReadSnapshot();
    if (!snapshot) return;
    std::unordered_map<std::wstring, uint64_t> directoryIds;
    uint64_t nextId = 1;
    for (const auto& [path, directory] : *snapshot) {
        if (directory.status == ffprotocol::DirectoryEnumerationStatus::Success)
            directoryIds.emplace(path, directory.directoryIdLow != 0 ? directory.directoryIdLow : nextId++);
    }
    std::unordered_map<std::wstring, size_t> directoryIndexes;
    for (const auto& [path, id] : directoryIds) {
        const std::wstring parent = ffsearch::ParentPath(path);
        const auto parentId = directoryIds.find(parent);
        const auto& directory = snapshot->at(path);
            built.push_back({ffsearch::LeafName(path), parent, 0, 0, true, 0,
                              directory.volumeRowId != 0 ? directory.volumeRowId : ffsearch::VolumeId(path), id,
                              directory.parentIdLow != 0 ? directory.parentIdLow : (parentId == directoryIds.end() ? id : parentId->second),
                              directory.volumeRowId != 0});
        directoryIndexes.emplace(path, built.size() - 1);
    }
    for (const auto& [path, directory] : *snapshot) {
        if (directory.status != ffprotocol::DirectoryEnumerationStatus::Success) continue;
        const uint64_t parentId = directoryIds.contains(path) ? directoryIds.at(path) : 0;
        for (const auto& entry : directory.entries) {
            const std::wstring fullPath = JoinPath(path, entry.name);
            if (entry.isDirectory && directoryIndexes.contains(fullPath)) {
                auto& candidate = built[directoryIndexes.at(fullPath)];
                candidate.sizeBytes = entry.sizeBytes;
                candidate.createdTime = entry.creationTime;
                candidate.modifiedTime = entry.lastModifiedTime;
                // indexed-search-allocated-size D4: propagate the snapshot entry's
                // explicit allocated-state verbatim; degraded enumeration never sets
                // it, so this stays unknown rather than cloning logical size.
                candidate.allocatedSizeBytes = entry.allocatedSizeBytes;
                candidate.allocatedSizeKnown = entry.allocatedSizeKnown;
                continue;
            }
            const uint64_t id = entry.isDirectory && directoryIds.contains(fullPath) ? directoryIds.at(fullPath) : nextId++;
            built.push_back({entry.name, path, entry.sizeBytes, entry.lastModifiedTime, entry.isDirectory,
                                  entry.creationTime, entry.volumeRowId != 0 ? entry.volumeRowId : ffsearch::VolumeId(path),
                                  entry.fileIdLow != 0 ? entry.fileIdLow : id,
                                  entry.parentIdLow != 0 ? entry.parentIdLow : parentId,
                                  directory.volumeRowId != 0 || entry.volumeRowId != 0,
                                  // indexed-search-allocated-size D4: verbatim propagation of the
                                  // snapshot entry's allocated state (unknown in degraded mode).
                                  entry.allocatedSizeBytes, entry.allocatedSizeKnown});
        }
    }

    std::shared_ptr<const std::vector<ffsearch::Candidate>> cache;
    {
        std::lock_guard<std::mutex> lock(candidateCacheMutex_);
        if (candidateCacheEpoch_.load(std::memory_order_acquire) == cacheEpoch) {
            candidateCache_ = std::make_shared<const std::vector<ffsearch::Candidate>>(std::move(built));
            cache = candidateCache_;
        } else if (candidateCache_) {
            cache = candidateCache_;
        }
    }
    if (cache) {
        candidates = *cache;
    } else {
        // A generation changed while this search was being materialized. Keep
        // the current query useful, but do not retain that stale projection.
        candidates = std::move(built);
    }
}

void SearchPanel::SetEngineActive(bool active) {
    const bool lostBroadScope = engineActive_ && !active && SendMessageW(scope_, CB_GETCURSEL, 0, 0) >= 2;
    engineActive_ = active;
    UpdateFilterSummary(); // ui-v3 search: the degraded chip tracks engine state
    if (visible_) UpdateScopeAvailability(lostBroadScope);
}

void SearchPanel::UpdateScopeAvailability(bool showFallbackNotice) {
    // Every advertised scope is backed by the in-process snapshot, including
    // degraded mode. Broad scopes mean "browsed locations" rather than a
    // privileged whole-volume index, so disconnecting the service must not
    // silently rewrite the user's selected scope.
    if (SendMessageW(scope_, CB_GETCURSEL, 0, 0) < 0) {
        SendMessageW(scope_, CB_SETCURSEL, 3, 0);
    }
    if (showFallbackNotice && !engineActive_) {
        UpdateStatus(L"Index service disconnected; searching the browsed snapshot.");
    }
    InvalidateRect(scope_, nullptr, TRUE);
}

void SearchPanel::UpdateStatus(const std::wstring& text, StatusKind kind) {
    statusKind_ = kind;
    // ui-v3 search: the live status line lives in the filter bar now; a
    // searching state also arms the bounded progress-cue timer, which is
    // killed on every terminal state so no idle animation ever runs.
    statusText_ = text;
    if (kind == StatusKind::Searching) {
        progressPhase_ = 0.0f;
        if (owner_) SetTimer(owner_, kProgressTimer, kProgressTimerMilliseconds, nullptr);
    } else {
        KillTimer(owner_, kProgressTimer);
    }
    if (filterBar_) InvalidateRect(filterBar_, nullptr, TRUE);
}

std::filesystem::path SearchPanel::HistoryPath() {
    std::vector<wchar_t> localAppData(32768);
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData.data(), static_cast<DWORD>(localAppData.size()));
    if (length == 0 || length >= localAppData.size()) return {};
    return std::filesystem::path(localAppData.data()) / L"FastFiles" / L"search-history.txt";
}

void SearchPanel::LoadHistory() {
    historyStore_.Load(HistoryPath());
    history_ = historyStore_.Queries();
    for (const auto& query : history_) SendMessageW(query_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(query.c_str()));
}

void SearchPanel::SaveHistory() const {
    historyStore_.Save(HistoryPath());
}

void SearchPanel::RecordHistory(const std::wstring& query) {
    if (!retainHistory_ || query.empty()) return;
    // In-memory record only; the disk flush is coalesced onto a timer so the
    // UI thread never performs a WRITE_THROUGH file write per recorded query.
    historyStore_.RecordQuery(query);
    historyDirty_ = true;
    history_ = historyStore_.Queries();
    updatingHistory_ = true;
    SendMessageW(query_, CB_RESETCONTENT, 0, 0);
    for (const auto& item : history_) SendMessageW(query_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
    SetWindowTextW(query_, query.c_str());
    updatingHistory_ = false;
    SetTimer(owner_, kHistorySaveTimer, kHistorySaveCoalesceMilliseconds, nullptr);
    // quick-access-home 5.6: the recorded query changed the Recent searches set.
    if (historyChanged_) historyChanged_();
}

void SearchPanel::ClearHistory() {
    history_.clear();
    SendMessageW(query_, CB_RESETCONTENT, 0, 0);
    historyStore_.Clear(HistoryPath());
    UpdateStatus(L"Search history cleared.");
    // quick-access-home 5.6: the Recent searches set is now empty.
    if (historyChanged_) historyChanged_();
}

std::vector<std::wstring> SearchPanel::RecentQueries(size_t maxCount) const {
    const auto& queries = historyStore_.Queries();
    if (queries.size() <= maxCount) return queries;
    return {queries.begin(), queries.begin() + static_cast<std::ptrdiff_t>(maxCount)};
}

void SearchPanel::SetQueryAndDispatch(const std::wstring& query) {
    // Same guard as RecordHistory: the programmatic set raises CBN_EDITCHANGE,
    // which would otherwise arm the debounce timer and run the query twice.
    updatingHistory_ = true;
    SetWindowTextW(query_, query.c_str());
    updatingHistory_ = false;
    Dispatch(true);
}

void SearchPanel::SetHistoryChangedCallback(std::function<void()> cb) {
    historyChanged_ = std::move(cb);
}

// ---------------------------------------------------------------------------
// semantic-uia-core-surfaces 4.x: UiaSurface implementation for the search
// result list. Queries project the real result model and native list state;
// actions route into the exact native list / navigation logic user input
// uses (design D15).
// ---------------------------------------------------------------------------

size_t SearchPanel::UiaSearchResultCount() const {
    return results_.size();
}

UiaSearchResultInfo SearchPanel::UiaSearchResultInfoOf(size_t index) const {
    UiaSearchResultInfo info;
    if (index >= results_.size()) return info;
    const ffsearch::Candidate& candidate = results_[index].candidate;
    info.name = candidate.name;
    info.folder = candidate.folder;
    info.sizeBytes = candidate.sizeBytes;
    info.modifiedTime = candidate.modifiedTime;
    info.isDirectory = candidate.isDirectory;
    info.volumeId = candidate.volumeId;
    info.id = candidate.id;
    // indexed-search-allocated-size D9: project on-disk size + knownness from the
    // exact rendered result so the accessible description agrees with the row.
    info.allocatedSizeBytes = candidate.allocatedSizeBytes;
    info.allocatedSizeKnown = candidate.allocatedSizeKnown;
    info.fullPath = (std::filesystem::path(candidate.folder) / candidate.name).wstring();
    if (list_ != nullptr) {
        const int itemState = ListView_GetItemState(list_, static_cast<int>(index),
                                                    LVIS_SELECTED | LVIS_FOCUSED);
        info.selected = (itemState & LVIS_SELECTED) != 0;
        info.focused = (itemState & LVIS_FOCUSED) != 0;
    }
    return info;
}

bool SearchPanel::UiaSearchResultRect(size_t index, RECT& outScreen) const {
    if (list_ == nullptr || index >= results_.size()) return false;
    RECT item{};
    if (!ListView_GetItemRect(list_, static_cast<int>(index), &item, LVIR_BOUNDS)) return false;
    outScreen = item;
    ClientToScreen(list_, reinterpret_cast<POINT*>(&outScreen.left));
    ClientToScreen(list_, reinterpret_cast<POINT*>(&outScreen.right));
    return true;
}

bool SearchPanel::UiaSearchResultOffscreen(size_t index) const {
    RECT rect{};
    if (!UiaSearchResultRect(index, rect)) return true;
    RECT client{};
    GetClientRect(list_, &client);
    POINT origin{0, 0};
    ClientToScreen(list_, &origin);
    client.left += origin.x;
    client.right += origin.x;
    client.top += origin.y;
    client.bottom += origin.y;
    return rect.bottom <= client.top || rect.top >= client.bottom ||
           rect.right <= client.left || rect.left >= client.right;
}

bool SearchPanel::UiaSearchVisible() const {
    return visible_;
}

bool SearchPanel::UiaSearchHasKeyboardFocus() const {
    return list_ != nullptr && GetFocus() == list_;
}

HWND SearchPanel::UiaSearchHwnd() const {
    return list_;
}

void SearchPanel::UiaSearchSelectItem(size_t index) {
    if (list_ == nullptr || index >= results_.size()) return;
    // The native single-select list: selection implies the focus item.
    ListView_SetItemState(list_, static_cast<int>(index), LVIS_SELECTED, LVIS_SELECTED);
    ListView_SetItemState(list_, static_cast<int>(index), LVIS_FOCUSED, LVIS_FOCUSED);
}

void SearchPanel::UiaSearchAddToSelection(size_t index) {
    if (list_ == nullptr || index >= results_.size()) return;
    if ((ListView_GetItemState(list_, static_cast<int>(index), LVIS_SELECTED) & LVIS_SELECTED) == 0) {
        UiaSearchSelectItem(index);
    }
}

void SearchPanel::UiaSearchRemoveFromSelection(size_t index) {
    if (list_ == nullptr || index >= results_.size()) return;
    ListView_SetItemState(list_, static_cast<int>(index), 0, LVIS_SELECTED);
}

void SearchPanel::UiaSearchSetFocusItem(size_t index) {
    if (list_ == nullptr || index >= results_.size()) return;
    UiaSearchSelectItem(index);
    SetFocus(list_);
}

void SearchPanel::UiaSearchActivateItem(size_t index) {
    if (index >= results_.size()) return;
    // stabilize-live-uia-e2e: provider calls can land on a UIAutomationCore
    // worker thread; UI state (workspace, columns, chrome tab rebuild) must
    // be mutated on the UI thread. See SearchActivationNeedsUiThreadMarshal.
    if (SearchActivationNeedsUiThreadMarshal(GetCurrentThreadId(),
                                             GetWindowThreadProcessId(list_, nullptr))) {
        if (PostMessageW(owner_, WM_APP_SEARCH_RESULT_ACTIVATE,
                         static_cast<WPARAM>(index), 0)) {
            return; // WindowShell runs ActivateResultItem on the UI thread.
        }
        // PostMessage failure (owner gone): fall through to the inline path
        // rather than dropping the activation.
    }
    ActivateResultItem(index);
}

void SearchPanel::ActivateResultItem(size_t index) {
    if (index >= results_.size()) return;
    // The exact search-to-navigation path used by NM_DBLCLK / LVN_ITEMACTIVATE.
    const auto candidate = results_[index].candidate;
    const auto found = std::find_if(candidates_.begin(), candidates_.end(), [&](const ffsearch::Candidate& entry) {
        return entry.volumeId == candidate.volumeId && entry.id == candidate.id;
    });
    const auto reconstruction = found == candidates_.end()
        ? ffsearch::PathReconstruction{}
        : ffsearch::ReconstructPath(candidates_, static_cast<size_t>(found - candidates_.begin()));
    Hide();
    navigate_(candidate, reconstruction);
}

void SearchPanel::UiaSearchScrollIntoView(size_t index) {
    if (list_ == nullptr || index >= results_.size()) return;
    ListView_EnsureVisible(list_, static_cast<int>(index), FALSE);
}

void SearchPanel::SetUiaEventSink(UiaEventSink sink, void* context) {
    uiaEvents_.Set(sink, context);
}

Microsoft::WRL::ComPtr<IRawElementProviderFragmentRoot> SearchPanel::UiaRootProvider(HWND listHwnd) {
    if (uiaProvider_ == nullptr) {
        if (!uiaState_) {
            uiaState_ = std::make_shared<UiaOwnerState>();
        }
        auto provider = Microsoft::WRL::ComPtr<SearchUiaProvider>(new SearchUiaProvider(uiaState_, this, listHwnd));
        uiaProvider_ = provider;
        uiaEvents_.Set(&SearchUiaProvider::SinkThunk, provider.Get());
    }
    return uiaProvider_;
}

void SearchPanel::ReleaseUiaProvider() {
    uiaEvents_.Clear();
    if (uiaState_) {
        uiaState_->alive = false;
    }
    uiaState_.reset();
    uiaProvider_.Reset();
}

void SearchPanel::NotifyUia(const UiaEventNotification& notification) {
    uiaEvents_.Notify(notification);
}

// ui-v3 search a11y hardening: see SearchButtonInvokeProvider above. The
// provider shares the panel's owner-lifetime token so it dies with the panel.
Microsoft::WRL::ComPtr<IRawElementProviderSimple> SearchPanel::UiaButtonFixupProvider(HWND button) {
    if (!uiaState_) uiaState_ = std::make_shared<UiaOwnerState>();
    return Microsoft::WRL::ComPtr<IRawElementProviderSimple>(new SearchButtonInvokeProvider(uiaState_, button));
}

// ui-v3 search a11y hardening: explicit Tab traversal. Maps the focused HWND
// onto the headless ffui::SearchTabOrder seam and applies the neighbor. No-op
// when the panel is hidden; expanded-row targets can only be produced while
// the disclosure is open (the seam clamps stale targets after collapse).
void SearchPanel::CycleSearchFocus(HWND from, bool forward) {
    if (!visible_) return;
    const ffui::SearchFocusTarget current = [&]() {
        if (from == queryEdit_ || from == query_) return ffui::SearchFocusTarget::QueryEdit;
        if (from == filtersToggle_) return ffui::SearchFocusTarget::FiltersToggle;
        if (from == scope_) return ffui::SearchFocusTarget::ScopeCombo;
        if (from == sort_) return ffui::SearchFocusTarget::SortCombo;
        if (from == sortDirection_) return ffui::SearchFocusTarget::SortDirection;
        if (from == clearHistory_) return ffui::SearchFocusTarget::ClearHistory;
        if (from == list_) return ffui::SearchFocusTarget::ResultList;
        return ffui::SearchFocusTarget::None;
    }();
    const ffui::SearchFocusTarget next = forward ? ffui::NextSearchFocusTarget(current, filtersExpanded_)
                                                 : ffui::PreviousSearchFocusTarget(current, filtersExpanded_);
    switch (next) {
        case ffui::SearchFocusTarget::QueryEdit:
            if (queryEdit_ != nullptr) SetFocus(queryEdit_);
            break;
        case ffui::SearchFocusTarget::FiltersToggle:
            if (filtersToggle_ != nullptr) SetFocus(filtersToggle_);
            break;
        case ffui::SearchFocusTarget::ScopeCombo:
            if (scope_ != nullptr) SetFocus(scope_);
            break;
        case ffui::SearchFocusTarget::SortCombo:
            if (sort_ != nullptr) SetFocus(sort_);
            break;
        case ffui::SearchFocusTarget::SortDirection:
            if (sortDirection_ != nullptr) SetFocus(sortDirection_);
            break;
        case ffui::SearchFocusTarget::ClearHistory:
            if (clearHistory_ != nullptr) SetFocus(clearHistory_);
            break;
        case ffui::SearchFocusTarget::ResultList:
            if (list_ != nullptr) SetFocus(list_);
            break;
        case ffui::SearchFocusTarget::None:
            break;
    }
}

} // namespace ffui
