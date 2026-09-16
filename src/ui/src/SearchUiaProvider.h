#pragma once
// semantic-uia-core-surfaces 4.x: UIA provider tree for the Search result
// list (design D4/D15).
//
// The native SysListView32 result list cannot expose LVS_OWNERDATA virtual
// rows through the UIA/MSAA proxy (the bridge does not enumerate virtual
// list children), so the list HWND is subclassed and answers WM_GETOBJECT
// (UiaRootObjectId) with this fragment root. Every other message falls
// through to DefSubclassProc: the control stays fully native for input,
// painting, and virtualization.
//
//   Search.Results (List, fragment root; Selection + ItemContainer)
//    └─ result ListItems (SelectionItem + Invoke + ScrollItem)
//
// Results are created on demand and cached with a hard cap; a query
// replacement drops the cache and raises structure events. Stale result
// elements self-verify their identity (volumeId+id / folder+name hash) and
// fail cleanly instead of retargeting the new occupant of the same index.

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include <windows.h>
#include <wrl/client.h>

#include "ffui/UiaSurface.h"
#include "UiaNotifications.h"
#include "UiaProviderBase.h" // also provides uiautomationcore.h + OLE prerequisites
#include "UiaRuntimeIds.h"

namespace ffui {

class SearchUiaProvider;
class SearchResultUiaProvider;

// Fragment root + controller for the search result list.
class SearchResultUiaProvider final : public UiaProviderBase,
                                      public IRawElementProviderFragment,
                                      public ISelectionItemProvider,
                                      public IInvokeProvider,
                                      public IScrollItemProvider {
public:
    SearchResultUiaProvider(SearchUiaProvider* owner, size_t index, uint64_t idHash);

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

    size_t Index() const noexcept { return index_; }

private:
    // Verifies the result at `index_` still exists with the same identity.
    bool Resolve(UiaSearchResultInfo& info) const;

    SearchUiaProvider* owner_ = nullptr;
    size_t index_ = 0;
    uint64_t idHash_ = 0;
};

class SearchUiaProvider final : public UiaProviderBase,
                                public IRawElementProviderFragment,
                                public IRawElementProviderFragmentRoot,
                                public ISelectionProvider,
                                public IItemContainerProvider {
public:
    SearchUiaProvider(UiaOwnerStatePtr state, UiaSurface* surface, HWND listHwnd);
    ~SearchUiaProvider() override;

    FFUIA_QI_BEGIN()
        FFUIA_QI_ENTRY(IID_IRawElementProviderFragment, IRawElementProviderFragment)
        FFUIA_QI_ENTRY(IID_IRawElementProviderFragmentRoot, IRawElementProviderFragmentRoot)
        FFUIA_QI_ENTRY(IID_ISelectionProvider, ISelectionProvider)
        FFUIA_QI_ENTRY(IID_IItemContainerProvider, IItemContainerProvider)
    FFUIA_QI_END()

    FFUIA_REFCOUNT()

    // IRawElementProviderSimple
    STDMETHODIMP GetPatternProvider(PATTERNID patternId, IUnknown** retVal) noexcept override;
    STDMETHODIMP GetPropertyValue(PROPERTYID propertyId, VARIANT* retVal) noexcept override;
    STDMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** retVal) noexcept override;

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

    // ISelectionProvider
    STDMETHODIMP GetSelection(SAFEARRAY** retVal) noexcept override;
    STDMETHODIMP get_CanSelectMultiple(BOOL* retVal) noexcept override;
    STDMETHODIMP get_IsSelectionRequired(BOOL* retVal) noexcept override;

    // IItemContainerProvider
    STDMETHODIMP FindItemByProperty(IRawElementProviderSimple* startAfter,
                                    PROPERTYID propertyId, VARIANT value,
                                    IRawElementProviderSimple** retVal) noexcept override;

    // Controller API.
    UiaSurface* Surface() const noexcept { return surface_; }
    bool SurfaceAlive() const noexcept { return Alive(); }
    Microsoft::WRL::ComPtr<SearchResultUiaProvider> ResultElement(size_t index);
    void InvalidateResults();

    static void SinkThunk(const UiaEventNotification& notification, void* context);

    static constexpr size_t kMaxCachedResults = 2048;

private:
    friend class SearchResultUiaProvider;
    void HandleNotification(const UiaEventNotification& notification);
    void NotifyFocusChanged();

    Microsoft::WRL::ComPtr<IRawElementProviderSimple> hostProvider_;
    UiaSurface* surface_ = nullptr;
    HWND listHwnd_ = nullptr;
    // UIAutomationCore may query result elements on a worker thread while
    // the UI thread replaces the result set. Protect the bounded cache from
    // concurrent find/erase/clear operations.
    mutable std::recursive_mutex cacheMutex_;
    std::unordered_map<uint64_t, Microsoft::WRL::ComPtr<SearchResultUiaProvider>> results_;
    size_t cacheSize_ = 0;
};

// One search result row.

// Row height from the native list's LVM_GETITEMSPACING metric: HIWORD is the
// vertical spacing (LOWORD is horizontal), with the established 28px
// fallback for degenerate values. Exposed pure for headless tests.
inline int SearchRowHeightFromSpacing(LONG spacing) {
    return spacing > 0 ? static_cast<int>(HIWORD(static_cast<DWORD>(spacing))) : 28;
}

} // namespace ffui
