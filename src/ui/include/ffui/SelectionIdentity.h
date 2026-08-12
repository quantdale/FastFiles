#pragma once

#include <cstdint>
#include <cwctype>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <windows.h>

namespace ffui {

// A refresh-safe identity for one visible filesystem entry. Indexed entries
// use the durable (volume row, file reference number) pair; degraded entries
// have no index identity and therefore use their full path.
struct SelectionIdentity {
    std::wstring path;
    int64_t volumeRowId = 0;
    uint64_t fileIdLow = 0;
    uint64_t fileIdHigh = 0;
    bool hasStableFileId = false;
};

struct SelectionIndexState {
    std::set<int> selectedIndices;
    int selectionAnchor = -1;
    int focusIndex = -1;
};

struct SelectionIdentityState {
    std::vector<SelectionIdentity> selected;
    std::optional<SelectionIdentity> anchor;
    std::optional<SelectionIdentity> focus;
};

inline std::wstring NormalizeSelectionPath(std::wstring_view path) {
    std::wstring normalized(path);
    for (wchar_t& character : normalized) {
        if (character == L'/') character = L'\\';
    }

    const bool driveRoot = normalized.size() == 3 && std::iswalpha(normalized[0]) &&
                           normalized[1] == L':' && normalized[2] == L'\\';
    while (normalized.size() > 1 && normalized.back() == L'\\' && !driveRoot) {
        normalized.pop_back();
    }
    return normalized;
}

inline bool SameSelectionPath(std::wstring_view left, std::wstring_view right) {
    const std::wstring normalizedLeft = NormalizeSelectionPath(left);
    const std::wstring normalizedRight = NormalizeSelectionPath(right);
    return normalizedLeft.size() == normalizedRight.size() &&
           CompareStringOrdinal(normalizedLeft.data(), static_cast<int>(normalizedLeft.size()),
                                normalizedRight.data(), static_cast<int>(normalizedRight.size()), TRUE) == CSTR_EQUAL;
}

inline bool SameSelectionIdentity(const SelectionIdentity& left, const SelectionIdentity& right) {
    if (left.hasStableFileId && right.hasStableFileId) {
        return left.volumeRowId == right.volumeRowId &&
               left.fileIdLow == right.fileIdLow && left.fileIdHigh == right.fileIdHigh;
    }
    // Do not downgrade a genuinely indexed identity to a path match: a
    // deleted indexed entry and a newly-created entry at the same path are
    // not proven to be the same item. A degraded-to-indexed transition is
    // likewise not proven to be continuous, so it clears safely rather than
    // guessing from the path alone.
    if (left.hasStableFileId || right.hasStableFileId) return false;
    return SameSelectionPath(left.path, right.path);
}

inline SelectionIdentity MakeSelectionIdentity(std::wstring_view parentPath,
                                               std::wstring_view name,
                                               int64_t volumeRowId,
                                               uint64_t fileIdLow,
                                               uint64_t fileIdHigh) {
    std::wstring path(parentPath);
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/') path.push_back(L'\\');
    path += name;
    return {NormalizeSelectionPath(path), volumeRowId, fileIdLow, fileIdHigh,
            volumeRowId != 0 && (fileIdLow != 0 || fileIdHigh != 0)};
}

inline SelectionIdentityState CaptureSelectionIdentityState(std::span<const SelectionIdentity> items,
                                                            const SelectionIndexState& state) {
    SelectionIdentityState result;
    result.selected.reserve(state.selectedIndices.size());
    for (const int index : state.selectedIndices) {
        if (index >= 0 && index < static_cast<int>(items.size())) result.selected.push_back(items[static_cast<size_t>(index)]);
    }
    if (state.selectionAnchor >= 0 && state.selectionAnchor < static_cast<int>(items.size())) {
        result.anchor = items[static_cast<size_t>(state.selectionAnchor)];
    }
    if (state.focusIndex >= 0 && state.focusIndex < static_cast<int>(items.size())) {
        result.focus = items[static_cast<size_t>(state.focusIndex)];
    }
    return result;
}

inline std::optional<int> FindSelectionIdentity(std::span<const SelectionIdentity> items,
                                                const SelectionIdentity& target) {
    for (int index = 0; index < static_cast<int>(items.size()); ++index) {
        if (SameSelectionIdentity(items[static_cast<size_t>(index)], target)) return index;
    }
    return std::nullopt;
}

inline SelectionIndexState RestoreSelectionIndexState(std::span<const SelectionIdentity> items,
                                                       const SelectionIdentityState& state) {
    SelectionIndexState result;
    for (const auto& selected : state.selected) {
        const auto index = FindSelectionIdentity(items, selected);
        if (index) result.selectedIndices.insert(*index);
    }
    if (state.anchor) {
        const auto index = FindSelectionIdentity(items, *state.anchor);
        if (index) result.selectionAnchor = *index;
    }
    if (state.focus) {
        const auto index = FindSelectionIdentity(items, *state.focus);
        if (index) result.focusIndex = *index;
    }
    return result;
}

} // namespace ffui
