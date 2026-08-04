#include "IconCache.h"

#include "UITheme.h"

#include <shellapi.h>

namespace ffui {

using Microsoft::WRL::ComPtr;

IconCache::IconCache(HWND owner) : owner_(owner) {
    worker_ = std::thread(&IconCache::WorkerMain, this);
}

IconCache::~IconCache() {
    stopping_.store(true);
    workAvailable_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
    // Release any HICONs the worker resolved but the UI thread never converted.
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : pendingIcons_) {
        if (entry.second != nullptr) {
            DestroyIcon(entry.second);
            entry.second = nullptr;
        }
    }
    pendingIcons_.clear();
}

void IconCache::Prefetch(const std::wstring& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (bitmaps_.find(key) != bitmaps_.end() || pendingIcons_.find(key) != pendingIcons_.end()) {
        return;
    }
    for (const std::wstring& queued : queue_) {
        if (queued == key) {
            return;
        }
    }
    if (stopping_.load()) {
        return;
    }
    queue_.push_back(key);
    workAvailable_.notify_one();
}

void IconCache::WorkerMain() {
    for (;;) {
        std::wstring key;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            workAvailable_.wait(lock, [this] { return stopping_.load() || !queue_.empty(); });
            if (stopping_.load() && queue_.empty()) {
                return;
            }
            key = std::move(queue_.front());
            queue_.pop_front();
        }

        HICON icon = ResolveIcon(key);
        if (icon == nullptr) {
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // Keep the first unresolved icon for a key; drop later duplicates
            // so GDI handles stay bounded.
            if (pendingIcons_.find(key) != pendingIcons_.end()) {
                DestroyIcon(icon);
                continue;
            }
            pendingIcons_.emplace(key, icon);
        }
        if (owner_ != nullptr) {
            PostMessageW(owner_, WM_APP_ICON_READY, 0, 0);
        }
    }
}

HICON IconCache::ResolveIcon(const std::wstring& key) {
    SHFILEINFOW info{};
    info.hIcon = nullptr;

    if (key == FolderKey()) {
        HICON icon = nullptr;
        // Pseudo-path + FILE_ATTRIBUTE_DIRECTORY + SHGFI_USEFILEATTRIBUTES:
        // the shell returns the generic folder icon without touching the disk.
        if (SHGetFileInfoW(L"folder", FILE_ATTRIBUTE_DIRECTORY, &info, static_cast<UINT>(sizeof(info)),
                           SHGFI_ICON | SHGFI_USEFILEATTRIBUTES) != 0 &&
            info.hIcon != nullptr) {
            icon = info.hIcon;
        }
        if (icon == nullptr) {
            // Fallback: the stock folder icon, also disk-free.
            SHSTOCKICONINFO stock{};
            stock.cbSize = static_cast<DWORD>(sizeof(stock));
            if (SUCCEEDED(SHGetStockIconInfo(SIID_FOLDER, SHGSI_ICON, &stock)) && stock.hIcon != nullptr) {
                icon = stock.hIcon;
            }
        }
        return icon;
    }

    // File type key: a pseudo filename carries the extension so
    // SHGFI_USEFILEATTRIBUTES resolves the right type icon (still no I/O).
    std::wstring pseudoPath = L"file";
    if (!key.empty()) {
        if (key[0] == L'.') {
            pseudoPath += key;
        } else {
            pseudoPath += L".";
            pseudoPath += key;
        }
    }
    if (SHGetFileInfoW(pseudoPath.c_str(), FILE_ATTRIBUTE_NORMAL, &info, static_cast<UINT>(sizeof(info)),
                       SHGFI_ICON | SHGFI_USEFILEATTRIBUTES) != 0 &&
        info.hIcon != nullptr) {
        return info.hIcon;
    }
    return nullptr;
}

