#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "ffipc/PipeFraming.h"
#include "ffprotocol/Commands.h"
#include "ffprotocol/Records.h"

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
    using VolumeListCallback = std::function<void(std::vector<ffprotocol::VolumeInfo>)>;
    using ScanBatchCallback = std::function<void(
        ffprotocol::VolumeId, std::vector<uint8_t> resumeCursor, std::vector<ffprotocol::MftRecordV1> records)>;
    // allocated-size-storage-accuracy 5.1: V2 scan records (additive
    // allocated size) from peers that negotiated protocol {2,1}+.
    using ScanBatchV2Callback = std::function<void(
        ffprotocol::VolumeId, std::vector<uint8_t> resumeCursor, std::vector<ffprotocol::MftRecordV2> records)>;
    using ScanCompleteCallback = std::function<void(ffprotocol::VolumeId)>;
    // index-durability-false-success-closure D1: terminal failure counterpart,
    // sent by a {2,2}+ service instead of ScanComplete for scans that could
    // not run to completion.
    using ScanFailedCallback = std::function<void(ffprotocol::VolumeId)>;
    using JournalOpenedCallback = std::function<void(ffprotocol::VolumeId, uint64_t journalId, uint64_t currentUsn)>;
    using JournalBatchCallback =
        std::function<void(ffprotocol::VolumeId, uint64_t latestUsn, std::vector<ffprotocol::UsnDeltaV2> records)>;
    // allocated-size-storage-accuracy 5.1: V3 journal records (additive
    // allocated-size presence bit).
    using JournalBatchV3Callback =
        std::function<void(ffprotocol::VolumeId, uint64_t latestUsn, std::vector<ffprotocol::UsnDeltaV3> records)>;
    // The service rejected the requested resume position as outside the
    // journal's retained range (MessageType::JournalResumeInvalid) -- the
    // payload carries no position, only the volume it concerns.
    using JournalResumeInvalidCallback = std::function<void(ffprotocol::VolumeId)>;

    ~PrivilegedConnection();

    // index-storage-and-scanning: callbacks for the asynchronously
    // streamed batch/notification message types (tasks.md section 6's
    // engine-side consumer). Must be set before Start() -- the Active
    // connection's reader thread dispatches to whatever is registered at
    // the time a frame arrives, and these are not expected to change
    // after the connection is up.
    void SetVolumeListCallback(VolumeListCallback callback) { volumeListCallback_ = std::move(callback); }
    void SetScanBatchCallback(ScanBatchCallback callback) { scanBatchCallback_ = std::move(callback); }
    void SetScanBatchV2Callback(ScanBatchV2Callback callback) { scanBatchV2Callback_ = std::move(callback); }
    void SetScanCompleteCallback(ScanCompleteCallback callback) { scanCompleteCallback_ = std::move(callback); }
    void SetScanFailedCallback(ScanFailedCallback callback) { scanFailedCallback_ = std::move(callback); }
    void SetJournalOpenedCallback(JournalOpenedCallback callback) { journalOpenedCallback_ = std::move(callback); }
    void SetJournalBatchCallback(JournalBatchCallback callback) { journalBatchCallback_ = std::move(callback); }
    void SetJournalBatchV3Callback(JournalBatchV3Callback callback) { journalBatchV3Callback_ = std::move(callback); }
    void SetJournalResumeInvalidCallback(JournalResumeInvalidCallback callback) {
        journalResumeInvalidCallback_ = std::move(callback);
    }

    // installDir: the ACL-locked directory FastFilesIndexSvc.exe is
    // expected to live in, for the engine-side identity check (task 4.4).
    void Start(std::wstring installDir, StateChangeCallback onStateChange);
    void Stop();

    ConnectionState CurrentState() const noexcept { return state_.load(); }
    ffprotocol::ProtocolVersion NegotiatedVersion() const noexcept {
        return {negotiatedMajor_.load(), negotiatedMinor_.load()};
    }

    // Sends a request frame on the current Active connection, thread-safe
    // against the connection's own Heartbeat writes and any other caller
    // of SendRequest (e.g. a scan request racing a journal-open request
    // issued from a different volume's handling code). Returns false
    // (without blocking) if the connection is not currently Active.
    bool SendRequest(uint16_t messageType, const void* payload = nullptr, uint32_t payloadSize = 0);

    // Task 4.10: voluntarily drop an Active connection when idle. The
    // background loop will not attempt to reconnect again until
    // RequestReconnect is called.
    void DropForIdle();
    // Wakes the background loop immediately (skipping any remaining
    // backoff delay) to attempt reconnection -- called on UI launch or an
    // activity burst after an idle drop (task 4.10).
    void RequestReconnect();

