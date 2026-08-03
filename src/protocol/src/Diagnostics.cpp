#include "ffprotocol/Diagnostics.h"

#include <windows.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace ffprotocol {
namespace {

std::mutex& LogMutex() {
    static std::mutex mutex;
    return mutex;
}

std::wstring EnvironmentDirectory(const wchar_t* variable) {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetEnvironmentVariableW(variable, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return {};
    return std::wstring(buffer.data(), length);
}

std::wstring AppDirectory() {
    // Per-user clients keep diagnostics under LOCALAPPDATA. A service token
    // may not receive that user-scoped variable, so fall back to the machine
    // scope rather than silently dropping the only persistent evidence of a
    // raw-volume-open attempt.
    std::wstring base = EnvironmentDirectory(L"LOCALAPPDATA");
    if (base.empty()) {
        base = EnvironmentDirectory(L"PROGRAMDATA");
    }
    return base.empty() ? std::wstring{} : base + L"\\FastFiles";
}

const wchar_t* CategoryName(DiagnosticCategory category) {
    switch (category) {
        case DiagnosticCategory::IndexingError: return L"indexing-error";
        case DiagnosticCategory::InaccessibleDirectory: return L"inaccessible-directory";
        case DiagnosticCategory::VolumeStateTransition: return L"volume-state";
        case DiagnosticCategory::DatabaseError: return L"database-error";
    }
    return L"unknown";
}

std::wstring SafeField(const std::wstring& value) {
    std::wstring result;
    result.reserve(value.size());
    for (wchar_t ch : value) {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t') result.push_back(L' ');
        else result.push_back(ch);
    }
    return result;
}

} // namespace

std::wstring DiagnosticLogPath() {
    const std::wstring directory = AppDirectory();
    return directory.empty() ? std::wstring{} : directory + L"\\logs\\diagnostics.log";
}

bool AppendDiagnostic(const DiagnosticEvent& event) {
    const std::wstring path = DiagnosticLogPath();
    if (path.empty()) return false;
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
    if (error) return false;
    std::lock_guard lock(LogMutex());
    std::wofstream output(path, std::ios::app);
    if (!output) return false;
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    output << now << L" category=" << CategoryName(event.category)
           << L" path=" << SafeField(event.path)
           << L" volume=" << SafeField(event.volumeId)
           << L" state=" << SafeField(event.state)
           << L" outcome=" << SafeField(event.outcome)
           << L" account=" << SafeField(event.accountName)
           << L" sid=" << SafeField(event.accountSid)
           << L" privilegeHeld=" << (event.privilegeHeld ? 1 : 0)
           << L" privilegeEnabled=" << (event.privilegeEnabled ? 1 : 0)
           << L" error=0x" << std::hex << event.errorCode << std::dec
           << L" items=" << event.itemCount;
    // Matrix-only fields: emitted solely for candidate-matrix rows (those
    // that set candidateId), so non-matrix events keep their exact prior
    // line format and any existing log parser stays unaffected.
    if (!event.candidateId.empty()) {
        output << L" candidate=" << SafeField(event.candidateId)
               << L" privilegeName=" << SafeField(event.privilegeName)
               << L" group=" << SafeField(event.groupContext)
               << L" journalQueried=" << (event.journalQueried ? 1 : 0)
               << L" journalError=0x" << std::hex << event.journalQueryError << std::dec
               << L" journalRead=" << (event.journalRead ? 1 : 0)
               << L" journalReadError=0x" << std::hex << event.journalReadError << std::dec
               << L" regOrder=" << event.registrationOrder;
    }
    output << L"\n";
    output.flush();
    return static_cast<bool>(output);
}

namespace {

// settings-and-appearance 8.3: hash a single path component so the redacted
// bundle can show *structure* (a directory existed at depth N with M errors)
// without leaking the literal name. FNV-1a over UTF-16 code units, rendered
// as lowercase hex.
std::wstring HashComponent(const std::wstring& component) {
    uint32_t hash = 2166136261u;
    for (wchar_t ch : component) {
        const uint32_t low = static_cast<uint32_t>(static_cast<uint16_t>(ch) & 0xFF);
        const uint32_t high = static_cast<uint32_t>(static_cast<uint16_t>(ch) >> 8);
        hash ^= low;
        hash *= 16777619u;
        hash ^= high;
        hash *= 16777619u;
    }
    wchar_t buffer[16]{};
    swprintf_s(buffer, L"%08x", hash);
    return buffer;
}

// 8.4: strips a path to its literal form for the opt-in bundle, or to a
// redacted structure summary (depth + hashed components) by default.
std::wstring RedactPath(const std::wstring& path, bool includeLiteralPaths) {
    if (includeLiteralPaths) {
        return path;
    }
    std::wstring result;
    size_t depth = 0;
    size_t start = 0;
    while (start < path.size()) {
        const size_t end = path.find_first_of(L"\\/", start);
        if (end == std::wstring::npos) {
            if (start < path.size()) {
                ++depth;
                if (depth > 1 && !result.empty()) result += L"\\";
                result += HashComponent(path.substr(start));
            }
            break;
        }
        if (end > start) {
            ++depth;
            if (depth > 1 && !result.empty()) result += L"\\";
            result += HashComponent(path.substr(start, end - start));
        }
        start = end + 1;
    }
    return result;
}

// Parses one diagnostics.log line into a category/error/path triple so the
// bundle can aggregate counts without materializing every raw entry.
struct LogLine {
    std::wstring category;
    std::wstring path;
    std::wstring volume;
    uint32_t errorCode = 0;
};

bool ParseLogLine(const std::wstring& line, LogLine& out) {
    // Format: "<ts> category=<name> path=<p> volume=<v> state=<s> outcome=<o> ..."
    const auto field = [&line](const wchar_t* name) {
        const std::wstring marker = std::wstring(name) + L"=";
        const size_t start = line.find(marker);
        if (start == std::wstring::npos) return std::wstring{};
        const size_t valueStart = start + marker.size();
        const size_t valueEnd = line.find(L' ', valueStart);
        if (valueStart >= line.size()) return std::wstring{};
        return line.substr(valueStart, valueEnd == std::wstring::npos ? std::wstring::npos : valueEnd - valueStart);
    };
    out.category = field(L"category");
    out.path = field(L"path");
    out.volume = field(L"volume");
    const std::wstring errorText = field(L"error");
    if (!errorText.empty() && errorText.size() > 2 && errorText[0] == L'0' &&
        (errorText[1] == L'x' || errorText[1] == L'X')) {
        out.errorCode = static_cast<uint32_t>(wcstoul(errorText.c_str() + 2, nullptr, 16));
    }
    return !out.category.empty();
}

} // namespace

bool ExportDiagnosticBundle(const std::wstring& destinationPath, bool includeLiteralPaths) {
    const std::wstring sourcePath = DiagnosticLogPath();
    if (sourcePath.empty() || destinationPath.empty()) {
        return false;
    }

    std::ifstream input(sourcePath, std::ios::binary);
    if (!input) {
        return false;
    }
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size <= 0) {
        return false;
    }
    std::string bytes(static_cast<size_t>(size), '\0');
    input.seekg(0, std::ios::beg);
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!input) {
        return false;
    }

    // The log is written via std::wofstream under the "C" locale, which on
    // MSVC narrows each wchar_t to one char; the on-disk bytes are ASCII.
    // Convert the whole buffer once (CP_ACP round-trips ASCII exactly) and
    // split on L'\n'.
    std::wstring text;
    if (!bytes.empty()) {
        const int needed = MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        if (needed > 0) {
            text.resize(static_cast<size_t>(needed));
            MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(), needed);
        }
    }

    // Aggregates: category -> count, error code -> count, depth -> count
    // (directory-structure summary), plus the raw redacted log body.
    std::map<std::wstring, uint32_t> categoryCounts;
    std::map<uint32_t, uint32_t> errorCounts;
    std::map<uint32_t, uint32_t> depthCounts;
    std::map<std::wstring, uint32_t> volumeCounts;
    std::wstring redactedBody;
    redactedBody.reserve(text.size());

    size_t lineStart = 0;
    while (lineStart < text.size()) {
        size_t lineEnd = text.find(L'\n', lineStart);
        if (lineEnd == std::wstring::npos) lineEnd = text.size();
        std::wstring line = text.substr(lineStart, lineEnd - lineStart);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        lineStart = lineEnd + 1;
        if (line.empty()) continue;

        LogLine parsed{};
        if (ParseLogLine(line, parsed)) {
            ++categoryCounts[parsed.category];
            ++volumeCounts[parsed.volume.empty() ? L"(none)" : parsed.volume];
            if (parsed.errorCode != 0) ++errorCounts[parsed.errorCode];
            if (!parsed.path.empty()) {
                size_t depth = 1;
                for (wchar_t ch : parsed.path) {
                    if (ch == L'\\' || ch == L'/') ++depth;
                }
                ++depthCounts[static_cast<uint32_t>(depth)];
            }
        }

        // Redact per-line: replace the path= value with its redacted form.
        if (!includeLiteralPaths && !parsed.path.empty()) {
            const size_t marker = line.find(L"path=");
            if (marker != std::wstring::npos) {
                const size_t valueStart = marker + 5;
                const size_t valueEnd = line.find(L' ', valueStart);
                const size_t end = valueEnd == std::wstring::npos ? line.size() : valueEnd;
                const std::wstring redacted = RedactPath(parsed.path, false);
                line.replace(valueStart, end - valueStart, redacted);
            }
        }
        redactedBody += line;
        redactedBody += L"\n";
    }

    std::wofstream output(destinationPath, std::ios::trunc);
    if (!output) {
        return false;
    }
    output << L"FastFiles diagnostic bundle\n";
    output << L"export mode: " << (includeLiteralPaths ? L"literal paths (opt-in)" : L"redacted (default)")
           << L"\n\n";
    output << L"== Per-category event counts ==\n";
    for (const auto& [category, count] : categoryCounts) {
        output << category << L": " << count << L"\n";
    }
    output << L"\n== Per-volume event counts ==\n";
    for (const auto& [volume, count] : volumeCounts) {
        output << volume << L": " << count << L"\n";
    }
    output << L"\n== Error-code histogram ==\n";
    for (const auto& [code, count] : errorCounts) {
        wchar_t buffer[16]{};
        swprintf_s(buffer, L"0x%08x", code);
        output << buffer << L": " << count << L"\n";
    }
    output << L"\n== Directory-structure depth summary (errors per depth) ==\n";
    for (const auto& [depth, count] : depthCounts) {
        output << L"depth " << depth << L": " << count << L"\n";
    }
    output << L"\n== Redacted log body ==\n";
    output << redactedBody;
    output.flush();
    return static_cast<bool>(output);
}

} // namespace ffprotocol
