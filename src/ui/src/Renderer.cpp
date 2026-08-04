#include "Renderer.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dcomp.lib")

using Microsoft::WRL::ComPtr;

namespace ffui {

bool Renderer::Initialize(HWND hwnd) {
    hwnd_ = hwnd;

    RECT clientRect{};
    GetClientRect(hwnd, &clientRect);
    width_ = static_cast<UINT>(clientRect.right - clientRect.left);
    height_ = static_cast<UINT>(clientRect.bottom - clientRect.top);

    UINT deviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if (FAILED(D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, deviceFlags, nullptr, 0,
            D3D11_SDK_VERSION, &d3dDevice_, nullptr, nullptr))) {
        return false;
    }
    if (FAILED(d3dDevice_.As(&dxgiDevice_))) {
        return false;
    }

    D2D1_FACTORY_OPTIONS factoryOptions{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factoryOptions, d2dFactory_.GetAddressOf()))) {
        return false;
    }
    if (FAILED(d2dFactory_->CreateDevice(dxgiDevice_.Get(), &d2dDevice_))) {
        return false;
    }
    if (FAILED(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2dContext_))) {
        return false;
    }
    ApplyDpi();
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                    reinterpret_cast<IUnknown**>(dwriteFactory_.GetAddressOf())))) {
        return false;
    }

    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice_->GetAdapter(&adapter)) || FAILED(adapter->GetParent(IID_PPV_ARGS(&dxgiFactory_)))) {
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
    swapChainDesc.Width = width_;
    swapChainDesc.Height = height_;
    swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    if (FAILED(dxgiFactory_->CreateSwapChainForComposition(dxgiDevice_.Get(), &swapChainDesc, nullptr, &swapChain_))) {
        return false;
    }

    if (FAILED(DCompositionCreateDevice(dxgiDevice_.Get(), IID_PPV_ARGS(&dcompDevice_)))) {
        return false;
    }
    if (FAILED(dcompDevice_->CreateTargetForHwnd(hwnd_, TRUE, &dcompTarget_))) {
        return false;
    }
    if (FAILED(dcompDevice_->CreateVisual(&dcompVisual_))) {
        return false;
    }
    if (FAILED(dcompVisual_->SetContent(swapChain_.Get()))) {
        return false;
    }
    if (FAILED(dcompTarget_->SetRoot(dcompVisual_.Get()))) {
        return false;
    }
    dcompDevice_->Commit();

    return CreateTargetBitmap();
}

bool Renderer::CreateTargetBitmap() {
    ComPtr<IDXGISurface> surface;
    if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&surface)))) {
        return false;
    }

    D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    ComPtr<ID2D1Bitmap1> targetBitmap;
    if (FAILED(d2dContext_->CreateBitmapFromDxgiSurface(surface.Get(), &bitmapProperties, &targetBitmap))) {
        return false;
    }
    d2dContext_->SetTarget(targetBitmap.Get());
    return true;
}

void Renderer::Resize(UINT width, UINT height) {
    if (width == 0 || height == 0 || swapChain_ == nullptr) {
        return;
    }
    width_ = width;
    height_ = height;

    d2dContext_->SetTarget(nullptr);
    const HRESULT hrResize = swapChain_->ResizeBuffers(0, width_, height_, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hrResize)) {
        // The swap chain is broken (device removed/reset or render target
        // recreated); rebuild it from scratch in the new size rather than
        // presenting stale buffers.
        RecreateSwapChainAndTarget();
        return;
    }
    ApplyDpi();
    CreateTargetBitmap();
}

bool Renderer::RecreateSwapChainAndTarget() {
    // Detach the (now stale) target bitmap before replacing the swap chain.
    d2dContext_->SetTarget(nullptr);

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
    swapChainDesc.Width = width_;
    swapChainDesc.Height = height_;
    swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    ComPtr<IDXGISwapChain1> newSwapChain;
    if (FAILED(dxgiFactory_->CreateSwapChainForComposition(dxgiDevice_.Get(), &swapChainDesc, nullptr, &newSwapChain))) {
        return false;
    }
    swapChain_ = newSwapChain;

    // The composition visual must point at the replacement swap chain; the
    // commit is required for the new content to take effect.
    if (FAILED(dcompVisual_->SetContent(swapChain_.Get()))) {
        return false;
    }
    dcompDevice_->Commit();

    if (!CreateTargetBitmap()) {
        return false;
    }

    deviceRecreated_ = true;
    return true;
}

void Renderer::ApplyDpi() {
    // The D2D device context defaults to 96 DPI. With per-monitor DPI awareness
    // enabled, set it to the DPI of the window the renderer is bound to so all
    // DIP-based drawing (columns, rows, fonts, treemap) scales correctly on
    // high-DPI displays. Querying the window rather than the system handles
    // per-monitor differences correctly when the window is moved across
    // monitors of different scales. At 100% scaling (96 DPI) this is a no-op.
    if (d2dContext_) {
        UINT dpi = 96;
        if (hwnd_ != nullptr) {
            HMODULE user32 = GetModuleHandleW(L"user32.dll");
            if (user32) {
                using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
                auto getDpi = reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow"));
                if (getDpi) {
                    dpi = getDpi(hwnd_);
                }
            }
        }
        const float scale = static_cast<float>(dpi) / 96.0f;
        d2dContext_->SetDpi(96.0f * scale, 96.0f * scale);
    }
}

ID2D1DeviceContext* Renderer::BeginFrame() {
    d2dContext_->BeginDraw();
    return d2dContext_.Get();
}

void Renderer::EndFrame() {
    // Device-loss hardening: a failed EndDraw (e.g. D2DERR_RECREATE_TARGET
    // after a mode change) or a failed Present (target recreated, or the
    // device was removed/reset) invalidates the swap chain. Rebuild it and
    // the target bitmap so the next frame can render again. When EndDraw
    // failed, skip Present -- there is nothing ready to show, and Present on
    // a stale/broken chain would only produce a second spurious failure.
    const HRESULT hrEnd = d2dContext_->EndDraw();
    if (FAILED(hrEnd)) {
        RecreateSwapChainAndTarget();
        return;
    }
    if (FAILED(swapChain_->Present(1, 0))) {
        RecreateSwapChainAndTarget();
    }
}

bool Renderer::ConsumeDeviceRecreated() {
    const bool recreated = deviceRecreated_;
    deviceRecreated_ = false;
    return recreated;
}

} // namespace ffui
