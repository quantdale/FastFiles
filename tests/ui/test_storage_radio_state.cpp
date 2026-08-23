#include "../TestSupport.h"

#include "ffui/StorageRadioState.h"

int main() {
    int failures = 0;
    auto check = [&failures](bool condition, const char* description) {
        fftest::Check(condition, description);
        if (!condition) ++failures;
    };

    check(ffui::ResolveStorageRadioVisualState(false, false, false, false) ==
              ffui::StorageRadioVisualState::Rest,
          "rest radio state");
    check(ffui::ResolveStorageRadioVisualState(false, true, false, false) ==
              ffui::StorageRadioVisualState::Pressed,
          "pressed radio state");
    check(ffui::ResolveStorageRadioVisualState(true, false, false, false) ==
              ffui::StorageRadioVisualState::Checked,
          "checked radio state");
    check(ffui::ResolveStorageRadioVisualState(true, true, false, false) ==
              ffui::StorageRadioVisualState::CheckedPressed,
          "checked pressed radio state");
    check(ffui::ResolveStorageRadioVisualState(false, false, true, false) ==
              ffui::StorageRadioVisualState::Focused,
          "focused radio state");
    check(ffui::ResolveStorageRadioVisualState(true, false, true, false) ==
              ffui::StorageRadioVisualState::CheckedFocused,
          "checked focused radio state");
    check(ffui::ResolveStorageRadioVisualState(false, true, true, true) ==
              ffui::StorageRadioVisualState::Disabled,
          "disabled state takes precedence over interaction state");
    check(ffui::ResolveStorageRadioVisualState(true, true, true, true) ==
              ffui::StorageRadioVisualState::DisabledChecked,
          "disabled checked state takes precedence over interaction state");

    return failures == 0 ? 0 : 1;
}
