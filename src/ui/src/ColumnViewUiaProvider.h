#pragma once
// semantic-uia-core-surfaces 2.x/3.x: UIA provider tree for the Column View
// navigation surface, including the Home pane mode (design D4).
//
// One fragment root (the Window element returned from WM_GETOBJECT) hosts:
//   Window "FastFiles" (fragment root + controller)
//    └─ Group "Column View"
//        ├─ Pane.0 (Scroll pattern)
//        │   ├─ (Home mode) Group "Home" -> section Groups -> items/Buttons
//        │   └─ (filesystem) List columns (Selection + ItemContainer)
//        │       -> ListItems (SelectionItem + Invoke + ScrollItem)
//        └─ Pane.1 (only while dual-pane is active)
//
// Lifetime (design D6): children hold a raw pointer to the controller (the
// fragment root). UIAutomationCore keeps the fragment root alive as long as
// any element of the fragment is referenced by a client; the shared
// UiaOwnerState token is flipped dead by the owning surface before the tree
// is released (WM_DESTROY), so a client that outlives the window gets clean
// UIA_E_ELEMENTNOTAVAILABLE failures instead of use-after-free.
//
// Virtualization (design D13): item providers are created on demand and
// cached per column with hard caps; structure changes drop the caches;
// FindItemByProperty scans the surface model without materializing rows.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <windows.h>
#include <wrl/client.h>

#include "ffui/UiaSurface.h"
#include "UiaGeometry.h"
#include "UiaNotifications.h"
#include "UiaProviderBase.h" // also provides uiautomationcore.h + OLE prerequisites
#include "UiaRuntimeIds.h"

namespace ffui {

class ColumnViewUiaProvider;
class ColumnViewGroupUiaProvider;
class PaneUiaProvider;
class ColumnUiaProvider;
class ColumnItemUiaProvider;
class ColumnStatusUiaProvider;
class HomeGroupUiaProvider;
class HomeSectionUiaProvider;
class HomeItemUiaProvider;
class HomeClearUiaProvider;

// Group "Column View": the single child of the root; hosts the panes.
class ColumnViewGroupUiaProvider final : public UiaProviderBase,
                                         public IRawElementProviderFragment {
public:
    ColumnViewGroupUiaProvider(ColumnViewUiaProvider* owner);

    FFUIA_QI_BEGIN()
        FFUIA_QI_ENTRY(IID_IRawElementProviderFragment, IRawElementProviderFragment)
    FFUIA_QI_END()

    FFUIA_REFCOUNT()

    STDMETHODIMP GetPatternProvider(PATTERNID, IUnknown** retVal) noexcept override;
    STDMETHODIMP GetPropertyValue(PROPERTYID propertyId, VARIANT* retVal) noexcept override;
    STDMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** retVal) noexcept override;
    STDMETHODIMP GetRuntimeId(SAFEARRAY** retVal) noexcept override;
    STDMETHODIMP get_BoundingRectangle(UiaRect* retVal) noexcept override;
    STDMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** retVal) noexcept override;
    STDMETHODIMP SetFocus() noexcept override;
    STDMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** retVal) noexcept override;

private:
    ColumnViewUiaProvider* owner_ = nullptr;
};

