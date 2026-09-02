#include "Gallery.h"
#include <shellapi.h>
#include "UITheme.h"
#include "UiStyle.h"
#include "UiComponents.h"
#include <wrl/client.h>

#include <algorithm>
#include <cwctype>
#include <cwchar>

namespace ffui {

// Draws a null-terminated literal with its exact length. Hardcoded DrawTextW
// counts drift from the literal (truncation or over-read), so every gallery
// label goes through this helper.
inline void DrawGalleryText(ID2D1DeviceContext* context, const wchar_t* text, IDWriteTextFormat* format,
                            const D2D1_RECT_F& rect, ID2D1Brush* brush) {
    if (!context || !text || !format || !brush) return;
    context->DrawTextW(text, static_cast<UINT32>(wcslen(text)), format, rect, brush);
}

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
        {L"Buttons & Commands", L"Primary, secondary, and icon buttons in rest/hover/pressed/disabled states.", {L"Primary", L"Hover", L"Secondary", L"Disabled"}},
        {L"Semantic Status Chips", L"Semantic status indicators for engine, indexing, and health.", {L"Active", L"Degraded", L"Error", L"Info"}},
        {L"Navigation & Sidebar", L"Sidebar destinations with active marker and hover overlays.", {L"Active (Home)", L"Rest (Browse)", L"Hover (Search)"}},
        {L"Progress & Operations", L"Determinate and indeterminate progress indicators.", {L"Determinate 65%", L"Indeterminate"}},
        {L"Data Rows & Selection", L"Data rows with tokenized selection, soft selection, and keyboard focus.", {L"Rest", L"Hover", L"Selected Focused", L"Selected Unfocused"}},
        {L"Drop Target Affordance", L"Shape and stroke affordance for drag-and-drop targets.", {L"Drop Target"}},
        {L"State Panels & Placeholders", L"Centered empty, loading, or error presentation with actionable recovery.", {L"Empty State"}},
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
    const bool highContrast = UiSystemHighContrast();
    const float padding = 24.0f;
    const float sectionGap = 16.0f;
    const float itemGap = 8.0f;

    // Background
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush;
    context->CreateSolidColorBrush(theme.background, &bgBrush);
    context->FillRectangle(clientRect, bgBrush.Get());

    UiBrushCache brushes;

    float y = clientRect.top + padding;

    // Header
    {
        D2D1_RECT_F headerRect = D2D1::RectF(clientRect.left + padding, y, clientRect.right - padding, y + 32.0f);
        DrawGalleryText(context, L"FastFiles Component Gallery  (debug only)", titleFormat, headerRect, textBrush);
        y += 36.0f;

        if (captionFormat) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> secBrush;
            context->CreateSolidColorBrush(theme.textSecondary, &secBrush);
            D2D1_RECT_F descRect = D2D1::RectF(clientRect.left + padding, y, clientRect.right - padding, y + 20.0f);
            DrawGalleryText(context, L"Deterministic showcase of production components — theme/density/geometry matrix.", captionFormat, descRect, secBrush.Get());
        }
        y += 24.0f;

