#include "StalenessMonitor.h"

#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iterator>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace ffindexsvc {

namespace {

using Sha256Digest = std::array<uint8_t, 32>;

constexpr std::chrono::minutes kStalenessCheckInterval{5};

std::optional<Sha256Digest> HashFileAtCurrentModulePath() {
    wchar_t modulePath[MAX_PATH * 4];
    if (GetModuleFileNameW(nullptr, modulePath, static_cast<DWORD>(std::size(modulePath))) == 0) {
        return std::nullopt;
    }

    HANDLE file = CreateFileW(modulePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        CloseHandle(file);
        return std::nullopt;
    }

    BCRYPT_HASH_HANDLE hash = nullptr;
    std::optional<Sha256Digest> result;
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
        std::vector<uint8_t> buffer(1 << 16);
        DWORD bytesRead = 0;
        bool readError = false;
        while (ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) && bytesRead > 0) {
            if (BCryptHashData(hash, buffer.data(), bytesRead, 0) != 0) {
                readError = true;
                break;
            }
        }

        if (!readError) {
            Sha256Digest digest{};
            if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) == 0) {
                result = digest;
            }
        }
        BCryptDestroyHash(hash);
    }

    BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    return result;
}

std::atomic<bool> g_haveLoadedHash{false};
Sha256Digest g_loadedHash{};
std::atomic<bool> g_monitorRunning{false};
std::thread g_monitorThread;
std::mutex g_monitorMutex;
std::condition_variable g_monitorCv;

// Not a real NTSTATUS (pulling in <ntstatus.h> just for one code isn't
// worth the WIN32_NO_STATUS header dance) -- just a distinctive nonzero
// exit code the failure-actions/event log path treats as "abnormal".
constexpr UINT kStaleBinaryExitCode = 0xDEADDEADu;

void TerminateAbnormally() {
    // Deliberately abnormal (not a clean SERVICE_STOP) so SCM's configured
    // failure actions (task 3.10) restart the service with the new
    // on-disk binary -- no client-invoked SERVICE_STOP is involved (spec
    // "Self-Directed Staleness Recovery").
    TerminateProcess(GetCurrentProcess(), kStaleBinaryExitCode);
}

} // namespace

void CaptureLoadedBinaryHash() {
    auto digest = HashFileAtCurrentModulePath();
    if (digest) {
        g_loadedHash = *digest;
        g_haveLoadedHash = true;
    }
}

void CheckStalenessNow() {
    if (!g_haveLoadedHash) {
        return; // nothing captured yet to compare against
    }
    auto currentDigest = HashFileAtCurrentModulePath();
    if (currentDigest && *currentDigest != g_loadedHash) {
        TerminateAbnormally();
    }
}

void StartStalenessMonitor() {
    g_monitorRunning = true;
    g_monitorThread = std::thread([] {
        std::unique_lock<std::mutex> lock(g_monitorMutex);
        while (g_monitorRunning) {
            // wait_for wakes early (and re-checks the predicate) if
            // StopStalenessMonitor notifies during the interval, so a
            // service stop isn't blocked for up to a whole check interval.
            if (g_monitorCv.wait_for(lock, kStalenessCheckInterval, [] { return !g_monitorRunning.load(); })) {
                break;
            }
            CheckStalenessNow();
        }
    });
}

void StopStalenessMonitor() {
    {
        std::lock_guard<std::mutex> lock(g_monitorMutex);
        g_monitorRunning = false;
    }
    g_monitorCv.notify_all();
    if (g_monitorThread.joinable()) {
        g_monitorThread.join();
    }
}

} // namespace ffindexsvc
