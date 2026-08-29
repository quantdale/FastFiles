// Unit tests for the modernize-ui-appearance foundation seams that are
// testable without a live renderer:
//   - IconCache keying (FolderKey / IconKeyForExtension normalization)
//   - FloatAnimation easing, snap-when-disabled, re-target
//   - ToColorRef / ToD2DColor / UiLerpColor color conversions
// All production logic exercised here is header-inline or pure; only
// UiAnimation.cpp (SystemParametersInfo + easing math) is compiled in.

#include "IconCache.h"
#include "NativeChromePaint.h"
#include "UiAnimation.h"
#include "UiComponents.h"
#include "UiStyle.h"
#include "UITheme.h"

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <algorithm>
#include <cmath>
#include <string>
#include "../TestSupport.h"

using namespace fftest;

namespace {

bool NearlyEqual(float a, float b) { return (a > b ? a - b : b - a) < 0.001f; }

float SrgbToLinear(float channel) {
    return channel <= 0.03928f ? channel / 12.92f
                               : std::pow((channel + 0.055f) / 1.055f, 2.4f);
}

float RelativeLuminance(const D2D1_COLOR_F& color) {
    return 0.2126f * SrgbToLinear(color.r) + 0.7152f * SrgbToLinear(color.g) +
           0.0722f * SrgbToLinear(color.b);
}

float ContrastRatio(const D2D1_COLOR_F& foreground, const D2D1_COLOR_F& background) {
    const float foregroundLuminance = RelativeLuminance(foreground);
    const float backgroundLuminance = RelativeLuminance(background);
    const float lighter = std::max(foregroundLuminance, backgroundLuminance);
    const float darker = std::min(foregroundLuminance, backgroundLuminance);
    return (lighter + 0.05f) / (darker + 0.05f);
}

D2D1_COLOR_F CompositeOver(const D2D1_COLOR_F& foreground, const D2D1_COLOR_F& background) {
    const float alpha = std::clamp(foreground.a, 0.0f, 1.0f);
    return D2D1::ColorF(foreground.r * alpha + background.r * (1.0f - alpha),
                        foreground.g * alpha + background.g * (1.0f - alpha),
                        foreground.b * alpha + background.b * (1.0f - alpha), 1.0f);
}

void CheckContrast(const char* name, const D2D1_COLOR_F& foreground,
                   const D2D1_COLOR_F& background, float minimum) {
    const float ratio = ContrastRatio(foreground, background);
    const std::string description = std::string(name) + " contrast meets " +
                                    std::to_string(minimum) + ":1 (actual " +
                                    std::to_string(ratio) + ":1)";
    Check(ratio + 0.0001f >= minimum, description.c_str());
}

void TestThemeContrast() {
    using namespace ffui;
    for (const bool dark : {false, true}) {
        const UiTheme theme = GetUiTheme(dark);
        const char* mode = dark ? "dark" : "light";
        const std::string prefix = std::string(mode) + " ";

        // Primary, secondary, tertiary, search, and glyph text are normal
        // content and retain the 4.5:1 WCAG AA threshold.
        CheckContrast((prefix + "primary text").c_str(), theme.text, theme.background, 4.5f);
        CheckContrast((prefix + "secondary text").c_str(), theme.textSecondary, theme.background, 4.5f);
        CheckContrast((prefix + "tertiary text").c_str(), theme.textTertiary, theme.background, 4.5f);
        CheckContrast((prefix + "search placeholder").c_str(), theme.searchPlaceholder,
                      theme.surfaceElevated, 4.5f);
        CheckContrast((prefix + "folder glyph").c_str(), theme.folderGlyph, theme.background, 3.0f);
        CheckContrast((prefix + "file glyph").c_str(), theme.fileGlyph, theme.background, 3.0f);

        // Disabled labels are explicitly exempt from the normal-text WCAG
        // threshold; keep a two-to-one floor so disabled controls do not vanish.
        CheckContrast((prefix + "disabled text remains visible").c_str(), theme.textDisabled,
                      theme.surface, 2.0f);

        // Selected text and keyboard focus are tested against the surfaces
        // actually painted by UiComponents and owner-drawn chrome.
        CheckContrast((prefix + "selected text").c_str(), theme.textOnAccent, theme.accent, 4.5f);
        CheckContrast((prefix + "accent hover text").c_str(), theme.textOnAccent,
                      theme.accentHover, 4.5f);
        CheckContrast((prefix + "focus on background").c_str(), theme.focusStroke,
                      theme.background, 3.0f);
        CheckContrast((prefix + "focus on surface").c_str(), theme.focusStroke,
                      theme.surface, 3.0f);

        // Status chips use their own paired foreground/background tokens. The
        // informational chip is translucent, so test the composited surface.
        CheckContrast((prefix + "active status").c_str(), theme.badgeActiveText,
                      theme.badgeActiveBg, 4.5f);
        CheckContrast((prefix + "degraded status").c_str(), theme.badgeDegradedText,
                      theme.badgeDegradedBg, 4.5f);
        CheckContrast((prefix + "error status").c_str(), theme.textOnAccent, theme.error, 4.5f);
        CheckContrast((prefix + "info status").c_str(), theme.info,
                      CompositeOver(theme.selectionSoft, theme.background), 3.0f);
        CheckContrast((prefix + "success status").c_str(), theme.success, theme.background, 4.5f);
        CheckContrast((prefix + "warning status").c_str(), theme.warning, theme.background, 4.5f);
        CheckContrast((prefix + "info text").c_str(), theme.info, theme.background, 4.5f);
    }
}

void TestIconKeys() {
    using namespace ffui;
    Check(std::wstring(FolderKey()) == L"folder", "FolderKey is the folder cache key");
    Check(IconKeyForExtension(L"txt") == L".txt", "extension without dot gains a leading dot");
    Check(IconKeyForExtension(L".txt") == L".txt", "extension with a dot is unchanged");
    Check(IconKeyForExtension(L"") == L"", "empty extension yields an empty key");
    Check(IconKeyForExtension(L"jpg") != IconKeyForExtension(L"png"), "distinct extensions map to distinct keys");
    Check(IconKeyForExtension(L"TXT") == L".TXT", "extension case is preserved (display-safe keying)");
}

void TestToColorRef() {
    using namespace ffui;
    // D2D1::ColorF(0xRRGGBB) interprets the value as RGB; ToColorRef must
    // produce the matching 0x00bbggrr COLORREF.
    const D2D1_COLOR_F white = D2D1::ColorF(0xFFFFFF);
    Check(ToColorRef(white) == RGB(0xFF, 0xFF, 0xFF), "white converts to RGB(255,255,255)");
    const D2D1_COLOR_F black = D2D1::ColorF(0x000000);
    Check(ToColorRef(black) == RGB(0x00, 0x00, 0x00), "black converts to RGB(0,0,0)");
    const D2D1_COLOR_F red = D2D1::ColorF(0xFF0000);
    Check(ToColorRef(red) == RGB(0xFF, 0x00, 0x00), "red converts to RGB(255,0,0)");
    const D2D1_COLOR_F green = D2D1::ColorF(0x00FF00);
    Check(ToColorRef(green) == RGB(0x00, 0xFF, 0x00), "green converts to RGB(0,255,0)");
    const D2D1_COLOR_F blue = D2D1::ColorF(0x0000FF);
    Check(ToColorRef(blue) == RGB(0x00, 0x00, 0xFF), "blue converts to RGB(0,0,255)");
    // Out-of-range channels clamp to [0,255] instead of wrapping.
    D2D1_COLOR_F overRange{1.5f, -0.5f, 0.0f, 1.0f};
    Check(ToColorRef(overRange) == RGB(0xFF, 0x00, 0x00), "out-of-range channels clamp when converting");
}

void TestToD2DColor() {
    using namespace ffui;
    const D2D1_COLOR_F white = ToD2DColor(RGB(0xFF, 0xFF, 0xFF));
    Check(NearlyEqual(white.r, 1.0f) && NearlyEqual(white.g, 1.0f) && NearlyEqual(white.b, 1.0f),
          "RGB(255,255,255) converts to opaque white D2D color");
    const D2D1_COLOR_F black = ToD2DColor(RGB(0x00, 0x00, 0x00));
    Check(black.r == 0.0f && black.g == 0.0f && black.b == 0.0f,
          "RGB(0,0,0) converts to zero channels");
    const D2D1_COLOR_F blue = ToD2DColor(RGB(0x00, 0x00, 0xFF));
    Check(NearlyEqual(blue.b, 1.0f) && blue.r == 0.0f && blue.g == 0.0f,
          "RGB(0,0,255) places the blue channel in b");
    Check(blue.a == 1.0f, "COLORREF conversion is always opaque");
    // Round trip: ToD2DColor(ToColorRef(c)) reproduces the rounded color. The
    // single-UINT32 ColorF constructor interprets 0xRRGGBB (red in the high byte).
    const D2D1_COLOR_F source = D2D1::ColorF(0x123456);
    const D2D1_COLOR_F roundTrip = ToD2DColor(ToColorRef(source));
    Check(NearlyEqual(roundTrip.r, source.r) && NearlyEqual(roundTrip.g, source.g) &&
          NearlyEqual(roundTrip.b, source.b),
          "D2D -> COLORREF -> D2D round trip preserves the color");
}

void TestLerpColor() {
    using namespace ffui;
    const D2D1_COLOR_F a = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f);
    const D2D1_COLOR_F b = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
    const D2D1_COLOR_F mid = UiLerpColor(a, b, 0.5f);
    Check(NearlyEqual(mid.r, 0.5f) && NearlyEqual(mid.g, 0.5f) && NearlyEqual(mid.b, 0.5f) && NearlyEqual(mid.a, 0.5f),
          "lerp at t=0.5 lands at the midpoint of every channel");
    const D2D1_COLOR_F start = UiLerpColor(a, b, 0.0f);
    Check(NearlyEqual(start.r, 0.0f) && NearlyEqual(start.a, 0.0f), "lerp at t=0 returns the from color");
    const D2D1_COLOR_F end = UiLerpColor(a, b, 1.0f);
    Check(NearlyEqual(end.r, 1.0f) && NearlyEqual(end.a, 1.0f), "lerp at t=1 returns the to color");
    const D2D1_COLOR_F clamped = UiLerpColor(a, b, 3.0f);
    Check(NearlyEqual(clamped.r, 1.0f), "lerp clamps t above 1");
    const D2D1_COLOR_F clampedLow = UiLerpColor(a, b, -1.0f);
    Check(NearlyEqual(clampedLow.r, 0.0f), "lerp clamps t below 0");
    // Channel independence: only the red channel moves.
    const D2D1_COLOR_F g = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.5f);
    const D2D1_COLOR_F partial = UiLerpColor(a, g, 1.0f);
    Check(NearlyEqual(partial.g, 1.0f) && NearlyEqual(partial.a, 0.5f), "each channel interpolates independently");
}

