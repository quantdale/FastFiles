#include "ffsearch/Path.h"

#include <algorithm>
#include <cwctype>
#include <functional>

#include <windows.h>

namespace ffsearch {
namespace {

bool IsDrivePath(std::wstring_view path, size_t* rootLength, wchar_t* driveLetter) {
    if (path.size() >= 3 && std::iswalpha(path[0]) && path[1] == L':' && path[2] == L'\\') {
        if (rootLength != nullptr) *rootLength = 3;
        if (driveLetter != nullptr) *driveLetter = static_cast<wchar_t>(std::towupper(path[0]));
        return true;
    }
    if (path.size() >= 6 && path.starts_with(L"\\\\?\\") && std::iswalpha(path[4]) &&
        path[5] == L':' && (path.size() == 6 || path[6] == L'\\')) {
        if (rootLength != nullptr) *rootLength = path.size() >= 7 ? 7 : 6;
        if (driveLetter != nullptr) *driveLetter = static_cast<wchar_t>(std::towupper(path[4]));
        return true;
    }
    return false;
}

size_t ShareBoundary(std::wstring_view path, size_t prefixLength) {
    if (path.size() <= prefixLength) return std::wstring::npos;
    const size_t serverEnd = path.find(L'\\', prefixLength);
    if (serverEnd == std::wstring::npos || serverEnd + 1 >= path.size()) return std::wstring::npos;
    const size_t shareEnd = path.find(L'\\', serverEnd + 1);
    return shareEnd == std::wstring::npos ? path.size() : shareEnd;
}

bool IsExtendedUnc(std::wstring_view path) {
    return path.starts_with(L"\\\\?\\UNC\\");
}

bool IsUnc(std::wstring_view path) {
    return path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\' && !IsExtendedUnc(path);
}

std::wstring TrimTrailingSeparators(std::wstring_view path) {
    std::wstring value(path);
    while (value.size() > 1 && (value.back() == L'\\' || value.back() == L'/')) value.pop_back();
    return value;
}

bool IsRoot(std::wstring_view path) {
    size_t driveRootLength = 0;
    if (IsDrivePath(path, &driveRootLength, nullptr)) return path.size() == driveRootLength;
    if (IsExtendedUnc(path)) {
        const size_t boundary = ShareBoundary(path, 8);
        return boundary != std::wstring::npos && path.size() == boundary;
    }
    if (IsUnc(path)) {
        const size_t boundary = ShareBoundary(path, 2);
        return boundary != std::wstring::npos && path.size() == boundary;
    }
    return false;
}

std::wstring CanonicalVolumeRoot(std::wstring_view path) {
    const std::wstring value = TrimTrailingSeparators(path);
    wchar_t driveLetter = L'\0';
    if (IsDrivePath(value, nullptr, &driveLetter)) return std::wstring{driveLetter, L':'};
    if (IsExtendedUnc(value)) {
        const size_t boundary = ShareBoundary(value, 8);
        if (boundary == std::wstring::npos) return {};
        return L"\\\\" + value.substr(8, boundary - 8);
    }
    if (IsUnc(value)) {
        const size_t boundary = ShareBoundary(value, 2);
        if (boundary == std::wstring::npos) return {};
        return value.substr(0, boundary);
    }
    const size_t separator = value.find_first_of(L"\\/");
    return separator == std::wstring::npos ? value : value.substr(0, separator);
}

} // namespace

std::wstring ParentPath(std::wstring_view path) {
    if (path.empty()) return {};
    const std::wstring value = TrimTrailingSeparators(path);
    if (IsRoot(value)) return {};

    const size_t separator = value.find_last_of(L"\\/");
    if (separator == std::wstring::npos) return {};

    size_t driveRootLength = 0;
    if (IsDrivePath(value, &driveRootLength, nullptr) && separator + 1 == driveRootLength) {
        return value.substr(0, driveRootLength);
    }

    if (IsExtendedUnc(value)) {
        const size_t boundary = ShareBoundary(value, 8);
        if (boundary == std::wstring::npos || separator < boundary) return {};
    } else if (IsUnc(value)) {
        const size_t boundary = ShareBoundary(value, 2);
        if (boundary == std::wstring::npos || separator < boundary) return {};
    }
    return value.substr(0, separator);
}

std::wstring LeafName(std::wstring_view path) {
    if (path.empty()) return {};
    const std::wstring value = TrimTrailingSeparators(path);
    if (IsRoot(path) || IsRoot(value)) return std::wstring(path);
    const size_t separator = value.find_last_of(L"\\/");
    return separator == std::wstring::npos ? value : value.substr(separator + 1);
}

bool SameVolume(std::wstring_view left, std::wstring_view right) {
    const std::wstring leftRoot = CanonicalVolumeRoot(left);
    const std::wstring rightRoot = CanonicalVolumeRoot(right);
    if (leftRoot.empty() || rightRoot.empty()) return false;
    return CompareStringOrdinal(leftRoot.data(), static_cast<int>(leftRoot.size()),
                                rightRoot.data(), static_cast<int>(rightRoot.size()), TRUE) == CSTR_EQUAL;
}

int64_t VolumeId(std::wstring_view path) {
    std::wstring root = CanonicalVolumeRoot(path);
    if (root.size() == 2 && root[1] == L':') return static_cast<int64_t>(root[0]);
    std::transform(root.begin(), root.end(), root.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return static_cast<int64_t>(std::hash<std::wstring>{}(root));
}

} // namespace ffsearch
