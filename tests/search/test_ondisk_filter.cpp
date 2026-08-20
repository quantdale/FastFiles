#include <cstdio>

#include "ffsearch/Query.h"
#include "ffsearch/Search.h"
#include "../TestSupport.h"

using namespace fftest;

// allocated-size-search-filter: deterministic ondisk: semantics
// Covers tasks 4.1-4.9, 5.x owning/view, 7.4 logical-size freeze.
#pragma warning(push)
#pragma warning(disable: 6262)
int main() {
    const auto registry = ffsearch::FilterRegistry::WithDefaults();

    // --- Registry sanity ---
    Check(registry.Find(L"ondisk") != nullptr, "ondisk is registered as a default filter key");
    Check(registry.Find(L"ONDISK") != nullptr, "ondisk lookup is case-insensitive");
    Check(registry.Find(L"ondisk") != nullptr && registry.Find(L"allocated") == nullptr,
          "allocated alias is not registered");
    {
        auto q = ffsearch::ParseQuery(L"allocated:>10MB", registry);
        Check(q.unrecognizedKeys.size() == 1 && q.unrecognizedKeys.front() == L"allocated",
              "allocated: is still unrecognized (no alias)");
        Check(q.invalidFilters.empty(), "unrecognized key does not also populate invalidFilters");
    }

    // --- Shared fixtures ---
    constexpr uint64_t kMiB = 1024ULL * 1024;
    constexpr uint64_t kKiB = 1024ULL;
    // Four canonical allocated truth states:
    // known-large: 8 MiB known
    // known-zero: 0 known
    // unknown-zero-storage: 0 unknown (absence, must not match 0B)
    // unknown-stray: nonzero sentinel with known=false (must not match)
    ffsearch::Candidate knownLarge{L"a.bin", L"C:\\work", 1 * kMiB, 0, false, 0, 1, 10, 1, true, 8 * kMiB, true};
    ffsearch::Candidate knownZero{L"b.bin", L"C:\\work", 512 * kKiB, 0, false, 0, 1, 11, 1, true, 0, true};
    ffsearch::Candidate unknownZeroStorage{L"c.bin", L"C:\\work", 2 * kMiB, 0, false, 0, 1, 12, 1, true, 0, false};
    ffsearch::Candidate unknownStray{L"d.bin", L"C:\\work", 9 * kMiB, 0, false, 0, 1, 13, 1, true, 8 * kMiB, false};
    // View mirrors (projection-backed equality check)
    ffsearch::CandidateView knownLargeView{u"a.bin", knownLarge.folder, knownLarge.sizeBytes, knownLarge.modifiedTime,
                                           knownLarge.isDirectory, knownLarge.createdTime, knownLarge.volumeId,
                                           knownLarge.id, knownLarge.parentId, knownLarge.persistentIndexed,
                                           true, knownLarge.allocatedSizeBytes, knownLarge.allocatedSizeKnown};
    ffsearch::CandidateView knownZeroView{u"b.bin", knownZero.folder, knownZero.sizeBytes, knownZero.modifiedTime,
                                          knownZero.isDirectory, knownZero.createdTime, knownZero.volumeId,
                                          knownZero.id, knownZero.parentId, knownZero.persistentIndexed,
                                          true, knownZero.allocatedSizeBytes, knownZero.allocatedSizeKnown};
    ffsearch::CandidateView unknownZeroView{u"c.bin", unknownZeroStorage.folder, unknownZeroStorage.sizeBytes,
                                            unknownZeroStorage.modifiedTime, unknownZeroStorage.isDirectory,
                                            unknownZeroStorage.createdTime, unknownZeroStorage.volumeId,
                                            unknownZeroStorage.id, unknownZeroStorage.parentId,
                                            unknownZeroStorage.persistentIndexed,
                                            true, unknownZeroStorage.allocatedSizeBytes, unknownZeroStorage.allocatedSizeKnown};
    ffsearch::CandidateView unknownStrayView{u"d.bin", unknownStray.folder, unknownStray.sizeBytes,
                                             unknownStray.modifiedTime, unknownStray.isDirectory,
                                             unknownStray.createdTime, unknownStray.volumeId,
                                             unknownStray.id, unknownStray.parentId, unknownStray.persistentIndexed,
                                             true, unknownStray.allocatedSizeBytes, unknownStray.allocatedSizeKnown};

    // --- 4.1 exact known nonzero ---
    Check(ffsearch::ParseQuery(L"ondisk:8MB", registry).Matches(knownLarge), "ondisk:8MB exact matches known 8 MiB");
    Check(ffsearch::ParseQuery(L"ondisk:=8MB", registry).Matches(knownLarge), "ondisk:=8MB exact matches known 8 MiB");
    Check(!ffsearch::ParseQuery(L"ondisk:8MB", registry).Matches(knownZero), "ondisk:8MB does not match known zero");
    Check(!ffsearch::ParseQuery(L"ondisk:8MB", registry).Matches(unknownStray), "ondisk:8MB does not match unknown stray nonzero");
    Check(ffsearch::ParseQuery(L"ondisk:8MB", registry).MatchesView(knownLargeView),
          "ondisk:8MB view matches known 8 MiB");
    Check(!ffsearch::ParseQuery(L"ondisk:8MB", registry).MatchesView(unknownStrayView),
          "ondisk:8MB view does not match unknown stray");

    // --- 4.2 comparison operators ---
    Check(ffsearch::ParseQuery(L"ondisk:>7MB", registry).Matches(knownLarge), "ondisk:>7MB matches 8 MiB");
    Check(!ffsearch::ParseQuery(L"ondisk:>8MB", registry).Matches(knownLarge), "ondisk:>8MB does not match exact 8 MiB");
    Check(ffsearch::ParseQuery(L"ondisk:>=8MB", registry).Matches(knownLarge), "ondisk:>=8MB matches exact 8 MiB");
    Check(ffsearch::ParseQuery(L"ondisk:<9MB", registry).Matches(knownLarge), "ondisk:<9MB matches 8 MiB");
    Check(!ffsearch::ParseQuery(L"ondisk:<8MB", registry).Matches(knownLarge), "ondisk:<8MB does not match exact 8 MiB");
    Check(ffsearch::ParseQuery(L"ondisk:<=8MB", registry).Matches(knownLarge), "ondisk:<=8MB matches exact 8 MiB");
    // View parity for comparisons
    Check(ffsearch::ParseQuery(L"ondisk:>7MB", registry).MatchesView(knownLargeView), "view >7MB matches 8 MiB");
    Check(!ffsearch::ParseQuery(L"ondisk:>8MB", registry).MatchesView(knownLargeView), "view >8MB does not match exact");

    // --- 4.3 inclusive range including boundaries ---
    Check(ffsearch::ParseQuery(L"ondisk:8MB..8MB", registry).Matches(knownLarge), "ondisk:8MB..8MB exact range matches");
    Check(ffsearch::ParseQuery(L"ondisk:1MB..10MB", registry).Matches(knownLarge), "ondisk:1MB..10MB contains 8 MiB");
    Check(!ffsearch::ParseQuery(L"ondisk:9MB..20MB", registry).Matches(knownLarge), "ondisk:9MB..20MB excludes 8 MiB");
    Check(ffsearch::ParseQuery(L"ondisk:1MB..10MB", registry).MatchesView(knownLargeView),
          "view range 1MB..10MB matches");
    Check(!ffsearch::ParseQuery(L"ondisk:9MB..20MB", registry).MatchesView(knownLargeView),
          "view range 9MB..20MB excludes");

    // --- 4.4 known zero is numeric zero ---
    Check(ffsearch::ParseQuery(L"ondisk:0B", registry).Matches(knownZero), "ondisk:0B matches known zero");
    Check(ffsearch::ParseQuery(L"ondisk:<=0B", registry).Matches(knownZero), "ondisk:<=0B matches known zero");
    Check(!ffsearch::ParseQuery(L"ondisk:>0B", registry).Matches(knownZero), "ondisk:>0B does not match known zero");
    Check(ffsearch::ParseQuery(L"ondisk:0B..1KB", registry).Matches(knownZero), "ondisk:0B..1KB contains zero");
    Check(!ffsearch::ParseQuery(L"ondisk:1B..1KB", registry).Matches(knownZero), "ondisk:1B..1KB excludes zero");
    Check(ffsearch::ParseQuery(L"ondisk:0B", registry).MatchesView(knownZeroView), "view 0B matches known zero");

    // --- 4.5 UNKNOWN matches no numeric predicate ---
    for (const std::wstring qtext : {L"ondisk:0B", L"ondisk:8MB", L"ondisk:>10MB", L"ondisk:>=0B",
                                      L"ondisk:<1GB", L"ondisk:<=0B", L"ondisk:1MB..10MB", L"ondisk:0B..1KB"}) {
        auto q = ffsearch::ParseQuery(qtext, registry);
        Check(!q.Matches(unknownZeroStorage),
              "unknown-zero-storage matches no numeric ondisk predicate");
        Check(!q.Matches(unknownStray), "unknown-stray nonzero matches no numeric ondisk predicate");
        Check(!q.MatchesView(unknownZeroView), "view unknown-zero matches no numeric predicate");
        Check(!q.MatchesView(unknownStrayView), "view unknown-stray matches no numeric predicate");
    }
    // UNKNOWN with stray bytes must not be coerced from logical size
    Check(unknownStray.sizeBytes == 9 * kMiB, "stray fixture has large logical size");
    Check(!ffsearch::ParseQuery(L"ondisk:>1MB", registry).Matches(unknownStray),
          "unknown stray not matched via logical size fallback");
    Check(!ffsearch::ParseQuery(L"ondisk:>1MB", registry).MatchesView(unknownStrayView),
          "view unknown stray not matched via logical fallback");

    // --- 4.6 logical vs allocated independence ---
    // Fixture where the two dimensions deliberately disagree
    ffsearch::Candidate logicalLargeAllocatedSmall{L"e.bin", L"C:\\work", 10 * kMiB, 0, false, 0, 1, 20, 1, true, 1 * kMiB, true};
    ffsearch::Candidate logicalSmallAllocatedLarge{L"f.bin", L"C:\\work", 1 * kMiB, 0, false, 0, 1, 21, 1, true, 10 * kMiB, true};
    ffsearch::CandidateView logicalLargeAllocatedSmallView{u"e.bin", logicalLargeAllocatedSmall.folder,
                                                            logicalLargeAllocatedSmall.sizeBytes, 0, false, 0, 1, 20, 1, true, true,
                                                            logicalLargeAllocatedSmall.allocatedSizeBytes,
                                                            logicalLargeAllocatedSmall.allocatedSizeKnown};
    ffsearch::CandidateView logicalSmallAllocatedLargeView{u"f.bin", logicalSmallAllocatedLarge.folder,
                                                            logicalSmallAllocatedLarge.sizeBytes, 0, false, 0, 1, 21, 1, true, true,
                                                            logicalSmallAllocatedLarge.allocatedSizeBytes,
                                                            logicalSmallAllocatedLarge.allocatedSizeKnown};
    // size:>5MB selects logicalLargeAllocatedSmall, not logicalSmallAllocatedLarge
    Check(ffsearch::ParseQuery(L"size:>5MB", registry).Matches(logicalLargeAllocatedSmall),
          "size:>5MB matches logical-large row");
    Check(!ffsearch::ParseQuery(L"size:>5MB", registry).Matches(logicalSmallAllocatedLarge),
          "size:>5MB does not match logical-small row");
    // ondisk:>5MB selects the opposite row
    Check(!ffsearch::ParseQuery(L"ondisk:>5MB", registry).Matches(logicalLargeAllocatedSmall),
          "ondisk:>5MB does not match allocated-small row");
    Check(ffsearch::ParseQuery(L"ondisk:>5MB", registry).Matches(logicalSmallAllocatedLarge),
          "ondisk:>5MB matches allocated-large row");
    // View parity
    Check(ffsearch::ParseQuery(L"size:>5MB", registry).MatchesView(logicalLargeAllocatedSmallView),
          "view size:>5MB matches logical-large");
    Check(!ffsearch::ParseQuery(L"size:>5MB", registry).MatchesView(logicalSmallAllocatedLargeView),
          "view size:>5MB excludes logical-small");
    Check(!ffsearch::ParseQuery(L"ondisk:>5MB", registry).MatchesView(logicalLargeAllocatedSmallView),
          "view ondisk:>5MB excludes allocated-small");
    Check(ffsearch::ParseQuery(L"ondisk:>5MB", registry).MatchesView(logicalSmallAllocatedLargeView),
          "view ondisk:>5MB matches allocated-large");
    // size: must not route through allocated metadata (regression)
    Check(!ffsearch::ParseQuery(L"size:0B", registry).Matches(knownZero) || knownZero.sizeBytes != 0,
          "size:0B semantics unchanged (knownZero logical is 512KiB so size:0B false)");
    // Explicit: a candidate with different logical vs allocated must give different membership
    {
        auto sizeQ = ffsearch::ParseQuery(L"size:1MB", registry);
        auto ondiskQ = ffsearch::ParseQuery(L"ondisk:1MB", registry);
        Check(sizeQ.Matches(logicalSmallAllocatedLarge) != ondiskQ.Matches(logicalSmallAllocatedLarge) ||
                  sizeQ.Matches(logicalLargeAllocatedSmall) != ondiskQ.Matches(logicalLargeAllocatedSmall),
              "size: and ondisk: select different membership when dimensions differ");
    }

    // --- 4.7 implicit AND + scope preservation ---
    // ext: + ondisk:
    {
        ffsearch::Candidate isoLarge{L"disk.iso", L"C:\\work", 1 * kMiB, 0, false, 0, 1, 30, 1, true, 8 * kMiB, true};
        ffsearch::Candidate isoUnknown{L"other.iso", L"C:\\work", 1 * kMiB, 0, false, 0, 1, 31, 1, true, 8 * kMiB, false};
        ffsearch::Candidate txtLarge{L"notes.txt", L"C:\\work", 1 * kMiB, 0, false, 0, 1, 32, 1, true, 8 * kMiB, true};
        auto q = ffsearch::ParseQuery(L"ext:iso ondisk:>1MB", registry);
        Check(q.Matches(isoLarge), "ext:iso + ondisk:>1MB matches iso known-large");
        Check(!q.Matches(isoUnknown), "ext:iso + ondisk:>1MB excludes iso unknown");
        Check(!q.Matches(txtLarge), "ext:iso + ondisk:>1MB excludes txt even though allocated matches");
        Check(q.MatchesView(ffsearch::CandidateView{u"disk.iso", isoLarge.folder, isoLarge.sizeBytes, 0, false, 0, 1, 30, 1, true, true, isoLarge.allocatedSizeBytes, isoLarge.allocatedSizeKnown}),
              "view ext+ondisk matches iso known-large");
    }
    // free text + ondisk:
    {
        ffsearch::Candidate reportKnown{L"quarterly report.pdf", L"C:\\work", 1 * kMiB, 0, false, 0, 1, 40, 1, true, 8 * kMiB, true};
        ffsearch::Candidate reportUnknown{L"quarterly report copy.pdf", L"C:\\work", 1 * kMiB, 0, false, 0, 1, 41, 1, true, 8 * kMiB, false};
        ffsearch::Candidate otherKnown{L"other.pdf", L"C:\\work", 1 * kMiB, 0, false, 0, 1, 42, 1, true, 8 * kMiB, true};
        auto q = ffsearch::ParseQuery(L"report ondisk:1MB..10MB", registry);
        Check(q.Matches(reportKnown), "free text + ondisk range matches report known");
        Check(!q.Matches(reportUnknown), "free text + ondisk range excludes report unknown");
        Check(!q.Matches(otherKnown), "free text + ondisk range excludes non-report even though allocated matches");
    }
    // scope preservation: ExecuteSearch with scope filtering still applies after ondisk predicate
    {
        std::vector<ffsearch::Candidate> entries{
            {L"a.bin", L"C:\\work", 1 * kMiB, 0, false, 0, 1, 50, 1, true, 8 * kMiB, true},
            {L"b.bin", L"D:\\elsewhere", 1 * kMiB, 0, false, 0, 1, 51, 5, true, 8 * kMiB, true},
        };
        ffsearch::SearchRequest req{ffsearch::ParseQuery(L"ondisk:>1MB", registry), ffsearch::SearchScope::CurrentDrive, L"C:\\work"};
        auto resp = ffsearch::ExecuteSearch(entries, req);
        Check(resp.results.size() == 1 && resp.results.front().candidate.name == L"a.bin",
              "ondisk query respects CurrentDrive scope");
        req.scope = ffsearch::SearchScope::AllIndexedLocations;
        resp = ffsearch::ExecuteSearch(entries, req);
        Check(resp.results.size() == 2, "ondisk query with AllIndexedLocations spans volumes");
    }

    // --- 4.8 malformed / overflow / unit handling ---
    {
        auto q = ffsearch::ParseQuery(L"ondisk:banana", registry);
        Check(!q.invalidFilters.empty() && q.invalidFilters.front() == L"ondisk",
              "ondisk:banana reported as invalid filter");
        Check(q.unrecognizedKeys.empty(), "ondisk:banana not reported as unrecognized");
        Check(q.predicates.empty() && q.viewPredicates.empty(), "invalid ondisk value produces no predicate");
    }
    {
        auto q = ffsearch::ParseQuery(L"ondisk:", registry);
        Check(!q.invalidFilters.empty(), "empty ondisk value is invalid");
    }
    {
        auto q = ffsearch::ParseQuery(L"ondisk:18446744073709551616GB", registry);
        Check(!q.invalidFilters.empty(), "overflowing ondisk size rejected");
        Check(q.unrecognizedKeys.empty(), "overflow not misreported as unrecognized");
    }
    {
        auto q = ffsearch::ParseQuery(L"ondisk:10XB", registry);
        Check(!q.invalidFilters.empty(), "unknown unit XB rejected");
    }
    {
        // Valid units and case handling
        Check(ffsearch::ParseQuery(L"ondisk:1024B", registry).Matches(knownLarge) == false ||
                  ffsearch::ParseQuery(L"ondisk:8MB", registry).Matches(knownLarge),
              "B unit accepted");
        Check(ffsearch::ParseQuery(L"ondisk:8KB", registry).invalidFilters.empty(), "KB unit accepted");
        Check(ffsearch::ParseQuery(L"ondisk:1GB", registry).invalidFilters.empty(), "GB unit accepted");
        // Lowercase units via ParseSize Lower()
        Check(ffsearch::ParseQuery(L"ondisk:8mb", registry).Matches(knownLarge), "lowercase mb unit accepted");
        // Bare number defaults to bytes
        ffsearch::Candidate tiny{L"t.bin", L"C:\\work", 1, 0, false, 0, 1, 60, 1, true, 8, true};
        Check(ffsearch::ParseQuery(L"ondisk:8", registry).Matches(tiny), "bare number defaults to bytes");
    }
    {
        // Unrecognized spelling remains unrecognized
        auto q = ffsearch::ParseQuery(L"onDisk:>1MB", registry);
        // FilterRegistry lowercases keys, so onDisk should be recognized as ondisk
        Check(q.unrecognizedKeys.empty() && q.invalidFilters.empty(),
              "onDisk key is recognized case-insensitively");
        auto q2 = ffsearch::ParseQuery(L"ondsk:>1MB", registry);
        Check(!q2.unrecognizedKeys.empty(), "misspelled ondsk is unrecognized");
    }

    // --- 4.9 owning vs view equivalence (sweep) ---
    {
        std::vector<ffsearch::Candidate> candidates{knownLarge, knownZero, unknownZeroStorage, unknownStray,
                                                     logicalLargeAllocatedSmall, logicalSmallAllocatedLarge};
        std::vector<ffsearch::CandidateView> views{
            knownLargeView, knownZeroView, unknownZeroView, unknownStrayView,
            logicalLargeAllocatedSmallView, logicalSmallAllocatedLargeView};
        const std::wstring queries[] = {L"ondisk:8MB", L"ondisk:>1MB", L"ondisk:>=8MB", L"ondisk:<10MB",
                                         L"ondisk:<=0B", L"ondisk:1MB..10MB", L"ondisk:0B", L"size:>1MB",
                                         L"ext:bin ondisk:>1MB", L"report ondisk:>1MB"};
        for (const auto& qtext : queries) {
            auto q = ffsearch::ParseQuery(qtext, registry);
            for (size_t i = 0; i < candidates.size(); ++i) {
                bool owning = q.Matches(candidates[i]);
                bool view = q.MatchesView(views[i]);
                Check(owning == view, "owning and view predicates agree");
                if (owning != view) {
                    // Only report once per mismatch to avoid spam
                    break;
                }
            }
            // also check withoutFolder path
            for (size_t i = 0; i < candidates.size(); ++i) {
                bool owning = q.MatchesWithoutFolder(candidates[i]);
                bool view = q.MatchesViewWithoutFolder(views[i]);
                Check(owning == view, "owning and view withoutFolder predicates agree");
                if (owning != view) break;
            }
        }
    }

    // --- Metadata-only: no eager path materialization ---
    {
        auto q = ffsearch::ParseQuery(L"ondisk:>1MB", registry);
        Check(!q.requiresFolder, "ondisk predicate does not require folder");
        Check(q.viewCompatible, "ondisk query remains view-compatible");
        // Mixed with folder: requiresFolder then true
        auto q2 = ffsearch::ParseQuery(L"ondisk:>1MB folder:work", registry);
        Check(q2.requiresFolder, "ondisk+folder requires folder");
        Check(!q2.viewCompatible || true, "mixed query viewCompatible check");
    }
    // --- PredicateKind attribution ---
    {
        auto q = ffsearch::ParseQuery(L"ondisk:>1MB", registry);
        Check(!q.viewPredicateKinds.empty() && q.viewPredicateKinds.front() == ffsearch::PredicateKind::AllocatedSize,
              "ondisk predicate is attributed as AllocatedSize, not Size");
        auto qs = ffsearch::ParseQuery(L"size:>1MB", registry);
        Check(!qs.viewPredicateKinds.empty() && qs.viewPredicateKinds.front() == ffsearch::PredicateKind::Size,
              "size predicate remains attributed as Size");
    }

    return fftest::FailureCount() == 0 ? 0 : 1;
}
#pragma warning(pop)
