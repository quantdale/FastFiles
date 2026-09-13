#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace ffengine {

// Task 4.10: after a configurable idle period with no significant recent
// activity (UI requests or filesystem-change volume), voluntarily drops the
// privileged connection; re-establishes on the next UI launch or activity
// burst (spec Risks: "Standing exposure window").
//
// close-idle-drop-activity-race D1: the idle callback runs outside the mutex
// (store maintenance can take seconds), so activity can arrive while it is in
// flight. The manager tracks an activity generation and the drop-in-flight
// state: a reconnect decided while the drop callback is running is published
// after the drop completes, and a reconnect decided during maintenance
// cancels the drop entirely. The privileged path must never be left dropped
// with no subsequent reconnect.
class IdleManager {
public:
    ~IdleManager();

    // onIdleTimeout is called (from the background monitor thread) when the
    // idle condition is met; it runs outside the manager mutex and must not
    // call back into IdleManager. onIdleDrop performs the actual privileged
    // connection drop and is invoked only when no activity raced the
    // maintenance callback; onReconnectNeeded is called when activity resumes
    // while the connection is dropped (or while a drop is completing). Kept as
    // callbacks (rather than taking a PrivilegedConnection& directly) so this
    // class doesn't need to know about that type.
    void Start(std::chrono::milliseconds idleTimeout,
               std::function<void()> onIdleTimeout,
               std::function<void()> onIdleDrop,
               std::function<void()> onReconnectNeeded);
    void Stop();

    // Any of: a UI request arrived or a watched directory changed -- both
    // reset the idle clock.
    void NotifyActivity();

private:
    void MonitorLoop();

    std::function<void()> onIdleTimeout_;
    std::function<void()> onIdleDrop_;
    std::function<void()> onReconnectNeeded_;
    std::chrono::milliseconds idleTimeout_{0};

    std::mutex mutex_;
    std::condition_variable cv_;
    std::chrono::steady_clock::time_point lastActivity_;
    bool droppedForIdle_ = false;
    // Set while onIdleDrop_ is executing so activity cannot clear the dropped
    // state and reconnect before the drop lands (D1).
    bool dropInFlight_ = false;
    // Activity arrived while the drop was in flight: publish the reconnect
    // after the drop returns.
    bool reconnectPending_ = false;
    // Bumped by NotifyActivity; an idle epoch whose generation changed during
    // the maintenance callback must not drop (D1).
    unsigned long long activityGeneration_ = 0;
    bool running_ = false;
    std::thread thread_;
};

} // namespace ffengine
