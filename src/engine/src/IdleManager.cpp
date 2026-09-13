#include "IdleManager.h"

namespace ffengine {

namespace {
#ifdef FF_IDLE_MANAGER_TESTING
// The monitor cadence is compressed for the deterministic unit test; the
// production binary keeps the 15-second check interval.
constexpr std::chrono::milliseconds kCheckInterval{10};
#else
constexpr std::chrono::milliseconds kCheckInterval{15000};
#endif
}

IdleManager::~IdleManager() {
    Stop();
}

void IdleManager::Start(std::chrono::milliseconds idleTimeout,
                         std::function<void()> onIdleTimeout,
                         std::function<void()> onIdleDrop,
                         std::function<void()> onReconnectNeeded) {
    idleTimeout_ = idleTimeout;
    onIdleTimeout_ = std::move(onIdleTimeout);
    onIdleDrop_ = std::move(onIdleDrop);
    onReconnectNeeded_ = std::move(onReconnectNeeded);
    lastActivity_ = std::chrono::steady_clock::now();
    running_ = true;
    thread_ = std::thread(&IdleManager::MonitorLoop, this);
}

void IdleManager::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) {
            return;
        }
        running_ = false;
    }
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void IdleManager::NotifyActivity() {
    bool shouldReconnect = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lastActivity_ = std::chrono::steady_clock::now();
        ++activityGeneration_;
        if (droppedForIdle_) {
            if (dropInFlight_) {
                // The drop is landing right now: reconnecting before it lands
                // would be undone by the drop. Record the reconnect and let
                // MonitorLoop publish it once the drop has returned (D1).
                reconnectPending_ = true;
            } else {
                droppedForIdle_ = false;
                shouldReconnect = true;
            }
        }
    }
    if (shouldReconnect && onReconnectNeeded_) {
        onReconnectNeeded_();
    }
}

void IdleManager::MonitorLoop() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (running_) {
        cv_.wait_for(lock, kCheckInterval, [this] { return !running_; });
        if (!running_) {
            break;
        }

        const auto idleFor = std::chrono::steady_clock::now() - lastActivity_;
        // The idle condition is activity-based only. A designed "no UI window
        // open" conjunct (NotifyUiWindowOpen) was never wired to any
        // UI->engine signal, so it was removed rather than kept as dead code
        // that could never be true.
        if (!droppedForIdle_ && idleFor >= idleTimeout_) {
            droppedForIdle_ = true;
            // D1: epoch of this idle decision. Activity during the unlocked
            // maintenance callback invalidates the drop for this epoch.
            const unsigned long long epoch = activityGeneration_;
            lock.unlock();
            if (onIdleTimeout_) {
                onIdleTimeout_();
            }
            lock.lock();
            if (activityGeneration_ == epoch && droppedForIdle_) {
                // Maintenance finished without activity. Run the drop outside
                // the mutex, but keep dropInFlight_ set under it so activity
                // that lands during the drop defers its reconnect instead of
                // reconnecting before the drop.
                dropInFlight_ = true;
                lock.unlock();
                if (onIdleDrop_) {
                    onIdleDrop_();
                }
                lock.lock();
                dropInFlight_ = false;
                if (reconnectPending_) {
                    reconnectPending_ = false;
                    droppedForIdle_ = false;
                    lock.unlock();
                    if (onReconnectNeeded_) {
                        onReconnectNeeded_();
                    }
                    lock.lock();
                }
            }
        }
    }
}

} // namespace ffengine