void TestFloatAnimationEasing() {
    using namespace ffui;
    // When system animations are enabled, AnimateTo must ease-toward the target
    // and settle at exactly the target when the duration elapses.
    if (SystemAnimationsEnabled()) {
        FloatAnimation anim{0.0f};
        anim.AnimateTo(100.0f, kUiAnimationDefaultMs, 0);
        Check(anim.IsAnimating(), "AnimateTo starts an animation when enabled");
        // Ease-out cubic: at half-time the value is > halfway (fast start).
        anim.Tick(static_cast<uint64_t>(kUiAnimationDefaultMs * 0.5f));
        Check(anim.Value() > 50.0f && anim.Value() < 100.0f,
              "ease-out cubic overshoots the linear halfway point at t=0.5");
        Check(anim.IsAnimating(), "animation still in progress at half-time");
        anim.Tick(static_cast<uint64_t>(kUiAnimationDefaultMs));
        Check(!anim.IsAnimating() && NearlyEqual(anim.Value(), 100.0f),
              "animation settles exactly at the target when the duration elapses");
        // Re-target mid-flight restarts from the current held value.
        FloatAnimation retarget{0.0f};
        retarget.AnimateTo(50.0f, kUiAnimationDefaultMs, 0);
        retarget.Tick(static_cast<uint64_t>(kUiAnimationDefaultMs * 0.25f));
        const float midway = retarget.Value();
        retarget.AnimateTo(0.0f, kUiAnimationDefaultMs, static_cast<uint64_t>(kUiAnimationDefaultMs * 0.25f));
        Check(retarget.IsAnimating() && retarget.Value() == midway,
              "re-targeting mid-flight starts from the current value");
        retarget.Tick(static_cast<uint64_t>(kUiAnimationDefaultMs * 1.25f));
        Check(NearlyEqual(retarget.Value(), 0.0f) && !retarget.IsAnimating(),
              "re-targeted animation settles at the new target");
    } else {
        FloatAnimation anim{0.0f};
        anim.AnimateTo(100.0f, kUiAnimationDefaultMs, 0);
        Check(!anim.IsAnimating() && NearlyEqual(anim.Value(), 100.0f),
              "animations snap instantly when the system setting disables them");
    }
}

