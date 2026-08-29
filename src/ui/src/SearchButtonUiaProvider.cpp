#include "SearchButtonUiaProvider.h"
#include "UiaHostQuery.h"

#include <iterator>

namespace ffui {
namespace {

constexpr HRESULT kElementNotAvailable = UIA_E_ELEMENTNOTAVAILABLE;

}  // namespace

SearchButtonUiaProvider::SearchButtonUiaProvider(UiaOwnerStatePtr state, HWND button,
                                                 SearchButtonKind kind,
                                                 std::function<bool()> expandedReader,
                                                 std::function<bool()> visibleReader)
    : UiaProviderBase(std::move(state)), button_(button), kind_(kind),
      expandedReader_(std::move(expandedReader)), visibleReader_(std::move(visibleReader)) {}

STDMETHODIMP SearchButtonUiaProvider::get_ProviderOptions(ProviderOptions* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = ProviderOptions_ServerSideProvider;
    return S_OK;
}

STDMETHODIMP SearchButtonUiaProvider::get_HostRawElementProvider(
    IRawElementProviderSimple** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive()) return kElementNotAvailable;
    if (UiaHostQueryGuard::Active(button_)) return S_OK;
    UiaHostQueryGuard guard(button_);
    return UiaHostProviderFromHwnd(button_, retVal);
}

STDMETHODIMP SearchButtonUiaProvider::GetPropertyValue(PROPERTYID propertyId,
                                                       VARIANT* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    VariantInit(retVal);
    if (!Alive() || button_ == nullptr || !IsWindow(button_)) return kElementNotAvailable;
    switch (propertyId) {
        case UIA_ControlTypePropertyId:
            SetVariantInt(retVal, UIA_ButtonControlTypeId);
            return S_OK;
        case UIA_NamePropertyId: {
            wchar_t text[128]{};
            GetWindowTextW(button_, text, static_cast<int>(std::size(text)));
            SetVariantString(retVal, text);
            return S_OK;
        }
        case UIA_AutomationIdPropertyId: {
            const int controlId = GetDlgCtrlID(button_);
            if (const wchar_t* named = SearchButtonAutomationId(controlId)) {
                SetVariantString(retVal, named);
                return S_OK;
            }
            wchar_t id[16]{};
            swprintf_s(id, L"%d", controlId);
            SetVariantString(retVal, id);
            return S_OK;
        }
        case UIA_IsKeyboardFocusablePropertyId:
            SetVariantBool(retVal, true);
            return S_OK;
        case UIA_IsEnabledPropertyId:
            SetVariantBool(retVal, IsWindowEnabled(button_) != FALSE);
            return S_OK;
        case UIA_HasKeyboardFocusPropertyId:
            SetVariantBool(retVal, GetFocus() == button_);
            return S_OK;
        case UIA_IsOffscreenPropertyId:
            SetVariantBool(retVal, IsWindowVisible(button_) == FALSE);
            return S_OK;
        case UIA_ExpandCollapseExpandCollapseStatePropertyId:
            if (Disclosure() && SurfaceVisible()) {
                SetVariantInt(retVal, static_cast<int32_t>(Expanded() ? ExpandCollapseState_Expanded
                                                                      : ExpandCollapseState_Collapsed));
                return S_OK;
            }
            break;
        default:
            break;
    }
    SetVariantEmpty(retVal);
    return S_OK;
}

STDMETHODIMP SearchButtonUiaProvider::GetPatternProvider(PATTERNID patternId,
                                                         IUnknown** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive()) return kElementNotAvailable;
    if (patternId == UIA_InvokePatternId) {
        *retVal = static_cast<IInvokeProvider*>(this);
        static_cast<IRawElementProviderSimple*>(this)->AddRef();
        return S_OK;
    }
    if (Disclosure() && SurfaceVisible() && patternId == UIA_ExpandCollapsePatternId) {
        *retVal = static_cast<IExpandCollapseProvider*>(this);
        static_cast<IRawElementProviderSimple*>(this)->AddRef();
        return S_OK;
    }
    return UiaProviderBase::GetPatternProvider(patternId, retVal);
}

STDMETHODIMP SearchButtonUiaProvider::Invoke() noexcept {
    if (!Alive() || button_ == nullptr || !IsWindow(button_)) return kElementNotAvailable;
    PostMessageW(button_, BM_CLICK, 0, 0);
    return S_OK;
}

bool SearchButtonUiaProvider::Expanded() const {
    return expandedReader_ ? expandedReader_() : false;
}

bool SearchButtonUiaProvider::SurfaceVisible() const {
    return visibleReader_ ? visibleReader_() : true;
}

HRESULT SearchButtonUiaProvider::PostExpanded(bool expanded) noexcept {
    if (!Alive() || button_ == nullptr || !IsWindow(button_)) return kElementNotAvailable;
    PostMessageW(button_, WM_APP_SEARCH_FILTERS_SET_EXPANDED, expanded ? 1 : 0, 0);
    return S_OK;
}

STDMETHODIMP SearchButtonUiaProvider::Expand() noexcept {
    if (!Disclosure()) return UIA_E_INVALIDOPERATION;
    if (Expanded()) return S_OK;
    return PostExpanded(true);
}

STDMETHODIMP SearchButtonUiaProvider::Collapse() noexcept {
    if (!Disclosure()) return UIA_E_INVALIDOPERATION;
    if (!Expanded()) return S_OK;
    return PostExpanded(false);
}

STDMETHODIMP SearchButtonUiaProvider::get_ExpandCollapseState(ExpandCollapseState* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    if (!Alive()) return kElementNotAvailable;
    if (!Disclosure()) {
        *retVal = ExpandCollapseState_LeafNode;
        return UIA_E_INVALIDOPERATION;
    }
    *retVal = Expanded() ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed;
    return S_OK;
}

}  // namespace ffui
