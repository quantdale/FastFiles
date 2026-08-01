#include "ffprotocol/Settings.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>

namespace ffprotocol {
namespace {

std::wstring AppDirectory() {
    wchar_t value[MAX_PATH * 4]{};
    DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", value, static_cast<DWORD>(std::size(value)));
    if (length == 0 || length >= std::size(value)) return L"";
    std::wstring directory(value, length);
    directory += L"\\FastFiles";
    CreateDirectoryW(directory.c_str(), nullptr);
    return directory;
}

void LogFallback(const std::wstring& message) {
    const std::wstring appDirectory = AppDirectory();
    if (appDirectory.empty()) return;
    const std::wstring directory = appDirectory + L"\\logs";
    CreateDirectoryW(directory.c_str(), nullptr);
    std::wofstream log(directory + L"\\settings.log", std::ios::app);
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
        }
        else result.push_back(ch);
    }
    return result;
}

std::optional<std::wstring> Unescape(const std::wstring& value) {
    std::wstring result;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] != L'\\') { result.push_back(value[i]); continue; }
        if (++i == value.size()) return std::nullopt;
        const wchar_t escaped = value[i];
        if (escaped == L'\"' || escaped == L'\\' || escaped == L'/') result.push_back(escaped);
        else if (escaped == L'n') result.push_back(L'\n');
        else if (escaped == L'r') result.push_back(L'\r');
        else if (escaped == L't') result.push_back(L'\t');
        else if (escaped == L'b') result.push_back(L'\b');
        else if (escaped == L'f') result.push_back(L'\f');
        else if (escaped == L'u') {
            if (i + 4 >= value.size()) return std::nullopt;
            unsigned int codeUnit = 0;
            for (size_t digitIndex = 0; digitIndex < 4; ++digitIndex) {
                const wchar_t digit = value[++i];
                codeUnit <<= 4;
                if (digit >= L'0' && digit <= L'9') codeUnit += digit - L'0';
                else if (digit >= L'a' && digit <= L'f') codeUnit += digit - L'a' + 10;
                else if (digit >= L'A' && digit <= L'F') codeUnit += digit - L'A' + 10;
                else return std::nullopt;
            }
            result.push_back(static_cast<wchar_t>(codeUnit));
        }
        else return std::nullopt;
    }
    return result;
}

class JsonValidator {
public:
    explicit JsonValidator(const std::wstring& text) : text_(text) {}
    bool Validate() { Skip(); return Value() && (Skip(), position_ == text_.size()); }
private:
    void Skip() { while (position_ < text_.size() && iswspace(text_[position_])) ++position_; }
    bool Take(wchar_t ch) { Skip(); if (position_ >= text_.size() || text_[position_] != ch) return false; ++position_; return true; }
    bool Literal(const wchar_t* value) {
        Skip(); const size_t length = wcslen(value);
        if (text_.compare(position_, length, value) != 0) return false;
        position_ += length; return true;
    }
    bool String() {
        if (!Take(L'\"')) return false;
        while (position_ < text_.size()) {
            const wchar_t ch = text_[position_++];
            if (ch == L'\"') return true;
            if (ch < 0x20) return false;
            if (ch == L'\\') {
                if (position_ >= text_.size()) return false;
                const wchar_t escaped = text_[position_++];
                if (escaped == L'u') {
                    for (int index = 0; index < 4; ++index) {
                        if (position_ >= text_.size() || !iswxdigit(text_[position_++])) return false;
                    }
                } else if (wcschr(L"\"\\/bfnrt", escaped) == nullptr) return false;
            }
        }
        return false;
    }
    bool Number() {
        Skip(); const size_t start = position_;
        if (position_ < text_.size() && text_[position_] == L'-') ++position_;
        if (position_ >= text_.size() || !iswdigit(text_[position_])) return false;
        if (text_[position_] == L'0') ++position_;
        else while (position_ < text_.size() && iswdigit(text_[position_])) ++position_;
        if (position_ < text_.size() && text_[position_] == L'.') {
            ++position_; if (position_ >= text_.size() || !iswdigit(text_[position_])) return false;
            while (position_ < text_.size() && iswdigit(text_[position_])) ++position_;
        }
        if (position_ < text_.size() && (text_[position_] == L'e' || text_[position_] == L'E')) {
            ++position_; if (position_ < text_.size() && (text_[position_] == L'+' || text_[position_] == L'-')) ++position_;
            if (position_ >= text_.size() || !iswdigit(text_[position_])) return false;
            while (position_ < text_.size() && iswdigit(text_[position_])) ++position_;
        }
        return position_ > start;
    }
    bool Array() {
        if (!Take(L'[')) return false; Skip(); if (Take(L']')) return true;
        do { if (!Value()) return false; Skip(); if (Take(L']')) return true; } while (Take(L','));
        return false;
    }
    bool Object() {
        if (!Take(L'{')) return false; Skip(); if (Take(L'}')) return true;
        do { if (!String() || !Take(L':') || !Value()) return false; Skip(); if (Take(L'}')) return true; } while (Take(L','));
        return false;
    }
    bool Value() {
        Skip(); if (position_ >= text_.size()) return false;
        if (text_[position_] == L'{') return Object();
        if (text_[position_] == L'[') return Array();
        if (text_[position_] == L'\"') return String();
        return Literal(L"true") || Literal(L"false") || Literal(L"null") || Number();
    }
    const std::wstring& text_;
    size_t position_ = 0;
};

