// Bounded, DPI-aware, thread-deferred system-icon cache.
//
// Icons are keyed by file TYPE — the extension with a leading dot (L".txt",
// L".jpg") or the FolderKey() constant — never by full path, so the cache is
// bounded no matter how many files the UI enumerates. Icon resolution happens
// on a single dedicated worker thread via SHGetFileInfoW with
// SHGFI_USEFILEATTRIBUTES (no disk access); the resulting HICON is converted
// into an ID2D1Bitmap1 on the UI thread by the next Get() call, and the
// worker posts WM_APP_ICON_READY to the constructor-supplied HWND so the shell
// repaints the moment an icon arrives. The render thread never blocks on icon
// resolution.
//
// Icons are theme-independent: a theme change does not flush the cache. A DPI
// change does (bitmap pixel sizes are resolution-dependent); WM_DPICHANGED
// handlers should call Flush(), and Get() also self-flushes on a DPI change.
//
// Memory bound: at most kMaxCachedBitmaps (512) entries of ~16-DIP
// premultiplied BGRA bitmaps — hundreds of KB to a few MB — enforced by LRU
// eviction.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <d2d1_1.h>
#include <deque>
#include <list>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <wincodec.h>
#include <windows.h>
#include <wrl/client.h>

namespace ffui {

// Posted to the owner HWND (constructor argument) when the worker thread
// finishes resolving an icon. The receiver should repaint the surface showing
// that key; wParam/lParam carry no payload. The icon itself is picked up by
// the next Get() call; until then Get() returns false and the caller draws its
// themed glyph placeholder.
constexpr UINT WM_APP_ICON_READY = WM_APP + 27;

// Cache key for the generic folder icon.
inline const wchar_t* FolderKey() { return L"folder"; }

// Normalizes an extension into a cache key: IconKeyForExtension(L"txt") and
// IconKeyForExtension(L".txt") both yield L".txt"; an empty extension yields
// the empty key (generic file icon).
inline std::wstring IconKeyForExtension(const std::wstring& extension) {
    if (extension.empty()) {
        return std::wstring();
    }
    if (extension[0] == L'.') {
        return extension;
    }
    return L"." + extension;
}

class IconCache {
public:
    // owner receives WM_APP_ICON_READY on icon-resolution completion. The
    // cache is single-owner; destruction stops and joins the worker thread.
    explicit IconCache(HWND owner);
    ~IconCache();

    IconCache(const IconCache&) = delete;
    IconCache& operator=(const IconCache&) = delete;

    // Requests the icon for key, resolving it in the background if it is not
    // already cached, pending conversion, or queued. Safe to call from any
    // thread; returns immediately.
    void Prefetch(const std::wstring& key);

    // Returns the bitmap for key, converting a worker-resolved HICON into an
    // ID2D1Bitmap1 at the current DPI (UiMetrics::kIconSize DIPs) when one is
    // waiting. Returns true and writes the AddRef'd bitmap to *outBitmap when
    // available; false (with *outBitmap = nullptr) when the icon has not
    // arrived yet — callers fall back to their themed glyph and repaint on
    // WM_APP_ICON_READY. UI thread only.
    bool Get(ID2D1DeviceContext* context, const std::wstring& key, ID2D1Bitmap1** outBitmap);

    // Drops all cached bitmaps; icons re-resolve at the current DPI on demand.
    // HICONs for keys already resolved survive (they are DPI-independent).
    // Called on WM_DPICHANGED (Get also self-flushes on DPI change).
    void Flush();

    // The last-seen DPI scale (ffui::UiDpiScale) that cached bitmaps target.
    float DpiScale() const;

private:
    static constexpr std::size_t kMaxCachedBitmaps = 512;

    void WorkerMain();
    static HICON ResolveIcon(const std::wstring& key);
    HRESULT EnsureWicFactory();
    bool ConvertPendingIcon(ID2D1DeviceContext* context, const std::wstring& key, HICON icon);

    HWND owner_ = nullptr;
    std::thread worker_;
    mutable std::mutex mutex_;                            // guards everything below
    std::condition_variable workAvailable_;
    std::atomic<bool> stopping_{false};
    std::deque<std::wstring> queue_;                      // keys queued for the worker
    std::map<std::wstring, HICON> pendingIcons_;          // resolved, awaiting UI-thread conversion (owns HICONs)
    std::map<std::wstring, Microsoft::WRL::ComPtr<ID2D1Bitmap1>> bitmaps_;  // UI-thread bitmap cache
    std::list<std::wstring> lru_;                         // MRU ordering over bitmaps_
    Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory_;  // UI thread only
    float dpiScale_ = 1.0f;                               // last-seen DPI scale
};

}  // namespace ffui
