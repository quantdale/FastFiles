#include "SnapshotPublisher.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <iterator>

#include "ffsetup/Identifiers.h"

namespace ffengine {

namespace {

uint32_t LoadSharedU32(const uint32_t* value) {
    return static_cast<uint32_t>(InterlockedCompareExchange(
        reinterpret_cast<volatile LONG*>(const_cast<uint32_t*>(value)), 0, 0));
}

void StoreSharedU32(uint32_t* value, uint32_t data) {
    InterlockedExchange(reinterpret_cast<volatile LONG*>(value), static_cast<LONG>(data));
}

void StoreSharedU64(uint64_t* value, uint64_t data) {
    InterlockedExchange64(reinterpret_cast<volatile LONG64*>(value), static_cast<LONG64>(data));
}

} // namespace

SnapshotPublisher::~SnapshotPublisher() {
    Stop();
}

bool SnapshotPublisher::Start(DWORD sessionId) {
    wchar_t nameBuffer[128];
    swprintf(nameBuffer, std::size(nameBuffer), ffsetup::kSnapshotSectionNameFormat, sessionId);
    sectionName_ = nameBuffer;

    const size_t totalSize = sizeof(ffprotocol::SnapshotSharedHeader) + 2 * ffprotocol::kSnapshotSlotCapacityBytes;

    mapping_ = CreateFileMappingW(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        static_cast<DWORD>(totalSize >> 32), static_cast<DWORD>(totalSize & 0xFFFFFFFFu),
        sectionName_.c_str());
    if (mapping_ == nullptr) {
        return false;
    }

    view_ = static_cast<uint8_t*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, totalSize));
    if (view_ == nullptr) {
        CloseHandle(mapping_);
        mapping_ = nullptr;
        return false;
    }

    auto* header = reinterpret_cast<ffprotocol::SnapshotSharedHeader*>(view_);
    // A publisher can be stopped and restarted by the engine lifecycle. A
    // fresh mapping starts at generation zero; keep the local counter in
    // lockstep so the first publication after restart is generation one.
    generation_.store(0);
    header->formatVersion = ffprotocol::kSnapshotFormatVersion;
    header->reserved = 0;
    header->generation = 0;
    header->activeSlot = 0;
    header->slotDataSize[0] = 0;
    header->slotDataSize[1] = 0;
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
}

uint64_t SnapshotPublisher::Publish(const std::map<std::wstring, ffprotocol::SnapshotDirectory>& directories,
                                    PublishTimings* timings) {
    using Clock = std::chrono::steady_clock;
    std::lock_guard<std::mutex> lock(publishMutex_);
    if (view_ == nullptr) {
        return 0;
    }

    const auto serializationStart = timings != nullptr ? Clock::now() : Clock::time_point{};
    std::vector<uint8_t> serialized = ffprotocol::SerializeSnapshot(directories);
    if (timings != nullptr) {
        timings->serializationMs =
            std::chrono::duration<double, std::milli>(Clock::now() - serializationStart).count();
        timings->serializedBytes = serialized.size();
    }
    if (serialized.size() > ffprotocol::kSnapshotSlotCapacityBytes) {
        return 0; // exceeds the configured slot capacity
    }

    const auto copyStart = timings != nullptr ? Clock::now() : Clock::time_point{};
    auto* header = reinterpret_cast<ffprotocol::SnapshotSharedHeader*>(view_);
    const uint32_t activeSlot = LoadSharedU32(&header->activeSlot);
    const uint32_t inactiveSlot = activeSlot == 0 ? 1 : 0;

    uint8_t* slotBase = view_ + sizeof(ffprotocol::SnapshotSharedHeader) +
        static_cast<size_t>(inactiveSlot) * ffprotocol::kSnapshotSlotCapacityBytes;
    std::memcpy(slotBase, serialized.data(), serialized.size());

    const uint64_t newGeneration = generation_.fetch_add(1) + 1;

    // Publish the inactive slot's descriptor first, then flip activeSlot as
    // the final selection step. Readers use the size belonging to the slot
    // they selected, eliminating the old shared-size torn descriptor race.
    // Interlocked stores are full barriers and are visible across processes;
    // this is stronger than relying on the C++ memory model for plain fields
    // in a shared mapping.
    // address-engineering-audit-findings 4.2: repeat the version publication
    // on every generation so a reused section cannot retain stale metadata.
    StoreSharedU32(&header->formatVersion, ffprotocol::kSnapshotFormatVersion);
    StoreSharedU32(&header->slotDataSize[inactiveSlot], static_cast<uint32_t>(serialized.size()));
    StoreSharedU32(&header->activeSlot, inactiveSlot);
    StoreSharedU64(&header->generation, newGeneration);

    if (timings != nullptr) {
        timings->copyAndCommitMs =
            std::chrono::duration<double, std::milli>(Clock::now() - copyStart).count();
    }

    return newGeneration;
}

} // namespace ffengine
