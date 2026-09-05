#include "ffprotocol/Diagnostics.h"

#include <windows.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace ffprotocol {
namespace {

// A record must reach the file in one WriteFile call for the append to stay
// atomic, so an over-long record is dropped rather than torn across two
// writes. This bound is deliberately unreachable for any legitimate record:
// the largest field is a path, capped at kMaxPathLengthChars (32,767), which
// costs at most 3 UTF-8 bytes per UTF-16 code unit (~98 KB), or 6 bytes per
// unit once JSON-escaped as \uXXXX (~197 KB). One mebibyte leaves ~5x
// headroom over that worst case, so the drop path is a backstop against a
// corrupt event -- never a size a real diagnostic can reach. A smaller cap
// would silently discard long-path diagnostics, because every caller
// discards this function's return value.
constexpr size_t kMaximumRecordBytes = 1024 * 1024;

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
        case DiagnosticCategory::UiInteraction: return L"ui-interaction";
    }
    return L"unknown";
}

std::wstring JsonEscape(const std::wstring& value) {
    std::wstring result;
    result.reserve(value.size());
    for (wchar_t ch : value) {
        switch (ch) {
            case L'"': result += L"\\\""; break;
            case L'\\': result += L"\\\\"; break;
            case L'\b': result += L"\\b"; break;
            case L'\f': result += L"\\f"; break;
            case L'\n': result += L"\\n"; break;
            case L'\r': result += L"\\r"; break;
            case L'\t': result += L"\\t"; break;
            default:
                if (static_cast<unsigned int>(ch) < 0x20u) {
                    wchar_t escaped[7]{};
                    swprintf_s(escaped, L"\\u%04x", static_cast<unsigned int>(ch));
                    result += escaped;
                } else {
                    result.push_back(ch);
                }
                break;
        }
    }
    return result;
}

// The diagnostics log is written by more than one process (UI, engine,
// service). Encode explicitly as UTF-8 rather than letting a wide stream
// narrow through the "C" locale, which fails the whole record -- silently,
// because every caller discards the return value -- as soon as a path
// contains a non-ASCII character.
std::string NarrowUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string result(static_cast<size_t>(needed), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), needed,
                            nullptr, nullptr) != needed) {
        return {};
    }
    return result;
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

