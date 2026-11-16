#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>

#include "ffindexstore/Projection.h"
#include "ffindexstore/Store.h"
#include "../TestSupport.h"

namespace {
using namespace ffindexstore;
using Model = std::map<FileId, EntryRecord>;

bool Equal(const EntryRecord& a, const EntryRecord& b) {
    return a.id == b.id && a.parentId == b.parentId && a.name == b.name &&
        a.sizeBytes == b.sizeBytes && a.allocatedSizeKnown == b.allocatedSizeKnown &&
        (!a.allocatedSizeKnown || a.allocatedSizeBytes == b.allocatedSizeBytes) &&
        a.creationTime == b.creationTime && a.lastModifiedTime == b.lastModifiedTime &&
        a.lastAccessTime == b.lastAccessTime && a.attributes == b.attributes;
}

EntryRecord ReadProjection(const Projection& projection, const ProjectionEntry& row) {
    EntryRecord record;
    record.id = row.frn;
    record.parentId = row.parentFrn;
    record.name = projection.Names().Get(row.nameId);
    record.sizeBytes = row.sizeBytes;
    record.allocatedSizeBytes = row.allocatedSizeBytes;
    record.allocatedSizeKnown = row.allocatedSizeKnown;
    record.creationTime = row.creationTime;
    record.lastModifiedTime = row.lastModifiedTime;
    record.lastAccessTime = row.lastAccessTime;
    record.attributes = row.attributes;
    return record;
}

void ApplyModel(Model& model, const EntryChange& change) {
    if (change.kind == EntryChangeKind::Remove) {
        model.erase(change.record.id);
        return;
    }
    auto& row = model[change.record.id];
    row.id = change.record.id;
    const auto& input = change.record;
    const auto fields = change.fields;
    if ((fields & kEntryFieldParentId) != 0) row.parentId = input.parentId;
    if ((fields & kEntryFieldName) != 0) row.name = input.name;
    if ((fields & kEntryFieldSizeBytes) != 0) row.sizeBytes = input.sizeBytes;
    if ((fields & kEntryFieldCreationTime) != 0) row.creationTime = input.creationTime;
    if ((fields & kEntryFieldLastModifiedTime) != 0) row.lastModifiedTime = input.lastModifiedTime;
    if ((fields & kEntryFieldLastAccessTime) != 0) row.lastAccessTime = input.lastAccessTime;
    if ((fields & kEntryFieldAttributes) != 0) row.attributes = input.attributes;
    if ((fields & kEntryFieldAllocatedSizeBytes) != 0) {
        row.allocatedSizeKnown = input.allocatedSizeKnown;
        row.allocatedSizeBytes = input.allocatedSizeBytes;
    }
}

bool Matches(std::u16string_view name, std::u16string_view needle) {
    if (needle.size() > name.size()) return false;
    for (size_t i = 0; i <= name.size() - needle.size(); ++i) {
        if (CompareStringOrdinal(reinterpret_cast<LPCWCH>(name.data() + i),
                static_cast<int>(needle.size()), reinterpret_cast<LPCWCH>(needle.data()),
                static_cast<int>(needle.size()), TRUE) == CSTR_EQUAL) return true;
    }
    return false;
}

bool VerifyVolume(Store& store, const Projection& projection, VolumeRowId volume, const Model& model) {
    bool valid = true;
    size_t durableCount = 0;
    valid &= store.ForEachEntryUntil(volume, [&](const EntryRecord& row) {
        const auto expected = model.find(row.id);
        valid &= expected != model.end() && Equal(row, expected->second);
        ++durableCount;
        return true;
    });
    valid &= durableCount == model.size();
    size_t residentCount = 0;
    projection.ForEachEntry([&](const EntryKey& key, const ProjectionEntry& row) {
        if (key.volumeRowId != volume) return;
        const auto expected = model.find(key.frn);
        valid &= expected != model.end() && Equal(ReadProjection(projection, row), expected->second);
        ++residentCount;
    });
    valid &= residentCount == model.size();
    for (const auto& [id, expected] : model) {
        const auto durable = store.FindEntry(volume, id);
        const auto* resident = projection.Find(volume, id);
        valid &= durable && Equal(*durable, expected);
        valid &= resident && Equal(ReadProjection(projection, *resident), expected);
    }

    // Independently derive child membership, including missing parent 0 and
    // cross-volume repeated FRNs. Detect stale slot reuse and reparent links.
    for (const FileId parent : {FileId{}, FileId{5, 0}, FileId{10, 0}, FileId{11, 1}}) {
        std::set<FileId> expected;
        for (const auto& [id, row] : model) {
            if (row.parentId == parent && id != parent) expected.insert(id);
        }
        std::set<FileId> actual;
        const auto children = projection.ChildIndices(volume, parent);
        for (const uint32_t slot : children) {
            const auto& row = projection.EntryAt(slot);
            valid &= row.volumeRowId == volume && row.parentFrn == parent;
            valid &= actual.insert(row.frn).second;
        }
        valid &= actual == expected;
    }

    auto reader = store.OpenReadSession();
    if (!reader) return false;
    for (const std::u16string_view needle : {u"report", u"xyz", u"a_b", u"[%]", u"absent"}) {
        std::set<FileId> expected;
        for (const auto& [id, row] : model) {
            if (Matches(row.name, needle)) expected.insert(id);
        }
        std::set<FileId> durable;
        std::string ascii;
        for (const char16_t unit : needle) ascii.push_back(static_cast<char>(unit));
        valid &= reader->ForEachEntryWithAsciiNameUntil(volume, ascii, [&](const EntryRecord& row) {
            const auto item = model.find(row.id);
            valid &= item != model.end() && Equal(row, item->second);
            if (Matches(row.name, needle)) valid &= durable.insert(row.id).second;
            return true;
        });
        valid &= durable == expected;
        std::set<FileId> resident;
        const auto candidates = projection.FilenameCandidateIndices(needle);
        if (!candidates) return false; // This small resident fixture must stay accelerated.
        for (const uint32_t slot : *candidates) {
            const auto& row = projection.EntryAt(slot);
            if (row.volumeRowId != volume) continue;
            const auto item = model.find(row.frn);
            valid &= item != model.end() && Equal(ReadProjection(projection, row), item->second);
            if (Matches(projection.Names().Get(row.nameId), needle)) valid &= resident.insert(row.frn).second;
        }
        valid &= resident == expected;
    }
    return valid;
}

void RunModel(uint32_t seed, const std::string& path) {
    std::mt19937_64 random(seed);
    Store store;
    if (!store.Open(path)) {
        fftest::Check(false, "model database opens");
        return;
    }
    Projection projection;
    std::array<VolumeRowId, 3> volumes{};
    std::array<Model, 3> models;
    for (size_t v = 0; v < volumes.size(); ++v) {
        VolumeKey key;
        key.volumeGuid[0] = static_cast<uint8_t>(v + 1);
        key.serialNumber = static_cast<uint32_t>(v + 1);
        const auto volume = store.GetOrCreateVolume(key);
        if (!volume) { fftest::Check(false, "model volume created"); return; }
        volumes[v] = *volume;
    }
    std::vector<std::u16string> names = {
        u"report.txt", u"REPORT.TXT", u"xyz_tail", u"a_b[%].txt", u"aXb.txt",
        u"\u00c9_report.txt", u"\u017Fand.txt", u"photo_\U0001F600_xyz.txt", u"short", u"",
        std::u16string(1, static_cast<char16_t>(0xD800)) + u"report",
        u"xyz" + std::u16string(1, static_cast<char16_t>(0xDC00)),
    };
    bool valid = true;
    for (uint32_t step = 0; step < 160 && valid; ++step) {
        const size_t v = static_cast<size_t>(random() % volumes.size());
        const auto volume = volumes[v];
        // Hold an independent reader across the next commit. Its old snapshot
        // must remain exact while the writer/projection move to the next state.
        auto oldReader = store.OpenReadSession();
        if (!oldReader) { valid = false; break; }
        const Model previous = models[v];
        std::vector<EntryChange> batch;
        for (size_t i = 0; i < 12; ++i) {
            EntryChange change;
            change.kind = random() % 4 == 0 ? EntryChangeKind::Remove : EntryChangeKind::Upsert;
            change.fields = static_cast<EntryFieldMask>(random() & kEntryFieldMaskAll);
            if (i == 0) {
                change.kind = EntryChangeKind::Upsert;
                change.fields = kEntryFieldMaskAll;
            }
            auto& row = change.record;
            row.id = FileId{100 + random() % 100, random() % 2};
            const std::array<FileId, 3> parents{FileId{5, 0}, FileId{10, 0}, FileId{11, 1}};
            row.parentId = parents[static_cast<size_t>(random() % parents.size())];
            row.name = names[static_cast<size_t>(random() % names.size())];
            row.sizeBytes = random();
            row.allocatedSizeKnown = (random() & 1) != 0;
            row.allocatedSizeBytes = row.allocatedSizeKnown ? random() : 0;
            row.creationTime = random();
            row.lastModifiedTime = random();
            row.lastAccessTime = random();
            row.attributes = static_cast<uint32_t>(random());
            batch.push_back(change);
        }
        if (!store.ApplyBatch(volume, batch)) { valid = false; break; }
        for (const auto& change : batch) {
            ApplyModel(models[v], change);
            if (change.kind == EntryChangeKind::Remove) projection.Remove(volume, change.record.id);
            else projection.UpsertPartial(volume, change.record, change.fields);
        }
        if (step == 0) {
            // The oracle must reject wrong values, missing rows and invented
            // identities, rather than just proving two production readers agree.
            Model corrupt = models[v];
            corrupt.begin()->second.lastModifiedTime ^= 1;
            valid &= !VerifyVolume(store, projection, volume, corrupt);
            corrupt = models[v];
            corrupt.erase(corrupt.begin());
            valid &= !VerifyVolume(store, projection, volume, corrupt);
            corrupt = models[v];
            EntryRecord invented;
            invented.id = FileId{9999, 9999};
            corrupt.emplace(invented.id, invented);
            valid &= !VerifyVolume(store, projection, volume, corrupt);
        }
        size_t oldCount = 0;
        valid &= oldReader->ForEachEntryUntil(volume, [&](const EntryRecord& row) {
            const auto expected = previous.find(row.id);
            valid &= expected != previous.end() && Equal(row, expected->second);
            ++oldCount;
            return true;
        });
        valid &= oldCount == previous.size();
        oldReader.reset();
        if (step % 17 == 16) {
            projection.EvictVolume(volume);
            valid &= projection.ReloadEvictedVolume(store, volume);
        }
        if (step % 31 == 30) {
            store.Close();
            valid &= store.Open(path);
            for (size_t n = 0; n < volumes.size() && valid; ++n) {
                valid &= projection.RebuildVolumeFromStore(store, volumes[n], models[n].size());
            }
        }
        for (size_t n = 0; n < volumes.size() && valid; ++n) {
            valid &= VerifyVolume(store, projection, volumes[n], models[n]);
        }
        if (!valid) std::fprintf(stderr, "model mismatch seed=%u step=%u volume=%lld\n",
            seed, step, static_cast<long long>(volume));
    }
    fftest::Check(valid, "seeded model agrees with durable/resident rows, children, candidates and pinned readers");
}
} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() /
        ("ff-store-projection-model-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    if (!std::filesystem::create_directory(root)) return 1;
    try {
        for (const uint32_t seed : {0x147ACu, 0xBAD5EEDu, 0xFFFFFFFFu, 0u}) {
            RunModel(seed, (root / (std::to_string(seed) + ".db")).string());
        }
    } catch (...) {
        fftest::Check(false, "model completes without exception");
    }
    std::error_code error;
    std::filesystem::remove_all(root, error);
    fftest::Check(!error, "owned model fixture cleans up");
    return fftest::FailureCount() == 0 ? 0 : 1;
}
