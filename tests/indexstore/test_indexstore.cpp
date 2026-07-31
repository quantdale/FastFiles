#include <cstdio>
#include <cstdlib>
#include <string>

#include "ffindexstore/IndexStore.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* description) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++g_failures;
    } else {
        std::printf("ok: %s\n", description);
    }
}

using namespace ffindexstore;

std::wstring TestDatabasePath(const wchar_t* name) {
    // A bare relative filename, deliberately -- CTest always runs a test
    // executable in a writable working directory, so this needs no
    // platform-specific "find a temp directory" lookup at all.
    return std::wstring(name);
}

std::string ToNarrow(const std::wstring& wide) {
    std::string narrow(wide.begin(), wide.end());
    return narrow;
}

void DeleteDatabaseFiles(const std::wstring& path) {
    std::remove(ToNarrow(path).c_str());
    std::remove(ToNarrow(path + L"-wal").c_str());
    std::remove(ToNarrow(path + L"-shm").c_str());
}

IngestEntry MakeEntry(DurableVolumeId volumeId, uint64_t frn, uint64_t parentFrn, const std::u16string& name,
                       uint64_t size = 0, uint32_t attributes = 0) {
    IngestEntry entry;
    entry.op = IngestOp::Upsert;
    entry.key = EntryKey{volumeId, FileId128::FromNtfs(frn)};
    entry.parentFileReferenceNumber = FileId128::FromNtfs(parentFrn);
    entry.name = name;
    entry.sizeBytes = size;
    entry.attributes = attributes;
    return entry;
}

// task 9.1: crash-recovery -- an unclean shutdown (no explicit checkpoint,
// just Close()) must not lose committed data; WAL replay on the next Open
// must recover a consistent, fully-populated projection.
void TestCrashRecoveryViaWalReplay() {
    const std::wstring path = TestDatabasePath(L"ffindexstore_test_crash.db");
    DeleteDatabaseFiles(path);

    DurableVolumeId volumeId;
    {
        IndexStore store;
        Check(store.Open(path), "database opens (creating a fresh file)");
        Check(store.LastIntegrityCheckPassed(), "fresh database passes integrity check");

        volumeId = store.ResolveVolume(VolumeIdentity{L"{vol-1}", 1}, L'C', 1000);
        Check(volumeId != kInvalidDurableVolumeId, "ResolveVolume returns a valid durable id");

        IngestEntry root = MakeEntry(volumeId, 5, 5, u"", 0, kFileAttributeDirectory);
        IngestEntry dir = MakeEntry(volumeId, 10, 5, u"Users", 0, kFileAttributeDirectory);
        IngestEntry file = MakeEntry(volumeId, 11, 10, u"notes.txt", 42);

        int published = 0;
        Check(store.IngestBatch(volumeId, {root, dir, file}, [&] { ++published; }), "batch commits successfully");
        Check(published == 1, "onProjectionUpdated fires exactly once per successful batch");
        Check(store.View().EntryCount() == 3, "projection reflects all 3 ingested entries");

        store.Close(); // no explicit checkpoint -- simulates an unclean-ish shutdown
    }

    {
        IndexStore reopened;
        Check(reopened.Open(path), "database reopens after close");
        Check(reopened.LastIntegrityCheckPassed(), "reopened database passes integrity check (WAL replay succeeded)");

        int rebuiltVolumes = 0;
        reopened.RebuildProjectionFromStore([&](DurableVolumeId) { ++rebuiltVolumes; });
        Check(rebuiltVolumes == 1, "exactly one volume rebuilt");
        Check(reopened.View().EntryCount() == 3, "no committed data was lost across the close/reopen cycle");

        auto path2 = reopened.View().ReconstructPath(EntryKey{volumeId, FileId128::FromNtfs(11)}, L"C:");
        Check(path2.has_value() && *path2 == L"C:\\Users\\notes.txt", "path reconstruction survives rebuild-from-store");
    }

    DeleteDatabaseFiles(path);
}