const wchar_t* DiagnosticCodeName(DiagnosticCode code) {
    switch (code) {
        case DiagnosticCode::FFDIAG_UNKNOWN: return L"FFDIAG_UNKNOWN";
        case DiagnosticCode::FFDIAG_CORRUPT_DB: return L"FFDIAG_CORRUPT_DB";
        case DiagnosticCode::FFDIAG_DATABASE_OPEN_FAILED: return L"FFDIAG_DATABASE_OPEN_FAILED";
        case DiagnosticCode::FFDIAG_WATCH_EVICTED: return L"FFDIAG_WATCH_EVICTED";
        case DiagnosticCode::FFDIAG_WATCH_EVENT_CREATE_FAILED: return L"FFDIAG_WATCH_EVENT_CREATE_FAILED";
        case DiagnosticCode::FFDIAG_WATCH_BUFFER_OVERFLOW: return L"FFDIAG_WATCH_BUFFER_OVERFLOW";
        case DiagnosticCode::FFDIAG_WATCH_GAVE_UP: return L"FFDIAG_WATCH_GAVE_UP";
        case DiagnosticCode::FFDIAG_WATCH_FAILED: return L"FFDIAG_WATCH_FAILED";
        case DiagnosticCode::FFDIAG_SNAPSHOT_PUBLISH_FAILED: return L"FFDIAG_SNAPSHOT_PUBLISH_FAILED";
        case DiagnosticCode::FFDIAG_FORGET_VOLUME_FAILED: return L"FFDIAG_FORGET_VOLUME_FAILED";
        case DiagnosticCode::FFDIAG_ENGINE_STATUS_CHANGED: return L"FFDIAG_ENGINE_STATUS_CHANGED";
        case DiagnosticCode::FFDIAG_DIRECTORY_ACCESS_DENIED: return L"FFDIAG_DIRECTORY_ACCESS_DENIED";
        case DiagnosticCode::FFDIAG_DIRECTORY_NOT_FOUND: return L"FFDIAG_DIRECTORY_NOT_FOUND";
        case DiagnosticCode::FFDIAG_DIRECTORY_ENUMERATION_FAILED: return L"FFDIAG_DIRECTORY_ENUMERATION_FAILED";
        case DiagnosticCode::FFDIAG_ENGINE_DIRECTORY_REQUEST_RECEIVED: return L"FFDIAG_ENGINE_DIRECTORY_REQUEST_RECEIVED";
        case DiagnosticCode::FFDIAG_ENGINE_UI_SERVER_START_FAILED: return L"FFDIAG_ENGINE_UI_SERVER_START_FAILED";
        case DiagnosticCode::FFDIAG_ENGINE_PARK_EVENT_FAILED: return L"FFDIAG_ENGINE_PARK_EVENT_FAILED";
        case DiagnosticCode::FFDIAG_ENGINE_LAUNCHED: return L"FFDIAG_ENGINE_LAUNCHED";
        case DiagnosticCode::FFDIAG_ENGINE_PIPE_NOT_READY: return L"FFDIAG_ENGINE_PIPE_NOT_READY";
        case DiagnosticCode::FFDIAG_ENGINE_LAUNCH_FAILED: return L"FFDIAG_ENGINE_LAUNCH_FAILED";
        case DiagnosticCode::FFDIAG_ENGINE_NOT_FOUND: return L"FFDIAG_ENGINE_NOT_FOUND";
        case DiagnosticCode::FFDIAG_ENGINE_CONNECT_FAILED: return L"FFDIAG_ENGINE_CONNECT_FAILED";
        case DiagnosticCode::FFDIAG_ENGINE_CONNECT_SUCCEEDED: return L"FFDIAG_ENGINE_CONNECT_SUCCEEDED";
        case DiagnosticCode::FFDIAG_ENGINE_UNAVAILABLE: return L"FFDIAG_ENGINE_UNAVAILABLE";
        case DiagnosticCode::FFDIAG_ENGINE_DISCONNECTED: return L"FFDIAG_ENGINE_DISCONNECTED";
        case DiagnosticCode::FFDIAG_UI_DIRECTORY_REQUEST_QUEUED: return L"FFDIAG_UI_DIRECTORY_REQUEST_QUEUED";
        case DiagnosticCode::FFDIAG_UI_DIRECTORY_REQUEST_SENT: return L"FFDIAG_UI_DIRECTORY_REQUEST_SENT";
        case DiagnosticCode::FFDIAG_UI_SNAPSHOT_PATH_MISS: return L"FFDIAG_UI_SNAPSHOT_PATH_MISS";
        case DiagnosticCode::FFDIAG_UI_SNAPSHOT_COLUMN_REFRESH: return L"FFDIAG_UI_SNAPSHOT_COLUMN_REFRESH";
        case DiagnosticCode::FFDIAG_UI_SNAPSHOT_READ_FAILED: return L"FFDIAG_UI_SNAPSHOT_READ_FAILED";
        case DiagnosticCode::FFDIAG_RAW_VOLUME_OPEN: return L"FFDIAG_RAW_VOLUME_OPEN";
        case DiagnosticCode::FFDIAG_CANDIDATE_MATRIX: return L"FFDIAG_CANDIDATE_MATRIX";
        case DiagnosticCode::FFDIAG_STARTUP_TOKEN: return L"FFDIAG_STARTUP_TOKEN";
        case DiagnosticCode::FFDIAG_PROJECTION_MEMORY_PRESSURE: return L"FFDIAG_PROJECTION_MEMORY_PRESSURE";
        case DiagnosticCode::FFDIAG_ENGINE_SIGNATURE_REJECTED: return L"FFDIAG_ENGINE_SIGNATURE_REJECTED";
        case DiagnosticCode::FFDIAG_AUTHENTICODE_PINS_UNCONFIGURED: return L"FFDIAG_AUTHENTICODE_PINS_UNCONFIGURED";
        case DiagnosticCode::FFDIAG_SNAPSHOT_SUBSCRIBE_INVALID: return L"FFDIAG_SNAPSHOT_SUBSCRIBE_INVALID";
        case DiagnosticCode::FFDIAG_WATCH_TERMINAL_REFRESH: return L"FFDIAG_WATCH_TERMINAL_REFRESH";
        case DiagnosticCode::FFDIAG_INDEXED_MERGE_SUPPRESSED: return L"FFDIAG_INDEXED_MERGE_SUPPRESSED";
        case DiagnosticCode::FFDIAG_UI_RESULT_SUPERSEDED: return L"FFDIAG_UI_RESULT_SUPERSEDED";
        case DiagnosticCode::FFDIAG_UI_COMMAND_DISPATCHED: return L"FFDIAG_UI_COMMAND_DISPATCHED";
        case DiagnosticCode::FFDIAG_UI_PROVIDER_CHILD_QUERY: return L"FFDIAG_UI_PROVIDER_CHILD_QUERY";
        case DiagnosticCode::FFDIAG_SCAN_FAILED: return L"FFDIAG_SCAN_FAILED";
        case DiagnosticCode::FFDIAG_STALE_SCAN_TERMINAL: return L"FFDIAG_STALE_SCAN_TERMINAL";
        case DiagnosticCode::FFDIAG_SCAN_BATCH_APPLY_FAILED: return L"FFDIAG_SCAN_BATCH_APPLY_FAILED";
        case DiagnosticCode::FFDIAG_RECONCILIATION_DEFERRED: return L"FFDIAG_RECONCILIATION_DEFERRED";
        case DiagnosticCode::FFDIAG_DB_INTEGRITY_INCONCLUSIVE: return L"FFDIAG_DB_INTEGRITY_INCONCLUSIVE";
    }
    return L"FFDIAG_UNKNOWN";
}

