#include <windows.h>

#include <cstdint>
#include <algorithm>
#include <atomic>
#include <map>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

void TestPublisherUsesImmutableGenerations() {
    ffengine::SnapshotPublisher publisher;
    Check(publisher.Start(GetCurrentProcessId()), "snapshot publisher starts");
    if (publisher.SectionName().empty()) return;

    const std::map<std::wstring, ffprotocol::SnapshotDirectory> first{
        {L"C:\\", MakeDirectory(L"first.txt", 11)}};
    const uint64_t firstGeneration = publisher.Publish(first);
    Check(firstGeneration == 1, "first publication receives generation one");

    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, publisher.CurrentSectionName().c_str());
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

    const auto* firstHeader = reinterpret_cast<const ffprotocol::SnapshotSharedHeader*>(view);
    Check(firstHeader->formatVersion == ffprotocol::kSnapshotFormatVersion,
          "the mapping header publishes the supported snapshot format version");
    Check(ffprotocol::IsSnapshotFormatCompatible(firstHeader->formatVersion),
          "the current snapshot format is accepted");
    Check(!ffprotocol::IsSnapshotFormatCompatible(ffprotocol::kSnapshotFormatVersion + 1),
          "a future snapshot format is rejected");
    Check(firstHeader->generation == firstGeneration, "immutable mapping commits its generation");
    const uint64_t firstSize = firstHeader->payloadSizeBytes;
    Check(firstSize == ffprotocol::SerializeSnapshot(first).size(),
          "immutable mapping size matches serialized snapshot");
    Check(ffprotocol::ParseSnapshot(view + sizeof(*firstHeader), static_cast<size_t>(firstSize)).has_value(),
          "first immutable mapping parses successfully");

    const std::map<std::wstring, ffprotocol::SnapshotDirectory> second{
        {L"C:\\", MakeDirectory(L"second.txt", 22)}};
    const std::wstring firstSectionName = publisher.CurrentSectionName();
    UnmapViewOfFile(view);
    CloseHandle(mapping);
    const uint64_t secondGeneration = publisher.Publish(second);
    Check(secondGeneration == 2, "second publication increments generation");

    Check(firstSectionName != publisher.CurrentSectionName(),
          "each publication receives a distinct immutable mapping name");
    mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, publisher.CurrentSectionName().c_str());
    Check(mapping != nullptr, "second immutable mapping opens read-only");
    if (mapping == nullptr) {
        publisher.Stop();
        return;
    }
    view = static_cast<const uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
    Check(view != nullptr, "second immutable mapping maps read-only");
    if (view == nullptr) {
        CloseHandle(mapping);
        publisher.Stop();
        return;
    }
    const auto* secondHeader = reinterpret_cast<const ffprotocol::SnapshotSharedHeader*>(view);
    Check(secondHeader->generation == secondGeneration &&
              secondHeader->payloadSizeBytes == ffprotocol::SerializeSnapshot(second).size(),
          "second immutable mapping has its own committed descriptor");
    const auto parsed = ffprotocol::ParseSnapshot(view + sizeof(*secondHeader),
                                                  static_cast<size_t>(secondHeader->payloadSizeBytes));
    Check(parsed.has_value() && parsed->contains(L"C:\\") &&
              parsed->at(L"C:\\").entries.front().name == L"second.txt",
          "latest immutable mapping parses to the newest snapshot");

    const std::map<std::wstring, ffprotocol::SnapshotDirectory> large{
        {L"C:\\", MakeLargeDirectory()}};
    const size_t largeSize = ffprotocol::SerializeSnapshot(large).size();
    Check(largeSize > 8ULL * 1024ULL * 1024ULL &&
              largeSize < ffprotocol::kSnapshotMaximumPayloadBytes,
          "large-directory fixture is above the old 8 MiB cliff but below the bounded payload");
    UnmapViewOfFile(view);
    CloseHandle(mapping);
    Check(publisher.Publish(large) == 3,
          "large directory publishes without falling into the old fixed-slot capacity cliff");

    std::map<std::wstring, ffprotocol::SnapshotDirectory> oversized;
    ffprotocol::SnapshotDirectory oversizedDirectory;
    oversizedDirectory.status = ffprotocol::DirectoryEnumerationStatus::Success;
    const size_t oversizedPathLength = ffprotocol::kSnapshotMaximumPayloadBytes / sizeof(wchar_t) + 1;
    oversized.emplace(std::wstring(oversizedPathLength, L'x'), std::move(oversizedDirectory));
    const uint64_t generationBeforeFailure = publisher.CurrentGeneration();
    Check(publisher.Publish(oversized) == 0, "oversized publication fails closed");
    Check(publisher.CurrentGeneration() == generationBeforeFailure,
          "failed publication does not advance or announce a generation");
    publisher.Stop();

    Check(publisher.Start(GetCurrentProcessId() + 1), "snapshot publisher restarts");
    Check(publisher.Publish(first) == 1, "restart begins a fresh generation sequence");
    publisher.Stop();
}