// Pane.N: the per-pane viewport; owns the Scroll pattern.
class PaneUiaProvider final : public UiaProviderBase,
                              public IRawElementProviderFragment,
                              public IScrollProvider {
public:
    PaneUiaProvider(ColumnViewUiaProvider* owner, int pane);

    FFUIA_QI_BEGIN()
        FFUIA_QI_ENTRY(IID_IRawElementProviderFragment, IRawElementProviderFragment)
        FFUIA_QI_ENTRY(IID_IScrollProvider, IScrollProvider)
    FFUIA_QI_END()

    FFUIA_REFCOUNT()

    STDMETHODIMP GetPatternProvider(PATTERNID patternId, IUnknown** retVal) noexcept override;
    STDMETHODIMP GetPropertyValue(PROPERTYID propertyId, VARIANT* retVal) noexcept override;
    STDMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** retVal) noexcept override;
    STDMETHODIMP GetRuntimeId(SAFEARRAY** retVal) noexcept override;
    STDMETHODIMP get_BoundingRectangle(UiaRect* retVal) noexcept override;
    STDMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** retVal) noexcept override;
    STDMETHODIMP SetFocus() noexcept override;
    STDMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** retVal) noexcept override;

    // IScrollProvider
    STDMETHODIMP Scroll(enum ScrollAmount horizontalAmount, enum ScrollAmount verticalAmount) noexcept override;
    STDMETHODIMP SetScrollPercent(double horizontalPercent, double verticalPercent) noexcept override;
    STDMETHODIMP get_HorizontalScrollPercent(double* retVal) noexcept override;
    STDMETHODIMP get_VerticalScrollPercent(double* retVal) noexcept override;
    STDMETHODIMP get_HorizontalViewSize(double* retVal) noexcept override;
    STDMETHODIMP get_VerticalViewSize(double* retVal) noexcept override;
    STDMETHODIMP get_HorizontallyScrollable(BOOL* retVal) noexcept override;
    STDMETHODIMP get_VerticallyScrollable(BOOL* retVal) noexcept override;

    int Pane() const noexcept { return pane_; }

private:
    ColumnViewUiaProvider* owner_ = nullptr;
    int pane_ = 0;
};

// Pane.N.Column.M: a directory listing column; Selection + ItemContainer.
class ColumnUiaProvider final : public UiaProviderBase,
                                public IRawElementProviderFragment,
                                public ISelectionProvider,
                                public IItemContainerProvider {
public:
    ColumnUiaProvider(ColumnViewUiaProvider* owner, int pane, int column, uint64_t pathHash);

    FFUIA_QI_BEGIN()
        FFUIA_QI_ENTRY(IID_IRawElementProviderFragment, IRawElementProviderFragment)
        FFUIA_QI_ENTRY(IID_ISelectionProvider, ISelectionProvider)
        FFUIA_QI_ENTRY(IID_IItemContainerProvider, IItemContainerProvider)
    FFUIA_QI_END()

    FFUIA_REFCOUNT()

    STDMETHODIMP GetPatternProvider(PATTERNID patternId, IUnknown** retVal) noexcept override;
    STDMETHODIMP GetPropertyValue(PROPERTYID propertyId, VARIANT* retVal) noexcept override;
    STDMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** retVal) noexcept override;
    STDMETHODIMP GetRuntimeId(SAFEARRAY** retVal) noexcept override;
    STDMETHODIMP get_BoundingRectangle(UiaRect* retVal) noexcept override;
    STDMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** retVal) noexcept override;
    STDMETHODIMP SetFocus() noexcept override;
    STDMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** retVal) noexcept override;

    // ISelectionProvider
    STDMETHODIMP GetSelection(SAFEARRAY** retVal) noexcept override;
    STDMETHODIMP get_CanSelectMultiple(BOOL* retVal) noexcept override;
    STDMETHODIMP get_IsSelectionRequired(BOOL* retVal) noexcept override;

    // IItemContainerProvider
    STDMETHODIMP FindItemByProperty(IRawElementProviderSimple* startAfter,
                                    PROPERTYID propertyId, VARIANT value,
                                    IRawElementProviderSimple** retVal) noexcept override;

    int Pane() const noexcept { return pane_; }
    int Column() const noexcept { return column_; }
    uint64_t PathHash() const noexcept { return pathHash_; }

private:
    ColumnViewUiaProvider* owner_ = nullptr;
    int pane_ = 0;
    int column_ = 0;
    uint64_t pathHash_ = 0;
};

