to continue.]
read: C:/Users/Michael Roy/Documents/FastFiles/build/worktrees/...
grep: C:/Users/Michael Roy/Documents/FastFiles/build/worktrees/...
        } catch (...) {
            InvalidateFilenameIndex();
        }
    }
    bulkScanActive_ = active;
}
#ifdef FF_INDEXSTORE_TESTING
void Projection::SetTestingFoldExceptionalUnit(const char16_t unit, const bool exceptional) {
    // large-index-lifecycle-and-search-scalability review-remediation (P2-A):
    // testing-only fold-exceptional override; process-wide and idempotent.
    gTestingForceFoldExceptional[static_cast<uint16_t>(unit)].store(
        exceptional ? 1 : 0, std::memory_order_release);
}
#endif
size_t Projection::EstimatedMemoryBytes() const noexcept {
    size_t result = entries_.capacity() * sizeof(ProjectionEntry)
        + freeList_.capacity() * sizeof(uint32_t)
        + idToIndex_.MemoryUsageBytes()
        + parentToChildren_.MemoryUsageBytes()
        + namePool_.MemoryUsageBytes()
        + filenameExceptionalEntries_.capacity() * sizeof(uint32_t)
        + lastAccessTimes_.capacity() * sizeof(uint64_t);
    for (const auto& [trigram, posting] : filenameTrigramPostings_) {
        (void)trigram;
        result += sizeof(uint64_t) + sizeof(std::vector<uint32_t>)
            + posting.capacity() * sizeof(uint32_t);
    }
    result += filenameTrigramPostings_.bucket_count() * sizeof(void*);
[1073 more lines in file. Use offset=188 to continue.]
test_projection.cpp-1179-     Check(!OrdinalUnitEqual(u'\uFB01', u'f'),
test_projection.cpp-1180-           "campaign host: fi ligature is NOT equal to f under ordinal ignore-case");
test_projection.cpp-1181- }
test_projection.cpp-1182- 
test_projection.cpp-1183- // review-remediation (P2-A), RED-FIRST: force the host table to exhibit the
test_projection.cpp-1184- // exact equivalence the PR review assumed (U+017F LONG S folds to ASCII 'S').
test_projection.cpp-1185- // The pre-fix intersection-only accelerator omits the long-s row for the
test_projection.cpp-1186- // ASCII needle "soo" (its ASCII fold cannot contain the 'soo' trigram) --
test_projection.cpp-1187- // a data-retrieval false negative. The fold-exceptional merge must retain it.
test_projection.cpp-1188- void TestFilenameCandidatesRetainHostFoldExceptionalNames() {
test_projection.cpp:1189:     Projection::SetTestingFoldExceptionalUnit(u'\u017F', true);
test_projection.cpp-1190-     {
test_projection.cpp-1191-         Projection proj;
test_projection.cpp-1192-         proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
test_projection.cpp-1193-         proj.Upsert(1, MakeEntry(100, 5, u"\u017Foo.txt", 0));
test_projection.cpp-1194-         proj.Upsert(1, MakeEntry(101, 5, u"control.txt", 0));
test_projection.cpp-1195-         proj.Upsert(1, MakeEntry(102, 5, u"soo.txt", 0));
test_projection.cpp-1196-         const auto candidates = proj.FilenameCandidateIndices(u"soo");
test_projection.cpp-1197-         Check(candidates.has_value(), "eligible ASCII query still uses the accelerator");
test_projection.cpp-1198-         if (candidates.has_value()) {
test_projection.cpp-1199-             Check(CandidateContains(proj, *candidates, 100),
test_projection.cpp-1207-         proj.Upsert(1, MakeEntry(100, 5, u"foo.txt", 0));
test_projection.cpp-1208-         const auto afterRename = proj.FilenameCandidateIndices(u"soo");
test_projection.cpp-1209-         Check(afterRename.has_value() && !CandidateContains(proj, *afterRename, 100),
test_projection.cpp-1210-               "renaming away the exceptional unit removes the row from the exceptional merge");
test_projection.cpp-1211-         // Deletion removes rows from the exceptional merge exactly like postings.
test_projection.cpp-1212-         proj.Remove(1, FileId{102, 0});
test_projection.cpp-1213-         const auto afterRemove = proj.FilenameCandidateIndices(u"soo");
test_projection.cpp-1214-         Check(afterRemove.has_value() && !CandidateContains(proj, *afterRemove, 102),
test_projection.cpp-1215-               "deleting a row removes it from candidates");
test_projection.cpp-1216-     }
test_projection.cpp:1217:     Projection::SetTestingFoldExceptionalUnit(u'\u017F', false);
test_projection.cpp-1218- }
test_projection.cpp-1219- 
test_projection.cpp-1220- // review-remediation (P2-A): surrogate-bearing (astral) names are always
test_projection.cpp-1221- // fold-exceptional (conservative: pair semantics cannot be probed unit-wise
test_projection.cpp-1222- // and false negatives are forbidden), so they are retained in every ASCII
test_projection.cpp-1223- // candidate set even when their ASCII fold cannot contain the needle. RED
test_projection.cpp-1224- // against the pre-fix intersection-only accelerator: for a needle absent
test_projection.cpp-1225- // from every name its candidate set is empty, so the conservative row was
test_projection.cpp-1226- // silently dropped.
test_projection.cpp-1227- void TestFilenameCandidatesRetainSurrogateNamesConservatively() {