std::optional<std::wstring> DecodeUtf8(const std::string& bytes) {
    if (bytes.empty()) return std::wstring{};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (length <= 0) return std::nullopt;
    std::wstring text(length, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), text.data(), length);
    return text;
}

std::optional<std::string> EncodeUtf8(const std::wstring& text) {
    if (text.empty()) return std::string{};
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return std::nullopt;
    std::string bytes(length, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), bytes.data(), length, nullptr, nullptr);
    return bytes;
}

std::optional<std::wstring> ReadJsonString(const std::wstring& text, const wchar_t* field) {
    std::wsmatch match;
    const std::wregex pattern(std::wstring(L"\"") + field + LR"re(\"\s*:\s*\"((?:\\.|[^\"])*)\")re");
    if (!std::regex_search(text, match, pattern)) return std::nullopt;
    return Unescape(match[1]);
}

bool HasJsonField(const std::wstring& text, const wchar_t* field) {
    return std::regex_search(text, std::wregex(std::wstring(L"\"") + field + LR"re(\"\s*:)re"));
}

std::wstring ThemeName(ThemePreference theme) {
    return theme == ThemePreference::Light ? L"Light" : theme == ThemePreference::Dark ? L"Dark" : L"Follow-System";
}

} // namespace

std::wstring SettingsPath() {
    const std::wstring directory = AppDirectory();
    return directory.empty() ? std::wstring{} : directory + L"\\settings.json";
}

Settings DefaultSettings() {
    Settings settings;
    DWORD drives = GetLogicalDrives();
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
    settings.shortcuts = {{L"Back", L"Alt+Left"}, {L"Forward", L"Alt+Right"}, {L"Settings", L"Ctrl+,"}};
    settings.storageCategories = {{L"Documents", L".doc;.docx;.pdf;.txt"}, {L"Images", L".png;.jpg;.jpeg;.gif"}};
    return settings;
}

