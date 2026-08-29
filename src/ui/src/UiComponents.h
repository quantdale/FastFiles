// UI-v3 shared component primitives (docs/ui-improvement/02 §7–8).
//
// Pure, allocation-free render/state helpers for the recurring visual elements
// — selectable rows, nav items, buttons, chips, state panels, progress, focus
// rings, drop targets — so every Direct2D surface resolves interaction states
// through one grammar instead of inventing per-file variants. These are
// deliberately functions, not control classes: callers keep ownership of input,
// hit-testing, and text formats. All helpers are safe inside BeginDraw/Paint,
// consume UiTheme tokens exclusively (High Contrast falls back to system
// colors), and take brushes from a UiBrushCache so steady-state paint creates
// no D2D objects. GDI chrome (owner-draw buttons, dialogs) uses UiGdiRoundedRect.
#pragma once

#include <algorithm>
#include <d2d1_1.h>
#include <dwrite.h>
#include <windows.h>
#include <wrl/client.h>

#include "UITheme.h"
#include "UiStyle.h"

namespace ffui {

// ---------------------------------------------------------------------------
// State resolution
// ---------------------------------------------------------------------------

// Resolved paint colors for any selectable row-like element (file rows, search
// results, nav items, list items in panels). One resolver keeps hover,
// focused, selected-focused, and selected-unfocused visually consistent
// across browse and search surfaces.
struct UiRowPalette {
    D2D1_COLOR_F fill;
    D2D1_COLOR_F stroke;
    D2D1_COLOR_F text;
};

inline UiRowPalette ResolveUiRowPalette(const UiTheme& theme, UiVisualState state, bool highContrast) {
    UiRowPalette p{/*fill=*/D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f),
                   /*stroke=*/D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f), /*text=*/theme.text};
    if (highContrast) {
        // Accessibility fallback: system highlight/menu colors, never token
        // overlays, so the user's contrast scheme always wins.
        switch (state) {
            case UiVisualState::SelectedFocused:
            case UiVisualState::SelectedUnfocused:
                p.fill = ToD2DColor(GetSysColor(COLOR_HIGHLIGHT));
                p.text = ToD2DColor(GetSysColor(COLOR_HIGHLIGHTTEXT));
                break;
            case UiVisualState::Hover:
            case UiVisualState::Pressed:
                p.fill = ToD2DColor(GetSysColor(COLOR_MENUHILIGHT));
                p.text = ToD2DColor(GetSysColor(COLOR_HIGHLIGHTTEXT));
                break;
            case UiVisualState::Disabled:
                p.text = ToD2DColor(GetSysColor(COLOR_GRAYTEXT));
                break;
            default:
                break;
        }
        return p;
    }
    switch (state) {
        case UiVisualState::Hover:
            p.fill = theme.hoverOverlay;
            break;
        case UiVisualState::Pressed:
            p.fill = theme.pressOverlay;
            break;
        case UiVisualState::SelectedFocused:
            p.fill = theme.accent;
            p.text = theme.textOnAccent;
            break;
        case UiVisualState::SelectedUnfocused:
            p.fill = theme.selectionSoft;
            break;
        case UiVisualState::Disabled:
            p.text = theme.textDisabled;
            break;
        case UiVisualState::DropTarget:
            p.fill = theme.selectionSoft;
            p.stroke = theme.dropTargetStroke;
            break;
        case UiVisualState::Busy:
        case UiVisualState::Focused:
        case UiVisualState::Rest:
            break;
    }
    return p;
}

// Small fixed-slot brush cache so composite helpers avoid per-frame brush
// churn without requiring every caller to manage named ComPtrs. Slots are
// recreated only when the requested color changes (theme flip / state change).
struct UiBrushCache {
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fill;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> stroke;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> text;

    void Reset() {
        fill.Reset();
        stroke.Reset();
        text.Reset();
    }
};

// ---------------------------------------------------------------------------
// Primitive painters
// ---------------------------------------------------------------------------