void TestFloatAnimationSnap() {
    using namespace ffui;
    // durationMs <= 0 snaps regardless of the system animation setting.
    FloatAnimation anim{0.0f};
    anim.AnimateTo(42.0f, 0.0f, 0);
    Check(!anim.IsAnimating() && NearlyEqual(anim.Value(), 42.0f),
          "zero-duration AnimateTo snaps to the target");
    anim.Snap(7.0f);
    Check(!anim.IsAnimating() && NearlyEqual(anim.Value(), 7.0f), "explicit Snap jumps to the value");
    // A settled animation does not advance on further ticks.
    anim.Tick(1000);
    Check(NearlyEqual(anim.Value(), 7.0f), "ticks on a settled animation are no-ops");
}

void TestIconCacheHeaderApi() {
    using namespace ffui;
    // The LRU bound constant is documented and stable; verify keying contract
    // used by the UI surfaces (column rows key folders separately from files).
    Check(IconKeyForExtension(L"folder") != FolderKey(),
          "a literal 'folder' filename extension can never collide with the folder key");
    Check(IconKeyForExtension(L"txt") != FolderKey(), "extension keys are distinct from the folder key");
}

void TestNativeExplorerThemeNullHwnd() {
    using namespace ffui;
    NativeExplorerTheme kind = NativeExplorerTheme::Combo;
    ApplyNativeExplorerTheme(nullptr, true);
    ApplyNativeExplorerTheme(nullptr, false, kind);
    Check(kind == NativeExplorerTheme::Combo, "combo theme kind is preserved across a null-HWND apply");
}

