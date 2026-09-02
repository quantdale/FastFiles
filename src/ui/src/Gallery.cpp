#include "Gallery.h"
#include <shellapi.h>
#include "UITheme.h"
#include "UiStyle.h"
#include <wrl/client.h>

#include <algorithm>
#include <cwctype>

namespace ffui {

bool IsGalleryRequested() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return false;
    bool found = false;
    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        std::transform(arg.begin(), arg.end(), arg.begin(), ::towlower);
        if (arg == L"--gallery" || arg == L"/gallery" || arg == L"-gallery") {
            found = true;
            break;
        }
    }
    LocalFree(argv);
    return found;
}

bool IsGalleryAllowed() {
#ifdef _DEBUG
    return IsGalleryRequested();
#else
    return false;
#endif
}

std::vector<GallerySection> GallerySections() {
    return {
        {L"Buttons", L"Primary, secondary, and icon buttons in rest/hover/pressed/disabled states.", {L"Rest", L"Hover", L"Pressed", L"Disabled", L"Focused"}},
        {L"Navigation / Sidebar", L"Sidebar items, breadcrumbs, and tab affordances.", {L"Default", L"Selected", L"Hover", L"Focused"}},
        {L"Search & Filters", L"Search field, filter chips, and empty/loading/error states.", {L"Empty", L"Populated", L"Loading", L"Error", L"Filtered"}},
        {L"Rows & Selection", L"File rows with selection, focus, and hover states.", {L"Rest", L"Hover", L"Selected", L"Selected+Focused", L"Disabled"}},
        {L"Cards & Empty States", L"Cards, empty placeholders, and status banners.", {L"Card", L"Empty", L"Loading", L"Error"}},
        {L"Settings Controls", L"Toggles, combos, and settings rows.", {L"On", L"Off", L"Disabled", L"Focused"}},
        {L"Storage Controls", L"Segmented control and storage analysis affordances.", {L"Overview", L"Drilldown", L"Treemap"}},
        {L"Command Palette", L"Palette input and result rows.", {L"Idle", L"Query", L"Results", L"No Results"}},
    };
}

std::vector<GallerySizeVariant> GallerySizeVariants() {
    return {
        {L"Narrow (800px)", 800.0f, 720.0f},
        {L"Normal (1280px)", 1280.0f, 800.0f},
        {L"Wide (1600px)", 1600.0f, 800.0f},
    };
}

