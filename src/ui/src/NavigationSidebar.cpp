#include "NavigationSidebar.h"
#include "UiComponents.h"
#include "QuickActions.h"
#include "ffui/DisplayPath.h"
#include "ffui/SidebarDestinations.h"

#include <algorithm>
#include <commctrl.h>
#include <cwchar>
#include <windowsx.h>

namespace ffui {
namespace {
constexpr wchar_t kWindowClass[] = L"FastFilesNavigationSidebar";
constexpr int kExpandedWidth = 220;  // DIP
constexpr int kCollapsedWidth = 32;  // DIP
// DIP offset of the sidebar top inside the owner client; equals
// UiMetrics::kChromeHeight (kept local so the chrome layer owns its token).
constexpr int kChromeHeight = 72;
constexpr UINT kBookmarkRename = 1;
constexpr UINT kBookmarkRemove = 2;
constexpr UINT kBookmarkMoveUp = 3;
constexpr UINT kBookmarkMoveDown = 4;
// ui-v3 sidebar: recovery command for an unavailable known-folder row.
constexpr UINT kKnownFolderRetry = 5;
// ui-v3 sidebar: collapsed-rail tooltip ids live outside the row-id space.
constexpr UINT_PTR kToolRailHome = 0x10000;
constexpr UINT_PTR kToolRailSearch = 0x10002;
constexpr UINT_PTR kToolRailStorage = 0x10003;
constexpr UINT_PTR kToolRailExpand = 0x10001;
// ui-v3 sidebar: Segoe MDL2 Assets glyphs -- the same family NavigationChrome
// and ColumnView already standardize on for command/navigation icons.
constexpr wchar_t kGlyphHome = L'\uE80F';
constexpr wchar_t kGlyphFolder = L'\uE8B7';
constexpr wchar_t kGlyphSearch = L'\uE721';
constexpr wchar_t kGlyphStorage = L'\uE9D2';

// Scale a DIP metric to physical pixels for Win32 control layout. Exact
// int/float overloads so theme-token floats and literal ints never convert
// implicitly at /W4.
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

// ui-v3 sidebar: GDI mirror of ffui::ResolveUiRowPalette so every nav entry
// shares one rest/hover/pressed/focused/selected/disabled grammar. High
// Contrast falls back to system colors; translucent overlays composite over
// the sidebar background.
struct NavPaint {
    COLORREF fill = 0;
    bool hasFill = false;
    COLORREF text = 0;
    COLORREF ring = 0;
    bool hasRing = false;
};

NavPaint ResolveNavPaint(const UiTheme& theme, UiVisualState state, bool highContrast, COLORREF background) {
    NavPaint paint{};
    paint.text = highContrast ? GetSysColor(COLOR_WINDOWTEXT) : ToColorRef(theme.text);
    if (highContrast) {
        // Accessibility fallback: system highlight/menu colors, never token
        // overlays, so the user's contrast scheme always wins.
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
            case UiVisualState::Disabled:
                paint.text = GetSysColor(COLOR_GRAYTEXT);
                break;
            case UiVisualState::Focused:
                paint.ring = GetSysColor(COLOR_WINDOWTEXT);
                paint.hasRing = true;
                break;
            default:
                break;
        }
        return paint;
    }
    switch (state) {
        case UiVisualState::Hover:
            paint.fill = CompositeOver(theme.hoverOverlay, background);
            paint.hasFill = true;
            break;
        case UiVisualState::Pressed:
            // Pressed is immediately stronger than hover.
            paint.fill = CompositeOver(theme.pressOverlay, background);
            paint.hasFill = true;
            break;
        case UiVisualState::SelectedUnfocused:
            // Active-destination treatment: soft fill + accent leading bar,
            // quieter than the accent-filled selection used for files.
            paint.fill = CompositeOver(theme.selectionSoft, background);
            paint.hasFill = true;
            break;
        case UiVisualState::SelectedFocused:
            paint.fill = ToColorRef(theme.accent);
            paint.hasFill = true;
            paint.text = ToColorRef(theme.textOnAccent);
            break;
        case UiVisualState::Disabled:
            paint.text = ToColorRef(theme.textDisabled);
            break;
        case UiVisualState::Focused:
            paint.ring = ToColorRef(theme.focusStroke);
            paint.hasRing = true;
            break;
        case UiVisualState::DropTarget:
            paint.fill = CompositeOver(theme.selectionSoft, background);
            paint.hasFill = true;
            break;
        case UiVisualState::Busy:
        case UiVisualState::Rest:
            break;
    }
    return paint;
}

void DrawNavText(HDC dc, const std::wstring& text, RECT rect, HFONT font, COLORREF color) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    HFONT previous = font != nullptr ? static_cast<HFONT>(SelectObject(dc, font)) : nullptr;
    DrawTextW(dc, text.c_str(), -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (previous != nullptr) SelectObject(dc, previous);
}

// Quiet disclosure chevron drawn with lines so it never depends on glyph-font
// availability. expanded=false yields the right-pointing variant.
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

// ui-v3 sidebar: non-color "unavailable" cue -- a slashed circle in the icon
// slot, paired with disabled text so meaning never rides on color alone.
void DrawUnavailableBadge(HDC dc, const RECT& slot, COLORREF color) {
    const int cx = (slot.left + slot.right) / 2;
    const int cy = (slot.top + slot.bottom) / 2;
    const int radius = (std::max)(3, static_cast<int>((std::min)(slot.right - slot.left,
                                                                 slot.bottom - slot.top)) / 2 -
                                       Scaled(UiMetrics::kSpaceXxs));
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    if (pen == nullptr) return;
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
    Ellipse(dc, cx - radius, cy - radius, cx + radius, cy + radius);
    const int diagonal = radius * 7 / 10;
    MoveToEx(dc, cx - diagonal, cy - diagonal, nullptr);
    LineTo(dc, cx + diagonal, cy + diagonal);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

// Inset keyboard-focus outline, independent of hover/selection (the GDI twin
// of ffui::DrawUiFocusIndicator).
void DrawFocusRing(HDC dc, const RECT& rect, int radius, COLORREF color) {
    const int inset = Scaled(UiMetrics::kSpaceXxs) + 1;
    const RECT ring{rect.left + inset, rect.top + inset, rect.right - inset, rect.bottom - inset};
    const int width = ring.right - ring.left;
    const int height = ring.bottom - ring.top;
    if (width <= 0 || height <= 0) return;
    radius = std::clamp(radius, 0, std::min(width, height) / 2);
    HPEN pen = CreatePen(PS_SOLID, (std::max)(1, Scaled(2)), color);
    if (pen == nullptr) return;
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
    RoundRect(dc, ring.left, ring.top, ring.right + 1, ring.bottom + 1, radius * 2, radius * 2);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

// ui-v3 sidebar: collapsed-rail hit/paint geometry. Buttons meet the
// kTargetMinSize pointer-target floor.
struct RailLayout {
    RECT home{};
    RECT search{};
    RECT storage{};
    RECT expand{};
};

RailLayout ComputeRailLayout(const RECT& client) {
    const int side = Scaled(UiMetrics::kTargetMinSize);
    const int gap = Scaled(UiMetrics::kSpaceXs);
    const int x = (std::max)(0, static_cast<int>(((client.right - client.left) - side) / 2));
    int y = Scaled(UiMetrics::kSpaceS);
    RailLayout rail;
    rail.home = RECT{x, y, x + side, y + side};
    y += side + gap;
    rail.search = RECT{x, y, x + side, y + side};
    y += side + gap;
    rail.storage = RECT{x, y, x + side, y + side};
    y += side + gap;
    rail.expand = RECT{x, y, x + side, y + side};
    return rail;
}
} // namespace

bool NavigationSidebar::Initialize(HWND owner, NavigationWorkspace* workspace, NavigateHandler navigate) {
    owner_ = owner;
    workspace_ = workspace;
    navigate_ = std::move(navigate);
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = &NavigationSidebar::WindowProcThunk;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    windowClass.lpszClassName = kWindowClass;
    RegisterClassW(&windowClass);
    window_ = CreateWindowExW(0, kWindowClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                              0, Scaled(kChromeHeight), Scaled(kExpandedWidth), 0, owner_, nullptr,
                              windowClass.hInstance, this);
    if (!window_) return false;
    // ui-v3 sidebar: name the surface so accessibility hierarchies can label it.
    SetWindowTextW(window_, L"Navigation");
    EnsureFonts();
    EnsureTooltip();
    RebuildRows();
    Reposition();
    return true;
}

int NavigationSidebar::Width() const {
    return workspace_ != nullptr && workspace_->State().sidebarCollapsed ? kCollapsedWidth : kExpandedWidth;
}

void NavigationSidebar::Reposition() {
    if (!window_) return;
    EnsureFonts();  // no-op unless the DPI scale changed
    toolsDirty_ = true;
    RECT client{};
    GetClientRect(owner_, &client);
    const int height = (std::max)(0L, client.bottom - Scaled(kChromeHeight));
    SetWindowPos(window_, HWND_TOP, 0, Scaled(kChromeHeight), Scaled(Width()), height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(window_, nullptr, FALSE);
}

void NavigationSidebar::Refresh() {
    if (!window_) return;
    RebuildRows();
    Reposition();
    InvalidateRect(window_, nullptr, FALSE);
}

void NavigationSidebar::SetDarkTheme(bool dark) {
    darkTheme_ = dark;
    if (window_) InvalidateRect(window_, nullptr, FALSE);
}

void NavigationSidebar::SetBookmarksChangedCallback(std::function<void()> cb) {
    bookmarksChanged_ = std::move(cb);
}

void NavigationSidebar::SetFocusedRow(int row) {
    if (row >= static_cast<int>(rows_.size())) row = -1;
    if (!window_ || focusedRow_ == row) return;
    focusedRow_ = row;
    InvalidateRect(window_, nullptr, FALSE);
}

void NavigationSidebar::SetActiveWorkspace(ActiveWorkspace workspace) {
    if (activeWorkspace_ == workspace) return;
    activeWorkspace_ = workspace;
    if (window_) InvalidateRect(window_, nullptr, FALSE);
}

int NavigationSidebar::ItemHeightPx() const {
    return Scaled(GetUiDensity(gUiDensityProfile).rowHeight);
}

int NavigationSidebar::SectionHeightPx() const {
    return Scaled(UiMetrics::kControlHeight);
}

int NavigationSidebar::HitTestRow(const POINT& point) const {
    for (size_t index = 0; index < rows_.size(); ++index) {
        if (PtInRect(&rows_[index].bounds, point)) return static_cast<int>(index);
    }
    return -1;
}

bool NavigationSidebar::IsRowCurrent(const Row& row) const {
    if (!workspace_) return false;
    if (activeWorkspace_ == ActiveWorkspace::Search) return row.search;
    if (activeWorkspace_ == ActiveWorkspace::Storage) return row.storage;
    // quick-access-home 5.1: the Home row is current whenever the active
    // context is Home (its path is empty, so the path match below would
    // otherwise never select it).
    if (row.home) return workspace_->ActiveContext().isHomeLocation;
    if (row.search || row.storage) return false;
    if (row.path.empty()) return false;
    const auto normalized = [](const std::wstring& value) -> std::wstring {
        std::wstring result = value;
        std::replace(result.begin(), result.end(), L'/', L'\\');
        while (!result.empty() && result.back() == L'\\') result.pop_back();
        return result;
    };
    const std::wstring current = normalized(workspace_->ActiveContext().currentPath);
    const std::wstring item = normalized(row.path);
    if (current == item) return true;
    return current.size() > item.size() && current.compare(0, item.size(), item) == 0 &&
           current[item.size()] == L'\\';
}

void NavigationSidebar::ActivateRow(int index, bool openInNewTab) {
    if (index < 0 || index >= static_cast<int>(rows_.size())) return;
    const Row& row = rows_[static_cast<size_t>(index)];
    if (row.kind != RowKind::Item || !navigate_) return;
    navigate_({row.path, row.label, openInNewTab, row.home, row.search, row.storage});
    InvalidateRect(window_, nullptr, FALSE);
}

int NavigationSidebar::NextItemRow(int current, int direction) const {
    const int count = static_cast<int>(rows_.size());
    if (count == 0) return -1;
    int index = current;
    for (int step = 0; step < count; ++step) {
        index += direction;
        if (index < 0) index = count - 1;
        if (index >= count) index = 0;
        if (rows_[static_cast<size_t>(index)].kind == RowKind::Item) return index;
    }
    return -1;
}

void NavigationSidebar::RebuildRows() {
    rows_.clear();
    hoveredRow_ = -1;
    pressedRow_ = -1;
    if (!workspace_ || workspace_->State().sidebarCollapsed) return;
    const auto& state = workspace_->State();
    SidebarDestinationInput input;
    input.drivesCollapsed = state.drivesCollapsed;
    input.knownFoldersCollapsed = state.knownFoldersCollapsed;
    input.bookmarksCollapsed = state.bookmarksCollapsed;
    for (const auto& path : NavigationWorkspace::EnumerateDrives()) input.drives.push_back({path, path});
    for (const auto& folder : workspace_->EnumerateKnownFolders()) {
        input.knownFolders.push_back({folder.path, folder.displayName});
    }
    for (const auto& bookmark : state.bookmarks) {
        input.bookmarks.push_back({bookmark.path, bookmark.displayName});
    }
    const auto destinations = BuildSidebarDestinations(input);
    size_t driveIndex = 0;
    size_t knownIndex = 0;
    size_t bookmarkIndex = 0;
    for (const auto& destination : destinations) {
        Row row{};
        row.label = destination.label;
        row.path = destination.path;
        row.home = destination.kind == SidebarDestinationKind::Home;
        row.search = destination.kind == SidebarDestinationKind::Search;
        row.storage = destination.kind == SidebarDestinationKind::Storage;
        if (destination.kind == SidebarDestinationKind::Section) {
            row.kind = RowKind::Section;
            if (destination.label == L"Drives") row.section = 0;
            else if (destination.label == L"Known Folders") row.section = 1;
            else row.section = 2;
        } else {
            row.kind = RowKind::Item;
            row.section = row.home || row.search || row.storage ? -1
                          : destination.kind == SidebarDestinationKind::Drive        ? 0
                          : destination.kind == SidebarDestinationKind::KnownFolder ? 1
                                                                                    : 2;
            if (destination.kind == SidebarDestinationKind::Drive) row.item = driveIndex++;
            else if (destination.kind == SidebarDestinationKind::KnownFolder) row.item = knownIndex++;
            else if (destination.kind == SidebarDestinationKind::Bookmark) row.item = bookmarkIndex++;
        }
        rows_.push_back(std::move(row));
    }
}

void NavigationSidebar::EnsureFonts() {
    const float scale = UiDpiScale();
    if (bodyFont_ != nullptr && scale == fontScale_) return;
    ReleaseFonts();
    fontScale_ = scale;
    // ui-v3 sidebar: typography from the shared ramp -- Body labels, Caption
    // section headers (docs/ui-improvement/02 §3).
    bodyFont_ = CreateFontW(-Scaled(UiTypeSize(UiTypeRole::Body)), 0, 0, 0, FW_NORMAL,
                            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    captionFont_ = CreateFontW(-Scaled(UiTypeSize(UiTypeRole::Caption)), 0, 0, 0, FW_NORMAL,
                               FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    glyphFont_ = CreateFontW(-Scaled(UiMetrics::kFontSizeCaption), 0, 0, 0, FW_NORMAL,
                             FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe MDL2 Assets");
    // Verify the glyph font actually mapped (font fallback would render tofu).
    glyphsAvailable_ = false;
    if (glyphFont_ != nullptr && window_ != nullptr) {
        HDC dc = GetDC(window_);
        if (dc != nullptr) {
            HFONT previous = static_cast<HFONT>(SelectObject(dc, glyphFont_));
            wchar_t face[LF_FACESIZE] = {};
            GetTextFaceW(dc, LF_FACESIZE, face);
            SelectObject(dc, previous);
            ReleaseDC(window_, dc);
            glyphsAvailable_ = wcsncmp(face, L"Segoe MDL2", 10) == 0 || wcsncmp(face, L"Segoe Fluent", 12) == 0;
        }
    }
    // ui-v3 hardening: the face-name check alone is not sufficient — some
    // Windows builds resolve "Segoe MDL2 Assets" yet lack individual
    // codepoints (observed: E8B7 Folder tofu while E80F Home renders), which
    // painted raw boxes on every folder/drive row. Verify each glyph we draw
    // and degrade to icon-less rows instead of tofu.
    glyphsHome_ = glyphsAvailable_ && GlyphMapped(kGlyphHome);
    glyphsFolder_ = glyphsAvailable_ && GlyphMapped(kGlyphFolder);
    glyphsSearch_ = glyphsAvailable_ && GlyphMapped(kGlyphSearch);
    glyphsStorage_ = glyphsAvailable_ && GlyphMapped(kGlyphStorage);
    if (tooltip_ != nullptr) SendMessageW(tooltip_, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont_), FALSE);
}

bool NavigationSidebar::GlyphMapped(wchar_t glyph) {
    // Returns false both for unmapped codepoints and for codepoints the font
    // funnels into its shared .notdef/fallback box: some Windows builds report
    // legacy MDL2 codepoints as present yet rasterize them as a hollow
    // rectangle. Comparing against the index of a guaranteed-unassigned
    // private-use sentinel catches that case.
    auto indexOf = [this](wchar_t code) -> WORD {
        if (glyphFont_ == nullptr || window_ == nullptr) return 0xFFFF;
        HDC dc = GetDC(window_);
        if (dc == nullptr) return 0xFFFF;
        HFONT previous = static_cast<HFONT>(SelectObject(dc, glyphFont_));
        WORD index = 0xFFFF;
        GetGlyphIndicesW(dc, &code, 1, &index, GGI_MARK_NONEXISTING_GLYPHS);
        SelectObject(dc, previous);
        ReleaseDC(window_, dc);
        return index;
    };
    const WORD index = indexOf(glyph);
    if (index == 0xFFFF || index == 0) return false;
    return index != indexOf(L'');
}

void NavigationSidebar::ReleaseFonts() {
    if (bodyFont_ != nullptr) {
        DeleteObject(bodyFont_);
        bodyFont_ = nullptr;
    }
    if (captionFont_ != nullptr) {
        DeleteObject(captionFont_);
        captionFont_ = nullptr;
    }
    if (glyphFont_ != nullptr) {
        DeleteObject(glyphFont_);
        glyphFont_ = nullptr;
    }
    glyphsAvailable_ = false;
}

void NavigationSidebar::EnsureTooltip() {
    if (tooltip_ != nullptr || window_ == nullptr) return;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    tooltip_ = CreateWindowExW(0, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX,
                               0, 0, 0, 0, window_, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (tooltip_ == nullptr) return;
    SendMessageW(tooltip_, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont_), FALSE);
    // Keep long paths inside a readable column; they wrap instead of running
    // off-screen. (TTM_SETMAXWIDTH predates some SDK guard sets; provide the
    // raw message value when the header does not define it.)
#ifndef TTM_SETMAXWIDTH
#define TTM_SETMAXWIDTH (WM_USER + 25)
#endif
    SendMessageW(tooltip_, TTM_SETMAXWIDTH, 0, Scaled(280));
}

void NavigationSidebar::RefreshTools(const RECT& client) {
    if (tooltip_ == nullptr) return;
    for (const UINT_PTR id : tooltipIds_) {
        TOOLINFOW tool{};
        tool.cbSize = sizeof(tool);
        tool.hwnd = window_;
        tool.uId = id;
        SendMessageW(tooltip_, TTM_DELTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    }
    tooltipIds_.clear();
    const auto addTool = [this](UINT_PTR id, const RECT& rect) {
        TOOLINFOW tool{};
        tool.cbSize = sizeof(tool);
        tool.uFlags = TTF_SUBCLASS;
        tool.hwnd = window_;
        tool.uId = id;
        tool.rect = rect;
        tool.lpszText = LPSTR_TEXTCALLBACKW;
        if (SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool)) != 0) {
            tooltipIds_.push_back(id);
        }
    };
    const bool collapsed = workspace_ != nullptr && workspace_->State().sidebarCollapsed;
    if (collapsed) {
        const RailLayout rail = ComputeRailLayout(client);
        addTool(kToolRailHome, rail.home);
        addTool(kToolRailSearch, rail.search);
        addTool(kToolRailStorage, rail.storage);
        addTool(kToolRailExpand, rail.expand);
    } else {
        for (size_t index = 0; index < rows_.size(); ++index) {
            if (rows_[index].kind == RowKind::Item) addTool(index, rows_[index].bounds);
        }
    }
}

void NavigationSidebar::ToggleSection(int section) {
    if (!workspace_) return;
    auto& state = workspace_->State();
    if (section == 0) state.drivesCollapsed = !state.drivesCollapsed;
    else if (section == 1) state.knownFoldersCollapsed = !state.knownFoldersCollapsed;
    else state.bookmarksCollapsed = !state.bookmarksCollapsed;
    workspace_->MarkStateDirty();
    Refresh();
}

void NavigationSidebar::DrawRail(HDC dc, const RECT& client, const UiTheme& theme, bool highContrast,
                                 COLORREF background) {
    const RailLayout rail = ComputeRailLayout(client);
    const int radius = Scaled(UiMetrics::kRadiusSmall);
    const RECT* rects[4] = {&rail.home, &rail.search, &rail.storage, &rail.expand};
    const RailHit hits[4] = {RailHit::Home, RailHit::Search, RailHit::Storage, RailHit::Expand};
    const wchar_t glyphs[4] = {kGlyphHome, kGlyphSearch, kGlyphStorage, 0};
    const bool mapped[4] = {glyphsHome_, glyphsSearch_, glyphsStorage_, false};
    for (int button = 0; button < 4; ++button) {
        UiVisualState state = UiVisualState::Rest;
        if (pressedRail_ == hits[button]) state = UiVisualState::Pressed;
        else if (hoveredRail_ == hits[button]) state = UiVisualState::Hover;
        else if ((hits[button] == RailHit::Search && activeWorkspace_ == ActiveWorkspace::Search) ||
                 (hits[button] == RailHit::Storage && activeWorkspace_ == ActiveWorkspace::Storage) ||
                 (hits[button] == RailHit::Home && activeWorkspace_ != ActiveWorkspace::Search &&
                  activeWorkspace_ != ActiveWorkspace::Storage && workspace_ &&
                  workspace_->ActiveContext().isHomeLocation)) {
            state = UiVisualState::SelectedUnfocused;
        }
        const NavPaint paint = ResolveNavPaint(theme, state, highContrast, background);
        if (paint.hasFill) UiGdiRoundedRect(dc, *rects[button], paint.fill, radius);
        if (hits[button] == RailHit::Expand) {
            const POINT center{(rects[button]->left + rects[button]->right) / 2,
                               (rects[button]->top + rects[button]->bottom) / 2};
            DrawChevron(dc, center, Scaled(4), /*expanded=*/false, paint.text);
        } else if (mapped[button]) {
            if (HFONT previous = static_cast<HFONT>(SelectObject(dc, glyphFont_))) {
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, paint.text);
                RECT glyphRect = *rects[button];
                DrawTextW(dc, &glyphs[button], 1, &glyphRect, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
                SelectObject(dc, previous);
            }
        }
    }
}

void NavigationSidebar::Draw(HDC dc, const RECT& client) {
    const UiTheme theme = GetUiTheme(darkTheme_);
    const bool highContrast = UiSystemHighContrast();
    const COLORREF background = highContrast ? GetSysColor(COLOR_WINDOW) : ToColorRef(theme.background);
    HBRUSH bgBrush = CreateSolidBrush(background);
    if (bgBrush) {
        FillRect(dc, &client, bgBrush);
        DeleteObject(bgBrush);
    }
    if (!workspace_) return;
    if (workspace_->State().sidebarCollapsed) {
        DrawRail(dc, client, theme, highContrast, background);
        return;
    }

    const COLORREF textSecondary =
        highContrast ? GetSysColor(COLOR_GRAYTEXT) : ToColorRef(theme.textSecondary);
    const COLORREF accentColor = ToColorRef(theme.accent);
    const int pillRadius = Scaled(UiMetrics::kRadiusSmall);
    const int pillInsetX = Scaled(UiMetrics::kSpaceXs);
    const int pillInsetY = Scaled(UiMetrics::kSpaceXxs);
    const int iconSlot = Scaled(UiMetrics::kIconSizeLg);

    int y = 0;
    for (size_t index = 0; index < rows_.size(); ++index) {
        Row& row = rows_[index];
        const bool isSection = row.kind == RowKind::Section;
        // ui-v3 sidebar: sections group through spacing + typography, not
        // painted bands -- one small gap above each header separates groups.
        if (isSection && index != 0) y += Scaled(UiMetrics::kSpaceS);
        row.bounds = {0, y, client.right, y + (isSection ? SectionHeightPx() : ItemHeightPx())};
        RECT pill = row.bounds;
        pill.left += pillInsetX;
        pill.right -= pillInsetX;
        pill.top += pillInsetY;
        pill.bottom -= pillInsetY;

        if (isSection) {
            if (static_cast<int>(index) == hoveredRow_) {
                const NavPaint hover = ResolveNavPaint(theme, UiVisualState::Hover, highContrast, background);
                if (hover.hasFill) UiGdiRoundedRect(dc, pill, hover.fill, pillRadius);
            }
            const auto& state = workspace_->State();
            const bool expanded = row.section == 0 ? !state.drivesCollapsed
                                  : row.section == 1 ? !state.knownFoldersCollapsed
                                                     : !state.bookmarksCollapsed;
            const int contentLeft = pill.left + Scaled(UiMetrics::kSpaceS);
            DrawChevron(dc, {contentLeft + Scaled(4), (row.bounds.top + row.bounds.bottom) / 2}, Scaled(4),
                        expanded, textSecondary);
            DrawNavText(dc, row.label,
                        {contentLeft + Scaled(16), row.bounds.top, row.bounds.right - Scaled(UiMetrics::kSpaceS),
                         row.bounds.bottom},
                        captionFont_, textSecondary);
        } else {
            // quick-access-home 5.1: the Home row has an empty path but is a
            // real, navigable destination -- never render it unavailable.
            const bool unavailable = row.path.empty() && !row.home && !row.search && !row.storage;
            const bool pressed = static_cast<int>(index) == pressedRow_;
            const bool hovered = static_cast<int>(index) == hoveredRow_;
            const bool selected = IsRowCurrent(row);
            // Pressed beats hover beats selection for the fill, matching
            // DrawUiNavItem's fill-state precedence; hover therefore never
            // looks identical to the selected treatment.
            UiVisualState fillState = UiVisualState::Rest;
            if (unavailable) fillState = UiVisualState::Disabled;
            else if (pressed) fillState = UiVisualState::Pressed;
            else if (hovered) fillState = UiVisualState::Hover;
            else if (selected) fillState = UiVisualState::SelectedUnfocused;
            const NavPaint paint = ResolveNavPaint(theme, fillState, highContrast, background);
            if (paint.hasFill) UiGdiRoundedRect(dc, pill, paint.fill, pillRadius);
            if (selected && !highContrast) {
                // Accent location marker: narrow rounded bar, matching
                // ffui::DrawUiNavItem's active treatment.
                const int barLeft = pill.left + Scaled(UiMetrics::kSpaceS);
                const RECT bar{barLeft, pill.top + Scaled(UiMetrics::kSpaceXs), barLeft + Scaled(3),
                               pill.bottom - Scaled(UiMetrics::kSpaceXs)};
                UiGdiRoundedRect(dc, bar, accentColor, Scaled(1));
            }
            // Fixed icon slot keeps labels aligned across items whether or not
            // a glyph is available.
            const int slotLeft = pill.left + Scaled(UiMetrics::kSpaceS + UiMetrics::kSpaceXs);
            const RECT slot{slotLeft, row.bounds.top, slotLeft + iconSlot, row.bounds.bottom};
            if (unavailable) {
                DrawUnavailableBadge(dc, slot, paint.text);
            } else {
                // ui-v3 hardening: per-glyph coverage guard — draw only glyphs
                // this font actually maps; never tofu.
                const wchar_t glyph = row.home      ? kGlyphHome
                                      : row.search  ? kGlyphSearch
                                      : row.storage ? kGlyphStorage
                                                    : kGlyphFolder;
                const bool mapped = row.home      ? glyphsHome_
                                    : row.search  ? glyphsSearch_
                                    : row.storage ? glyphsStorage_
                                                  : glyphsFolder_;
                HFONT previous =
                    mapped ? static_cast<HFONT>(SelectObject(dc, glyphFont_)) : nullptr;
                if (previous != nullptr) {
                    const COLORREF glyphColor = selected ? paint.text : textSecondary;
                    SetBkMode(dc, TRANSPARENT);
                    SetTextColor(dc, glyphColor);
                    RECT glyphRect = slot;
                    DrawTextW(dc, &glyph, 1, &glyphRect, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
                    SelectObject(dc, previous);
                }
            }
            const int labelLeft = slot.right + Scaled(UiMetrics::kSpaceXs);
            const std::wstring displayLabel =
                row.path.empty() ? row.label
                                 : ffui::NormalizePathDerivedDisplayLabel(row.label, row.path);
            DrawNavText(dc, displayLabel,
                        {labelLeft, row.bounds.top, row.bounds.right - Scaled(UiMetrics::kSpaceS),
                         row.bounds.bottom},
                        bodyFont_, paint.text);
            // Keyboard-focus ring is independent of hover and selection.
            if (static_cast<int>(index) == focusedRow_) {
                const NavPaint focus = ResolveNavPaint(theme, UiVisualState::Focused, highContrast, background);
                if (focus.hasRing) DrawFocusRing(dc, pill, pillRadius, focus.ring);
            }
        }
        y += isSection ? SectionHeightPx() : ItemHeightPx();
    }
}

NavigationSidebar::RailHit NavigationSidebar::HitTestRail(const POINT& point, const RECT& client) const {
    const RailLayout rail = ComputeRailLayout(client);
    if (PtInRect(&rail.home, point)) return RailHit::Home;
    if (PtInRect(&rail.search, point)) return RailHit::Search;
    if (PtInRect(&rail.storage, point)) return RailHit::Storage;
    if (PtInRect(&rail.expand, point)) return RailHit::Expand;
    return RailHit::None;
}

LRESULT CALLBACK NavigationSidebar::WindowProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    NavigationSidebar* self = nullptr;
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<NavigationSidebar*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<NavigationSidebar*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->HandleMessage(hwnd, message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT NavigationSidebar::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_ERASEBKGND:
            // The paint path fills the whole client; skipping the class-brush
            // erase removes hover flicker.
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            RECT client{};
            GetClientRect(hwnd, &client);
            Draw(dc, client);
            EndPaint(hwnd, &paint);
            // Tool rects are only known after a layout pass; refresh lazily.
            if (toolsDirty_) {
                RefreshTools(client);
                toolsDirty_ = false;
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!workspace_) return 0;
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (workspace_->State().sidebarCollapsed) {
                RECT client{};
                GetClientRect(hwnd, &client);
                const RailHit hit = HitTestRail(point, client);
                if (hit != hoveredRail_) {
                    hoveredRail_ = hit;
                    InvalidateRect(window_, nullptr, FALSE);
                }
            } else {
                const int row = HitTestRow(point);
                if (row != hoveredRow_) {
                    hoveredRow_ = row;
                    InvalidateRect(window_, nullptr, FALSE);
                }
            }
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            return 0;
        }
        case WM_MOUSELEAVE: {
            if (hoveredRow_ != -1 || hoveredRail_ != RailHit::None) {
                hoveredRow_ = -1;
                hoveredRail_ = RailHit::None;
                InvalidateRect(window_, nullptr, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_MBUTTONDOWN: {
            if (!workspace_) return 0;
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (workspace_->State().sidebarCollapsed) {
                RECT client{};
                GetClientRect(hwnd, &client);
                const RailHit hit = HitTestRail(point, client);
                if (hit == RailHit::Home || hit == RailHit::Search || hit == RailHit::Storage) {
                    if (navigate_) {
                        NavigationTarget target;
                        target.openInNewTab = message == WM_MBUTTONDOWN;
                        if (hit == RailHit::Home) {
                            target.displayName = L"Home";
                            target.isHome = true;
                        } else if (hit == RailHit::Search) {
                            target.displayName = L"Fast Search";
                            target.isSearch = true;
                        } else {
                            target.displayName = L"Storage Analysis";
                            target.isStorage = true;
                        }
                        navigate_(target);
                    }
                    InvalidateRect(window_, nullptr, FALSE);
                    return 0;
                }
                if (message == WM_MBUTTONDOWN) {
                    // Middle click keeps the old immediate expand semantic.
                    workspace_->State().sidebarCollapsed = false;
                    workspace_->MarkStateDirty();
                    Refresh();
                    return 0;
                }
                pressedRail_ = hit;
                SetCapture(hwnd);
                InvalidateRect(window_, nullptr, FALSE);
                return 0;
            }
            for (size_t index = 0; index < rows_.size(); ++index) {
                const Row& row = rows_[index];
                if (!PtInRect(&row.bounds, point)) continue;
                if (row.kind == RowKind::Section) {
                    ToggleSection(row.section);
                } else {
                    // Pressed feedback lives between down and up; navigation
                    // stays on mouse-down as before.
                    pressedRow_ = static_cast<int>(index);
                    SetCapture(hwnd);
                    ActivateRow(static_cast<int>(index), message == WM_MBUTTONDOWN);
                }
                return 0;
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            const bool railPressed = pressedRail_ != RailHit::None;
            if (pressedRow_ != -1 || railPressed) {
                pressedRow_ = -1;
                if (railPressed && GetCapture() == hwnd) ReleaseCapture();
                pressedRail_ = RailHit::None;
                // ui-v3 sidebar: the expand affordance activates on release so
                // its pressed state is visible.
                if (workspace_ != nullptr && workspace_->State().sidebarCollapsed) {
                    workspace_->State().sidebarCollapsed = false;
                    workspace_->MarkStateDirty();
                    Refresh();
                    return 0;
                }
                InvalidateRect(window_, nullptr, FALSE);
            } else if (GetCapture() == hwnd) {
                ReleaseCapture();
            }
            return 0;
        }
        case WM_CAPTURECHANGED: {
            pressedRow_ = -1;
            pressedRail_ = RailHit::None;
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        }
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS;
        case WM_KEYDOWN: {
            // Keyboard model for when the shell hands the sidebar focus:
            // arrows walk nav items in visual order, Enter/Space activate.
            if (!workspace_ || workspace_->State().sidebarCollapsed || rows_.empty()) return 0;
            switch (wParam) {
                case VK_DOWN:
                case VK_UP: {
                    const int next = NextItemRow(focusedRow_, wParam == VK_DOWN ? 1 : -1);
                    if (next != focusedRow_) {
                        focusedRow_ = next;
                        InvalidateRect(window_, nullptr, FALSE);
                    }
                    return 0;
                }
                case VK_HOME:
                case VK_END: {
                    int target = -1;
                    if (wParam == VK_HOME) {
                        for (size_t index = 0; index < rows_.size() && target == -1; ++index) {
                            if (rows_[index].kind == RowKind::Item) target = static_cast<int>(index);
                        }
                    } else {
                        for (size_t index = rows_.size(); index > 0 && target == -1; --index) {
                            if (rows_[index - 1].kind == RowKind::Item) target = static_cast<int>(index - 1);
                        }
                    }
                    if (target != focusedRow_) {
                        focusedRow_ = target;
                        InvalidateRect(window_, nullptr, FALSE);
                    }
                    return 0;
                }
                case VK_RETURN:
                case VK_SPACE:
                    ActivateRow(focusedRow_, false);
                    return 0;
                default:
                    break;
            }
            // Unhandled keys (Tab, accelerators) fall through to DefWindowProc.
            break;
        }
        case WM_CONTEXTMENU: {
            if (!workspace_ || workspace_->State().sidebarCollapsed) return 0;
            POINT screenPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (screenPoint.x == -1 && screenPoint.y == -1) {
                // Keyboard invocation anchors on the focused row, then hover.
                int anchor = focusedRow_;
                if (anchor < 0) anchor = hoveredRow_;
                if (anchor < 0 || anchor >= static_cast<int>(rows_.size())) return 0;
                const RECT& row = rows_[static_cast<size_t>(anchor)].bounds;
                screenPoint = {row.left + Scaled(UiMetrics::kSpaceS),
                               row.top + (row.bottom - row.top) / 2};
                ClientToScreen(hwnd, &screenPoint);
            }
            POINT clientPoint = screenPoint;
            ScreenToClient(hwnd, &clientPoint);
            const Row* itemRow = nullptr;
            for (const auto& row : rows_) {
                if (row.kind == RowKind::Item && PtInRect(&row.bounds, clientPoint)) {
                    itemRow = &row;
                    break;
                }
            }
            if (itemRow == nullptr) return 0;

            // ui-v3 sidebar: recovery affordance where the data model supports
            // one -- an unresolved known folder can re-resolve; bookmarks keep
            // their existing edit/reorder menu.
            const bool bookmarkRow = itemRow->section == 2 &&
                                     itemRow->item < workspace_->State().bookmarks.size();
            const bool unavailableFolder = itemRow->section == 1 && itemRow->path.empty() && !itemRow->home;
            if (!bookmarkRow && !unavailableFolder) return 0;

            HMENU menu = CreatePopupMenu();
            if (menu == nullptr) return 0;
            const size_t index = itemRow->item;
            if (bookmarkRow) {
                AppendMenuW(menu, MF_STRING, kBookmarkRename, L"Rename");
                AppendMenuW(menu, MF_STRING, kBookmarkRemove, L"Remove");
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, index == 0 ? MF_GRAYED : MF_STRING, kBookmarkMoveUp, L"Move Up");
                AppendMenuW(menu, index + 1 >= workspace_->State().bookmarks.size() ? MF_GRAYED : MF_STRING,
                            kBookmarkMoveDown, L"Move Down");
            } else {
                AppendMenuW(menu, MF_STRING, kKnownFolderRetry, L"Try Again");
            }
            const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                                 screenPoint.x, screenPoint.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
            if (command == kKnownFolderRetry) {
                workspace_->ReResolveKnownFolders();
                Refresh();
                return 0;
            }
            if (command == kBookmarkRename) {
                const auto& bookmark = workspace_->State().bookmarks[index];
                const auto name = PromptForLeafName(owner_, L"Rename Bookmark", bookmark.displayName);
                if (name) workspace_->RenameBookmark(index, *name);
            } else if (command == kBookmarkRemove) {
                workspace_->RemoveBookmark(index);
            } else if (command == kBookmarkMoveUp && index > 0) {
                workspace_->ReorderBookmark(index, index - 1);
            } else if (command == kBookmarkMoveDown && index + 1 < workspace_->State().bookmarks.size()) {
                workspace_->ReorderBookmark(index, index + 1);
            } else {
                return 0;
            }
            workspace_->FlushState();
            Refresh();
            // quick-access-home 5.1: bookmark edits change what Home's Pinned
            // section shows; let the shell refresh the Home surface.
            if (bookmarksChanged_) bookmarksChanged_();
            return 0;
        }
        case WM_NOTIFY: {
            auto* header = reinterpret_cast<NMHDR*>(lParam);
            if (header != nullptr && header->code == TTN_GETDISPINFOW) {
                auto* info = reinterpret_cast<NMTTDISPINFOW*>(lParam);
                tooltipText_.clear();
                if (info->hdr.idFrom == kToolRailHome) {
                    tooltipText_ = L"Home";
                } else if (info->hdr.idFrom == kToolRailSearch) {
                    tooltipText_ = L"Fast Search";
                } else if (info->hdr.idFrom == kToolRailStorage) {
                    tooltipText_ = L"Storage Analysis";
                } else if (info->hdr.idFrom == kToolRailExpand) {
                    tooltipText_ = L"Expand sidebar";
                } else if (info->hdr.idFrom < rows_.size()) {
                    const Row& row = rows_[info->hdr.idFrom];
                    if (row.kind == RowKind::Item) {
                        // Full value via tooltip: trimmed labels and paths stay
                        // reachable (docs/ui-improvement/03 §2).
                        tooltipText_ = row.path.empty()
                            ? row.label
                            : ffui::NormalizePathDerivedDisplayLabel(row.label, row.path);
                        if (row.path.empty() && !row.home && !row.search && !row.storage) {
                            tooltipText_ += L" (unavailable)";
                        }
                        if (!row.path.empty() && row.path != row.label) {
                            tooltipText_ += L"\n";
                            tooltipText_ += ffui::NormalizeDisplayPath(row.path);
                        }
                    }
                }
                info->lpszText = tooltipText_.data();
                return 0;
            }
            break;
        }
        case WM_NCDESTROY: {
            ReleaseFonts();
            tooltip_ = nullptr;  // owned popup dies with its owner window
            break;
        }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace ffui
