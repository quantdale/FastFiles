// Pure state seam for the owner-drawn Storage Analysis mode radios.
#pragma once

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
