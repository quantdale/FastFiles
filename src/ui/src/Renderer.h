#pragma once
#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <wrl/client.h>

namespace ffui {

// Task 5.1: the Direct2D/DirectComposition window shell (design.md D1).
// Owns the D3D11/DXGI/D2D1/DirectComposition device chain and the
// swap-chain-backed render target bound to a single HWND, and exposes
// just BeginFrame/EndFrame plus the D2D1/DirectWrite factories drawing
// code needs -- device-independent resources (brushes, text formats) are
// created once against the D2D1 factory/device context and survive a
// resize; only the swap chain and its target bitmap are resize-dependent.
class Renderer {
public:
    bool Initialize(HWND hwnd);
    void Resize(UINT width, UINT height);
    void ApplyDpi();

    // Returns the device context to draw into, having already called
    // BeginDraw(). Caller must call EndFrame() when done.
    ID2D1DeviceContext* BeginFrame();
    void EndFrame();

    ID2D1Factory1* D2DFactory() const noexcept { return d2dFactory_.Get(); }
    IDWriteFactory* DWriteFactory() const noexcept { return dwriteFactory_.Get(); }

private:
    bool CreateTargetBitmap();

    Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice_;
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice_;
    Microsoft::WRL::ComPtr<ID2D1Factory1> d2dFactory_;
    Microsoft::WRL::ComPtr<ID2D1Device> d2dDevice_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory_;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
    Microsoft::WRL::ComPtr<IDCompositionDevice> dcompDevice_;
    Microsoft::WRL::ComPtr<IDCompositionTarget> dcompTarget_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> dcompVisual_;

    HWND hwnd_ = nullptr;
    UINT width_ = 0;
    UINT height_ = 0;
};

} // namespace ffui
