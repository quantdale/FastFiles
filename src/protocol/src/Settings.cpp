#include "ffprotocol/Settings.h"
#include "ffprotocol/WindowsPath.h"
#include "SettingsJson.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <exception>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>

namespace ffprotocol {
namespace {

using Json = detail::SettingsJsonValue;
using Kind = Json::Kind;
constexpr size_t kMaxSettingsBytes = 1024 * 1024;

std::wstring FileApiPath(std::wstring path) {
    std::replace(path.begin(), path.end(), L'/', L'\\');
    if (path.starts_with(L"\\\\?\\") || path.starts_with(L"\\\\.\\")) return path;
    if (path.starts_with(L"\\\\")) return L"\\\\?\\UNC\\" + path.substr(2);
    if (path.size() >= 3 && path[1] == L':' && path[2] == L'\\') return L"\\\\?\\" + path;
    return path;
}

bool WriteSettingsBytesAtomically(const std::wstring& path, const std::string& bytes) {
    if (bytes.size() > kMaxSettingsBytes) return false;
    static std::atomic<unsigned long long> nextTemporary{0};
    HANDLE file = INVALID_HANDLE_VALUE;
    std::wstring temporary;
    for (unsigned attempt = 0; attempt < 16; ++attempt) {
        temporary = FileApiPath(path + L".write-" + std::to_wstring(GetCurrentProcessId()) + L"-"
            + std::to_wstring(GetTickCount64()) + L"-"
            + std::to_wstring(nextTemporary.fetch_add(1, std::memory_order_relaxed)) + L".tmp");
        // Exclusive creation, not truncation of a predictable .tmp path:
        // stale files, reparse points and hard links are never consumed.
        file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file != INVALID_HANDLE_VALUE) break;
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) return false;
    }
    if (file == INVALID_HANDLE_VALUE) return false;
    bool written = true;
    size_t offset = 0;
    while (offset < bytes.size()) {
        DWORD count = 0;
        if (!WriteFile(file, bytes.data() + offset, static_cast<DWORD>(bytes.size() - offset), &count, nullptr)
            || count == 0) {
            written = false;
            break;
        }
        offset += count;
    }
    written = written && FlushFileBuffers(file) != FALSE;
    CloseHandle(file);
    const std::wstring destination = FileApiPath(path);
    if (written && MoveFileExW(temporary.c_str(), destination.c_str(),
                              MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    DeleteFileW(temporary.c_str()); // only the file this call created
    return false;
}

std::wstring AppDirectory() {
    wchar_t value[MAX_PATH * 4]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", value, static_cast<DWORD>(std::size(value)));
    if (length == 0 || length >= std::size(value)) return L"";
    std::wstring directory(value, length);
    directory += L"\\FastFiles";
    CreateDirectoryW(FileApiPath(directory).c_str(), nullptr);
    return directory;
}

void LogFallback(const std::wstring& message) {
    const std::wstring appDirectory = AppDirectory();
    if (appDirectory.empty()) return;
    const std::wstring directory = appDirectory + L"\\logs";
    CreateDirectoryW(FileApiPath(directory).c_str(), nullptr);
    std::wofstream log(FileApiPath(directory + L"\\settings.log"), std::ios::app);
    if (log) log << message << L"\n"; // metadata only; never file contents
}

std::wstring Escape(const std::wstring& value) {
    std::wstring result;
    for (wchar_t ch : value) {
        if (ch == L'\\' || ch == L'\"') result.push_back(L'\\');
        if (ch == L'\n') result += L"\\n";
        else if (ch == L'\r') result += L"\\r";
        else if (ch == L'\t') result += L"\\t";
        else if (ch < 0x20) {
            wchar_t escaped[7]{};
            swprintf_s(escaped, L"\\u%04X", static_cast<unsigned int>(ch));
            result += escaped;
        } else result.push_back(ch);
    }
    return result;
}

std::optional<std::wstring> DecodeUtf8(const std::string& bytes) {
    if (bytes.empty()) return std::wstring{};
    if (bytes.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) return std::nullopt;
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (length <= 0) return std::nullopt;
    std::wstring text(static_cast<size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), text.data(), length) != length) return std::nullopt;
    return text;
}

std::optional<std::string> EncodeUtf8(const std::wstring& text) {
    if (text.empty()) return std::string{};
    if (text.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) return std::nullopt;
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return std::nullopt;
    std::string bytes(static_cast<size_t>(length), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), bytes.data(), length, nullptr, nullptr) != length) return std::nullopt;
    return bytes;
}

struct SettingsInput {
    std::string bytes;
    std::optional<Json> root;
    bool complete = false;
};