private:
    struct OwnedPipe {
        explicit OwnedPipe(HANDLE value) noexcept : handle(value) {}
        ~OwnedPipe();

        OwnedPipe(const OwnedPipe&) = delete;
        OwnedPipe& operator=(const OwnedPipe&) = delete;

        HANDLE handle = INVALID_HANDLE_VALUE;
    };
    using PipePtr = std::shared_ptr<OwnedPipe>;

    void RunLoop();
    void SetState(ConnectionState state, UnavailableReason reason);
    // Wakes the lifecycle loop (clearing backoff/park) without touching
    // idleDropped_ -- used by both DropForIdle and RequestReconnect.
    void WakeLoop();

    // Returns a live, verified, handshaken pipe handle, or nullptr on any
    // failure (having already set state/reason appropriately).
    PipePtr ConnectVerifyAndHandshake();
    bool VerifyServiceIdentity(HANDLE pipe) const;

    // Runs the Active-state loop until failure or a stop/idle request;
    // returns only once the connection should be torn down. Owns a
    // reader thread (dispatching HeartbeatAck plus every scan/journal
    // message type to the registered callbacks) running concurrently
    // with this thread's own Heartbeat-send/backoff cadence.
    void ActiveLoop(const PipePtr& pipe);
    void DispatchReceivedFrame(const ffipc::ReceivedFrame& frame, std::atomic<bool>& heartbeatAckPending,
                                std::mutex& ackMutex, std::condition_variable& ackCv, std::atomic<bool>& protocolViolation);

    std::wstring installDir_;
    StateChangeCallback onStateChange_;
    VolumeListCallback volumeListCallback_;
    ScanBatchCallback scanBatchCallback_;
    ScanBatchV2Callback scanBatchV2Callback_;
    ScanCompleteCallback scanCompleteCallback_;
    ScanFailedCallback scanFailedCallback_;
    JournalOpenedCallback journalOpenedCallback_;
    JournalBatchCallback journalBatchCallback_;
    JournalBatchV3Callback journalBatchV3Callback_;
    JournalResumeInvalidCallback journalResumeInvalidCallback_;

    std::atomic<ConnectionState> state_{ConnectionState::Disconnected};
    std::atomic<bool> running_{false};
    std::atomic<bool> idleDropped_{false};
    std::atomic<uint16_t> negotiatedMajor_{0};
    std::atomic<uint16_t> negotiatedMinor_{0};

    // Guards every WriteFrame call on activePipe_ -- ActiveLoop's own
    // Heartbeat and any thread calling SendRequest (e.g. the volume
    // session orchestrator) both write to the same handle.
    std::mutex writeMutex_;
    std::mutex pipeStateMutex_;
    PipePtr activePipe_;
    PipePtr connectingPipe_;

    // Manual-reset, signaled by Stop()/DropForIdle() (and by ActiveLoop's own
    // teardown) so the Active reader's INFINITE blocking read is woken
    // deterministically. CancelIoEx alone is a no-op when the reader is
    // between frames -- e.g. inside a durable scan/journal batch callback --
    // which previously let it re-arm an unbounded read and deadlock the
    // subsequent reader.join(). Mirrors EngineClient::ReaderLoop and
    // UiServer::HandleConnection.
    HANDLE shutdownEvent_ = nullptr;

    std::thread lifecycleThread_;
    std::mutex wakeMutex_;
    std::condition_variable wakeCv_;
    bool wakeRequested_ = false;
};

} // namespace ffengine
