#include "IdleManager.h"

namespace ffengine {

namespace {
constexpr std::chrono::milliseconds kCheckInterval{15000};
}

IdleManager::~IdleManager() {
    Stop();
}

void IdleManager::Start(std::chrono::milliseconds idleTimeout,
                         std::function<void()> onIdleTimeout,
                         std::function<void()> onReconnectNeeded) {
    idleTimeout_ = idleTimeout;
    onIdleTimeout_ = std::move(onIdleTimeout);
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
        if (droppedForIdle_) {
            droppedForIdle_ = false;
            shouldReconnect = true;
        }
    }
    if (shouldReconnect && onReconnectNeeded_) {
        onReconnectNeeded_();
    }
}

void IdleManager::NotifyUiWindowOpen(bool anyWindowOpen) {
    bool shouldReconnect = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        anyWindowOpen_ = anyWindowOpen;
        if (anyWindowOpen) {
            lastActivity_ = std::chrono::steady_clock::now();
            if (droppedForIdle_) {
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
        if (!anyWindowOpen_ && !droppedForIdle_ && idleFor >= idleTimeout_) {
            droppedForIdle_ = true;
            lock.unlock();
            if (onIdleTimeout_) {
                onIdleTimeout_();
            }
            lock.lock();
        }
    }
}

} // namespace ffengine