SettingsInput ReadSettingsInput(std::ifstream& file) {
    SettingsInput input;
    try {
        std::array<char, 4096> chunk{};
        while (file) {
            const size_t remaining = kMaxSettingsBytes - input.bytes.size();
            const size_t request = remaining < chunk.size() ? remaining + 1 : chunk.size();
            file.read(chunk.data(), static_cast<std::streamsize>(request));
            const size_t count = static_cast<size_t>(file.gcount());
            if (count > kMaxSettingsBytes - input.bytes.size()) return input;
            input.bytes.append(chunk.data(), count);
        }
        if (file.bad()) return input;
        input.complete = true;
        const auto decoded = DecodeUtf8(input.bytes);
        if (decoded) input.root = detail::SettingsJsonReader(*decoded).Read();
    } catch (const std::exception&) {
        input.root.reset();
    }
    return input;
}

void PreserveFallbackInput(const std::wstring& path, const SettingsInput& input) {
    // Preserve the exact bounded snapshot that was parsed, not a path that
    // another UI process may already have replaced. Never rewrite the source
    // during fallback, particularly when preservation fails. Oversized or
    // incompletely read inputs remain intact in place; do not copy an
    // unbounded file merely to report a bounded parse failure.
    if (!input.complete) {
        LogFallback(L"Settings fallback; source retained unchanged; incomplete or oversized input was not copied.");
        return;
    }
    const bool preserved = WriteSettingsBytesAtomically(path + L".bak", input.bytes);
    LogFallback(preserved ? L"Settings fallback; source retained unchanged and snapshot preserved as settings.json.bak."
                       : L"Settings fallback; backup failed; source retained unchanged.");
}

bool StringField(const Json& object, std::wstring_view name, std::wstring& value, bool required = false) {
    const Json* field = object.Member(name);
    if (!field) return !required;
    if (field->kind != Kind::String) return false;
    value = field->text;
    return true;
}

bool BoolField(const Json& object, std::wstring_view name, bool& value) {
    const Json* field = object.Member(name);
    if (!field) return true;
    if (field->kind != Kind::Boolean) return false;
    value = field->boolean;
    return true;
}

bool UnsignedField(const Json& object, std::wstring_view name, unsigned long long& value) {
    const Json* field = object.Member(name);
    if (!field) return true;
    if (field->kind != Kind::Number || field->text.empty()) return false;
    unsigned long long parsed = 0;
    for (wchar_t ch : field->text) {
        if (ch < L'0' || ch > L'9') return false;
        const auto digit = static_cast<unsigned long long>(ch - L'0');
        if (parsed > ((std::numeric_limits<unsigned long long>::max)() - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    }
    value = parsed;
    return true;
}

std::wstring ThemeName(ThemePreference theme) {
    return theme == ThemePreference::Light ? L"Light" : theme == ThemePreference::Dark ? L"Dark" : L"Follow-System";
}

std::wstring DensityName(DensityPreference density) {
    return density == DensityPreference::Comfortable ? L"Comfortable" : L"Compact";
}

bool ReadVolumes(const Json& section, std::vector<VolumeSetting>& result) {
    const Json* volumes = section.Member(L"volumes");
    if (!volumes) return true;
    if (volumes->kind != Kind::Array) return false;
    std::vector<VolumeSetting> parsed;
    for (const auto& value : volumes->elements) {
        VolumeSetting volume;
        if (!StringField(value, L"key", volume.key, true) || volume.key.empty()
            || !BoolField(value, L"enabled", volume.enabled)) return false;
        if (const Json* rules = value.Member(L"rules")) {
            if (rules->kind != Kind::Array) return false;
            for (const auto& ruleValue : rules->elements) {
                DirectoryRule rule;
                std::wstring action;
                if (!StringField(ruleValue, L"path", rule.path, true) || rule.path.empty()
                    || !StringField(ruleValue, L"action", action, true)
                    || (action != L"include" && action != L"exclude")) return false;
                rule.include = action == L"include";
                volume.rules.push_back(std::move(rule));
            }
        }
        parsed.push_back(std::move(volume));
    }
    result = std::move(parsed);
    return true;
}

bool ReadCategories(const Json& section, std::vector<std::pair<std::wstring, std::wstring>>& result) {
    const Json* categories = section.Member(L"categories");
    if (!categories) return true;
    if (categories->kind != Kind::Array) return false;
    std::vector<std::pair<std::wstring, std::wstring>> parsed;
    for (const auto& value : categories->elements) {
        std::wstring name, extensions;
        if (!StringField(value, L"name", name, true) || !StringField(value, L"extensions", extensions, true)) return false;
        parsed.emplace_back(std::move(name), std::move(extensions));
    }
    result = std::move(parsed);
    return true;
}

} // namespace

std::wstring SettingsPath() {
    const std::wstring directory = AppDirectory();
    return directory.empty() ? std::wstring{} : directory + L"\\settings.json";
}

std::wstring ShortcutSettingsPath() {
    const std::wstring directory = AppDirectory();
    return directory.empty() ? std::wstring{} : directory + L"\\shortcuts.json";
}

Settings DefaultSettings() {
    Settings settings;
    const DWORD drives = GetLogicalDrives();
    for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
        const DWORD bit = 1U << (letter - L'A');
        if ((drives & bit) == 0) continue;
        std::wstring root{letter, L':', L'\\'};
        if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED) continue;
        VolumeSetting volume;
        volume.key = root;
        volume.rules.push_back({root + L"$Recycle.Bin", false});
        volume.rules.push_back({root + L"System Volume Information", false});
        settings.indexing.push_back(std::move(volume));
    }
    settings.storageCategories = {{L"Documents", L".doc;.docx;.pdf;.txt"}, {L"Images", L".png;.jpg;.jpeg;.gif"}};
    return settings;
}

