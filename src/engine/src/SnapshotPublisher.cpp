#include "SnapshotPublisher.h"

#include <cstdio>
#include <cstring>
#include <iterator>

#include "ffsetup/Identifiers.h"

namespace ffengine {

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
    header->generation = 0;
    header->activeSlot = 0;
    header->activeSlotDataSize = 0;
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

uint64_t SnapshotPublisher::Publish(const std::map<std::wstring, ffprotocol::SnapshotDirectory>& directories) {
    std::lock_guard<std::mutex> lock(publishMutex_);
    if (view_ == nullptr) {
        return 0;
    }

    std::vector<uint8_t> serialized = ffprotocol::SerializeSnapshot(directories);
    if (serialized.size() > ffprotocol::kSnapshotSlotCapacityBytes) {
        return 0; // exceeds the configured slot capacity
    }

    auto* header = reinterpret_cast<ffprotocol::SnapshotSharedHeader*>(view_);
    const uint32_t inactiveSlot = header->activeSlot == 0 ? 1 : 0;

    uint8_t* slotBase = view_ + sizeof(ffprotocol::SnapshotSharedHeader) +
        static_cast<size_t>(inactiveSlot) * ffprotocol::kSnapshotSlotCapacityBytes;
    std::memcpy(slotBase, serialized.data(), serialized.size());

    const uint64_t newGeneration = generation_.fetch_add(1) + 1;

    // Publish the new slot's size first, then flip activeSlot as the final
    // step -- a reader that observes the new activeSlot value will also
    // observe the matching activeSlotDataSize, since x86/x64 doesn't
    // reorder stores past earlier stores in program order.
    header->activeSlotDataSize = static_cast<uint32_t>(serialized.size());
    header->activeSlot = inactiveSlot;
    header->generation = newGeneration;

    return newGeneration;
}

} // namespace ffengine