// One file/folder row: SelectionItem + Invoke + ScrollItem.
class ColumnItemUiaProvider final : public UiaProviderBase,
                                    public IRawElementProviderFragment,
                                    public ISelectionItemProvider,
                                    public IInvokeProvider,
                                    public IScrollItemProvider {
public:
    ColumnItemUiaProvider(ColumnViewUiaProvider* owner, int pane, int column, int row,
                          uint64_t pathHash, uint64_t idHash);

    FFUIA_QI_BEGIN()
        FFUIA_QI_ENTRY(IID_IRawElementProviderFragment, IRawElementProviderFragment)
        FFUIA_QI_ENTRY(IID_ISelectionItemProvider, ISelectionItemProvider)
        FFUIA_QI_ENTRY(IID_IInvokeProvider, IInvokeProvider)
        FFUIA_QI_ENTRY(IID_IScrollItemProvider, IScrollItemProvider)
    FFUIA_QI_END()

    FFUIA_REFCOUNT()

    STDMETHODIMP GetPatternProvider(PATTERNID patternId, IUnknown** retVal) noexcept override;
    STDMETHODIMP GetPropertyValue(PROPERTYID propertyId, VARIANT* retVal) noexcept override;
    STDMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** retVal) noexcept override;
    STDMETHODIMP GetRuntimeId(SAFEARRAY** retVal) noexcept override;
    STDMETHODIMP get_BoundingRectangle(UiaRect* retVal) noexcept override;
    STDMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** retVal) noexcept override;
    STDMETHODIMP SetFocus() noexcept override;
    STDMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** retVal) noexcept override;

    // ISelectionItemProvider
    STDMETHODIMP Select() noexcept override;
    STDMETHODIMP AddToSelection() noexcept override;
    STDMETHODIMP RemoveFromSelection() noexcept override;
    STDMETHODIMP get_IsSelected(BOOL* retVal) noexcept override;
    STDMETHODIMP get_SelectionContainer(IRawElementProviderSimple** retVal) noexcept override;

    // IInvokeProvider
    STDMETHODIMP Invoke() noexcept override;

    // IScrollItemProvider
    STDMETHODIMP ScrollIntoView() noexcept override;

    int Pane() const noexcept { return pane_; }
    int Column() const noexcept { return column_; }
    int Row() const noexcept { return row_; }
    uint64_t PathHash() const noexcept { return pathHash_; }

private:
    // Verifies the element's identity still exists in the current model and
    // resolves the live item state. False = stale element (fail cleanly,
    // never retarget the new occupant of the same row index).
    bool Resolve(UiaItemInfo& item, bool& statusLineShown) const;

    ColumnViewUiaProvider* owner_ = nullptr;
    int pane_ = 0;
    int column_ = 0;
    int row_ = 0;
    uint64_t pathHash_ = 0;
    uint64_t idHash_ = 0;
};

// The unavailable/loading/empty status text drawn inside a column.
class ColumnStatusUiaProvider final : public UiaProviderBase,
                                      public IRawElementProviderFragment {
public:
    ColumnStatusUiaProvider(ColumnViewUiaProvider* owner, int pane, int column);

    FFUIA_QI_BEGIN()
        FFUIA_QI_ENTRY(IID_IRawElementProviderFragment, IRawElementProviderFragment)
    FFUIA_QI_END()

    FFUIA_REFCOUNT()

    STDMETHODIMP GetPatternProvider(PATTERNID, IUnknown** retVal) noexcept override;
    STDMETHODIMP GetPropertyValue(PROPERTYID propertyId, VARIANT* retVal) noexcept override;
    STDMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** retVal) noexcept override;
    STDMETHODIMP GetRuntimeId(SAFEARRAY** retVal) noexcept override;
    STDMETHODIMP get_BoundingRectangle(UiaRect* retVal) noexcept override;
    STDMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** retVal) noexcept override;
    STDMETHODIMP SetFocus() noexcept override;
    STDMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** retVal) noexcept override;

    int Pane() const noexcept { return pane_; }

private:
    ColumnViewUiaProvider* owner_ = nullptr;
    int pane_ = 0;
    int column_ = 0;
};

