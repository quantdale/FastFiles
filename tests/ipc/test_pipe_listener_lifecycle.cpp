#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "ffipc/PipeListener.h"
#include "ffipc/PipeFraming.h"
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

// Audit regression (engine.md P3): finished per-connection threads must be
// reaped by the accept loop instead of accumulating until Stop(). Sequential
// connect/disconnect churn must keep the retained-thread registry bounded.
void TestFinishedConnectionThreadsAreReaped() {
    constexpr uint32_t kIterations = 24;
    const std::wstring pipeName = L"\\\\.\\pipe\\FastFiles.ListenerReap." + std::to_wstring(GetCurrentProcessId());
    ffipc::PipeListener listener;
    const bool started = listener.Start(pipeName, nullptr, [](HANDLE pipe) {
        if (pipe != INVALID_HANDLE_VALUE && pipe != nullptr) CloseHandle(pipe);
    });
    Check(started, "reap listener starts");
    if (!started) return;

    size_t maxObserved = 0;
    for (uint32_t iteration = 0; iteration < kIterations; ++iteration) {
        bool connected = false;
        for (int attempt = 0; attempt < 50 && !connected; ++attempt) {
            HANDLE client = CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                        OPEN_EXISTING, 0, nullptr);
            if (client != INVALID_HANDLE_VALUE) {
                CloseHandle(client);
                connected = true;
                break;
            }
            Sleep(2);
        }
        Check(connected, "reap iteration connects a client");
        if (!connected) break;
        // Poll briefly for the connection callback to finish and the accept
        // loop to reap it; the registry must stay bounded either way.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline) {
            maxObserved = std::max(maxObserved, listener.ConnectionThreadCountForTesting());
            if (listener.ConnectionThreadCountForTesting() == 0) break;
            Sleep(2);
        }
    }
    Check(maxObserved <= 2,
          "retained connection threads stay bounded under sequential churn");
    listener.Stop();
}

// Shutdown-termination regression: PipeListener::Stop() must return promptly
// when a connection callback's reader is BETWEEN frames rather than blocked in
// a read.
//
// CancelIoEx only cancels *pending* I/O. A reader that is busy in a dispatch
// step -- the service control loop runs durable scan/journal work here -- has
// no overlapped read outstanding, so the cancel is a no-op. Without a
// listener-owned shutdown event the reader then re-arms a fresh deadline read
// and Stop() blocks at its join barrier for that entire deadline (300 s for
// the service's kInboundIdleTimeout). Mirrors the EngineClient/UiServer
// shutdown-termination regressions.
void TestStopReleasesReaderParkedBetweenFrames() {
    static constexpr auto kReadDeadline = std::chrono::seconds(15);
    static constexpr auto kMaxStopDuration = std::chrono::seconds(5);
    static constexpr auto kBusyDispatchSlice = std::chrono::milliseconds(500);

    ffipc::PipeListener listener;
    const std::wstring pipeName =
        L"\\\\.\\pipe\\FastFiles.ListenerShutdownWake." + std::to_wstring(GetCurrentProcessId());
    std::atomic<bool> dispatchStarted{false};
    std::atomic<bool> readerObservedCancelled{false};

    const bool started = listener.Start(
        pipeName, nullptr,
        [&listener, &dispatchStarted, &readerObservedCancelled](HANDLE pipe) {
            dispatchStarted.store(true, std::memory_order_release);
            // Stand-in for the control loop's durable dispatch step: the reader
            // is busy and holds NO overlapped read, which is precisely the
            // window CancelIoEx cannot cover. Stop() runs during this slice.
            std::this_thread::sleep_for(kBusyDispatchSlice);
            ffipc::ReceivedFrame frame{};
            const ffipc::FrameReadStatus status = ffipc::ReadFrameOverlappedDetailed(
                pipe, listener.ShutdownEvent(), std::chrono::steady_clock::now() + kReadDeadline, &frame);
            readerObservedCancelled.store(status == ffipc::FrameReadStatus::Cancelled,
                                          std::memory_order_release);
            if (pipe != INVALID_HANDLE_VALUE && pipe != nullptr) {
                CloseHandle(pipe);
            }
        });
    Check(started, "shutdown-wake listener starts");
    if (!started) return;

    // Hold the client open and send nothing, so the peer is idle but connected.
    HANDLE client = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 50 && client == INVALID_HANDLE_VALUE; ++attempt) {
        client = CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (client == INVALID_HANDLE_VALUE) Sleep(2);
    }
    Check(client != INVALID_HANDLE_VALUE, "shutdown-wake client connects");

    // Enter Stop() while the reader is mid-dispatch, with no I/O pending.
    for (int attempt = 0; attempt < 500 && !dispatchStarted.load(std::memory_order_acquire); ++attempt) {
        Sleep(2);
    }
    Check(dispatchStarted.load(std::memory_order_acquire), "shutdown-wake dispatch step entered");

    const auto stopStart = std::chrono::steady_clock::now();
    listener.Stop();
    const auto stopElapsed = std::chrono::steady_clock::now() - stopStart;

    Check(stopElapsed < kMaxStopDuration,
          "listener Stop() releases a reader parked between frames promptly");
    Check(readerObservedCancelled.load(std::memory_order_acquire),
          "reader re-armed after dispatch observes Cancelled on listener shutdown");

    if (client != INVALID_HANDLE_VALUE) CloseHandle(client);
}

} // namespace

int main() {
    TestImmediateStopDuringPendingAccept();
    TestConcurrentListenerLifecycle();
    TestFinishedConnectionThreadsAreReaped();
    TestStopReleasesReaderParkedBetweenFrames();
    return FailureCount() == 0 ? 0 : 1;
}