        // Divider
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> borderBrush;
        context->CreateSolidColorBrush(theme.border, &borderBrush);
        context->DrawLine(D2D1::Point2F(clientRect.left + padding, y), D2D1::Point2F(clientRect.right - padding, y), borderBrush.Get(), 1.0f);
        y += sectionGap;
    }

    const float availableWidth = (clientRect.right - clientRect.left) - 2.0f * padding;
    const bool twoColumn = availableWidth >= 900.0f;
    const float colGap = 24.0f;
    const float colWidth = twoColumn ? (availableWidth - colGap) * 0.5f : availableWidth;

    const float col1Left = clientRect.left + padding;
    const float col1Right = col1Left + colWidth;
    const float col2Left = twoColumn ? (col1Right + colGap) : col1Left;
    const float col2Right = col2Left + colWidth;

    float y1 = y;
    float y2 = twoColumn ? y : y;

    // --- Column 1: Buttons, Status Chips, Navigation, Progress ---

    // 1. Buttons
    {
        D2D1_RECT_F tRect = D2D1::RectF(col1Left, y1, col1Right, y1 + 22.0f);
        DrawGalleryText(context, L"Buttons & Commands", bodyFormat, tRect, textBrush);
        y1 += 26.0f;

        const float btnW = std::min(110.0f, (colWidth - 3.0f * itemGap) / 4.0f);
        const float btnH = 30.0f;
        float bx = col1Left;

        DrawUiTextButton(context, D2D1::RectF(bx, y1, bx + btnW, y1 + btnH),
                         bodyFormat, L"Primary", /*primary=*/true, UiVisualState::Rest, theme, highContrast, &brushes);
        bx += btnW + itemGap;

        DrawUiTextButton(context, D2D1::RectF(bx, y1, bx + btnW, y1 + btnH),
                         bodyFormat, L"Hover", /*primary=*/true, UiVisualState::Hover, theme, highContrast, &brushes);
        bx += btnW + itemGap;

        DrawUiTextButton(context, D2D1::RectF(bx, y1, bx + btnW, y1 + btnH),
                         bodyFormat, L"Secondary", /*primary=*/false, UiVisualState::Rest, theme, highContrast, &brushes);
        bx += btnW + itemGap;

        DrawUiTextButton(context, D2D1::RectF(bx, y1, bx + btnW, y1 + btnH),
                         bodyFormat, L"Disabled", /*primary=*/false, UiVisualState::Disabled, theme, highContrast, &brushes);

        y1 += btnH + sectionGap;
    }

    // 2. Status Chips
    {
        D2D1_RECT_F tRect = D2D1::RectF(col1Left, y1, col1Right, y1 + 22.0f);
        DrawGalleryText(context, L"Semantic Status Chips", bodyFormat, tRect, textBrush);
        y1 += 26.0f;

        const float chipW = std::min(90.0f, (colWidth - 3.0f * itemGap) / 4.0f);
        const float chipH = 24.0f;
        float cx = col1Left;
        IDWriteTextFormat* chipFormat = captionFormat ? captionFormat : bodyFormat;

        DrawUiStatusChip(context, D2D1::RectF(cx, y1, cx + chipW, y1 + chipH), chipFormat, L"Active", UiChipKind::Active, theme, highContrast, &brushes);
        cx += chipW + itemGap;

        DrawUiStatusChip(context, D2D1::RectF(cx, y1, cx + chipW, y1 + chipH), chipFormat, L"Degraded", UiChipKind::Degraded, theme, highContrast, &brushes);
        cx += chipW + itemGap;

        DrawUiStatusChip(context, D2D1::RectF(cx, y1, cx + chipW, y1 + chipH), chipFormat, L"Error", UiChipKind::Error, theme, highContrast, &brushes);
        cx += chipW + itemGap;

        DrawUiStatusChip(context, D2D1::RectF(cx, y1, cx + chipW, y1 + chipH), chipFormat, L"Info", UiChipKind::Info, theme, highContrast, &brushes);

        y1 += chipH + sectionGap;
    }

    // 3. Navigation Items
    {
        D2D1_RECT_F tRect = D2D1::RectF(col1Left, y1, col1Right, y1 + 22.0f);
        DrawGalleryText(context, L"Navigation & Sidebar", bodyFormat, tRect, textBrush);
        y1 += 26.0f;

        const float navH = 32.0f;
        IDWriteTextFormat* glyphFmt = captionFormat ? captionFormat : bodyFormat;

        DrawUiNavItem(context, D2D1::RectF(col1Left, y1, col1Right, y1 + navH),
                      bodyFormat, L"Home", glyphFmt, L"\uE80F", /*active=*/true, UiVisualState::Rest, theme, highContrast, &brushes);
        y1 += navH + 4.0f;

        DrawUiNavItem(context, D2D1::RectF(col1Left, y1, col1Right, y1 + navH),
                      bodyFormat, L"Browse Folders", glyphFmt, L"\uE8B7", /*active=*/false, UiVisualState::Rest, theme, highContrast, &brushes);
        y1 += navH + 4.0f;

        DrawUiNavItem(context, D2D1::RectF(col1Left, y1, col1Right, y1 + navH),
                      bodyFormat, L"Fast Search", glyphFmt, L"\uE721", /*active=*/false, UiVisualState::Hover, theme, highContrast, &brushes);
        y1 += navH + sectionGap;
    }

    // 4. Progress Surfaces
    {
        D2D1_RECT_F tRect = D2D1::RectF(col1Left, y1, col1Right, y1 + 22.0f);
        DrawGalleryText(context, L"Progress & Operations", bodyFormat, tRect, textBrush);
        y1 += 26.0f;

        const float progH = 6.0f;
        DrawUiProgressSurface(context, D2D1::RectF(col1Left, y1, col1Right, y1 + progH), 0.65f, 0.0f, theme, highContrast, &brushes);
        y1 += progH + 10.0f;

        DrawUiProgressSurface(context, D2D1::RectF(col1Left, y1, col1Right, y1 + progH), -1.0f, 0.35f, theme, highContrast, &brushes);
        y1 += progH + sectionGap;
    }

    if (!twoColumn) {
        y2 = y1;
    }

    // --- Column 2: Rows & Selection, Drop Target, State Panel ---

    // 5. Data Rows & Selection
    {
        D2D1_RECT_F tRect = D2D1::RectF(col2Left, y2, col2Right, y2 + 22.0f);
        DrawGalleryText(context, L"Data Rows & Selection States", bodyFormat, tRect, textBrush);
        y2 += 26.0f;

        const float rowH = 28.0f;
        const float radius = UiMetrics::kRadiusSmall;

        // Row 1: Rest
        {
            D2D1_RECT_F rRect = D2D1::RectF(col2Left, y2, col2Right, y2 + rowH);
            DrawUiStateFill(context, rRect, ResolveUiRowPalette(theme, UiVisualState::Rest, highContrast), radius, &brushes);
            UiDrawTextAt(context, bodyFormat, L"Default row (Rest)", col2Left + 12.0f, y2, y2 + rowH, textBrush);
            y2 += rowH + 4.0f;
        }

        // Row 2: Hover
        {
            D2D1_RECT_F rRect = D2D1::RectF(col2Left, y2, col2Right, y2 + rowH);
            DrawUiStateFill(context, rRect, ResolveUiRowPalette(theme, UiVisualState::Hover, highContrast), radius, &brushes);
            UiDrawTextAt(context, bodyFormat, L"Hovered row (Hover)", col2Left + 12.0f, y2, y2 + rowH, textBrush);
            y2 += rowH + 4.0f;
        }

        // Row 3: Selected Focused
        {
            D2D1_RECT_F rRect = D2D1::RectF(col2Left, y2, col2Right, y2 + rowH);
            DrawUiStateFill(context, rRect, ResolveUiRowPalette(theme, UiVisualState::SelectedFocused, highContrast), radius, &brushes);
            DrawUiFocusIndicator(context, rRect, theme, highContrast, radius, &brushes);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> onAccentBrush;
            context->CreateSolidColorBrush(theme.textOnAccent, &onAccentBrush);
            UiDrawTextAt(context, bodyFormat, L"Active selection (Selected + Focused)", col2Left + 12.0f, y2, y2 + rowH, onAccentBrush.Get());
            y2 += rowH + 4.0f;
        }

        // Row 4: Selected Unfocused
        {
            D2D1_RECT_F rRect = D2D1::RectF(col2Left, y2, col2Right, y2 + rowH);
            DrawUiStateFill(context, rRect, ResolveUiRowPalette(theme, UiVisualState::SelectedUnfocused, highContrast), radius, &brushes);
            UiDrawTextAt(context, bodyFormat, L"Soft selection (Selected + Unfocused)", col2Left + 12.0f, y2, y2 + rowH, textBrush);
            y2 += rowH + sectionGap;
        }
    }

    // 6. Drop Target Affordance
    {
        D2D1_RECT_F tRect = D2D1::RectF(col2Left, y2, col2Right, y2 + 22.0f);
        DrawGalleryText(context, L"Drop Target Affordance", bodyFormat, tRect, textBrush);
        y2 += 26.0f;

        const float dropH = 48.0f;
        D2D1_RECT_F dropRect = D2D1::RectF(col2Left, y2, col2Right, y2 + dropH);
        DrawUiDropTargetEmphasis(context, dropRect, theme, highContrast, UiMetrics::kRadiusMedium, &brushes);
        UiDrawTextCentered(context, bodyFormat, L"Drop files here to copy or move", dropRect, textBrush);
        y2 += dropH + sectionGap;
    }

    // 7. State Panel (Empty State)
    {
        D2D1_RECT_F tRect = D2D1::RectF(col2Left, y2, col2Right, y2 + 22.0f);
        DrawGalleryText(context, L"State Panels & Placeholders", bodyFormat, tRect, textBrush);
        y2 += 26.0f;

        const float panelH = 64.0f;
        D2D1_RECT_F panelRect = D2D1::RectF(col2Left, y2, col2Right, y2 + panelH);
        DrawUiStatePanel(context, panelRect, bodyFormat, L"No Items Found",
                         captionFormat ? captionFormat : bodyFormat,
                         L"Try changing filters or searching another folder.",
                         theme, highContrast, &brushes);
        y2 += panelH + sectionGap;
    }

    // Footer note
    const float maxY = std::max(y1, y2);
    if (captionFormat && maxY + 20.0f < clientRect.bottom) {
        D2D1_RECT_F footerRect = D2D1::RectF(clientRect.left + padding, clientRect.bottom - 24.0f, clientRect.right - padding, clientRect.bottom - 4.0f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> secBrush;
        context->CreateSolidColorBrush(theme.textSecondary, &secBrush);
        DrawGalleryText(context, L"Gallery is debug-only and compiled out in production builds.", captionFormat, footerRect, secBrush.Get());
    }
}

