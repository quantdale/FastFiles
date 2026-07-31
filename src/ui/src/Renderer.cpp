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
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                    reinterpret_cast<IUnknown**>(dwriteFactory_.GetAddressOf())))) {
        return false;
    }

    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> dxgiFactory;
    if (FAILED(dxgiDevice_->GetAdapter(&adapter)) || FAILED(adapter->GetParent(IID_PPV_ARGS(&dxgiFactory)))) {
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

    if (FAILED(dxgiFactory->CreateSwapChainForComposition(dxgiDevice_.Get(), &swapChainDesc, nullptr, &swapChain_))) {
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
    swapChain_->ResizeBuffers(0, width_, height_, DXGI_FORMAT_UNKNOWN, 0);
    CreateTargetBitmap();
}

ID2D1DeviceContext* Renderer::BeginFrame() {
    d2dContext_->BeginDraw();
    return d2dContext_.Get();
}

void Renderer::EndFrame() {
    d2dContext_->EndDraw();
    swapChain_->Present(1, 0);
}

} // namespace ffui