void TestNativeHeaderAndTabPalettes() {
    using namespace ffui;
    for (const bool dark : {false, true}) {
        const NativeHeaderPalette header = ResolveNativeHeaderPalette(dark, false);
        const NativeTabPalette tab = ResolveNativeTabPalette(dark, false);
        Check(!header.useSystemHeader, "tokenized header is used outside High Contrast");
        Check(!tab.useSystemTab, "tokenized tab strip is used outside High Contrast");
        Check(header.background == ToColorRef(GetUiTheme(dark).surfaceSubtle),
              "header fill uses surfaceSubtle");
        Check(header.text == ToColorRef(GetUiTheme(dark).textSecondary),
              "header text uses textSecondary");
        Check(tab.stripBackground == ToColorRef(GetUiTheme(dark).background),
              "tab remainder uses the dialog/shell background");
        Check(tab.selectedFill == ToColorRef(GetUiTheme(dark).surfaceElevated),
              "selected tab uses surfaceElevated");
        Check(tab.unselectedText == ToColorRef(GetUiTheme(dark).textSecondary),
              "unselected tab text uses textSecondary");
        Check(tab.accentBar == ToColorRef(GetUiTheme(dark).accent),
              "selected tab accent bar uses the accent token");
        Check(header.background != header.text, "header text contrasts with its fill");
        Check(tab.stripBackground != tab.selectedText, "selected tab text contrasts with the strip");
    }
    const NativeHeaderPalette hcHeader = ResolveNativeHeaderPalette(true, true);
    const NativeTabPalette hcTab = ResolveNativeTabPalette(true, true);
    Check(hcHeader.useSystemHeader, "High Contrast keeps the system header");
    Check(hcTab.useSystemTab, "High Contrast keeps the system tab control");
}
} // namespace

int main() {
    TestIconKeys();
    TestToColorRef();
    TestToD2DColor();
    TestLerpColor();
    TestFloatAnimationEasing();
    TestFloatAnimationSnap();
    TestIconCacheHeaderApi();
    TestNativeExplorerThemeNullHwnd();
    TestNativeHeaderAndTabPalettes();
    TestThemeContrast();
    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
