#pragma once
#include <windows.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ffipc {

// Owns one named pipe's accept loop: creates a fresh pipe instance for
// each connection (FILE_FLAG_FIRST_PIPE_INSTANCE only on the very first
// instance, so a name already squatted by another process fails loudly at
// startup -- indexsvc task 3.2, spec "Pipe-name collision fails loudly"),
// and invokes `onConnect` on a retained, joinable thread per accepted client.
// Stop() cancels tracked overlapped handles and joins every callback before
// returning, so an accepted connection cannot outlive the listener.
// Reused by both FastFilesIndexSvc (Ctrl/Data pipes, ACL'd to the
// authorized client group) and FastFilesEngine (its own same-privilege
// UI-facing pipe, ACL'd to the current user) -- each caller builds and
// owns its own security descriptor rather than this class assuming one
// particular policy.
class PipeListener {
public:
    ~PipeListener();

    // securityAttributes must remain valid for the listener's lifetime
    // (the caller owns it, typically an ffsetup::OwnedSecurityDescriptor).
    // Returns false (having already logged to stderr) if the very first
    // pipe instance could not be created -- most commonly because a pipe
    // of this name already exists (squatted). The caller must treat this
    // as fatal and refuse to proceed, not silently continue without the
    // expected pipe.
    bool Start(const std::wstring& pipeName, const SECURITY_ATTRIBUTES* securityAttributes,
               std::function<void(HANDLE)> onConnect);

    void Stop();

    // Manual-reset event, created by Start() and signaled by Stop() before it
    // joins connection callbacks. A connection callback whose reader is
    // blocked on an overlapped read -- or parked between frames, e.g. inside a
    // durable scan/journal step -- can wait on this to wake deterministically.
    // CancelIoEx alone is a no-op when no I/O is pending, which would otherwise
    // let the reader re-arm a fresh bounded read and stall Stop() for that
    // deadline (300 s for the service control loop). Null before Start() or if
    // event creation failed; callers must treat null as "no shutdown signal
    // available" and fall back to the plain deadline-bounded read.
    HANDLE ShutdownEvent() const noexcept { return shutdownEvent_; }

private:
    class OwnedHandle {
    public:
        explicit OwnedHandle(HANDLE value) noexcept : value_(value) {}
        ~OwnedHandle();

        OwnedHandle(const OwnedHandle&) = delete;
        OwnedHandle& operator=(const OwnedHandle&) = delete;

        HANDLE Get() const noexcept { return value_; }
        HANDLE Release() noexcept;

    private:
        HANDLE value_ = INVALID_HANDLE_VALUE;
    };

    using OwnedHandlePtr = std::shared_ptr<OwnedHandle>;

    HANDLE CreateInstance(bool isFirstInstance) noexcept;
    void JoinConnectionThreads();
    // Joins and erases connection threads whose callback has returned. Called
    // from the accept loop so finished per-connection threads do not
    // accumulate for the listener's lifetime.
    void ReapFinishedConnectionThreads();

    std::wstring pipeName_;
    const SECURITY_ATTRIBUTES* securityAttributes_ = nullptr;
    std::function<void(HANDLE)> onConnect_;
    std::thread acceptThread_;
    std::atomic<bool> running_{false};
    // Created by Start(), signaled by Stop(). Owned here for the listener's
    // whole lifetime; closed in the destructor after Stop() has joined.
    HANDLE shutdownEvent_ = nullptr;
    std::mutex lifecycleMutex_;
    std::mutex pendingMutex_;
    OwnedHandlePtr pendingInstance_;
    std::mutex connectionMutex_;
    // Every cancellation handle in this registry has exactly one RAII owner.
    // Stop() copies the shared owners before calling CancelIoEx; the owner
    // therefore cannot close/recycle the value until cancellation and the
    // callback teardown have both completed.
    std::vector<OwnedHandlePtr> connectionHandles_;
    // Each entry pairs a retained joinable thread with a flag its own lambda
    // sets when the connection callback has returned, so the accept loop can
    // reap finished entries instead of retaining them until Stop().
    struct ConnectionThread {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> finished;
    };
    std::vector<ConnectionThread> connectionThreads_;

public:
    // Test-only observation of the retained thread registry size.
#ifdef FF_PIPE_LISTENER_TESTING
    size_t ConnectionThreadCountForTesting() {
        std::lock_guard<std::mutex> lock(connectionMutex_);
        return connectionThreads_.size();
    }
#endif
};

} // namespace ffipc
