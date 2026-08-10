#pragma once
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <windows.h>

#include "ffprotocol/SnapshotFormat.h"

namespace ffengine {

// Task 4.7/4.8: publishes the engine's current view of enumerated
// directories into a read-only, double-buffered memory-mapped section
// (design.md D3) so a UI client can read already-published directory
// contents with zero IPC round trip. The section name is suffixed by the
// Terminal Services session ID (task 4.8) so multiple concurrently
// logged-on sessions -- each running their own FastFilesEngine -- don't
// collide.
//
// Double-buffering: Publish() writes a full new serialization into the
// currently-INACTIVE slot, then publishes the slot descriptor and selection
// with Windows interlocked stores. The interlocked operations provide the
// required cross-process ordering; a reader never observes a torn descriptor.
class SnapshotPublisher {
public:
    // Optional attribution seam for the explicit scalability benchmark. The
    // regular publisher call passes no timing object and emits no logging.
    struct PublishTimings {
        double serializationMs = 0.0;
        double copyAndCommitMs = 0.0;
        size_t serializedBytes = 0;
    };

    ~SnapshotPublisher();

    bool Start(DWORD sessionId);
    void Stop();

    // Replaces the full published view and flips to a new generation.
    // Returns the new generation ID, or 0 on failure (e.g. serialized size
    // exceeds the bounded 64 MiB slot capacity).
    uint64_t Publish(const std::map<std::wstring, ffprotocol::SnapshotDirectory>& directories,
                     PublishTimings* timings = nullptr);

    const std::wstring& SectionName() const noexcept { return sectionName_; }
    uint64_t CurrentGeneration() const noexcept { return generation_.load(); }

private:
    HANDLE mapping_ = nullptr;
    uint8_t* view_ = nullptr;
    std::wstring sectionName_;
    std::atomic<uint64_t> generation_{0};
    std::mutex publishMutex_;
};

} // namespace ffengine
