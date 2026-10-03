#include "SnapshotPublisher.h"

#include <chrono>
#include <cstdio>
#include <iterator>
#include <limits>
#include <objbase.h>

#include "ffsetup/Identifiers.h"

namespace ffengine {

namespace {

void CommitGeneration(uint64_t* generation, uint64_t value) {
    MemoryBarrier();
    InterlockedExchange64(reinterpret_cast<volatile LONG64*>(generation), static_cast<LONG64>(value));
}

std::wstring GenerationSectionName(const std::wstring& prefix, uint64_t generation) {
    return prefix + L"." + std::to_wstring(generation);
}

#ifdef FF_SNAPSHOT_PUBLISHER_TESTING
// fix-initial-snapshot-handshake failure-injection hooks; see
// SnapshotPublisher.h. Installed only by test targets that compile this
// translation unit with FF_SNAPSHOT_PUBLISHER_TESTING defined.
SnapshotPublisher::TestCreateFileMapping gCreateFileMappingHook;
SnapshotPublisher::TestMapViewOfFile gMapViewOfFileHook;
SnapshotPublisher::TestWriteSnapshot gWriteSnapshotHook;
#endif

} // namespace

#ifdef FF_SNAPSHOT_PUBLISHER_TESTING
void SnapshotPublisher::SetCreateFileMappingHook(TestCreateFileMapping hook) {
    gCreateFileMappingHook = std::move(hook);
}

void SnapshotPublisher::SetMapViewOfFileHook(TestMapViewOfFile hook) {
    gMapViewOfFileHook = std::move(hook);
}

void SnapshotPublisher::SetWriteSnapshotHook(TestWriteSnapshot hook) {
    gWriteSnapshotHook = std::move(hook);
}
#endif

SnapshotPublisher::~SnapshotPublisher() {
    Stop();
}

bool SnapshotPublisher::Start(DWORD sessionId) {
    Stop();

    wchar_t sessionBuffer[128]{};
    if (swprintf_s(sessionBuffer, std::size(sessionBuffer), ffsetup::kSnapshotSectionNameFormat, sessionId) < 0) {
        return false;
    }

    GUID instanceId{};
    if (FAILED(CoCreateGuid(&instanceId))) {
        return false;
    }
    wchar_t instanceBuffer[64]{};
    if (StringFromGUID2(instanceId, instanceBuffer, static_cast<int>(std::size(instanceBuffer))) == 0) {
        return false;
    }

    sectionPrefix_ = std::wstring(sessionBuffer) + L"." + instanceBuffer;
    generation_.store(0);

    // fix-initial-snapshot-handshake: a successful Start must immediately
    // expose a usable snapshot generation, because UiServer exposes the
    // control pipe right after this returns and a fresh UI subscribes before
    // any directory has been requested or any durable index rebuild has
    // published. Generation 0 stays the "no committed generation" sentinel;
    // the first committed generation is a canonical empty snapshot -- zero
    // directories in the ordinary serialized format (the directory count
    // field alone, a non-empty 4-byte payload) -- so every reader parses it
    // with the production parser and no special empty wire form exists.
    const std::map<std::wstring, ffprotocol::SnapshotDirectory> emptySnapshot;
    const size_t initialPayloadSize = ffprotocol::SerializedSnapshotSize(emptySnapshot);
    const SnapshotWriter initialWriter = [&emptySnapshot](uint8_t* destination, size_t capacity) {
        return ffprotocol::SerializeSnapshotTo(emptySnapshot, destination, capacity);
    };

    HANDLE initialMapping = nullptr;
    uint8_t* initialView = nullptr;
    if (!CreateGenerationMapping(kFirstGeneration, initialPayloadSize, initialWriter,
                                 initialMapping, initialView, nullptr)) {
        sectionPrefix_.clear();
        generation_.store(0);
        return false;
    }
    mapping_ = initialMapping;
    view_ = initialView;
    generation_.store(kFirstGeneration);
    return true;
}

void SnapshotPublisher::Stop() {
    if (view_ != nullptr) {
        UnmapViewOfFile(view_);
        view_ = nullptr;
    }
    if (mapping_ != nullptr) {
        CloseHandle(mapping_);
        mapping_ = nullptr;
    }
    sectionPrefix_.clear();
    generation_.store(0);
}

std::wstring SnapshotPublisher::CurrentSectionName() const {
    if (sectionPrefix_.empty()) return {};
    return GenerationSectionName(sectionPrefix_, generation_.load());
}

bool SnapshotPublisher::CreateGenerationMapping(uint64_t generation,
                                                size_t payloadSize,
                                                const SnapshotWriter& writer,
                                                HANDLE& mapping, uint8_t*& view,
                                                double* serializationMs) noexcept {
    mapping = nullptr;
    view = nullptr;
    if (serializationMs != nullptr) {
        *serializationMs = 0.0;
    }
    if (payloadSize > ffprotocol::kSnapshotMaximumPayloadBytes || sectionPrefix_.empty()) {
        return false;
    }

    const uint64_t totalSize = sizeof(ffprotocol::SnapshotSharedHeader) +
        static_cast<uint64_t>(payloadSize);
    if (totalSize > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        return false;
    }
    const std::wstring name = GenerationSectionName(sectionPrefix_, generation);
    SetLastError(ERROR_SUCCESS);
    HANDLE candidate = nullptr;
#ifdef FF_SNAPSHOT_PUBLISHER_TESTING
    if (gCreateFileMappingHook) {
        candidate = gCreateFileMappingHook(name);
    } else
#endif
    {
        candidate = CreateFileMappingW(
            INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
            static_cast<DWORD>(totalSize >> 32), static_cast<DWORD>(totalSize & 0xFFFFFFFFu), name.c_str());
    }
    if (candidate == nullptr) {
        return false;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(candidate);
        return false;
    }

    uint8_t* candidateView = nullptr;
#ifdef FF_SNAPSHOT_PUBLISHER_TESTING
    if (gMapViewOfFileHook) {
        candidateView = gMapViewOfFileHook(candidate);
    } else
#endif
    {
        candidateView = static_cast<uint8_t*>(MapViewOfFile(
            candidate, FILE_MAP_ALL_ACCESS, 0, 0, static_cast<SIZE_T>(totalSize)));
    }
    if (candidateView == nullptr) {
        CloseHandle(candidate);
        return false;
    }

    auto* header = reinterpret_cast<ffprotocol::SnapshotSharedHeader*>(candidateView);
    header->formatVersion = ffprotocol::kSnapshotFormatVersion;
    header->reserved = 0;
    header->payloadSizeBytes = static_cast<uint64_t>(payloadSize);
    header->generation = 0;
    if (writer) {
        const auto serializationStart = std::chrono::steady_clock::now();
        bool wrote = false;
#ifdef FF_SNAPSHOT_PUBLISHER_TESTING
        if (gWriteSnapshotHook) {
            wrote = gWriteSnapshotHook(candidateView + sizeof(*header), payloadSize);
        } else
#endif
        {
            wrote = writer(candidateView + sizeof(*header), payloadSize);
        }
        if (!wrote) {
            UnmapViewOfFile(candidateView);
            CloseHandle(candidate);
            return false;
        }
        if (serializationMs != nullptr) {
            *serializationMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - serializationStart).count();
        }
    }
    // The generation is the final commit marker. The notification is sent
    // only after this function returns, so a reader never opens a partially
    // initialized immutable generation through the normal handoff.
    CommitGeneration(&header->generation, generation);

