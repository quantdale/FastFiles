#pragma once
#include <windows.h>
#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace ffipc {

// Owns one named pipe's accept loop: creates a fresh pipe instance for
// each connection (FILE_FLAG_FIRST_PIPE_INSTANCE only on the very first
// instance, so a name already squatted by another process fails loudly at
// startup -- indexsvc task 3.2, spec "Pipe-name collision fails loudly"),
// and invokes `onConnect` on a dedicated thread per accepted client.
//
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
    HANDLE CreateInstance(bool isFirstInstance) noexcept;

    std::wstring pipeName_;
    const SECURITY_ATTRIBUTES* securityAttributes_ = nullptr;

    std::function<void(HANDLE)> onConnect_;
    std::thread acceptThread_;
    std::atomic<bool> running_{false};
};

} // namespace ffipc