Settings LoadSettings(bool preserveCorruptFile) {
    Settings result = DefaultSettings();
    const std::wstring path = SettingsPath();
    if (path.empty()) return result;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (preserveCorruptFile) SaveSettings(result);
        return result;
    }
    const std::string bytes((std::istreambuf_iterator<char>(file)), {});
    file.close();
    const auto decoded = DecodeUtf8(bytes);
    if (!decoded || !JsonValidator(*decoded).Validate()) {
        if (preserveCorruptFile) {
            MoveFileExW(path.c_str(), (path + L".bak").c_str(), MOVEFILE_REPLACE_EXISTING);
            SaveSettings(result);
        }
        LogFallback(L"Settings parse failed; defaults loaded and source preserved as settings.json.bak.");
        return result;
    }
    const std::wstring& text = *decoded;
    try {
        // Each section is independently recognized; an invalid/missing value leaves its default intact.
        std::wsmatch match;
        if (std::regex_search(text, match, std::wregex(LR"re("schemaVersion"\s*:\s*([0-9]+))re"))) {
            try { result.schemaVersion = std::stoul(match[1]); }
            catch (const std::exception&) { LogFallback(L"Invalid schemaVersion; default version loaded."); }
        } else if (HasJsonField(text, L"schemaVersion")) LogFallback(L"Invalid schemaVersion; default version loaded.");
        if (const auto theme = ReadJsonString(text, L"theme")) {
            if (*theme == L"Light") result.theme = ThemePreference::Light;
            else if (*theme == L"Dark") result.theme = ThemePreference::Dark;
            else if (*theme == L"Follow-System") result.theme = ThemePreference::FollowSystem;
            else LogFallback(L"Invalid appearance section; appearance defaults loaded.");
        } else if (HasJsonField(text, L"theme")) LogFallback(L"Invalid appearance section; appearance defaults loaded.");
        if (const auto value = ReadJsonString(text, L"defaultSearchScope")) result.defaultSearchScope = *value;
        else if (HasJsonField(text, L"defaultSearchScope")) LogFallback(L"Invalid search section; search defaults loaded.");
        if (std::regex_search(text, match, std::wregex(LR"re("retainSearchHistory"\s*:\s*(true|false))re"))) result.retainSearchHistory = match[1] == L"true";
        else if (HasJsonField(text, L"retainSearchHistory")) LogFallback(L"Invalid search section; search defaults loaded.");
        if (const auto value = ReadJsonString(text, L"startupLocation")) result.startupLocation = *value;
        else if (HasJsonField(text, L"startupLocation")) LogFallback(L"Invalid navigation section; navigation defaults loaded.");
        if (std::regex_search(text, match, std::wregex(LR"re("restorePreviousSession"\s*:\s*(true|false))re"))) result.restorePreviousSession = match[1] == L"true";
        else if (HasJsonField(text, L"restorePreviousSession")) LogFallback(L"Invalid navigation section; navigation defaults loaded.");
        if (std::regex_search(text, match, std::wregex(LR"re("previewEnabled"\s*:\s*(true|false))re"))) result.previewEnabled = match[1] == L"true";
        else if (HasJsonField(text, L"previewEnabled")) LogFallback(L"Invalid preview section; preview defaults loaded.");
        if (std::regex_search(text, match, std::wregex(LR"re("maxAutoPreviewBytes"\s*:\s*([0-9]+))re"))) {
            try { result.maxAutoPreviewBytes = std::stoull(match[1]); }
            catch (const std::exception&) { LogFallback(L"Invalid preview section; preview defaults loaded."); }
        } else if (HasJsonField(text, L"maxAutoPreviewBytes")) LogFallback(L"Invalid preview section; preview defaults loaded.");

        std::vector<VolumeSetting> volumes;
        const std::wsregex_iterator regexEnd;
        const std::wregex volumePattern(LR"re(\{\s*"key"\s*:\s*"((?:\\.|[^"])*)"\s*,\s*"enabled"\s*:\s*(true|false)\s*,\s*"rules"\s*:\s*\[([^\]]*)\])re");
        const std::wregex rulePattern(LR"re(\{\s*"path"\s*:\s*"((?:\\.|[^"])*)"\s*,\s*"action"\s*:\s*"(include|exclude)"\s*\})re");
        for (std::wsregex_iterator volumeIt(text.begin(), text.end(), volumePattern); volumeIt != regexEnd; ++volumeIt) {
            VolumeSetting volume;
            const auto key = Unescape((*volumeIt)[1]);
            if (!key) continue;
            volume.key = *key;
            volume.enabled = (*volumeIt)[2] == L"true";
            const std::wstring rules = (*volumeIt)[3];
            for (std::wsregex_iterator ruleIt(rules.begin(), rules.end(), rulePattern); ruleIt != regexEnd; ++ruleIt) {
                const auto rulePath = Unescape((*ruleIt)[1]);
                if (rulePath) volume.rules.push_back({*rulePath, (*ruleIt)[2] == L"include"});
            }
            volumes.push_back(std::move(volume));
        }
        if (!volumes.empty() || std::regex_search(text, std::wregex(LR"re("volumes"\s*:\s*\[\s*\])re"))) {
            result.indexing = std::move(volumes);
        }

        std::vector<std::pair<std::wstring, std::wstring>> shortcuts;
        const std::wregex shortcutPattern(LR"re(\{\s*"command"\s*:\s*"((?:\\.|[^"])*)"\s*,\s*"binding"\s*:\s*"((?:\\.|[^"])*)"\s*\})re");
        for (std::wsregex_iterator it(text.begin(), text.end(), shortcutPattern); it != regexEnd; ++it) {
            const auto command = Unescape((*it)[1]); const auto binding = Unescape((*it)[2]);
            if (command && binding) shortcuts.emplace_back(*command, *binding);
        }
        if (!shortcuts.empty() || std::regex_search(text, std::wregex(LR"re("bindings"\s*:\s*\[\s*\])re"))) {
            result.shortcuts = std::move(shortcuts);
        }

        std::vector<std::pair<std::wstring, std::wstring>> categories;
        const std::wregex categoryPattern(LR"re(\{\s*"name"\s*:\s*"((?:\\.|[^"])*)"\s*,\s*"extensions"\s*:\s*"((?:\\.|[^"])*)"\s*\})re");
        for (std::wsregex_iterator it(text.begin(), text.end(), categoryPattern); it != regexEnd; ++it) {
            const auto name = Unescape((*it)[1]); const auto extensions = Unescape((*it)[2]);
            if (name && extensions) categories.emplace_back(*name, *extensions);
        }
        if (!categories.empty() || std::regex_search(text, std::wregex(LR"re("categories"\s*:\s*\[\s*\])re"))) {
            result.storageCategories = std::move(categories);
        }
    } catch (const std::exception&) {
        LogFallback(L"Invalid settings section; affected section defaults loaded.");
    }
    return result;
}

bool SaveSettings(const Settings& settings) {
    const std::wstring path = SettingsPath();
    if (path.empty()) return false;
    const std::wstring temporary = path + L".tmp";
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
         << L"\"},\n  \"navigation\": {\"startupLocation\":\"" << Escape(settings.startupLocation) << L"\",\"restorePreviousSession\":" << (settings.restorePreviousSession ? L"true" : L"false")
         << L"},\n  \"shortcuts\": {\"bindings\":[";
    for (size_t i = 0; i < settings.shortcuts.size(); ++i) {
        if (i) file << L",";
        file << L"{\"command\":\"" << Escape(settings.shortcuts[i].first) << L"\",\"binding\":\"" << Escape(settings.shortcuts[i].second) << L"\"}";
    }
    file << L"]},\n  \"preview\": {\"previewEnabled\":" << (settings.previewEnabled ? L"true" : L"false") << L",\"maxAutoPreviewBytes\":" << settings.maxAutoPreviewBytes
         << L"},\n  \"storage-analysis\": {\"categories\":[";
    for (size_t i = 0; i < settings.storageCategories.size(); ++i) {
        if (i) file << L",";
        file << L"{\"name\":\"" << Escape(settings.storageCategories[i].first) << L"\",\"extensions\":\"" << Escape(settings.storageCategories[i].second) << L"\"}";
    }
    file << L"]}\n}\n";
    const auto bytes = EncodeUtf8(file.str());
    if (!bytes) return false;
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(bytes->data(), static_cast<std::streamsize>(bytes->size()));
    output.close();
    if (!output) return false;
    return MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

bool ResetSettings() { return SaveSettings(DefaultSettings()); }

bool IsPathIncluded(const VolumeSetting& volume, const std::wstring& canonicalPath) noexcept {
    bool included = volume.enabled;
    size_t longest = 0;
    for (const auto& rule : volume.rules) {
        if (canonicalPath.size() >= rule.path.size() && _wcsnicmp(canonicalPath.c_str(), rule.path.c_str(), rule.path.size()) == 0 && rule.path.size() >= longest) {
            longest = rule.path.size(); included = rule.include;
        }
    }
    return included;
}

} // namespace ffprotocol