int GalleryHitTest(D2D1_POINT_2F point, const D2D1_RECT_F& clientRect) {
    if (point.x < clientRect.left || point.x > clientRect.right ||
        point.y < clientRect.top || point.y > clientRect.bottom) {
        return -1;
    }

    const float padding = 24.0f;
    const float startY = clientRect.top + padding + 36.0f + 24.0f + 16.0f; // After header
    if (point.y < startY) {
        return -1; // In header area
    }

    const float availableWidth = (clientRect.right - clientRect.left) - 2.0f * padding;
    const bool twoColumn = availableWidth >= 900.0f;
    const float colGap = 24.0f;
    const float colWidth = twoColumn ? (availableWidth - colGap) * 0.5f : availableWidth;

    const float col1Left = clientRect.left + padding;
    const float col1Right = col1Left + colWidth;
    const float col2Left = twoColumn ? (col1Right + colGap) : col1Left;
    const float col2Right = col2Left + colWidth;

    if (point.x >= col1Left && point.x <= col1Right) {
        // In column 1
        float y = startY;
        if (point.y >= y && point.y < y + 60.0f) return 0; // Buttons
        y += 60.0f + 16.0f;
        if (point.y >= y && point.y < y + 54.0f) return 1; // Chips
        y += 54.0f + 16.0f;
        if (point.y >= y && point.y < y + 130.0f) return 2; // Nav
        y += 130.0f + 16.0f;
        if (point.y >= y && point.y < y + 60.0f) return 3; // Progress
    } else if (twoColumn && point.x >= col2Left && point.x <= col2Right) {
        // In column 2
        float y = startY;
        if (point.y >= y && point.y < y + 150.0f) return 4; // Rows & Selection
        y += 150.0f + 16.0f;
        if (point.y >= y && point.y < y + 80.0f) return 5; // Drop target
        y += 80.0f + 16.0f;
        if (point.y >= y && point.y < y + 90.0f) return 6; // State panel
    }

    return -1;
}

} // namespace ffui
