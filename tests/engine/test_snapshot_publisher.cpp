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

    // fix-initial-snapshot-handshake: startup alone must expose a usable
    // generation -- no directory has been published at this point.
    const uint64_t initialGeneration = publisher.CurrentGeneration();
    Check(initialGeneration == ffengine::SnapshotPublisher::kFirstGeneration,
          "startup commits the first usable generation before any publication");
    HANDLE startupMapping = OpenFileMappingW(FILE_MAP_READ, FALSE, publisher.CurrentSectionName().c_str());
    Check(startupMapping != nullptr, "the startup generation mapping opens read-only");
    if (startupMapping != nullptr) CloseHandle(startupMapping);

    const std::map<std::wstring, ffprotocol::SnapshotDirectory> first{
        {L"C:\\", MakeDirectory(L"first.txt", 11)}};
    const uint64_t firstGeneration = publisher.Publish(first);
    Check(firstGeneration == initialGeneration + 1, "first publication advances the generation by one");

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
    // fix-initial-snapshot-handshake: a stale reader that still holds
    // generation `firstGeneration` open must stay safe while the publisher
    // advances -- the retained view keeps parsing the old immutable payload.
    const uint64_t secondGeneration = publisher.Publish(second);
    Check(secondGeneration == firstGeneration + 1, "second publication increments generation");
    Check(firstHeader->generation == firstGeneration &&
              ffprotocol::ParseSnapshot(view + sizeof(*firstHeader),
                                        static_cast<size_t>(firstHeader->payloadSizeBytes)).has_value(),
          "a stale prior-generation reader remains safe across publication");
    UnmapViewOfFile(view);
    CloseHandle(mapping);

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
    Check(publisher.Publish(large) == secondGeneration + 1,
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
    const std::wstring priorPrefix = publisher.SectionName();
    publisher.Stop();

    Check(publisher.Start(GetCurrentProcessId() + 1), "snapshot publisher restarts");
    Check(!priorPrefix.empty() && publisher.SectionName() != priorPrefix,
          "restart creates a fresh epoch prefix");
    Check(publisher.CurrentGeneration() == ffengine::SnapshotPublisher::kFirstGeneration,
          "restart again exposes a usable initial generation");
    Check(publisher.Publish(first) == ffengine::SnapshotPublisher::kFirstGeneration + 1,
          "restart begins a fresh generation sequence");
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
        // fix-initial-snapshot-handshake: the startup empty snapshot occupies
        // kFirstGeneration, so the contended publishes are contiguous from
        // kFirstGeneration + 1.
        if (generations[index] != index + ffengine::SnapshotPublisher::kFirstGeneration + 1) {
            contiguous = false;
            break;
        }
    }
    Check(contiguous, "contended publications receive unique contiguous generations");
    Check(publisher.CurrentGeneration() == kPublicationCount + ffengine::SnapshotPublisher::kFirstGeneration,
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
            Check(header->generation == kPublicationCount + ffengine::SnapshotPublisher::kFirstGeneration,
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
    const uint64_t generationBeforePublish = publisher.CurrentGeneration();
    Check(publisher.Publish(snapshot) == generationBeforePublish + 1,
          "case-insensitive snapshot map uses the publisher overload and advances by one");
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

// fix-initial-snapshot-handshake (tasks.md 1.x): startup before any user
// directory is published must expose a valid, canonical, parseable empty
// snapshot as the current generation.
void TestPublisherStartupEmptySnapshot() {
    ffengine::SnapshotPublisher publisher;
    Check(publisher.Start(GetCurrentProcessId() + 8), "startup snapshot publisher starts");
    if (publisher.SectionName().empty()) return;

    const uint64_t initialGeneration = publisher.CurrentGeneration();
    Check(initialGeneration == ffengine::SnapshotPublisher::kFirstGeneration,
          "current generation is nonzero and equals the first usable generation after startup");
    Check(initialGeneration != 0, "current generation is nonzero after successful startup");

    const std::wstring sectionName = publisher.CurrentSectionName();
    Check(!sectionName.empty(), "current section name is available after startup");
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, sectionName.c_str());
    Check(mapping != nullptr, "current section name opens read-only");
    if (mapping == nullptr) {
        publisher.Stop();
        return;
    }
    const auto* view = static_cast<const uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
    Check(view != nullptr, "current section maps read-only");
    if (view == nullptr) {
        CloseHandle(mapping);
        publisher.Stop();
        return;
    }

    const auto* header = reinterpret_cast<const ffprotocol::SnapshotSharedHeader*>(view);
    Check(header->formatVersion == ffprotocol::kSnapshotFormatVersion,
          "startup generation header format version is current");
    Check(ffprotocol::IsSnapshotFormatCompatible(header->formatVersion),
          "startup generation header is format-compatible");
    Check(header->generation == initialGeneration,
          "startup generation header commits the acknowledged generation");
    Check(header->payloadSizeBytes == ffprotocol::SerializedSnapshotSize(
              std::map<std::wstring, ffprotocol::SnapshotDirectory>{}),
          "startup generation payload size equals the canonical serialized empty snapshot");
    const auto parsed = ffprotocol::ParseSnapshot(view + sizeof(*header),
                                                  static_cast<size_t>(header->payloadSizeBytes));
    Check(parsed.has_value(), "startup generation payload parses with the production parser");
    Check(parsed.has_value() && parsed->empty(),
          "startup generation parses to an empty directory map");

    // Immutability: without a Publish, the committed generation does not move.
    const std::wstring stableSectionName = publisher.CurrentSectionName();
    const uint64_t stableGeneration = publisher.CurrentGeneration();
    Check(stableSectionName == sectionName && stableGeneration == initialGeneration,
          "startup generation remains immutable until a publication");
    UnmapViewOfFile(view);
    CloseHandle(mapping);

    // A subsequent non-empty publication advances by exactly one.
    const std::map<std::wstring, ffprotocol::SnapshotDirectory> directories{
        {L"C:\\startup", MakeDirectory(L"after-start.txt", 7)}};
    const uint64_t nextGeneration = publisher.Publish(directories);
    Check(nextGeneration == initialGeneration + 1,
          "subsequent non-empty publication advances by exactly one generation");
    Check(publisher.CurrentGeneration() == nextGeneration,
          "publisher current generation follows the publication");

    // A failed later publication preserves the prior valid generation.
    std::map<std::wstring, ffprotocol::SnapshotDirectory> oversized;
    ffprotocol::SnapshotDirectory oversizedDirectory;
    oversizedDirectory.status = ffprotocol::DirectoryEnumerationStatus::Success;
    const size_t oversizedPathLength = ffprotocol::kSnapshotMaximumPayloadBytes / sizeof(wchar_t) + 1;
    oversized.emplace(std::wstring(oversizedPathLength, L'x'), std::move(oversizedDirectory));
    Check(publisher.Publish(oversized) == 0, "failed later publication returns zero");
    Check(publisher.CurrentGeneration() == nextGeneration,
          "failed later publication preserves the prior valid generation");
    const std::wstring priorPrefix = publisher.SectionName();
    publisher.Stop();

    // Restart: fresh epoch prefix, usable initial generation again.
    Check(publisher.Start(GetCurrentProcessId() + 9), "startup snapshot publisher restarts");
    Check(!priorPrefix.empty() && publisher.SectionName() != priorPrefix,
          "restart creates a fresh epoch/prefix");
    Check(publisher.CurrentGeneration() == ffengine::SnapshotPublisher::kFirstGeneration,
          "restart again provides a usable initial generation");
    HANDLE restartMapping = OpenFileMappingW(FILE_MAP_READ, FALSE, publisher.CurrentSectionName().c_str());
    Check(restartMapping != nullptr, "restarted startup generation opens read-only");
    if (restartMapping != nullptr) CloseHandle(restartMapping);
    publisher.Stop();
}

#ifdef FF_SNAPSHOT_PUBLISHER_TESTING
// fix-initial-snapshot-handshake (tasks.md 8.x): initial publication failure
// must fail Start closed -- no usable generation, no leftover prefix.
void TestPublisherStartFailureInjection() {
    ffengine::SnapshotPublisher publisher;

    ffengine::SnapshotPublisher::SetCreateFileMappingHook(
        [](const std::wstring&) -> HANDLE { return nullptr; });
    Check(!publisher.Start(GetCurrentProcessId() + 10),
          "CreateFileMappingW failure on the initial generation fails Start");
    Check(publisher.CurrentGeneration() == 0,
          "failed startup leaves the generation at the zero sentinel");
    Check(publisher.SectionName().empty(),
          "failed startup rolls back the section prefix");
    ffengine::SnapshotPublisher::SetCreateFileMappingHook({});
    Check(publisher.Start(GetCurrentProcessId() + 11),
          "publisher starts again once the injection is cleared");
    publisher.Stop();

    ffengine::SnapshotPublisher::SetMapViewOfFileHook(
        [](HANDLE) -> uint8_t* { return nullptr; });
    Check(!publisher.Start(GetCurrentProcessId() + 12),
          "MapViewOfFile failure on the initial generation fails Start");
    Check(publisher.CurrentGeneration() == 0 && publisher.SectionName().empty(),
          "failed startup rolls back mapping and prefix state");
    ffengine::SnapshotPublisher::SetMapViewOfFileHook({});
    Check(publisher.Start(GetCurrentProcessId() + 13),
          "publisher starts again after MapViewOfFile injection is cleared");
    publisher.Stop();
}

void TestPublisherSerializationFailurePreservesLastKnownGood() {
    ffengine::SnapshotPublisher publisher;
    Check(publisher.Start(GetCurrentProcessId() + 14),
          "publisher starts before serialization failure injection");
    if (publisher.SectionName().empty()) return;

    const std::map<std::wstring, ffprotocol::SnapshotDirectory> prior{
        {L"C:\\serialization-failure", MakeDirectory(L"prior.txt", 17)}};
    const uint64_t priorGeneration = publisher.Publish(prior);
    const std::wstring priorSection = publisher.CurrentSectionName();
    Check(priorGeneration != 0, "a last-known-good generation is published before injection");

    ffengine::SnapshotPublisher::SetWriteSnapshotHook(
        [](uint8_t*, size_t) { return false; });
    const std::map<std::wstring, ffprotocol::SnapshotDirectory> replacement{
        {L"C:\\serialization-failure", MakeDirectory(L"replacement.txt", 29)}};
    Check(publisher.Publish(replacement) == 0,
          "serialization failure rejects the candidate publication");
    Check(publisher.CurrentGeneration() == priorGeneration,
          "serialization failure does not advance the visible generation");
    Check(publisher.CurrentSectionName() == priorSection,
          "serialization failure retains the last-known-good mapping name");
    ffengine::SnapshotPublisher::SetWriteSnapshotHook({});

    Check(publisher.Publish(replacement) == priorGeneration + 1,
          "publication recovers after serialization injection is cleared");
    Check(publisher.CurrentGeneration() == priorGeneration + 1,
          "recovery publication advertises the next valid generation");
    publisher.Stop();
}
#endif

} // namespace

int main() {
    TestPublisherUsesImmutableGenerations();
    TestPublisherUnderThreadContention();
    TestCaseVariantSnapshotKeysCoalesce();
    TestTwoPassSerializerBounds();
    TestPublisherStartupEmptySnapshot();
#ifdef FF_SNAPSHOT_PUBLISHER_TESTING
    TestPublisherStartFailureInjection();
    TestPublisherSerializationFailurePreservesLastKnownGood();
#endif
    return FailureCount() == 0 ? 0 : 1;
}