std::wstring DiagnosticLogPath() {
    const std::wstring directory = AppDirectory();
    return directory.empty() ? std::wstring{} : directory + L"\\logs\\diagnostics.log";
}

std::wstring SerializeDiagnosticEvent(const DiagnosticEvent& event, DiagnosticFormat format) {
    // One time point for both fields: the leading seconds token is the
    // stable, externally-parsed identity, and epochMs resolves ordering
    // between records that share a second.
    const auto capturedAt = std::chrono::system_clock::now();
    const auto now = std::chrono::system_clock::to_time_t(capturedAt);
    const long long epochMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(capturedAt.time_since_epoch()).count();
    const unsigned long processId = GetCurrentProcessId();
    const unsigned long threadId = GetCurrentThreadId();
    const auto message = !event.state.empty() ? event.state
        : (!event.outcome.empty() ? event.outcome : L"diagnostic");
    std::wostringstream output;
    if (format == DiagnosticFormat::Json) {
        output << L"{\"timestamp\":" << now
               << L",\"category\":\"" << JsonEscape(CategoryName(event.category))
               << L"\",\"code\":" << static_cast<uint32_t>(event.code)
               << L",\"codeName\":\"" << DiagnosticCodeName(event.code)
               << L"\",\"message\":\"" << JsonEscape(message)
               << L"\",\"path\":\"" << JsonEscape(event.path)
               << L"\",\"volume\":\"" << JsonEscape(event.volumeId)
               << L"\",\"state\":\"" << JsonEscape(event.state)
               << L"\",\"outcome\":\"" << JsonEscape(event.outcome)
               << L"\",\"account\":\"" << JsonEscape(event.accountName)
               << L"\",\"sid\":\"" << JsonEscape(event.accountSid)
               << L"\",\"privilegeHeld\":" << (event.privilegeHeld ? L"true" : L"false")
               << L",\"privilegeEnabled\":" << (event.privilegeEnabled ? L"true" : L"false")
               << L",\"errorCode\":" << event.errorCode
               << L",\"items\":" << event.itemCount;
        if (!event.candidateId.empty()) {
            output << L",\"candidate\":\"" << JsonEscape(event.candidateId)
                   << L"\",\"privilegeName\":\"" << JsonEscape(event.privilegeName)
                   << L"\",\"group\":\"" << JsonEscape(event.groupContext)
                   << L"\",\"journalQueried\":" << (event.journalQueried ? L"true" : L"false")
                   << L",\"journalError\":" << event.journalQueryError
                   << L",\"journalRead\":" << (event.journalRead ? L"true" : L"false")
                   << L",\"journalReadError\":" << event.journalReadError
                   << L",\"registrationOrder\":" << event.registrationOrder;
        }
        if (!event.signatureThumbprint.empty()) {
            output << L",\"signatureThumbprint\":\""
                   << JsonEscape(event.signatureThumbprint) << L"\"";
        }
        // Appended last so existing key lookups are unaffected. Process and
        // thread identity are what make a multi-process log attributable.
        output << L",\"pid\":" << processId
               << L",\"tid\":" << threadId
               << L",\"epochMs\":" << epochMs;
        output << L"}";
        return output.str();
    }

    output << now << L" category=" << CategoryName(event.category)
           << L" code=" << static_cast<uint32_t>(event.code)
           << L" codeName=" << DiagnosticCodeName(event.code)
           << L" path=" << SafeField(event.path)
           << L" volume=" << SafeField(event.volumeId)
           << L" state=" << SafeField(event.state)
           << L" message=" << SafeField(message)
           << L" outcome=" << SafeField(event.outcome)
           << L" account=" << SafeField(event.accountName)
           << L" sid=" << SafeField(event.accountSid)
           << L" privilegeHeld=" << (event.privilegeHeld ? 1 : 0)
           << L" privilegeEnabled=" << (event.privilegeEnabled ? 1 : 0)
           << L" error=0x" << std::hex << event.errorCode << std::dec
           << L" items=" << event.itemCount;
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
    if (!event.signatureThumbprint.empty()) {
        output << L" signatureThumbprint=" << SafeField(event.signatureThumbprint);
    }
    // Append-only trailing fields: substring-based consumers are unaffected.
    output << L" pid=" << processId << L" tid=" << threadId << L" epochMs=" << epochMs;
    return output.str();
}