// Paints a row-like element's background/stroke for the resolved state. Text
// remains the caller's job (it owns the text format and string). Radius should
// be UiMetrics::kRadiusSmall for rows.
inline void DrawUiStateFill(ID2D1DeviceContext* ctx, D2D1_RECT_F rect, const UiRowPalette& palette,
                            float radius, UiBrushCache* brushes) {
    if (ctx == nullptr || brushes == nullptr) {
        return;
    }
    if (palette.fill.a > 0.0f && SUCCEEDED(UiEnsureSolidBrush(ctx, palette.fill, &brushes->fill))) {
        UiFillRoundedRect(ctx, rect, brushes->fill.Get(), radius);
    }
    if (palette.stroke.a > 0.0f && SUCCEEDED(UiEnsureSolidBrush(ctx, palette.stroke, &brushes->stroke))) {
        UiDrawRoundedRect(ctx, rect, brushes->stroke.Get(), radius, 1.5f);
    }
}

// Keyboard-focus indicator: an inset rounded outline in focusStroke, drawn
// independently of hover/selection so focus is always visible. The inset keeps
// the ring inside the element bounds (no clipping against neighbors).
inline void DrawUiFocusIndicator(ID2D1DeviceContext* ctx, D2D1_RECT_F rect, const UiTheme& theme,
                                 bool highContrast, float radius, UiBrushCache* brushes) {
    if (ctx == nullptr || brushes == nullptr) {
        return;
    }
    const float inset = UiMetrics::kSpaceXxs + 1.0f;
    const D2D1_RECT_F ring{rect.left + inset, rect.top + inset, rect.right - inset, rect.bottom - inset};
    const D2D1_COLOR_F color =
        highContrast ? ToD2DColor(GetSysColor(COLOR_WINDOWTEXT)) : theme.focusStroke;
    if (SUCCEEDED(UiEnsureSolidBrush(ctx, color, &brushes->stroke))) {
        UiDrawRoundedRect(ctx, ring, brushes->stroke.Get(), radius, 2.0f);
    }
}

// Drop-target affordance: soft accent wash plus a strong dropTargetStroke
// outline — shape + stroke redundancy, not color alone.
inline void DrawUiDropTargetEmphasis(ID2D1DeviceContext* ctx, D2D1_RECT_F rect, const UiTheme& theme,
                                     bool highContrast, float radius, UiBrushCache* brushes) {
    if (ctx == nullptr || brushes == nullptr) {
        return;
    }
    DrawUiStateFill(ctx, rect, ResolveUiRowPalette(theme, UiVisualState::DropTarget, highContrast),
                    radius, brushes);
}

// Draws a nav item (sidebar destinations): optional glyph + label with an
// accent leading bar when `active`. Hover must not look identical to selected:
// hover is a faint overlay while active carries selectionSoft + accent bar.
// Glyph/label formats are vertically centered in the row; the label starts
// after a fixed icon slot so callers align text across items without measuring.
inline void DrawUiNavItem(ID2D1DeviceContext* ctx, D2D1_RECT_F rect, IDWriteTextFormat* format,
                          const wchar_t* label, IDWriteTextFormat* glyphFormat, const wchar_t* glyph,
                          bool active, UiVisualState state, const UiTheme& theme, bool highContrast,
                          UiBrushCache* brushes) {
    if (ctx == nullptr || brushes == nullptr) {
        return;
    }
    const float radius = UiMetrics::kRadiusSmall;
    UiVisualState fillState = state;
    if (active && (fillState == UiVisualState::Rest || fillState == UiVisualState::Focused)) {
        fillState = UiVisualState::SelectedUnfocused;  // active destination treatment
    }
    const UiRowPalette palette = ResolveUiRowPalette(theme, fillState, highContrast);
    DrawUiStateFill(ctx, rect, palette, radius, brushes);

    float x = rect.left + UiMetrics::kSpaceS;
    if (active && !highContrast) {
        // Accent location marker: narrow rounded bar, not a full accent fill.
        const D2D1_RECT_F bar{x, rect.top + UiMetrics::kSpaceXs, x + 3.0f,
                              rect.bottom - UiMetrics::kSpaceXs};
        if (SUCCEEDED(UiEnsureSolidBrush(ctx, theme.accent, &brushes->stroke))) {
            UiFillRoundedRect(ctx, bar, brushes->stroke.Get(), 1.5f);
        }
    }
    x += UiMetrics::kSpaceXs;
    if (glyph != nullptr && glyph[0] != L'\0' && glyphFormat != nullptr) {
        if (SUCCEEDED(UiEnsureSolidBrush(ctx, palette.text.a > 0.0f ? palette.text : theme.textSecondary,
                                         &brushes->text))) {
            // Glyph occupies a fixed square slot so labels align across items.
            const D2D1_RECT_F slot{x, rect.top, x + UiMetrics::kIconSizeLg, rect.bottom};
            UiDrawTextCentered(ctx, glyphFormat, glyph, slot, brushes->text.Get());
        }
    }
    if (label != nullptr && label[0] != L'\0' && format != nullptr) {
        if (SUCCEEDED(UiEnsureSolidBrush(ctx, palette.text, &brushes->text))) {
            UiDrawTextAt(ctx, format, label, x + UiMetrics::kIconSizeLg + UiMetrics::kSpaceXs,
                         rect.top, rect.bottom, brushes->text.Get());
        }
    }
}