// Home surface of a pane: group -> sections -> items / clear buttons.
class HomeGroupUiaProvider final : public UiaProviderBase,
                                   public IRawElementProviderFragment {
public:
    HomeGroupUiaProvider(ColumnViewUiaProvider* owner, int pane);

    FFUIA_QI_BEGIN()
        FFUIA_QI_ENTRY(IID_IRawElementProviderFragment, IRawElementProviderFragment)
    FFUIA_QI_END()

    FFUIA_REFCOUNT()

    STDMETHODIMP GetPatternProvider(PATTERNID, IUnknown** retVal) noexcept override;
    STDMETHODIMP GetPropertyValue(PROPERTYID propertyId, VARIANT* retVal) noexcept override;
    STDMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** retVal) noexcept override;
    STDMETHODIMP GetRuntimeId(SAFEARRAY** retVal) noexcept override;
    STDMETHODIMP get_BoundingRectangle(UiaRect* retVal) noexcept override;
    STDMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** retVal) noexcept override;
    STDMETHODIMP SetFocus() noexcept override;
    STDMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** retVal) noexcept override;

    int Pane() const noexcept { return pane_; }

private:
    ColumnViewUiaProvider* owner_ = nullptr;
    int pane_ = 0;
};

// One Home section group (heading + item grid); headings are not selectable.
class HomeSectionUiaProvider final : public UiaProviderBase,
                                     public IRawElementProviderFragment {
public:
    HomeSectionUiaProvider(ColumnViewUiaProvider* owner, int pane, int ordinal);

    FFUIA_QI_BEGIN()
        FFUIA_QI_ENTRY(IID_IRawElementProviderFragment, IRawElementProviderFragment)
    FFUIA_QI_END()

    FFUIA_REFCOUNT()

    STDMETHODIMP GetPatternProvider(PATTERNID, IUnknown** retVal) noexcept override;
    STDMETHODIMP GetPropertyValue(PROPERTYID propertyId, VARIANT* retVal) noexcept override;
    STDMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** retVal) noexcept override;
    STDMETHODIMP GetRuntimeId(SAFEARRAY** retVal) noexcept override;
    STDMETHODIMP get_BoundingRectangle(UiaRect* retVal) noexcept override;
    STDMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** retVal) noexcept override;
    STDMETHODIMP SetFocus() noexcept override;
    STDMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** retVal) noexcept override;

    int Pane() const noexcept { return pane_; }

private:
    ColumnViewUiaProvider* owner_ = nullptr;
    int pane_ = 0;
    int ordinal_ = 0;
};

// One Home item: folders/recents are ListItems; Quick actions are Buttons.
// Disabled items (unavailable known folders, empty-Pinned hint) expose no
// Invoke and report IsEnabled=false.
class HomeItemUiaProvider final : public UiaProviderBase,
                                  public IRawElementProviderFragment,
                                  public IInvokeProvider,
                                  public IScrollItemProvider {
public:
    HomeItemUiaProvider(ColumnViewUiaProvider* owner, int pane, int globalIndex,
                        int sectionKind, int indexInSection, uint64_t valueHash);

    FFUIA_QI_BEGIN()
        FFUIA_QI_ENTRY(IID_IRawElementProviderFragment, IRawElementProviderFragment)
        FFUIA_QI_ENTRY(IID_IInvokeProvider, IInvokeProvider)
        FFUIA_QI_ENTRY(IID_IScrollItemProvider, IScrollItemProvider)
    FFUIA_QI_END()

    FFUIA_REFCOUNT()

    STDMETHODIMP GetPatternProvider(PATTERNID patternId, IUnknown** retVal) noexcept override;
    STDMETHODIMP GetPropertyValue(PROPERTYID propertyId, VARIANT* retVal) noexcept override;
    STDMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** retVal) noexcept override;
    STDMETHODIMP GetRuntimeId(SAFEARRAY** retVal) noexcept override;
    STDMETHODIMP get_BoundingRectangle(UiaRect* retVal) noexcept override;
    STDMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** retVal) noexcept override;
    STDMETHODIMP SetFocus() noexcept override;
    STDMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** retVal) noexcept override;

    // IInvokeProvider (absent for disabled items via GetPatternProvider).
    STDMETHODIMP Invoke() noexcept override;

    // IScrollItemProvider
    STDMETHODIMP ScrollIntoView() noexcept override;

    int Pane() const noexcept { return pane_; }