bool AppendDiagnostic(const DiagnosticEvent& event, DiagnosticFormat format) {
    const std::wstring path = DiagnosticLogPath();
    if (path.empty()) return false;
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
    if (error) return false;

    const std::string record = NarrowUtf8(SerializeDiagnosticEvent(event, format) + L"\n");
    if (record.empty() || record.size() > kMaximumRecordBytes) return false;

    std::lock_guard lock(LogMutex());
    // The UI, engine and service all append to this one file. A wide stream
    // opened with std::ios::app appends by seeking to end-of-file and then
    // writing at that offset, which is atomic only within a single process
    // (LogMutex). Two processes that resolve the same end offset overwrite
    // each other, so records were silently destroyed -- observed as orphaned
    // record tails in retained logs, and as a lost
    // FFDIAG_ENGINE_CONNECT_SUCCEEDED when a just-launched engine's first
    // record collided with it.
    //
    // FILE_APPEND_DATA *without* FILE_WRITE_DATA makes the filesystem pick
    // the offset under its own lock, so one WriteFile of a whole record is
    // atomic against every other appender. LogMutex is kept for
    // intra-process ordering.
    const HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL wrote = WriteFile(file, record.data(), static_cast<DWORD>(record.size()), &written, nullptr);
    CloseHandle(file);
    return wrote != FALSE && written == record.size();
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

std::wstring JsonUnescape(const std::wstring& value) {
    std::wstring result;
    result.reserve(value.size());
    for (size_t index = 0; index < value.size(); ++index) {
        if (value[index] != L'\\' || index + 1 >= value.size()) {
            result.push_back(value[index]);
            continue;
        }
        const wchar_t escaped = value[++index];
        switch (escaped) {
            case L'"': result.push_back(L'"'); break;
            case L'\\': result.push_back(L'\\'); break;
            case L'/': result.push_back(L'/'); break;
            case L'b': result.push_back(L'\b'); break;
            case L'f': result.push_back(L'\f'); break;
            case L'n': result.push_back(L'\n'); break;
            case L'r': result.push_back(L'\r'); break;
            case L't': result.push_back(L'\t'); break;
            case L'u': {
                if (index + 4 >= value.size()) return {};
                const std::wstring hex = value.substr(index + 1, 4);
                wchar_t* end = nullptr;
                const unsigned long code = wcstoul(hex.c_str(), &end, 16);
                if (end == nullptr || end != hex.c_str() + hex.size()) return {};
                result.push_back(static_cast<wchar_t>(code));
                index += 4;
                break;
            }
            default: return {};
        }
    }
    return result;
}

bool ExtractJsonString(const std::wstring& line, const wchar_t* name,
                       std::wstring& value, size_t* valueStart = nullptr, size_t* valueEnd = nullptr) {
    const std::wstring marker = std::wstring(L"\"") + name + L"\":\"";
    const size_t start = line.find(marker);
    if (start == std::wstring::npos) return false;
    const size_t encodedStart = start + marker.size();
    size_t end = encodedStart;
    for (; end < line.size(); ++end) {
        if (line[end] == L'\\') {
            ++end;
            continue;
        }
        if (line[end] == L'\"') break;
    }
    if (end >= line.size()) return false;
    value = JsonUnescape(line.substr(encodedStart, end - encodedStart));
    if (valueStart != nullptr) *valueStart = encodedStart;
    if (valueEnd != nullptr) *valueEnd = end;
    return true;
}

bool ExtractJsonNumber(const std::wstring& line, const wchar_t* name, uint32_t& value) {
    const std::wstring marker = std::wstring(L"\"") + name + L"\":";
    const size_t start = line.find(marker);
    if (start == std::wstring::npos) return false;
    const size_t valueStart = start + marker.size();
    wchar_t* end = nullptr;
    const unsigned long parsed = wcstoul(line.c_str() + valueStart, &end, 10);
    if (end == line.c_str() + valueStart || parsed > UINT32_MAX) return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}

bool ParseLogLine(const std::wstring& line, LogLine& out) {
    if (!line.empty() && line.front() == L'{') {
        if (!ExtractJsonString(line, L"category", out.category)) return false;
        ExtractJsonString(line, L"path", out.path);
        ExtractJsonString(line, L"volume", out.volume);
        ExtractJsonNumber(line, L"errorCode", out.errorCode);
        return true;
    }
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

    // AppendDiagnostic writes UTF-8. Decode with the matching code page so a
    // non-ASCII path is summarised correctly instead of as mojibake; UTF-8
    // round-trips the ASCII-only records written before this change.
    std::wstring text;
    if (!bytes.empty()) {
        const int needed = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        if (needed > 0) {
            text.resize(static_cast<size_t>(needed));
            MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(), needed);
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

        // Redact per-line: replace the path value with its redacted form in
        // either the legacy key/value stream or the optional JSON stream.
        if (!includeLiteralPaths && !parsed.path.empty()) {
            const std::wstring redacted = RedactPath(parsed.path, false);
            if (!line.empty() && line.front() == L'{') {
                size_t valueStart = 0;
                size_t valueEnd = 0;
                std::wstring ignored;
                if (ExtractJsonString(line, L"path", ignored, &valueStart, &valueEnd)) {
                    line.replace(valueStart, valueEnd - valueStart, JsonEscape(redacted));
                }
            } else {
                const size_t marker = line.find(L"path=");
                if (marker != std::wstring::npos) {
                    const size_t valueStart = marker + 5;
                    const size_t valueEnd = line.find(L' ', valueStart);
                    const size_t end = valueEnd == std::wstring::npos ? line.size() : valueEnd;
                    line.replace(valueStart, end - valueStart, redacted);
                }
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