// Text button presentation. Primary = the single highest-priority action
// (filled accent); secondary = quiet elevated surface with border. Hover/
// pressed map onto accentHover / press overlays; disabled dims the label but
// keeps it readable.
inline void DrawUiTextButton(ID2D1DeviceContext* ctx, D2D1_RECT_F rect, IDWriteTextFormat* format,
                             const wchar_t* label, bool primary, UiVisualState state,
                             const UiTheme& theme, bool highContrast, UiBrushCache* brushes) {
    if (ctx == nullptr || brushes == nullptr) {
        return;
    }
    const float radius = UiMetrics::kRadiusSmall;
    D2D1_COLOR_F fill = theme.surfaceElevated;
    D2D1_COLOR_F stroke = theme.border;
    D2D1_COLOR_F textColor = theme.text;
    if (highContrast) {
        fill = ToD2DColor(GetSysColor(COLOR_BTNFACE));
        stroke = ToD2DColor(GetSysColor(COLOR_WINDOWTEXT));
        textColor = ToD2DColor(GetSysColor(COLOR_WINDOWTEXT));
        if (primary) {
            fill = ToD2DColor(GetSysColor(COLOR_HIGHLIGHT));
            textColor = ToD2DColor(GetSysColor(COLOR_HIGHLIGHTTEXT));
        }
    } else if (primary) {
        fill = theme.accent;
        stroke = theme.accent;
        textColor = theme.textOnAccent;
        if (state == UiVisualState::Hover || state == UiVisualState::Pressed) {
            fill = theme.accentHover;
        }
    } else if (state == UiVisualState::Hover || state == UiVisualState::Pressed) {
        fill = theme.surfaceSubtle;
    }
    if (state == UiVisualState::Disabled) {
        textColor = highContrast ? ToD2DColor(GetSysColor(COLOR_GRAYTEXT)) : theme.textDisabled;
    }
    if (SUCCEEDED(UiEnsureSolidBrush(ctx, fill, &brushes->fill))) {
        UiFillRoundedRect(ctx, rect, brushes->fill.Get(), radius);
    }
    if (SUCCEEDED(UiEnsureSolidBrush(ctx, stroke, &brushes->stroke))) {
        UiDrawRoundedRect(ctx, rect, brushes->stroke.Get(), radius, 1.0f);
    }
    if (label != nullptr && label[0] != L'\0' && format != nullptr) {
        if (SUCCEEDED(UiEnsureSolidBrush(ctx, textColor, &brushes->text))) {
            UiDrawTextCentered(ctx, format, label, rect, brushes->text.Get());
        }
    }
}

// Compact semantic status chip (degraded engine, analysis state). Pill form is
// reserved for chips; everything else uses kRadiusSmall.
enum class UiChipKind { Active, Degraded, Error, Info };

inline void DrawUiStatusChip(ID2D1DeviceContext* ctx, D2D1_RECT_F rect, IDWriteTextFormat* format,
                             const wchar_t* label, UiChipKind kind, const UiTheme& theme,
                             bool highContrast, UiBrushCache* brushes) {
    if (ctx == nullptr || brushes == nullptr) {
        return;
    }
    D2D1_COLOR_F bg = theme.badgeActiveBg;
    D2D1_COLOR_F fg = theme.badgeActiveText;
    switch (kind) {
        case UiChipKind::Degraded:
            bg = theme.badgeDegradedBg;
            fg = theme.badgeDegradedText;
            break;
        case UiChipKind::Error:
            bg = highContrast ? ToD2DColor(GetSysColor(COLOR_BTNFACE)) : theme.error;
            fg = highContrast ? ToD2DColor(GetSysColor(COLOR_WINDOWTEXT)) : theme.textOnAccent;
            break;
        case UiChipKind::Info:
            bg = highContrast ? ToD2DColor(GetSysColor(COLOR_BTNFACE)) : theme.selectionSoft;
            fg = highContrast ? ToD2DColor(GetSysColor(COLOR_WINDOWTEXT)) : theme.info;
            break;
        case UiChipKind::Active:
        default:
            break;
    }
    const float height = rect.bottom - rect.top;
    if (height <= 0.0f) {
        return;
    }
    const float radius = height * 0.5f;  // pill — chips only
    if (SUCCEEDED(UiEnsureSolidBrush(ctx, bg, &brushes->fill))) {
        UiFillRoundedRect(ctx, rect, brushes->fill.Get(), radius);
    }
    if (label != nullptr && label[0] != L'\0' && format != nullptr) {
        if (SUCCEEDED(UiEnsureSolidBrush(ctx, fg, &brushes->text))) {
            UiDrawTextCentered(ctx, format, label, rect, brushes->text.Get());
        }
    }
}