    mapping = candidate;
    view = candidateView;
    return true;
}

uint64_t SnapshotPublisher::Publish(
    const std::map<std::wstring, ffprotocol::SnapshotDirectory>& directories,
    PublishTimings* timings) {
    const size_t payloadSize = ffprotocol::SerializedSnapshotSize(directories);
    const SnapshotWriter writer = [&directories](uint8_t* destination, size_t capacity) {
        return ffprotocol::SerializeSnapshotTo(directories, destination, capacity);
    };
    return PublishPayload(payloadSize, writer, timings);
}

uint64_t SnapshotPublisher::Publish(const ffprotocol::CaseInsensitiveSnapshotMap& directories,
                                     PublishTimings* timings) {
    const size_t payloadSize = ffprotocol::SerializedSnapshotSize(directories);
    const SnapshotWriter writer = [&directories](uint8_t* destination, size_t capacity) {
        return ffprotocol::SerializeSnapshotTo(directories, destination, capacity);
    };
    return PublishPayload(payloadSize, writer, timings);
}

uint64_t SnapshotPublisher::Publish(const ffprotocol::PooledCaseInsensitiveSnapshotMap& directories,
                                     PublishTimings* timings) {
    const size_t payloadSize = ffprotocol::SerializedSnapshotSize(directories);
    const SnapshotWriter writer = [&directories](uint8_t* destination, size_t capacity) {
        return ffprotocol::SerializeSnapshotTo(directories, destination, capacity);
    };
    return PublishPayload(payloadSize, writer, timings);
}

uint64_t SnapshotPublisher::Publish(const ffprotocol::PooledSnapshotMap& directories,
                                     PublishTimings* timings) {
    const size_t payloadSize = ffprotocol::SerializedSnapshotSize(directories);
    const SnapshotWriter writer = [&directories](uint8_t* destination, size_t capacity) {
        return ffprotocol::SerializeSnapshotTo(directories, destination, capacity);
    };
    return PublishPayload(payloadSize, writer, timings);
}

uint64_t SnapshotPublisher::PublishPayload(size_t payloadSize, const SnapshotWriter& writer,
                                           PublishTimings* timings) {
    using Clock = std::chrono::steady_clock;
    std::lock_guard<std::mutex> lock(publishMutex_);
    if (timings != nullptr) {
        timings->serializationMs = 0.0;
        timings->copyAndCommitMs = 0.0;
        timings->serializedBytes = payloadSize;
    }
    if (view_ == nullptr || payloadSize == 0
        || payloadSize > ffprotocol::kSnapshotMaximumPayloadBytes) {
        return 0;
    }

    const auto operationStart = timings != nullptr ? Clock::now() : Clock::time_point{};
    const uint64_t newGeneration = generation_.load() + 1;
    HANDLE nextMapping = nullptr;
    uint8_t* nextView = nullptr;
    double serializationMs = 0.0;
    if (!CreateGenerationMapping(newGeneration, payloadSize, writer, nextMapping, nextView,
                                 timings != nullptr ? &serializationMs : nullptr)) {
        return 0;
    }

    HANDLE previousMapping = mapping_;
    uint8_t* previousView = view_;
    mapping_ = nextMapping;
    view_ = nextView;
    generation_.store(newGeneration);

    if (previousView != nullptr) UnmapViewOfFile(previousView);
    if (previousMapping != nullptr) CloseHandle(previousMapping);

    if (timings != nullptr) {
        timings->serializationMs = serializationMs;
        const double totalMs = std::chrono::duration<double, std::milli>(
            Clock::now() - operationStart).count();
        timings->copyAndCommitMs = totalMs > serializationMs ? totalMs - serializationMs : 0.0;
    }
    return newGeneration;
}

} // namespace ffengine