void TestPublisherUnderThreadContention() {
    constexpr size_t kThreadCount = 8;
    constexpr size_t kPublishesPerThread = 64;
    constexpr size_t kPublicationCount = kThreadCount * kPublishesPerThread;

    ffengine::SnapshotPublisher publisher;
    Check(publisher.Start(GetCurrentProcessId() + 2),
          "snapshot publisher starts for the contention test");
    if (publisher.SectionName().empty()) return;

    std::vector<uint64_t> generations(kPublicationCount, 0);
    std::atomic<size_t> readyCount{0};
    std::atomic<bool> start{false};
    std::vector<std::thread> workers;
    workers.reserve(kThreadCount);

    for (size_t threadIndex = 0; threadIndex < kThreadCount; ++threadIndex) {
        workers.emplace_back([&, threadIndex]() {
            readyCount.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                YieldProcessor();
            }

            for (size_t iteration = 0; iteration < kPublishesPerThread; ++iteration) {
                const std::wstring root = L"C:\\snapshot-contention-" +
                    std::to_wstring(threadIndex) + L"-" + std::to_wstring(iteration);
                const std::map<std::wstring, ffprotocol::SnapshotDirectory> directories{
                    {root, MakeDirectory(L"contention.txt", iteration)}};
                generations[threadIndex * kPublishesPerThread + iteration] =
                    publisher.Publish(directories);
            }
        });
    }

    while (readyCount.load(std::memory_order_acquire) != kThreadCount) {
        Sleep(0);
    }
    start.store(true, std::memory_order_release);

    for (auto& worker : workers) {
        worker.join();
    }

    std::sort(generations.begin(), generations.end());
    bool contiguous = true;
    for (size_t index = 0; index < generations.size(); ++index) {
        if (generations[index] != index + 1) {
            contiguous = false;
            break;
        }
    }
    Check(contiguous, "contended publications receive unique contiguous generations");
    Check(publisher.CurrentGeneration() == kPublicationCount,
          "contended publications advance the publisher to the final generation");

    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, publisher.CurrentSectionName().c_str());
    Check(mapping != nullptr, "contention test opens the published mapping read-only");
    if (mapping != nullptr) {
        const auto* view = static_cast<const uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
        Check(view != nullptr, "contention test maps the published section read-only");
        if (view != nullptr) {
            const auto* header = reinterpret_cast<const ffprotocol::SnapshotSharedHeader*>(view);
            Check(header->formatVersion == ffprotocol::kSnapshotFormatVersion,
                  "contended publication keeps the supported snapshot format version");
            Check(header->generation == kPublicationCount,
                  "contended publication keeps the final immutable generation");
            Check(header->payloadSizeBytes <= ffprotocol::kSnapshotMaximumPayloadBytes,
                  "contended publication keeps the payload within the explicit bound");
            const auto parsed = ffprotocol::ParseSnapshot(view + sizeof(*header),
                                                          static_cast<size_t>(header->payloadSizeBytes));
            Check(parsed.has_value() && parsed->size() == 1,
                  "contended publication leaves a parseable final snapshot");
            UnmapViewOfFile(view);
        }
        CloseHandle(mapping);
    }

    publisher.Stop();
}

void TestCaseVariantSnapshotKeysCoalesce() {
    ffprotocol::CaseInsensitiveSnapshotMap snapshot;
    snapshot.emplace(L"C:\\Fixture", MakeDirectory(L"first.txt", 11));
    snapshot.emplace(L"c:\\fixture", MakeDirectory(L"second.txt", 22));
    Check(snapshot.size() == 1 && snapshot.begin()->second.entries.front().name == L"first.txt",
          "case-variant snapshot keys preserve one Windows directory and first spelling");

    ffengine::SnapshotPublisher publisher;
    Check(publisher.Start(GetCurrentProcessId() + 4),
          "case-insensitive snapshot publisher starts");
    if (publisher.SectionName().empty()) return;
    Check(publisher.Publish(snapshot) == 1,
          "case-insensitive snapshot map uses the publisher overload");
    publisher.Stop();
}

void TestTwoPassSerializerBounds() {
    const std::map<std::wstring, ffprotocol::SnapshotDirectory> directories{
        {L"C:\\two-pass", MakeDirectory(L"payload.txt", 42)}};
    const size_t expectedSize = ffprotocol::SerializedSnapshotSize(directories);
    const std::vector<uint8_t> serialized = ffprotocol::SerializeSnapshot(directories);
    Check(expectedSize == serialized.size(),
          "two-pass size calculation matches the compatibility serializer");

    std::vector<uint8_t> direct(expectedSize);
    Check(ffprotocol::SerializeSnapshotTo(directories, direct.data(), direct.size()),
          "two-pass serializer writes into caller-owned storage");
    Check(direct == serialized, "two-pass serializer preserves the snapshot byte format");
    Check(expectedSize == 0 || !ffprotocol::SerializeSnapshotTo(
              directories, direct.data(), expectedSize - 1),
          "two-pass serializer rejects a truncated destination");
    Check(ffprotocol::SerializedSnapshotSize(
              std::map<std::wstring, ffprotocol::SnapshotDirectory>{}) == sizeof(uint32_t),
          "an empty snapshot still has its directory-count payload");
}

} // namespace

int main() {
    TestPublisherUsesImmutableGenerations();
    TestPublisherUnderThreadContention();
    TestCaseVariantSnapshotKeysCoalesce();
    TestTwoPassSerializerBounds();
    return FailureCount() == 0 ? 0 : 1;
}
