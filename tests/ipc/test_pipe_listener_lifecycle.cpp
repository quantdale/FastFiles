#include <windows.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "ffipc/PipeListener.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {

std::wstring TestPipeName(uint32_t iteration) {
    return L"\\\\.\\pipe\\FastFiles.ListenerLifecycle." + std::to_wstring(GetCurrentProcessId()) + L"." +
           std::to_wstring(iteration);
}

std::wstring PendingStopPipeName(uint32_t iteration) {
    return L"\\\\.\\pipe\\FastFiles.ListenerPendingStop." + std::to_wstring(GetCurrentProcessId()) + L"." +
           std::to_wstring(iteration);
}

void ConnectAndDisconnect(const std::wstring& pipeName, uint32_t delayMilliseconds) {
    // Several clients race the listener's accept instance. A failed open is
    // expected when Stop wins the race; the test is checking that the
    // listener remains tear-down safe under exactly that churn.
    for (int attempt = 0; attempt < 4; ++attempt) {
        HANDLE client = CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_OVERLAPPED, nullptr);
        if (client != INVALID_HANDLE_VALUE) {
            if (delayMilliseconds != 0) Sleep(delayMilliseconds);
            CloseHandle(client);
            return;
        }
        Sleep(1);
    }
}

void TestConcurrentListenerLifecycle() {
    constexpr uint32_t kIterations = 64;
    constexpr size_t kClientsPerIteration = 12;
    std::atomic<uint32_t> callbackCount{0};

    for (uint32_t iteration = 0; iteration < kIterations; ++iteration) {
        ffipc::PipeListener listener;
        const std::wstring pipeName = TestPipeName(iteration);
        const bool started = listener.Start(pipeName, nullptr, [&callbackCount](HANDLE pipe) {
            callbackCount.fetch_add(1, std::memory_order_relaxed);
            // Keep the callback live while Stop() cancels the duplicated
            // control handle and waits at its join barrier.
            Sleep(1);
            if (pipe != INVALID_HANDLE_VALUE && pipe != nullptr) CloseHandle(pipe);
        });
        Check(started, "listener starts a unique overlapped pipe instance");
        if (!started) continue;

        std::vector<std::thread> clients;
        clients.reserve(kClientsPerIteration);
        for (size_t index = 0; index < kClientsPerIteration; ++index) {
            clients.emplace_back(ConnectAndDisconnect, pipeName,
                                 static_cast<uint32_t>((index + iteration) % 3));
        }

        // Stop is deliberately concurrent with connection callbacks and
        // pending ConnectNamedPipe operations. The implementation's shared
        // owners prove the cancellation registry cannot retain a raw HANDLE
        // after the callback has logically retired it (FF-POST-05).
        std::thread stopper([&listener, iteration] {
            Sleep((iteration % 4 == 0) ? 0 : 1);
            listener.Stop();
        });

        for (auto& client : clients) client.join();
        stopper.join();
        listener.Stop(); // idempotent second teardown exercises the barrier.
    }

    Check(callbackCount.load(std::memory_order_relaxed) > 0,
          "lifecycle stress accepted at least one connection callback");
}

void TestImmediateStopDuringPendingAccept() {
    constexpr uint32_t kIterations = 128;
    for (uint32_t iteration = 0; iteration < kIterations; ++iteration) {
        ffipc::PipeListener listener;
        const bool started = listener.Start(PendingStopPipeName(iteration), nullptr, [](HANDLE pipe) {
            if (pipe != INVALID_HANDLE_VALUE && pipe != nullptr) CloseHandle(pipe);
        });
        Check(started, "listener starts before immediate pending-accept stop");
        if (started) {
            // There is intentionally no client. Stop races the accept thread's
            // pending registration and ConnectNamedPipe initiation. The
            // listener's handshake must make this joinable without an
            // uncancelled overlapped wait (FF-POST-11).
            listener.Stop();
        }
    }
}

} // namespace

int main() {
    TestImmediateStopDuringPendingAccept();
    TestConcurrentListenerLifecycle();
    return FailureCount() == 0 ? 0 : 1;
}