private:
    // Verifies the item's identity (section + index + value hash) still
    // exists after a Home data refresh; false = stale element (never
    // retarget the new occupant of the same global index).
    bool Resolve(UiaHomeItemInfo& info) const;

    ColumnViewUiaProvider* owner_ = nullptr;
    int pane_ = 0;
    int globalIndex_ = 0;
    int sectionKind_ = -1;
    int indexInSection_ = -1;
    uint64_t valueHash_ = 0;
};

// The "Clear" affordance in a Recent folders / Recent searches header.
class HomeClearUiaProvider final : public UiaProviderBase,
                                   public IRawElementProviderFragment,
                                   public IInvokeProvider {
public:
    HomeClearUiaProvider(ColumnViewUiaProvider* owner, int pane, int clearKind);

    FFUIA_QI_BEGIN()
        FFUIA_QI_ENTRY(IID_IRawElementProviderFragment, IRawElementProviderFragment)
        FFUIA_QI_ENTRY(IID_IInvokeProvider, IInvokeProvider)
    FFUIA_QI_END()

    FFUIA_REFCOUNT()

    STDMETHODIMP GetPatternProvider(PATTERNID patternId, IUnknown** retVal) noexcept override;
    STDMETHODIMP GetPropertyValue(PROPERTYID propertyId, VARIANT* retVal) noexcept override;
    STDMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** retVal) noexcept override;
    STDMETHODIMP GetRuntimeId(SAFEARRAY** retVal) noexcept override;
    STDMETHODIMP get_BoundingRectangle(UiaRect* retVal) noexcept override;
    STDMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** retVal) noexcept override;
    STDMETHODIMP SetFocus() noexcept override;
    STDMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** retVal) noexcept override;

    // IInvokeProvider
    STDMETHODIMP Invoke() noexcept override;

    int Pane() const noexcept { return pane_; }

private:
    // Locate the hosting section's info (sectionKind == -1 when absent).
    UiaHomeSectionInfo HomeClearSection(UiaSurface* surface) const;

    ColumnViewUiaProvider* owner_ = nullptr;
    int pane_ = 0;
    int clearKind_ = 0;
};