// Deliberate empty/loading/error/degraded presentation: centers an optional
// title and concise explanation. Returns the y where a recovery-action button
// may be placed so callers can add interactive actions without this helper
// owning input or hit-testing.
inline float DrawUiStatePanel(ID2D1DeviceContext* ctx, D2D1_RECT_F rect, IDWriteTextFormat* titleFormat,
                              const wchar_t* title, IDWriteTextFormat* bodyFormat, const wchar_t* message,
                              const UiTheme& theme, bool highContrast, UiBrushCache* brushes) {
    if (ctx == nullptr || brushes == nullptr) {
        return rect.bottom;
    }
    const float centerX = (rect.left + rect.right) * 0.5f;
    const float centerY = (rect.top + rect.bottom) * 0.5f;
    const D2D1_COLOR_F titleColor =
        highContrast ? ToD2DColor(GetSysColor(COLOR_WINDOWTEXT)) : theme.text;
    const D2D1_COLOR_F bodyColor =
        highContrast ? ToD2DColor(GetSysColor(COLOR_WINDOWTEXT)) : theme.textSecondary;
    float y = centerY;
    if (title != nullptr && title[0] != L'\0' && titleFormat != nullptr) {
        const float titleHeight = titleFormat->GetFontSize() * 1.4f;
        y -= titleHeight * 0.5f;
        if (SUCCEEDED(UiEnsureSolidBrush(ctx, titleColor, &brushes->text))) {
            const D2D1_RECT_F layout{rect.left, y, rect.right, y + titleHeight};
            ctx->DrawTextW(title, static_cast<UINT32>(wcslen(title)), titleFormat, layout,
                           brushes->text.Get());
        }
        y += titleHeight + UiMetrics::kSpaceXs;
    }
    if (message != nullptr && message[0] != L'\0' && bodyFormat != nullptr) {
        const float bodyHeight = bodyFormat->GetFontSize() * 1.4f;
        if (SUCCEEDED(UiEnsureSolidBrush(ctx, bodyColor, &brushes->text))) {
            const D2D1_RECT_F layout{rect.left, y, rect.right, y + bodyHeight};
            ctx->DrawTextW(message, static_cast<UINT32>(wcslen(message)), bodyFormat, layout,
                           brushes->text.Get());
        }
        y += bodyHeight;
    }
    (void)centerX;
    return y + UiMetrics::kSpaceS;
}

// Progress surface for operations/analysis. Bounded mode: fraction in [0,1].
// Indeterminate mode: pass fraction < 0 and a caller-driven phase in [0,1]
// (the caller animates phase only while work is actually running — no timers
// are created here and no idle animation runs when nothing is in progress).
inline void DrawUiProgressSurface(ID2D1DeviceContext* ctx, D2D1_RECT_F rect, float fraction,
                                  float indeterminatePhase, const UiTheme& theme, bool highContrast,
                                  UiBrushCache* brushes) {
    if (ctx == nullptr || brushes == nullptr || rect.right <= rect.left || rect.bottom <= rect.top) {
        return;
    }
    const float radius = (rect.bottom - rect.top) * 0.5f;
    const D2D1_COLOR_F track = highContrast ? ToD2DColor(GetSysColor(COLOR_BTNFACE)) : theme.dividerSubtle;
    const D2D1_COLOR_F barColor = highContrast ? ToD2DColor(GetSysColor(COLOR_HIGHLIGHT)) : theme.accent;
    if (SUCCEEDED(UiEnsureSolidBrush(ctx, track, &brushes->fill))) {
        UiFillRoundedRect(ctx, rect, brushes->fill.Get(), radius);
    }
    D2D1_RECT_F bar = rect;
    if (fraction >= 0.0f) {
        const float t = std::clamp(fraction, 0.0f, 1.0f);
        bar.right = rect.left + (rect.right - rect.left) * t;
    } else {
        const float phase = std::clamp(indeterminatePhase, 0.0f, 1.0f);
        const float bandWidth = (rect.right - rect.left) * 0.25f;
        const float travel = (rect.right - rect.left) - bandWidth;
        bar.left = rect.left + travel * phase;
        bar.right = bar.left + bandWidth;
    }
    if (bar.right > bar.left && SUCCEEDED(UiEnsureSolidBrush(ctx, barColor, &brushes->stroke))) {
        UiFillRoundedRect(ctx, bar, brushes->stroke.Get(), radius);
    }
}

