#include "UiaProviderBase.h"

namespace ffui {

UiaProviderBase::UiaProviderBase(UiaOwnerStatePtr state) : state_(std::move(state)) {}

UiaProviderBase::~UiaProviderBase() = default;

STDMETHODIMP UiaProviderBase::GetPatternProvider(PATTERNID /*patternId*/, IUnknown** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    return S_OK;
}

STDMETHODIMP UiaProviderBase::GetPropertyValue(PROPERTYID /*propertyId*/, VARIANT* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    VariantInit(retVal);
    return S_OK;
}

STDMETHODIMP UiaProviderBase::get_HostRawElementProvider(IRawElementProviderSimple** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    return S_OK;
}

STDMETHODIMP UiaProviderBase::get_ProviderOptions(ProviderOptions* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = ProviderOptions_ServerSideProvider;
    return S_OK;
}

void UiaProviderBase::SetVariantEmpty(VARIANT* value) {
    VariantClear(value);
    value->vt = VT_EMPTY;
}

void UiaProviderBase::SetVariantInt(VARIANT* value, int32_t integer) {
    VariantClear(value);
    value->vt = VT_I4;
    value->lVal = integer;
}

void UiaProviderBase::SetVariantBool(VARIANT* value, bool boolean) {
    VariantClear(value);
    value->vt = VT_BOOL;
    value->boolVal = boolean ? VARIANT_TRUE : VARIANT_FALSE;
}

void UiaProviderBase::SetVariantDouble(VARIANT* value, double number) {
    VariantClear(value);
    value->vt = VT_R8;
    value->dblVal = number;
}

void UiaProviderBase::SetVariantString(VARIANT* value, const std::wstring& text) {
    VariantClear(value);
    value->vt = VT_BSTR;
    value->bstrVal = SysAllocStringLen(text.c_str(), static_cast<UINT>(text.size()));
}

void UiaProviderBase::SetVariantRuntimeId(VARIANT* value, const std::vector<int>& runtimeId) {
    VariantClear(value);
    SAFEARRAYBOUND bound{static_cast<ULONG>(runtimeId.size()), 0};
    SAFEARRAY* array = SafeArrayCreate(VT_I4, 1, &bound);
    if (array == nullptr) {
        value->vt = VT_EMPTY;
        return;
    }
    for (LONG i = 0; i < static_cast<LONG>(runtimeId.size()); ++i) {
        (void)SafeArrayPutElement(array, &i, const_cast<int*>(&runtimeId[static_cast<size_t>(i)]));
    }
    value->vt = VT_I4 | VT_ARRAY;
    value->parray = array;
}

HRESULT UiaProviderBase::RaiseFocusChanged(IRawElementProviderSimple* element) {
    return UiaRaiseAutomationEvent(element, UIA_AutomationFocusChangedEventId);
}

HRESULT UiaProviderBase::RaiseSelectionInvalidated(IRawElementProviderSimple* element) {
    return UiaRaiseAutomationEvent(element, UIA_Selection_InvalidatedEventId);
}

HRESULT UiaProviderBase::RaiseElementAddedToSelection(IRawElementProviderSimple* element) {
    return UiaRaiseAutomationEvent(element, UIA_SelectionItem_ElementAddedToSelectionEventId);
}

HRESULT UiaProviderBase::RaiseElementRemovedFromSelection(IRawElementProviderSimple* element) {
    return UiaRaiseAutomationEvent(element, UIA_SelectionItem_ElementRemovedFromSelectionEventId);
}

HRESULT UiaProviderBase::RaiseInvoked(IRawElementProviderSimple* element) {
    return UiaRaiseAutomationEvent(element, UIA_Invoke_InvokedEventId);
}

HRESULT UiaProviderBase::RaiseStructureChanged(IRawElementProviderSimple* element,
                                               StructureChangeType changeType,
                                               const std::vector<int>* runtimeIds) {
    int* ids = nullptr;
    int count = 0;
    if (runtimeIds != nullptr && !runtimeIds->empty()) {
        ids = const_cast<int*>(runtimeIds->data());
        count = static_cast<int>(runtimeIds->size());
    }
    return UiaRaiseStructureChangedEvent(element, changeType, ids, count);
}

HRESULT UiaProviderBase::RaisePropertyChanged(IRawElementProviderSimple* element,
                                              PROPERTYID propertyId,
                                              const VARIANT& oldValue,
                                              const VARIANT& newValue) {
    return UiaRaiseAutomationPropertyChangedEvent(element, propertyId, oldValue, newValue);
}

} // namespace ffui
