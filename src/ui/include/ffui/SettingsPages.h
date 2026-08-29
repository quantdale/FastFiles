#pragma once

#include <cstdint>

namespace ffui {

// Headless membership of Settings dialog controls on each tab. SwitchPage
// consumes this so a hide-all/show-some edit cannot drop Indexing rules or
// other page chrome. final-product-completion-and-production-readiness:
// the Indexing page previously hid `ruleList_` / `excludeRuleCheck_` and
// never showed them again.

enum class SettingsPage : int {
    General = 0,
    Search = 1,
    Navigation = 2,
    Indexing = 3,
    Storage = 4,
    Shortcuts = 5,
};

enum SettingsPageControl : std::uint32_t {
    kSettingsThemeLabel = 1u << 0,
    kSettingsThemeCombo = 1u << 1,
    kSettingsDensityLabel = 1u << 2,
    kSettingsDensityCombo = 1u << 3,
    kSettingsPreviewCheck = 1u << 4,
    kSettingsPreviewSizeLabel = 1u << 5,
    kSettingsPreviewSizeEdit = 1u << 6,
    kSettingsSearchScope = 1u << 7,
    kSettingsRetainHistory = 1u << 8,
    kSettingsStartupLabel = 1u << 9,
    kSettingsStartupPath = 1u << 10,
    kSettingsRestoreSession = 1u << 11,
    kSettingsVolumeList = 1u << 12,
    kSettingsRuleList = 1u << 13,
    kSettingsStatusDetail = 1u << 14,
    kSettingsAddRule = 1u << 15,
    kSettingsExcludeRule = 1u << 16,
    kSettingsRemoveRule = 1u << 17,
    kSettingsMoveUp = 1u << 18,
    kSettingsMoveDown = 1u << 19,
    kSettingsPauseAll = 1u << 20,
    kSettingsPauseResume = 1u << 21,
    kSettingsAddToIndexing = 1u << 22,
    kSettingsCategoryList = 1u << 23,
    kSettingsAddCategory = 1u << 24,
    kSettingsRemoveCategory = 1u << 25,
    kSettingsShortcutList = 1u << 26,
    kSettingsResetShortcuts = 1u << 27,
};

inline std::uint32_t SettingsPageVisibleControls(int pageIndex) {
    switch (pageIndex) {
        case 0:
            return kSettingsThemeLabel | kSettingsThemeCombo | kSettingsDensityLabel | kSettingsDensityCombo |
                   kSettingsPreviewCheck | kSettingsPreviewSizeLabel | kSettingsPreviewSizeEdit;
        case 1:
            return kSettingsSearchScope | kSettingsRetainHistory;
        case 2:
            return kSettingsStartupLabel | kSettingsStartupPath | kSettingsRestoreSession;
        case 3:
            return kSettingsVolumeList | kSettingsRuleList | kSettingsStatusDetail | kSettingsAddRule |
                   kSettingsExcludeRule | kSettingsRemoveRule | kSettingsMoveUp | kSettingsMoveDown |
                   kSettingsPauseAll | kSettingsPauseResume | kSettingsAddToIndexing;
        case 4:
            return kSettingsCategoryList | kSettingsAddCategory | kSettingsRemoveCategory;
        case 5:
            return kSettingsShortcutList | kSettingsResetShortcuts;
        default:
            return 0;
    }
}

inline bool SettingsControlOnPage(std::uint32_t control, int pageIndex) {
    return (SettingsPageVisibleControls(pageIndex) & control) != 0;
}

}  // namespace ffui
