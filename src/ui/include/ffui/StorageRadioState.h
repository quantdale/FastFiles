// Pure state seam for the owner-drawn Storage Analysis mode radios.
#pragma once

#include <cstddef>
#include <windows.h>

namespace ffui {

enum class StorageRadioVisualState {
    Rest,
    Pressed,
    Checked,
    CheckedPressed,
    Focused,
    CheckedFocused,
    Disabled,
    DisabledChecked,
};

constexpr size_t kStorageModeCount = 6;

// The native button controls retain their keyboard and UIA semantics. These
// style seams make the intended Tab topology executable without requiring a
// live window: Back/Up are direct Tab stops, and only the first radio enters
// the mutually-exclusive arrow-navigation group.
constexpr DWORD StoragePushButtonStyle() noexcept {
    return WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON;
}

constexpr DWORD StorageModeRadioStyle(size_t index) noexcept {
    return WS_CHILD | BS_AUTORADIOBUTTON |
           (index == 0 ? (WS_TABSTOP | WS_GROUP) : 0u);
}

constexpr bool StorageModeHasTabStop(size_t index) noexcept {
    return index == 0;
}

inline StorageRadioVisualState ResolveStorageRadioVisualState(bool checked, bool pressed,
                                                              bool focused, bool disabled) {
    if (disabled) {
        return checked ? StorageRadioVisualState::DisabledChecked : StorageRadioVisualState::Disabled;
    }
    if (checked) {
        if (pressed) return StorageRadioVisualState::CheckedPressed;
        if (focused) return StorageRadioVisualState::CheckedFocused;
        return StorageRadioVisualState::Checked;
    }
    if (pressed) return StorageRadioVisualState::Pressed;
    if (focused) return StorageRadioVisualState::Focused;
    return StorageRadioVisualState::Rest;
}

} // namespace ffui
