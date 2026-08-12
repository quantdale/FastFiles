#pragma once

#include <string>
#include <string_view>
#include <windows.h>

namespace ffprotocol {

// Windows filesystem paths are case-insensitive even though the in-memory
// containers that carry them are not. Keep this comparison ordinal and
// locale-independent so a path's identity does not change with the user's
// locale settings.
inline bool WindowsPathEqual(std::wstring_view left, std::wstring_view right) noexcept {
    return CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
                                right.data(), static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

struct CaseInsensitivePathLess {
    bool operator()(const std::wstring& left, const std::wstring& right) const noexcept {
        return CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
                                    right.data(), static_cast<int>(right.size()), TRUE) == CSTR_LESS_THAN;
    }
};

} // namespace ffprotocol
