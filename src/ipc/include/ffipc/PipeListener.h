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

    std::wstring pipeName_;
    const SECURITY_ATTRIBUTES* securityAttributes_ = nullptr;
    std::function<void(HANDLE)> onConnect_;
    std::thread acceptThread_;
    std::atomic<bool> running_{false};
    std::mutex lifecycleMutex_;
    std::mutex pendingMutex_;
    OwnedHandlePtr pendingInstance_;
    std::mutex connectionMutex_;
    // Every cancellation handle in this registry has exactly one RAII owner.
    // Stop() copies the shared owners before calling CancelIoEx; the owner
    // therefore cannot close/recycle the value until cancellation and the
    // callback teardown have both completed.
    std::vector<OwnedHandlePtr> connectionHandles_;
    std::vector<std::thread> connectionThreads_;
};

} // namespace ffipc
