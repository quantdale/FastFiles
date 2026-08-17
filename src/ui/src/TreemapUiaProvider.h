#pragma once
// semantic-treemap-uia 8/16/17/33: UIA provider tree for the Direct2D
// Storage Analysis Treemap, hosted under the EXISTING main-window fragment
// root (ColumnViewUiaProvider). No second fragment root, no per-tile HWNDs,
// no hidden accessibility-only windows.
//
//   Window "FastFiles" (fragment root + controller)
//    └─ Group "Storage Treemap"   (ControlType=Group, AutomationId=Storage.Treemap,
//                                  ItemContainer: find a tile by Name/AutomationId)
//        ├─ DataItem <tile>       (Invoke for real navigable tiles)
//        ├─ DataItem <tile>
//        └─ ...
//
// Tiles are DataItems: they represent one storage object / aggregate with
// rich metadata (FullDescription, HelpText=full path, ItemStatus). There is
// NO Selection/SelectionItem -- the treemap has no selection model -- and
// IsKeyboardFocusable is honestly false (no hidden focus model; clients
// invoke through InvokePattern).
//
// Provider reads consume the bounded, thread-safe TreemapUiaSurface
// projection (plain values, never TreemapNode/TreemapRect). Invoke marshals
// the real navigation mutation to the owning UI thread (semantic-treemap-uia
// 11.x). Stale elements fail with UIA_E_ELEMENTNOTAVAILABLE and can never
// retarget a new tile occupying the same index.
//
// Virtualization: tile providers are created on demand and cached with a
// hard cap; overflow clears the cache deterministically. No provider work
// happens during paint and none is allocated when no UIA client exists.

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <windows.h>
#include <wrl/client.h>

#include "ffui/TreemapSemantics.h"
#include "UiaGeometry.h"
#include "UiaProviderBase.h" // also provides uiautomationcore.h + OLE prerequisites
#include "UiaRuntimeIds.h"

namespace ffui {

class ColumnViewUiaProvider;
class TreemapGroupUiaProvider;
class TreemapTileUiaProvider;

// Group "Storage Treemap": the treemap surface as one semantic group hosting
// the visible tiles. Implements ItemContainerProvider so a client can locate
// a named tile without walking thousands of children.
class TreemapGroupUiaProvider final : public UiaProviderBase,
                                      public IRawElementProviderFragment,
                                      public IItemContainerProvider {
public:
    explicit TreemapGroupUiaProvider(ColumnViewUiaProvider* owner);

    FFUIA_QI_BEGIN()
        FFUIA_QI_ENTRY(IID_IRawElementProviderFragment, IRawElementProviderFragment)
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

    // IItemContainerProvider
    STDMETHODIMP FindItemByProperty(IRawElementProviderSimple* startAfter,
                                    PROPERTYID propertyId, VARIANT value,
                                    IRawElementProviderSimple** retVal) noexcept override;

private:
    ColumnViewUiaProvider* owner_ = nullptr;
};

// One treemap tile: DataItem with the full canonical metadata. Real
// navigable tiles expose IInvokeProvider; the synthetic "Other small items"
// aggregate exposes no Invoke (it is not one filesystem object and must never
// navigate to an arbitrary child).
class TreemapTileUiaProvider final : public UiaProviderBase,
                                     public IRawElementProviderFragment,
                                     public IInvokeProvider {
public:
    TreemapTileUiaProvider(ColumnViewUiaProvider* owner, uint64_t scopeHash, uint64_t idHash);

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

    uint64_t ScopeHash() const noexcept { return scopeHash_; }
    uint64_t IdHash() const noexcept { return idHash_; }

private:
    // Resolves the tile's CURRENT semantic state by its stable identity.
    // False = stale element (scope changed / snapshot rebuilt / filter
    // changed / treemap hidden / window destroyed): fail cleanly, never
    // retarget the new occupant of the same index.
    bool Resolve(TreemapUiaItemInfo& info) const;

    ColumnViewUiaProvider* owner_ = nullptr;
    uint64_t scopeHash_ = 0;
    uint64_t idHash_ = 0;
};

} // namespace ffui
