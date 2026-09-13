#include "IdleManager.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "../TestSupport.h"

using namespace fftest;
using namespace std::chrono_literals;

namespace {

bool SpinUntil(const std::atomic<int>& value, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (value.load() > 0) return true;
        std::this_thread::sleep_for(1ms);
    }
    return value.load() > 0;
}

bool SpinUntil(const std::atomic<bool>& flag, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (flag.load()) return true;
        std::this_thread::sleep_for(1ms);
    }
    return flag.load();
}

} // namespace

int main() {
    using namespace ffengine;

    // Quiet idle still runs maintenance and the drop, and activity after the
    // completed drop requests reconnection (D2).
    {
        IdleManager manager;
        std::atomic<int> maintenanceCount{0};
        std::atomic<int> dropCount{0};
        std::atomic<int> reconnectCount{0};
        manager.Start(150ms,
            [&] { ++maintenanceCount; },
            [&] { ++dropCount; },
            [&] { ++reconnectCount; });
        Check(SpinUntil(dropCount, 5s), "quiet idle did not invoke the drop callback");
        Check(maintenanceCount.load() >= 1, "quiet idle did not run the maintenance callback");
        manager.NotifyActivity();
        Check(SpinUntil(reconnectCount, 5s),
              "activity after a completed idle drop did not request reconnect");
        manager.Stop();
    }

    // Activity during the unlocked maintenance callback cancels the drop; the
    // connection is never left dropped with no reconnect (D1). The callback
    // blocks until the test releases it so the race window is deterministic.
    {
        IdleManager manager;
        std::atomic<bool> maintenanceEntered{false};
        std::atomic<bool> releaseMaintenance{false};
        std::atomic<int> dropCount{0};
        std::atomic<int> reconnectCount{0};
        manager.Start(150ms,
            [&] {
                maintenanceEntered = true;
                while (!releaseMaintenance.load()) std::this_thread::sleep_for(1ms);
            },
            [&] { ++dropCount; },
            [&] { ++reconnectCount; });
        Check(SpinUntil(maintenanceEntered, 5s), "idle maintenance did not start");
        manager.NotifyActivity();
        releaseMaintenance = true;
        std::this_thread::sleep_for(50ms);
        Check(dropCount.load() == 0, "activity during the idle callback did not cancel the drop");
        Check(reconnectCount.load() >= 1, "activity during the idle callback did not request reconnect");
        manager.Stop();
    }

    // Activity while the drop callback itself is in flight defers the
    // reconnect until after the drop returns, so a reconnect can never be
    // undone by a drop that lands afterwards (D1).
    {
        IdleManager manager;
        std::atomic<bool> dropEntered{false};
        std::atomic<bool> releaseDrop{false};
        std::atomic<int> dropCount{0};
        std::atomic<int> reconnectCount{0};
        manager.Start(150ms,
            [&] {},
            [&] {
                ++dropCount;
                dropEntered = true;
                while (!releaseDrop.load()) std::this_thread::sleep_for(1ms);
            },
            [&] { ++reconnectCount; });
        Check(SpinUntil(dropEntered, 5s), "idle drop did not start");
        manager.NotifyActivity();
        std::this_thread::sleep_for(100ms);
        Check(reconnectCount.load() == 0, "reconnect ran before the in-flight drop completed");
        releaseDrop = true;
        Check(SpinUntil(reconnectCount, 5s),
              "activity during the in-flight drop did not reconnect after the drop");
        manager.Stop();
    }

    return FailureCount();
}
