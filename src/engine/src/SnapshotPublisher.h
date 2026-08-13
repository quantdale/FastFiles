#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <windows.h>

#include "ffprotocol/SnapshotFormat.h"

namespace ffengine {

// Task 4.7/4.8: publishes the engine's current view of enumerated
// directories into immutable, generation-sized memory-mapped sections
// (design.md D3) so a UI client can read already-published directory contents
// with zero IPC round trip. The stable prefix is suffixed by the Terminal
// Services session ID and a per-engine GUID epoch so concurrently logged-on
// sessions and restarts cannot collide.
//
// Immutable generations: Publish() creates and completely initializes a new
// mapping named <prefix>.<generation>, then the UI is notified of that name's
// generation. The engine keeps only the current mapping; a UI that still holds
// the previous mapping can finish reading it safely.
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

    // Replaces the full published view with a new immutable generation.
    // Returns the new generation ID, or 0 on failure (e.g. serialized size
    // exceeds the bounded 256 MiB payload capacity).
    uint64_t Publish(const std::map<std::wstring, ffprotocol::SnapshotDirectory>& directories,
                     PublishTimings* timings = nullptr);
    uint64_t Publish(const ffprotocol::CaseInsensitiveSnapshotMap& directories,
                     PublishTimings* timings = nullptr);

    // Stable prefix sent in SubscribeAck. The generation suffix is appended
    // by CurrentSectionName() and is intentionally not part of this value.
    const std::wstring& SectionName() const noexcept { return sectionPrefix_; }
    std::wstring CurrentSectionName() const;
    uint64_t CurrentGeneration() const noexcept { return generation_.load(); }

private:
    using SnapshotWriter = std::function<bool(uint8_t* destination, size_t capacity)>;

    bool CreateGenerationMapping(uint64_t generation, size_t payloadSize,
                                 const SnapshotWriter& writer, HANDLE& mapping,
                                 uint8_t*& view, double* serializationMs) noexcept;
    uint64_t PublishPayload(size_t payloadSize, const SnapshotWriter& writer,
                            PublishTimings* timings);

    HANDLE mapping_ = nullptr;
    uint8_t* view_ = nullptr;
    std::wstring sectionPrefix_;
    std::atomic<uint64_t> generation_{0};
    std::mutex publishMutex_;
};

} // namespace ffengine