// ---------------------------------------------------------------------------
// GDI helper for owner-draw chrome and dialogs
// ---------------------------------------------------------------------------

// Fills a rounded rect with GDI (chrome owner-draw, dialogs). radiusPx is
// clamped to half the smaller side; DC state is restored.
inline void UiGdiRoundedRect(HDC dc, RECT rect, COLORREF fill, int radiusPx) {
    if (dc == nullptr) {
        return;
    }
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        return;
    }
    radiusPx = std::clamp(radiusPx, 0, std::min(width, height) / 2);
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = static_cast<HPEN>(GetStockObject(NULL_PEN));
    if (brush == nullptr) {
        return;
    }
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, brush));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
    RoundRect(dc, rect.left, rect.top, rect.right + 1, rect.bottom + 1, radiusPx * 2, radiusPx * 2);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
}

// Applies the Windows Explorer or ComboFlyout visual style so remaining native
// ListView/Tab/Combo/Edit chrome matches the tokenized shell. High Contrast
// resets to the system theme. Loads uxtheme on demand (same pattern as DWM).
enum class NativeExplorerTheme {
    Explorer,
    Combo,
};

inline void ApplyNativeExplorerTheme(HWND hwnd, bool dark,
                                     NativeExplorerTheme kind = NativeExplorerTheme::Explorer) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return;
    }
    HMODULE module = GetModuleHandleW(L"uxtheme.dll");
    if (module == nullptr) {
        module = LoadLibraryW(L"uxtheme.dll");
    }
    if (module == nullptr) {
        return;
    }
    using SetWindowThemeFn = HRESULT(WINAPI*)(HWND, LPCWSTR, LPCWSTR);
    auto setWindowTheme = reinterpret_cast<SetWindowThemeFn>(GetProcAddress(module, "SetWindowTheme"));
    if (setWindowTheme == nullptr) {
        return;
    }
    if (UiSystemHighContrast()) {
        setWindowTheme(hwnd, L"", nullptr);
        return;
    }
    const wchar_t* name = (kind == NativeExplorerTheme::Combo)
                              ? (dark ? L"DarkMode_CFD" : L"CFD")
                              : (dark ? L"DarkMode_Explorer" : L"Explorer");
    setWindowTheme(hwnd, name, nullptr);
}

// Disables visual styles so NM_CUSTOMDRAW / owner-draw can paint the control.
inline void DisableNativeWindowTheme(HWND hwnd) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return;
    }
    HMODULE module = GetModuleHandleW(L"uxtheme.dll");
    if (module == nullptr) {
        module = LoadLibraryW(L"uxtheme.dll");
    }
    if (module == nullptr) {
        return;
    }
    using SetWindowThemeFn = HRESULT(WINAPI*)(HWND, LPCWSTR, LPCWSTR);
    auto setWindowTheme = reinterpret_cast<SetWindowThemeFn>(GetProcAddress(module, "SetWindowTheme"));
    if (setWindowTheme == nullptr) {
        return;
    }
    // SetWindowTheme sends WM_THEMECHANGED to the same HWND. The owner-paint
    // header/tab subclasses re-apply this empty theme on that message; without
    // a re-entry guard that recurses until STATUS_STACK_OVERFLOW.
    static thread_local int depth = 0;
    if (depth > 0) {
        return;
    }
    ++depth;
    setWindowTheme(hwnd, L"", L"");
    --depth;
}

}  // namespace ffui
