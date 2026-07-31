#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "ffipc/PipeFraming.h"

namespace ffengine {

// Task 4.2: the engine's connection to FastFilesIndexSvc as an explicit
// state machine. Any failure at any stage -- including a live disconnect
// from Active -- transitions back to Disconnected (spec "Connection
// failure returns to Disconnected").
enum class ConnectionState {
    Disconnected,
    Connecting,
    Handshaking,
    Active,
};

// Why the privileged path is currently unreachable, surfaced to the UI
// status badge (task 4.9, 5.9).
enum class UnavailableReason {
    None, // Active, or not yet determined
    ServiceNotRunning,
    UnverifiedServiceIdentity,
    HandshakeRejected,
    IncompatibleProtocolVersion,
    IdleDropped,
};

class PrivilegedConnection {
public:
    using StateChangeCallback = std::function<void(ConnectionState, UnavailableReason)>;

    ~PrivilegedConnection();

    // installDir: the ACL-locked directory FastFilesIndexSvc.exe is
    // expected to live in, for the engine-side identity check (task 4.4).
    void Start(std::wstring installDir, StateChangeCallback onStateChange);
    void Stop();

    ConnectionState CurrentState() const noexcept { return state_.load(); }

    // Task 4.10: voluntarily drop an Active connection when idle. The
    // background loop will not attempt to reconnect again until
    // RequestReconnect is called.
    void DropForIdle();
    // Wakes the background loop immediately (skipping any remaining
    // backoff delay) to attempt reconnection -- called on UI launch or an
    // activity burst after an idle drop (task 4.10).
    void RequestReconnect();

private:
    void RunLoop();
    void SetState(ConnectionState state, UnavailableReason reason);

    // Returns a live, verified, handshaken pipe handle, or nullptr on any
    // failure (having already set state/reason appropriately).
    HANDLE ConnectVerifyAndHandshake();
    bool VerifyServiceIdentity(HANDLE pipe) const;

    // Runs the Active-state heartbeat loop until failure or a stop/idle
    // request; returns only once the connection should be torn down.
    void ActiveLoop(HANDLE pipe);

    std::wstring installDir_;
    StateChangeCallback onStateChange_;

    std::atomic<ConnectionState> state_{ConnectionState::Disconnected};
    std::atomic<bool> running_{false};
    std::atomic<bool> idleDropped_{false};

    std::thread lifecycleThread_;
    std::mutex wakeMutex_;
    std::condition_variable wakeCv_;
    bool wakeRequested_ = false;
};

} // namespace ffengine
