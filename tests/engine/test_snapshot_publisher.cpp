#include <windows.h>

#include <cstdint>
#include <map>
#include <string>
#include <utility>

#include "SnapshotPublisher.h"
#include "ffprotocol/SnapshotFormat.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {

ffprotocol::SnapshotDirectory MakeDirectory(const wchar_t* name, uint64_t size) {
    ffprotocol::SnapshotDirectory directory;
    directory.status = ffprotocol::DirectoryEnumerationStatus::Success;
    ffprotocol::SnapshotDirectoryEntry entry;
    entry.name = name;
    entry.sizeBytes = size;
    entry.isDirectory = false;
    directory.entries.push_back(std::move(entry));
    return directory;
}

ffprotocol::SnapshotDirectory MakeLargeDirectory() {
    ffprotocol::SnapshotDirectory directory;
    directory.status = ffprotocol::DirectoryEnumerationStatus::Success;
    directory.entries.reserve(120000);
    for (uint32_t index = 0; index < 120000; ++index) {
        ffprotocol::SnapshotDirectoryEntry entry;
        entry.name = L"large-entry-" + std::to_wstring(index) + L"-snapshot-capacity-regression.txt";
        entry.sizeBytes = index;
        directory.entries.push_back(std::move(entry));
    }
    return directory;
}

void TestPublisherUsesPerSlotDescriptors() {
    ffengine::SnapshotPublisher publisher;
    Check(publisher.Start(GetCurrentProcessId()), "snapshot publisher starts");
    if (publisher.SectionName().empty()) return;

    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, publisher.SectionName().c_str());
    Check(mapping != nullptr, "snapshot mapping opens read-only");
    if (mapping == nullptr) {
        publisher.Stop();
        return;
    }
    const auto* view = static_cast<const uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
    Check(view != nullptr, "snapshot mapping maps read-only");
    if (view == nullptr) {
        CloseHandle(mapping);
        publisher.Stop();
        return;
    }

    const std::map<std::wstring, ffprotocol::SnapshotDirectory> first{
        {L"C:\\", MakeDirectory(L"first.txt", 11)}};
    const uint64_t firstGeneration = publisher.Publish(first);
    Check(firstGeneration == 1, "first publication receives generation one");

    const auto* firstHeader = reinterpret_cast<const ffprotocol::SnapshotSharedHeader*>(view);
    const uint32_t firstSlot = firstHeader->activeSlot;
    Check(firstSlot <= 1, "active slot is in range");
    const uint32_t safeFirstSlot = firstSlot <= 1 ? firstSlot : 0;
    const uint32_t firstSize = firstHeader->slotDataSize[safeFirstSlot];
    Check(firstSize == ffprotocol::SerializeSnapshot(first).size(),
          "active slot size matches serialized snapshot");

    const std::map<std::wstring, ffprotocol::SnapshotDirectory> second{
        {L"C:\\", MakeDirectory(L"second.txt", 22)}};
    const uint64_t secondGeneration = publisher.Publish(second);
    Check(secondGeneration == 2, "second publication increments generation");

    const uint32_t secondSlot = firstHeader->activeSlot;
    const uint32_t safeSecondSlot = secondSlot <= 1 ? secondSlot : 0;
    Check(secondSlot != firstSlot, "publication flips to the inactive slot");
    Check(firstHeader->slotDataSize[safeSecondSlot] == ffprotocol::SerializeSnapshot(second).size(),
          "second slot has its own descriptor");
    Check(firstHeader->slotDataSize[safeFirstSlot] == firstSize,
          "previous slot descriptor remains stable");

    const uint8_t* slotBase = view + sizeof(ffprotocol::SnapshotSharedHeader) +
        static_cast<size_t>(safeSecondSlot) * ffprotocol::kSnapshotSlotCapacityBytes;
    const auto parsed = ffprotocol::ParseSnapshot(slotBase, firstHeader->slotDataSize[safeSecondSlot]);
    Check(parsed.has_value() && parsed->contains(L"C:\\") &&
              parsed->at(L"C:\\").entries.front().name == L"second.txt",
          "active slot parses to the latest snapshot");

    const std::map<std::wstring, ffprotocol::SnapshotDirectory> large{
        {L"C:\\", MakeLargeDirectory()}};
    const size_t largeSize = ffprotocol::SerializeSnapshot(large).size();
    Check(largeSize > 8ULL * 1024ULL * 1024ULL &&
              largeSize < ffprotocol::kSnapshotSlotCapacityBytes,
          "large-directory fixture is above the old 8 MiB cliff but below the bounded slot");
    Check(publisher.Publish(large) == 3,
          "large directory publishes without falling into the old 8 MiB capacity cliff");

    UnmapViewOfFile(view);
    CloseHandle(mapping);
    publisher.Stop();

    Check(publisher.Start(GetCurrentProcessId() + 1), "snapshot publisher restarts");
    Check(publisher.Publish(first) == 1, "restart begins a fresh generation sequence");
    publisher.Stop();
}

} // namespace

int main() {
    TestPublisherUsesPerSlotDescriptors();
    return FailureCount() == 0 ? 0 : 1;
}
