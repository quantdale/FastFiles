#pragma once

#include <algorithm>
#include <functional>
#include <string>
#include <vector>
#include <windows.h>

namespace ffui {

inline bool IsLocalDrivePath(const std::wstring& path) noexcept {
    const auto isAsciiLetter = [](wchar_t value) noexcept {
        return (value >= L'A' && value <= L'Z') || (value >= L'a' && value <= L'z');
    };
    if (path.size() >= 3 && isAsciiLetter(path[0]) && path[1] == L':' &&
        (path[2] == L'\\' || path[2] == L'/')) {
        return true;
    }
    return path.size() >= 7 && path.rfind(L"\\\\?\\", 0) == 0 && isAsciiLetter(path[4]) &&
           path[5] == L':' && (path[6] == L'\\' || path[6] == L'/');
}

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
    if (!IsLocalDrivePath(path) || path.find(L'~') == std::wstring::npos) {
        return StripDisplayPathPrefix(path);
    }
    const std::wstring resolved = resolver(path);
    return StripDisplayPathPrefix(resolved.empty() ? path : resolved);
}

inline std::wstring NormalizeDisplayPath(const std::wstring& path) {
    if (path.empty()) return path;
    if (!IsLocalDrivePath(path) || path.find(L'~') == std::wstring::npos) {
        return StripDisplayPathPrefix(path);
    }

    // Calling GetLongPathNameW for UNC text from a paint or UIA path can
    // perform network I/O and make presentation depend on a remote server.
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

inline std::wstring RawDisplayPathLeaf(const std::wstring& path) {
    const std::wstring stripped = StripDisplayPathPrefix(path);
    const size_t end = stripped.find_last_not_of(L"\\/");
    if (end == std::wstring::npos) return stripped;
    const size_t separator = stripped.find_last_of(L"\\/", end);
    if (separator == std::wstring::npos) return stripped.substr(0, end + 1);
    if (separator == end) return stripped.substr(0, end + 1);
    return stripped.substr(separator + 1, end - separator);
}

inline std::wstring DisplayPathLeaf(const std::wstring& path) {
    return RawDisplayPathLeaf(NormalizeDisplayPath(path));
}

// Bookmark and sidebar labels historically stored either the full canonical
// path or the auto-derived canonical leaf. Normalize only those path-derived
// forms; an explicitly different user label remains untouched.
inline std::wstring NormalizePathDerivedDisplayLabel(
    const std::wstring& label, const std::wstring& canonicalPath) {
    if (label.empty() || label == canonicalPath) {
        return NormalizeDisplayPath(canonicalPath);
    }
    if (label == RawDisplayPathLeaf(canonicalPath)) {
        return DisplayPathLeaf(canonicalPath);
    }
    return label;
}

} // namespace ffui