LegacyShortcutSettings LoadLegacyShortcutSettings() {
    LegacyShortcutSettings result;
    const std::wstring path = SettingsPath();
    if (path.empty()) return result;
    std::ifstream file(FileApiPath(path), std::ios::binary);
    if (!file) return result;
    const auto input = ReadSettingsInput(file);
    if (!input.root) { result.valid = false; return result; }
    const Json* section = input.root->Member(L"shortcuts");
    if (!section) return result;
    result.sectionPresent = true;
    const Json* bindings = section->Member(L"bindings");
    if (!bindings || bindings->kind != Kind::Array) { result.valid = false; return result; }
    for (const auto& value : bindings->elements) {
        std::wstring command, binding;
        if (!StringField(value, L"command", command, true) || !StringField(value, L"binding", binding, true)) {
            result.valid = false;
            continue;
        }
        result.bindings.emplace_back(std::move(command), std::move(binding));
    }
    return result;
}

Settings LoadSettings(bool preserveCorruptFile) {
    Settings result = DefaultSettings();
    const std::wstring path = SettingsPath();
    if (path.empty()) return result;
    std::ifstream file(FileApiPath(path), std::ios::binary);
    if (!file) {
        const DWORD attributes = GetFileAttributesW(FileApiPath(path).c_str());
        const DWORD error = GetLastError();
        if (preserveCorruptFile && attributes == INVALID_FILE_ATTRIBUTES
            && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)) SaveSettings(result);
        return result;
    }
    const auto input = ReadSettingsInput(file);
    file.close();
    if (!input.root) {
        if (preserveCorruptFile) PreserveFallbackInput(path, input);
        else LogFallback(L"Settings parse failed; defaults loaded; source retained unchanged.");
        return result;
    }
    bool invalidSection = false;
    const auto section = [&](const wchar_t* name, const auto& apply) {
        const Json* value = input.root->Member(name);
        if (!value) return;
        if (value->kind != Kind::Object || !apply(*value)) {
            invalidSection = true;
            LogFallback(std::wstring(L"Invalid ") + name + L" settings section; section defaults loaded.");
        }
    };
    unsigned long long version = result.schemaVersion;
    if (UnsignedField(*input.root, L"schemaVersion", version)
        && version <= (std::numeric_limits<unsigned>::max)()) {
        result.schemaVersion = static_cast<unsigned>(version);
    } else {
        invalidSection = true;
        LogFallback(L"Invalid schemaVersion; default version loaded.");
    }
    section(L"indexing", [&](const Json& value) { return ReadVolumes(value, result.indexing); });
    section(L"appearance", [&](const Json& value) {
        std::wstring theme = ThemeName(result.theme), density = DensityName(result.density);
        if (!StringField(value, L"theme", theme) || !StringField(value, L"density", density)
            || (theme != L"Light" && theme != L"Dark" && theme != L"Follow-System")
            || (density != L"Compact" && density != L"Comfortable")) return false;
        result.theme = theme == L"Light" ? ThemePreference::Light : theme == L"Dark" ? ThemePreference::Dark : ThemePreference::FollowSystem;
        result.density = density == L"Comfortable" ? DensityPreference::Comfortable : DensityPreference::Compact;
        return true;
    });
    section(L"search", [&](const Json& value) {
        std::wstring scope = result.defaultSearchScope;
        bool retain = result.retainSearchHistory;
        if (!StringField(value, L"defaultSearchScope", scope) || !BoolField(value, L"retainSearchHistory", retain)) return false;
        result.defaultSearchScope = std::move(scope);
        result.retainSearchHistory = retain;
        return true;
    });
    section(L"navigation", [&](const Json& value) {
        std::wstring location = result.startupLocation;
        bool restore = result.restorePreviousSession;
        if (!StringField(value, L"startupLocation", location) || !BoolField(value, L"restorePreviousSession", restore)) return false;
        result.startupLocation = std::move(location);
        result.restorePreviousSession = restore;
        return true;
    });
    section(L"preview", [&](const Json& value) {
        bool enabled = result.previewEnabled;
        auto maximum = result.maxAutoPreviewBytes;
        if (!BoolField(value, L"previewEnabled", enabled) || !UnsignedField(value, L"maxAutoPreviewBytes", maximum)) return false;
        result.previewEnabled = enabled;
        result.maxAutoPreviewBytes = maximum;
        return true;
    });
    section(L"maintenance", [&](const Json& value) {
        auto interval = result.vacuumIntervalHours, growth = result.vacuumSizeGrowthPercent;
        if (!UnsignedField(value, L"vacuumIntervalHours", interval) || !UnsignedField(value, L"vacuumSizeGrowthPercent", growth)) return false;
        result.vacuumIntervalHours = interval;
        result.vacuumSizeGrowthPercent = growth;
        return true;
    });
    section(L"storage-analysis", [&](const Json& value) { return ReadCategories(value, result.storageCategories); });
    if (invalidSection && preserveCorruptFile) PreserveFallbackInput(path, input);
    return result;
}

