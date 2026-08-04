// Shared Direct2D/DirectWrite styling helpers for the modernized UI surfaces.
// Thin, allocation-free wrappers so the shell's paint paths use one
// rounded-rect/text-format idiom instead of per-surface copies. All helpers
// are safe to call from the render thread inside BeginDraw/Present.
#pragma once

#include <algorithm>
#include <d2d1_1.h>
#include <dwrite.h>
#include <windows.h>
#include <wrl/client.h>

namespace ffui {

// Creates a DirectWrite text format using the requested family, retrying
// "Segoe UI" when the primary family cannot be created (e.g. "Segoe UI
// Variable Text" only exists on Windows 11). sizeDip is the font size in DIPs.
// The result replaces *out; returns S_OK or the failed-create HRESULT.
HRESULT UiCreateTextFormat(
    IDWriteFactory* factory,
    float sizeDip,
    DWRITE_FONT_WEIGHT weight,
    const wchar_t* family,
    Microsoft::WRL::ComPtr<IDWriteTextFormat>* out);

// Fills a rounded rectangle, clamping radius to half the smaller side so the
// corner radii never produce a malformed geometry. Caller must be in a
// BeginDraw target set on ctx.
inline void UiFillRoundedRect(ID2D1DeviceContext* ctx, D2D1_RECT_F rect, ID2D1Brush* brush, float radius) {
    if (ctx == nullptr || brush == nullptr) {
        return;
    }
    float maxRadius = std::min(rect.right - rect.left, rect.bottom - rect.top) * 0.5f;
    if (maxRadius < 0.0f) {
        maxRadius = 0.0f;
    }
    if (radius < 0.0f) {
        radius = 0.0f;
    }
    radius = std::min(radius, maxRadius);
    const D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(rect, radius, radius);
    ctx->FillRoundedRectangle(&roundedRect, brush);
}

// Outlines a rounded rectangle with the same radius clamping as
// UiFillRoundedRect.
inline void UiDrawRoundedRect(ID2D1DeviceContext* ctx, D2D1_RECT_F rect, ID2D1Brush* brush, float radius, float strokeWidth = 1.0f) {
    if (ctx == nullptr || brush == nullptr) {
        return;
    }
    float maxRadius = std::min(rect.right - rect.left, rect.bottom - rect.top) * 0.5f;
    if (maxRadius < 0.0f) {
        maxRadius = 0.0f;
    }
    if (radius < 0.0f) {
        radius = 0.0f;
    }
    radius = std::min(radius, maxRadius);
    const D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(rect, radius, radius);
    ctx->DrawRoundedRectangle(&roundedRect, brush, strokeWidth);
}

// Lazily creates (or re-creates) a solid brush for color in the caller-owned
// *out: an existing brush with the same color is reused, a color change
// recreates it (so one cached ComPtr can track theme changes across paint
// passes). Returns S_OK, E_INVALIDARG for bad arguments, or the creation
// failure HRESULT.
inline HRESULT UiEnsureSolidBrush(
    ID2D1DeviceContext* ctx,
    D2D1_COLOR_F color,
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>* out) {
    if (ctx == nullptr || out == nullptr) {
        return E_INVALIDARG;
    }
    if (*out) {
        const D2D1_COLOR_F current = (*out)->GetColor();
        if (current.r == color.r && current.g == color.g && current.b == color.b && current.a == color.a) {
            return S_OK;
        }
        out->Reset();
    }
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    HRESULT hr = ctx->CreateSolidColorBrush(color, &brush);
    if (SUCCEEDED(hr)) {
        *out = std::move(brush);
    }
    return hr;
}

// Fills a rounded rect with a translucent interaction overlay (typically
// GetUiTheme().hoverOverlay / .pressOverlay). The caller caches the brush in
// *brush; the overlay color parameter lets consumers share one cache across
// related hover/press states.
inline void UiFillHoverOverlay(
    ID2D1DeviceContext* ctx,
    D2D1_RECT_F rect,
    float radius,
    D2D1_COLOR_F overlayColor,
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>* brush) {
    if (SUCCEEDED(UiEnsureSolidBrush(ctx, overlayColor, brush))) {
        UiFillRoundedRect(ctx, rect, brush->Get(), radius);
    }
}

// See UiFillHoverOverlay — pressed-state variant.
inline void UiFillPressOverlay(
    ID2D1DeviceContext* ctx,
    D2D1_RECT_F rect,
    float radius,
    D2D1_COLOR_F overlayColor,
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>* brush) {
    if (SUCCEEDED(UiEnsureSolidBrush(ctx, overlayColor, brush))) {
        UiFillRoundedRect(ctx, rect, brush->Get(), radius);
    }
}

// Per-channel (r,g,b,a) linear interpolation between two colors; t is clamped
// to [0,1]. Used for theme-transition fades.
inline D2D1_COLOR_F UiLerpColor(D2D1_COLOR_F a, D2D1_COLOR_F b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    D2D1_COLOR_F result;
    result.r = a.r + (b.r - a.r) * t;
    result.g = a.g + (b.g - a.g) * t;
    result.b = a.b + (b.b - a.b) * t;
    result.a = a.a + (b.a - a.a) * t;
    return result;
}

}  // namespace ffui
