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
        // ui-overhaul phase 12: extended coverage so every shared command/state
        // component can be reviewed deterministically without launching the
        // product surfaces.
        {L"Icon Buttons & Focus", L"Command-family squares with an independent keyboard focus ring.", {L"Rest", L"Hover", L"Pressed", L"Focus", L"Disabled"}},
        {L"Tabs, Breadcrumb & Fields", L"Tab pills, breadcrumb segments, and the address/search field wells.", {L"Active Tab", L"Inactive Tab", L"Breadcrumb", L"Field"}},
        {L"Section Headers & Dividers", L"Section title + count hint over a quiet hairline divider.", {L"Section", L"Divider"}},
    };
}

std::vector<GallerySizeVariant> GallerySizeVariants() {
    // ui-overhaul phase 12: the single-column narrow variant stacks all ten
    // sections, so it needs a taller canvas than the two-column variants.
    return {
        {L"Narrow (800px)", 800.0f, 1120.0f},
        {L"Normal (1280px)", 1280.0f, 900.0f},
        {L"Wide (1600px)", 1600.0f, 900.0f},
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

    // 8. Icon Buttons & Focus (ui-overhaul phase 12)
    {
        D2D1_RECT_F tRect = D2D1::RectF(col2Left, y2, col2Right, y2 + 22.0f);
        DrawGalleryText(context, L"Icon Buttons & Focus", bodyFormat, tRect, textBrush);
        y2 += 26.0f;

        const float btn = 30.0f;
        const float stride = 56.0f;
        const UiVisualState states[] = {UiVisualState::Rest, UiVisualState::Hover, UiVisualState::Pressed,
                                        UiVisualState::Focused, UiVisualState::Disabled};
        const wchar_t* stateLabels[] = {L"Rest", L"Hover", L"Pressed", L"Focus", L"Disabled"};
        float bx = col2Left;
        for (int i = 0; i < 5; ++i) {
            const D2D1_RECT_F rect = D2D1::RectF(bx, y2, bx + btn, y2 + btn);
            const UiVisualState fillState = states[i] == UiVisualState::Focused ? UiVisualState::Hover : states[i];
            const UiRowPalette palette = ResolveUiRowPalette(theme, fillState, highContrast);
            DrawUiStateFill(context, rect, palette, UiMetrics::kRadiusSmall, &brushes);
            ID2D1Brush* glyph = textBrush;
            if (states[i] == UiVisualState::Disabled) {
                if (SUCCEEDED(UiEnsureSolidBrush(context,
                        highContrast ? ToD2DColor(GetSysColor(COLOR_GRAYTEXT)) : theme.textDisabled,
                        &brushes.text))) {
                    glyph = brushes.text.Get();
                }
            } else if (SUCCEEDED(UiEnsureSolidBrush(context, theme.text, &brushes.text))) {
                glyph = brushes.text.Get();
            }
            UiDrawTextCentered(context, bodyFormat, L"\u203A", rect, glyph);
            if (states[i] == UiVisualState::Focused) {
                DrawUiFocusIndicator(context, rect, theme, highContrast, UiMetrics::kRadiusSmall, &brushes);
            }
            DrawGalleryText(context, stateLabels[i], captionFormat ? captionFormat : bodyFormat,
                            D2D1::RectF(bx - 4.0f, y2 + btn + 2.0f, bx + stride, y2 + btn + 18.0f), textBrush);
            bx += stride;
        }
        y2 += btn + 24.0f + sectionGap;
    }

    // 9. Tabs, breadcrumb, and fields (ui-overhaul phase 12)
    {
        D2D1_RECT_F tRect = D2D1::RectF(col2Left, y2, col2Right, y2 + 22.0f);
        DrawGalleryText(context, L"Tabs, Breadcrumb & Fields", bodyFormat, tRect, textBrush);
        y2 += 26.0f;

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> secBrush;
        context->CreateSolidColorBrush(theme.textSecondary, &secBrush);
        // Active tab: subtle lift plus the accent underline; inactive tab is
        // background with secondary text so the pair reads at a glance.
        const D2D1_RECT_F activeTab = D2D1::RectF(col2Left, y2, col2Left + 176.0f, y2 + 30.0f);
        if (SUCCEEDED(UiEnsureSolidBrush(context, theme.surfaceSubtle, &brushes.fill))) {
            UiFillRoundedRect(context, activeTab, brushes.fill.Get(), UiMetrics::kRadiusMedium);
        }
        if (SUCCEEDED(UiEnsureSolidBrush(context, theme.accent, &brushes.stroke))) {
            UiFillRoundedRect(context, D2D1::RectF(activeTab.left + 8.0f, activeTab.bottom - 3.0f,
                                                   activeTab.right - 8.0f, activeTab.bottom - 1.0f),
                              brushes.stroke.Get(), 1.0f);
        }
        UiDrawTextCentered(context, bodyFormat, L"1: Documents", activeTab, textBrush);
        const D2D1_RECT_F inactiveTab = D2D1::RectF(activeTab.right + 6.0f, y2, activeTab.right + 166.0f, y2 + 30.0f);
        UiDrawTextCentered(context, bodyFormat, L"2: Downloads", inactiveTab,
                           secBrush ? secBrush.Get() : textBrush);
        y2 += 38.0f;

        // Breadcrumb: quiet ancestors in accent, the current location in text,
        // then the address/search field well with its focus stroke.
        float crumbX = col2Left;
        const auto drawCrumb = [&](const wchar_t* text, ID2D1Brush* brush) {
            const float width = static_cast<float>(wcslen(text)) * 7.2f + 8.0f;
            UiDrawTextCentered(context, bodyFormat, text,
                               D2D1::RectF(crumbX, y2, crumbX + width, y2 + 24.0f), brush);
            crumbX += width;
        };
        ID2D1Brush* accentBrush = textBrush;
        if (SUCCEEDED(UiEnsureSolidBrush(context, theme.accent, &brushes.stroke))) accentBrush = brushes.stroke.Get();
        drawCrumb(L"This PC", accentBrush);
        drawCrumb(L"\u203A", secBrush ? secBrush.Get() : textBrush);
        drawCrumb(L"Documents", textBrush);
        const D2D1_RECT_F field = D2D1::RectF(col2Left + 300.0f, y2, col2Right, y2 + 28.0f);
        if (SUCCEEDED(UiEnsureSolidBrush(context, theme.surfaceElevated, &brushes.fill))) {
            UiFillRoundedRect(context, field, brushes.fill.Get(), UiMetrics::kRadiusField);
        }
        if (SUCCEEDED(UiEnsureSolidBrush(context, theme.focusStroke, &brushes.text))) {
            UiDrawRoundedRect(context, field, brushes.text.Get(), UiMetrics::kRadiusField, 1.0f);
        }
        UiDrawTextAt(context, bodyFormat, L"Search browsed folders", field.left + 10.0f, field.top,
                     field.bottom, secBrush ? secBrush.Get() : textBrush);
        y2 += 36.0f + sectionGap;
    }

    // 10. Section headers and dividers (ui-overhaul phase 12)
    {
        D2D1_RECT_F tRect = D2D1::RectF(col2Left, y2, col2Right, y2 + 22.0f);
        DrawGalleryText(context, L"Section Headers & Dividers", bodyFormat, tRect, textBrush);
        y2 += 28.0f;
        DrawGalleryText(context, L"Recent folders", titleFormat,
                        D2D1::RectF(col2Left, y2, col2Right, y2 + 26.0f), textBrush);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hintBrush;
        context->CreateSolidColorBrush(theme.textSecondary, &hintBrush);
        if (captionFormat) {
            DrawGalleryText(context, L"12 items", captionFormat,
                            D2D1::RectF(col2Left, y2 + 24.0f, col2Right, y2 + 40.0f),
                            hintBrush ? hintBrush.Get() : textBrush);
        }
        if (SUCCEEDED(UiEnsureSolidBrush(context, theme.dividerSubtle, &brushes.stroke))) {
            context->DrawLine(D2D1::Point2F(col2Left, y2 + 46.0f),
                              D2D1::Point2F(col2Right, y2 + 46.0f), brushes.stroke.Get(), 1.0f);
        }
        y2 += 56.0f + sectionGap;
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
        // ui-overhaul phase 12 extended sections follow in the same column.
        y += 90.0f + 16.0f;
        if (point.y >= y && point.y < y + 80.0f) return 7; // Icon buttons & focus
        y += 80.0f + 16.0f;
        if (point.y >= y && point.y < y + 80.0f) return 8; // Tabs/breadcrumb/fields
        y += 80.0f + 16.0f;
        if (point.y >= y && point.y < y + 60.0f) return 9; // Section headers
    }

    return -1;
}

} // namespace ffui