// Fragment root + tree controller (Window element). Also resolves element
// identity for events, hit-testing, and focus.
class ColumnViewUiaProvider final
    : public UiaProviderBase,
      public IRawElementProviderFragment,
      public IRawElementProviderFragmentRoot {
public:
    ColumnViewUiaProvider(UiaOwnerStatePtr state, UiaSurface* surface, HWND hwnd);
    ~ColumnViewUiaProvider() override;

    FFUIA_QI_BEGIN()
        FFUIA_QI_ENTRY(IID_IRawElementProviderFragment, IRawElementProviderFragment)
        FFUIA_QI_ENTRY(IID_IRawElementProviderFragmentRoot, IRawElementProviderFragmentRoot)
    FFUIA_QI_END()

    FFUIA_REFCOUNT()

    // IRawElementProviderSimple
    STDMETHODIMP GetPatternProvider(PATTERNID patternId, IUnknown** retVal) noexcept override;
    STDMETHODIMP GetPropertyValue(PROPERTYID propertyId, VARIANT* retVal) noexcept override;

    // IRawElementProviderFragment
    STDMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** retVal) noexcept override;
    STDMETHODIMP GetRuntimeId(SAFEARRAY** retVal) noexcept override;
    STDMETHODIMP get_BoundingRectangle(UiaRect* retVal) noexcept override;
    STDMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** retVal) noexcept override;
    STDMETHODIMP SetFocus() noexcept override;
    STDMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** retVal) noexcept override;

    // IRawElementProviderFragmentRoot
    STDMETHODIMP ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** retVal) noexcept override;
    STDMETHODIMP GetFocus(IRawElementProviderFragment** retVal) noexcept override;

    // --- controller API ---------------------------------------------------
    UiaSurface* Surface() const noexcept { return surface_; }
    bool SurfaceAlive() const noexcept { return Alive(); }
    UiaViewGeometry Geometry() const { return surface_ != nullptr ? surface_->UiaGeometry() : UiaViewGeometry{}; }
    HWND Hwnd() const noexcept { return hwnd_; }

    // Event sink entry point (registered as the view's UiaEventRouter sink).
    static void SinkThunk(const UiaEventNotification& notification, void* context);

    // Element factories (cached; created on demand).
    Microsoft::WRL::ComPtr<ColumnViewGroupUiaProvider> GroupElement();
    Microsoft::WRL::ComPtr<PaneUiaProvider> PaneElement(int pane);
    Microsoft::WRL::ComPtr<ColumnUiaProvider> ColumnElement(int pane, int column);
    Microsoft::WRL::ComPtr<ColumnItemUiaProvider> ItemElement(int pane, int column, int row);
    Microsoft::WRL::ComPtr<ColumnStatusUiaProvider> StatusElement(int pane, int column);
    Microsoft::WRL::ComPtr<HomeGroupUiaProvider> HomeGroupElement(int pane);
    Microsoft::WRL::ComPtr<HomeSectionUiaProvider> HomeSectionElement(int pane, int ordinal);
    Microsoft::WRL::ComPtr<HomeItemUiaProvider> HomeItemElement(int pane, int globalIndex);
    Microsoft::WRL::ComPtr<HomeClearUiaProvider> HomeClearElement(int pane, int clearKind);

    // Focus resolution (GetFocus + focus events).
    Microsoft::WRL::ComPtr<UiaProviderBase> FocusElement();

    // Cache invalidation on semantic changes.
    void InvalidateColumn(int pane, uint64_t pathHash);
    void InvalidatePane(int pane);
    void InvalidateHome(int pane);
    void InvalidateAll();

    // Bounded item caches: on overflow the whole item cache clears (simple,
    // deterministic). See design D13.
    static constexpr size_t kMaxCachedItems = 4096;
    static constexpr size_t kMaxCachedColumns = 64;
    static constexpr size_t kMaxCachedHomeItems = 512;

private:
    friend class ColumnViewGroupUiaProvider;
    friend class PaneUiaProvider;
    friend class ColumnUiaProvider;
    friend class ColumnItemUiaProvider;
    friend class ColumnStatusUiaProvider;
    friend class HomeGroupUiaProvider;
    friend class HomeSectionUiaProvider;
    friend class HomeItemUiaProvider;
    friend class HomeClearUiaProvider;

    void HandleNotification(const UiaEventNotification& notification);
    void NotifyFocusChanged();
    // Hit-test helpers shared with ElementProviderFromPoint.
    int PaneAtPoint(double xDip, double yDip) const;
    bool ResolveColumnIdentity(int pane, int column, uint64_t& pathHash) const;

    // Caches (UI thread only).
    Microsoft::WRL::ComPtr<ColumnViewGroupUiaProvider> group_;
    std::array<Microsoft::WRL::ComPtr<PaneUiaProvider>, 2> panes_;
    std::unordered_map<uint64_t, Microsoft::WRL::ComPtr<ColumnUiaProvider>> columns_;
    std::unordered_map<uint64_t, Microsoft::WRL::ComPtr<ColumnItemUiaProvider>> items_;
    std::unordered_map<uint64_t, Microsoft::WRL::ComPtr<ColumnStatusUiaProvider>> statuses_;
    std::array<Microsoft::WRL::ComPtr<HomeGroupUiaProvider>, 2> homeGroups_;
    std::unordered_map<uint64_t, Microsoft::WRL::ComPtr<HomeSectionUiaProvider>> homeSections_;
    std::unordered_map<uint64_t, Microsoft::WRL::ComPtr<HomeItemUiaProvider>> homeItems_;
    std::unordered_map<uint64_t, Microsoft::WRL::ComPtr<HomeClearUiaProvider>> homeClears_;
    size_t itemCacheSize_ = 0;
    size_t homeItemCacheSize_ = 0;
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> hostProvider_; // UiaHostProviderFromHwnd
    UiaSurface* surface_ = nullptr;
    HWND hwnd_ = nullptr;
};


} // namespace ffui
