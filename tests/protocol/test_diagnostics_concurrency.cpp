// The diagnostics log is appended to by more than one process at a time --
// the UI, the engine it launches, and the service. Appending by seeking to
// end-of-file and then writing at that offset is atomic only within one
// process, so concurrent writers silently overwrote one another's records.
// That destroyed a FFDIAG_ENGINE_CONNECT_SUCCEEDED record in a live UIA
// lifecycle and left orphaned record tails across the retained evidence logs.
//
// This test spawns real writer processes because a single-process thread test
// cannot reproduce it: the in-process log mutex already serialises threads.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include <windows.h>

#include "ffprotocol/Diagnostics.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {

constexpr int kWorkerCount = 4;
constexpr int kRecordsPerWorker = 250;

std::wstring OwnExecutablePath() {
    std::vector<wchar_t> buffer(MAX_PATH * 4);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    return std::wstring(buffer.data(), length);
}

// Records of differing lengths make a torn write obvious: a short record
// landing on an offset a long one already claimed leaves the long record's
// tail behind as an orphan line.
int RunWorker(int workerIndex) {
    for (int record = 0; record < kRecordsPerWorker; ++record) {
        ffprotocol::DiagnosticEvent event{};
        event.category = ffprotocol::DiagnosticCategory::VolumeStateTransition;
        event.code = ffprotocol::DiagnosticCode::FFDIAG_ENGINE_CONNECT_SUCCEEDED;
        event.state = L"worker-" + std::to_wstring(workerIndex);
        event.path = std::wstring(static_cast<size_t>(16 + ((record * 37) % 256)), L'p');
        event.itemCount = static_cast<uint64_t>(record);
        if (!ffprotocol::AppendDiagnostic(event)) {
            return 2;
        }
    }
    return 0;
}

bool IsWellFormedRecord(const std::string& line) {
    size_t index = 0;
    while (index < line.size() && line[index] >= '0' && line[index] <= '9') ++index;
    if (index == 0) return false;
    return line.compare(index, 10, " category=") == 0;
}

std::string PidFieldOf(const std::string& record) {
    const size_t field = record.rfind(" pid=");
    if (field == std::string::npos) return {};
    const size_t valueStart = field + 5;
    const size_t valueEnd = record.find(' ', valueStart);
    return record.substr(valueStart, valueEnd == std::string::npos ? std::string::npos : valueEnd - valueStart);
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "--append-worker") == 0) {
        return RunWorker(std::atoi(argv[2]));
    }

    wchar_t temp[MAX_PATH]{};
    Check(GetTempPathW(MAX_PATH, temp) != 0, "temporary path is available");
    const std::filesystem::path localAppData = std::filesystem::path(temp) /
        (L"FastFiles-diag-concurrency-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code error;
    std::filesystem::remove_all(localAppData, error);
    SetEnvironmentVariableW(L"LOCALAPPDATA", localAppData.c_str());

    const std::wstring exePath = OwnExecutablePath();
    std::vector<HANDLE> workers;
    std::vector<DWORD> workerPids;
    for (int worker = 0; worker < kWorkerCount; ++worker) {
        std::wstring commandLine = L"\"" + exePath + L"\" --append-worker " + std::to_wstring(worker);
        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo{};
        // Inherits this process's environment, so every worker resolves the
        // same LOCALAPPDATA and therefore the same log file.
        const BOOL created = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                                            nullptr, &startupInfo, &processInfo);
        Check(created != FALSE, "concurrent diagnostics writer process starts");
        if (!created) continue;
        CloseHandle(processInfo.hThread);
        workers.push_back(processInfo.hProcess);
        workerPids.push_back(processInfo.dwProcessId);
    }
    Check(workers.size() == static_cast<size_t>(kWorkerCount), "every concurrent writer was launched");

    for (HANDLE worker : workers) {
        Check(WaitForSingleObject(worker, 60000) == WAIT_OBJECT_0,
              "concurrent writer finishes within its budget");
        DWORD exitCode = 1;
        Check(GetExitCodeProcess(worker, &exitCode) != FALSE && exitCode == 0,
              "concurrent writer reports every append as accepted");
        CloseHandle(worker);
    }

    const std::wstring logPath = ffprotocol::DiagnosticLogPath();
    std::ifstream input(logPath, std::ios::binary);
    Check(input.good(), "the concurrently written diagnostics log is readable");
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) lines.push_back(line);
    }

    size_t malformed = 0;
    std::map<std::string, size_t> perWorker;
    std::map<std::string, size_t> perPid;
    for (const std::string& record : lines) {
        if (!IsWellFormedRecord(record)) {
            ++malformed;
            continue;
        }
        for (int worker = 0; worker < kWorkerCount; ++worker) {
            const std::string tag = "state=worker-" + std::to_string(worker);
            if (record.find(tag) != std::string::npos) ++perWorker[tag];
        }
        const std::string pid = PidFieldOf(record);
        if (!pid.empty()) ++perPid[pid];
    }

    // The decisive assertions. Under the previous seek-then-write append all
    // three fail: records are overwritten (count short), their survivors leave
    // orphaned tails (malformed), and individual writers lose records.
    Check(malformed == 0, "no record is torn by a concurrent append");
    Check(lines.size() == static_cast<size_t>(kWorkerCount) * kRecordsPerWorker,
          "every record written by every process survives");
    bool everyWorkerComplete = true;
    for (int worker = 0; worker < kWorkerCount; ++worker) {
        const std::string tag = "state=worker-" + std::to_string(worker);
        if (perWorker[tag] != static_cast<size_t>(kRecordsPerWorker)) everyWorkerComplete = false;
    }
    Check(everyWorkerComplete, "each writer process retains all of its own records");

    // Attribution: a shared multi-process log is only analysable if each
    // record says which process wrote it.
    bool pidsAttributed = perPid.size() == static_cast<size_t>(kWorkerCount);
    for (DWORD pid : workerPids) {
        if (perPid.find(std::to_string(pid)) == perPid.end()) pidsAttributed = false;
    }
    Check(pidsAttributed, "every record is attributed to the process that wrote it");

    std::filesystem::remove_all(localAppData, error);
    if (FailureCount() == 0) std::printf("diagnostics concurrency tests passed\n");
    return FailureCount() == 0 ? 0 : 1;
}