bool IconCache::Get(ID2D1DeviceContext* context, const std::wstring& key, ID2D1Bitmap1** outBitmap) {
    if (outBitmap == nullptr) {
        return false;
    }
    *outBitmap = nullptr;
    if (context == nullptr) {
        return false;
    }

    // DPI change: cached pixel sizes no longer match the target; drop the
    // bitmap layer so the icons re-resolve at the current scale on demand.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const float scale = UiDpiScale();
        if (dpiScale_ != scale) {
            bitmaps_.clear();
            lru_.clear();
            dpiScale_ = scale;
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto found = bitmaps_.find(key);
        if (found != bitmaps_.end()) {
            lru_.remove(key);
            lru_.push_front(key);
            found->second.CopyTo(outBitmap);
            return true;
        }
    }

    // Not cached yet: convert a worker-resolved icon if one is waiting.
    HICON icon = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto pending = pendingIcons_.find(key);
        if (pending == pendingIcons_.end()) {
            return false;
        }
        icon = pending->second;
        pendingIcons_.erase(pending);
    }

    if (!ConvertPendingIcon(context, key, icon)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto converted = bitmaps_.find(key);
    if (converted != bitmaps_.end()) {
        converted->second.CopyTo(outBitmap);
        return true;
    }
    return false;
}

void IconCache::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    bitmaps_.clear();
    lru_.clear();
    dpiScale_ = UiDpiScale();
}

float IconCache::DpiScale() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dpiScale_;
}

HRESULT IconCache::EnsureWicFactory() {
    if (wicFactory_ != nullptr) {
        return S_OK;
    }
    return CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory_));
}

bool IconCache::ConvertPendingIcon(ID2D1DeviceContext* context, const std::wstring& key, HICON icon) {
    UINT targetPx = static_cast<UINT>(UiMetrics::kIconSize * UiDpiScale() + 0.5f);
    if (targetPx == 0) {
        targetPx = 1;
    }
    if (FAILED(EnsureWicFactory()) || wicFactory_ == nullptr) {
        DestroyIcon(icon);
        return false;
    }

    ComPtr<IWICBitmap> sourceBitmap;
    HRESULT hr = wicFactory_->CreateBitmapFromHICON(icon, sourceBitmap.GetAddressOf());
    if (FAILED(hr)) {
        DestroyIcon(icon);
        return false;
    }

    ComPtr<IWICBitmapSource> source = sourceBitmap;
    UINT sourceWidth = 0;
    UINT sourceHeight = 0;
    sourceBitmap->GetSize(&sourceWidth, &sourceHeight);
    if (sourceWidth != targetPx || sourceHeight != targetPx) {
        ComPtr<IWICBitmapScaler> scaler;
        hr = wicFactory_->CreateBitmapScaler(scaler.GetAddressOf());
        if (SUCCEEDED(hr)) {
            hr = scaler->Initialize(sourceBitmap.Get(), targetPx, targetPx, WICBitmapInterpolationModeHighQualityCubic);
        }
        if (SUCCEEDED(hr)) {
            source = scaler;
        }
        // On scaler failure the unscaled source bitmap is used as-is.
    }

    D2D1_BITMAP_PROPERTIES1 bitmapProperties = {};
    bitmapProperties.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    bitmapProperties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bitmapProperties.dpiX = 96.0f;
    bitmapProperties.dpiY = 96.0f;

    ComPtr<ID2D1Bitmap1> bitmap;
    hr = context->CreateBitmapFromWicBitmap(source.Get(), &bitmapProperties, bitmap.GetAddressOf());
    if (FAILED(hr)) {
        DestroyIcon(icon);
        return false;
    }
    // The D2D bitmap is a copy; the HICON is now ours to destroy.
    DestroyIcon(icon);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        lru_.remove(key);
        lru_.push_front(key);
        bitmaps_[key] = bitmap;
        while (lru_.size() > kMaxCachedBitmaps) {
            const std::wstring& victim = lru_.back();
            bitmaps_.erase(victim);
            lru_.pop_back();
        }
    }
    return true;
}

}  // namespace ffui
