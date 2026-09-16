#pragma once
// semantic-uia-core-surfaces 1.3: the pure projection seam between the UIA
// provider layer and the real render/interaction state.
//
// The provider tree depends ONLY on this header (plus provider-layer
// helpers). `ColumnView` and `SearchPanel` implement `UiaSurface` with thin
// forwarding under their existing locks, so the providers project the exact
// state used for rendering and input -- there is no parallel
// accessibility-only state model. Tests implement a fake `UiaSurface`
// (headless; no window, renderer, or engine), following the established
// seam pattern of `HomeLayout.h` / `ShellLayout.h`.
//
// All geometry below is in DIPs in the navigation-viewport coordinate space
// (origin = the viewport's top-left), except where a rect is explicitly
// marked screen/physical. Rows follow the render math:
//   columnX  = paneX + column * kColumnWidth - horizontalScroll
//   contentTop = kBadgeHeight + (statusLine ? 24 : 0)
//   itemY    = contentTop + row * BrowseRowHeight() - verticalScroll
// The renderer applies the same formulas (ColumnView::Render); the provider
// layer owns the DIP->physical->screen conversion (UiaGeometry.h).

#include <cstdint>
#include <string>
#include <windows.h>

#include "ffui/HomeLayout.h" // HomeSectionKind values + layout geometry

