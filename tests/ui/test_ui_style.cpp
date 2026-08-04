// Unit tests for the modernize-ui-appearance foundation seams that are
// testable without a live renderer:
//   - IconCache keying (FolderKey / IconKeyForExtension normalization)
//   - FloatAnimation easing, snap-when-disabled, re-target
//   - ToColorRef / ToD2DColor / UiLerpColor color conversions
// All production logic exercised here is header-inline or pure; only
// UiAnimation.cpp (SystemParametersInfo + easing math) is compiled in.

#include "IconCache.h"
#include "UiAnimation.h"
#include "UiStyle.h"
#include "UITheme.h"

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <string>

namespace {
int failures = 0;
void Check(bool value, const char* text) { if (!value) { std::fprintf(stderr, "FAIL: %s\n", text); ++failures; } }

bool NearlyEqual(float a, float b) { return (a > b ? a - b : b - a) < 0.001f; }

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
} // namespace

int main() {
    TestIconKeys();
    TestToColorRef();
    TestToD2DColor();
    TestLerpColor();
    TestFloatAnimationEasing();
    TestFloatAnimationSnap();
    TestIconCacheHeaderApi();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