void RenderGallery(ID2D1DeviceContext* context, ID2D1SolidColorBrush* textBrush,
                   [[maybe_unused]] ID2D1SolidColorBrush* secondaryBrush, IDWriteTextFormat* titleFormat,
                   IDWriteTextFormat* bodyFormat, IDWriteTextFormat* captionFormat,
                   const D2D1_RECT_F& clientRect, bool dark, [[maybe_unused]] float dpiScale) {
    if (!context || !textBrush || !titleFormat || !bodyFormat) return;

    const UiTheme theme = GetUiTheme(dark);
    const float padding = 24.0f;
    const float sectionGap = 20.0f;
    const float itemGap = 8.0f;

    // Background
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush;
    context->CreateSolidColorBrush(theme.background, &bgBrush);
    context->FillRectangle(clientRect, bgBrush.Get());

    // Title area
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accentBrush;
    context->CreateSolidColorBrush(theme.accent, &accentBrush);

    float y = clientRect.top + padding;

    // Header
    {
        D2D1_RECT_F headerRect = D2D1::RectF(clientRect.left + padding, y, clientRect.right - padding, y + 32.0f);
        context->DrawTextW(L"FastFiles Component Gallery  (debug only)", 42, titleFormat, headerRect, textBrush);
        y += 36.0f;
        D2D1_RECT_F descRect = D2D1::RectF(clientRect.left + padding, y, clientRect.right - padding, y + 20.0f);
        if (captionFormat) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> secBrush;
            context->CreateSolidColorBrush(theme.textSecondary, &secBrush);
            context->DrawTextW(L"Deterministic showcase of production components — theme/density/geometry matrix.", 70, captionFormat, descRect, secBrush.Get());
        }
        y += 28.0f;

        // Divider
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> borderBrush;
        context->CreateSolidColorBrush(theme.border, &borderBrush);
        context->DrawLine(D2D1::Point2F(clientRect.left + padding, y), D2D1::Point2F(clientRect.right - padding, y), borderBrush.Get(), 1.0f);
        y += sectionGap;
    }

    const auto sections = GallerySections();
    for (size_t si = 0; si < sections.size(); ++si) {
        const auto& section = sections[si];
        if (y + 80.0f > clientRect.bottom - padding) break; // clip, don't overflow

        // Section title
        D2D1_RECT_F titleRect = D2D1::RectF(clientRect.left + padding, y, clientRect.right - padding, y + 22.0f);
        context->DrawTextW(section.title.c_str(), (UINT32)section.title.size(), bodyFormat, titleRect, textBrush);
        y += 22.0f;

        // Description
        if (captionFormat) {
            D2D1_RECT_F capRect = D2D1::RectF(clientRect.left + padding, y, clientRect.right - padding, y + 16.0f);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> secBrush;
            context->CreateSolidColorBrush(theme.textSecondary, &secBrush);
            context->DrawTextW(section.description.c_str(), (UINT32)section.description.size(), captionFormat, capRect, secBrush.Get());
            y += 16.0f + itemGap;
        }

        // State chips - rendered as rounded rects using production metrics
        float x = clientRect.left + padding;
        for (const auto& state : section.states) {
            const float chipWidth = 90.0f;
            const float chipHeight = 28.0f;
            if (x + chipWidth > clientRect.right - padding) break;

            D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(x, y, x + chipWidth, y + chipHeight),
                                                     UiMetrics::kRadiusSmall, UiMetrics::kRadiusSmall);
            // Alternate fill for visual variety using theme tokens
            bool isSelected = (state == L"Selected" || state == L"Pressed" || state == L"Selected+Focused");
            if (isSelected) {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> selBrush;
                context->CreateSolidColorBrush(theme.accent, &selBrush);
                context->FillRoundedRectangle(rr, selBrush.Get());
            } else {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> surfBrush;
                context->CreateSolidColorBrush(theme.surfaceElevated, &surfBrush);
                context->FillRoundedRectangle(rr, surfBrush.Get());
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bdBrush;
                context->CreateSolidColorBrush(theme.border, &bdBrush);
                context->DrawRoundedRectangle(rr, bdBrush.Get(), 1.0f);
            }

            D2D1_RECT_F labelRect = D2D1::RectF(x, y, x + chipWidth, y + chipHeight);
            ID2D1SolidColorBrush* labelBrush = isSelected ? textBrush : textBrush;
            // For selected chips, use textOnAccent for contrast
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> onAccentBrush;
            ID2D1SolidColorBrush* effectiveBrush = labelBrush;
            if (isSelected) {
                context->CreateSolidColorBrush(theme.textOnAccent, &onAccentBrush);
                effectiveBrush = onAccentBrush.Get();
            }
            // Center text: use caption format
            if (captionFormat) {
                // Simple: just draw; centering via text alignment set on format
                context->DrawTextW(state.c_str(), (UINT32)state.size(), captionFormat, labelRect, effectiveBrush);
            }

            x += chipWidth + itemGap;
        }
        y += 32.0f + itemGap;

        // Row preview for row-oriented sections
        if (section.title == L"Rows & Selection" || section.title == L"Search & Filters") {
            for (int row = 0; row < 3; ++row) {
                const float rowH = 28.0f;
                D2D1_RECT_F rowRect = D2D1::RectF(clientRect.left + padding, y, clientRect.right - padding, y + rowH);
                bool hover = (row == 1);
                bool selected = (row == 2);
                if (selected) {
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> selBrush;
                    context->CreateSolidColorBrush(theme.selectionSoft, &selBrush);
                    context->FillRectangle(rowRect, selBrush.Get());
                } else if (hover) {
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hovBrush;
                    context->CreateSolidColorBrush(theme.hoverOverlay, &hovBrush);
                    context->FillRectangle(rowRect, hovBrush.Get());
                }
                // Row label
                std::wstring rowLabel = L"  " + section.title + L" row " + std::to_wstring(row + 1);
                if (captionFormat) {
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> secBrush;
                    context->CreateSolidColorBrush(theme.textSecondary, &secBrush);
                    context->DrawTextW(rowLabel.c_str(), (UINT32)rowLabel.size(), captionFormat, rowRect, secBrush.Get());
                }
                y += rowH + 2.0f;
            }
            y += itemGap;
        }

        y += 4.0f;
    }

    // Footer note
    if (captionFormat && y + 20.0f < clientRect.bottom) {
        D2D1_RECT_F footerRect = D2D1::RectF(clientRect.left + padding, clientRect.bottom - 20.0f, clientRect.right - padding, clientRect.bottom - 4.0f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> secBrush;
        context->CreateSolidColorBrush(theme.textSecondary, &secBrush);
        context->DrawTextW(L"Gallery is debug-only and not available in release builds.", 58, captionFormat, footerRect, secBrush.Get());
    }
}

int GalleryHitTest([[maybe_unused]] D2D1_POINT_2F point, [[maybe_unused]] const D2D1_RECT_F& clientRect) {
    // Deterministic: gallery has no interactive hit targets in this version.
    return -1;
}

} // namespace ffui