namespace ffui {

// HomeSectionKind values mirrored for the provider layer (they match
// ffui::HomeSectionKind exactly; kept as plain ints so the seam stays
// header-light).
inline constexpr int kUiaHomeSectionPinned = 0;
inline constexpr int kUiaHomeSectionRecentFolders = 1;
inline constexpr int kUiaHomeSectionRecentSearches = 2;
inline constexpr int kUiaHomeSectionCommonLocations = 3;
inline constexpr int kUiaHomeSectionQuickActions = 4;

// Home focus target kinds (mirror HomeFocusTarget; plain ints).
inline constexpr int kUiaHomeFocusNone = 0;
inline constexpr int kUiaHomeFocusItem = 1;
inline constexpr int kUiaHomeFocusClearRecentFolders = 2;
inline constexpr int kUiaHomeFocusClearRecentSearches = 3;

// Clear-affordance kinds for the Home surface.
inline constexpr int kUiaHomeClearRecentFolders = 0;
inline constexpr int kUiaHomeClearRecentSearches = 1;

// Column failure/status states (mirror the rendered snapshot states that
// decide whether a status line is drawn and what text it shows).
enum class UiaColumnStatus {
    Current,     // no status line; items rendered normally
    Loading,     // "Refreshing…" status line (+24 DIP content offset)
    Unavailable, // error message replaces items
    Empty,       // "This folder is empty" message
};

// Live geometry for the navigation surface, pushed by the shell on layout /
// DPI changes and every render (plain floats; cheap). `keyboardFocus` is the
// real "does the main HWND own keyboard focus" state, updated on
// WM_SETFOCUS/WM_KILLFOCUS and at render.
struct UiaViewGeometry {
    float originXDip = 0.0f;    // viewport origin in client DIPs
    float originYDip = 0.0f;
    float widthDip = 0.0f;      // viewport size in DIPs
    float heightDip = 0.0f;
    float dpiScale = 1.0f;      // physical / DIP
    float hScroll0Dip = 0.0f;   // pane 0 horizontal offset (shell-animated)
    float hScroll1Dip = 0.0f;   // pane 1 horizontal offset
    HWND hwnd = nullptr;        // main window (ClientToScreen / focus checks)
    bool keyboardFocus = false; // GetFocus() == hwnd (shell-maintained)
    uint64_t revision = 0;      // bumped on every update (cache validation)
};

// One rendered column.
struct UiaColumnInfo {
    std::wstring path;           // full path; empty = "This PC" synthetic root
    std::wstring displayPath;    // display-only path; identity remains `path`
    int itemCount = 0;
    UiaColumnStatus status = UiaColumnStatus::Current;
    std::wstring statusText;     // error / refreshing / empty message
};

// One column item (file/folder row).
struct UiaItemInfo {
    std::wstring name;
    bool isDirectory = false;
    uint64_t sizeBytes = 0;
    uint32_t attributes = 0;
    // Stable identity, taken verbatim from the real model (0 = absent):
    int64_t volumeRowId = 0;
    uint64_t fileIdLow = 0;
    uint64_t fileIdHigh = 0;
    std::wstring fullPath;       // column path + name (identity fallback)
    std::wstring displayFullPath; // display-only path; identity remains `fullPath`
    bool selected = false;
    bool focused = false;        // row == the column's focusIndex
};

// One Home surface item (bounded <= kHomeMaxItems = 200 by the layout seam).
struct UiaHomeItemInfo {
    int sectionKind = kUiaHomeSectionPinned;
    int indexInSection = 0;
    int globalIndex = 0;
    std::wstring primary;        // display name
    std::wstring secondary;      // context line (folder paths)
    std::wstring value;          // activation value (path / query / command id)
    bool enabled = true;         // false = unavailable known folder / hint
    // Layout rect (Home-layout coordinates; y relative to the content top).
    float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
};

// One Home section (header + item grid slice).
struct UiaHomeSectionInfo {
    int sectionKind = kUiaHomeSectionPinned;
    int itemCount = 0;
    int firstGlobalIndex = 0;
    float headerX = 0.0f, headerY = 0.0f, headerWidth = 0.0f, headerHeight = 0.0f;
    bool clearAffordance = false; // Recent folders/searches sections with items
};

// Home scroll geometry (per pane).
struct UiaHomeScrollInfo {
    int columnCount = 1;
    float contentHeightDip = 0.0f;
};

// One search result (mirror of ffsearch::Candidate + list state).
struct UiaSearchResultInfo {
    std::wstring name;
    std::wstring folder;
    std::wstring displayFolder;  // display-only path; identity remains `folder`
    uint64_t sizeBytes = 0;
    uint64_t modifiedTime = 0;   // FILETIME ticks
    bool isDirectory = false;
    int64_t volumeId = 0;        // 0 = no indexed identity
    uint64_t id = 0;
    std::wstring fullPath;       // folder + name (identity fallback)
    bool selected = false;
    bool focused = false;
    // indexed-search-allocated-size D7/D9: on-disk allocated size + explicit
    // knownness, projected verbatim from the rendered result so the accessible
    // description and the visible row agree. Default-constructed == unknown.
    uint64_t allocatedSizeBytes = 0;
    bool allocatedSizeKnown = false;
};

// The one seam. Non-pure members have safe defaults so a test fake (or a
// surface that owns only part of the semantics) overrides only what it
// needs. Action methods MUST route into the exact real mutation logic used
// by keyboard/mouse input; they are invoked after the surface released its
// locks (see the provider controller), so UIA callbacks can never deadlock.
class UiaSurface {
public:
    virtual ~UiaSurface() = default;

    // --- navigation surface (ColumnView) ---
    virtual UiaViewGeometry UiaGeometry() const { return {}; }
    // The shell details pane publishes its current terminal state through the
    // root help text.  This is a user-visible accessibility projection, not a
    // test-only channel: screen readers and black-box diagnostics can tell
    // Loading from Ready, Unsupported, or Disabled without scraping pixels.
    virtual std::wstring UiaDetailsStatus() const { return {}; }
    virtual int UiaPaneCount() const { return 0; }
    virtual int UiaActivePane() const { return 0; }
    virtual bool UiaPaneIsHome(int pane) const { (void)pane; return false; }
    virtual int UiaColumnCount(int pane) const { (void)pane; return 0; }
    virtual UiaColumnInfo UiaColumnInfoOf(int pane, int col) const { (void)pane; (void)col; return {}; }
    virtual UiaItemInfo UiaItemInfoOf(int pane, int col, int row) const { (void)pane; (void)col; (void)row; return {}; }
    virtual int UiaFocusedColumn(int pane) const { (void)pane; return 0; }
    virtual int UiaFocusedRow(int pane, int column) const { (void)pane; (void)column; return -1; }
    virtual float UiaVerticalScroll(int pane) const { (void)pane; return 0.0f; }

