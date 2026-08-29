#pragma once

// HWND overlay provider for Search owner-draw push buttons.
//
// The panel's custom fragment root otherwise leaves these HWNDs on UIA's
// default MSAA proxy (ControlType=Pane, no Invoke, not keyboard-focusable).
// This overlay keeps UiaHostProviderFromHwnd as the host so tree shape,
// geometry, and runtime id stay native, and overrides Button semantics,
// Invoke, AutomationId, and (for Filters) ExpandCollapse.

#include "UiaProviderBase.h"

#include <functional>

#include "ffui/SearchFiltersDisclosure.h"

namespace ffui {

// Posted to the Filters button HWND (never SendMessage) so Expand/Collapse
// from a UIA worker thread cannot deadlock against the UI thread. ButtonHoverProc
// applies SetFiltersExpanded. WPARAM is 1 = expanded, 0 = collapsed.
constexpr UINT WM_APP_SEARCH_FILTERS_SET_EXPANDED = WM_APP + 54;

enum class SearchButtonKind {
    InvokeOnly,
    FiltersDisclosure,
};

class SearchButtonUiaProvider final : public UiaProviderBase,
                                      public IInvokeProvider,
                                      public IExpandCollapseProvider {
public:
    SearchButtonUiaProvider(UiaOwnerStatePtr state, HWND button, SearchButtonKind kind,
                            std::function<bool()> expandedReader, std::function<bool()> visibleReader = {});

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) noexcept override {
        if (ppv == nullptr) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IRawElementProviderSimple) {
            *ppv = static_cast<IRawElementProviderSimple*>(this);
        } else if (riid == IID_IInvokeProvider) {
            *ppv = static_cast<IInvokeProvider*>(this);
        } else if (riid == IID_IExpandCollapseProvider && Disclosure() && SurfaceVisible()) {
            *ppv = static_cast<IExpandCollapseProvider*>(this);
        } else {
            return E_NOINTERFACE;
        }
        static_cast<IRawElementProviderSimple*>(this)->AddRef();
        return S_OK;
    }
    FFUIA_REFCOUNT()

    STDMETHODIMP get_ProviderOptions(ProviderOptions* retVal) noexcept override;
    STDMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** retVal) noexcept override;
    STDMETHODIMP GetPropertyValue(PROPERTYID propertyId, VARIANT* retVal) noexcept override;
    STDMETHODIMP GetPatternProvider(PATTERNID patternId, IUnknown** retVal) noexcept override;

    STDMETHODIMP Invoke() noexcept override;

    STDMETHODIMP Expand() noexcept override;
    STDMETHODIMP Collapse() noexcept override;
    STDMETHODIMP get_ExpandCollapseState(ExpandCollapseState* retVal) noexcept override;

private:
    bool Disclosure() const noexcept { return kind_ == SearchButtonKind::FiltersDisclosure; }
    bool Expanded() const;
    bool SurfaceVisible() const;
    HRESULT PostExpanded(bool expanded) noexcept;

    HWND button_ = nullptr;
    SearchButtonKind kind_ = SearchButtonKind::InvokeOnly;
    std::function<bool()> expandedReader_;
    std::function<bool()> visibleReader_;
};

}  // namespace ffui
