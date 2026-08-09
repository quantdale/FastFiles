#pragma once

#include <optional>
#include <string>
#include <vector>

namespace ffprotocol {

enum class ThemePreference { Light, Dark, FollowSystem };

struct DirectoryRule {
    std::wstring path;
    bool include = false;
};

struct VolumeSetting {
    std::wstring key;
    bool enabled = true;
    std::vector<DirectoryRule> rules;
};

struct Settings {
    unsigned schemaVersion = 1;
    std::vector<VolumeSetting> indexing;
    std::wstring defaultSearchScope = L"Everywhere";
    bool retainSearchHistory = true;
    ThemePreference theme = ThemePreference::FollowSystem;
    std::wstring startupLocation = L"This PC";
    bool restorePreviousSession = true;
    bool previewEnabled = true;
    unsigned long long maxAutoPreviewBytes = 16ULL * 1024ULL * 1024ULL;
    std::vector<std::pair<std::wstring, std::wstring>> storageCategories;
};

// The general settings file used to carry a separate, display-oriented
// shortcut list. It is retained only as a one-time migration input; runtime
// shortcut state belongs to the UI command system's dedicated configuration.
struct LegacyShortcutSettings {
    bool sectionPresent = false;
    bool valid = true;
    std::vector<std::pair<std::wstring, std::wstring>> bindings;
};

// All file mutation is performed by FastFiles. Engine calls Load only.
std::wstring SettingsPath();
std::wstring ShortcutSettingsPath();
Settings DefaultSettings();
Settings LoadSettings(bool preserveCorruptFile = true);
LegacyShortcutSettings LoadLegacyShortcutSettings();
bool SaveSettings(const Settings& settings);
bool ResetSettings();
bool IsPathIncluded(const VolumeSetting& volume, const std::wstring& canonicalPath) noexcept;

} // namespace ffprotocol