    // --- Home surface (ColumnView) ---
    virtual int UiaHomeItemCount(int pane) const { (void)pane; return 0; }
    virtual UiaHomeItemInfo UiaHomeItemInfoOf(int pane, int globalIndex) const { (void)pane; (void)globalIndex; return {}; }
    virtual int UiaHomeSectionCount(int pane) const { (void)pane; return 0; }
    virtual UiaHomeSectionInfo UiaHomeSectionInfoOf(int pane, int section) const { (void)pane; (void)section; return {}; }
    virtual UiaHomeScrollInfo UiaHomeScrollInfoOf(int pane) const { (void)pane; return {}; }
    virtual int UiaHomeFocusTarget(int pane) const { (void)pane; return kUiaHomeFocusNone; }
    virtual int UiaHomeFocusIndex(int pane) const { (void)pane; return -1; }

    // --- actions (navigation) ---
    virtual void UiaActivatePane(int pane) { (void)pane; }
    virtual void UiaSelectItem(int pane, int col, int row) { (void)pane; (void)col; (void)row; }
    virtual void UiaToggleItem(int pane, int col, int row) { (void)pane; (void)col; (void)row; }
    virtual void UiaRangeSelectItem(int pane, int col, int row) { (void)pane; (void)col; (void)row; }
    virtual void UiaActivateItem(int pane, int col, int row) { (void)pane; (void)col; (void)row; }
    virtual void UiaSetItemFocus(int pane, int col, int row) { (void)pane; (void)col; (void)row; }
    virtual void UiaSetColumnFocus(int pane, int col) { (void)pane; (void)col; }
    virtual void UiaScrollPane(int pane, float deltaX, float deltaY) { (void)pane; (void)deltaX; (void)deltaY; }
    virtual void UiaSetHorizontalScroll(int pane, float offsetDip) { (void)pane; (void)offsetDip; }
    virtual void UiaScrollItemIntoView(int pane, int col, int row) { (void)pane; (void)col; (void)row; }

    // --- actions (Home) ---
    virtual void UiaActivateHomeItem(int pane, int globalIndex, bool openInNewTab) { (void)pane; (void)globalIndex; (void)openInNewTab; }
    virtual void UiaActivateHomeClear(int pane, int clearKind) { (void)pane; (void)clearKind; }
    virtual void UiaSetHomeFocusItem(int pane, int globalIndex) { (void)pane; (void)globalIndex; }
    virtual void UiaSetHomeFocusClear(int pane, int clearKind) { (void)pane; (void)clearKind; }
    virtual void UiaScrollHomeItemIntoView(int pane, int globalIndex) { (void)pane; (void)globalIndex; }

    // --- search surface (SearchPanel) ---
    virtual size_t UiaSearchResultCount() const { return 0; }
    virtual UiaSearchResultInfo UiaSearchResultInfoOf(size_t index) const { (void)index; return {}; }
    virtual bool UiaSearchResultRect(size_t index, RECT& outScreen) const { (void)index; (void)outScreen; return false; }
    virtual bool UiaSearchResultOffscreen(size_t index) const { (void)index; return true; }
    virtual bool UiaSearchVisible() const { return false; }
    virtual bool UiaSearchHasKeyboardFocus() const { return false; }
    virtual HWND UiaSearchHwnd() const { return nullptr; }
    virtual void UiaSearchSelectItem(size_t index) { (void)index; }
    virtual void UiaSearchAddToSelection(size_t index) { (void)index; }
    virtual void UiaSearchRemoveFromSelection(size_t index) { (void)index; }
    virtual void UiaSearchSetFocusItem(size_t index) { (void)index; }
    virtual void UiaSearchActivateItem(size_t index) { (void)index; }
    virtual void UiaSearchScrollIntoView(size_t index) { (void)index; }
};

} // namespace ffui
