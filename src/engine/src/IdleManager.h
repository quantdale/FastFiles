#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace ffengine {

// Task 4.10: after a configurable idle period with no UI window open and
// no significant recent activity (UI requests or filesystem-change
// volume), voluntarily drops the privileged connection; re-establishes on
// the next UI launch or activity burst (spec Risks: "Standing exposure
// window").
class IdleManager {
public:
    ~IdleManager();

    // onIdleTimeout is called (from the background monitor thread) when
    // the idle condition is met; onReconnectNeeded is called when activity
    // resumes after an idle drop. Kept as callbacks (rather than taking a
    // PrivilegedConnection& directly) so this class doesn't need to know
    // about that type.
    void Start(std::chrono::milliseconds idleTimeout,
               std::function<void()> onIdleTimeout,
               std::function<void()> onReconnectNeeded);
    void Stop();

    // Any of: a UI request arrived, a watched directory changed, or a UI
    // window opened -- all reset the idle clock.
    void NotifyActivity();

    void NotifyUiWindowOpen(bool anyWindowOpen);

private:
    void MonitorLoop();

    std::function<void()> onIdleTimeout_;
    std::function<void()> onReconnectNeeded_;
    std::chrono::milliseconds idleTimeout_{0};

    std::mutex mutex_;
    std::condition_variable cv_;
    std::chrono::steady_clock::time_point lastActivity_;
    bool anyWindowOpen_ = false;
    bool droppedForIdle_ = false;
    bool running_ = false;
    std::thread thread_;
};

} // namespace ffengine
