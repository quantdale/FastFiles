#include "UiStyle.h"

#include <cwchar>

namespace ffui {

using Microsoft::WRL::ComPtr;

HRESULT UiCreateTextFormat(
    IDWriteFactory* factory,
    float sizeDip,
    DWRITE_FONT_WEIGHT weight,
    const wchar_t* family,
    ComPtr<IDWriteTextFormat>* out) {
    if (factory == nullptr || out == nullptr) {
        return E_INVALIDARG;
    }
    out->Reset();

    const wchar_t* kPrimaryFamily = L"Segoe UI Variable Text";
    const wchar_t* kFallbackFamily = L"Segoe UI";
    const wchar_t* requestedFamily = (family != nullptr && family[0] != L'\0') ? family : kPrimaryFamily;

    ComPtr<IDWriteTextFormat> format;
    HRESULT hr = factory->CreateTextFormat(
        requestedFamily, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        sizeDip, L"", format.GetAddressOf());
    if (FAILED(hr) && std::wcscmp(requestedFamily, kFallbackFamily) != 0) {
        format.Reset();
        hr = factory->CreateTextFormat(
            kFallbackFamily, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            sizeDip, L"", format.GetAddressOf());
    }
    if (SUCCEEDED(hr)) {
        // Labels are single-line by default; consumers that draw wrapped text
        // can opt back into DWRITE_WORD_WRAPPING_WRAP on their format.
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        *out = std::move(format);
    }
    return hr;
}

}  // namespace ffui