bool SaveSettings(const Settings& settings) {
    const std::wstring path = SettingsPath();
    if (path.empty()) return false;
    std::wostringstream file;
    file << L"{\n  \"schemaVersion\": " << settings.schemaVersion
         << L",\n  \"indexing\": {\"volumes\": [";
    for (size_t i = 0; i < settings.indexing.size(); ++i) {
        const auto& volume = settings.indexing[i];
        if (i) file << L",";
        file << L"{\"key\":\"" << Escape(volume.key) << L"\",\"enabled\":" << (volume.enabled ? L"true" : L"false") << L",\"rules\":[";
        for (size_t j = 0; j < volume.rules.size(); ++j) {
            if (j) file << L",";
            file << L"{\"path\":\"" << Escape(volume.rules[j].path) << L"\",\"action\":\"" << (volume.rules[j].include ? L"include" : L"exclude") << L"\"}";
        }
        file << L"]}";
    }
    file << L"]},\n  \"search\": {\"defaultSearchScope\":\"" << Escape(settings.defaultSearchScope) << L"\",\"retainSearchHistory\":" << (settings.retainSearchHistory ? L"true" : L"false")
         << L"},\n  \"appearance\": {\"theme\":\"" << ThemeName(settings.theme)
         << L"\",\"density\":\"" << DensityName(settings.density)
         << L"\"},\n  \"navigation\": {\"startupLocation\":\"" << Escape(settings.startupLocation) << L"\",\"restorePreviousSession\":" << (settings.restorePreviousSession ? L"true" : L"false")
         << L"},\n  \"preview\": {\"previewEnabled\":" << (settings.previewEnabled ? L"true" : L"false") << L",\"maxAutoPreviewBytes\":" << settings.maxAutoPreviewBytes
         << L"},\n  \"maintenance\": {\"vacuumIntervalHours\":" << settings.vacuumIntervalHours
         << L",\"vacuumSizeGrowthPercent\":" << settings.vacuumSizeGrowthPercent
         << L"},\n  \"storage-analysis\": {\"categories\":[";
    for (size_t i = 0; i < settings.storageCategories.size(); ++i) {
        if (i) file << L",";
        file << L"{\"name\":\"" << Escape(settings.storageCategories[i].first) << L"\",\"extensions\":\"" << Escape(settings.storageCategories[i].second) << L"\"}";
    }
    file << L"]}\n}\n";
    const auto bytes = EncodeUtf8(file.str());
    return bytes && WriteSettingsBytesAtomically(path, *bytes);
}

bool ResetSettings() { return SaveSettings(DefaultSettings()); }

bool IsPathIncluded(const VolumeSetting& volume, const std::wstring& canonicalPath) noexcept {
    if (!volume.enabled) return false;
    bool included = true;
    size_t longest = 0;
    for (const auto& rule : volume.rules) {
        std::wstring_view rulePath(rule.path);
        while (!rulePath.empty() && (rulePath.back() == L'\\' || rulePath.back() == L'/')) {
            rulePath.remove_suffix(1);
        }
        if (rulePath.empty() || rulePath.size() < longest || canonicalPath.size() < rulePath.size()) continue;
        const bool componentBoundary = canonicalPath.size() == rulePath.size()
            || canonicalPath[rulePath.size()] == L'\\' || canonicalPath[rulePath.size()] == L'/';
        if (componentBoundary && WindowsPathEqual(std::wstring_view(canonicalPath).substr(0, rulePath.size()), rulePath)) {
            longest = rulePath.size();
            included = rule.include;
        }
    }
    return included;
}

} // namespace ffprotocol
