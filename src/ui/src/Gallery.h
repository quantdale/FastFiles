#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>

namespace ffui {

// GallerySection: deterministic showcase of a component family.
struct GallerySection {
    std::wstring title;
    std::wstring description;
    // Ordered state labels for this section (e.g. "Rest, Hover, Pressed")
    std::vector<std::wstring> states;
};

struct GallerySizeVariant {
    std::wstring label;
    float width;
    float height;
};

// Returns true if --gallery (or /gallery) was present on the command line.
bool IsGalleryRequested();

// Debug-only gate: gallery is only available in _DEBUG builds. Release builds
// return false even if the flag is present, preventing an exposed debug surface.
bool IsGalleryAllowed();

// Deterministic gallery section inventory. Used both to render and to enumerate
// for the catalog (-Gallery) capture matrix.
std::vector<GallerySection> GallerySections();

// Size variants for geometry coverage (narrow/normal/wide).
std::vector<GallerySizeVariant> GallerySizeVariants();

// Render the gallery into the given Direct2D context. The caller provides the
// client rect, theme darkness, and density. All drawing sources colors/metrics
// from UITheme.h so the gallery uses actual production rendering primitives.
void RenderGallery(ID2D1DeviceContext* context, ID2D1SolidColorBrush* textBrush,
                   ID2D1SolidColorBrush* secondaryBrush, IDWriteTextFormat* titleFormat,
                   IDWriteTextFormat* bodyFormat, IDWriteTextFormat* captionFormat,
                   const D2D1_RECT_F& clientRect, bool dark, float dpiScale);

// Hit-test helper: returns the section index at the given point, or -1.
int GalleryHitTest(D2D1_POINT_2F point, const D2D1_RECT_F& clientRect);

} // namespace ffui
