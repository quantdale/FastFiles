#include "UiStyle.h"

// UI-v3 foundation headers are validated in this translation unit: the style
// TU is the natural compile-time home for the shared token/component seams
// until their consumer surfaces land.
#include "UiComponents.h"
#include "ffui/ResponsiveLayout.h"

#include <cwchar>
#include <string>

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

HRESULT UiCreateSingleLineTextLayout(
    IDWriteFactory* factory, IDWriteTextFormat* format,
    const wchar_t* text, UINT32 length, float widthDip, float heightDip,
    ComPtr<IDWriteTextLayout>* out) {
    if (out == nullptr) return E_INVALIDARG;
    out->Reset();
    if (factory == nullptr || format == nullptr || text == nullptr || widthDip <= 0.0f || heightDip <= 0.0f) return E_INVALIDARG;
    // NO_WRAP prevents soft wrapping, not explicit line separators in an OS
    // error message. Flatten only the display copy; model/path text is intact.
    const auto lineBreak = [](wchar_t value) {
        return value == L'\r' || value == L'\n' || value == L'\u2028' || value == L'\u2029';
    };
    std::wstring singleLine;
    if (std::any_of(text, text + length, lineBreak)) {
        singleLine.assign(text, length);
        for (auto& value : singleLine) if (lineBreak(value)) value = L' ';
        text = singleLine.c_str();
    }
    ComPtr<IDWriteTextLayout> layout;
    HRESULT hr = factory->CreateTextLayout(text, length, format, widthDip, heightDip, &layout);
    if (FAILED(hr)) return hr;
    hr = layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    if (FAILED(hr)) return hr;
    hr = layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    if (FAILED(hr)) return hr;
    ComPtr<IDWriteInlineObject> ellipsis;
    // Create the sign from the unmodified format and attach it to the layout;
    // the shared format remains suitable for multi-line recovery/details text.
    hr = factory->CreateEllipsisTrimmingSign(format, &ellipsis);
    if (FAILED(hr)) return hr;
    const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    hr = layout->SetTrimming(&trimming, ellipsis.Get());
    if (SUCCEEDED(hr)) *out = std::move(layout);
    return hr;
}

}  // namespace ffui
