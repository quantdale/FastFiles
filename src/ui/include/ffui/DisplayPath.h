#pragma once

#include <algorithm>
#include <functional>
#include <string>
#include <vector>
#include <windows.h>

namespace ffui {

// Display-only path normalization. The navigation model deliberately retains
// the canonical path (including a long-path prefix) for Win32 operations; this
// seam removes that API detail and resolves a DOS 8.3 component only for text
// shown to a user.
inline std::wstring StripDisplayPathPrefix(std::wstring path) {
    if (path.rfind(L"\\\\?\\UNC\\", 0) == 0) {
        return L"\\\\" + path.substr(8);
    }
    if (path.rfind(L"\\\\?\\", 0) == 0) {
        return path.substr(4);
    }
    return path;
}

// Resolver-injected seam used by headless tests. A resolver returns the long
// form or an empty string when the source is unavailable/unresolvable.
template <typename Resolver>
inline std::wstring NormalizeDisplayPathWithResolver(const std::wstring& path, Resolver&& resolver) {
    if (path.empty()) return path;
    const std::wstring resolved = resolver(path);
    return StripDisplayPathPrefix(resolved.empty() ? path : resolved);
}

inline std::wstring NormalizeDisplayPath(const std::wstring& path) {
    if (path.empty()) return path;
    if (path.find(L'~') == std::wstring::npos && path.rfind(L"\\\\?\\", 0) != 0) {
        return path;
    }

    constexpr size_t kInitialBufferChars = 260;
    constexpr size_t kMaxDisplayPathChars = 32768;
    std::vector<wchar_t> buffer(kInitialBufferChars);
    for (;;) {
        const DWORD length = GetLongPathNameW(path.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) return StripDisplayPathPrefix(path);
        if (static_cast<size_t>(length) < buffer.size()) {
            return StripDisplayPathPrefix(std::wstring(buffer.data(), length));
        }
        const size_t required = static_cast<size_t>(length) + 1;
        if (required <= buffer.size() || required > kMaxDisplayPathChars) {
            return StripDisplayPathPrefix(path);
        }
        buffer.resize(required);
    }
}

} // namespace ffui
