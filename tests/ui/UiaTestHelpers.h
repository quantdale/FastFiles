#pragma once
// semantic-uia-core-surfaces 6.x: shared headless UIA test helpers. Property
// reads QI to IRawElementProviderSimple internally because the fragment and
// simple interfaces are siblings in the SDK (and concrete provider types
// have ambiguous IUnknown conversions).

#include <string>

#include <uiautomationcore.h>
#include <wrl/client.h>

namespace fftest {

inline Microsoft::WRL::ComPtr<IRawElementProviderSimple> ToSimple(IUnknown* element) {
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;
    if (element != nullptr && FAILED(element->QueryInterface(IID_IRawElementProviderSimple, &simple))) {
        simple.Reset();
    }
    return simple;
}

inline std::wstring GetName(IUnknown* element) {
    auto simple = ToSimple(element);
    if (!simple) return {};
    VARIANT value{};
    VariantInit(&value);
    simple->GetPropertyValue(UIA_NamePropertyId, &value);
    std::wstring name;
    if (value.vt == VT_BSTR && value.bstrVal != nullptr) {
        name.assign(value.bstrVal, SysStringLen(value.bstrVal));
    }
    VariantClear(&value);
    return name;
}

inline int GetControlType(IUnknown* element) {
    auto simple = ToSimple(element);
    if (!simple) return -1;
    VARIANT value{};
    VariantInit(&value);
    simple->GetPropertyValue(UIA_ControlTypePropertyId, &value);
    const int result = value.vt == VT_I4 ? value.lVal : -1;
    VariantClear(&value);
    return result;
}

inline bool GetBoolProperty(IUnknown* element, PROPERTYID propertyId) {
    auto simple = ToSimple(element);
    if (!simple) return false;
    VARIANT value{};
    VariantInit(&value);
    simple->GetPropertyValue(propertyId, &value);
    const bool result = value.vt == VT_BOOL && value.boolVal == VARIANT_TRUE;
    VariantClear(&value);
    return result;
}

inline std::wstring GetStringProperty(IUnknown* element, PROPERTYID propertyId) {
    auto simple = ToSimple(element);
    if (!simple) return {};
    VARIANT value{};
    VariantInit(&value);
    simple->GetPropertyValue(propertyId, &value);
    std::wstring result;
    if (value.vt == VT_BSTR && value.bstrVal != nullptr) {
        result.assign(value.bstrVal, SysStringLen(value.bstrVal));
    }
    VariantClear(&value);
    return result;
}

inline HRESULT GetPropertyHr(IUnknown* element, PROPERTYID propertyId, VARIANT* value) {
    auto simple = ToSimple(element);
    if (!simple) return E_FAIL;
    return simple->GetPropertyValue(propertyId, value);
}

} // namespace fftest
