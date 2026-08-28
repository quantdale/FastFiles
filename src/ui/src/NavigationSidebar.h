#pragma once

#include <functional>
#include <string>
#include <vector>
#include <windows.h>

#include "NavigationWorkspace.h"
#include "UITheme.h"

namespace ffui {

class NavigationSidebar {
public:
    struct NavigationTarget {
        std::wstring path;
        std::wstring displayName;
        bool openInNewTab = false;
        // quick-access-home 5.1: true when the target is the Home surface (its
        // path is empty; checked by the shell before the filesystem branches).
        bool isHome = false;
        // final-product-completion-and-production-readiness 2.1: tool workspaces
        // also have empty paths and must not be treated as This PC / unavailable.
        bool isSearch = false;
        bool isStorage = false;
    };
    using NavigateHandler = std::function<void(const NavigationTarget&)>;

    bool Initialize(HWND owner, NavigationWorkspace* workspace, NavigateHandler navigate);
    void Reposition();
    void Refresh();
    int Width() const;
    void SetDarkTheme(bool dark);
    // ui-v3 hardening: true only when the selected glyph font maps the codepoint.
    bool GlyphMapped(wchar_t glyph);
    // quick-access-home 5.1: invoked after the context-menu bookmark edits
    // (rename/remove/reorder) commit so the shell can refresh the Home surface.
    void SetBookmarksChangedCallback(std::function<void()> cb);
    // ui-v3 sidebar: keyboard-focus seam. The shell can drive focus across nav
    // items without owning the row layout; -1 clears. The focus ring renders
    // independently of hover and selection.
    void SetFocusedRow(int row);
    // final-product-completion-and-production-readiness 2.2: selected-state for
    // Fast Search / Storage Analysis while those workspaces occupy contentRect.
    enum class ActiveWorkspace { Browse, Home, Search, Storage };
    void SetActiveWorkspace(ActiveWorkspace workspace);

private:
    enum class RowKind { Section, Item };
    struct Row {
        RowKind kind;
        int section;
        size_t item;
        RECT bounds{};
        std::wstring label;
        std::wstring path;
        bool home = false;
        bool search = false;
        bool storage = false;
    };
    // ui-v3 sidebar: interactive targets of the collapsed icon rail.
    enum class RailHit { None, Home, Search, Storage, Expand };

    static LRESULT CALLBACK WindowProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    void RebuildRows();
    void Draw(HDC dc, const RECT& client);
    void ToggleSection(int section);

    // ui-v3 sidebar: density-driven row metrics (physical pixels).
    int ItemHeightPx() const;
    int SectionHeightPx() const;
    int HitTestRow(const POINT& point) const;
    // quick-access-home 5.1 selection identity: true when the row's path is the
    // browsed location (or an ancestor of it); Home matches a Home context.
    bool IsRowCurrent(const Row& row) const;
    void ActivateRow(int index, bool openInNewTab);
    int NextItemRow(int current, int direction) const;
    void DrawRail(HDC dc, const RECT& client, const UiTheme& theme, bool highContrast, COLORREF background);
    RailHit HitTestRail(const POINT& point, const RECT& client) const;
    void EnsureFonts();
    void ReleaseFonts();
    void EnsureTooltip();
    void RefreshTools(const RECT& client);

    HWND owner_ = nullptr;
    HWND window_ = nullptr;
    HWND tooltip_ = nullptr;
    NavigationWorkspace* workspace_ = nullptr;
    NavigateHandler navigate_;
    std::function<void()> bookmarksChanged_;
    std::vector<Row> rows_;
    bool darkTheme_ = false;
    int hoveredRow_ = -1;
    // ui-v3 sidebar: transient press feedback and keyboard-focus tracking.
    int pressedRow_ = -1;
    int focusedRow_ = -1;
    RailHit hoveredRail_ = RailHit::None;
    RailHit pressedRail_ = RailHit::None;
    // ui-v3 sidebar: shared GDI fonts (body/caption/glyph), recreated on DPI change.
    HFONT bodyFont_ = nullptr;
    HFONT captionFont_ = nullptr;
    HFONT glyphFont_ = nullptr;
    bool glyphsAvailable_ = false;
    // ui-v3 hardening: per-glyph coverage (face-name match alone painted
    // tofu on builds where individual MDL2 codepoints are unmapped).
    bool glyphsHome_ = false;
    bool glyphsFolder_ = false;
    bool glyphsSearch_ = false;
    bool glyphsStorage_ = false;
    ActiveWorkspace activeWorkspace_ = ActiveWorkspace::Browse;
    float fontScale_ = 0.0f;
    // ui-v3 sidebar: tooltip tools are (re)added after the next paint lays out
    // row rects; tooltipIds_ tracks what must be removed first.
    bool toolsDirty_ = true;
    std::vector<UINT_PTR> tooltipIds_;
    std::wstring tooltipText_;
};

} // namespace ffui
