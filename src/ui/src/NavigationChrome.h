#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>
#include <windows.h>

#include "ffui/DisplayPath.h"
#include "NavigationWorkspace.h"

namespace ffui {

// Base control id for tab buttons; tab index = controlId - kTabBaseId.
constexpr int kTabBaseId = 8300;

// Pure owner-draw active-tab identity: the tab pill for `controlId` renders as
// active exactly when it is the workspace's current tab. Used by DrawTabButton
// and kept headless-testable here.
inline bool IsTabDrawItemActive(int controlId, size_t activeTabIndex) {
    return controlId >= kTabBaseId && static_cast<size_t>(controlId - kTabBaseId) == activeTabIndex;
}

// Display-only path hygiene for user-visible text (tab labels): the Win32
// extended-length prefix is an API detail that navigation semantics rely on
// (long paths), so the model keeps it — but no label should ever print it.
inline std::wstring StripExtendedLengthPrefix(std::wstring path) {
    return StripDisplayPathPrefix(std::move(path));
}

// Pure tab-label derivation: "N: " + leaf, with Home/This-PC special cases and
// extended-length prefixes stripped from display. Headless-testable seam used
// by RebuildTabs.
inline std::wstring TabLabelForLocation(size_t oneBasedIndex, const NavigationLocation& location) {
    std::wstring label = std::to_wstring(oneBasedIndex) + L": ";
    if (location.IsHome()) {
        label += L"Home";
        return label;
    }
    if (location.path.empty()) {
        label += L"This PC";
        return label;
    }
    const std::wstring display = NormalizeDisplayPath(location.path);
    const size_t slash = display.find_last_of(L"\\/");
    label += slash == std::wstring::npos || slash + 1 >= display.size()
        ? display : display.substr(slash + 1);
    return label;
}

class NavigationChrome {
public:
    // The second argument is true only when a tab transition should restore
    // the target tab's saved ColumnView state instead of treating the location
    // as a fresh navigation.  Address, breadcrumb, drive, and history changes
    // remain ordinary navigations. quick-access-home 5.2: the location is now
    // typed so back/forward/new-tab/tab-switch can carry the Home kind.
    using NavigateHandler = std::function<void(const ffui::NavigationLocation&, bool restoreTabState)>;
    using BeforeTabChangeHandler = std::function<void()>;
    using ToolCommandHandler = std::function<void(const std::wstring& commandId)>;

    ~NavigationChrome();
    bool Initialize(HWND owner, NavigationWorkspace* workspace, NavigateHandler navigate,
                    BeforeTabChangeHandler beforeTabChange = {},
                    ToolCommandHandler toolCommand = {});
    void Reposition();
    void Refresh();
    void FocusAddressBar();
    void SetDarkTheme(bool dark);
    void SetSearchActive(bool active);
    bool Visible() const { return window_ != nullptr; }

private:
    static LRESULT CALLBACK WindowProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK AddressEditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                            UINT_PTR subclassId, DWORD_PTR referenceData);
    static LRESULT CALLBACK CloseButtonProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                            UINT_PTR subclassId, DWORD_PTR referenceData);
    // ui-v3 shell: generic hover/pressed tracking for owner-draw buttons
    // (nav buttons and tab pills).
    static LRESULT CALLBACK HoverButtonProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                            UINT_PTR subclassId, DWORD_PTR referenceData);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void BeginAddressEdit();
    void CommitAddressEdit();
    void CancelAddressEdit();
    void EndAddressEdit(bool restoreFocus);
    void HandleAddressFocusLost();
    void NavigateTo(const std::wstring& path, bool restoreTabState = false);
    // quick-access-home 5.2: typed navigation entry. Filesystem destinations
    // (address/drive/breadcrumb commits) go through the FromPath wrapper below;
    // back/forward/new-tab/tab-switch pass the active context's location so
    // Home reaches the shell as a Home location, never as an empty path.
    void NavigateToLocation(const NavigationLocation& location, bool restoreTabState = false);
    void RebuildDrives();
    void DrawBreadcrumbs(HDC dc, const RECT& client);
    void RebuildTabs();
    void RepositionTabs();
    void CloseTab(size_t index);
    void UpdateChromeBrush();
    // ui-v3 shell: shared icon-button presentation for Back/Forward/New Tab —
    // one family, hover/pressed/disabled/focus resolved identically.
    void DrawIconButton(const DRAWITEMSTRUCT* item, const wchar_t* glyph, bool mirrorGlyph);
    void DrawSearchField(const DRAWITEMSTRUCT* item);
    void SetControlHover(HWND control, int controlId, bool hover);
    void SetControlPressed(HWND control, int controlId, bool pressed);
    void SetCloseHover(HWND hwnd, bool hover);
    int IndexOfCloseButton(HWND hwnd) const;
    void DrawTabButton(const DRAWITEMSTRUCT* item);
    void DrawCloseButton(const DRAWITEMSTRUCT* item);
    void DrawDriveItem(const DRAWITEMSTRUCT* item);
    void Cleanup();

    HWND owner_ = nullptr;
    HWND window_ = nullptr;
    HWND back_ = nullptr;
    HWND forward_ = nullptr;
    HWND up_ = nullptr;
    HWND drives_ = nullptr;
    HWND addressEdit_ = nullptr;
    HWND search_ = nullptr;
    HWND settings_ = nullptr;
    HWND newTab_ = nullptr;
    NavigationWorkspace* workspace_ = nullptr;
    NavigateHandler navigate_;
    BeforeTabChangeHandler beforeTabChange_;
    ToolCommandHandler toolCommand_;
    bool searchActive_ = false;
    std::vector<std::wstring> drivePaths_;
    std::vector<RECT> breadcrumbRects_;
    // ui-v3 shell: original breadcrumb index per painted rect; -1 marks the
    // collapsed-run "…" placeholder (navigates to collapsedTargetPath_).
    std::vector<int> breadcrumbIndices_;
    std::vector<HWND> tabButtons_;
    std::vector<HWND> tabCloseButtons_;
    bool editing_ = false;
    bool darkTheme_ = false;
    int hoverSegment_ = -1;
    int hoverCloseTab_ = -1;
    // ui-v3 shell: hover/pressed tracking for the shared icon-button family
    // and tab pills (control ids, -1 = none).
    int hoverItemId_ = -1;
    int pressedItemId_ = -1;
    HBRUSH chromeBrush_ = nullptr;
    HBRUSH editBrush_ = nullptr;
    HFONT mdl2Font_ = nullptr;
    HFONT bodyFont_ = nullptr;
    // ui-v3 shell: target path behind the collapsed-breadcrumb "…" segment.
    std::wstring collapsedTargetPath_;
};

} // namespace ffui
