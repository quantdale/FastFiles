#include "NavigationChrome.h"
#include "ffui/AddressBarFocus.h"
#include "ffui/ChromeTabTraversal.h"
#include "UITheme.h"

#include <algorithm>
#include <iterator>
#include <commctrl.h>
#include <windowsx.h>

namespace ffui {
namespace {
constexpr wchar_t kWindowClass[] = L"FastFilesNavigationChrome";
constexpr int kBackId = 8201;
constexpr int kForwardId = 8202;
constexpr int kDriveId = 8203;
constexpr int kNewTabId = 8204;
constexpr int kUpId = 8205;
constexpr int kSearchId = 8206;
constexpr int kSettingsId = 8207;
constexpr int kOverflowId = 8208;
constexpr int kTabCloseBaseId = 8400;
constexpr UINT WM_APP_ADDRESS_FOCUS_LOST = WM_APP + 52;

// Scale a DIP metric to physical pixels for Win32 control layout. Accepts a
// float so themed corner radii (UiMetrics::kRadiusMedium) pass without a
// narrowing conversion.
int Scaled(float dipValue) {
    return static_cast<int>(ffui::UiScale(dipValue));
}

// ui-v3 shell: composites a translucent theme token over an opaque base color
// so GDI (no alpha channel) renders what Direct2D would.
COLORREF CompositeOver(D2D1_COLOR_F overlay, D2D1_COLOR_F base) {
    const float alpha = overlay.a;
    D2D1_COLOR_F blended{};
    blended.r = base.r * (1.0f - alpha) + overlay.r * alpha;
    blended.g = base.g * (1.0f - alpha) + overlay.g * alpha;
    blended.b = base.b * (1.0f - alpha) + overlay.b * alpha;
    blended.a = 1.0f;
    return ToColorRef(blended);
}

void DrawTextSegment(HDC dc, const std::wstring& text, RECT rect, COLORREF color) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
}

// Fills a rectangle with a freshly created solid brush (GDI has no rounded-fill
// helper, so the caller layers a rounded region on top of this for pills).
void FillRectColor(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    if (brush) {
        FillRect(dc, &rect, brush);
        DeleteObject(brush);
    }
}

// Fills a rounded-corner region with a solid brush. GDI's RoundRect only strokes;
// a rounded fill needs a region + FillRgn.
void DrawPill(HDC dc, const RECT& rect, COLORREF color, int radius) {
    HRGN region = CreateRoundRectRgn(rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    if (!region) return;
    HBRUSH brush = CreateSolidBrush(color);
    if (brush) {
        FillRgn(dc, region, brush);
        DeleteObject(brush);
    }
    DeleteObject(region);
}

std::wstring GetControlText(HWND hwnd) {
    const int length = GetWindowTextLengthW(hwnd);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(hwnd, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
}
} // namespace

NavigationChrome::~NavigationChrome() { Cleanup(); }

bool NavigationChrome::Initialize(HWND owner, NavigationWorkspace* workspace, NavigateHandler navigate,
                                  BeforeTabChangeHandler beforeTabChange,
                                  ToolCommandHandler toolCommand) {
    owner_ = owner;
    workspace_ = workspace;
    navigate_ = std::move(navigate);
    beforeTabChange_ = std::move(beforeTabChange);
    toolCommand_ = std::move(toolCommand);
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = &NavigationChrome::WindowProcThunk;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    windowClass.lpszClassName = kWindowClass;
    RegisterClassW(&windowClass);

    window_ = CreateWindowExW(0, kWindowClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                              0, 0, 0, Scaled(ffui::UiMetrics::kChromeHeight), owner, nullptr, windowClass.hInstance, this);
    if (!window_) return false;
    // ui-v3 shell: Back/Forward/New Tab render through the shared owner-drawn
    // icon-button family (see DrawIconButton); Reposition sizes them from the
    // shared chrome layout policy.
    back_ = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                            0, 0, Scaled(28), Scaled(28), window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBackId)), nullptr, nullptr);
    forward_ = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                               0, 0, Scaled(28), Scaled(28), window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kForwardId)), nullptr, nullptr);
    up_ = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                          0, 0, Scaled(28), Scaled(28), window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kUpId)), nullptr, nullptr);
    drives_ = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST |
                                 CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP,
                              0, 0, Scaled(140), Scaled(260), window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDriveId)), nullptr, nullptr);
    search_ = CreateWindowExW(0, L"BUTTON", L"Fast Search", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                              0, 0, Scaled(200), Scaled(28), window_,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSearchId)), nullptr, nullptr);
    settings_ = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                0, 0, Scaled(28), Scaled(28), window_,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingsId)), nullptr, nullptr);
    newTab_ = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                              0, 0, Scaled(28), Scaled(28), window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kNewTabId)), nullptr, nullptr);
    // ui-overhaul phase 1: hidden until the tab strip overflows its visible
    // window; opens the tab list so no tab becomes unreachable.
    overflow_ = CreateWindowExW(0, L"BUTTON", L"Tab list", WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                                0, 0, Scaled(30), Scaled(28), window_,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOverflowId)), nullptr, nullptr);
    // ui-v3 shell: the address edit is borderless — while editing, the
    // breadcrumb row paints a themed field backdrop so edit mode reads as a
    // transformation of the breadcrumb, not a different subsystem.
    addressEdit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL | WS_TABSTOP,
                                   0, 0, Scaled(200), Scaled(28), window_, nullptr, nullptr, nullptr);
    if (!back_ || !forward_ || !up_ || !drives_ || !search_ || !settings_ || !newTab_ || !addressEdit_ || !overflow_) return false;
    darkTheme_ = gUiDarkTheme;
    UpdateChromeBrush();
    // ui-v3 shell: the MDL2 glyph face tracks the body ramp size (as
    // NavigationSidebar's glyph font does) rather than carrying a bare
    // literal that can drift from the type scale.
    mdl2Font_ = CreateFontW(-static_cast<int>(UiScale(UiMetrics::kFontSizeBody)), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe MDL2 Assets");
    // ui-v3 shell: breadcrumbs and the address field use the product type ramp
    // instead of the DC default system font.
    bodyFont_ = CreateFontW(-static_cast<int>(UiScale(UiMetrics::kFontSizeBody)), 0, 0, 0, FW_NORMAL,
                            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    // ui-v3 shell: nav buttons join one owner-drawn icon-button family with
    // shared hover/pressed/focus resolution (no per-button system chrome).
    SetWindowSubclass(back_, HoverButtonProc, 1, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(forward_, HoverButtonProc, 1, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(up_, HoverButtonProc, 1, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(search_, HoverButtonProc, 1, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(settings_, HoverButtonProc, 1, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(newTab_, HoverButtonProc, 1, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(overflow_, HoverButtonProc, 1, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(addressEdit_, AddressEditProc, 1, reinterpret_cast<DWORD_PTR>(this));
    RebuildDrives();
    Refresh();
    return true;
}

ffui::ChromeLayout NavigationChrome::CurrentChromeLayout() const {
    RECT client{};
    GetClientRect(owner_, &client);
    const float scale = (std::max)(0.01f, ffui::UiDpiScale());
    const float widthDip = static_cast<float>((std::max)(0L, client.right - client.left)) / scale;
    const int tabCount = workspace_ != nullptr ? static_cast<int>(workspace_->TabCount()) : 0;
    const int activeIndex = workspace_ != nullptr ? static_cast<int>(workspace_->ActiveTabIndex()) : 0;
    return ffui::ComputeChromeLayout(widthDip, tabCount, activeIndex);
}

void NavigationChrome::Reposition() {
    if (!window_) return;
    RECT client{};
    GetClientRect(owner_, &client);
    const int width = (std::max)(0L, client.right - client.left);
    // ui-overhaul phase 1: every position comes from one responsive policy, so
    // the command family, location region, search affordance, and tab strip
    // can never disagree about the available width.
    const ffui::ChromeLayout layout = CurrentChromeLayout();
    SetWindowPos(window_, HWND_TOP, 0, 0, width, Scaled(layout.chromeHeight), SWP_NOACTIVATE | SWP_SHOWWINDOW);
    // Drives remain in the sidebar product map; the chrome trailing slot is Fast Search.
    ShowWindow(drives_, SW_HIDE);
    const auto placeSquare = [this](HWND control, const ffui::ChromeRect& rect) {
        if (control == nullptr) return;
        SetWindowPos(control, HWND_TOP, Scaled(rect.left), Scaled(rect.top), Scaled(rect.Width()),
                     Scaled(rect.Height()), SWP_NOACTIVATE);
    };
    placeSquare(back_, layout.back);
    placeSquare(forward_, layout.forward);
    placeSquare(up_, layout.up);
    placeSquare(settings_, layout.settings);
    placeSquare(newTab_, layout.newTab);
    placeSquare(overflow_, layout.tabs.overflowButton);
    if (addressEdit_ != nullptr) {
        // Address errors get a reserved slot inside the location row that the
        // opaque edit control does not cover, so the message stays readable.
        const int errorWidth = editing_ ? MeasureAddressErrorWidth(nullptr) : 0;
        const ffui::ChromeLocationRow row =
            ffui::ComputeChromeLocationRow(layout, static_cast<float>(errorWidth) / ffui::UiDpiScale());
        SetWindowPos(addressEdit_, HWND_TOP, Scaled(row.field.left), Scaled(row.field.top),
                     (std::max)(1, Scaled(row.field.Width())), Scaled(row.field.Height()), SWP_NOACTIVATE);
    }
    placeSquare(search_, layout.search);
    RepositionTabs();
    InvalidateRect(window_, nullptr, FALSE);
}

void NavigationChrome::Refresh() {
    if (!window_ || !workspace_) return;
    EnableWindow(back_, workspace_->CanGoBack());
    EnableWindow(forward_, workspace_->CanGoForward());
    const auto& context = workspace_->ActiveContext();
    EnableWindow(up_, !context.isHomeLocation && !context.currentPath.empty());
    RebuildTabs();
    RepositionTabs();
    if (!editing_) InvalidateRect(window_, nullptr, FALSE);
}

void NavigationChrome::FocusAddressBar() { BeginAddressEdit(); }

bool NavigationChrome::HandleTabNavigation(bool reverse) {
    if (!window_ || !owner_ || editing_) return false;

    const HWND focused = GetFocus();
    const bool focusIsShellSurface = focused == owner_ || focused == window_;
    const bool focusIsChromeControl = focused == addressEdit_ || IsChild(window_, focused);
    if (!IsChromeTabTraversalEligible(window_ != nullptr, owner_ != nullptr, editing_, focusIsShellSurface,
                                      focusIsChromeControl, focused == addressEdit_)) {
        return false;
    }

    // retry-ownership-and-focus-escape-closure D4: the
    // candidate list is filtered exactly like the chrome's own visibility
    // rules (disabled Back/Forward/Up and a hidden overflow affordance are
    // not Tab stops), then the pure seam decides entry, adjacency, and the
    // escape to the shell owner at either boundary.
    std::vector<HWND> focusable;
    focusable.reserve(8 + tabButtons_.size() + tabCloseButtons_.size());
    AppendChromeTabStop(focusable, back_);
    AppendChromeTabStop(focusable, forward_);
    AppendChromeTabStop(focusable, up_);
    AppendChromeTabStop(focusable, search_);
    AppendChromeTabStop(focusable, settings_);
    AppendChromeTabStop(focusable, newTab_);
    for (size_t index = 0; index < tabButtons_.size(); ++index) {
        AppendChromeTabStop(focusable, tabButtons_[index]);
        if (index < tabCloseButtons_.size()) AppendChromeTabStop(focusable, tabCloseButtons_[index]);
    }
    AppendChromeTabStop(focusable, overflow_);

    const ChromeTabTarget decision = NextChromeTabTarget(focusable, focused, owner_, reverse);
    if (!decision.consume || decision.target == nullptr) return false;
    SetFocus(decision.target);
    return GetFocus() == decision.target;
}

void NavigationChrome::SetSearchActive(bool active) {
    if (searchActive_ == active) return;
    searchActive_ = active;
    if (search_) InvalidateRect(search_, nullptr, FALSE);
}

void NavigationChrome::BeginAddressEdit() {
    if (editing_ || !workspace_) return;
    workspace_->BeginAddressBarEdit();
    editing_ = true;
    hoverSegment_ = -1;
    SetWindowTextW(addressEdit_, workspace_->ActiveContext().addressBarText.c_str());
    ShowWindow(addressEdit_, SW_SHOW);
    SetFocus(addressEdit_);
    SendMessageW(addressEdit_, EM_SETSEL, 0, -1);
    // Beginning an edit clears any prior address error, so the row is rebuilt
    // without a reserved slot before it is painted.
    Reposition();
    InvalidateRect(window_, nullptr, FALSE);
}

void NavigationChrome::CommitAddressEdit() {
    if (!editing_ || !workspace_) return;
    std::wstring text(32768, L'\0');
    const int length = GetWindowTextW(addressEdit_, text.data(), static_cast<int>(text.size()));
    text.resize(static_cast<size_t>((std::max)(0, length)));
    const PathCommitResult result = CommitAddressBarPath(*workspace_, text);
    if (result == PathCommitResult::Navigated) {
        editing_ = false;
        ShowWindow(addressEdit_, SW_HIDE);
        NavigateTo(workspace_->ActiveContext().currentPath);
        // keyboard-focus-forensics FF-VIS-FOCUS-001: hiding the focused edit
        // strands keyboard focus on the chrome child (no WM_KEYDOWN routing),
        // silently killing arrow/Enter/shortcut navigation until the user
        // clicks content. Return focus to the shell owner that routes keys.
        SetFocus(AddressEditClosedFocusTarget(owner_, window_));
        return;
    }
    // An invalid commit keeps the editor open with an error message; the
    // location row must reserve the message's slot so the opaque edit no
    // longer covers it.
    Reposition();
    InvalidateRect(window_, nullptr, FALSE);
    SetFocus(addressEdit_);
    SendMessageW(addressEdit_, EM_SETSEL, 0, -1);
}

void NavigationChrome::CancelAddressEdit() {
    EndAddressEdit(true);
}

void NavigationChrome::EndAddressEdit(bool restoreFocus) {
    if (!editing_ || !workspace_) return;
    workspace_->CancelAddressBarEdit();
    editing_ = false;
    ShowWindow(addressEdit_, SW_HIDE);
    // Reclaim the whole location row: the error slot only exists while editing.
    Reposition();
    // keyboard-focus-forensics FF-VIS-FOCUS-001: the chrome child has no
    // WM_KEYDOWN routing, so restoring focus to it (instead of the shell
    // owner) leaves keyboard navigation dead after Escape. See the seam
    // contract in ffui/AddressBarFocus.h.
    if (restoreFocus) SetFocus(AddressEditClosedFocusTarget(owner_, window_));
    InvalidateRect(window_, nullptr, FALSE);
}

void NavigationChrome::HandleAddressFocusLost() {
    EndAddressEdit(false);
}

void NavigationChrome::NavigateTo(const std::wstring& path, bool restoreTabState) {
    // Address/drive/breadcrumb commits are always filesystem navigations; an
    // empty path is the synthetic "This PC" root, never Home.
    NavigateToLocation(NavigationLocation::FromPath(path), restoreTabState);
}

void NavigationChrome::NavigateToLocation(const NavigationLocation& location, bool restoreTabState) {
    if (navigate_) navigate_(location, restoreTabState);
    Refresh();
}

void NavigationChrome::RebuildDrives() {
    if (!drives_) return;
    // Preserve the current selection across the refresh so an open dropdown
    // does not visibly jump when nothing changed.
    const int previousIndex = static_cast<int>(SendMessageW(drives_, CB_GETCURSEL, 0, 0));
    std::wstring previousPath;
    if (previousIndex >= 0 && static_cast<size_t>(previousIndex) < drivePaths_.size()) {
        previousPath = drivePaths_[static_cast<size_t>(previousIndex)];
    }
    drivePaths_ = NavigationWorkspace::EnumerateDrives();
    SendMessageW(drives_, CB_RESETCONTENT, 0, 0);
    SendMessageW(drives_, CB_SETITEMHEIGHT, 0, Scaled(28));
    SendMessageW(drives_, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), Scaled(24));
    int restoredSelection = -1;
    for (const auto& path : drivePaths_) {
        const int index = static_cast<int>(SendMessageW(drives_, CB_ADDSTRING, 0,
                                                         reinterpret_cast<LPARAM>(path.c_str())));
        SendMessageW(drives_, CB_SETITEMDATA, index, static_cast<LPARAM>(index));
        if (path == previousPath) restoredSelection = index;
    }
    if (restoredSelection >= 0) {
        SendMessageW(drives_, CB_SETCURSEL, restoredSelection, 0);
    }
}

void NavigationChrome::RebuildTabs() {
    if (!window_ || !workspace_) return;
    for (HWND tab : tabButtons_) DestroyWindow(tab);
    for (HWND close : tabCloseButtons_) DestroyWindow(close);
    tabButtons_.clear();
    tabCloseButtons_.clear();
    hoverCloseTab_ = -1;
    // ui-v3 shell: destroyed tab buttons can no longer be hovered/pressed.
    if (hoverItemId_ >= kTabBaseId) hoverItemId_ = -1;
    if (pressedItemId_ >= kTabBaseId) pressedItemId_ = -1;
    for (size_t index = 0; index < workspace_->TabCount(); ++index) {
        // quick-access-home 5.2: read the typed tab location so a Home tab
        // labels as "Home" instead of being collapsed into the "This PC"
        // (empty-path) case. The label seam also strips extended-length
        // prefixes — the model keeps them for long-path navigation, but no
        // user-visible label prints "\\?\".
        const NavigationLocation location = workspace_->TabLocation(index);
        const std::wstring label = TabLabelForLocation(index + 1, location);
        HWND tab = CreateWindowExW(0, L"BUTTON", label.c_str(),
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                   0, 0, Scaled(120), Scaled(27), window_,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTabBaseId + index)),
                                   GetModuleHandleW(nullptr), nullptr);
        HWND close = CreateWindowExW(0, L"BUTTON", L"\uE8BB",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                     0, 0, Scaled(24), Scaled(27), window_,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTabCloseBaseId + index)),
                                     GetModuleHandleW(nullptr), nullptr);
        if (tab != nullptr && close != nullptr) {
            tabButtons_.push_back(tab);
            tabCloseButtons_.push_back(close);
            // ui-v3 shell: tab pills track hover so the close affordance can be
            // revealed from the tab itself and inactive tabs show a hover state.
            SetWindowSubclass(tab, HoverButtonProc, 1, reinterpret_cast<DWORD_PTR>(this));
            SetWindowSubclass(close, CloseButtonProc, 1, reinterpret_cast<DWORD_PTR>(this));
            // Inactive tabs must remain clickable; the active tab is visually
            // distinguished by DrawTabButton via IsTabDrawItemActive (the real
            // active-tab identity), not by disabling the others.
            EnableWindow(tab, TRUE);
        } else {
            if (tab != nullptr) DestroyWindow(tab);
            if (close != nullptr) DestroyWindow(close);
        }
    }
}

void NavigationChrome::RepositionTabs() {
    if (!window_) return;
    // ui-overhaul phase 1: tab geometry comes from the shared responsive
    // policy. When the readable floor cannot fit, a bounded window of tabs
    // stays visible (always containing the active tab) and the overflow button
    // exposes the rest through the tab list.
    const ffui::ChromeLayout layout = CurrentChromeLayout();
    const int tabTop = Scaled(layout.tabs.top);
    const int tabHeight = Scaled(layout.tabs.height);
    const int tabWidth = Scaled(layout.tabs.width);
    const int tabGap = Scaled(layout.tabs.gap);
    const int firstVisible = layout.tabs.visibleFirst;
    const int visibleCount = layout.tabs.visibleCount;
    for (size_t index = 0; index < tabButtons_.size(); ++index) {
        const int visibleIndex = static_cast<int>(index) - firstVisible;
        const bool visible = visibleIndex >= 0 && visibleIndex < visibleCount;
        if (!visible) {
            if (IsWindowVisible(tabButtons_[index])) ShowWindow(tabButtons_[index], SW_HIDE);
            if (IsWindowVisible(tabCloseButtons_[index])) ShowWindow(tabCloseButtons_[index], SW_HIDE);
            continue;
        }
        if (!IsWindowVisible(tabButtons_[index])) ShowWindow(tabButtons_[index], SW_SHOWNA);
        if (!IsWindowVisible(tabCloseButtons_[index])) ShowWindow(tabCloseButtons_[index], SW_SHOWNA);
        const int x = Scaled(layout.tabs.left) + visibleIndex * (tabWidth + tabGap);
        SetWindowPos(tabButtons_[index], HWND_TOP, x, tabTop, tabWidth, tabHeight, SWP_NOACTIVATE);
        SetWindowPos(tabCloseButtons_[index], HWND_TOP, x + tabWidth - Scaled(24), tabTop, Scaled(24),
                     tabHeight, SWP_NOACTIVATE);
    }
    if (overflow_ != nullptr) {
        if (layout.tabs.overflow) {
            SetWindowPos(overflow_, HWND_TOP, Scaled(layout.tabs.overflowButton.left), tabTop,
                         Scaled(layout.tabs.overflowButton.size), tabHeight,
                         SWP_NOACTIVATE | SWP_SHOWWINDOW);
        } else if (IsWindowVisible(overflow_)) {
            ShowWindow(overflow_, SW_HIDE);
        }
    }
    // A hidden tab can no longer be hovered or pressed.
    if (hoverItemId_ >= kTabBaseId && hoverItemId_ < kTabBaseId + static_cast<int>(tabButtons_.size())) {
        const size_t index = static_cast<size_t>(hoverItemId_ - kTabBaseId);
        if (index < tabButtons_.size() && !IsWindowVisible(tabButtons_[index])) hoverItemId_ = -1;
    }
    if (pressedItemId_ >= kTabBaseId && pressedItemId_ < kTabBaseId + static_cast<int>(tabButtons_.size())) {
        const size_t index = static_cast<size_t>(pressedItemId_ - kTabBaseId);
        if (index < tabButtons_.size() && !IsWindowVisible(tabButtons_[index])) pressedItemId_ = -1;
    }
}

void NavigationChrome::ShowTabList() {
    if (!workspace_ || overflow_ == nullptr || tabButtons_.empty()) return;
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return;
    const size_t count = workspace_->TabCount();
    for (size_t index = 0; index < count; ++index) {
        const std::wstring label = TabLabelForLocation(index + 1, workspace_->TabLocation(index));
        UINT flags = MF_STRING;
        if (index == workspace_->ActiveTabIndex()) flags |= MF_CHECKED;
        AppendMenuW(menu, flags, static_cast<UINT_PTR>(index + 1), label.c_str());
    }
    RECT anchor{};
    GetWindowRect(overflow_, &anchor);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN,
                                        anchor.left, anchor.bottom, 0, window_, nullptr);
    DestroyMenu(menu);
    if (command == 0) return;
    const size_t index = static_cast<size_t>(command - 1);
    if (index >= workspace_->TabCount()) return;
    const bool switching = index != workspace_->ActiveTabIndex();
    if (switching && beforeTabChange_) beforeTabChange_();
    if (workspace_->SwitchTab(index)) {
        const auto& context = workspace_->ActiveContext();
        NavigateToLocation(ffui::LocationOf(context), switching);
    }
    Refresh();
}

void NavigationChrome::CloseTab(size_t index) {
    if (!workspace_ || index >= workspace_->TabCount()) return;
    if (beforeTabChange_) beforeTabChange_();
    if (index != workspace_->ActiveTabIndex()) {
        workspace_->SwitchTab(index);
    }
    if (workspace_->CloseActiveTab()) {
        // quick-access-home 5.2: navigate the newly-active tab with its typed
        // location so a Home tab restores as Home, not as an empty path.
        const auto& context = workspace_->ActiveContext();
        NavigateToLocation(ffui::LocationOf(context),
                           true);
    } else {
        Refresh();
    }
}

int NavigationChrome::MeasureAddressErrorWidth(HDC dc) const {
    if (!workspace_ || !window_) return 0;
    const std::wstring& error = workspace_->ActiveContext().addressBarError;
    if (error.empty()) return 0;
    HDC measureDc = dc;
    if (measureDc == nullptr) measureDc = GetDC(window_);
    if (measureDc == nullptr) return 0;
    HFONT oldFont = bodyFont_ ? static_cast<HFONT>(SelectObject(measureDc, bodyFont_)) : nullptr;
    RECT measured{0, 0, 0, 0};
    DrawTextW(measureDc, error.c_str(), -1, &measured, DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
    if (oldFont) SelectObject(measureDc, oldFont);
    if (dc == nullptr) ReleaseDC(window_, measureDc);
    return static_cast<int>(measured.right - measured.left) + Scaled(12);
}

void NavigationChrome::DrawBreadcrumbs(HDC dc, const RECT& client) {
    breadcrumbRects_.clear();
    breadcrumbIndices_.clear();
    collapsedTargetPath_.clear();
    if (!workspace_) return;
    const UiTheme theme = GetUiTheme(darkTheme_);
    const bool highContrast = UiSystemHighContrast();
    HFONT oldFont = bodyFont_ ? static_cast<HFONT>(SelectObject(dc, bodyFont_)) : nullptr;
    // ui-v3 shell: one quiet surface for the whole chrome bar plus a single
    // subtle hairline at the bottom edge — no stacked bands/borders.
    FillRectColor(dc, client,
                  highContrast ? GetSysColor(COLOR_WINDOW) : ToColorRef(theme.background));

    const auto breadcrumbs = workspace_->Breadcrumbs();
    // ui-v3 shell breadcrumb layout: two-pass measure so narrow widths collapse
    // intermediate segments into one "…" entry instead of clipping. First and
    // current segments always stay visible.
    struct SegmentLayout {
        size_t sourceIndex;  // index into breadcrumbs; SIZE_MAX = collapsed run
        int width;
    };
    const int separatorWidth = Scaled(14);
    // ui-overhaul phase 1: the breadcrumb band and its right limit come from
    // the same responsive policy that positions the search affordance, so the
    // painted path can never collide with the trailing controls.
    const ffui::ChromeLayout chromeLayout = CurrentChromeLayout();
    int x = Scaled(chromeLayout.addressLeft);
    const ffui::ChromeBand crumbBand = ffui::ChromeLocationBand(chromeLayout);
    const int crumbTop = Scaled(crumbBand.top);
    const int crumbBottom = crumbTop + Scaled(crumbBand.Height());
    // ui-overhaul: an address error owns a readable slot at the end of the
    // location row; the breadcrumb run stops at the slot so they never overlap.
    const ffui::ChromeLocationRow locationRow = ffui::ComputeChromeLocationRow(
        chromeLayout, static_cast<float>(MeasureAddressErrorWidth(dc)) / ffui::UiDpiScale());
    int rightLimit = (std::max)(x + Scaled(40), Scaled(chromeLayout.addressRight));
    if (locationRow.hasError) {
        rightLimit = (std::max)(x + Scaled(40), Scaled(locationRow.field.right()));
    }
    std::vector<SegmentLayout> layout;
    layout.reserve(breadcrumbs.size());
    int totalWidth = 0;
    for (size_t index = 0; index < breadcrumbs.size(); ++index) {
        RECT measured{x, crumbTop, rightLimit, crumbBottom};
        DrawTextW(dc, breadcrumbs[index].label.c_str(), -1, &measured,
                  DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
        const int width = (std::max)(28L, measured.right - measured.left + Scaled(12));
        layout.push_back({index, width});
        totalWidth += width + separatorWidth;
    }
    const bool needsCollapse =
        totalWidth > (rightLimit - x) && breadcrumbs.size() > 2;
    if (needsCollapse) {
        // Collapse indices 1..n-2 into a single "…" segment targeting the
        // deepest hidden ancestor (breadcrumbs[1]).
        std::vector<SegmentLayout> collapsed;
        collapsed.reserve(3);
        collapsed.push_back(layout.front());
        collapsed.push_back({SIZE_MAX, Scaled(24)});
        collapsed.push_back(layout.back());
        int collapsedTotal = 0;
        for (const auto& segment : collapsed) {
            collapsedTotal += segment.width + separatorWidth;
        }
        if (collapsedTotal <= rightLimit - x) {
            collapsedTargetPath_ = breadcrumbs[1].path;
            layout = std::move(collapsed);
        } else {
            // Even the collapsed form does not fit: drop from the front of the
            // remaining run until it does (current segment is last, never dropped).
            while (layout.size() > 1 && totalWidth > rightLimit - x) {
                totalWidth -= layout.front().width + separatorWidth;
                layout.erase(layout.begin());
            }
            if (layout.size() == 2 && !breadcrumbs.empty()) {
                collapsedTargetPath_ = breadcrumbs[1].path;
                std::vector<SegmentLayout> minimal;
                minimal.push_back({SIZE_MAX, Scaled(24)});
                minimal.push_back(layout.back());
                layout = std::move(minimal);
            }
        }
    }

    for (size_t position = 0; position < layout.size(); ++position) {
        const auto& entry = layout[position];
        if (entry.sourceIndex == SIZE_MAX) {
            // Collapsed-run placeholder: clickable, navigates to the deepest
            // hidden ancestor.
            RECT segmentRect{x, crumbTop, (std::min)(rightLimit, x + entry.width), crumbBottom};
            if (!collapsedTargetPath_.empty()) {
                breadcrumbRects_.push_back(segmentRect);
                breadcrumbIndices_.push_back(-1);
            }
            const bool hovered = hoverSegment_ >= 0 &&
                                 static_cast<size_t>(hoverSegment_) == breadcrumbRects_.size() - 1 &&
                                 !collapsedTargetPath_.empty();
            if (hovered) {
                DrawPill(dc, segmentRect,
                         highContrast ? GetSysColor(COLOR_HIGHLIGHT)
                                      : CompositeOver(theme.hoverOverlay, theme.background),
                         Scaled(UiMetrics::kRadiusSmall));
            }
            DrawTextSegment(dc, L"…", segmentRect,
                            highContrast ? GetSysColor(COLOR_WINDOWTEXT)
                                         : ToColorRef(theme.textSecondary));
            x = segmentRect.right;
        } else {
            const auto& segment = breadcrumbs[entry.sourceIndex];
            RECT segmentRect{x, crumbTop, (std::min)(rightLimit, x + entry.width), crumbBottom};
            const bool current = entry.sourceIndex + 1 == breadcrumbs.size();
            const bool clickable = !segment.path.empty() || segment.syntheticRoot;
            breadcrumbRects_.push_back(segmentRect);
            breadcrumbIndices_.push_back(static_cast<int>(entry.sourceIndex));
            // ui-v3 shell: hover feedback only on interactive segments; the
            // current location is not a button.
            if (hoverSegment_ == static_cast<int>(breadcrumbRects_.size() - 1) && clickable) {
                DrawPill(dc, segmentRect,
                         highContrast ? GetSysColor(COLOR_HIGHLIGHT)
                                      : CompositeOver(theme.hoverOverlay, theme.background),
                         Scaled(UiMetrics::kRadiusSmall));
            }
            const COLORREF textColor = highContrast
                ? GetSysColor(clickable ? COLOR_HIGHLIGHT : COLOR_WINDOWTEXT)
                : ToColorRef(current ? theme.text : (clickable ? theme.accent : theme.textSecondary));
            DrawTextSegment(dc, segment.label, segmentRect, textColor);
            x = segmentRect.right;
        }
        if (position + 1 < layout.size() && x + separatorWidth < rightLimit) {
            // ui-v3 shell: quiet separators in tertiary text keep the path
            // readable without competing with segment labels.
            RECT separator{x, crumbTop, x + separatorWidth, crumbBottom};
            DrawTextSegment(dc, L"›", separator,
                            highContrast ? GetSysColor(COLOR_GRAYTEXT)
                                         : ToColorRef(theme.textTertiary));
            x += separatorWidth;
        }
        if (x >= rightLimit) break;
    }

    // ui-v3 shell: while editing, the address field reads as a transformation
    // of the breadcrumb — a themed field backdrop with focus stroke replaces
    // the old inset-edge system edit box.
    if (editing_ && addressEdit_) {
        RECT editClient{};
        GetClientRect(addressEdit_, &editClient);
        MapWindowPoints(addressEdit_, window_, reinterpret_cast<POINT*>(&editClient), 2);
    RECT field{editClient.left - Scaled(4), crumbTop, editClient.right + Scaled(4), crumbBottom};
        DrawPill(dc, field,
                 highContrast ? GetSysColor(COLOR_WINDOW) : ToColorRef(theme.surfaceElevated),
                 Scaled(UiMetrics::kRadiusField));
        HRGN inside = CreateRoundRectRgn(field.left + 1, field.top + 1, field.right - 1,
                                         field.bottom - 1, Scaled(UiMetrics::kRadiusField),
                                         Scaled(UiMetrics::kRadiusField));
        if (inside) {
            const bool hasError = locationRow.hasError;
            HBRUSH frame = CreateSolidBrush(highContrast ? GetSysColor(COLOR_WINDOWTEXT)
                                                         : ToColorRef(hasError ? theme.error
                                                                               : theme.focusStroke));
            if (frame) {
                FrameRgn(dc, inside, frame, 1, 1);
                DeleteObject(frame);
            }
            DeleteObject(inside);
        }
    }

    if (!workspace_->ActiveContext().addressBarError.empty()) {
        // Inside the reserved slot and vertically centred in the location row:
        // the previous rect sat below the 72-DIP chrome and was clipped.
        const RECT error{Scaled(locationRow.error.left), crumbTop, Scaled(locationRow.error.right()),
                         crumbBottom};
        DrawTextSegment(dc, workspace_->ActiveContext().addressBarError, error,
                        highContrast ? GetSysColor(COLOR_HIGHLIGHT) : ToColorRef(theme.error));
    }
    // ui-v3 shell: single subtle divider closing the chrome bar.
    const RECT hairline{0, client.bottom - 1, client.right, client.bottom};
    FillRectColor(dc, hairline,
                  highContrast ? GetSysColor(COLOR_WINDOWTEXT)
                               : CompositeOver(theme.dividerSubtle, theme.background));
    if (oldFont) SelectObject(dc, oldFont);
}

void NavigationChrome::DrawSearchField(const DRAWITEMSTRUCT* item) {
    HDC dc = item->hDC;
    const RECT& rect = item->rcItem;
    const UiTheme theme = GetUiTheme(darkTheme_);
    const bool highContrast = UiSystemHighContrast();
    const bool hovered = static_cast<int>(item->CtlID) == hoverItemId_;
    const bool pressed = static_cast<int>(item->CtlID) == pressedItemId_;
    FillRectColor(dc, rect, highContrast ? GetSysColor(COLOR_WINDOW) : ToColorRef(theme.background));
    COLORREF fill = highContrast ? GetSysColor(COLOR_WINDOW) : ToColorRef(theme.surfaceElevated);
    if (searchActive_) {
        fill = highContrast ? GetSysColor(COLOR_HIGHLIGHT)
                            : CompositeOver(theme.selectionSoft, theme.surfaceElevated);
    } else if (pressed) {
        fill = highContrast ? GetSysColor(COLOR_HIGHLIGHT)
                            : CompositeOver(theme.pressOverlay, theme.surfaceElevated);
    } else if (hovered) {
        fill = highContrast ? GetSysColor(COLOR_HIGHLIGHT)
                            : CompositeOver(theme.hoverOverlay, theme.surfaceElevated);
    }
    DrawPill(dc, rect, fill, Scaled(UiMetrics::kRadiusField));
    if ((item->itemState & ODS_FOCUS) != 0 || searchActive_) {
        HRGN region = CreateRoundRectRgn(rect.left + 1, rect.top + 1, rect.right - 1, rect.bottom - 1,
                                         Scaled(UiMetrics::kRadiusField), Scaled(UiMetrics::kRadiusField));
        if (region) {
            HBRUSH frame = CreateSolidBrush(highContrast ? GetSysColor(COLOR_WINDOWTEXT)
                                                         : ToColorRef(theme.focusStroke));
            if (frame) {
                FrameRgn(dc, region, frame, 1, 1);
                DeleteObject(frame);
            }
            DeleteObject(region);
        }
    }
    COLORREF fg = highContrast
        ? GetSysColor(searchActive_ ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT)
        : ToColorRef(searchActive_ ? theme.text : theme.textSecondary);
    HFONT oldFont = mdl2Font_ ? static_cast<HFONT>(SelectObject(dc, mdl2Font_)) : nullptr;
    RECT glyphRect = rect;
    // Collapsed (icon-only) search centers the glyph in its command square so
    // the family reads identically to the other icon buttons; the labelled
    // form keeps the glyph in a leading slot.
    const bool iconOnly = (rect.right - rect.left) <= Scaled(64);
    if (iconOnly) {
        glyphRect.left = rect.left + ((rect.right - rect.left) - Scaled(18)) / 2;
    } else {
        glyphRect.left += Scaled(8);
    }
    glyphRect.right = glyphRect.left + Scaled(18);
    const wchar_t glyph = L'\uE721';
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, fg);
    DrawTextW(dc, &glyph, 1, &glyphRect, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
    if (oldFont) SelectObject(dc, oldFont);
    if (!iconOnly && bodyFont_) {
        oldFont = static_cast<HFONT>(SelectObject(dc, bodyFont_));
        RECT textRect = rect;
        textRect.left = glyphRect.right + Scaled(4);
        textRect.right -= Scaled(8);
        DrawTextW(dc, L"Fast Search", -1, &textRect,
                  DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(dc, oldFont);
    }
}

void NavigationChrome::DrawIconButton(const DRAWITEMSTRUCT* item, const wchar_t* glyph,
                                      bool mirrorGlyph) {
    HDC dc = item->hDC;
    const RECT& rect = item->rcItem;
    const UiTheme theme = GetUiTheme(darkTheme_);
    const bool highContrast = UiSystemHighContrast();
    const bool enabled = (item->itemState & ODS_DISABLED) == 0;
    const bool hovered = enabled && static_cast<int>(item->CtlID) == hoverItemId_;
    const bool pressed = enabled && static_cast<int>(item->CtlID) == pressedItemId_;
    // ui-v3 shell icon-button family: quiet at rest, overlay feedback on
    // hover/press, focus ring independent of hover, no persistent border.
    FillRectColor(dc, rect, highContrast ? GetSysColor(COLOR_WINDOW) : ToColorRef(theme.background));
    if (pressed) {
        DrawPill(dc, rect,
                 highContrast ? GetSysColor(COLOR_HIGHLIGHT)
                              : CompositeOver(theme.pressOverlay, theme.background),
                 Scaled(UiMetrics::kRadiusSmall));
    } else if (hovered) {
        DrawPill(dc, rect,
                 highContrast ? GetSysColor(COLOR_HIGHLIGHT)
                              : CompositeOver(theme.hoverOverlay, theme.background),
                 Scaled(UiMetrics::kRadiusSmall));
    }
    if ((item->itemState & ODS_FOCUS) != 0) {
        // ui-v3 shell: keyboard focus indicator inset within the button bounds.
        RECT ring{rect.left + Scaled(2), rect.top + Scaled(2), rect.right - Scaled(2),
                  rect.bottom - Scaled(2)};
        HRGN region = CreateRoundRectRgn(ring.left, ring.top, ring.right, ring.bottom,
                                         Scaled(UiMetrics::kRadiusSmall), Scaled(UiMetrics::kRadiusSmall));
        if (region) {
            HBRUSH frame = CreateSolidBrush(highContrast ? GetSysColor(COLOR_WINDOWTEXT)
                                                         : ToColorRef(theme.focusStroke));
            if (frame) {
                FrameRgn(dc, region, frame, 2, 2);
                DeleteObject(frame);
            }
            DeleteObject(region);
        }
    }
    COLORREF fg = highContrast
        ? GetSysColor(enabled ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT)
        : ToColorRef(enabled ? theme.text : theme.textDisabled);
    // MDL2 glyphs (E7xx private-use area) need the icon font; Unicode arrow
    // fallbacks draw with the default UI font.
    HFONT oldFont = nullptr;
    const bool useGlyphFont = mdl2Font_ != nullptr && glyph[0] != L'\0' &&
                              static_cast<UINT>(glyph[0]) >= 0xE000 &&
                              static_cast<UINT>(glyph[0]) <= 0xF8FF;
    if (useGlyphFont) oldFont = static_cast<HFONT>(SelectObject(dc, mdl2Font_));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, fg);
    if (mirrorGlyph) {
        // ui-v3 shell: Forward mirrors the Back glyph via DC layout mirroring,
        // guaranteeing the pair stays one consistent family.
        const DWORD oldLayout = GetLayout(dc);
        SetLayout(dc, oldLayout | LAYOUT_RTL);
        DrawTextW(dc, glyph, -1, const_cast<LPRECT>(&rect),
                  DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
        SetLayout(dc, oldLayout);
    } else {
        DrawTextW(dc, glyph, -1, const_cast<LPRECT>(&rect),
                  DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
    }
    if (oldFont) SelectObject(dc, oldFont);
}

void NavigationChrome::SetControlHover(HWND control, int controlId, bool hover) {
    const int newIndex = hover ? controlId : -1;
    if (newIndex == hoverItemId_) return;
    const int oldId = hoverItemId_;
    hoverItemId_ = newIndex;
    if (control) InvalidateRect(control, nullptr, FALSE);
    // ui-v3 shell: a close strip inside a hovered tab must reveal itself, so
    // both the previously and newly hovered tab's strip repaint.
    auto invalidateCloseFor = [this](int id) {
        if (id < kTabBaseId) return;
        const size_t index = static_cast<size_t>(id - kTabBaseId);
        if (index < tabCloseButtons_.size()) {
            InvalidateRect(tabCloseButtons_[index], nullptr, FALSE);
        }
    };
    invalidateCloseFor(oldId);
    invalidateCloseFor(newIndex);
}

void NavigationChrome::SetControlPressed(HWND control, int controlId, bool pressed) {
    const int newIndex = pressed ? controlId : -1;
    if (newIndex == pressedItemId_) return;
    pressedItemId_ = newIndex;
    if (control) InvalidateRect(control, nullptr, FALSE);
}

void NavigationChrome::DrawTabButton(const DRAWITEMSTRUCT* item) {
    HDC dc = item->hDC;
    const RECT& rect = item->rcItem;
    const UiTheme theme = GetUiTheme(darkTheme_);
    const bool highContrast = UiSystemHighContrast();
    // The real active-tab identity: ODS_DISABLED is never set (all tabs stay
    // enabled for clickability), so it cannot signal the active tab.
    const int controlId = static_cast<int>(item->CtlID);
    const bool active = IsTabDrawItemActive(controlId, workspace_ ? workspace_->ActiveTabIndex() : 0);
    const bool hovered = !active && controlId == hoverItemId_;
    const bool pressed = controlId == pressedItemId_;
    const COLORREF parentBg = highContrast ? GetSysColor(COLOR_WINDOW) : ToColorRef(theme.background);
    // ui-v3 shell tab hierarchy: the active tab is an elevated pill with a thin
    // accent underline — clear without a large accent block; inactive tabs are
    // subdued and only surface on hover/press.
    const COLORREF pill = highContrast
        ? GetSysColor(active ? COLOR_HIGHLIGHT : COLOR_WINDOW)
        : (pressed ? CompositeOver(theme.pressOverlay, theme.background)
                   : hovered ? CompositeOver(theme.hoverOverlay, theme.background)
                             : active ? ToColorRef(theme.surfaceSubtle)
                                      : ToColorRef(theme.background));
    FillRectColor(dc, rect, parentBg);
    DrawPill(dc, rect, pill, Scaled(UiMetrics::kRadiusMedium));
    if (active && !highContrast) {
        RECT underline{rect.left + Scaled(8), rect.bottom - Scaled(3), rect.right - Scaled(8),
                       rect.bottom - Scaled(1)};
        DrawPill(dc, underline, ToColorRef(theme.accent), 1);
    }
    if ((item->itemState & ODS_FOCUS) != 0) {
        RECT ring{rect.left + Scaled(2), rect.top + Scaled(2), rect.right - Scaled(2),
                  rect.bottom - Scaled(2)};
        HRGN region = CreateRoundRectRgn(ring.left, ring.top, ring.right, ring.bottom,
                                         Scaled(UiMetrics::kRadiusSmall), Scaled(UiMetrics::kRadiusSmall));
        if (region) {
            HBRUSH frame = CreateSolidBrush(highContrast ? GetSysColor(COLOR_WINDOWTEXT)
                                                         : ToColorRef(theme.focusStroke));
            if (frame) {
                FrameRgn(dc, region, frame, 2, 2);
                DeleteObject(frame);
            }
            DeleteObject(region);
        }
    }
    const COLORREF fg = highContrast
        ? GetSysColor(active ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT)
        : ToColorRef(active ? theme.text : (hovered ? theme.text : theme.textSecondary));
    RECT textRect = rect;
    textRect.left += Scaled(8);
    textRect.right -= Scaled(26);  // keep clear of the in-pill close affordance
    if (!active && textRect.right <= textRect.left) textRect.right = textRect.left;
    const std::wstring label = GetControlText(item->hwndItem);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, fg);
    DrawTextW(dc, label.c_str(), -1, &textRect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
}

void NavigationChrome::DrawCloseButton(const DRAWITEMSTRUCT* item) {
    const int index = static_cast<int>(item->CtlID) - kTabCloseBaseId;
    const UiTheme theme = GetUiTheme(darkTheme_);
    const bool highContrast = UiSystemHighContrast();
    HDC dc = item->hDC;
    const RECT& rect = item->rcItem;
    const bool active = IsTabDrawItemActive(kTabBaseId + index,
                                            workspace_ ? workspace_->ActiveTabIndex() : 0);
    // ui-v3 shell: the close strip sits inside its tab pill, so it blends with
    // the pill fill; the × reveals on tab hover/focus or hovering the strip.
    const bool revealed = index == hoverCloseTab_ ||
                          kTabBaseId + index == hoverItemId_ ||
                          (item->itemState & ODS_FOCUS) != 0;
    const COLORREF backdrop = highContrast
        ? GetSysColor(active ? COLOR_HIGHLIGHT : COLOR_WINDOW)
        : ToColorRef(active ? theme.surfaceSubtle : theme.background);
    FillRectColor(dc, rect, backdrop);
    if (!revealed) return;
    const bool stripHovered = index == hoverCloseTab_;
    const COLORREF fg = highContrast
        ? GetSysColor(COLOR_WINDOWTEXT)
        : ToColorRef(stripHovered || active ? theme.text : theme.textSecondary);
    HFONT oldFont = nullptr;
    if (mdl2Font_) oldFont = static_cast<HFONT>(SelectObject(dc, mdl2Font_));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, fg);
    RECT glyphRect = rect;
    DrawTextW(dc, L"\uE8BB", -1, &glyphRect, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
    if (oldFont) SelectObject(dc, oldFont);
}

void NavigationChrome::DrawDriveItem(const DRAWITEMSTRUCT* item) {
    const UINT itemId = item->itemID;
    if (itemId == static_cast<UINT>(-1)) return;
    HDC dc = item->hDC;
    const UiTheme theme = GetUiTheme(darkTheme_);
    const bool highContrast = UiSystemHighContrast();
    const bool selected = (item->itemState & ODS_SELECTED) != 0;
    // ui-v3 shell: the closed-combo selection is a soft highlight, not a full
    // accent fill; High Contrast keeps the system highlight.
    const COLORREF bg = highContrast
        ? GetSysColor(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW)
        : (selected ? CompositeOver(theme.selectionSoft, theme.background)
                    : ToColorRef(theme.background));
    const COLORREF fg = highContrast
        ? GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT)
        : ToColorRef(theme.text);
    FillRectColor(dc, item->rcItem, bg);
    std::wstring text(512, L'\0');
    const int length = static_cast<int>(SendMessageW(item->hwndItem, CB_GETLBTEXT, itemId,
                                                       reinterpret_cast<LPARAM>(text.data())));
    text.resize(static_cast<size_t>((std::max)(0, length)));
    RECT textRect = item->rcItem;
    textRect.left += Scaled(4);
    textRect.right -= Scaled(4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, fg);
    DrawTextW(dc, text.c_str(), -1, &textRect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
}

void NavigationChrome::UpdateChromeBrush() {
    if (chromeBrush_) {
        DeleteObject(chromeBrush_);
        chromeBrush_ = nullptr;
    }
    if (editBrush_) {
        DeleteObject(editBrush_);
        editBrush_ = nullptr;
    }
    const UiTheme theme = GetUiTheme(darkTheme_);
    chromeBrush_ = CreateSolidBrush(ToColorRef(theme.background));
    // ui-v3 shell: the borderless address edit paints on an elevated field.
    editBrush_ = CreateSolidBrush(ToColorRef(theme.surfaceElevated));
}

void NavigationChrome::SetDarkTheme(bool dark) {
    darkTheme_ = dark;
    UpdateChromeBrush();
    if (!window_) return;
    InvalidateRect(window_, nullptr, FALSE);
    if (back_) InvalidateRect(back_, nullptr, FALSE);
    if (forward_) InvalidateRect(forward_, nullptr, FALSE);
    if (up_) InvalidateRect(up_, nullptr, FALSE);
    if (search_) InvalidateRect(search_, nullptr, FALSE);
    if (settings_) InvalidateRect(settings_, nullptr, FALSE);
    if (newTab_) InvalidateRect(newTab_, nullptr, FALSE);
    if (overflow_) InvalidateRect(overflow_, nullptr, FALSE);
    if (drives_) InvalidateRect(drives_, nullptr, FALSE);
    for (HWND tab : tabButtons_) InvalidateRect(tab, nullptr, FALSE);
    for (HWND close : tabCloseButtons_) InvalidateRect(close, nullptr, FALSE);
}

void NavigationChrome::SetCloseHover(HWND hwnd, bool hover) {
    const int newIndex = hover ? IndexOfCloseButton(hwnd) : -1;
    if (newIndex == hoverCloseTab_) return;
    const int oldIndex = hoverCloseTab_;
    hoverCloseTab_ = newIndex;
    if (oldIndex >= 0 && static_cast<size_t>(oldIndex) < tabCloseButtons_.size()) {
        InvalidateRect(tabCloseButtons_[static_cast<size_t>(oldIndex)], nullptr, FALSE);
    }
    if (newIndex >= 0 && static_cast<size_t>(newIndex) < tabCloseButtons_.size()) {
        InvalidateRect(tabCloseButtons_[static_cast<size_t>(newIndex)], nullptr, FALSE);
    }
}

int NavigationChrome::IndexOfCloseButton(HWND hwnd) const {
    for (size_t index = 0; index < tabCloseButtons_.size(); ++index) {
        if (tabCloseButtons_[index] == hwnd) return static_cast<int>(index);
    }
    return -1;
}

void NavigationChrome::Cleanup() {
    if (chromeBrush_) {
        DeleteObject(chromeBrush_);
        chromeBrush_ = nullptr;
    }
    if (editBrush_) {
        DeleteObject(editBrush_);
        editBrush_ = nullptr;
    }
    if (mdl2Font_) {
        DeleteObject(mdl2Font_);
        mdl2Font_ = nullptr;
    }
    if (bodyFont_) {
        DeleteObject(bodyFont_);
        bodyFont_ = nullptr;
    }
}

LRESULT CALLBACK NavigationChrome::WindowProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    NavigationChrome* self = nullptr;
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<NavigationChrome*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<NavigationChrome*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->HandleMessage(hwnd, message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK NavigationChrome::AddressEditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                                    UINT_PTR subclassId, DWORD_PTR referenceData) {
    UNREFERENCED_PARAMETER(subclassId);
    auto* self = reinterpret_cast<NavigationChrome*>(referenceData);
    if (message == WM_KEYDOWN && wParam == VK_RETURN) {
        if (self) self->CommitAddressEdit();
        return 0;
    }
    if (message == WM_KEYDOWN && wParam == VK_ESCAPE) {
        if (self) self->CancelAddressEdit();
        return 0;
    }
    if (message == WM_KILLFOCUS && self) {
        PostMessageW(self->window_, WM_APP_ADDRESS_FOCUS_LOST, 0, 0);
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK NavigationChrome::CloseButtonProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                                    UINT_PTR subclassId, DWORD_PTR referenceData) {
    UNREFERENCED_PARAMETER(subclassId);
    auto* self = reinterpret_cast<NavigationChrome*>(referenceData);
    if (self && message == WM_MOUSEMOVE) {
        self->SetCloseHover(hwnd, true);
        TRACKMOUSEEVENT tme{};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);
    } else if (self && message == WM_MOUSELEAVE) {
        self->SetCloseHover(hwnd, false);
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK NavigationChrome::HoverButtonProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                                   UINT_PTR subclassId, DWORD_PTR referenceData) {
    UNREFERENCED_PARAMETER(subclassId);
    auto* self = reinterpret_cast<NavigationChrome*>(referenceData);
    if (!self) return DefSubclassProc(hwnd, message, wParam, lParam);
    const int controlId = GetDlgCtrlID(hwnd);
    switch (message) {
        case WM_MOUSEMOVE:
            self->SetControlHover(hwnd, controlId, true);
            {
                TRACKMOUSEEVENT tme{};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
            }
            break;
        case WM_MOUSELEAVE:
            if (self->hoverItemId_ == controlId) self->SetControlHover(hwnd, controlId, false);
            break;
        case WM_LBUTTONDOWN:
            self->SetControlPressed(hwnd, controlId, true);
            break;
        case WM_LBUTTONUP:
        case WM_CAPTURECHANGED:
            if (self->pressedItemId_ == controlId) self->SetControlPressed(hwnd, controlId, false);
            break;
        case WM_DESTROY:
            // ui-v3 shell: a destroyed control can no longer be hovered/pressed.
            if (self->hoverItemId_ == controlId) self->hoverItemId_ = -1;
            if (self->pressedItemId_ == controlId) self->pressedItemId_ = -1;
            break;
        default:
            break;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

LRESULT NavigationChrome::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_SIZE:
            Reposition();
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            RECT client{};
            GetClientRect(hwnd, &client);
            DrawBreadcrumbs(dc, client);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_MOUSEMOVE: {
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            int hoverSegment = -1;
            for (size_t index = 0; index < breadcrumbRects_.size(); ++index) {
                if (PtInRect(&breadcrumbRects_[index], point)) {
                    hoverSegment = static_cast<int>(index);
                    break;
                }
            }
            if (hoverSegment != hoverSegment_) {
                hoverSegment_ = hoverSegment;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            return 0;
        }
        case WM_MOUSELEAVE:
            if (hoverSegment_ != -1) {
                hoverSegment_ = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONDOWN: {
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            for (size_t index = 0; index < breadcrumbRects_.size(); ++index) {
                if (!PtInRect(&breadcrumbRects_[index], point)) continue;
                // ui-v3 shell: the collapsed-run "…" placeholder navigates to
                // the deepest hidden ancestor; real segments keep their model
                // indices via breadcrumbIndices_.
                if (breadcrumbIndices_[index] < 0) {
                    if (!collapsedTargetPath_.empty()) {
                        NavigateTo(collapsedTargetPath_);
                        return 0;
                    }
                    continue;
                }
                if (workspace_ &&
                    workspace_->NavigateBreadcrumb(static_cast<size_t>(breadcrumbIndices_[index]))) {
                    NavigateTo(workspace_->ActiveContext().currentPath);
                    return 0;
                }
            }
            RECT client{};
            GetClientRect(hwnd, &client);
            const ffui::ChromeLayout chromeLayout = CurrentChromeLayout();
            const ffui::ChromeBand crumbBand = ffui::ChromeLocationBand(chromeLayout);
            const int crumbTop = Scaled(crumbBand.top);
            const int crumbBottom = crumbTop + Scaled(crumbBand.Height());
            if (point.y >= crumbTop && point.y < crumbBottom &&
                point.x >= Scaled(chromeLayout.addressLeft) && point.x < Scaled(chromeLayout.addressRight)) {
                BeginAddressEdit();
                return 0;
            }
            break;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id == kBackId && HIWORD(wParam) == BN_CLICKED && workspace_ && workspace_->GoBack()) {
                // quick-access-home 5.2: GoBack moved the model; sync the view
                // with the active context's typed location (Home stays Home).
                const auto& context = workspace_->ActiveContext();
                NavigateToLocation(ffui::LocationOf(context));
                return 0;
            }
            if (id == kForwardId && HIWORD(wParam) == BN_CLICKED && workspace_ && workspace_->GoForward()) {
                const auto& context = workspace_->ActiveContext();
                NavigateToLocation(ffui::LocationOf(context));
                return 0;
            }
            if (id == kUpId && HIWORD(wParam) == BN_CLICKED && toolCommand_) {
                toolCommand_(L"navigation.up");
                return 0;
            }
            if (id == kSearchId && HIWORD(wParam) == BN_CLICKED && toolCommand_) {
                toolCommand_(L"search.focus");
                return 0;
            }
            if (id == kSettingsId && HIWORD(wParam) == BN_CLICKED && toolCommand_) {
                toolCommand_(L"app.settings");
                return 0;
            }
            if (id == kOverflowId && HIWORD(wParam) == BN_CLICKED) {
                ShowTabList();
                return 0;
            }
            if (id == kDriveId && HIWORD(wParam) == CBN_DROPDOWN) {
                // Refresh on open so drives attached/ejected after startup are
                // reflected immediately (RebuildDrives preserves the selection).
                RebuildDrives();
                return 0;
            }
            if (id == kDriveId && HIWORD(wParam) == CBN_SELCHANGE) {
                const int index = static_cast<int>(SendMessageW(drives_, CB_GETCURSEL, 0, 0));
                if (index >= 0 && static_cast<size_t>(index) < drivePaths_.size()) {
                    if (workspace_) workspace_->Navigate(drivePaths_[static_cast<size_t>(index)]);
                    NavigateTo(drivePaths_[static_cast<size_t>(index)]);
                }
                return 0;
            }
            if (id == kNewTabId && HIWORD(wParam) == BN_CLICKED && workspace_) {
                if (beforeTabChange_) beforeTabChange_();
                workspace_->OpenTab(); // quick-access-home D7: new tab opens Home
                const auto& context = workspace_->ActiveContext();
                NavigateToLocation(ffui::LocationOf(context),
                                   true);
                return 0;
            }
            if (HIWORD(wParam) == BN_CLICKED && workspace_ && id >= kTabBaseId && id < kTabBaseId + static_cast<int>(workspace_->TabCount())) {
                const size_t index = static_cast<size_t>(id - kTabBaseId);
                const bool switching = index != workspace_->ActiveTabIndex();
                if (switching && beforeTabChange_) beforeTabChange_();
                if (workspace_->SwitchTab(index)) {
                    const auto& context = workspace_->ActiveContext();
                    NavigateToLocation(ffui::LocationOf(context),
                                       switching);
                }
                return 0;
            }
            if (HIWORD(wParam) == BN_CLICKED && workspace_ && id >= kTabCloseBaseId && id < kTabCloseBaseId + static_cast<int>(workspace_->TabCount())) {
                CloseTab(static_cast<size_t>(id - kTabCloseBaseId));
                return 0;
            }
            break;
        }
        case WM_DRAWITEM: {
            const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (item->CtlType == ODT_BUTTON) {
                const int id = static_cast<int>(item->CtlID);
                // ui-v3 shell: one owner-drawn icon-button family.
                if (id == kBackId) {
                    DrawIconButton(item, L"\uE72B", false);
                    return TRUE;
                }
                if (id == kForwardId) {
                    DrawIconButton(item, L"\uE72B", true);
                    return TRUE;
                }
                if (id == kUpId) {
                    DrawIconButton(item, L"\uE70E", false);
                    return TRUE;
                }
                if (id == kSearchId) {
                    DrawSearchField(item);
                    return TRUE;
                }
                if (id == kNewTabId) {
                    DrawIconButton(item, L"\uE710", false);
                    return TRUE;
                }
                if (id == kSettingsId) {
                    DrawIconButton(item, L"\uE713", false);
                    return TRUE;
                }
                if (id == kOverflowId) {
                    // ui-overhaul phase 1: tab-strip overflow affordance.
                    DrawIconButton(item, L"\uE712", false);
                    return TRUE;
                }
                if (id >= kTabBaseId && id < kTabBaseId + static_cast<int>(tabButtons_.size())) {
                    DrawTabButton(item);
                    return TRUE;
                }
                if (id >= kTabCloseBaseId && id < kTabCloseBaseId + static_cast<int>(tabCloseButtons_.size())) {
                    DrawCloseButton(item);
                    return TRUE;
                }
            } else if (item->CtlType == ODT_COMBOBOX) {
                DrawDriveItem(item);
                return TRUE;
            }
            break;
        }
        case WM_CTLCOLOREDIT: {
            // ui-v3 shell: theme the borderless address edit onto its elevated
            // field so edit mode stays continuous with the breadcrumb row.
            HDC editDc = reinterpret_cast<HDC>(wParam);
            const bool highContrast = UiSystemHighContrast();
            const UiTheme theme = GetUiTheme(darkTheme_);
            SetBkColor(editDc, highContrast ? GetSysColor(COLOR_WINDOW) : ToColorRef(theme.surfaceElevated));
            SetTextColor(editDc, highContrast ? GetSysColor(COLOR_WINDOWTEXT)
                                              : ToColorRef(theme.searchText));
            return reinterpret_cast<LRESULT>(highContrast ? GetSysColorBrush(COLOR_WINDOW) : editBrush_);
        }
        case WM_CTLCOLORBTN: {
            HDC controlDc = reinterpret_cast<HDC>(wParam);
            const bool highContrast = UiSystemHighContrast();
            const UiTheme theme = GetUiTheme(darkTheme_);
            const bool enabled = IsWindowEnabled(reinterpret_cast<HWND>(lParam)) != 0;
            const COLORREF bg = highContrast ? GetSysColor(COLOR_WINDOW) : ToColorRef(theme.background);
            const COLORREF fg = highContrast
                ? GetSysColor(enabled ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT)
                : ToColorRef(enabled ? theme.text : theme.textSecondary);
            SetBkColor(controlDc, bg);
            SetTextColor(controlDc, fg);
            return reinterpret_cast<LRESULT>(highContrast ? GetSysColorBrush(COLOR_WINDOW) : chromeBrush_);
        }
        case WM_CTLCOLORLISTBOX: {
            HDC listDc = reinterpret_cast<HDC>(wParam);
            const bool highContrast = UiSystemHighContrast();
            const UiTheme theme = GetUiTheme(darkTheme_);
            SetBkColor(listDc, highContrast ? GetSysColor(COLOR_WINDOW) : ToColorRef(theme.background));
            SetTextColor(listDc, highContrast ? GetSysColor(COLOR_WINDOWTEXT) : ToColorRef(theme.text));
            return reinterpret_cast<LRESULT>(highContrast ? GetSysColorBrush(COLOR_WINDOW) : chromeBrush_);
        }
        case WM_APP_ADDRESS_FOCUS_LOST:
            HandleAddressFocusLost();
            return 0;
        case WM_SETFOCUS:
            if (editing_) SetFocus(addressEdit_);
            return 0;
        case WM_DESTROY:
            Cleanup();
            return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace ffui