// task 9.5 (projection half): a self-referential root (NTFS record 5) and
// a fabricated parent cycle must not make ChildrenOf/ReconstructPath loop
// or double-count.
void TestCycleSafety() {
    const std::wstring path = TestDatabasePath(L"ffindexstore_test_cycle.db");
    DeleteDatabaseFiles(path);

    IndexStore store;
    Check(store.Open(path), "cycle-safety test database opens");
    DurableVolumeId volumeId = store.ResolveVolume(VolumeIdentity{L"{vol-2}", 2}, L'D', 1000);

    IngestEntry root = MakeEntry(volumeId, 5, 5, u"", 0, kFileAttributeDirectory);
    Check(store.IngestBatch(volumeId, {root}, [] {}), "self-referential root ingests successfully");

    auto rootChildren = store.View().ChildrenOf(EntryKey{volumeId, FileId128::FromNtfs(5)});
    Check(rootChildren.empty(), "a self-referential root is not its own child in the parent->children index");

    IngestEntry a = MakeEntry(volumeId, 10, 5, u"A", 0, kFileAttributeDirectory);
    IngestEntry b = MakeEntry(volumeId, 11, 10, u"B", 0, kFileAttributeDirectory);
    Check(store.IngestBatch(volumeId, {a, b}, [] {}), "two-level chain ingests successfully");

    // Fabricate a cycle: reparent A (10) under B (11), whose parent is A --
    // something a well-formed volume should never produce, but the
    // defensive walk must still terminate (task 2.4/D7).
    IngestEntry cyc = MakeEntry(volumeId, 10, 11, u"A", 0, kFileAttributeDirectory);
    Check(store.IngestBatch(volumeId, {cyc}, [] {}), "fabricated cycle ingests without error");

    auto cyclePath = store.View().ReconstructPath(EntryKey{volumeId, FileId128::FromNtfs(10)}, L"D:");
    Check(cyclePath.has_value(), "ReconstructPath terminates and returns a value even given a fabricated parent cycle");

    store.Close();
    DeleteDatabaseFiles(path);
}

// task 9.3/9.6 (store half): volume unavailability retains entries, and
// reconciliation both removes a stale entry and adds a missed one without
// touching the rest of the volume's data.
void TestVolumeLifecycleAndReconciliation() {
    const std::wstring path = TestDatabasePath(L"ffindexstore_test_lifecycle.db");
    DeleteDatabaseFiles(path);

    IndexStore store;
    Check(store.Open(path), "lifecycle test database opens");
    DurableVolumeId volumeId = store.ResolveVolume(VolumeIdentity{L"{vol-3}", 3}, L'E', 1000);

    IngestEntry root = MakeEntry(volumeId, 5, 5, u"", 0, kFileAttributeDirectory);
    IngestEntry keep = MakeEntry(volumeId, 20, 5, u"keep.txt", 1);
    IngestEntry stale = MakeEntry(volumeId, 21, 5, u"deleted-while-offline.txt", 2);
    Check(store.IngestBatch(volumeId, {root, keep, stale}, [] {}), "initial batch ingests");

    store.MarkAbsentVolumesUnavailable({}, 2000); // this volume is not in "still present" -> unavailable
    auto record = store.Store().GetVolume(volumeId);
    Check(record.has_value() && !record->available, "volume marked unavailable");
    Check(store.View().EntryCount() == 3, "marking unavailable does not delete entries");

    // Reappearance is driven by ResolveVolume being called again for a
    // volume the engine sees in a fresh EnumerateVolumes response
    // (IngestionPipeline::HandleVolumeList) -- MarkAbsentVolumesUnavailable
    // only ever handles the opposite (disappearance) direction.
    store.ResolveVolume(VolumeIdentity{L"{vol-3}", 3}, L'E', 3000);
    record = store.Store().GetVolume(volumeId);
    Check(record.has_value() && record->available, "volume marked available again via ResolveVolume on reappearance");

    // Reconciliation: ground truth omits `stale` (deleted while offline)
    // and adds a new file never ingested (missed create).
    IngestEntry missedCreate = MakeEntry(volumeId, 22, 5, u"created-while-offline.txt", 3);
    size_t touched = store.ReconcileVolume(volumeId, {root, keep, missedCreate}, 4000, [] {});
    Check(touched > 0, "reconciliation reports having touched entries");
    Check(!store.View().TryGet(EntryKey{volumeId, FileId128::FromNtfs(21)}).has_value(),
          "reconciliation removes an entry with no ground-truth counterpart (missed delete)");
    Check(store.View().TryGet(EntryKey{volumeId, FileId128::FromNtfs(22)}).has_value(),
          "reconciliation adds an entry present in ground truth but missing from the index (missed create)");
    Check(store.View().TryGet(EntryKey{volumeId, FileId128::FromNtfs(20)}).has_value(),
          "reconciliation leaves an unaffected entry alone");

    store.Close();
    DeleteDatabaseFiles(path);
}

// task 9.2: an initial scan interrupted partway through resumes from its
// persisted cursor rather than from zero.
void TestResumableScanCursor() {
    const std::wstring path = TestDatabasePath(L"ffindexstore_test_resume.db");
    DeleteDatabaseFiles(path);

    IndexStore store;
    Check(store.Open(path), "resume test database opens");
    DurableVolumeId volumeId = store.ResolveVolume(VolumeIdentity{L"{vol-4}", 4}, L'F', 1000);

    Check(!store.Store().GetVolume(volumeId)->scanComplete, "a newly-resolved volume starts with scanComplete = false");

    std::vector<uint8_t> cursor{1, 2, 3, 4};
    store.Store().SetScanCursor(volumeId, cursor);
    auto record = store.Store().GetVolume(volumeId);
    Check(record.has_value() && record->scanCursor == cursor, "scan cursor persists across a GetVolume read");

    store.Store().SetScanComplete(volumeId, true);
    store.Store().ClearScanCursor(volumeId);
    record = store.Store().GetVolume(volumeId);
    Check(record->scanComplete, "scan-complete flag persists");
    Check(record->scanCursor.empty(), "cursor is cleared once the scan completes");

    store.Close();
    DeleteDatabaseFiles(path);
}

// task 7.4: forgetting a volume is an explicit, separate action from
// automatic disconnect handling -- it actually removes durable rows and
// projection entries, unlike MarkAbsentVolumesUnavailable.
void TestForgetVolume() {
    const std::wstring path = TestDatabasePath(L"ffindexstore_test_forget.db");
    DeleteDatabaseFiles(path);

    IndexStore store;
    Check(store.Open(path), "forget-volume test database opens");
    DurableVolumeId volumeId = store.ResolveVolume(VolumeIdentity{L"{vol-5}", 5}, L'G', 1000);
    IngestEntry root = MakeEntry(volumeId, 5, 5, u"", 0, kFileAttributeDirectory);
    IngestEntry file = MakeEntry(volumeId, 6, 5, u"a.txt", 1);
    Check(store.IngestBatch(volumeId, {root, file}, [] {}), "entries ingest before forgetting");

    Check(store.ForgetVolume(volumeId), "ForgetVolume succeeds");
    Check(store.View().EntryCount() == 0, "ForgetVolume removes the volume's entries from the projection");
    Check(!store.Store().GetVolume(volumeId).has_value(), "ForgetVolume removes the volume's row from the durable store");

    store.Close();
    DeleteDatabaseFiles(path);
}

// task 7.5/7.6: journal resume is only safe when the reported identity
// matches what was persisted; a mismatch flags the volume for
// reconciliation instead of a blind resume, and the explicit
// "out-of-range on actual read" signal (MarkNeedsReconciliation) does the
// same for the case that can only be discovered by attempting the read.
void TestJournalResumeDecisions() {
    const std::wstring path = TestDatabasePath(L"ffindexstore_test_journal.db");
    DeleteDatabaseFiles(path);

    IndexStore store;
    Check(store.Open(path), "journal-resume test database opens");
    DurableVolumeId volumeId = store.ResolveVolume(VolumeIdentity{L"{vol-6}", 6}, L'H', 1000);

    uint64_t resumeUsn = 0;
    Check(!store.TryResumeJournal(volumeId, 42, resumeUsn), "no persisted journal state yet -- resume is correctly refused");

    store.Store().SetJournalId(volumeId, 100);
    store.Store().SetResumeUsn(volumeId, 5000);
    Check(store.TryResumeJournal(volumeId, 100, resumeUsn) && resumeUsn == 5000,
          "matching journal identity resumes from the persisted USN");

    Check(!store.TryResumeJournal(volumeId, 999, resumeUsn),
          "mismatched journal identity (deleted/recreated journal) refuses to resume");
    auto record = store.Store().GetVolume(volumeId);
    Check(record.has_value() && record->needsReconciliation,
          "a journal-identity mismatch flags the volume for reconciliation (D6/task 7.6)");

    store.Store().SetNeedsReconciliation(volumeId, false);
    store.MarkNeedsReconciliation(volumeId);
    record = store.Store().GetVolume(volumeId);
    Check(record.has_value() && record->needsReconciliation,
          "MarkNeedsReconciliation flags the volume when an out-of-range resume position is discovered mid-read");

    store.Close();
    DeleteDatabaseFiles(path);
}

} // namespace

int main() {
    TestCrashRecoveryViaWalReplay();
    TestCycleSafety();
    TestVolumeLifecycleAndReconciliation();
    TestResumableScanCursor();
    TestForgetVolume();
    TestJournalResumeDecisions();

    if (g_failures > 0) {
        std::fprintf(stderr, "\n%d check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("\nall checks passed\n");
    return EXIT_SUCCESS;
}
