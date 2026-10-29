Task: READ-ONLY research lane D (FastFiles repo; work ONLY read-only, no shell, no edits) — Query planner and correctness reference design. Repo: C:/Users/Michael Roy/Documents/FastFiles. Map the full search path: (1) canonical matcher (CompareStringOrdinal semantics — where invoked, exact flags), the hot planner (Projection::FilenameCandidateIndices + eligibility gates + kMaximumFilenameIndexCandidates cap), the cold planner (SQLite stream + ForEachEntryWithAsciiNameUntil prefilter + fallback), query classification (12 classes in the bench harness — find how classes map to code paths), top-N admission and sorting semantics, stale-query cancellation (epochs/generations), and fallback behavior; (2) where a durable candidate generator would slot in (the seam), and what the cost-based selection rule must consider (needle length, candidate count, posting size, index validity, volume residency, result limit, sorting); (3) OWN THE REFERENCE DIFFERENTIAL-TEST DESIGN: specify the test suite that proves CanonicalMatches ⊆ CandidateSet and indexed-vs-canonical identity+ordering equality across: long-s/Kelvin/dotless-i exceptional mappings (host-derived fold-exceptional seam SetTestingFoldExceptionalUnit), surrogates, Unicode, mixed ASCII/non-ASCII, dotted names, repeated trigrams, very short literals, path scopes, extension queries, sorting, volume roots, capped top-N, ties, cancellation, stale index state. Existing tests to extend: tests/indexstore/test_projection.cpp (TestFilenameCandidatesAreConservativeSuperset family), ffengine_index_pipeline_tests hot/cold identity suites — find them. Deliver: the path map with file:line, the seam recommendation, and the concrete differential test plan (names, cases, red-first strategy).
grep: C:/Users/Michael Roy/Documents/FastFiles
grep: C:/Users/Michael Roy/Documents/FastFiles
docs/benchmarks/large-index-scalability.md:400: This follow-up slice measured and optimized only failed direct literal filename matching. The old simple-literal path pre-folded an ASCII query once, selected a rare anchor, scanned each ASCII-compatible name with `u16string_view::find` plus folded-view equality, and used a per-window `CompareStringOrdinal(..., TRUE)` fallback for uppercase/non-ASCII cases. It already rejected a needle longer than the candidate name before searching.
docs/agent/WORKSTATE.md:41: - **P2-A GROUND TRUTH (probe, `build/campaign/probe_ordinal/`):** `CompareStringOrdinal(...,TRUE)` on this host folds NON-ASCII case pairs (É=é CSTR_EQUAL) but **never** equates U+017F/Kelvin(U+212A)/dotless-i(U+0131)/dotted-I(U+0130)/ß/ﬁ/µ/Ａ with ASCII (all CSTR_GREATER); exhaustive scan of all 65,280 non-ASCII BMP units × 128 ASCII units = **zero** cross-ASCII fold classes; surrogate units/pairs never fold to ASCII. **The review's concrete example is empirically disproven on this host** (`ſoo.... [truncated]
docs/agent/WORKSTATE.md:42: - **P2-A REPAIR (Option A, host-derived exceptional fallback):** `Projection.cpp` now classifies a row fold-exceptional iff it contains a unit that the HOST `CompareStringOrdinal` equates with any ASCII unit (memoized 65,536-slot probe, fail-closed on API error) or any surrogate unit (conservative); `FilenameCandidateIndices` merges ONLY those rows into the posting intersection (restores superset invariant for ANY host table; empty set on this host = zero cost, accelerator speedups preserved). R... [truncated]
docs/agent/WORKSTATE.md:43: - **P2-A TESTS (all in `tests/indexstore/test_projection.cpp`):** `TestHostOrdinalFoldSemanticsMatchCampaignMeasurement` (tripwire: fails loudly if a future Windows table changes fold verdicts), `TestFilenameCandidatesRetainHostFoldExceptionalNames` (seam-injected ſ→S; RED-first: fails against intersection-only), `TestFilenameCandidatesRetainSurrogateNamesConservatively` (RED-first: fails against intersection-only), `TestFilenameCandidateSupersetInvariantHoldsForAdversarialCasing` (26-pair adver... [truncated]
docs/agent/history/FASTFILES_FRONTIER_HANDOFF.md:1733: 6. `ffsearch` currently uses Windows `CompareStringOrdinal` and Win32 time
tests/search/test_ascii_fold_matcher.cpp:45:             const bool apiEqual = CompareStringOrdinal(&wl, 1, &wr, 1, TRUE) == CSTR_EQUAL;
tests/search/bench_literal_matcher.cpp:119:     return CompareStringOrdinal(reinterpret_cast<const wchar_t*>(left.data()), static_cast<int>(left.size()),
codebase-audit/sessions/2026-08-20-product-campaign/audit/search.md:70: - Suggested fix: compare with `_wcsicmp` (or `CompareStringOrdinal(..., TRUE)`), matching the case-insensitive matching semantics of the search itself.
tests/indexstore/test_projection.cpp:1038: // through a non-ASCII unit under the host's CompareStringOrdinal ordinal
tests/indexstore/test_projection.cpp:1135: // CompareStringOrdinal(..., TRUE)). The candidate-superset invariant is
tests/indexstore/test_projection.cpp:1141:         if (CompareStringOrdinal(reinterpret_cast<const wchar_t*>(name.data() + i),
tests/indexstore/test_projection.cpp:1152:     return CompareStringOrdinal(reinterpret_cast<const wchar_t*>(&a), 1,
tests/indexstore/test_projection.cpp:1159: // CompareStringOrdinal verdicts on the campaign host are NOT that. If a
src/indexstore/src/Projection.cpp:39: // the HOST's ordinal table (CompareStringOrdinal(..., TRUE)), which the
src/indexstore/src/Projection.cpp:76:             const int verdict = CompareStringOrdinal(&wide, 1, &other, 1, TRUE);
src/ui/src/FileOperations.cpp:165:     return CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()),
src/indexstore/include/ffindexstore/Projection.h:331:     // (CompareStringOrdinal ignore-case) matcher would accept: every row the
src/ui/src/NavigationWorkspace.cpp:507:             return CompareStringOrdinal(bookmark.path.data(), static_cast<int>(bookmark.path.size()),
src/ui/src/RecentFolders.h:35:     // CompareStringOrdinal(..., TRUE). Extended-length (\\?\) and UNC forms are
src/ui/src/RecentFolders.cpp:64:     return CompareStringOrdinal(na.c_str(), -1, nb.c_str(), -1, TRUE) == CSTR_EQUAL;
src/protocol/include/ffprotocol/WindowsPath.h:14:     return CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
src/protocol/include/ffprotocol/WindowsPath.h:20:         return CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
src/ui/include/ffui/SelectionIdentity.h:57:            CompareStringOrdinal(normalizedLeft.data(), static_cast<int>(normalizedLeft.size()),
openspec/changes/durable-cold-search-index-and-hybrid-residency/specs/index-engine/spec.md:15: `CompareStringOrdinal(..., TRUE)` host semantics, including rows whose names
openspec/changes/durable-cold-search-index-and-hybrid-residency/proposal.md:56:    matcher's result set under `CompareStringOrdinal(..., TRUE)` semantics on
openspec/changes/durable-cold-search-index-and-hybrid-residency/design.md:29:   `CompareStringOrdinal(..., TRUE)` host semantics (predecessor P2-A repair:
openspec/changes/instant-search/design.md:115: Filenames are already stored/interned as UTF-16 (`WCHAR`) in the in-memory projection; the search path never transcodes. Case-insensitive comparison and substring search use **ordinal** case folding (`CompareStringOrdinal`-equivalent semantics) rather than locale-sensitive linguistic casing (`CompareStringEx`/culture-aware collation) — matching Everything's own approach — so results are deterministic and don't vary by the user's Windows locale, and combining marks/accented characters are matched... [truncated]
src/search/src/Search.cpp:14:     return left.size() == right.size() && CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
src/search/src/Search.cpp:19:     return value.size() >= prefix.size() && CompareStringOrdinal(value.data(), static_cast<int>(prefix.size()),
src/search/src/Search.cpp:37:     return CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(), static_cast<int>(right.size()), TRUE) - CSTR_EQUAL;
src/search/src/Query.cpp:607: bool OrdinalContains(std::wstring_view h, std::wstring_view n) { if (n.empty()) return true; for (size_t i = 0; i + n.size() <= h.size(); ++i) if (CompareStringOrdinal(h.data() + i, static_cast<int>(n.size()), n.data(), static_cast<int>(n.size()), TRUE) == CSTR_EQUAL) return true; return false; }
src/search/src/Query.cpp:621:             if (textWidth == patternWidth && CompareStringOrdinal(text.data() + textIndex, static_cast<int>(textWidth),
src/search/src/Query.cpp:765:     return CompareStringOrdinal(reinterpret_cast<const wchar_t*>(left.data()), static_cast<int>(left.size()),
src/search/src/Path.cpp:118:     return CompareStringOrdinal(leftRoot.data(), static_cast<int>(leftRoot.size()),
src/search/src/History.cpp:28:     return CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()),
src/search/include/ffsearch/Query.h:71:     // CompareStringOrdinal call per alignment for the dominant real-world
src/search/include/ffsearch/Query.h:169: // `OrdinalContains` while avoiding per-alignment CompareStringOrdinal calls;
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/review-remediation-2026-10-21.md:14: The finding's concrete claim: `CompareStringOrdinal(..., TRUE)` equates
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/review-remediation-2026-10-21.md:37: `CompareStringOrdinal` does on this Windows build.
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/review-remediation-2026-10-21.md:44:   UTF-16 unit that the *host* `CompareStringOrdinal(..., TRUE)` equates with
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/review-remediation-2026-10-21.md:78:   `CompareStringOrdinal` span compare, capped and uncapped.
openspec/changes/quick-access-home/tasks.md:13: - [x] 2.2 Implement lexical path identity — done: trailing-`\` trim except drive roots; `CompareStringOrdinal(..., TRUE)`; no filesystem resolution; first-seen spelling kept. per design D3: trim trailing `\` except drive/share roots; case-insensitive `CompareStringOrdinal(..., TRUE)`; no filesystem resolution; first-seen spelling preserved.
openspec/changes/quick-access-home/design.md:63: - **Path identity (exact equality policy):** lexical normalization only — never touch the filesystem. Normalize: trim trailing `\` except a 2-char drive root (`C:\`) and share roots (`\\server\share`); compare case-insensitively with `CompareStringOrdinal(..., TRUE)` (existing repo convention, see `AddBookmark`). UNC and extended-length (`\\?\`, `\\?\UNC\`) paths are compared on the same lexical basis after normalization; no server/share case resolution is attempted. Persisted entries keep the f... [truncated]
[Some lines truncated to 500 chars. Use read tool to see full lines]
docs/agent/WORKSTATE.md:40: - **PR #14 live reconciliation (2026-10-21):** branch `feat/large-index-lifecycle-and-search-scalability-20261008` at `34f7ddb` = origin tip = last campaign checkpoint; `origin/main` `7f9ce9a`; 9 ahead / 0 behind; working tree carried one doc-table tweak (`evidence/explore-findings.md`). Two UNRESOLVED P2 review threads (bot `chatgpt-codex-connector`, review 5595775355): (A) `Projection::FilenameCandidateIndices` review-6.3 intersection-only candidate set may false-negative `ſoo.txt` vs `soo`; (... [truncated]
docs/agent/WORKSTATE.md:42: - **P2-A REPAIR (Option A, host-derived exceptional fallback):** `Projection.cpp` now classifies a row fold-exceptional iff it contains a unit that the HOST `CompareStringOrdinal` equates with any ASCII unit (memoized 65,536-slot probe, fail-closed on API error) or any surrogate unit (conservative); `FilenameCandidateIndices` merges ONLY those rows into the posting intersection (restores superset invariant for ANY host table; empty set on this host = zero cost, accelerator speedups preserved). R... [truncated]
docs/agent/WORKSTATE.md:50: - **Campaign results (all measured, release-test, idle host):** (1) **Repeated-rebuild pathology FIXED (31-34x):** 1M replacement cycles 158,877/158,252/164,349 ms -> 4,716/5,101/4,772 ms (at first-build cost; removal arm 465x at 200k) via `Projection::RemoveBatch` bulk removal (one wholesale accelerator invalidation + one rebuild above 256 rows); memory unchanged. (2) **Search planner FIXED (56-189x on selective classes):** the unconditional `filenameNonAsciiEntries_` union made `FilenameCandid... [truncated]
docs/agent/WORKSTATE.md:68: - **Phase B IN PROGRESS — planner root cause FOUND (source+arithmetic, pending measurement confirmation):** `FilenameCandidateIndices` (`Projection.cpp`) unconditionally `set_union`s `filenameNonAsciiEntries_` into EVERY candidate set; the fixture's ~10% Unicode rows alone are ~19.7k at 200k / ~100k at 1M — both over `kMaximumFilenameIndexCandidates = 16,384` (`IndexPipeline.h:399`) — so `merged.size() > cap` is guaranteed and every lookup returns nullopt: **`filename_index_used = false` for ALL... [truncated]
docs/agent/WORKSTATE.md:69: - **NEXT EXACT ACTION:** collect `build/campaign/phase-b-matrix.log` -> `search-matrix-before.json` (running idle: 200k done; 1M quiet phases; 2M pending). Build + run the two staged tests to confirm RED; apply the planner fix (drop the unconditional union in `FilenameCandidateIndices`, comment citing design D4 + differential evidence); confirm GREEN + full suites; re-run the search matrix after-fix and diff result counts per class (must be EXACTLY equal — the scale differential) + `filename_ind... [truncated]
tests/indexstore/test_projection.cpp:367:     const auto candidates = proj.FilenameCandidateIndices(u"component_7391");
tests/indexstore/test_projection.cpp:375:     Check(!proj.FilenameCandidateIndices(u"ph").has_value(), "two-character terms use the canonical fallback");
tests/indexstore/test_projection.cpp:376:     Check(!proj.FilenameCandidateIndices(u"\u6771\u4EAC").has_value(), "Unicode terms use the canonical fallback");
tests/indexstore/test_projection.cpp:379:     auto afterRemove = proj.FilenameCandidateIndices(u"component_7391");
tests/indexstore/test_projection.cpp:384:     auto afterRename = proj.FilenameCandidateIndices(u"component_7391");
tests/indexstore/test_projection.cpp:580:     Check(!proj.FilenameCandidateIndices(u"deferred").has_value(),
tests/indexstore/test_projection.cpp:977:     const auto referenceCandidates = reference.FilenameCandidateIndices(u"common_component_keep");
tests/indexstore/test_projection.cpp:978:     const auto bulkCandidates = bulk.FilenameCandidateIndices(u"common_component_keep");
tests/indexstore/test_projection.cpp:1006:     const auto candidates = proj.FilenameCandidateIndices(u"common_component_keep");
tests/indexstore/test_projection.cpp:1027:     Check(!proj.FilenameCandidateIndices(u"common_component_keep").has_value(),
tests/indexstore/test_projection.cpp:1085:         const auto candidates = proj.FilenameCandidateIndices(folded, 0);  // uncapped reference
tests/indexstore/test_projection.cpp:1110:     Check(!proj.FilenameCandidateIndices(u"ab", 0).has_value(),
tests/indexstore/test_projection.cpp:1112:     Check(!proj.FilenameCandidateIndices(u"\u0441\u043f\u0438\u0441\u043e\u043a", 0).has_value(),
tests/indexstore/test_projection.cpp:1121:     const auto candidates = proj.FilenameCandidateIndices(u"unique_target", 8);
tests/indexstore/test_projection.cpp:1128:     const auto missing = proj.FilenameCandidateIndices(u"zzz_missing", 8);
tests/indexstore/test_projection.cpp:1196:         const auto candidates = proj.FilenameCandidateIndices(u"soo");
tests/indexstore/test_projection.cpp:1208:         const auto afterRename = proj.FilenameCandidateIndices(u"soo");
tests/indexstore/test_projection.cpp:1213:         const auto afterRemove = proj.FilenameCandidateIndices(u"soo");
tests/indexstore/test_projection.cpp:1236:     const auto candidates = proj.FilenameCandidateIndices(u"xyz");
tests/indexstore/test_projection.cpp:1248: // FilenameCandidateIndices(query). Adversarial cross-casing corpus: long s,
tests/indexstore/test_projection.cpp:1303:         const auto candidates = proj.FilenameCandidateIndices(folded, 0);
tests/indexstore/test_projection.cpp:1311:         const auto capped = proj.FilenameCandidateIndices(folded, 4096);
tests/indexstore/test_projection.cpp:1323:         const auto candidates = proj.FilenameCandidateIndices(FoldAsciiLower(needle), 0);
tests/indexstore/test_projection.cpp:1338:     Check(proj.FilenameCandidateIndices(u"unique_target").has_value(),
tests/indexstore/test_projection.cpp:1342:     Check(!proj.FilenameCandidateIndices(u"unique_target").has_value(),
tests/indexstore/test_projection.cpp:1347:     const auto candidates = proj.FilenameCandidateIndices(u"unique_target");
openspec/changes/durable-cold-search-index-and-hybrid-residency/design.md:25:   `kMaximumFilenameIndexCandidates = 16,384` is the existing cost-based
openspec/changes/large-index-lifecycle-and-search-scalability/tasks.md:35: - [x] 6.1 Determine why `filename_index_used = false` for all measured classes. Finding: `FilenameCandidateIndices` unconditionally unions `filenameNonAsciiEntries_` into every candidate set; the 10% Unicode fixture population alone exceeds `kMaximumFilenameIndexCandidates = 16,384` at 200k+, so every lookup returns nullopt.
openspec/changes/large-index-lifecycle-and-search-scalability/tasks.md:37: - [x] 6.3 Implemented: candidate set = posting intersection alone (union removed; `243199e`), red-first via `TestFilenameCandidatesNarrowForSelectiveNeedlesDespiteUnicodeRows`. Cost-based selection = the existing cap gate (smallest posting / candidate set above `kMaximumFilenameIndexCandidates` falls back to the canonical scan; broad classes measured on the scan path).
openspec/changes/large-index-lifecycle-and-search-scalability/proposal.md:48: - **Search planner (Phase B, in progress):** `FilenameCandidateIndices` unconditionally
openspec/changes/large-index-lifecycle-and-search-scalability/proposal.md:50:   population (19.7k rows at 200k) alone exceeds `kMaximumFilenameIndexCandidates`
openspec/changes/large-index-lifecycle-and-search-scalability/design.md:47: `FilenameCandidateIndices` currently unions every non-ASCII-named row into every
openspec/changes/large-index-lifecycle-and-search-scalability/design.md:49: `kMaximumFilenameIndexCandidates` (16,384) at 200k+ rows, every lookup returns
openspec/changes/large-index-lifecycle-and-search-scalability/design.md:70: (`kMaximumFilenameIndexCandidates`): queries whose smallest posting already exceeds
openspec/changes/performance-closure-and-multisurface-acceleration/evidence/explore-findings.md:70: Search already has `FilenameCandidateIndices` for a simple literal of at least three folded characters when the query has no cold volume. A broad or short query may not take that path. The fold-free matcher remains an unproven end-to-end idea.
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/explore-findings.md:56: `FilenameCandidateIndices` unconditionally unioned `filenameNonAsciiEntries_` into
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/explore-findings.md:58: 1M) exceed `kMaximumFilenameIndexCandidates = 16,384`, so every lookup returned
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/review-remediation-2026-10-21.md:47:   unit-wise). `FilenameCandidateIndices` merges exactly those rows into the
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/review-remediation-2026-10-21.md:59:   `kMaximumFilenameIndexCandidates` fall back to the canonical scan (correct,
src/indexstore/src/Projection.cpp:947: std::optional<std::vector<uint32_t>> Projection::FilenameCandidateIndices(
src/engine/src/IndexPipeline.cpp:1503:                             context.filenameCandidates = projection_.FilenameCandidateIndices(
src/engine/src/IndexPipeline.cpp:1504:                                 searchRequest.query.simpleLiteralFolded, kMaximumFilenameIndexCandidates);
src/engine/src/IndexPipeline.h:399:     static constexpr size_t kMaximumFilenameIndexCandidates = 16 * 1024;
src/indexstore/include/ffindexstore/Projection.h:337:     std::optional<std::vector<uint32_t>> FilenameCandidateIndices(
[Some lines truncated to 500 chars. Use read tool to see full lines]
ls: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
read: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
design.md
proposal.md
specs/
tasks.md
# Design / decision record: durable-cold-search-index-and-hybrid-residency
Status: **decision record seeded during the PR #14 closure campaign; every
alternative is UNDECIDED pending the measurement phase (task group 1).** No
production implementation exists or is authorized by this document.
## 1. Bounding facts (inherited, measured)
These constrain the design space and come from the completed
`large-index-lifecycle-and-search-scalability` campaign's evidence
(`evidence/memory-cold-decision.md`, `evidence/lifecycle-reload-resources.md`):
- Projection budget: 500 MiB default, 400 MiB early-enforcement watermark;
  `SetProjectionMemoryBudget` has no production caller (tests only). A 1M-row
  projection models ~414 MB (straddles the watermark); 2M models ~839.7 MB
  (always cold under default policy).
- Cold stream floor: ~1.05-1.10 us per scanned row (name-column scan,
  decode, match). The shipped conservative SQL name prefilter
  (`ForEachEntryWithAsciiNameUntil`) removes the survivor-decode share for
  ASCII literals (1.28-1.44x at 1M, 9.8% non-ASCII names) but cannot beat
  the scan floor; non-literal classes are ineligible by design.
- Hot path: filename-trigram accelerator (folded postings + host-derived
  fold-exceptional set + surrogate-conservative set) gives 56-189x on
  eligible literal classes at 1M; candidate cap
  `kMaximumFilenameIndexCandidates = 16,384` is the existing cost-based
  fallback to canonical scan.
- Correctness contract: hot/cold identity+ordering equivalence is pinned by
  differential suites; candidate sets must be conservative supersets under
  `CompareStringOrdinal(..., TRUE)` host semantics (predecessor P2-A repair:
  fold-exceptional classification via a memoized host probe, fail-closed on
  API error).
## 2. Alternatives to measure (all currently UNDECIDED)
| # | Alternative | Sketch | Principal risk |
|---|---|---|---|
| A | SQLite FTS5 candidate table | FTS5 (trigram or unicode61) over names, queried for rowid candidates, verified by the canonical matcher | Tokenizer case-folding is NOT Windows ordinal folding; must prove superset incl. fold-exceptional + surrogate classes, or maintain an exception side-set |
| B | Durable trigram posting table | Persist the in-memory accelerator's structure (folded trigram -> sorted rowid postings + exception set) in SQLite; incremental maintenance per batch | Write amplification on ingestion; crash-consistency of postings vs entries; disk size |
| C | Hybrid hot/cold residency | Keep a compact per-volume candidate subset resident (e.g. folded-name keys or a minimal automaton) while rows stay cold | Memory bound must be strict and provable; consistency with evicted rows; predecessor marked this "separately scoped architecture work" |
| D | Covering ordinal-key index | Sorted index on the matcher's folded name key; range scans for prefix/substring classes | Substring/wildcard classes get little benefit; index size |
| E | Budget policy (adaptive/default raise) | Control arm: serve 2M hot by raising/deriving the budget | Competes with system memory; forbidden without safety proof; does not help 5M |
## 3. Mandatory acceptance dimensions (per alternative)
1. **Completeness:** candidate superset proof under host ordinal semantics,
   with red-first differential tests (fold-exceptional, surrogate, dotted,
   mixed-case, short-name shapes) — the predecessor's
   `TestFilenameCandidatesAreConservativeSuperset` family extended to the
   durable generator.
2. **Equivalence:** cold-with-index results byte-identical to hot results for
   all 12 query classes at 200k/1M/2M (and 5M for the selected candidate).
3. **Migration:** v2 -> v3 cost at 2M-5M; rollback path; crash-consistency
   (kill-during-update recovery; index never disagrees with entries);
   rebuild-on-open as the fallback recovery.
4. **Resources:** planner/index cache memory bounds; disk bytes per 1M rows;
   no change to the default 500 MiB projection budget.
5. **Cost:** ingestion/USN-batch maintenance overhead; reopen/recovery cost;
   measured planner decision quality (when index is consulted vs scan).
6. **Scale:** p50/p90/p95 at 200k/1M/2M/5M per query class, machine-readable
   artifacts with embedded source identity (predecessor lane E E-8 lesson:
   artifacts must embed `--source-sha`).
## 4. Decision log
| Date | Decision | Status |
|---|---|---|
| 2026-10-21 | Proposal + decision record created at PR #14 closure; alternatives A-E recorded; measurement-first mandated; no alternative selected | RECORDED |
| (future) | Task group 1 measurements complete | PENDING |
| (future) | Alternative selected/rejected with measured rationale | PENDING |
| (future) | Schema/migration/rollback design for the selected alternative | PENDING |
## 5. Explicit non-decisions
- FTS5 is **not** presumed correct: its tokenizer folding must be proven a
  superset of Windows ordinal ignore-case on this host (see dimension 1) or
  supplemented with an exception side-set maintained like the in-memory
  accelerator's.
- Schema v3 is **not** presumed necessary: alternative C (hybrid residency)
  may achieve the latency targets without a schema change, and alternative E
  is retained only as a control arm.
- The 500 MiB default budget is **not** raised by this proposal.
read: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
read: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
# Proposal: durable-cold-search-index-and-hybrid-residency
Status: **PROPOSAL / DECISION RECORD ONLY — implementation is explicitly not
started.** This change was prepared during the `large-index-lifecycle-and-
search-scalability` closure campaign (PR #14 handoff) as the separately scoped
successor that campaign deliberately deferred. It must not be folded into PR
#14 and must not begin production implementation without an explicit owner
authorization and a completed measurement phase (task group 1).
## Problem
At 2M+ indexed rows the product enters a measured **cold regime**: the
in-memory projection (~839.7 MB modeled at 2M rows) exceeds the 500 MiB
default budget (`Projection::kDefaultMaximumMemoryBytes`; enforcement
watermark 400 MiB), `SetProjectionMemoryBudget` has no production caller, and
every query then serves from the SQLite cold stream. Measured at 2M
(`evidence/search/search-matrix-before.json`, historical p50): selective
~2.5 s, broad ~27.1 s, extension ~37.6 s per keystroke-triggered query class.
The completed predecessor campaign shipped the safe, schema-free
improvements and stopped here on purpose:
- hot/projection-resident selective search is already accelerated
  (filename-trigram accelerator, 56-189x on eligible literal classes at 1M,
  rows examined 1M -> 1);
- the cold path gained a conservative SQL name prefilter
  (`ForEachEntryWithAsciiNameUntil`, 1.28-1.44x on cold literal classes at
  1M with 9.8% non-ASCII names), but the **hard floor is the name-column
  scan itself (~1.05-1.10 us/scanned-row)** — a zero-survivor literal is
  still ~1.1 s at 1M and proportionally worse at 2M-5M;
- non-literal classes (broad, extension, short_prefix, unicode) are
  prefilter-ineligible by design and remain O(N) scans on the cold path.
The largest remaining user-relevant cost at production scale is therefore
**cold candidate generation at 2M-5M+ rows**: no durable structure exists
that can answer "which rows could match this needle?" without scanning and
decoding every row of the volume.
## What this proposal investigates (measure first, decide second)
The proposal does **not** assume FTS, schema v3, or hybrid residency is the
answer. It defines a measurement campaign over alternatives, with a decision
record per alternative:
1. **Durable candidate-index alternatives** (schema v3 or equivalent):
   - SQLite FTS5 (unicode61/trigram tokenizer) as a candidate generator;
   - a durable trigram posting table mirroring the in-memory accelerator's
     semantics (folded-trigram postings + exception set);
   - covering-index / sorted-name strategies (ordinal-key prefixes);
   - hybrid hot/cold residency (Option C of the predecessor's memory
     decision: a compact, always-resident candidate subset per volume) as a
     non-schema alternative;
   - budget-policy alternatives (adaptive budget) as the control arm.
2. **Candidate completeness under Windows ordinal comparison.** Every
   candidate generator must be a **conservative superset** of the canonical
   matcher's result set under `CompareStringOrdinal(..., TRUE)` semantics on
   the host table — including the fold-exceptional class (units the host
   uppercase table equates with ASCII, e.g. U+017F on some tables) and
   surrogate names, per the predecessor's P2-A repair
   (`Projection::SetTestingFoldExceptionalUnit`, host-derived memoized
   probe, fail-closed). An FTS tokenizer that drops such rows is not
   acceptable; the differential suites (hot/cold identity + ordering,
   `TestFilenameCandidatesAreConservativeSuperset` and descendants) are the
   acceptance oracle.
3. **Query planner selection.** When does the planner consult the durable
   index vs. stream the cold path? Candidate caps, eligibility gates (single
   literal, folded length, residency), and fallback-to-scan behavior must be
   cost-based and measured at 200k/1M/2M/5M.
4. **Schema migration and rollback.** Any schema v3 must be additive and
   versioned: migration cost at 2M-5M rows, crash-consistent index updates
   (the index must never disagree with the entries table after a crash —
   transactional coupling or rebuild-on-open), rollback to v2 with no data
   loss, and disk-size tradeoffs (measured index bytes per 1M rows per
   alternative).
5. **Bounded memory footprint.** The durable index is a disk structure, but
   caches/planners built on it must stay within documented bounds; no
   alternative may silently raise the default 500 MiB projection budget.
6. **Cold/hot result equivalence.** Cold (durable-index-assisted) and hot
   (projection-accelerated) results must remain byte-identical in identity
   and ordering for every query class — the pinned contract from the
   predecessor spec delta.
7. **Startup/rebuild cost.** Index build during scan/ingestion, incremental
   maintenance per USN batch, and reopen/recovery cost must be measured;
   a durable index that doubles ingestion time is a regression.
## Performance targets (to be validated, not promised)
Measure at 200k / 1M / 2M / 5M rows, same discipline as the predecessor
campaign (idle host gating, medians for hot, disclosed single samples for
cold, machine-readable artifacts with embedded identity):
- cold selective/exact/no-match at 2M: from seconds to the sub-200 ms class
  if a durable candidate generator is adopted;
- cold non-literal classes: measured improvement or an honest documented
  floor;
- ingestion and rebuild overhead: no material regression vs. the certified
  31-34x replacement-rebuild behavior at 1M.
## Impact
- **Affected components (if adopted later):** `ffindexstore` (schema,
  migration, candidate generation), `ffengine` (planner selection, budget
  policy), `ffsearch` (query classification), tests (differential +
  crash-consistency + migration suites). No protocol, service, installer, or
  UI change is implied.
- **Risk:** schema migration on multi-million-row user indexes is the
  highest-risk operation this product would yet ship; crash-consistency and
  rollback are mandatory acceptance items, not stretch goals.
- **Non-goals:** raising default memory consumption; changing canonical match
  semantics; hot-path redesign; any change to the certified PR #14 behavior.
- **Relationship to PR #14:** none — PR #14 remains a self-contained,
  certified candidate; this successor starts from its landed state.
## Decision record pointer
`design.md` in this directory is the living decision record: alternatives,
measurement plan, acceptance gates, and the retain/reject decision per
alternative (to be filled by the future campaign — current entries record
the deferred predecessor decisions that bound this work).
# Tasks
Proposal-stage ledger. **Nothing here is started.** The first action of the
future campaign is task 1.1; no production implementation task may begin
before the measurement phase (group 1) has produced its decision record.
## 1. Measurement phase (mandatory before any implementation)
- [ ] 1.1 Reproduce the 2M-5M cold-regime baseline on the landed PR #14 tree (same harness discipline: idle gating, embedded source identity, machine-readable artifacts) at 200k / 1M / 2M / 5M, all 12 query classes, hot and cold.
- [ ] 1.2 Measure alternatives A-E from `design.md` as prototypes behind test-only seams (never production): candidate-generation latency, completeness status, ingestion overhead, index size, memory bounds.
- [ ] 1.3 Prove or disprove candidate completeness under Windows ordinal folding for each alternative (fold-exceptional + surrogate classes included); record red-first differential evidence.
## 2. Decision
- [ ] 2.1 Select or reject each alternative with measured rationale in the `design.md` decision log; state the schema/migration consequence explicitly.
- [ ] 2.2 If no alternative meets the targets with acceptable risk, record the measured floor and stop — an honest documented floor is an acceptable outcome.
## 3. Implementation (only after 2.x selection and owner authorization)
- [ ] 3.1 Implement the selected candidate generator incrementally (additive schema, versioned), with crash-consistent updates and rebuild-on-open fallback.
- [ ] 3.2 Wire cost-based planner selection (candidate cap + fallback to canonical scan preserved).
- [ ] 3.3 Migration + rollback tooling with measured v2->v3 cost at 2M-5M rows and crash-recovery tests.
## 4. Acceptance
- [ ] 4.1 Cold/hot result identity + ordering equivalence suites green for all query classes at 200k/1M/2M/5M.
- [ ] 4.2 Resource bounds proven (planner caches, disk bytes per 1M rows, default 500 MiB projection budget unchanged).
- [ ] 4.3 Full certification ladder at the final candidate (Debug/release-test CTest, Analyze, ASan slice, strict OpenSpec, local CI parity, E2E, UIA, visual), with the same honest classification discipline as the predecessor campaign.
grep: C:/Users/Michael Roy/Documents/FastFiles/src
grep: C:/Users/Michael Roy/Documents/FastFiles
indexstore/src/Store.cpp:1210: bool StoreReadSession::ForEachEntryWithAsciiNameUntil(
indexstore/include/ffindexstore/Store.h:265:     bool ForEachEntryWithAsciiNameUntil(VolumeRowId volumeRowId, std::string_view asciiFoldedNeedle,
engine/src/IndexPipeline.cpp:367: // (StoreReadSession::ForEachEntryWithAsciiNameUntil): LIKE folds exactly the
engine/src/IndexPipeline.cpp:1905:                     ? context.reader->ForEachEntryWithAsciiNameUntil(coldVolumeId, *coldNamePrefilter,
engine/src/IndexPipeline.cpp:2035:                     ? context.reader->ForEachEntryWithAsciiNameUntil(volumeId, *durableNamePrefilter,
src/indexstore/src/Projection.cpp:1008:         // CanonicalMatches(query) is therefore a subset of this candidate set
tests/indexstore/test_projection.cpp:1247: // real production comparison function: CanonicalMatches(query) is a subset of
tests/engine/bench_secondary_filename_index.cpp:206: std::vector<EntryId> CanonicalMatches(const std::vector<Entry>& entries, const ffsearch::Query& query) {
tests/engine/bench_secondary_filename_index.cpp:501:     const std::vector<EntryId> canonical = CanonicalMatches(entries, query);
tests/engine/bench_secondary_filename_index.cpp:587:         const std::vector<EntryId> canonical = CanonicalMatches(entries, probe);
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/review-remediation-2026-10-21.md:35: satisfied `CanonicalMatches ⊆ CandidateSet` and the review example cannot
read: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/I...
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "ffindexstore/Projection.h"
#include "ffindexstore/Store.h"
#include "ffprotocol/Records.h"
#include "ffprotocol/Settings.h"
#include "ffprotocol/SnapshotFormat.h"
#include "ffprotocol/UiProtocol.h"
#include "ffsearch/Search.h"
namespace ffengine {
// index-storage-and-scanning tasks.md sections 3/6/8: owns the durable
// SQLite store and the in-memory projection, and is the single point
// where the engine's ingestion pipeline enforces "commit to the durable
// store before applying to the projection" (design.md D4) -- every
// mutating method here does exactly that internally, so no caller can
// accidentally apply a batch to the projection without it having first
// durably committed.
//
// Internally synchronized (a single mutex serializes rebuild/ingestion/
// export) -- callers (the engine's scan/journal callbacks, which run on
// PrivilegedConnection's reader thread, and any UI-facing snapshot-export
// caller) do not need to coordinate with each other.
class IndexPipeline {
public:
    // nonblocking-indexed-read-plane 2.1: deterministic test seam used to
    // park query work at a named phase. Production callers leave the hook
    // empty; the fixed implementation invokes it only after long-running
    // work has released the mutation/lifecycle critical section.
    enum class SearchPhase : uint8_t {
        WorkStarted,
        Matching,
        ColdStreaming,
        DurableFallback,
        GenerationValidation,
    };
    struct SearchTestHooks {
        std::function<void(SearchPhase)> onPhase;
    };
    // Optional attribution seam for the explicit scalability benchmark. The
    // normal product path passes no timing object and remains behaviorally
    // unchanged; the fields describe the real SearchIndexed stages rather
    // than adding production logging.
    struct SearchTimings {
        bool detailedDiagnostics = false;
        bool projectionOnly = false;
        double sqliteMetadataMs = 0.0;
        double projectionTraversalMs = 0.0;
        double projectionEnumerationMs = 0.0;
        double namePoolLookupMs = 0.0;
        double nameViewConstructionMs = 0.0;
        double candidateMaterializationMs = 0.0;
        double lightweightCandidateMaterializationMs = 0.0;
        double lazyPathMaterializationMs = 0.0;
        double queryParseMs = 0.0;
        double matchFilterSortMs = 0.0;
        double predicateDispatchMs = 0.0;
        double substringMatchingMs = 0.0;
        double globMatchingMs = 0.0;
        double extensionMatchingMs = 0.0;
        double metadataMatchingMs = 0.0;
        double folderMatchingMs = 0.0;
        double matchedRowHandlingMs = 0.0;
        double responseMaterializationMs = 0.0;
        double totalMs = 0.0;
        size_t candidateCount = 0;
        size_t projectionEntriesVisited = 0;
        size_t projectionRowsExamined = 0;
        size_t namePoolLookups = 0;
        size_t emptyNamesRejected = 0;
        size_t nameViewsConstructed = 0;
        size_t candidateObjectsMaterialized = 0;
        size_t candidateNameCopies = 0;
        size_t candidatesVisited = 0;
        size_t queryPredicateEvaluations = 0;
        size_t queryTermEvaluations = 0;
        size_t caseInsensitiveComparisons = 0;
        size_t caseFoldOperations = 0;
        size_t substringSearches = 0;
        size_t globMatches = 0;
        size_t prefixChecks = 0;
        size_t extensionPredicates = 0;
        size_t metadataPredicates = 0;
        size_t earlyRejects = 0;
        size_t predicateMatches = 0;
        size_t pathNeededDecisions = 0;
        size_t pathLookups = 0;
        size_t matchedRows = 0;
        size_t resultCount = 0;
        size_t searchResultObjectsMaterialized = 0;
        size_t lazyPathMaterializationCount = 0;
        size_t filenameIndexCandidateCount = 0;
        bool filenameIndexUsed = false;
        // memory-bounded-indexed-scale 3.2: hot/cold attribution for the
        // bounded-projection search path. Cold rows stream from the durable
        // store; they never pass through a projection reload.
        size_t hotRowsExamined = 0;
        size_t coldRowsExamined = 0;
        size_t coldVolumesSearched = 0;
        double storeStreamMs = 0.0;
        bool coldSearchUsed = false;
        bool truncated = false;
        bool cancelled = false;
        uint64_t searchGeneration = 0;
        uint64_t queryVisibleEpoch = 0;
        uint64_t dependencySetEpoch = 0;
        size_t generationRestarts = 0;
        size_t relevantInvalidations = 0;
        size_t durableFallbackAttempts = 0;
        size_t durableFallbackRowsStreamed = 0;
        bool durableFallbackUsed = false;
        bool durableFallbackSucceeded = false;
        double stateLockWaitMs = 0.0;
        double maxStateLockHoldMs = 0.0;
        // indexed-search-zero-copy-hot-path 7.x: view-first lightweight capture.
        // Timing attribution is a non-overlapping partition:
        //  - lightweightCaptureMs = bounded name-buffer capture under mutex
        //  - viewMatchingMs = pure view predicate (MatchesView*) time outside lock, excluding survivor work
        //  - survivorMaterializationMs = survivor Upgrade (MakeProjectionCandidate + accumulator Add) outside lock
        size_t hotLightweightRowsCaptured = 0;
        size_t hotRowsRejectedBeforeMaterialization = 0;
        size_t viewPathFallbackRows = 0;
        double lightweightCaptureMs = 0.0;
        double viewMatchingMs = 0.0;
        double survivorMaterializationMs = 0.0;
        bool viewPathUsed = false;
    };
    struct ReadPlaneDiagnostics {
        size_t activeSearches = 0;
        size_t peakSearches = 0;
        size_t activeDurableReaders = 0;
        size_t peakDurableReaders = 0;
        uint64_t completedSearches = 0;
        uint64_t cancelledSearches = 0;
        uint64_t unavailableSearches = 0;
        uint64_t readerFailures = 0;
        uint64_t generationRestarts = 0;
        uint64_t queryVisibleEpoch = 0;
        uint64_t dependencySetEpoch = 0;
        uint64_t queryVisibleInvalidations = 0;
        uint64_t metadataOnlyUpdates = 0;
        uint64_t metadataOnlyInvalidationsAvoided = 0;
        uint64_t durableFallbacks = 0;
        uint64_t successfulDurableFallbacks = 0;
        uint64_t failedDurableFallbacks = 0;
        uint64_t durableFallbackRowsStreamed = 0;
    };
    // outIntegrityFailed (optional, task 1.7): set to true when the open
    // failed specifically because the existing file failed its integrity
    // check, so the caller can apply the rebuild-from-fresh-scan fallback
    // (delete the file and reopen) rather than treating it as a generic
    // I/O failure.
    bool Open(const std::string& dbPathUtf8, bool* outIntegrityFailed = nullptr);
    void Close();
    // nonblocking-indexed-read-plane 2.1: test-only phase injection. The
    // callback is never invoked when no hook is installed.
    void SetSearchTestHooks(SearchTestHooks hooks);
    // nonblocking-indexed-read-plane 3.4: portable fault injection for the
    // per-query durable reader. Production callers leave this empty; a
    // forced reader failure must surface as Unavailable rather than a partial
    // completed response.
    void SetReadSessionFaultInjector(ffindexstore::ReadSessionFaultInjector injector);
#ifdef FF_INDEX_PIPELINE_TESTING
    // rule-pruning-durability-and-reconciliation-failure-closure D3: the
    // deterministic fault seam names the secondary transaction boundary so a
    // regression test cannot accidentally exercise a different caller.
    enum class SecondaryPruningBoundary : uint8_t {
        MftRulePrune,
        UsnRulePrune,
        RebuildRulePrune,
        ReconciliationRemoval,
    };
    using SecondaryPruningFaultInjector = std::function<bool(
        SecondaryPruningBoundary, ffindexstore::VolumeRowId,
        const std::vector<ffindexstore::EntryChange>&)>;
    // Test-only: production builds do not expose or compile this mutable fault
    // surface. The callback is invoked while the pipeline mutation lock is held.
    void SetSecondaryPruningFaultInjector(SecondaryPruningFaultInjector injector);
#endif
    ReadPlaneDiagnostics GetReadPlaneDiagnostics() const noexcept;
    // task 1.6: scheduled WAL maintenance, intended to be called on a
    // low-frequency timer (the engine calls it from VolumeSessionManager's
    // ReconciliationSchedulerLoop poll). Runs a cheap passive checkpoint
    // every call and escalates to a forced checkpoint once the WAL has
    // grown past the threshold (design.md "Risks": unbounded WAL growth
    // under sustained write bursts). No-op if the store was never opened.
    void RunStoreMaintenance(bool userIdle = false, uint64_t vacuumIntervalHours = 24,
                             uint64_t vacuumSizeGrowthPercent = 10);
    // task 3.1/3.2: rebuilds every volume already known to the durable
    // store, invoking onVolumeRebuilt once per volume whose rebuild and rule
    // pruning both complete successfully. A volume whose pruning transaction
    // fails remains retryable and is deliberately not reported as complete.
    using VolumeRebuiltCallback = std::function<void(ffindexstore::VolumeRowId)>;
    void RebuildAll(const VolumeRebuiltCallback& onVolumeRebuilt);
    // close-independent-validation-gaps 4.6: initial and reconciliation
    // scans are bulk projection mutations. Their durable writes continue
    // normally, while the projection defers compaction and its optional
    // filename accelerator until the bulk scan is no longer active.
    void SetBulkScanActive(bool active);
    ffindexstore::VolumeRowId ResolveVolume(const ffindexstore::VolumeKey& key);
    std::optional<ffindexstore::VolumeMetadata> GetVolumeMetadata(ffindexstore::VolumeRowId id);
    std::vector<ffindexstore::VolumeMetadata> GetAllVolumes();
    // zero-touch-autonomous-engineering (settings-and-appearance 2.5 /
    // subtree gating): stores the per-volume include/exclude rules + drive-
    // letter prefix the ingestion pipeline uses to decide IsPathIncluded.
    // A volume with no rules (or not yet configured) includes everything,
    // preserving the behavior prior to this change. Called by
    // VolumeSessionManager whenever a session is established or the persisted
    // configuration changes (ReloadConfiguration), so rule changes take effect
    // on subsequent ingestion and the next reconciliation pass without an
    // engine restart.
    void SetVolumeRules(ffindexstore::VolumeRowId volumeId, const ffprotocol::VolumeSetting& setting);
    // tasks.md 3.3/3.4/6.2: commits the batch to the durable store first;
    // only applies it to the projection once that commit succeeds. On a
    // failed commit, the projection is left untouched and false is
    // returned so the caller can retry the same batch later -- it is
    // never partially applied. Entries are always linked by
    // FileReferenceNumber (never a re-derived path), which is what tasks.md
    // 6.2 calls "filename canonicalization ... keyed off
    // FileReferenceNumber" in practice: EntryRecord.parentId always comes
    // from the wire record's own parent FRN field, never from a computed
    // path.
    bool ApplyMftBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::MftRecordV1>& records);
    // allocated-size-storage-accuracy 5.3: V2 scan records (additive
    // allocated size) applied with the full-record mask, exactly like V1.
    bool ApplyMftBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::MftRecordV2>& records);
    // USN_REASON_FILE_DELETE-flagged records are applied as removals;
    // everything else is an upsert (tasks.md 6.1's engine-side consumer).
    bool ApplyUsnBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::UsnDeltaV1>& records);
    bool ApplyUsnBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::UsnDeltaV2>& records);
    // allocated-size-storage-accuracy 5.3: V3 USN records (additive
    // allocated-size presence bit) applied with per-field presence masks.
    bool ApplyUsnBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::UsnDeltaV3>& records);
private:
    // Shared commit-then-apply tail for scan batches (allocated-size-
    // storage-accuracy refactor: the V1/V2 overloads differ only in record
    // conversion, so the durable-commit/projection/reconciliation logic
    // lives here exactly once).
    bool ApplyMftBatchChanges(ffindexstore::VolumeRowId volumeId,
                              std::vector<ffindexstore::EntryChange>&& changes);
    // Shared commit-then-apply tail for USN batches.
    bool ApplyUsnBatchChanges(ffindexstore::VolumeRowId volumeId,
                              std::vector<ffindexstore::EntryChange>&& changes);
#ifdef FF_INDEX_PIPELINE_TESTING
    bool ShouldInjectSecondaryPruningFailureLocked(
        SecondaryPruningBoundary boundary, ffindexstore::VolumeRowId volumeId,
        const std::vector<ffindexstore::EntryChange>& changes);
#endif
    ffprotocol::IndexedSearchResponse SearchIndexedReadPlane(
        const ffprotocol::IndexedSearchRequest& request,
        SearchTimings* timings, const std::function<bool()>& cancelled);
public:
    bool SetVolumeAvailable(ffindexstore::VolumeRowId id, bool available, uint64_t nowTimestamp);
    bool SetScanCursor(ffindexstore::VolumeRowId id, const std::vector<uint8_t>& cursor);
    bool MarkScanComplete(ffindexstore::VolumeRowId id);
    bool SetJournalPosition(ffindexstore::VolumeRowId id, uint64_t journalId, uint64_t resumeUsn);
    bool SetLastReconciliationTime(ffindexstore::VolumeRowId id, uint64_t nowTimestamp);
    bool ForgetVolume(ffindexstore::VolumeRowId id);
    // tasks.md section 8: a from-scratch scan (StartVolumeScan issued
    // with no resume cursor) doubles as a reconciliation pass once it
    // runs to completion, since a full MFT enumeration is ground truth
    // for what currently exists. BeginReconciliationPass starts tracking
    // which (already-known) entries this pass observes; every Upsert
    // applied via ApplyMftBatch while a pass is active for that volume is
    // recorded; FinishReconciliationPass (call on ScanComplete) removes
    // any previously-persisted entry that the pass never observed --
    // tasks.md 8.1's "entries present in the index but no longer
    // resolvable". Scoped to a single continuous engine session: if the
    // engine restarts mid-pass, the in-memory observed-set is lost and
    // the next *fully-completed* pass reconciles correctly rather than
    // acting on a partial view.
    void BeginReconciliationPass(ffindexstore::VolumeRowId id);
    void FinishReconciliationPass(ffindexstore::VolumeRowId id);
    // index-durability-false-success-closure D3/D5: discard an in-flight
    // pass without finalizing -- no removals, no epoch change. Used when
    // the scan that was supplying the pass's seen-set turned out not to
    // have run to completion (ScanFailed, batch-apply failure, or pause),
    // so its partial coverage can never be mistaken for the whole volume.
    void AbortReconciliationPass(ffindexstore::VolumeRowId id);
    bool IsReconciliationPassActive(ffindexstore::VolumeRowId id);
    // Converts the current projection state for one volume into the
    // existing directory-listing snapshot wire format (SnapshotFormat.h),
    // keyed by full reconstructed path under `rootPathPrefix` (e.g.
    // L"C:"), so it can be published through the already-established
    // committed-generation snapshot mechanism (tasks.md 3.5) without
    // inventing a new wire format for this change.
    std::map<std::wstring, ffprotocol::SnapshotDirectory> ExportDirectorySnapshot(
        ffindexstore::VolumeRowId id, const std::wstring& rootPathPrefix);
    // memory-bounded-indexed-scale 5.1: bulk indexed publication (startup
    // rebuild, scan-complete) exports at most maxDirectories entries in the
    // map's deterministic path-sorted order -- i.e., the lexicographically
    // shallowest/top-of-tree directories survive. Deeper directories remain
    // durable and re-materialize through the normal request path, so published
    // bytes scale with the browse working set rather than the whole tree.
    static constexpr size_t kInitialIndexedExportDirectories = 4096;
    std::map<std::wstring, ffprotocol::SnapshotDirectory> ExportDirectorySnapshotBounded(
        ffindexstore::VolumeRowId id, const std::wstring& rootPathPrefix,
        size_t maxDirectories);
    // performance-closure-and-multisurface-acceleration 6.1: pooled-name
    // export. Same traversal, ordering, and bounded-eviction semantics as
    // ExportDirectorySnapshotBounded (the owning exports above are converting
    // wrappers over this core), but entry names land in per-directory pools
    // instead of per-entry owning std::wstring allocations. Measured on the
    // 200k-entry D5 isolation matrix: the owning build+serialize stages cost
    // 59% more at byte-identical payload. Callers that publish must pass the
    // pooled result straight through so the eliminated allocations never
    // reappear at a merge boundary.
    ffprotocol::PooledSnapshotMap ExportDirectorySnapshotBoundedPooled(
        ffindexstore::VolumeRowId id, const std::wstring& rootPathPrefix,
        size_t maxDirectories);
    // file-preview-and-properties §6.2 / storage-analysis §3.1: returns the
    // subtree aggregate for `parentFrn` from the in-memory projection if
    // the folder is known, or std::nullopt if it is not.
    std::optional<ffindexstore::Projection::FolderAggregate> GetFolderAggregate(ffindexstore::VolumeRowId volumeId, ffindexstore::FileId parentFrn);
    // memory-bounded-indexed-scale 3.x: bounded-projection seams. The budget
    // override applies synchronously and strictly (like
    // Projection::SetMaximumMemoryBytes), so tests -- and any future settings
    // surface -- can force deterministic eviction. The diagnostics/volume
    // residency accessors back observability without exposing the projection.
    void SetProjectionMemoryBudget(size_t maximumBytes);
    ffindexstore::Projection::MemoryDiagnostics GetProjectionMemoryDiagnostics() const;
    // performance-closure-and-multisurface-acceleration 8.1: read-only
    // per-category projection memory decomposition (see
    // Projection::MemoryBreakdown). Diagnostic only; no representation or
    // policy change.
    ffindexstore::Projection::MemoryBreakdown GetProjectionMemoryBreakdown() const;
    bool IsVolumeEvicted(ffindexstore::VolumeRowId volumeId) const;
    // memory-bounded-indexed-scale 5.x: explicit policy hook dropping one
    // volume's resident rows (durable data untouched). Also the deterministic
    // forced-eviction seam for the bounded-scale test suite.
    void UnloadVolumeFromProjection(ffindexstore::VolumeRowId volumeId);
    // Executes a bounded query against the durable in-memory projection. The
    // returned candidates carry reconstructed parent folders so the UI can
    // navigate a result without requiring the entire index over IPC. The
    // projection lock is held while copying lightweight candidate data;
    // matching and sorting happen after that copy, and lazy folder
    // reconstruction reacquires the lock only for path-dependent survivors.
    using SearchCancellation = std::function<bool()>;
    ffprotocol::IndexedSearchResponse SearchIndexed(const ffprotocol::IndexedSearchRequest& request,
                                                    SearchTimings* timings = nullptr,
                                                    const SearchCancellation& cancelled = {});
private:
    enum class QueryMutationReason : uint8_t {
        EntryMutation,
        VolumeConfiguration,
        VolumeVisibility,
        Rebuild,
        Forget,
        ProjectionResidency,
        Lifecycle,
    };
    struct SearchDependency {
        ffindexstore::VolumeRowId volumeId = 0;
        uint64_t dataEpoch = 0;
        uint64_t configurationEpoch = 0;
    };
    static constexpr size_t kHotCandidateChunkRows = 256;
    // indexed-search-zero-copy-hot-path: batch size for the lightweight
    // view-first capture chunk (mirrors kHotCandidateChunkRows).
    static constexpr size_t kViewCandidateChunkRows = kHotCandidateChunkRows;
    static constexpr size_t kMaxGenerationRestarts = 3;
    static constexpr size_t kMaxDurableFallbackConfigurationRetries = 2;
    static constexpr size_t kMaximumFilenameIndexCandidates = 16 * 1024;
    void AdvanceQueryVisibleEpochLocked(ffindexstore::VolumeRowId volumeId,
                                        QueryMutationReason reason,
                                        bool dependencySetChanged = false);
    // large-index-lifecycle-and-search-scalability review-lane D P1-1: bumps
    // ProjectionResidency for every known volume after a structural
    // projection change (memory-pressure eviction + compaction, cold reload)
    // so in-flight searches restart instead of serving silently incomplete
    // results.
    void AdvanceResidencyEpochsForStructuralChangeLocked();
    void RecordMetadataOnlyUpdateLocked();
    static bool VolumeSettingsEqual(const ffprotocol::VolumeSetting& left,
                                    const ffprotocol::VolumeSetting& right);
    mutable std::mutex mutex_;
    ffindexstore::Store store_;
    ffindexstore::Projection projection_;
    SearchTestHooks searchTestHooks_;
    ffindexstore::ReadSessionFaultInjector readSessionFaultInjector_;
#ifdef FF_INDEX_PIPELINE_TESTING
    SecondaryPruningFaultInjector secondaryPruningFaultInjector_;
#endif
    std::atomic<uint64_t> queryVisibleEpoch_{1};
    std::atomic<uint64_t> dependencySetEpoch_{1};
    std::unordered_map<ffindexstore::VolumeRowId, uint64_t> volumeDataEpochs_;
    std::unordered_map<ffindexstore::VolumeRowId, uint64_t> volumeConfigurationEpochs_;
    std::atomic<size_t> activeSearches_{0};
    std::atomic<size_t> peakSearches_{0};
    std::atomic<size_t> activeDurableReaders_{0};
    std::atomic<size_t> peakDurableReaders_{0};
    std::atomic<uint64_t> completedSearches_{0};
    std::atomic<uint64_t> cancelledSearches_{0};
    std::atomic<uint64_t> unavailableSearches_{0};
    std::atomic<uint64_t> readerFailures_{0};
    std::atomic<uint64_t> generationRestarts_{0};
    std::atomic<uint64_t> queryVisibleInvalidations_{0};
    std::atomic<uint64_t> metadataOnlyUpdates_{0};
    std::atomic<uint64_t> metadataOnlyInvalidationsAvoided_{0};
    std::atomic<uint64_t> durableFallbacks_{0};
    std::atomic<uint64_t> successfulDurableFallbacks_{0};
    std::atomic<uint64_t> failedDurableFallbacks_{0};
    std::atomic<uint64_t> durableFallbackRowsStreamed_{0};
    std::atomic<uint64_t> lifecycleEpoch_{1};
    // zero-touch-autonomous-engineering (subtree gating): per-volume rules +
    // drive-letter prefix (VolumeSetting.key, e.g. "C:") used to evaluate
    // ffprotocol::IsPathIncluded against reconstructed canonical paths.
    std::unordered_map<ffindexstore::VolumeRowId, ffprotocol::VolumeSetting> volumeRules_;
    // Returns true if the entry should be kept (included) under the volume's
    // configured rules. Caller MUST hold mutex_. Returns true (include) when
    // no rules are configured for the volume (preserves pre-change behavior),
    // or when the entry's parent chain cannot yet be resolved in the
    // projection (deferred: decided on the next reconciliation pass rather
    // than risk dropping a record whose inclusion is genuinely undecided).
    bool IsEntryIncludedLocked(ffindexstore::VolumeRowId volumeId, ffindexstore::FileId frn);
    // address-engineering-audit-findings 5.1: keep planner statistics fresh
    // without adding ANALYZE work to every small journal update. The counter
    // is retained when ANALYZE fails so a later ingestion batch retries it.
    static constexpr uint64_t kAnalyzeRecordThreshold = 10'000;
    void MaybeAnalyzeAfterBulkInsertLocked(size_t recordCount);
    void MaybeReportProjectionMemoryPressureLocked();
    uint64_t recordsSinceAnalyze_ = 0;
    bool projectionMemoryWarningIssued_ = false;
    struct FileIdHash {
        size_t operator()(const ffindexstore::FileId& id) const noexcept {
            return std::hash<uint64_t>{}(id.low) ^ (std::hash<uint64_t>{}(id.high) << 1);
        }
    };
    std::unordered_map<ffindexstore::VolumeRowId, std::unordered_set<ffindexstore::FileId, FileIdHash>>
        reconciliationSeen_;
};
} // namespace ffengine
read: C:/Users/Michael Roy/Documents/FastFiles/src/search/inclu...
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>
namespace ffsearch {
struct Candidate {
    std::wstring name;
    std::wstring folder;
    uint64_t sizeBytes = 0;
    uint64_t modifiedTime = 0; // Windows FILETIME ticks
    bool isDirectory = false;
    uint64_t createdTime = 0;
    int64_t volumeId = 0;
    uint64_t id = 0;
    uint64_t parentId = 0;
    // True when the candidate came from the durable/index projection rather
    // than a degraded directory snapshot. Library callers historically build
    // candidates as indexed records, so the default preserves that contract;
    // the UI marks degraded-only records false at its snapshot boundary.
    bool persistentIndexed = true;
    // indexed-search-allocated-size D1: on-disk allocation as a dimension
    // separate from logical `sizeBytes`, with explicit knownness. Zero is a
    // valid KNOWN value, never the absence sentinel, so the default is
    // unknown-and-zero and unknown is never synthesized from `sizeBytes`.
    // Appended after `persistentIndexed` on purpose: existing positional
    // aggregate initializers keep initializing exactly the members they
    // always did, and a new source of allocated truth is never introduced.
    uint64_t allocatedSizeBytes = 0;
    bool allocatedSizeKnown = false;
};
// A projection-backed candidate. The name is a view into the immutable
// NamePool while a search holds the projection lock; folder is populated only
// when scope or a folder predicate requires path evaluation. Search results
// continue to own ordinary Candidate strings.
struct CandidateView {
    std::u16string_view name;
    std::wstring_view folder;
    uint64_t sizeBytes = 0;
    uint64_t modifiedTime = 0;
    bool isDirectory = false;
    uint64_t createdTime = 0;
    int64_t volumeId = 0;
    uint64_t id = 0;
    uint64_t parentId = 0;
    bool persistentIndexed = true;
    bool nameAsciiLower = false;
    // indexed-search-allocated-size D3: copied straight from the projection
    // entry while the existing traversal already holds the projection lock --
    // two scalars travelling with the lightweight record, no extra I/O and no
    // eager path reconstruction.
    uint64_t allocatedSizeBytes = 0;
    bool allocatedSizeKnown = false;
    // Appended last on purpose: existing positional aggregate initializers
    // keep initializing exactly the members they always did. True when every
    // name code unit is ASCII regardless of letter case. Matching an ASCII
    // haystack with a pure-ASCII needle is equivalence-preserving under
    // ordinal ignore-case comparison (exactly ASCII case folding -- proven
    // exhaustively over all 128x128 code-unit pairs in
    // ffsearch_ascii_fold_tests), which lets the matcher avoid one Win32
    // CompareStringOrdinal call per alignment for the dominant real-world
    // name class ("Documents", "README.md").
    bool nameIsAscii = false;
};
struct SizeFilter { enum class Op { Equal, Greater, GreaterEqual, Less, LessEqual, Range }; Op op; uint64_t first; uint64_t second = 0; };
struct DateFilter { enum class Op { EqualDay, GreaterThan, Range }; Op op; uint64_t first; uint64_t second = 0; };
using FilterValue = std::variant<std::wstring, SizeFilter, DateFilter>;
using Predicate = std::function<bool(const Candidate&)>;
using ViewPredicate = std::function<bool(const CandidateView&)>;
enum class PredicateKind {
    Unknown,
    LiteralName,
    GlobName,
    FolderName,
    Extension,
    Size,
    AllocatedSize,
    Modified,
    Kind,
};
// Optional benchmark-only attribution for the canonical view matcher. A
// null diagnostics pointer keeps ordinary searches on the existing path.
struct MatchDiagnostics {
    size_t predicateEvaluations = 0;
    size_t termEvaluations = 0;
    size_t caseInsensitiveComparisons = 0;
    size_t caseFoldOperations = 0;
    size_t substringSearches = 0;
    size_t globMatches = 0;
    size_t prefixChecks = 0;
    size_t extensionPredicates = 0;
    size_t metadataPredicates = 0;
    size_t earlyRejects = 0;
    size_t predicateMatches = 0;
    double predicateDispatchMs = 0.0;
    double substringMatchingMs = 0.0;
    double globMatchingMs = 0.0;
    double extensionMatchingMs = 0.0;
    double metadataMatchingMs = 0.0;
    double folderMatchingMs = 0.0;
};
struct FilterDefinition {
    std::function<std::optional<FilterValue>(std::wstring_view)> parseValue;
    std::function<Predicate(const FilterValue&)> makePredicate;
    std::function<ViewPredicate(const FilterValue&)> makeViewPredicate = {};
};
class FilterRegistry {
public:
    void Register(std::wstring key, FilterDefinition definition);
    const FilterDefinition* Find(std::wstring_view key) const;
    static const FilterRegistry& WithDefaults();
private:
    std::unordered_map<std::wstring, FilterDefinition> definitions_;
};
struct Query {
    std::vector<Predicate> predicates;
    // The canonical matcher keeps folder-dependent predicates separate so a
    // caller with a lazy path source can reject name/metadata non-matches
    // before reconstructing a full parent path.
    std::vector<Predicate> predicatesWithoutFolder;
    std::vector<ViewPredicate> viewPredicates;
    std::vector<ViewPredicate> viewPredicatesWithoutFolder;
    std::vector<PredicateKind> viewPredicateKinds;
    std::vector<PredicateKind> viewPredicateWithoutFolderKinds;
    std::vector<std::wstring> unrecognizedKeys;
    std::vector<std::wstring> invalidFilters;
    std::wstring primaryTerm;
    std::wstring simpleLiteralTerm;
    std::u16string simpleLiteralFolded;
    char16_t simpleLiteralAnchor = 0;
    size_t simpleLiteralAnchorIndex = 0;
    std::array<size_t, 128> simpleLiteralShifts{};
    bool requiresFolder = false;
    bool viewCompatible = true;
    bool simpleLiteralName = false;
    bool Matches(const Candidate& candidate) const;
    bool MatchesWithoutFolder(const Candidate& candidate) const;
    bool MatchesView(const CandidateView& candidate, MatchDiagnostics* diagnostics = nullptr) const;
    bool MatchesViewWithoutFolder(const CandidateView& candidate, MatchDiagnostics* diagnostics = nullptr) const;
};
Query ParseQuery(std::wstring_view text, const FilterRegistry& registry);
bool OrdinalContains(std::wstring_view haystack, std::wstring_view needle);
// performance-closure-and-multisurface-acceleration 7.3: ordinal comparison
// between a NamePool UTF-16 view and a wide-character string (Windows wchar_t
// is UTF-16; the comparison is unit-wise and case-insensitive).
int CompareOrdinal16(std::u16string_view left, std::wstring_view right);
bool GlobMatches(std::wstring_view text, std::wstring_view pattern);
// A literal needle prepared once for repeated case-insensitive ordinal
// substring searches over wide strings. When `ascii` is true the needle is
// pure ASCII and `AsciiFoldContains` is semantically identical to
// `OrdinalContains` while avoiding per-alignment CompareStringOrdinal calls;
// callers must fall back to `OrdinalContains` when `ascii` is false.
struct AsciiFoldNeedle {
    bool ascii = false;
    std::wstring value;
    std::u16string folded;
    char16_t anchor = 0;
    size_t anchorIndex = 0;
    std::array<size_t, 128> shifts{};
};
AsciiFoldNeedle PrepareAsciiFoldNeedle(std::wstring_view value);
bool AsciiFoldContains(std::wstring_view haystack, const AsciiFoldNeedle& needle);
bool AsciiFoldEquals(std::wstring_view left, std::wstring_view right);
bool AsciiFoldStartsWith(std::wstring_view value, std::wstring_view prefix);
} // namespace ffsearch
find: C:/Users/Michael Roy/Documents/FastFiles/src/search/inclu...
read: C:/Users/Michael Roy/Documents/FastFiles/src/search/src/S...
History.h
Path.h
Query.h
Search.h
#include "ffsearch/Search.h"
#include "ffsearch/Path.h"
#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <windows.h>
namespace ffsearch {
namespace {
bool OrdinalEqual(std::wstring_view left, std::wstring_view right) {
    return left.size() == right.size() && CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
        right.data(), static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}
bool OrdinalStartsWith(std::wstring_view value, std::wstring_view prefix) {
    return value.size() >= prefix.size() && CompareStringOrdinal(value.data(), static_cast<int>(prefix.size()),
        prefix.data(), static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
}
bool InScope(const Candidate& candidate, SearchScope scope, const std::wstring& root, bool persistentOnly) {
    if (persistentOnly && !candidate.persistentIndexed) return false;
    if (scope == SearchScope::AllIndexedLocations) return true;
    if (scope == SearchScope::CurrentFolder) return OrdinalEqual(candidate.folder, root);
    if (scope == SearchScope::CurrentDrive) {
        return SameVolume(root, candidate.folder);
    }
    if (OrdinalEqual(candidate.folder, root)) return true;
    if (!OrdinalStartsWith(candidate.folder, root)) return false;
    return root.empty() || root.back() == L'\\' ||
           (candidate.folder.size() > root.size() && candidate.folder[root.size()] == L'\\');
}
int CompareOrdinal(std::wstring_view left, std::wstring_view right) {
    return CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(), static_cast<int>(right.size()), TRUE) - CSTR_EQUAL;
}
struct EntryKey {
    int64_t volume;
    uint64_t id;
    bool operator==(const EntryKey&) const = default;
};
struct EntryKeyHash {
    size_t operator()(const EntryKey& key) const noexcept {
        return std::hash<int64_t>{}(key.volume) ^ (std::hash<uint64_t>{}(key.id) << 1);
    }
};
}
MatchTier ClassifyMatch(const Candidate& candidate, std::wstring_view term) {
    if (term.empty()) return MatchTier::Substring;
    // Tier classification runs once per matched row; ASCII terms classify via
    // fold comparison instead of three ordinal Win32 scans per row.
    const auto needle = PrepareAsciiFoldNeedle(term);
    const auto equals = [&term, &needle](std::wstring_view value) {
        return needle.ascii ? AsciiFoldEquals(value, term) : OrdinalEqual(value, term);
    };
    const auto startsWith = [&term, &needle](std::wstring_view value) {
        return needle.ascii ? AsciiFoldStartsWith(value, term) : OrdinalStartsWith(value, term);
    };
    std::wstring_view stem = candidate.name;
    const size_t dot = stem.find_last_of(L'.');
    if (dot != std::wstring_view::npos) stem = stem.substr(0, dot);
    if (equals(stem) || equals(candidate.name)) return MatchTier::Exact;
    if (startsWith(candidate.name)) return MatchTier::Prefix;
    if (needle.ascii ? AsciiFoldContains(candidate.name, needle) : OrdinalContains(candidate.name, term)) {
        return MatchTier::Substring;
    }
    return MatchTier::PathOnly;
}
// memory-bounded-indexed-scale: shared strict-weak total order over results
// (the former SortResults comparator). Truncation and final ordering both use
// this so capped result sets are the TOP-N by the requested sort -- never an
// enumeration-order-dependent prefix.
bool ResultLess(const SearchResult& left, const SearchResult& right,
                const SortField field, const bool descending) {
    int primary = 0;
    switch (field) {
        case SortField::Relevance:
            primary = static_cast<int>(left.tier) - static_cast<int>(right.tier);
            if (primary == 0) primary = static_cast<int>(left.candidate.name.size()) - static_cast<int>(right.candidate.name.size());
            if (primary == 0) primary = static_cast<int>(left.candidate.folder.size()) - static_cast<int>(right.candidate.folder.size());
            break;
        case SortField::Filename: primary = CompareOrdinal(left.candidate.name, right.candidate.name); break;
        case SortField::Path: primary = CompareOrdinal(left.candidate.folder, right.candidate.folder); break;
        case SortField::Size: primary = left.candidate.sizeBytes < right.candidate.sizeBytes ? -1 : left.candidate.sizeBytes > right.candidate.sizeBytes ? 1 : 0; break;
        case SortField::Modified: primary = left.candidate.modifiedTime < right.candidate.modifiedTime ? -1 : left.candidate.modifiedTime > right.candidate.modifiedTime ? 1 : 0; break;
        case SortField::Created: primary = left.candidate.createdTime < right.candidate.createdTime ? -1 : left.candidate.createdTime > right.candidate.createdTime ? 1 : 0; break;
        case SortField::AllocatedSize: {
            // indexed-search-allocated-size D6: known values sort numerically;
            // an UNKNOWN allocated size always sorts AFTER every known value in
            // BOTH directions (never behaves like zero or UINT64_MAX). When the
            // two rows differ only in knownness, the direction is decided here
            // and must not be flipped by `descending`.
            const bool leftKnown = left.candidate.allocatedSizeKnown;
            const bool rightKnown = right.candidate.allocatedSizeKnown;
            if (leftKnown != rightKnown) return leftKnown;
            if (leftKnown) {
                primary = left.candidate.allocatedSizeBytes < right.candidate.allocatedSizeBytes ? -1
                         : left.candidate.allocatedSizeBytes > right.candidate.allocatedSizeBytes ? 1 : 0;
            }
            // Both unknown: no primary ordering from allocated size; the
            // alphabetical tie-break below applies (deterministic unknowns).
            break;
        }
    }
    if (primary != 0) return descending ? primary > 0 : primary < 0;
    // memory-bounded-indexed-scale: the tie-break chain must be a TOTAL
    // order (name -> folder -> volume -> identity) so result ordering is
    // identical regardless of whether a row was enumerated hot from the
    // projection or streamed cold from the durable store. Duplicate
    // basenames across folders previously fell through to stable-sort
    // input order, which is enumeration-order dependent.
    if (const int byFolder = CompareOrdinal(left.candidate.folder, right.candidate.folder); byFolder != 0) {
        return byFolder < 0;
    }
    if (left.candidate.volumeId != right.candidate.volumeId) {
[331 more lines in file. Use offset=121 to continue.]
read: C:/Users/Michael Roy/Documents/FastFiles/src/search/src/S...
        return left.candidate.volumeId < right.candidate.volumeId;
    }
    return left.candidate.id < right.candidate.id;
}
void SortResults(std::vector<SearchResult>& results, SortField field, bool descending) {
    std::sort(results.begin(), results.end(),
              [field, descending](const SearchResult& left, const SearchResult& right) {
                  return ResultLess(left, right, field, descending);
              });
}
SearchAccumulator::SearchAccumulator(const SearchRequest& request,
                                     const std::function<bool()>& cancelled,
                                     SearchDiagnostics* diagnostics)
    : request_(request), cancelled_(cancelled), diagnostics_(diagnostics),
      folderNeededForEvaluation_(request.scope != SearchScope::AllIndexedLocations ||
                                 request.query.requiresFolder) {}
bool SearchAccumulator::Add(Candidate candidate, const CandidateMaterializer& materialize) {
    return AddImpl(std::move(candidate), materialize, false);
}
bool SearchAccumulator::AddViewMatched(Candidate candidate, const CandidateMaterializer& materialize) {
    return AddImpl(std::move(candidate), materialize, true);
}
bool SearchAccumulator::AdmitView(const CandidateView& candidate) {
    // performance-closure-and-multisurface-acceleration 7.3: conservative
    // top-N admission. Only a STRICTLY worse primary key can be skipped: the
    // comparator's secondary keys include folder text the view does not carry
    // (the folder is materialized only for results), so ties admit. Reports
    // false either when the row cannot enter the bounded set (and then sets
    // the response's truncated flag, because matches exceeded maxResults even
    // though no heap pop ran) or when cancellation was observed (which does
    // not set truncated).
    if (response_.cancelled) return false;
    if (request_.maxResults == 0) return true;
    if (response_.results.size() < request_.maxResults) return true;
    if (request_.candidateFilter || request_.candidateFilterRequiresFolder
        || folderNeededForEvaluation_) {
        return true;
    }
    const SearchResult& worst = response_.results.front();
    const bool descending = request_.descending;
    const auto verdict = [this](const bool admit) {
        if (!admit) response_.truncated = true;
        return admit;
    };
    switch (request_.sortField) {
        case SortField::Relevance: {
            int primary = static_cast<int>(ClassifyMatchView(candidate.name, request_.query.primaryTerm))
                - static_cast<int>(worst.tier);
            if (primary == 0) {
                primary = static_cast<int>(candidate.name.size())
                    - static_cast<int>(worst.candidate.name.size());
            }
            return verdict(descending ? primary >= 0 : primary <= 0);
        }
        case SortField::Filename: {
            const int primary = CompareOrdinal16(candidate.name, worst.candidate.name);
            return verdict(descending ? primary >= 0 : primary <= 0);
        }
        case SortField::Size: {
            const int primary = candidate.sizeBytes < worst.candidate.sizeBytes ? -1
                : candidate.sizeBytes > worst.candidate.sizeBytes ? 1 : 0;
            return verdict(descending ? primary >= 0 : primary <= 0);
        }
        case SortField::Modified: {
            const int primary = candidate.modifiedTime < worst.candidate.modifiedTime ? -1
                : candidate.modifiedTime > worst.candidate.modifiedTime ? 1 : 0;
            return verdict(descending ? primary >= 0 : primary <= 0);
        }
        case SortField::Created: {
            const int primary = candidate.createdTime < worst.candidate.createdTime ? -1
                : candidate.createdTime > worst.candidate.createdTime ? 1 : 0;
            return verdict(descending ? primary >= 0 : primary <= 0);
        }
        case SortField::AllocatedSize: {
            // indexed-search-allocated-size D6: an unknown allocated size sorts
            // after every known value in BOTH directions; mirrored here.
            const bool viewKnown = candidate.allocatedSizeKnown;
            const bool worstKnown = worst.candidate.allocatedSizeKnown;
            if (viewKnown != worstKnown) return verdict(viewKnown);
            if (!viewKnown) return true;
            const int primary = candidate.allocatedSizeBytes < worst.candidate.allocatedSizeBytes ? -1
                : candidate.allocatedSizeBytes > worst.candidate.allocatedSizeBytes ? 1 : 0;
            return verdict(descending ? primary >= 0 : primary <= 0);
        }
        case SortField::Path:
            return true;  // folder text is not available pre-materialization
    }
    return true;
}
bool SearchAccumulator::AddImpl(Candidate candidate, const CandidateMaterializer& materialize,
                                const bool queryPredicatesAlreadyEvaluated) {
    if (response_.cancelled) return false;
    const size_t chunkSize = (std::max)(size_t{1}, request_.chunkSize);
    if (response_.candidatesVisited % chunkSize == 0 && cancelled_ && cancelled_()) {
        response_.cancelled = true;
        response_.results.clear();
        return false;
    }
    ++response_.candidatesVisited;
    if (!queryPredicatesAlreadyEvaluated && !request_.query.MatchesWithoutFolder(candidate)) return true;
    bool materialized = false;
    if ((folderNeededForEvaluation_ || request_.candidateFilterRequiresFolder) && materialize) {
        if (!materialize(candidate)) return true;
        materialized = true;
    }
    if (request_.candidateFilter && !request_.candidateFilter(candidate)) return true;
    if (!InScope(candidate, request_.scope, request_.scopePath, request_.persistentOnly)) return true;
    if (!queryPredicatesAlreadyEvaluated && !request_.query.Matches(candidate)) return true;
    if (!materialized && !folderNeededForEvaluation_ && materialize && !materialize(candidate)) return true;
    const MatchTier tier = ClassifyMatch(candidate, request_.query.primaryTerm);
    response_.results.push_back({std::move(candidate), tier});
    if (diagnostics_ != nullptr) {
        ++diagnostics_->matchedRows;
    }
    if (request_.maxResults != 0) {
        // performance-closure-and-multisurface-acceleration 7.3 correctness
        // fix: the bounded heap's root must be the WORST result so the
        // pop_heap/pop_back pair discards the worst and the retained set is
        // the documented TOP-N ("capped result sets are the TOP-N by the
        // requested sort"). The previous swapped comparator made the root the
        // BEST result and discarded it, so capped searches retained the worst
        // N; ffsearch_admission_tests' differential proof caught it.
        const auto comparator = [field = request_.sortField, descending = request_.descending]
                                (const SearchResult& left, const SearchResult& right) {
            return ResultLess(left, right, field, descending);
        };
        std::push_heap(response_.results.begin(), response_.results.end(), comparator);
        if (response_.results.size() > request_.maxResults) {
            std::pop_heap(response_.results.begin(), response_.results.end(), comparator);
            response_.results.pop_back();
            response_.truncated = true;
        }
    }
    return true;
}
SearchResponse SearchAccumulator::Finish() {
    if (response_.cancelled) {
        response_.results.clear();
        return std::move(response_);
    }
    SortResults(response_.results, request_.sortField, request_.descending);
    return std::move(response_);
}
SearchResponse ExecuteSearchCore(std::vector<Candidate>* mutableCandidates,
                                 const std::vector<Candidate>& candidates, const SearchRequest& request,
                                 const CandidateMaterializer& materialize,
                                 const std::function<bool()>& cancelled) {
    SearchResponse response;
    const size_t chunkSize = (std::max)(size_t{1}, request.chunkSize);
    const bool folderNeededForEvaluation = request.scope != SearchScope::AllIndexedLocations ||
                                           request.query.requiresFolder;
    for (size_t index = 0; index < candidates.size(); ++index) {
        if (index % chunkSize == 0 && cancelled && cancelled()) {
            response.cancelled = true;
            response.results.clear();
            return response;
        }
        ++response.candidatesVisited;
        Candidate* mutableCandidate = mutableCandidates == nullptr ? nullptr : &(*mutableCandidates)[index];
        const Candidate& candidate = mutableCandidate == nullptr ? candidates[index] : *mutableCandidate;
        if (!request.query.MatchesWithoutFolder(candidate)) continue;
        bool materialized = false;
        if ((folderNeededForEvaluation || request.candidateFilterRequiresFolder) && materialize &&
            (mutableCandidate == nullptr || !materialize(*mutableCandidate))) continue;
        materialized = folderNeededForEvaluation || request.candidateFilterRequiresFolder;
        if (request.candidateFilter && !request.candidateFilter(candidate)) continue;
        if (!InScope(candidate, request.scope, request.scopePath, request.persistentOnly)) continue;
        if (!request.query.Matches(candidate)) continue;
        if (!materialized && !folderNeededForEvaluation && materialize &&
            (mutableCandidate == nullptr || !materialize(*mutableCandidate))) continue;
        response.results.push_back({candidate, ClassifyMatch(candidate, request.query.primaryTerm)});
        if (request.maxResults != 0) {
            // memory-bounded-indexed-scale: bounded top-N selection by the
            // requested total order (heap root = current worst). Truncation
            // never depends on enumeration order, which differs between hot
            // projection rows and cold durable-store rows.
            std::push_heap(response.results.begin(), response.results.end(),
                           [field = request.sortField, descending = request.descending]
                           (const SearchResult& a, const SearchResult& b) {
                               return ResultLess(a, b, field, descending);
                           });
            if (response.results.size() > request.maxResults) {
                std::pop_heap(response.results.begin(), response.results.end(),
                              [field = request.sortField, descending = request.descending]
                              (const SearchResult& a, const SearchResult& b) {
                                  return ResultLess(a, b, field, descending);
                              });
                response.results.pop_back();
                response.truncated = true;
            }
        }
    }
    SortResults(response.results, request.sortField, request.descending);
    return response;
}
SearchResponse ExecuteSearch(const std::vector<Candidate>& candidates, const SearchRequest& request,
                             const std::function<bool()>& cancelled) {
    return ExecuteSearchCore(nullptr, candidates, request, {}, cancelled);
}
SearchResponse ExecuteSearchLazy(std::vector<Candidate>& candidates, const SearchRequest& request,
                                  const CandidateMaterializer& materialize,
                                  const std::function<bool()>& cancelled) {
    return ExecuteSearchCore(&candidates, candidates, request, materialize, cancelled);
}
SearchResponse ExecuteSearchViews(const CandidateViewEnumerator& enumerate,
                                  const SearchRequest& request,
                                  const CandidateViewMaterializer& materialize,
                                  const std::function<bool()>& cancelled,
                                  SearchDiagnostics* diagnostics) {
    SearchResponse response;
    if (!request.query.viewCompatible) return response;
    const size_t chunkSize = (std::max)(size_t{1}, request.chunkSize);
    const bool folderNeededForEvaluation = request.scope != SearchScope::AllIndexedLocations ||
                                           request.query.requiresFolder;
    bool stopped = false;
    auto visitLambda = [&](const CandidateView& candidate) {
        if (stopped) return false;
        if (response.candidatesVisited % chunkSize == 0 && cancelled && cancelled()) {
            response.cancelled = true;
            response.results.clear();
            stopped = true;
            return false;
        }
        ++response.candidatesVisited;
        if (diagnostics != nullptr && diagnostics->projectionOnly) return true;
        std::optional<Candidate> ownedCandidate;
        if (diagnostics != nullptr) ++diagnostics->pathNeededDecisions;
        if (folderNeededForEvaluation) {
            if (!request.query.MatchesViewWithoutFolder(candidate,
                                                        diagnostics != nullptr ? &diagnostics->match : nullptr)) {
                return true;
            }
            if (diagnostics != nullptr) ++diagnostics->pathLookups;
            ownedCandidate = materialize(candidate);
            if (!ownedCandidate || (request.candidateFilter && !request.candidateFilter(*ownedCandidate)) ||
                !InScope(*ownedCandidate, request.scope, request.scopePath, request.persistentOnly) ||
                !request.query.Matches(*ownedCandidate)) {
                return true;
            }
        } else {
            if (!request.query.MatchesView(candidate,
                                           diagnostics != nullptr ? &diagnostics->match : nullptr)) return true;
            if (diagnostics != nullptr) ++diagnostics->pathLookups;
            ownedCandidate = materialize(candidate);
            if (!ownedCandidate || (request.candidateFilter && !request.candidateFilter(*ownedCandidate))) {
                return true;
            }
        }
        const auto matchedRowStart = diagnostics != nullptr ? std::chrono::steady_clock::now()
                                                             : std::chrono::steady_clock::time_point{};
        Candidate owned = std::move(*ownedCandidate);
        const MatchTier tier = ClassifyMatch(owned, request.query.primaryTerm);
        response.results.push_back({std::move(owned), tier});
        if (diagnostics != nullptr) {
            ++diagnostics->matchedRows;
            diagnostics->matchedRowHandlingMs +=
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - matchedRowStart).count();
        }
        if (request.maxResults != 0) {
            // memory-bounded-indexed-scale: bounded top-N by the total order
            // (see ExecuteSearchCore); enumeration continues so a cold/hot
            // source mix cannot change which rows survive the cap.
            std::push_heap(response.results.begin(), response.results.end(),
                           [field = request.sortField, descending = request.descending]
                           (const SearchResult& a, const SearchResult& b) {
                               return ResultLess(a, b, field, descending);
                           });
            if (response.results.size() > request.maxResults) {
                std::pop_heap(response.results.begin(), response.results.end(),
                              [field = request.sortField, descending = request.descending]
                              (const SearchResult& a, const SearchResult& b) {
                                  return ResultLess(a, b, field, descending);
                              });
                response.results.pop_back();
                response.truncated = true;
            }
        }
        return true;
    };
    const CandidateViewVisitor visit(visitLambda);
    enumerate(visit);
    if (response.cancelled) return response;
    SortResults(response.results, request.sortField, request.descending);
    return response;
}
PathReconstruction ReconstructPath(const std::vector<Candidate>& entries, size_t entryIndex) {
    PathReconstruction result;
    if (entryIndex >= entries.size()) return result;
    std::unordered_map<EntryKey, const Candidate*, EntryKeyHash> byId;
    for (const auto& entry : entries) byId.emplace(EntryKey{entry.volumeId, entry.id}, &entry);
    const Candidate* current = &entries[entryIndex];
    std::unordered_set<EntryKey, EntryKeyHash> seen;
    while (current != nullptr) {
        if (!seen.insert(EntryKey{current->volumeId, current->id}).second) {
            result.unresolvedParentId = current->id;
            break;
        }
        result.segments.push_back(current->name);
        if (current->parentId == 0 || current->parentId == current->id) {
            result.complete = true;
            break;
        }
        const auto parent = byId.find({current->volumeId, current->parentId});
        if (parent == byId.end()) {
            result.unresolvedParentId = current->parentId;
            break;
        }
        current = parent->second;
    }
    std::reverse(result.segments.begin(), result.segments.end());
    return result;
}
} // namespace ffsearch
read: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/i...
read: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/s...
    // tasks.md 7.4's projection half: drops every entry belonging to a
    // forgotten volume (dense-slot tombstoning, id->index and
    // parent->children rows) -- equivalent to calling Remove on each of
    // the volume's entries. Interned names are not reclaimed, exactly as
    // with individual Remove (the pool is reclaimed on the next full
    // rebuild -- design.md "Risks" name-pool eviction note).
    // large-index-lifecycle-and-search-scalability 4.4: routes through
    // RemoveBatch's bulk accelerator maintenance.
    void RemoveVolume(VolumeRowId volumeRowId, bool keepFilenameIndexCurrent = true);
#ifdef FF_INDEXSTORE_TESTING
    // large-index-lifecycle-and-search-scalability 4.2: benchmark-only
    // differential-isolation seam for the removal-cost attribution matrix
    // (bench_rebuild_isolation). Compiled only into the isolation benchmark
    // target via FF_INDEXSTORE_TESTING; production builds contain none of
    // this. RemovalMaintenanceMode::Production (the default) preserves exact
    // production removal semantics; the other modes skip ONE kind of
    // per-entry secondary maintenance so the matrix can attribute removal
    // wall-time to filename-accelerator maintenance vs. parent/child unlink
    // vs. the residual id-map/free-list work. Never set in production code.
    enum class RemovalMaintenanceMode : uint8_t {
        Production = 0,
        SkipFilenameMaintenance = 1,
        SkipChildUnlink = 2,
        SkipBoth = 3,
    };
    void SetRemovalMaintenanceMode(RemovalMaintenanceMode mode) noexcept {
        removalMaintenanceMode_ = mode;
    }
#endif
    // large-index-lifecycle-and-search-scalability review-remediation (P2-A):
    // testing-only seam that force-marks a UTF-16 unit as fold-exceptional
    // (simulates a host ordinal table that equates it with ASCII, e.g. U+017F
    // LONG S vs 's'), so the candidate-completeness machinery for the
    // exceptional-name fallback is deterministically testable on hosts whose
    // real table has no cross-ASCII fold classes. The DECLARATION is visible
    // to every build so static analysis sees the seam; the definition and its
    // override state are compiled only with FF_INDEXSTORE_TESTING (test and
    // bench targets), so production binaries contain no seam behavior. Never
    // set in production.
    static void SetTestingFoldExceptionalUnit(char16_t unit, bool exceptional);
    const ProjectionEntry* Find(VolumeRowId volumeRowId, FileId frn) const;
    ProjectionEntryCursor BeginEntryCursor() const;
    // Returns a conservative candidate set for an already ASCII-folded
    // simple literal. nullopt means the secondary accelerator is ineligible
    // or unavailable, so callers must use the canonical live-entry scan.
    // The candidate set is a CONSERVATIVE SUPERSET of the rows the canonical
    // (CompareStringOrdinal ignore-case) matcher would accept: every row the
    // matcher accepts is present, though the set may contain false positives
    // that only the canonical matcher rejects. False negatives are forbidden
    // (review-remediation P2-A enforces this for every host ordinal table).
    // Returned slot indices belong to this projection generation and are
    // valid only while the caller holds the projection's synchronization.
    std::optional<std::vector<uint32_t>> FilenameCandidateIndices(
        std::u16string_view foldedNeedle, size_t maximumCandidates = 0) const;
    // task 2.3: parent->children lookup for directory-listing/Column-View
    // style access. Returns empty span if the parent has no known children
    // (which is not the same as the parent itself being unknown).
    std::span<const uint32_t> ChildIndices(VolumeRowId volumeRowId, FileId parentFrn) const;
    // memory-bounded-indexed-scale 5.x: drops one volume's resident rows and
    // marks the volume cold -- an explicit policy hook (e.g. unloading an
    // offline volume's working set) that also gives tests deterministic
    // forced-eviction state without depending on LRU victim order. Data stays
    // durable in Store; a no-op for an unknown/empty volume.
    void EvictVolume(VolumeRowId volumeRowId);
    // file-preview-and-properties §6.2 / storage-analysis §3.1: walks the
    // in-memory subtree rooted at `parentFrn` and returns the aggregate item
    // count (all descendants, excluding the root itself) and the total
    // sizeBytes sum across every entry in that subtree. Returns std::nullopt
    // if the root is unknown, distinguishing "not indexed" from "empty folder".
    // allocated-size-storage-accuracy D7: the allocated-size subtotal covers
    // only descendants whose allocated size is known, with the completeness
    // pair (known/total counts); Complete only when every descendant is
    // known. Sums saturate at UINT64_MAX with completeness cleared on
    // saturation -- a wrapped-looking total must never be presented as exact.
    struct FolderAggregate {
        uint64_t itemCount = 0;
        uint64_t totalSizeBytes = 0;
        uint64_t totalAllocatedSizeBytes = 0;
        uint64_t allocatedBytesKnownCount = 0;
        uint64_t allocatedBytesTotalCount = 0;
        bool complete = true;
    };
    std::optional<FolderAggregate> GetFolderAggregate(VolumeRowId volumeRowId, FileId parentFrn) const;
    const ProjectionEntry& EntryAt(uint32_t index) const { return entries_[index]; }
    struct PathResult {
        std::u16string path; // backslash-joined names, root-to-leaf, leaf included
        // false if the walk stopped early because it hit an entry not
        // present in the projection, or detected revisiting an FRN already
        // seen in this same walk (task 2.4's defensive cycle detection) --
        // in both cases `path` holds whatever prefix was resolved.
        bool reachedRoot = false;
    };
    // task 2.4: walks parent references and concatenates interned names
    // on demand -- never reads a precomputed path field.
    PathResult ReconstructPath(VolumeRowId volumeRowId, FileId frn) const;
    // nonblocking-indexed-read-plane 4.3: the same bounded walk with a
    // cooperative cancellation check between parent links.
    PathResult ReconstructPathUntil(VolumeRowId volumeRowId, FileId frn,
                                     const std::function<bool()>& cancelled) const;
    // task 3.1: rebuilds this volume's portion of the projection from the
    // durable store in a single streaming pass, interning names and
    // building the parent->children index as rows arrive. Returns false when
    // the durable stream fails; the volume is left cold with no partial
    // projection so callers cannot publish an incomplete rebuild.
    // expectedEntryCount pre-sizes allocations (task 2.5/D4); the caller
    // gets this from Store::GetVolumeMetadata(...)->entryCount.
    bool RebuildVolumeFromStore(Store& store, VolumeRowId volumeRowId, uint64_t expectedEntryCount);
    size_t EntryCount() const noexcept { return idToIndex_.size(); }
    const NamePool& Names() const noexcept { return namePool_; }
[127 more lines in file. Use offset=400 to continue.]
#include "ffindexstore/Projection.h"
#include <algorithm>
#include <atomic>
#include <iterator>
#include <limits>
#include <windows.h>
#include "ffindexstore/Store.h"
namespace ffindexstore {
namespace {
constexpr uint32_t kFileAttributeDirectory = 0x00000010;
constexpr size_t kMaxProjectionPathDepth = 4096;
bool IsAsciiLower(std::u16string_view name) {
    for (const char16_t ch : name) {
        if (ch > 0x7f || (ch >= u'A' && ch <= u'Z')) return false;
    }
    return true;
}
bool IsAscii(std::u16string_view name) {
    return std::all_of(name.begin(), name.end(), [](const char16_t ch) { return ch <= 0x7f; });
}
std::u16string FoldAscii(std::u16string_view name) {
    std::u16string folded;
    folded.reserve(name.size());
    for (const char16_t ch : name) {
        folded.push_back(ch >= u'A' && ch <= u'Z' ? static_cast<char16_t>(ch + (u'a' - u'A')) : ch);
    }
    return folded;
}
// large-index-lifecycle-and-search-scalability review-remediation (P2-A):
// the canonical matcher equates two UTF-16 units case-insensitively through
// the HOST's ordinal table (CompareStringOrdinal(..., TRUE)), which the
// ASCII-folded trigram accelerator must never contradict. The accelerator is
// a conservative superset of canonical matches exactly when every name that
// could match an ASCII needle THROUGH a non-ASCII code unit is retained
// separately from the posting intersection. That exceptional set is derived
// from the actual host comparison function -- never from Unicode casefold
// tables or assumed equivalences: a unit is fold-exceptional iff the host
// equates it with any ASCII unit. Surrogate units (astral pairs and unpaired
// surrogates) are always treated as exceptional, because pair semantics
// cannot be probed unit-wise and false negatives are forbidden.
// Measured on the campaign host: NO BMP unit is fold-exceptional (U+017F
// LONG S is NOT equal to 's'/'S', U+212A KELVIN is not 'k'/'K', U+0131 is
// not 'i'/'I', and the exhaustive 65,280 x 128 scan found zero cross-ASCII
// fold classes), so ordinary corpora pay nothing for this guarantee and a
// host whose table ever changed is handled automatically at run time.
enum class FoldProbe : uint8_t { Unknown = 0, Ordinary = 1, Exceptional = 2 };
std::atomic<uint8_t> gFoldProbe[65536]; // FoldProbe per UTF-16 unit; idempotent lazy fill
#ifdef FF_INDEXSTORE_TESTING
// Review-remediation P2-A testing seam: force-mark units fold-exceptional to
// simulate a host ordinal table that equates them with ASCII. Never set in
// production code (see header seam).
std::atomic<uint8_t> gTestingForceFoldExceptional[65536];
#endif
bool UnitMayFoldToAscii(const char16_t unit) {
    const uint16_t raw = static_cast<uint16_t>(unit);
    if (raw <= 0x7f) return false;
    if (raw >= 0xd800 && raw <= 0xdfff) return true; // conservative: surrogates
#ifdef FF_INDEXSTORE_TESTING
    if (gTestingForceFoldExceptional[raw].load(std::memory_order_relaxed) != 0) return true;
#endif
    uint8_t state = gFoldProbe[raw].load(std::memory_order_acquire);
    if (state == static_cast<uint8_t>(FoldProbe::Unknown)) {
        bool exceptional = false;
        const wchar_t wide = static_cast<wchar_t>(unit);
        for (int ascii = 0; ascii < 0x80 && !exceptional; ++ascii) {
            const wchar_t other = static_cast<wchar_t>(ascii);
            const int verdict = CompareStringOrdinal(&wide, 1, &other, 1, TRUE);
            // A failed comparison is classified exceptional: fail closed, so
            // a transient API failure can only cost candidates, never a miss.
            exceptional = verdict == CSTR_EQUAL || verdict == 0;
        }
        state = static_cast<uint8_t>(exceptional ? FoldProbe::Exceptional : FoldProbe::Ordinary);
        gFoldProbe[raw].store(state, std::memory_order_release);
    }
    return state == static_cast<uint8_t>(FoldProbe::Exceptional);
}
bool NameNeedsExceptionalFallback(const std::u16string_view name) {
    for (const char16_t ch : name) {
        if (static_cast<uint16_t>(ch) > 0x7f && UnitMayFoldToAscii(ch)) return true;
    }
    return false;
}
uint64_t PackTrigram(std::u16string_view value, size_t offset) {
    return static_cast<uint64_t>(value[offset]) |
           (static_cast<uint64_t>(value[offset + 1]) << 16) |
           (static_cast<uint64_t>(value[offset + 2]) << 32);
}
std::vector<uint64_t> UniqueTrigrams(std::u16string_view value) {
    std::vector<uint64_t> result;
    if (value.size() < 3) return result;
    result.reserve(value.size() - 2);
    for (size_t index = 0; index + 2 < value.size(); ++index) {
        result.push_back(PackTrigram(value, index));
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
void InsertSorted(std::vector<uint32_t>& values, uint32_t value) {
    const auto it = std::lower_bound(values.begin(), values.end(), value);
    if (it == values.end() || *it != value) values.insert(it, value);
}
void EraseSorted(std::vector<uint32_t>& values, uint32_t value) {
    const auto it = std::lower_bound(values.begin(), values.end(), value);
    if (it != values.end() && *it == value) values.erase(it);
}
// large-index-lifecycle-and-search-scalability 4.4: batches above this size
// drop the filename accelerator wholesale and rebuild it once instead of
// paying per-entry sorted-vector posting erases (quadratic in index size).
// Small batches keep the per-entry path so the accelerator stays hot without
// a full O(N) rebuild; both paths leave identical observable state.
constexpr size_t kPerEntryFilenameRemovalThreshold = 256;
} // namespace
void Projection::SetMaximumMemoryBytes(const size_t maximumBytes) {
    maximumMemoryBytes_ = maximumBytes == 0 ? kDefaultMaximumMemoryBytes : maximumBytes;
    // An explicit limit change is synchronous and strict even during a bulk
    // scan; ingestion-path watermarks still defer per-record compaction but an
    // explicit caller expectation (e.g. tests or a settings change) must not be
    // silently ignored until the scan ends.
    const bool wasEnforcing = enforcingMemoryLimit_;
    enforcingMemoryLimit_ = true;
    EnforceMemoryLimitTo(maximumMemoryBytes_);
    enforcingMemoryLimit_ = wasEnforcing;
}
[1120 more lines in file. Use offset=141 to continue.]
grep: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/s...
Projection.cpp-177-         + idToIndex_.MemoryUsageBytes()
Projection.cpp-178-         + parentToChildren_.MemoryUsageBytes()
Projection.cpp-179-         + namePool_.MemoryUsageBytes()
Projection.cpp:180:         + filenameExceptionalEntries_.capacity() * sizeof(uint32_t)
Projection.cpp-181-         + lastAccessTimes_.capacity() * sizeof(uint64_t);
Projection.cpp-182-     for (const auto& [trigram, posting] : filenameTrigramPostings_) {
Projection.cpp-183-         (void)trigram;
Projection.cpp-269-     result.filenameIndexApproxNodeBytes =
Projection.cpp-270-         result.filenameIndexDistinctTrigrams * sizeof(uint64_t)
Projection.cpp-271-         + result.filenameIndexPostingHeaderBytes;
Projection.cpp:272:     result.filenameIndexExceptionalCount = filenameExceptionalEntries_.size();
Projection.cpp-273-     result.filenameIndexExceptionalSlots = filenameExceptionalEntries_.capacity();
Projection.cpp-274-     result.filenameIndexExceptionalBytes =
Projection.cpp-275-         result.filenameIndexExceptionalSlots * sizeof(uint32_t);
Projection.cpp-270-         result.filenameIndexDistinctTrigrams * sizeof(uint64_t)
Projection.cpp-271-         + result.filenameIndexPostingHeaderBytes;
Projection.cpp-272-     result.filenameIndexExceptionalCount = filenameExceptionalEntries_.size();
Projection.cpp:273:     result.filenameIndexExceptionalSlots = filenameExceptionalEntries_.capacity();
Projection.cpp-274-     result.filenameIndexExceptionalBytes =
Projection.cpp-275-         result.filenameIndexExceptionalSlots * sizeof(uint32_t);
Projection.cpp-276-     result.filenameIndexBytes = result.filenameIndexPostingBytes
Projection.cpp-603- void Projection::InvalidateFilenameIndex() noexcept {
Projection.cpp-604-     filenameIndexValid_ = false;
Projection.cpp-605-     filenameTrigramPostings_.clear();
Projection.cpp:606:     filenameExceptionalEntries_.clear();
Projection.cpp-607- }
Projection.cpp-608- 
Projection.cpp-609- void Projection::AddFilenameIndexEntry(const uint32_t index) {
Projection.cpp-611-     try {
Projection.cpp-612-         const std::u16string_view name = namePool_.Get(entries_[index].nameId);
Projection.cpp-613-         if (NameNeedsExceptionalFallback(name)) {
Projection.cpp:614:             if (filenameIndexBuilding_) filenameExceptionalEntries_.push_back(index);
Projection.cpp-615-             else InsertSorted(filenameExceptionalEntries_, index);
Projection.cpp-616-         }
Projection.cpp-617-         const std::u16string folded = FoldAscii(name);
Projection.cpp-612-         const std::u16string_view name = namePool_.Get(entries_[index].nameId);
Projection.cpp-613-         if (NameNeedsExceptionalFallback(name)) {
Projection.cpp-614-             if (filenameIndexBuilding_) filenameExceptionalEntries_.push_back(index);
Projection.cpp:615:             else InsertSorted(filenameExceptionalEntries_, index);
Projection.cpp-616-         }
Projection.cpp-617-         const std::u16string folded = FoldAscii(name);
Projection.cpp-618-         for (const uint64_t trigram : UniqueTrigrams(folded)) {
Projection.cpp-630-     try {
Projection.cpp-631-         // Must use the exact predicate AddFilenameIndexEntry used, or stale
Projection.cpp-632-         // exceptional entries would linger (or real ones get dropped).
Projection.cpp:633:         if (NameNeedsExceptionalFallback(name)) EraseSorted(filenameExceptionalEntries_, index);
Projection.cpp-634-         for (const uint64_t trigram : UniqueTrigrams(FoldAscii(name))) {
Projection.cpp-635-             const auto it = filenameTrigramPostings_.find(trigram);
Projection.cpp-636-             if (it == filenameTrigramPostings_.end()) continue;
Projection.cpp-644- 
Projection.cpp-645- void Projection::RebuildFilenameIndex() {
Projection.cpp-646-     filenameTrigramPostings_.clear();
Projection.cpp:647:     filenameExceptionalEntries_.clear();
Projection.cpp-648-     filenameIndexValid_ = true;
Projection.cpp-649-     filenameIndexBuilding_ = true;
Projection.cpp-650-     try {
Projection.cpp-654-             std::sort(posting.begin(), posting.end());
Projection.cpp-655-             posting.erase(std::unique(posting.begin(), posting.end()), posting.end());
Projection.cpp-656-         }
Projection.cpp:657:         std::sort(filenameExceptionalEntries_.begin(), filenameExceptionalEntries_.end());
Projection.cpp-658-         filenameExceptionalEntries_.erase(
Projection.cpp-659-             std::unique(filenameExceptionalEntries_.begin(), filenameExceptionalEntries_.end()),
Projection.cpp-660-             filenameExceptionalEntries_.end());
Projection.cpp-655-             posting.erase(std::unique(posting.begin(), posting.end()), posting.end());
Projection.cpp-656-         }
Projection.cpp-657-         std::sort(filenameExceptionalEntries_.begin(), filenameExceptionalEntries_.end());
Projection.cpp:658:         filenameExceptionalEntries_.erase(
Projection.cpp-659-             std::unique(filenameExceptionalEntries_.begin(), filenameExceptionalEntries_.end()),
Projection.cpp-660-             filenameExceptionalEntries_.end());
Projection.cpp-661-     } catch (...) {
Projection.cpp-656-         }
Projection.cpp-657-         std::sort(filenameExceptionalEntries_.begin(), filenameExceptionalEntries_.end());
Projection.cpp-658-         filenameExceptionalEntries_.erase(
Projection.cpp:659:             std::unique(filenameExceptionalEntries_.begin(), filenameExceptionalEntries_.end()),
Projection.cpp-660-             filenameExceptionalEntries_.end());
Projection.cpp-661-     } catch (...) {
Projection.cpp-662-         InvalidateFilenameIndex();
Projection.cpp-657-         std::sort(filenameExceptionalEntries_.begin(), filenameExceptionalEntries_.end());
Projection.cpp-658-         filenameExceptionalEntries_.erase(
Projection.cpp-659-             std::unique(filenameExceptionalEntries_.begin(), filenameExceptionalEntries_.end()),
Projection.cpp:660:             filenameExceptionalEntries_.end());
Projection.cpp-661-     } catch (...) {
Projection.cpp-662-         InvalidateFilenameIndex();
Projection.cpp-663-     }
Projection.cpp-944-     return ProjectionEntryCursor(*this);
Projection.cpp-945- }
Projection.cpp-946- 
Projection.cpp:947: std::optional<std::vector<uint32_t>> Projection::FilenameCandidateIndices(
Projection.cpp-948-     const std::u16string_view foldedNeedle, const size_t maximumCandidates) const {
Projection.cpp-949-     if (!filenameIndexValid_ || foldedNeedle.size() < 3 || !IsAscii(foldedNeedle)) {
Projection.cpp-950-         return std::nullopt;
Projection.cpp-1008-         // CanonicalMatches(query) is therefore a subset of this candidate set
Projection.cpp-1009-         // for every host ordinal table; false positives here are verified away
Projection.cpp-1010-         // by the authoritative canonical matcher.
Projection.cpp:1011:         if (!filenameExceptionalEntries_.empty()) {
Projection.cpp-1012-             const size_t maximumSize = (std::numeric_limits<size_t>::max)();
Projection.cpp-1013-             const size_t exceptionalCount = filenameExceptionalEntries_.size();
Projection.cpp-1014-             const size_t combinedSize = exceptionalCount > maximumSize - result.size()
Projection.cpp-1010-         // by the authoritative canonical matcher.
Projection.cpp-1011-         if (!filenameExceptionalEntries_.empty()) {
Projection.cpp-1012-             const size_t maximumSize = (std::numeric_limits<size_t>::max)();
Projection.cpp:1013:             const size_t exceptionalCount = filenameExceptionalEntries_.size();
Projection.cpp-1014-             const size_t combinedSize = exceptionalCount > maximumSize - result.size()
Projection.cpp-1015-                 ? maximumSize : result.size() + exceptionalCount;
Projection.cpp-1016-             std::vector<uint32_t> merged;
Projection.cpp-1017-             merged.reserve(maximumCandidates == 0 ? combinedSize
Projection.cpp-1018-                 : (std::min)(maximumCandidates == maximumSize ? maximumSize : maximumCandidates + 1,
Projection.cpp-1019-                              combinedSize));
Projection.cpp:1020:             std::set_union(result.begin(), result.end(), filenameExceptionalEntries_.begin(),
Projection.cpp-1021-                            filenameExceptionalEntries_.end(), std::back_inserter(merged));
Projection.cpp-1022-             if (maximumCandidates != 0 && merged.size() > maximumCandidates) {
Projection.cpp-1023-                 return std::nullopt;
Projection.cpp-1018-                 : (std::min)(maximumCandidates == maximumSize ? maximumSize : maximumCandidates + 1,
Projection.cpp-1019-                              combinedSize));
Projection.cpp-1020-             std::set_union(result.begin(), result.end(), filenameExceptionalEntries_.begin(),
Projection.cpp:1021:                            filenameExceptionalEntries_.end(), std::back_inserter(merged));
Projection.cpp-1022-             if (maximumCandidates != 0 && merged.size() > maximumCandidates) {
Projection.cpp-1023-                 return std::nullopt;
Projection.cpp-1024-             }
Projection.cpp-1239-     // quantity and would overstate the slack when stale copies are present.
Projection.cpp-1240-     slackBytes += parentToChildren_.ChildCapacityBytes()
Projection.cpp-1241-         - parentToChildren_.ChildLinkCount() * sizeof(uint32_t);
Projection.cpp:1242:     slackBytes += filenameExceptionalEntries_.capacity() * sizeof(uint32_t)
Projection.cpp-1243-         - filenameExceptionalEntries_.size() * sizeof(uint32_t);
Projection.cpp-1244-     for (const auto& [trigram, posting] : filenameTrigramPostings_) {
Projection.cpp-1245-         (void)trigram;
Projection.cpp-1240-     slackBytes += parentToChildren_.ChildCapacityBytes()
Projection.cpp-1241-         - parentToChildren_.ChildLinkCount() * sizeof(uint32_t);
Projection.cpp-1242-     slackBytes += filenameExceptionalEntries_.capacity() * sizeof(uint32_t)
Projection.cpp:1243:         - filenameExceptionalEntries_.size() * sizeof(uint32_t);
Projection.cpp-1244-     for (const auto& [trigram, posting] : filenameTrigramPostings_) {
Projection.cpp-1245-         (void)trigram;
Projection.cpp-1246-         slackBytes += (posting.capacity() - posting.size()) * sizeof(uint32_t);
Projection.cpp-1249- 
Projection.cpp-1250-     namePool_.ShrinkToFit();
Projection.cpp-1251-     parentToChildren_.ShrinkToFit();
Projection.cpp:1252:     filenameExceptionalEntries_.shrink_to_fit();
Projection.cpp-1253-     for (auto& [trigram, posting] : filenameTrigramPostings_) {
Projection.cpp-1254-         (void)trigram;
Projection.cpp-1255-         posting.shrink_to_fit();
read: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/s...
std::optional<std::vector<uint32_t>> Projection::FilenameCandidateIndices(
    const std::u16string_view foldedNeedle, const size_t maximumCandidates) const {
    if (!filenameIndexValid_ || foldedNeedle.size() < 3 || !IsAscii(foldedNeedle)) {
        return std::nullopt;
    }
    try {
        std::vector<uint32_t> result;
        const std::vector<uint64_t> trigrams = UniqueTrigrams(foldedNeedle);
        std::vector<const std::vector<uint32_t>*> postings;
        postings.reserve(trigrams.size());
        bool anyTrigramMissing = false;
        for (const uint64_t trigram : trigrams) {
            const auto it = filenameTrigramPostings_.find(trigram);
            if (it == filenameTrigramPostings_.end()) {
                // large-index-lifecycle-and-search-scalability review-lane C
                // Finding 7: one missing needle trigram proves no row can
                // match through its ASCII fold -- the candidate set is then
                // exactly the exceptional merge of an empty intersection,
                // not a re-intersection over the postings that happened to
                // exist.
                anyTrigramMissing = true;
                break;
            }
            postings.push_back(&it->second);
        }
        if (!anyTrigramMissing && !postings.empty()) {
            std::sort(postings.begin(), postings.end(), [](const auto* left, const auto* right) {
                return left->size() < right->size();
            });
            if (maximumCandidates != 0 && postings.front()->size() > maximumCandidates) {
                return std::nullopt;
            }
            result = *postings.front();
            std::vector<uint32_t> scratch;
            for (size_t index = 1; index < postings.size() && !result.empty(); ++index) {
                scratch.clear();
                std::set_intersection(result.begin(), result.end(), postings[index]->begin(), postings[index]->end(),
                                      std::back_inserter(scratch));
                if (maximumCandidates != 0 && scratch.size() > maximumCandidates) {
                    return std::nullopt;
                }
                result.swap(scratch);
            }
        }
        if (maximumCandidates != 0 && result.size() > maximumCandidates) {
            return std::nullopt;
        }
        // large-index-lifecycle-and-search-scalability 6.3 + review-
        // remediation (P2-A): the candidate set is the posting intersection
        // MERGED with the fold-exceptional rows. For a name that contains no
        // exceptional unit, a canonical match keeps its aligned span entirely
        // ASCII (a non-ASCII unit there would have to fold to the aligned
        // ASCII needle unit -- which is exactly what makes it exceptional),
        // so the span's ASCII-folded trigrams are in the name's folded
        // multiset and the posting intersection already retains the row.
        // Every remaining canonical match goes through a non-ASCII unit the
        // HOST ordinal table equates with ASCII (or a surrogate), i.e. an
        // exceptional name -- retained by this merge. The pre-6.3 code unioned
        // ALL non-ASCII rows, which added non-exceptional false positives and
        // at any Unicode population above the caller's cap forced every lookup
        // to nullopt (the accelerator was unusable at 200k+ rows).
        // CanonicalMatches(query) is therefore a subset of this candidate set
        // for every host ordinal table; false positives here are verified away
        // by the authoritative canonical matcher.
        if (!filenameExceptionalEntries_.empty()) {
            const size_t maximumSize = (std::numeric_limits<size_t>::max)();
            const size_t exceptionalCount = filenameExceptionalEntries_.size();
            const size_t combinedSize = exceptionalCount > maximumSize - result.size()
                ? maximumSize : result.size() + exceptionalCount;
            std::vector<uint32_t> merged;
            merged.reserve(maximumCandidates == 0 ? combinedSize
                : (std::min)(maximumCandidates == maximumSize ? maximumSize : maximumCandidates + 1,
                             combinedSize));
            std::set_union(result.begin(), result.end(), filenameExceptionalEntries_.begin(),
                           filenameExceptionalEntries_.end(), std::back_inserter(merged));
            if (maximumCandidates != 0 && merged.size() > maximumCandidates) {
                return std::nullopt;
            }
            return merged;
        }
        return result;
    } catch (...) {
        // The index is an accelerator only. A transient allocation failure in
        // candidate materialization must leave the canonical scan available.
        return std::nullopt;
    }
}
std::span<const uint32_t> Projection::ChildIndices(VolumeRowId volumeRowId, FileId parentFrn) const {
    const EntryKey key{volumeRowId, parentFrn};
    const uint32_t* index = idToIndex_.find(key);
    if (index != nullptr) TouchIndex(*index);
    return parentToChildren_.find(key);
}
std::optional<Projection::FolderAggregate> Projection::GetFolderAggregate(VolumeRowId volumeRowId, FileId parentFrn) const {
    FolderAggregate result = {0, 0};
    const ProjectionEntry* root = Find(volumeRowId, parentFrn);
    if (root == nullptr) {
        return std::nullopt;
    }
    std::vector<FileId> stack;
    stack.push_back(parentFrn);
    // allocated-size-storage-accuracy D7: checked, saturating addition --
    // a representationally impossible total must not wrap into a
    // plausible-looking number; on saturation the aggregate is marked
    // incomplete instead.
    const auto saturatingAdd = [](uint64_t& destination, uint64_t value, bool& overflowed) {
        if (std::numeric_limits<uint64_t>::max() - destination < value) {
            destination = std::numeric_limits<uint64_t>::max();
            overflowed = true;
            return;
        }
[199 more lines in file. Use offset=1062 to continue.]
grep: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/I...
IndexPipeline.cpp:13: namespace ffengine {
IndexPipeline.cpp:15: namespace {
IndexPipeline.cpp:21: constexpr uint32_t kFileAttributeDirectory = 0x00000010;
IndexPipeline.cpp:25: constexpr uint32_t kUsnReasonFileDelete = 0x00000200;
IndexPipeline.cpp:34: std::wstring ToWString(std::u16string_view text) {
IndexPipeline.cpp:38: std::u16string ToU16String(std::wstring_view text) {
IndexPipeline.cpp:42: std::wstring NormalizeVolumePrefix(std::wstring value) {
IndexPipeline.cpp:56: std::wstring StripVolumeRootName(const ffindexstore::Projection& projection,
IndexPipeline.cpp:81: bool MaterializeCandidateFolder(const ffindexstore::Projection& projection,
IndexPipeline.cpp:108: bool MaterializeCandidateFolderDurable(
IndexPipeline.cpp:177: struct LightweightHotRow {
IndexPipeline.cpp:236: std::wstring CandidateCanonicalPath(const ffsearch::Candidate& candidate) {
IndexPipeline.cpp:243: bool CandidateIncludedByRules(
IndexPipeline.cpp:314: std::optional<ffindexstore::EntryFieldMask> ToEntryFieldMask(ffprotocol::UsnDeltaFieldMask fields) {
IndexPipeline.cpp:334: constexpr uint64_t kWalForceCheckpointThresholdBytes = 4000ULL * 4096;
IndexPipeline.cpp:337: constexpr uint32_t kErrorNotEnoughMemory = 8;
IndexPipeline.cpp:339: void UpdatePeak(std::atomic<size_t>& peak, const size_t value) {
IndexPipeline.cpp:346: class CounterLease {
IndexPipeline.cpp:371: std::optional<std::string> ColdNamePrefilterNeedle(const ffsearch::Query& query) {
IndexPipeline.cpp:386: bool IndexPipeline::VolumeSettingsEqual(const ffprotocol::VolumeSetting& left,
IndexPipeline.cpp:400: void IndexPipeline::AdvanceQueryVisibleEpochLocked(const ffindexstore::VolumeRowId volumeId,
IndexPipeline.cpp:418: void IndexPipeline::RecordMetadataOnlyUpdateLocked() {
IndexPipeline.cpp:423: void IndexPipeline::AdvanceResidencyEpochsForStructuralChangeLocked() {
IndexPipeline.cpp:442: bool IndexPipeline::Open(const std::string& dbPathUtf8, bool* outIntegrityFailed) {
IndexPipeline.cpp:456: void IndexPipeline::Close() {
IndexPipeline.cpp:467: void IndexPipeline::SetSearchTestHooks(SearchTestHooks hooks) {
IndexPipeline.cpp:472: void IndexPipeline::SetReadSessionFaultInjector(ffindexstore::ReadSessionFaultInjector injector) {
IndexPipeline.cpp:478: void IndexPipeline::SetSecondaryPruningFaultInjector(SecondaryPruningFaultInjector injector) {
IndexPipeline.cpp:483: bool IndexPipeline::ShouldInjectSecondaryPruningFailureLocked(
IndexPipeline.cpp:514: void IndexPipeline::SetBulkScanActive(const bool active) {
IndexPipeline.cpp:520: void IndexPipeline::RunStoreMaintenance(bool userIdle, uint64_t vacuumIntervalHours,
IndexPipeline.cpp:536: void IndexPipeline::MaybeAnalyzeAfterBulkInsertLocked(size_t recordCount) {
IndexPipeline.cpp:559: void IndexPipeline::MaybeReportProjectionMemoryPressureLocked() {
IndexPipeline.cpp:572: void IndexPipeline::RebuildAll(const VolumeRebuiltCallback& onVolumeRebuilt) {
IndexPipeline.cpp:657: std::optional<ffindexstore::VolumeMetadata> IndexPipeline::GetVolumeMetadata(ffindexstore::VolumeRowId id) {
IndexPipeline.cpp:662: std::vector<ffindexstore::VolumeMetadata> IndexPipeline::GetAllVolumes() {
IndexPipeline.cpp:667: void IndexPipeline::SetVolumeRules(ffindexstore::VolumeRowId volumeId, const ffprotocol::VolumeSetting& setting) {
IndexPipeline.cpp:677: bool IndexPipeline::IsEntryIncludedLocked(ffindexstore::VolumeRowId volumeId, ffindexstore::FileId frn) {
IndexPipeline.cpp:707: bool IndexPipeline::ApplyMftBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::MftRecordV1>& records) {
IndexPipeline.cpp:716: bool IndexPipeline::ApplyMftBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::MftRecordV2>& records) {
IndexPipeline.cpp:727: bool IndexPipeline::ApplyMftBatchChanges(ffindexstore::VolumeRowId volumeId,
IndexPipeline.cpp:807: bool IndexPipeline::ApplyUsnBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::UsnDeltaV1>& records) {
IndexPipeline.cpp:814: bool IndexPipeline::ApplyUsnBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::UsnDeltaV2>& records) {
IndexPipeline.cpp:832: bool IndexPipeline::ApplyUsnBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::UsnDeltaV3>& records) {
IndexPipeline.cpp:853: bool IndexPipeline::ApplyUsnBatchChanges(ffindexstore::VolumeRowId volumeId,
IndexPipeline.cpp:945: bool IndexPipeline::SetVolumeAvailable(ffindexstore::VolumeRowId id, bool available, uint64_t nowTimestamp) {
IndexPipeline.cpp:959: bool IndexPipeline::SetScanCursor(ffindexstore::VolumeRowId id, const std::vector<uint8_t>& cursor) {
IndexPipeline.cpp:966: bool IndexPipeline::MarkScanComplete(ffindexstore::VolumeRowId id) {
IndexPipeline.cpp:973: bool IndexPipeline::SetJournalPosition(ffindexstore::VolumeRowId id, uint64_t journalId, uint64_t resumeUsn) {
IndexPipeline.cpp:980: bool IndexPipeline::SetLastReconciliationTime(ffindexstore::VolumeRowId id, uint64_t nowTimestamp) {
IndexPipeline.cpp:987: bool IndexPipeline::ForgetVolume(ffindexstore::VolumeRowId id) {
IndexPipeline.cpp:1002: void IndexPipeline::BeginReconciliationPass(ffindexstore::VolumeRowId id) {
IndexPipeline.cpp:1008: bool IndexPipeline::IsReconciliationPassActive(ffindexstore::VolumeRowId id) {
IndexPipeline.cpp:1013: void IndexPipeline::AbortReconciliationPass(ffindexstore::VolumeRowId id) {
IndexPipeline.cpp:1020: void IndexPipeline::FinishReconciliationPass(ffindexstore::VolumeRowId id) {
IndexPipeline.cpp:1163: namespace {
IndexPipeline.cpp:1165: std::map<std::wstring, ffprotocol::SnapshotDirectory> ToOwningSnapshotMap(
IndexPipeline.cpp:1201: std::map<std::wstring, ffprotocol::SnapshotDirectory> IndexPipeline::ExportDirectorySnapshot(
IndexPipeline.cpp:1207: std::map<std::wstring, ffprotocol::SnapshotDirectory> IndexPipeline::ExportDirectorySnapshotBounded(
IndexPipeline.cpp:1212: std::optional<ffindexstore::Projection::FolderAggregate> IndexPipeline::GetFolderAggregate(ffindexstore::VolumeRowId volumeId, ffindexstore::FileId parentFrn) {
IndexPipeline.cpp:1249: void IndexPipeline::SetProjectionMemoryBudget(const size_t maximumBytes) {
IndexPipeline.cpp:1281: bool IndexPipeline::IsVolumeEvicted(const ffindexstore::VolumeRowId volumeId) const {
IndexPipeline.cpp:1286: void IndexPipeline::UnloadVolumeFromProjection(const ffindexstore::VolumeRowId volumeId) {
IndexPipeline.cpp:1297: ffprotocol::IndexedSearchResponse IndexPipeline::SearchIndexedReadPlane(
IndexPipeline.cpp:2197: ffprotocol::IndexedSearchResponse IndexPipeline::SearchIndexed(
read: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/I...
constexpr uint64_t kWalForceCheckpointThresholdBytes = 4000ULL * 4096;
// Win32 ERROR_NOT_ENOUGH_MEMORY, kept numeric so this engine translation unit
// does not acquire a direct windows.h dependency for one diagnostic field.
constexpr uint32_t kErrorNotEnoughMemory = 8;
void UpdatePeak(std::atomic<size_t>& peak, const size_t value) {
    size_t observed = peak.load(std::memory_order_relaxed);
    while (observed < value && !peak.compare_exchange_weak(
               observed, value, std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
}
class CounterLease {
public:
    CounterLease(std::atomic<size_t>& active, std::atomic<size_t>& peak)
        : active_(&active), peak_(&peak) {
        const size_t current = active_->fetch_add(1, std::memory_order_relaxed) + 1;
        UpdatePeak(*peak_, current);
    }
    ~CounterLease() {
        if (active_ != nullptr) active_->fetch_sub(1, std::memory_order_relaxed);
    }
    CounterLease(const CounterLease&) = delete;
    CounterLease& operator=(const CounterLease&) = delete;
private:
    std::atomic<size_t>* active_;
    std::atomic<size_t>* peak_;
};
// large-index-lifecycle-and-search-scalability review-remediation 18.x:
// cold-stream prefilter eligibility. An ASCII-folded simple literal needle of
// >= 3 units can use the conservative SQL name prefilter
// (StoreReadSession::ForEachEntryWithAsciiNameUntil): LIKE folds exactly the
// ASCII case the superset proof needs and the query's own fold is already
// ASCII-lowercase. Non-ASCII needles return nullopt and keep the full stream
// because SQLite LIKE cannot fold non-ASCII case variants.
std::optional<std::string> ColdNamePrefilterNeedle(const ffsearch::Query& query) {
    if (!query.simpleLiteralName) return std::nullopt;
    const std::u16string& folded = query.simpleLiteralFolded;
    if (folded.size() < 3) return std::nullopt;
    std::string needle;
    needle.reserve(folded.size());
    for (const char16_t ch : folded) {
        if (ch > 0x7f) return std::nullopt;
        needle.push_back(static_cast<char>(ch));
    }
    return needle;
}
} // namespace
bool IndexPipeline::VolumeSettingsEqual(const ffprotocol::VolumeSetting& left,
                                        const ffprotocol::VolumeSetting& right) {
    if (left.key != right.key || left.enabled != right.enabled || left.rules.size() != right.rules.size()) {
        return false;
    }
    for (size_t index = 0; index < left.rules.size(); ++index) {
        if (left.rules[index].path != right.rules[index].path ||
            left.rules[index].include != right.rules[index].include) {
            return false;
        }
    }
    return true;
}
void IndexPipeline::AdvanceQueryVisibleEpochLocked(const ffindexstore::VolumeRowId volumeId,
                                                    const QueryMutationReason reason,
                                                    const bool dependencySetChanged) {
    const uint64_t nextEpoch = queryVisibleEpoch_.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (volumeId != 0) {
        volumeDataEpochs_[volumeId] = nextEpoch;
        if (reason == QueryMutationReason::VolumeConfiguration ||
            reason == QueryMutationReason::VolumeVisibility ||
            reason == QueryMutationReason::Forget) {
            volumeConfigurationEpochs_[volumeId] = nextEpoch;
        }
    }
    if (dependencySetChanged || reason == QueryMutationReason::Forget) {
        dependencySetEpoch_.fetch_add(1, std::memory_order_acq_rel);
    }
    queryVisibleInvalidations_.fetch_add(1, std::memory_order_relaxed);
}
void IndexPipeline::RecordMetadataOnlyUpdateLocked() {
    metadataOnlyUpdates_.fetch_add(1, std::memory_order_relaxed);
    metadataOnlyInvalidationsAvoided_.fetch_add(1, std::memory_order_relaxed);
}
void IndexPipeline::AdvanceResidencyEpochsForStructuralChangeLocked() {
    // large-index-lifecycle-and-search-scalability review-lane D P1-1 / lane C
    // Finding 1: memory-pressure enforcement and cold reload are structural
    // projection events -- EvictSubtree can cold-mark ANY volume (the LRU
    // victim is a global pick) and CompactStorage/RebuildVolumeFromStore
    // repack every row slot, so an in-flight search's captured cursor,
    // candidate slots, and cold-set can all be wrong afterwards. Surface the
    // change through the SAME ProjectionResidency epoch mechanism
    // SetProjectionMemoryBudget / UnloadVolumeFromProjection use, so searches
    // restart with a fresh capture instead of silently serving incomplete
    // results as Completed. Ordinary per-volume mutations never route through
    // here (scoped searches stay insensitive to unrelated writes).
    for (const auto& [volumeId, epoch] : volumeDataEpochs_) {
        (void)epoch;
        AdvanceQueryVisibleEpochLocked(volumeId, QueryMutationReason::ProjectionResidency);
    }
    AdvanceQueryVisibleEpochLocked(0, QueryMutationReason::ProjectionResidency, true);
}
bool IndexPipeline::Open(const std::string& dbPathUtf8, bool* outIntegrityFailed) {
    lifecycleEpoch_.fetch_add(1, std::memory_order_acq_rel);
[1761 more lines in file. Use offset=444 to continue.]
read: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/I...
ffprotocol::IndexedSearchResponse IndexPipeline::SearchIndexedReadPlane(
    const ffprotocol::IndexedSearchRequest& request, SearchTimings* timings,
    const std::function<bool()>& cancelled) {
    using Clock = std::chrono::steady_clock;
    const auto totalStart = timings != nullptr ? Clock::now() : Clock::time_point{};
    if (timings != nullptr) timings->cancelled = false;
    const bool detailedDiagnostics = timings != nullptr && timings->detailedDiagnostics;
    const bool projectionOnly = timings != nullptr && timings->projectionOnly;
    const auto setTotal = [&] {
        if (timings != nullptr) {
            timings->totalMs = std::chrono::duration<double, std::milli>(Clock::now() - totalStart).count();
        }
    };
    ffprotocol::IndexedSearchResponse result;
    result.requestId = request.requestId;
    const uint64_t searchLifecycleEpoch = lifecycleEpoch_.load(std::memory_order_acquire);
    const auto isCancelled = [this, &cancelled, searchLifecycleEpoch] {
        return lifecycleEpoch_.load(std::memory_order_acquire) != searchLifecycleEpoch ||
               (cancelled && cancelled());
    };
    const auto finishCancelled = [&] {
        result.status = ffprotocol::IndexedSearchStatus::Cancelled;
        result.candidates.clear();
        cancelledSearches_.fetch_add(1, std::memory_order_relaxed);
        if (timings != nullptr) timings->cancelled = true;
        setTotal();
    };
    if (request.requestId == 0 || !ffprotocol::IsIndexedSearchScopeValid(request.scope) ||
        !ffprotocol::IsIndexedSearchSortValid(request.sortField) || request.query.empty() ||
        request.query.size() > ffprotocol::kMaxIndexedSearchQueryLengthChars ||
        request.scopePath.size() > ffprotocol::kMaxPathLengthChars || request.maxResults == 0) {
        result.status = ffprotocol::IndexedSearchStatus::InvalidRequest;
        setTotal();
        return result;
    }
    CounterLease activeSearchLease(activeSearches_, peakSearches_);
    (void)activeSearchLease;
    if (isCancelled()) {
        finishCancelled();
        return result;
    }
    const auto queryStart = timings != nullptr ? Clock::now() : Clock::time_point{};
    ffsearch::SearchRequest searchRequest;
    searchRequest.query = ffsearch::ParseQuery(request.query, ffsearch::FilterRegistry::WithDefaults());
    searchRequest.scope = static_cast<ffsearch::SearchScope>(request.scope);
    searchRequest.scopePath = request.scopePath;
    searchRequest.sortField = static_cast<ffsearch::SortField>(request.sortField);
    searchRequest.descending = request.descending;
    searchRequest.maxResults = (std::min)(static_cast<size_t>(request.maxResults),
                                          static_cast<size_t>(ffprotocol::kMaxIndexedSearchResultCount));
    searchRequest.persistentOnly = true;
    searchRequest.chunkSize = kHotCandidateChunkRows;
    if (timings != nullptr) {
        timings->queryParseMs =
            std::chrono::duration<double, std::milli>(Clock::now() - queryStart).count();
    }
    struct SearchContext {
        uint64_t queryVisibleEpoch = 0;
        uint64_t dependencySetEpoch = 0;
        bool allVolumes = false;
        bool ruleFilterActive = false;
        std::unordered_map<ffindexstore::VolumeRowId, std::wstring> prefixes;
        std::unordered_map<ffindexstore::VolumeRowId, ffprotocol::VolumeSetting> rules;
        std::vector<SearchDependency> dependencies;
        std::vector<ffindexstore::VolumeRowId> searchableVolumes;
        std::unordered_set<ffindexstore::VolumeRowId> coldVolumes;
        std::vector<ffindexstore::VolumeRowId> orderedCold;
        std::unique_ptr<ffindexstore::StoreReadSession> reader;
        ffindexstore::ProjectionEntryCursor cursor;
        std::optional<std::vector<uint32_t>> filenameCandidates;
        std::function<void(SearchPhase)> phaseHook;
    };
    ffsearch::SearchResponse search;
    ffsearch::SearchDiagnostics searchDiagnostics;
    size_t generationRestarts = 0;
    size_t projectionRowsExamined = 0;
    size_t projectionEntriesVisited = 0;
    size_t namePoolLookups = 0;
    size_t emptyNamesRejected = 0;
    size_t nameViewsConstructed = 0;
    size_t candidateObjectsMaterialized = 0;
    size_t candidateNameCopies = 0;
    size_t coldRowsExamined = 0;
    size_t coldVolumesSearched = 0;
    double storeStreamMs = 0.0;
    double lazyPathMaterializationMs = 0.0;
    double candidateNameMaterializationMs = 0.0;
    double lockWaitMs = 0.0;
    double maxLockHoldMs = 0.0;
    bool searchedColdVolumes = false;
    bool unavailable = false;
    bool completed = false;
    bool durableFallbackNeeded = false;
    bool durableFallbackUsed = false;
    bool durableFallbackSucceeded = false;
    size_t durableFallbackAttempts = 0;
    size_t durableFallbackRowsStreamed = 0;
    size_t relevantInvalidations = 0;
    // indexed-search-zero-copy-hot-path: bounded view-first capture counters.
    size_t hotLightweightRowsCaptured = 0;
    size_t hotRowsRejectedBeforeMaterialization = 0;
    size_t viewPathFallbackRows = 0;
    double lightweightCaptureMs = 0.0;
    double viewMatchingMs = 0.0;
    double survivorMaterializationMs = 0.0;
    bool viewPathUsed = false;
    // search-availability-under-index-churn D1/D2/D5: capture only the
    // searchable volumes relevant to this request, together with their
    // epochs, rules, residency, and one independent WAL snapshot.
    const auto captureContext = [&](SearchContext& context, const bool prepareProjection) {
        const auto waitStart = Clock::now();
        std::unique_lock<std::mutex> lock(mutex_);
        lockWaitMs += std::chrono::duration<double, std::milli>(Clock::now() - waitStart).count();
        const auto holdStart = Clock::now();
        bool ready = false;
        if (!store_.IsOpen()) {
            unavailable = true;
        } else {
            context.queryVisibleEpoch = queryVisibleEpoch_.load(std::memory_order_acquire);
            context.dependencySetEpoch = dependencySetEpoch_.load(std::memory_order_acquire);
            context.allVolumes = searchRequest.scope == ffsearch::SearchScope::AllIndexedLocations;
            context.phaseHook = searchTestHooks_.onPhase;
            std::unordered_map<ffindexstore::VolumeRowId, std::wstring> configuredPrefixes;
            std::unordered_set<ffindexstore::VolumeRowId> disabledVolumes;
            configuredPrefixes.reserve(volumeRules_.size() + 4);
            disabledVolumes.reserve(volumeRules_.size());
            for (const auto& [volumeId, setting] : volumeRules_) {
                if (!setting.enabled) {
                    disabledVolumes.insert(volumeId);
                    continue;
                }
                if (const std::wstring prefix = NormalizeVolumePrefix(setting.key); !prefix.empty()) {
                    configuredPrefixes.emplace(volumeId, prefix);
                }
            }
            const auto metadataStart = timings != nullptr ? Clock::now() : Clock::time_point{};
            const auto metadata = store_.GetAllVolumesWithoutEntryCounts();
            if (timings != nullptr) {
                timings->sqliteMetadataMs +=
                    std::chrono::duration<double, std::milli>(Clock::now() - metadataStart).count();
            }
            context.prefixes.reserve(metadata.size());
            context.rules.reserve(metadata.size());
            context.searchableVolumes.reserve(metadata.size());
            for (const auto& metadataEntry : metadata) {
                // Availability is operational lifecycle state: indexed rows
                // remain queryable while a volume is offline so search can
                // provide retained results. Explicit VolumeSetting.enabled
                // remains the query-visibility switch.
                if (disabledVolumes.contains(metadataEntry.rowId)) continue;
                std::wstring prefix;
                const auto configuredPrefix = configuredPrefixes.find(metadataEntry.rowId);
                if (configuredPrefix != configuredPrefixes.end()) {
                    prefix = configuredPrefix->second;
                } else {
                    const wchar_t driveLetter = ResolveDriveLetterForVolumeKey(metadataEntry.key);
                    if (driveLetter != L'\0') {
                        prefix.push_back(static_cast<wchar_t>(towupper(driveLetter)));
                        prefix.push_back(L':');
                    }
                }
                if (prefix.empty() || (!context.allVolumes &&
                                       !ffsearch::SameVolume(searchRequest.scopePath, prefix))) {
                    continue;
                }
                context.prefixes.emplace(metadataEntry.rowId, std::move(prefix));
                const auto ruleIt = volumeRules_.find(metadataEntry.rowId);
                if (ruleIt != volumeRules_.end()) {
                    context.rules.emplace(metadataEntry.rowId, ruleIt->second);
                    if (!ruleIt->second.rules.empty()) context.ruleFilterActive = true;
                }
                context.searchableVolumes.push_back(metadataEntry.rowId);
            }
            std::sort(context.searchableVolumes.begin(), context.searchableVolumes.end());
            if (context.searchableVolumes.empty()) {
                unavailable = true;
            } else {
                context.dependencies.reserve(context.searchableVolumes.size());
                for (const auto volumeId : context.searchableVolumes) {
                    const auto dataIt = volumeDataEpochs_.find(volumeId);
                    const auto configurationIt = volumeConfigurationEpochs_.find(volumeId);
                    context.dependencies.push_back({
                        volumeId,
                        dataIt == volumeDataEpochs_.end() ? 0 : dataIt->second,
                        configurationIt == volumeConfigurationEpochs_.end() ? 0 : configurationIt->second});
                    if (prepareProjection && projection_.NeedsReload(volumeId)) {
                        context.coldVolumes.insert(volumeId);
                    }
                }
                context.orderedCold.assign(context.coldVolumes.begin(), context.coldVolumes.end());
                std::sort(context.orderedCold.begin(), context.orderedCold.end());
                searchedColdVolumes = searchedColdVolumes || !context.orderedCold.empty();
                context.reader = store_.OpenReadSession({readSessionFaultInjector_});
                if (!context.reader) {
                    readerFailures_.fetch_add(1, std::memory_order_relaxed);
                    unavailable = true;
                } else {
                    if (prepareProjection) {
                        context.cursor = projection_.BeginEntryCursor();
                        const bool filenameEligible = searchRequest.query.simpleLiteralName &&
                            searchRequest.query.simpleLiteralFolded.size() >= 3 && context.coldVolumes.empty();
                        if (filenameEligible) {
                            context.filenameCandidates = projection_.FilenameCandidateIndices(
                                searchRequest.query.simpleLiteralFolded, kMaximumFilenameIndexCandidates);
                        }
                        if (timings != nullptr) {
                            timings->filenameIndexUsed = context.filenameCandidates.has_value();
                            timings->filenameIndexCandidateCount = context.filenameCandidates.has_value()
                                ? context.filenameCandidates->size() : 0;
                        }
                    }
                    ready = true;
                }
            }
        }
        const double holdMs = std::chrono::duration<double, std::milli>(Clock::now() - holdStart).count();
        maxLockHoldMs = (std::max)(maxLockHoldMs, holdMs);
        lock.unlock();
        return ready;
    };
    const auto isContextCurrentLocked = [&](const SearchContext& context, const bool includeDataEpochs) {
        if (!store_.IsOpen()) return false;
        if (context.allVolumes && dependencySetEpoch_.load(std::memory_order_acquire) !=
                                     context.dependencySetEpoch) return false;
        for (const auto& dependency : context.dependencies) {
            const auto dataIt = volumeDataEpochs_.find(dependency.volumeId);
            const auto configurationIt = volumeConfigurationEpochs_.find(dependency.volumeId);
            const uint64_t dataEpoch = dataIt == volumeDataEpochs_.end() ? 0 : dataIt->second;
            const uint64_t configurationEpoch =
                configurationIt == volumeConfigurationEpochs_.end() ? 0 : configurationIt->second;
            if (configurationEpoch != dependency.configurationEpoch ||
                (includeDataEpochs && dataEpoch != dependency.dataEpoch)) {
                return false;
            }
        }
        return true;
    };
    for (size_t restart = 0; restart <= kMaxGenerationRestarts; ++restart) {
        if (isCancelled()) {
            finishCancelled();
            return result;
        }
        SearchContext context;
        bool contextReady = false;
        contextReady = captureContext(context, true);
        if (unavailable || !contextReady) break;
        if (!context.ruleFilterActive) {
            searchRequest.candidateFilter = {};
            searchRequest.candidateFilterRequiresFolder = false;
        } else {
            const auto capturedRules = context.rules;
            searchRequest.candidateFilter = [capturedRules](const ffsearch::Candidate& candidate) {
                return CandidateIncludedByRules(capturedRules, candidate);
            };
            searchRequest.candidateFilterRequiresFolder = true;
        }
        CounterLease durableReaderLease(activeDurableReaders_, peakDurableReaders_);
        (void)durableReaderLease;
        if (context.phaseHook) context.phaseHook(SearchPhase::WorkStarted);
        bool stale = false;
        bool streamFailed = false;
        bool matchingHookCalled = false;
        bool coldHookCalled = false;
        const auto markStaleIfChanged = [&] {
            const auto waitStart = Clock::now();
            std::unique_lock<std::mutex> lock(mutex_);
            lockWaitMs += std::chrono::duration<double, std::milli>(Clock::now() - waitStart).count();
            const auto holdStart = Clock::now();
            const bool changed = !isContextCurrentLocked(context, true);
            const double holdMs = std::chrono::duration<double, std::milli>(Clock::now() - holdStart).count();
            maxLockHoldMs = (std::max)(maxLockHoldMs, holdMs);
            lock.unlock();
            if (changed) {
                stale = true;
                ++relevantInvalidations;
            }
        };
        const auto materialize = [&](ffsearch::Candidate& candidate) {
            if (isCancelled() || stale) return false;
            const auto volumeId = static_cast<ffindexstore::VolumeRowId>(candidate.volumeId);
            if (context.coldVolumes.contains(volumeId)) {
                const bool materialized = context.reader != nullptr && MaterializeCandidateFolderDurable(
                    *context.reader, context.prefixes, candidate, isCancelled);
                if (!materialized && context.reader != nullptr && context.reader->Failed()) {
                    streamFailed = true;
                } else if (!materialized && !isCancelled() && !stale) {
                    // A durable row whose parent chain cannot be reconstructed
                    // is not a complete response candidate. Do not silently
                    // omit it from a supposedly completed cold search.
                    streamFailed = true;
                }
                return materialized;
            }
            bool materialized = false;
            {
                const auto waitStart = Clock::now();
                std::unique_lock<std::mutex> lock(mutex_);
                lockWaitMs += std::chrono::duration<double, std::milli>(Clock::now() - waitStart).count();
                const auto holdStart = Clock::now();
                if (isContextCurrentLocked(context, true)) {
                    materialized = MaterializeCandidateFolder(projection_, context.prefixes, candidate, isCancelled);
                } else {
                    stale = true;
                }
                const double holdMs = std::chrono::duration<double, std::milli>(Clock::now() - holdStart).count();
                maxLockHoldMs = (std::max)(maxLockHoldMs, holdMs);
                lock.unlock();
            }
            if (materialized || stale || isCancelled()) return materialized;
            // A hot candidate can have an unavailable parent chain during a
            // partial rebuild. Resolve that bounded fallback from the same
            // query snapshot, outside the pipeline mutex.
            const bool durableMaterialized = context.reader != nullptr && MaterializeCandidateFolderDurable(
                *context.reader, context.prefixes, candidate, isCancelled);
            if (!durableMaterialized && context.reader != nullptr && context.reader->Failed()) {
                streamFailed = true;
            } else if (!durableMaterialized && !isCancelled() && !stale) {
                streamFailed = true;
[578 more lines in file. Use offset=1627 to continue.]
read: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/I...
            }
            return durableMaterialized;
        };
        const auto searchStart = Clock::now();
        ffsearch::SearchAccumulator accumulator(searchRequest, isCancelled,
                                                 (detailedDiagnostics || projectionOnly)
                                                     ? &searchDiagnostics : nullptr);
        bool hotExhausted = false;
        size_t filenameIndex = 0;
        // indexed-search-zero-copy-hot-path: for view-compatible queries, capture
        // a lightweight row (scalars + stable NameId + name bytes in a chunk-
        // owned buffer) under the mutex and evaluate the cheap name/metadata
        // view predicate outside it. Rejected rows never get an owned Candidate
        // or a per-row name copy. Non-view-compatible or projectionOnly runs use
        // the canonical owned-candidate path untouched (survivors still go
        // through the identical accumulator.Add, so result semantics are unchanged).
        const bool useViewHotPath = !projectionOnly && searchRequest.query.viewCompatible;
        const bool folderNeededForView =
            (searchRequest.scope != ffsearch::SearchScope::AllIndexedLocations) ||
            searchRequest.query.requiresFolder || searchRequest.candidateFilterRequiresFolder;
        while (!hotExhausted && !stale && !isCancelled()) {
            if (useViewHotPath) {
                std::vector<LightweightHotRow> rows;
                rows.reserve(kHotCandidateChunkRows);
                std::u16string nameBuffer;
                {
                    const auto waitStart = Clock::now();
                    std::unique_lock<std::mutex> lock(mutex_);
                    lockWaitMs += std::chrono::duration<double, std::milli>(Clock::now() - waitStart).count();
                    const auto holdStart = Clock::now();
                    if (!isContextCurrentLocked(context, true)) {
                        stale = true;
                    } else {
                        const auto captureStart = Clock::now();
                        while (rows.size() < kHotCandidateChunkRows) {
                            const ffindexstore::ProjectionEntry* entry = nullptr;
                            if (context.filenameCandidates.has_value()) {
                                if (filenameIndex >= context.filenameCandidates->size()) {
                                    hotExhausted = true;
                                    break;
                                }
                                entry = &projection_.EntryAt((*context.filenameCandidates)[filenameIndex++]);
                            } else if (!context.cursor.Next(entry)) {
                                // large-index-lifecycle-and-search-scalability
                                // review-lane C Finding 2: a disturbed layout
                                // ends iteration early; treat it as a stale
                                // generation (restart + re-capture), never as
                                // exhausted (which would silently truncate).
                                if (context.cursor.Stale()) {
                                    stale = true;
                                    break;
                                }
                                hotExhausted = true;
                                break;
                            }
                            if (entry == nullptr || context.coldVolumes.contains(entry->volumeRowId)) continue;
                            const auto prefixIt = context.prefixes.find(entry->volumeRowId);
                            if (prefixIt == context.prefixes.end()) continue;
                            if (detailedDiagnostics) ++projectionEntriesVisited;
                            ++projectionRowsExamined;
                            LightweightHotRow row;
                            row.volumeRowId = entry->volumeRowId;
                            row.frn = entry->frn;
                            row.parentFrn = entry->parentFrn;
                            row.sizeBytes = entry->sizeBytes;
                            row.modifiedTime = entry->lastModifiedTime;
                            row.createdTime = entry->creationTime;
                            row.isDirectory = (entry->attributes & kFileAttributeDirectory) != 0;
                            row.allocatedSizeBytes = entry->allocatedSizeBytes;
                            row.allocatedSizeKnown = entry->allocatedSizeKnown;
                            row.nameId = entry->nameId;
                            row.nameIsAscii = entry->nameIsAscii;
                            if (entry->parentFrn != entry->frn) {
                                ++namePoolLookups;
                                const std::u16string_view name = projection_.Names().Get(entry->nameId);
                                if (name.empty()) {
                                    // Checked before the append: an empty
                                    // interned name may carry a null data()
                                    // pointer, and appending it is formally
                                    // undefined even for a zero count.
                                    if (detailedDiagnostics) ++emptyNamesRejected;
                                    continue;
                                }
                                row.nameOffset = nameBuffer.size();
                                row.nameLength = name.size();
                                nameBuffer.append(name.data(), name.size());
                            } else {
                                row.nameOffset = nameBuffer.size();
                                row.nameLength = 0;
                            }
                            ++nameViewsConstructed;
                            rows.push_back(std::move(row));
                        }
                        lightweightCaptureMs += std::chrono::duration<double, std::milli>(
                            Clock::now() - captureStart).count();
                        hotLightweightRowsCaptured += rows.size();
                        viewPathUsed = true;
                    }
                    const double holdMs = std::chrono::duration<double, std::milli>(Clock::now() - holdStart).count();
                    maxLockHoldMs = (std::max)(maxLockHoldMs, holdMs);
                    lock.unlock();
                }
                if (!matchingHookCalled) {
                    if (context.phaseHook) context.phaseHook(SearchPhase::Matching);
                    matchingHookCalled = true;
                }
                if (!projectionOnly) {
                    double chunkPureViewMs = 0.0;
                    for (const auto& row : rows) {
                        if (stale || isCancelled() || accumulator.Cancelled()) break;
                        const std::u16string_view name =
                            (row.parentFrn == row.frn)
                                ? std::u16string_view()
                                : std::u16string_view(nameBuffer).substr(row.nameOffset, row.nameLength);
                        ffsearch::CandidateView view;
                        view.name = name;
                        view.sizeBytes = row.sizeBytes;
                        view.modifiedTime = row.modifiedTime;
                        view.isDirectory = row.isDirectory;
                        view.createdTime = row.createdTime;
                        view.volumeId = static_cast<int64_t>(row.volumeRowId);
                        view.id = row.frn.low;
                        view.parentId = row.parentFrn.low;
                        view.persistentIndexed = true;
                        view.nameAsciiLower = false;
                        view.allocatedSizeBytes = row.allocatedSizeBytes;
                        view.allocatedSizeKnown = row.allocatedSizeKnown;
                        view.nameIsAscii = row.nameIsAscii;
                        const auto viewPredStart = Clock::now();
                        const bool viewAccepted = folderNeededForView
                            ? searchRequest.query.MatchesViewWithoutFolder(view, nullptr)
                            : searchRequest.query.MatchesView(view, nullptr);
                        chunkPureViewMs += std::chrono::duration<double, std::milli>(
                            Clock::now() - viewPredStart).count();
                        if (!viewAccepted) {
                            ++hotRowsRejectedBeforeMaterialization;
                            continue;
                        }
                        // performance-closure-and-multisurface-acceleration 7.3:
                        // when the view evaluated the FULL predicate set (no
                        // folder-dependent evaluation), a strictly worse
                        // primary key can be admitted out before any
                        // materialization, and Add need not re-evaluate the
                        // query predicates on the owned candidate.
                        //
                        // completion-review R1: a volume-root row's view name is
                        // EMPTY while its materialized candidate uses the display
                        // name `prefix + "\\"` (e.g. "Z:\\"). The admission
                        // compares view names, so it must never judge a root row --
                        // a descending filename search otherwise discards the root
                        // that correctly wins. Root rows also take the full Add
                        // path so no view-based skip applies to them at all.
                        const bool fullViewEvaluation = !folderNeededForView;
                        const bool isVolumeRoot = (row.parentFrn == row.frn);
                        if (fullViewEvaluation && !isVolumeRoot && !accumulator.AdmitView(view)) {
                            ++hotRowsRejectedBeforeMaterialization;
                            continue;
                        }
                        const auto survivorStart = Clock::now();
                        const auto& prefix = context.prefixes.at(row.volumeRowId);
                        ffsearch::Candidate candidate =
                            MakeProjectionCandidateFromLightweight(row, name, prefix);
                        ++candidateNameCopies;
                        const bool continueSearch = (fullViewEvaluation && !isVolumeRoot)
                            ? accumulator.AddViewMatched(std::move(candidate), materialize)
                            : accumulator.Add(std::move(candidate), materialize);
                        if (!continueSearch) break;
                        ++candidateObjectsMaterialized;
                        survivorMaterializationMs += std::chrono::duration<double, std::milli>(
                            Clock::now() - survivorStart).count();
                        if (stale || isCancelled() || accumulator.Cancelled()) break;
                    }
                    viewMatchingMs += chunkPureViewMs;
                }
                if (stale || isCancelled() || accumulator.Cancelled() || streamFailed) break;
            } else {
                std::vector<ffsearch::Candidate> chunk;
                chunk.reserve(kHotCandidateChunkRows);
                {
                    const auto waitStart = Clock::now();
                    std::unique_lock<std::mutex> lock(mutex_);
                    lockWaitMs += std::chrono::duration<double, std::milli>(Clock::now() - waitStart).count();
                    const auto holdStart = Clock::now();
                    if (!isContextCurrentLocked(context, true)) {
                        stale = true;
                    } else {
                        while (chunk.size() < kHotCandidateChunkRows) {
                            const ffindexstore::ProjectionEntry* entry = nullptr;
                            if (context.filenameCandidates.has_value()) {
                                if (filenameIndex >= context.filenameCandidates->size()) {
                                    hotExhausted = true;
                                    break;
                                }
                                entry = &projection_.EntryAt((*context.filenameCandidates)[filenameIndex++]);
                            } else if (!context.cursor.Next(entry)) {
                                // large-index-lifecycle-and-search-scalability
                                // review-lane C Finding 2: a disturbed layout
                                // ends iteration early; treat it as a stale
                                // generation (restart + re-capture), never as
                                // exhausted (which would silently truncate).
                                if (context.cursor.Stale()) {
                                    stale = true;
                                    break;
                                }
                                hotExhausted = true;
                                break;
                            }
                            if (entry == nullptr || context.coldVolumes.contains(entry->volumeRowId)) continue;
                            const auto prefixIt = context.prefixes.find(entry->volumeRowId);
                            if (prefixIt == context.prefixes.end()) continue;
                            if (detailedDiagnostics) ++projectionEntriesVisited;
                            ++projectionRowsExamined;
                            if (entry->parentFrn != entry->frn) ++namePoolLookups;
                            ffsearch::Candidate candidate = MakeProjectionCandidate(
                                projection_, *entry, prefixIt->second);
                            if (candidate.name.empty()) {
                                if (detailedDiagnostics) ++emptyNamesRejected;
                                continue;
                            }
                            ++nameViewsConstructed;
                            ++candidateNameCopies;
                            chunk.push_back(std::move(candidate));
                        }
                        if (!projectionOnly && !searchRequest.query.viewCompatible) {
                            viewPathFallbackRows += chunk.size();
                        }
                    }
                    const double holdMs = std::chrono::duration<double, std::milli>(Clock::now() - holdStart).count();
                    maxLockHoldMs = (std::max)(maxLockHoldMs, holdMs);
                    lock.unlock();
                }
                if (!matchingHookCalled) {
                    if (context.phaseHook) context.phaseHook(SearchPhase::Matching);
                    matchingHookCalled = true;
                }
                if (projectionOnly) {
                    search.candidatesVisited += chunk.size();
                } else {
                    for (auto& candidate : chunk) {
                        if (!accumulator.Add(std::move(candidate), materialize)) break;
                        ++candidateObjectsMaterialized;
                        if (stale || isCancelled() || accumulator.Cancelled()) break;
                    }
                }
                if (stale || isCancelled() || accumulator.Cancelled() || streamFailed) break;
            }
        }
        if (!stale && !isCancelled() && !accumulator.Cancelled()) {
            for (const auto coldVolumeId : context.orderedCold) {
                if (!coldHookCalled) {
                    if (context.phaseHook) context.phaseHook(SearchPhase::ColdStreaming);
                    coldHookCalled = true;
                }
                if (isCancelled() || stale) break;
                const auto prefixIt = context.prefixes.find(coldVolumeId);
                if (prefixIt == context.prefixes.end() || context.reader == nullptr) continue;
                ++coldVolumesSearched;
                const auto streamStart = Clock::now();
                const auto coldNamePrefilter = ColdNamePrefilterNeedle(searchRequest.query);
                const auto coldVisitor = [&](const ffindexstore::EntryRecord& record) {
                        if (isCancelled() || stale || accumulator.Cancelled()) return false;
                        ++coldRowsExamined;
                        ffsearch::Candidate candidate = MakeDurableCandidate(record, coldVolumeId,
                                                                               prefixIt->second);
                        if (candidate.name.empty()) return true;
                        ++candidateNameCopies;
                        if (projectionOnly) {
                            ++search.candidatesVisited;
                            return true;
                        }
                        const bool continueSearch = accumulator.Add(std::move(candidate), materialize);
                        if (!continueSearch || accumulator.Cancelled() || streamFailed) return false;
                        ++candidateObjectsMaterialized;
                        return !stale && !isCancelled();
                    };
                const bool streamed = coldNamePrefilter.has_value()
                    ? context.reader->ForEachEntryWithAsciiNameUntil(coldVolumeId, *coldNamePrefilter,
                                                                     coldVisitor)
                    : context.reader->ForEachEntryUntil(coldVolumeId, coldVisitor);
                storeStreamMs += std::chrono::duration<double, std::milli>(Clock::now() - streamStart).count();
                if (!streamed && !isCancelled() && !stale && !accumulator.Cancelled()) {
                    streamFailed = true;
                    break;
                }
            }
        }
        if (isCancelled() || accumulator.Cancelled()) {
            finishCancelled();
            return result;
        }
        if (context.phaseHook) context.phaseHook(SearchPhase::GenerationValidation);
        markStaleIfChanged();
        if (stale) {
            ++generationRestarts;
            generationRestarts_.fetch_add(1, std::memory_order_relaxed);
            if (restart == kMaxGenerationRestarts) {
                durableFallbackNeeded = true;
                break;
            }
            continue;
        }
        if (streamFailed) {
            readerFailures_.fetch_add(1, std::memory_order_relaxed);
            unavailable = true;
            break;
        }
        if (projectionOnly) {
            search.cancelled = false;
        } else {
            search = accumulator.Finish();
        }
        completed = true;
        if (timings != nullptr) {
            timings->searchGeneration = context.queryVisibleEpoch;
            timings->queryVisibleEpoch = context.queryVisibleEpoch;
            timings->dependencySetEpoch = context.dependencySetEpoch;
            timings->relevantInvalidations = relevantInvalidations;
            timings->generationRestarts = generationRestarts;
            timings->projectionTraversalMs =
                std::chrono::duration<double, std::milli>(Clock::now() - searchStart).count();
        }
        break;
    }
    if (durableFallbackNeeded && !unavailable && !completed) {
        durableFallbackUsed = true;
        durableFallbacks_.fetch_add(1, std::memory_order_relaxed);
[248 more lines in file. Use offset=1957 to continue.]
read: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/I...
        const auto fallbackStart = Clock::now();
        const auto recordFallbackTimings = [&](const uint64_t capturedQueryEpoch = 0,
                                               const uint64_t capturedDependencyEpoch = 0) {
            if (timings == nullptr) return;
            timings->searchGeneration = capturedQueryEpoch != 0
                ? capturedQueryEpoch : queryVisibleEpoch_.load(std::memory_order_acquire);
            timings->queryVisibleEpoch = timings->searchGeneration;
            timings->dependencySetEpoch = capturedDependencyEpoch != 0
                ? capturedDependencyEpoch : dependencySetEpoch_.load(std::memory_order_acquire);
            timings->relevantInvalidations = relevantInvalidations;
            timings->generationRestarts = generationRestarts;
            timings->durableFallbackUsed = durableFallbackUsed;
            timings->durableFallbackAttempts = durableFallbackAttempts;
            timings->durableFallbackRowsStreamed = durableFallbackRowsStreamed;
            timings->durableFallbackSucceeded = durableFallbackSucceeded;
        };
        for (size_t attempt = 0; attempt <= kMaxDurableFallbackConfigurationRetries; ++attempt) {
            if (isCancelled()) {
                recordFallbackTimings();
                finishCancelled();
                return result;
            }
            SearchContext context;
            if (!captureContext(context, false)) break;
            if (!context.ruleFilterActive) {
                searchRequest.candidateFilter = {};
                searchRequest.candidateFilterRequiresFolder = false;
            } else {
                const auto capturedRules = context.rules;
                searchRequest.candidateFilter = [capturedRules](const ffsearch::Candidate& candidate) {
                    return CandidateIncludedByRules(capturedRules, candidate);
                };
                searchRequest.candidateFilterRequiresFolder = true;
            }
            ++durableFallbackAttempts;
            if (context.phaseHook) context.phaseHook(SearchPhase::DurableFallback);
            CounterLease durableReaderLease(activeDurableReaders_, peakDurableReaders_);
            (void)durableReaderLease;
            bool streamFailed = false;
            size_t attemptRowsStreamed = 0;
            const auto materializeDurable = [&](ffsearch::Candidate& candidate) {
                if (isCancelled()) return false;
                const bool materialized = MaterializeCandidateFolderDurable(
                    *context.reader, context.prefixes, candidate, isCancelled);
                if (!materialized && !isCancelled()) streamFailed = true;
                return materialized;
            };
            const auto searchStart = Clock::now();
            ffsearch::SearchAccumulator accumulator(searchRequest, isCancelled,
                                                     (detailedDiagnostics || projectionOnly)
                                                         ? &searchDiagnostics : nullptr);
            for (const auto volumeId : context.searchableVolumes) {
                if (isCancelled() || accumulator.Cancelled()) break;
                const auto prefixIt = context.prefixes.find(volumeId);
                if (prefixIt == context.prefixes.end() || context.reader == nullptr) {
                    streamFailed = true;
                    break;
                }
                ++coldVolumesSearched;
                const auto streamStart = Clock::now();
                const auto durableNamePrefilter = ColdNamePrefilterNeedle(searchRequest.query);
                const auto durableVisitor = [&](const ffindexstore::EntryRecord& record) {
                        if (isCancelled() || accumulator.Cancelled()) return false;
                        ++attemptRowsStreamed;
                        ++coldRowsExamined;
                        ffsearch::Candidate candidate = MakeDurableCandidate(record, volumeId,
                                                                               prefixIt->second);
                        if (candidate.name.empty()) return true;
                        ++candidateNameCopies;
                        if (projectionOnly) {
                            ++search.candidatesVisited;
                            return true;
                        }
                        if (!accumulator.Add(std::move(candidate), materializeDurable)) return false;
                        ++candidateObjectsMaterialized;
                        return !isCancelled() && !accumulator.Cancelled() && !streamFailed;
                    };
                const bool streamed = durableNamePrefilter.has_value()
                    ? context.reader->ForEachEntryWithAsciiNameUntil(volumeId, *durableNamePrefilter,
                                                                     durableVisitor)
                    : context.reader->ForEachEntryUntil(volumeId, durableVisitor);
                storeStreamMs += std::chrono::duration<double, std::milli>(Clock::now() - streamStart).count();
                if (!streamed && !isCancelled() && !accumulator.Cancelled() && !streamFailed) {
                    streamFailed = true;
                }
                if (streamFailed) break;
            }
            durableFallbackRowsStreamed += attemptRowsStreamed;
            durableFallbackRowsStreamed_.fetch_add(attemptRowsStreamed, std::memory_order_relaxed);
            if (timings != nullptr) {
                timings->durableFallbackRowsStreamed += attemptRowsStreamed;
            }
            if (isCancelled() || accumulator.Cancelled()) {
                recordFallbackTimings(context.queryVisibleEpoch, context.dependencySetEpoch);
                finishCancelled();
                return result;
            }
            if (streamFailed || context.reader == nullptr || context.reader->Failed()) {
                readerFailures_.fetch_add(1, std::memory_order_relaxed);
                unavailable = true;
                break;
            }
            bool configurationCurrent = false;
            {
                const auto waitStart = Clock::now();
                std::unique_lock<std::mutex> lock(mutex_);
                lockWaitMs += std::chrono::duration<double, std::milli>(Clock::now() - waitStart).count();
                const auto holdStart = Clock::now();
                configurationCurrent = isContextCurrentLocked(context, false);
                const double holdMs = std::chrono::duration<double, std::milli>(Clock::now() - holdStart).count();
                maxLockHoldMs = (std::max)(maxLockHoldMs, holdMs);
                lock.unlock();
            }
            if (!configurationCurrent) {
                ++relevantInvalidations;
                continue;
            }
            if (projectionOnly) {
                search.cancelled = false;
            } else {
                search = accumulator.Finish();
            }
            completed = true;
            durableFallbackSucceeded = true;
            successfulDurableFallbacks_.fetch_add(1, std::memory_order_relaxed);
            recordFallbackTimings(context.queryVisibleEpoch, context.dependencySetEpoch);
            if (timings != nullptr) {
                timings->projectionTraversalMs =
                    std::chrono::duration<double, std::milli>(Clock::now() - searchStart).count();
            }
            break;
        }
        if (!durableFallbackSucceeded && !isCancelled()) {
            failedDurableFallbacks_.fetch_add(1, std::memory_order_relaxed);
            unavailable = true;
        }
        if (timings != nullptr) {
            recordFallbackTimings();
            timings->projectionTraversalMs =
                std::chrono::duration<double, std::milli>(Clock::now() - fallbackStart).count();
        }
    }
    if (unavailable || !completed) {
        unavailableSearches_.fetch_add(1, std::memory_order_relaxed);
        result.status = ffprotocol::IndexedSearchStatus::Unavailable;
        setTotal();
        return result;
    }
    if (isCancelled() || search.cancelled) {
        finishCancelled();
        return result;
    }
    if (timings != nullptr) {
        timings->relevantInvalidations = relevantInvalidations;
        timings->durableFallbackUsed = durableFallbackUsed;
        timings->durableFallbackAttempts = durableFallbackAttempts;
        timings->durableFallbackRowsStreamed = durableFallbackRowsStreamed;
        timings->durableFallbackSucceeded = durableFallbackSucceeded;
        timings->stateLockWaitMs = lockWaitMs;
        timings->maxStateLockHoldMs = maxLockHoldMs;
        timings->candidateMaterializationMs = candidateNameMaterializationMs + lazyPathMaterializationMs;
        timings->lightweightCandidateMaterializationMs = candidateNameMaterializationMs;
        timings->lazyPathMaterializationMs = lazyPathMaterializationMs;
        timings->candidateCount = projectionRowsExamined + coldRowsExamined;
        timings->projectionEntriesVisited = projectionEntriesVisited;
        timings->projectionRowsExamined = projectionRowsExamined;
        timings->namePoolLookups = namePoolLookups;
        timings->nameViewsConstructed = nameViewsConstructed;
        // large-index-lifecycle-and-search-scalability 12.x: the detailed-
        // diagnostics empty-name counter was tracked but never merged into the
        // timings like its sibling counters.
        timings->emptyNamesRejected = emptyNamesRejected;
        timings->hotRowsExamined = projectionRowsExamined;
        timings->coldRowsExamined = coldRowsExamined;
        timings->coldVolumesSearched = coldVolumesSearched;
        timings->coldSearchUsed = searchedColdVolumes;
        timings->storeStreamMs = storeStreamMs;
        timings->candidateObjectsMaterialized = candidateObjectsMaterialized;
        timings->candidateNameCopies = candidateNameCopies;
        // indexed-search-zero-copy-hot-path: view-first capture attribution.
        timings->hotLightweightRowsCaptured = hotLightweightRowsCaptured;
        timings->hotRowsRejectedBeforeMaterialization = hotRowsRejectedBeforeMaterialization;
        timings->viewPathFallbackRows = viewPathFallbackRows;
        timings->lightweightCaptureMs = lightweightCaptureMs;
        timings->viewMatchingMs = viewMatchingMs;
        timings->survivorMaterializationMs = survivorMaterializationMs;
        timings->viewPathUsed = viewPathUsed;
        timings->candidatesVisited = search.candidatesVisited;
        timings->queryPredicateEvaluations = searchDiagnostics.match.predicateEvaluations;
        timings->queryTermEvaluations = searchDiagnostics.match.termEvaluations;
        timings->caseInsensitiveComparisons = searchDiagnostics.match.caseInsensitiveComparisons;
        timings->caseFoldOperations = searchDiagnostics.match.caseFoldOperations;
        timings->substringSearches = searchDiagnostics.match.substringSearches;
        timings->globMatches = searchDiagnostics.match.globMatches;
        timings->prefixChecks = searchDiagnostics.match.prefixChecks;
        timings->extensionPredicates = searchDiagnostics.match.extensionPredicates;
        timings->metadataPredicates = searchDiagnostics.match.metadataPredicates;
        timings->earlyRejects = searchDiagnostics.match.earlyRejects;
        timings->predicateMatches = searchDiagnostics.match.predicateMatches;
        timings->pathNeededDecisions = searchDiagnostics.pathNeededDecisions;
        timings->pathLookups = searchDiagnostics.pathLookups;
        timings->matchedRows = searchDiagnostics.matchedRows;
        timings->predicateDispatchMs = searchDiagnostics.match.predicateDispatchMs;
        timings->substringMatchingMs = searchDiagnostics.match.substringMatchingMs;
        timings->globMatchingMs = searchDiagnostics.match.globMatchingMs;
        timings->resultCount = search.results.size();
        timings->searchResultObjectsMaterialized = search.results.size();
        timings->truncated = search.truncated;
    }
    result.status = ffprotocol::IndexedSearchStatus::Completed;
    result.truncated = search.truncated;
    result.candidates.reserve(search.results.size());
    const auto responseStart = timings != nullptr ? Clock::now() : Clock::time_point{};
    for (const auto& matched : search.results) {
        if (isCancelled()) {
            finishCancelled();
            return result;
        }
        const auto& candidate = matched.candidate;
        result.candidates.push_back({candidate.name, candidate.folder, candidate.sizeBytes,
                                     candidate.modifiedTime, candidate.isDirectory,
                                     candidate.createdTime, candidate.volumeId, candidate.id,
                                     candidate.parentId, candidate.persistentIndexed,
                                     candidate.allocatedSizeBytes, candidate.allocatedSizeKnown});
    }
    if (isCancelled()) {
        finishCancelled();
        return result;
    }
    completedSearches_.fetch_add(1, std::memory_order_relaxed);
    if (timings != nullptr) {
        timings->responseMaterializationMs =
            std::chrono::duration<double, std::milli>(Clock::now() - responseStart).count();
        setTotal();
    }
    return result;
}
ffprotocol::IndexedSearchResponse IndexPipeline::SearchIndexed(
    const ffprotocol::IndexedSearchRequest& request, SearchTimings* timings,
    const SearchCancellation& cancelled) {
    return SearchIndexedReadPlane(request, timings, cancelled);
}
} // namespace ffengine
grep: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/ben...
bench_secondary_filename_index.cpp-163- }
bench_secondary_filename_index.cpp-164- 
bench_secondary_filename_index.cpp:165: struct QuerySpec {
bench_secondary_filename_index.cpp-166-     const char* name;
bench_secondary_filename_index.cpp-167-     const wchar_t* text;
bench_secondary_filename_index.cpp-251- };
bench_secondary_filename_index.cpp-252- 
bench_secondary_filename_index.cpp:253: class LinearIndex {
bench_secondary_filename_index.cpp-254- public:
bench_secondary_filename_index.cpp-255-     void Build(const std::vector<Entry>&) {}
bench_secondary_filename_index.cpp-263- };
bench_secondary_filename_index.cpp-264- 
bench_secondary_filename_index.cpp:265: class TrigramIndex {
bench_secondary_filename_index.cpp-266- public:
bench_secondary_filename_index.cpp-267-     void Build(const std::vector<Entry>& entries) {
bench_secondary_filename_index.cpp-377- };
bench_secondary_filename_index.cpp-378- 
bench_secondary_filename_index.cpp:379: class PrefixIndex {
bench_secondary_filename_index.cpp-380- public:
bench_secondary_filename_index.cpp-381-     struct Posting {
bench_secondary_filename_index.cpp-464- };
bench_secondary_filename_index.cpp-465- 
bench_secondary_filename_index.cpp:466: struct QueryMeasurement {
bench_secondary_filename_index.cpp-467-     const char* name = "";
bench_secondary_filename_index.cpp-468-     std::string query;
bench_secondary_filename_index.cpp-722- 
bench_secondary_filename_index.cpp-723- void WriteQuery(std::ostream& output, const QueryMeasurement& measurement) {
bench_secondary_filename_index.cpp:724:     output << "{\"query_class\":";
bench_secondary_filename_index.cpp-725-     WriteJsonString(output, measurement.name);
bench_secondary_filename_index.cpp-726-     output << ",\"query\":";
grep: C:/Users/Michael Roy/Documents/FastFiles
grep: C:/Users/Michael Roy/Documents/FastFiles
docs/benchmarks/secondary-filename-index.json:1: {"schema":"fastfiles-secondary-filename-index-v1","sqlite_fts_capability":{"compile_option_enable_fts5":false,"fts5_create":false,"tokenized_infix_recall":false,"trigram_create":false,"trigram_infix_recall":false,"create_error":"no such module: fts5"},"machine":{"logical_cores":12,"ram_bytes":25028239360,"compiler":"MSVC 194435228","build_configuration":"Release"},"scales":[{"dataset_size":100000,"architectures":[{"name":"linear_canonical_scan","build_ms":0.0001,"memory_before_build":{"private_b... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:15:     {"dataset_size":100000,"query_class":"selective","query":"unique_component_7391","temperature":"cold-ish-first-after-rebuild","iteration":0,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.1147,"projection_traversal_ms":0.3356,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms"... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:16:     {"dataset_size":100000,"query_class":"selective","query":"unique_component_7391","temperature":"warm","iteration":0,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.0381,"projection_traversal_ms":0.2774,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0042,"lightweight_can... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:17:     {"dataset_size":100000,"query_class":"selective","query":"unique_component_7391","temperature":"warm","iteration":1,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.0257,"projection_traversal_ms":0.2770,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0031,"lightweight_can... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:18:     {"dataset_size":100000,"query_class":"selective","query":"unique_component_7391","temperature":"warm","iteration":2,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.0217,"projection_traversal_ms":0.2871,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0036,"lightweight_can... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:19:     {"dataset_size":100000,"query_class":"selective","query":"unique_component_7391","temperature":"warm","iteration":3,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.0207,"projection_traversal_ms":0.2814,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0026,"lightweight_can... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:20:     {"dataset_size":100000,"query_class":"selective","query":"unique_component_7391","temperature":"warm","iteration":4,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.0201,"projection_traversal_ms":0.2798,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0029,"lightweight_can... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:21:     {"dataset_size":100000,"query_class":"selective","query":"unique_component_7391","temperature":"warm","iteration":5,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.0201,"projection_traversal_ms":0.2963,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0031,"lightweight_can... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:22:     {"dataset_size":100000,"query_class":"selective","query":"unique_component_7391","temperature":"warm","iteration":6,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.0233,"projection_traversal_ms":0.2831,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0029,"lightweight_can... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:23:     {"dataset_size":100000,"query_class":"selective","query":"unique_component_7391","temperature":"warm","iteration":7,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.0209,"projection_traversal_ms":0.2740,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0033,"lightweight_can... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:24:     {"dataset_size":100000,"query_class":"selective","query":"unique_component_7391","temperature":"warm","iteration":8,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.0205,"projection_traversal_ms":0.3690,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0065,"lightweight_can... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:25:     {"dataset_size":100000,"query_class":"selective","query":"unique_component_7391","temperature":"warm","iteration":9,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.0424,"projection_traversal_ms":0.2723,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0037,"lightweight_can... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:26:     {"dataset_size":100000,"query_class":"moderate","query":"report","temperature":"warm","iteration":0,"result_count":4096,"candidate_count":9844,"candidates_visited":9844,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":true,"filename_index_candidate_count":9844,"sqlite_ms":0.0953,"projection_traversal_ms":48.5415,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":28.4798,"lightweight_cand... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:27:     {"dataset_size":100000,"query_class":"moderate","query":"report","temperature":"warm","iteration":1,"result_count":4096,"candidate_count":9844,"candidates_visited":9844,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":true,"filename_index_candidate_count":9844,"sqlite_ms":0.1158,"projection_traversal_ms":60.3254,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":37.0688,"lightweight_cand... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:28:     {"dataset_size":100000,"query_class":"moderate","query":"report","temperature":"warm","iteration":2,"result_count":4096,"candidate_count":9844,"candidates_visited":9844,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":true,"filename_index_candidate_count":9844,"sqlite_ms":0.1036,"projection_traversal_ms":55.5379,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":33.5185,"lightweight_cand... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:29:     {"dataset_size":100000,"query_class":"moderate","query":"report","temperature":"warm","iteration":3,"result_count":4096,"candidate_count":9844,"candidates_visited":9844,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":true,"filename_index_candidate_count":9844,"sqlite_ms":0.1043,"projection_traversal_ms":57.6211,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":34.1783,"lightweight_cand... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:30:     {"dataset_size":100000,"query_class":"moderate","query":"report","temperature":"warm","iteration":4,"result_count":4096,"candidate_count":9844,"candidates_visited":9844,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":true,"filename_index_candidate_count":9844,"sqlite_ms":0.0942,"projection_traversal_ms":57.4674,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":34.4873,"lightweight_cand... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:31:     {"dataset_size":100000,"query_class":"moderate","query":"report","temperature":"warm","iteration":5,"result_count":4096,"candidate_count":9844,"candidates_visited":9844,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":true,"filename_index_candidate_count":9844,"sqlite_ms":0.1072,"projection_traversal_ms":56.0617,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":33.6183,"lightweight_cand... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:32:     {"dataset_size":100000,"query_class":"moderate","query":"report","temperature":"warm","iteration":6,"result_count":4096,"candidate_count":9844,"candidates_visited":9844,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":true,"filename_index_candidate_count":9844,"sqlite_ms":0.0790,"projection_traversal_ms":63.3386,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":37.7240,"lightweight_cand... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:33:     {"dataset_size":100000,"query_class":"moderate","query":"report","temperature":"warm","iteration":7,"result_count":4096,"candidate_count":9844,"candidates_visited":9844,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":true,"filename_index_candidate_count":9844,"sqlite_ms":0.1022,"projection_traversal_ms":73.2820,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":39.8912,"lightweight_cand... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:34:     {"dataset_size":100000,"query_class":"moderate","query":"report","temperature":"warm","iteration":8,"result_count":4096,"candidate_count":9844,"candidates_visited":9844,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":true,"filename_index_candidate_count":9844,"sqlite_ms":0.1039,"projection_traversal_ms":54.7789,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":33.2788,"lightweight_cand... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:35:     {"dataset_size":100000,"query_class":"moderate","query":"report","temperature":"warm","iteration":9,"result_count":4096,"candidate_count":9844,"candidates_visited":9844,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":true,"filename_index_candidate_count":9844,"sqlite_ms":0.1324,"projection_traversal_ms":55.8091,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":33.3507,"lightweight_cand... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:36:     {"dataset_size":100000,"query_class":"broad","query":"common","temperature":"warm","iteration":0,"result_count":4096,"candidate_count":29530,"candidates_visited":29530,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":true,"filename_index_candidate_count":29530,"sqlite_ms":0.0990,"projection_traversal_ms":164.7092,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":103.4378,"lightweight_ca... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:37:     {"dataset_size":100000,"query_class":"broad","query":"common","temperature":"warm","iteration":1,"result_count":4096,"candidate_count":29530,"candidates_visited":29530,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":true,"filename_index_candidate_count":29530,"sqlite_ms":0.0971,"projection_traversal_ms":167.8807,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":101.1255,"lightweight_ca... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:38:     {"dataset_size":100000,"query_class":"broad","query":"common","temperature":"warm","iteration":2,"result_count":4096,"candidate_count":29530,"candidates_visited":29530,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":true,"filename_index_candidate_count":29530,"sqlite_ms":0.1081,"projection_traversal_ms":147.6198,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":92.0703,"lightweight_can... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:39:     {"dataset_size":100000,"query_class":"broad","query":"common","temperature":"warm","iteration":3,"result_count":4096,"candidate_count":29530,"candidates_visited":29530,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":true,"filename_index_candidate_count":29530,"sqlite_ms":0.0927,"projection_traversal_ms":152.0961,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":96.8397,"lightweight_can... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:40:     {"dataset_size":100000,"query_class":"broad","query":"common","temperature":"warm","iteration":4,"result_count":4096,"candidate_count":29530,"candidates_visited":29530,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":true,"filename_index_candidate_count":29530,"sqlite_ms":0.1153,"projection_traversal_ms":145.0303,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":87.8331,"lightweight_can... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:41:     {"dataset_size":100000,"query_class":"broad","query":"common","temperature":"warm","iteration":5,"result_count":4096,"candidate_count":29530,"candidates_visited":29530,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":true,"filename_index_candidate_count":29530,"sqlite_ms":0.0908,"projection_traversal_ms":148.8682,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":92.8579,"lightweight_can... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:42:     {"dataset_size":100000,"query_class":"broad","query":"common","temperature":"warm","iteration":6,"result_count":4096,"candidate_count":29530,"candidates_visited":29530,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":true,"filename_index_candidate_count":29530,"sqlite_ms":0.1125,"projection_traversal_ms":134.8151,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":81.3056,"lightweight_can... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:43:     {"dataset_size":100000,"query_class":"broad","query":"common","temperature":"warm","iteration":7,"result_count":4096,"candidate_count":29530,"candidates_visited":29530,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":true,"filename_index_candidate_count":29530,"sqlite_ms":0.1043,"projection_traversal_ms":147.2422,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":88.6464,"lightweight_can... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:44:     {"dataset_size":100000,"query_class":"broad","query":"common","temperature":"warm","iteration":8,"result_count":4096,"candidate_count":29530,"candidates_visited":29530,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":true,"filename_index_candidate_count":29530,"sqlite_ms":0.1136,"projection_traversal_ms":149.0251,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":90.7404,"lightweight_can... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:45:     {"dataset_size":100000,"query_class":"broad","query":"common","temperature":"warm","iteration":9,"result_count":4096,"candidate_count":29530,"candidates_visited":29530,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":true,"filename_index_candidate_count":29530,"sqlite_ms":0.1049,"projection_traversal_ms":163.9056,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":100.8132,"lightweight_ca... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:46:     {"dataset_size":100000,"query_class":"no_match","query":"zzzz_never_present","temperature":"warm","iteration":0,"result_count":0,"candidate_count":1562,"candidates_visited":1562,"truncated":false,"wire_result_count":0,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1562,"sqlite_ms":0.0212,"projection_traversal_ms":0.1037,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0000,"lightweigh... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:47:     {"dataset_size":100000,"query_class":"no_match","query":"zzzz_never_present","temperature":"warm","iteration":1,"result_count":0,"candidate_count":1562,"candidates_visited":1562,"truncated":false,"wire_result_count":0,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1562,"sqlite_ms":0.0195,"projection_traversal_ms":0.1004,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0000,"lightweigh... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:48:     {"dataset_size":100000,"query_class":"no_match","query":"zzzz_never_present","temperature":"warm","iteration":2,"result_count":0,"candidate_count":1562,"candidates_visited":1562,"truncated":false,"wire_result_count":0,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1562,"sqlite_ms":0.0185,"projection_traversal_ms":0.0982,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0000,"lightweigh... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:49:     {"dataset_size":100000,"query_class":"no_match","query":"zzzz_never_present","temperature":"warm","iteration":3,"result_count":0,"candidate_count":1562,"candidates_visited":1562,"truncated":false,"wire_result_count":0,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1562,"sqlite_ms":0.0170,"projection_traversal_ms":0.1191,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0000,"lightweigh... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:50:     {"dataset_size":100000,"query_class":"no_match","query":"zzzz_never_present","temperature":"warm","iteration":4,"result_count":0,"candidate_count":1562,"candidates_visited":1562,"truncated":false,"wire_result_count":0,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1562,"sqlite_ms":0.0216,"projection_traversal_ms":0.1064,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0000,"lightweigh... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:51:     {"dataset_size":100000,"query_class":"no_match","query":"zzzz_never_present","temperature":"warm","iteration":5,"result_count":0,"candidate_count":1562,"candidates_visited":1562,"truncated":false,"wire_result_count":0,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1562,"sqlite_ms":0.0191,"projection_traversal_ms":0.0984,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0000,"lightweigh... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:52:     {"dataset_size":100000,"query_class":"no_match","query":"zzzz_never_present","temperature":"warm","iteration":6,"result_count":0,"candidate_count":1562,"candidates_visited":1562,"truncated":false,"wire_result_count":0,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1562,"sqlite_ms":0.0284,"projection_traversal_ms":0.1067,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0000,"lightweigh... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:53:     {"dataset_size":100000,"query_class":"no_match","query":"zzzz_never_present","temperature":"warm","iteration":7,"result_count":0,"candidate_count":1562,"candidates_visited":1562,"truncated":false,"wire_result_count":0,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1562,"sqlite_ms":0.0313,"projection_traversal_ms":0.1232,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0000,"lightweigh... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:54:     {"dataset_size":100000,"query_class":"no_match","query":"zzzz_never_present","temperature":"warm","iteration":8,"result_count":0,"candidate_count":1562,"candidates_visited":1562,"truncated":false,"wire_result_count":0,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1562,"sqlite_ms":0.0360,"projection_traversal_ms":0.1041,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0000,"lightweigh... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:55:     {"dataset_size":100000,"query_class":"no_match","query":"zzzz_never_present","temperature":"warm","iteration":9,"result_count":0,"candidate_count":1562,"candidates_visited":1562,"truncated":false,"wire_result_count":0,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1562,"sqlite_ms":0.0254,"projection_traversal_ms":0.1038,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0000,"lightweigh... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:56:     {"dataset_size":100000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":0,"result_count":4096,"candidate_count":9844,"candidates_visited":9844,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":true,"filename_index_candidate_count":9844,"sqlite_ms":0.0966,"projection_traversal_ms":56.6028,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":34.1752,"lightweight_candida... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:57:     {"dataset_size":100000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":1,"result_count":4096,"candidate_count":9844,"candidates_visited":9844,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":true,"filename_index_candidate_count":9844,"sqlite_ms":0.0974,"projection_traversal_ms":62.0073,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":34.3377,"lightweight_candida... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:58:     {"dataset_size":100000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":2,"result_count":4096,"candidate_count":9844,"candidates_visited":9844,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":true,"filename_index_candidate_count":9844,"sqlite_ms":0.4100,"projection_traversal_ms":55.7871,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":35.2915,"lightweight_candida... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:59:     {"dataset_size":100000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":3,"result_count":4096,"candidate_count":9844,"candidates_visited":9844,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":true,"filename_index_candidate_count":9844,"sqlite_ms":0.0999,"projection_traversal_ms":55.0387,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":33.4297,"lightweight_candida... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:60:     {"dataset_size":100000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":4,"result_count":4096,"candidate_count":9844,"candidates_visited":9844,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":true,"filename_index_candidate_count":9844,"sqlite_ms":0.1149,"projection_traversal_ms":62.7013,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":39.7109,"lightweight_candida... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:61:     {"dataset_size":100000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":5,"result_count":4096,"candidate_count":9844,"candidates_visited":9844,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":true,"filename_index_candidate_count":9844,"sqlite_ms":0.0876,"projection_traversal_ms":57.5501,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":34.1175,"lightweight_candida... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:62:     {"dataset_size":100000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":6,"result_count":4096,"candidate_count":9844,"candidates_visited":9844,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":true,"filename_index_candidate_count":9844,"sqlite_ms":0.0990,"projection_traversal_ms":56.5877,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":35.3418,"lightweight_candida... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:63:     {"dataset_size":100000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":7,"result_count":4096,"candidate_count":9844,"candidates_visited":9844,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":true,"filename_index_candidate_count":9844,"sqlite_ms":0.5695,"projection_traversal_ms":61.5546,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":37.2166,"lightweight_candida... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:64:     {"dataset_size":100000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":8,"result_count":4096,"candidate_count":9844,"candidates_visited":9844,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":true,"filename_index_candidate_count":9844,"sqlite_ms":0.1227,"projection_traversal_ms":63.8041,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":37.0043,"lightweight_candida... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:65:     {"dataset_size":100000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":9,"result_count":4096,"candidate_count":9844,"candidates_visited":9844,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":true,"filename_index_candidate_count":9844,"sqlite_ms":0.1185,"projection_traversal_ms":54.6418,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":32.8566,"lightweight_candida... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:66:     {"dataset_size":100000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":0,"result_count":4096,"candidate_count":100000,"candidates_visited":100000,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"sqlite_ms":0.1000,"projection_traversal_ms":150.8995,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":89.7431,"lightweight... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:67:     {"dataset_size":100000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":1,"result_count":4096,"candidate_count":100000,"candidates_visited":100000,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"sqlite_ms":0.1107,"projection_traversal_ms":146.5376,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":89.0640,"lightweight... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:68:     {"dataset_size":100000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":2,"result_count":4096,"candidate_count":100000,"candidates_visited":100000,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"sqlite_ms":0.0981,"projection_traversal_ms":152.6621,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":93.8012,"lightweight... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:69:     {"dataset_size":100000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":3,"result_count":4096,"candidate_count":100000,"candidates_visited":100000,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"sqlite_ms":0.0939,"projection_traversal_ms":174.0874,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":103.8573,"lightweigh... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:70:     {"dataset_size":100000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":4,"result_count":4096,"candidate_count":100000,"candidates_visited":100000,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"sqlite_ms":0.1107,"projection_traversal_ms":172.1799,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":106.2781,"lightweigh... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:71:     {"dataset_size":100000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":5,"result_count":4096,"candidate_count":100000,"candidates_visited":100000,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"sqlite_ms":0.1048,"projection_traversal_ms":140.9376,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":84.9222,"lightweight... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:72:     {"dataset_size":100000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":6,"result_count":4096,"candidate_count":100000,"candidates_visited":100000,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"sqlite_ms":0.0810,"projection_traversal_ms":167.0888,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":100.9625,"lightweigh... [truncated]
docs/benchmarks/phase8-large-index-scalability.json:73:     {"dataset_size":100000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":7,"result_count":4096,"candidate_count":100000,"candidates_visited":100000,"truncated":true,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"sqlite_ms":0.1586,"projection_traversal_ms":168.1583,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":100.2339,"lightweigh... [truncated]
[60 matches limit reached. Use limit=120 for more, or refine pattern. Some lines truncated to 500 chars. Use read tool to see full lines]
openspec/changes/performance-closure-and-multisurface-acceleration/tasks.md:48: - [x] 7.1 After the name decision, freshly benchmark selective, moderate, broad, no-result, exact, short-prefix, mixed-case, Unicode, high-limit, and cancellation or replacement queries on the current candidate. Do not use the historical ~95 ms figure as the before row.
openspec/changes/performance-closure-and-multisurface-acceleration/specs/performance-closure-and-multisurface-acceleration/spec.md:150: After the name-path candidate is retained or rejected, the campaign SHALL measure selective, moderate, broad, no-result, exact, short-prefix, mixed-case, Unicode, high-limit, and cancellation or replacement queries on the current candidate. It MUST profile candidate acquisition, normalized-field access, matching, result construction, sorting, top-N selection, and publication before selecting a search optimization.
openspec/changes/performance-closure-and-multisurface-acceleration/design.md:150: **Search:** Use the current candidate and the existing informational benches (`ffengine_bench_large_index_scalability`, `ffengine_bench_secondary_filename_index`, and the literal-matcher bench) before adding a harness. Cover selective, moderate, broad, no-result, exact, short-prefix, mixed-case, Unicode, high result limit, and cancellation/replacement. Profile acquisition, normalized-field access, matching, result construction, sorting, top-N, and publication. The filename index already exists a... [truncated]
openspec/changes/performance-closure-and-multisurface-acceleration/APPLY_PROMPT.md:106: - Freshly benchmark selective, moderate, broad, no-result, exact, short-prefix, mixed-case, Unicode, high-limit, and cancellation/replacement queries. Profile acquisition, normalized-field access, matching, result construction, sorting, top-N, and publication before choosing a search change.
openspec/changes/large-index-lifecycle-and-search-scalability/tasks.md:30: - [x] 5.1 Benchmark the 12 query classes (selective, moderate, broad, no_match, prefix, extension, worst_substring, exact_name, short_prefix, mixed_case, unicode, high_limit_broad) at 200k/1M/2M on the current tree with machine-readable output and idle gating. Evidence: `evidence/search/search-matrix-before.json` (in collection).
openspec/changes/performance-closure-and-multisurface-acceleration/evidence/final-report.md:145: scan-floor classes): **broad -50.9%, extension -63.5%, short-prefix -51.6%,
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/explore-findings.md:44:   prefix 110/532/10,762; short_prefix 139/573/29,272; mixed_case 133/579/21,120;
openspec/changes/durable-cold-search-index-and-hybrid-residency/proposal.md:31: - non-literal classes (broad, extension, short_prefix, unicode) are
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/memory-cold-decision.md:41: | short_prefix | 16,354 / 1,000,000 | 15,357 / 1,000,000 | prefilter ineligible (<3) |
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/memory-cold-decision.md:62:   block (extension 19,971->21,758 ms; short_prefix 16,354->15,357; unicode
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-planner-fix.json:96:     {"dataset_size":200000,"query_class":"short_prefix","query":"co","temperature":"warm","iteration":0,"result_count":4096,"candidate_count":200000,"candidates_visited":19573,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durabl... [truncated]
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-planner-fix.json:97:     {"dataset_size":200000,"query_class":"short_prefix","query":"co","temperature":"warm","iteration":1,"result_count":4096,"candidate_count":200000,"candidates_visited":19573,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durabl... [truncated]
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-planner-fix.json:98:     {"dataset_size":200000,"query_class":"short_prefix","query":"co","temperature":"warm","iteration":2,"result_count":4096,"candidate_count":200000,"candidates_visited":19573,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durabl... [truncated]
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-planner-fix.json:99:     {"dataset_size":200000,"query_class":"short_prefix","query":"co","temperature":"warm","iteration":3,"result_count":4096,"candidate_count":200000,"candidates_visited":19573,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durabl... [truncated]
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-planner-fix.json:100:     {"dataset_size":200000,"query_class":"short_prefix","query":"co","temperature":"warm","iteration":4,"result_count":4096,"candidate_count":200000,"candidates_visited":19573,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durabl... [truncated]
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-planner-fix.json:101:     {"dataset_size":200000,"query_class":"short_prefix","query":"co","temperature":"warm","iteration":5,"result_count":4096,"candidate_count":200000,"candidates_visited":19573,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durabl... [truncated]
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-planner-fix.json:102:     {"dataset_size":200000,"query_class":"short_prefix","query":"co","temperature":"warm","iteration":6,"result_count":4096,"candidate_count":200000,"candidates_visited":19573,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durabl... [truncated]
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-planner-fix.json:103:     {"dataset_size":200000,"query_class":"short_prefix","query":"co","temperature":"warm","iteration":7,"result_count":4096,"candidate_count":200000,"candidates_visited":19573,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durabl... [truncated]
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-planner-fix.json:104:     {"dataset_size":200000,"query_class":"short_prefix","query":"co","temperature":"warm","iteration":8,"result_count":4096,"candidate_count":200000,"candidates_visited":19573,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durabl... [truncated]
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-planner-fix.json:105:     {"dataset_size":200000,"query_class":"short_prefix","query":"co","temperature":"warm","iteration":9,"result_count":4096,"candidate_count":200000,"candidates_visited":19573,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durabl... [truncated]
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-planner-fix.json:144:     {"dataset_size":200000,"query_class":"short_prefix","query":"co","temperature":"cold","iteration":0,"result_count":4096,"candidate_count":200000,"candidates_visited":200000,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":5,"query_visible_epoch":5,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durab... [truncated]
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-planner-fix.json:192:     {"dataset_size":1000000,"query_class":"short_prefix","query":"co","temperature":"warm","iteration":0,"result_count":4096,"candidate_count":1000000,"candidates_visited":52615,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"dura... [truncated]
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-planner-fix.json:193:     {"dataset_size":1000000,"query_class":"short_prefix","query":"co","temperature":"warm","iteration":1,"result_count":4096,"candidate_count":1000000,"candidates_visited":52615,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"dura... [truncated]
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-planner-fix.json:194:     {"dataset_size":1000000,"query_class":"short_prefix","query":"co","temperature":"warm","iteration":2,"result_count":4096,"candidate_count":1000000,"candidates_visited":52615,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"dura... [truncated]
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-planner-fix.json:195:     {"dataset_size":1000000,"query_class":"short_prefix","query":"co","temperature":"warm","iteration":3,"result_count":4096,"candidate_count":1000000,"candidates_visited":52615,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"dura... [truncated]
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-planner-fix.json:196:     {"dataset_size":1000000,"query_class":"short_prefix","query":"co","temperature":"warm","iteration":4,"result_count":4096,"candidate_count":1000000,"candidates_visited":52615,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"dura... [truncated]
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-planner-fix.json:220:     {"dataset_size":1000000,"query_class":"short_prefix","query":"co","temperature":"cold","iteration":0,"result_count":4096,"candidate_count":1000000,"candidates_visited":1000000,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":5,"query_visible_epoch":5,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"du... [truncated]
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-planner-fix.json:234:     {"dataset_size":200000,"query_class":"short_prefix","result_count":4096,"min_ms":155.6619,"median_ms":173.2640,"p90_ms":179.8793,"max_ms":180.5735},
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-planner-fix.json:246:     {"dataset_size":1000000,"query_class":"short_prefix","result_count":4096,"min_ms":723.6014,"median_ms":766.6664,"p90_ms":772.5678,"max_ms":788.7429},
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-before.json:97:     {"dataset_size":200000,"query_class":"short_prefix","query":"co","temperature":"warm","iteration":0,"result_count":4096,"candidate_count":200000,"candidates_visited":19573,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durabl... [truncated]
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-before.json:98:     {"dataset_size":200000,"query_class":"short_prefix","query":"co","temperature":"warm","iteration":1,"result_count":4096,"candidate_count":200000,"candidates_visited":19573,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durabl... [truncated]
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-before.json:99:     {"dataset_size":200000,"query_class":"short_prefix","query":"co","temperature":"warm","iteration":2,"result_count":4096,"candidate_count":200000,"candidates_visited":19573,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durabl... [truncated]
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-before.json:100:     {"dataset_size":200000,"query_class":"short_prefix","query":"co","temperature":"warm","iteration":3,"result_count":4096,"candidate_count":200000,"candidates_visited":19573,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durabl... [truncated]
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-before.json:101:     {"dataset_size":200000,"query_class":"short_prefix","query":"co","temperature":"warm","iteration":4,"result_count":4096,"candidate_count":200000,"candidates_visited":19573,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durabl... [truncated]
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-before.json:102:     {"dataset_size":200000,"query_class":"short_prefix","query":"co","temperature":"warm","iteration":5,"result_count":4096,"candidate_count":200000,"candidates_visited":19573,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durabl... [truncated]
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-before.json:103:     {"dataset_size":200000,"query_class":"short_prefix","query":"co","temperature":"warm","iteration":6,"result_count":4096,"candidate_count":200000,"candidates_visited":19573,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durabl... [truncated]
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-before.json:104:     {"dataset_size":200000,"query_class":"short_prefix","query":"co","temperature":"warm","iteration":7,"result_count":4096,"candidate_count":200000,"candidates_visited":19573,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durabl... [truncated]
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-before.json:105:     {"dataset_size":200000,"query_class":"short_prefix","query":"co","temperature":"warm","iteration":8,"result_count":4096,"candidate_count":200000,"candidates_visited":19573,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durabl... [truncated]
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-before.json:106:     {"dataset_size":200000,"query_class":"short_prefix","query":"co","temperature":"warm","iteration":9,"result_count":4096,"candidate_count":200000,"candidates_visited":19573,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durabl... [truncated]
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-before.json:145:     {"dataset_size":200000,"query_class":"short_prefix","query":"co","temperature":"cold","iteration":0,"result_count":4096,"candidate_count":200000,"candidates_visited":200000,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":5,"query_visible_epoch":5,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durab... [truncated]
[40 matches limit reached. Use limit=80 for more, or refine pattern. Some lines truncated to 500 chars. Use read tool to see full lines]
grep: C:/Users/Michael Roy/Documents/FastFiles
ls: C:/Users/Michael Roy/Documents/FastFiles/tests/engine
tests/engine/bench_large_index_scalability.cpp:2133:         {"worst_substring", L"component", 3},
tests/engine/bench_large_index_scalability.cpp:2135:         {"exact_name", L"unique_component_7391.txt", 0},
tests/engine/bench_large_index_scalability.cpp:2139:         {"high_limit_broad", L"common", 0, 50000},
AuthenticodeFixture.ps1
AuthenticodeFixtureSupport.h
bench_allocation_isolation.cpp
bench_allocator_probe.h
bench_first_generation_publish.cpp
bench_large_index_scalability.cpp
bench_secondary_filename_index.cpp
CMakeLists.txt
test_active_path_resilience.cpp
test_authenticode_verification.cpp
test_authenticode_wintrust_integration.cpp
test_degraded_special_files.cpp
test_directory_watcher.cpp
test_engine_client_handshake.cpp
test_idle_manager.cpp
test_index_pipeline_concurrency.cpp
test_index_pipeline.cpp
test_legacy_scan_failure_binding.cpp
test_pooled_snapshot_lifetime.cpp
test_privileged_connection_testing_policy.cpp
test_seeded_indexed_mode.cpp
test_snapshot_publisher.cpp
test_subtree_gating.cpp
test_ui_server_lifecycle.cpp
test_volume_session_manager.cpp
read: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/ben...
grep: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/tes...
[pi-lens: read widened to the enclosing function_definition "function_definition" (symbol boundary): you asked for lines 2100-2169, this shows lines 1997-2169. Re-request with limit > 100 for the exact range.]
int RunMemoryReportMode(const Options& options) {
    ffengine::SnapshotPublisher publisher;
    if (!publisher.Start(GetCurrentProcessId())) {
        std::fprintf(stderr, "snapshot publisher start failed\n");
        return 1;
    }
    std::vector<ScaleMemoryReport> reports;
    bool allSucceeded = true;
    for (const size_t scale : options.scales) {
        std::printf("Projection-memory decomposition: building %zu synthetic entries...\n", scale);
        std::fflush(stdout);
        ScaleMemoryReport report;
        if (!RunScaleMemoryReport(scale, options, publisher, report)) {
            allSucceeded = false;
            std::fprintf(stderr, "memory decomposition failed at %zu: %s\n", scale,
                         report.failureReason.c_str());
        }
        reports.push_back(std::move(report));
    }
    publisher.Stop();
    std::filesystem::path parent = options.memoryOutputPath.parent_path();
    if (!parent.empty()) {
        std::error_code error;
        std::filesystem::create_directories(parent, error);
        if (error) {
            std::fprintf(stderr, "unable to create memory report directory\n");
            return 1;
        }
    }
    std::ofstream output(options.memoryOutputPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        std::fprintf(stderr, "unable to open memory report output: %s\n",
                     PathToUtf8(options.memoryOutputPath).c_str());
        return 1;
    }
    const MachineInfo machine = ReadMachineInfo();
    output << "{\n  \"schema\":\"fastfiles-projection-memory-decomposition-v1\"\n"
           << "  ,\"generated_epoch_seconds\":" << static_cast<long long>(std::time(nullptr)) << '\n'
           << "  ,\"build_identity\":\"" << ffsetup::kBuildIdentityAscii << "\"\n"
           << "  ,\"build_configuration\":";
    WriteJsonString(output, machine.buildConfiguration);
    output << "\n  ,\"process_id\":" << GetCurrentProcessId()
           << "\n  ,\"requested_scales\":[";
    for (size_t index = 0; index < options.scales.size(); ++index) {
        if (index != 0) output << ',';
        output << options.scales[index];
    }
    output << "]";
    output << "\n  ,\"command_line\":";
    WriteJsonString(output, options.commandLine);
    output << "\n  ,\"source_state\":";
    WriteJsonString(output, options.sourceState);
    output << "\n  ,\"source_sha\":";
    WriteJsonString(output, options.sourceSha);
    output << "\n  ,\"preset\":";
    WriteJsonString(output, options.preset);
    output << "\n  ,\"units_are_binary\":true"
           << "\n  ,\"projection_memory_layout_revision\":"
           << ffindexstore::kProjectionMemoryLayoutRevision
           << "\n  ,\"allocator_probe_enabled\":true";
    output << "\n  ,\"rebuild_cycles_requested\":" << options.rebuildCycles
           << "\n  ,\"export_bound_directories\":" << options.exportBoundDirectories
           << "\n  ,\"harness_retention\":{\"capture_point_bytes\":" << sizeof(MemoryCapturePoint)
           << ",\"note\":\"each rebuild cycle and lifecycle capture retains one MemoryCapturePoint "
              "(plus its capture-name string) inside this report; that harness retention is part of the "
              "measured live-allocator delta and is reconciled against the projection category sums\"}"
           << "\n  ,\"machine\":";
    WriteMachine(output, machine);
    output << "\n  ,\"instrumentation\":{"
              "\"allocator_probe\":\"replaced global operator new/delete: live+cumulative requested and "
              "usable bytes, allocation counts, live peak; C++ allocations only (SQLite C allocations are "
              "outside the counter)\","
              "\"process_memory\":\"GetProcessMemoryInfo PROCESS_MEMORY_COUNTERS_EX (working set, private "
              "usage, peak working set, peak pagefile)\","
              "\"working_set_split\":\"QueryWorkingSet + QueryWorkingSetEx page classification (shareable "
              "vs private)\","
              "\"projection_categories\":\"Projection::GetMemoryBreakdown live capacity/size/bucket_count "
              "measurements; modeled_category_bytes equals Projection::EstimatedMemoryBytes\","
              "\"units\":\"bytes and milliseconds\"}\n"
           << "  ,\"limitations\":["
              "\"allocator counts cover C++ operator new/delete only; SQLite (compiled as C) allocates "
              "through malloc/free and appears in the private-bytes residual instead\","
              "\"standard-library node links, the probe's 32-byte allocation header, heap block headers and "
              "heap rounding are residual and are not attributed to any projection category\","
              "\"working-set shareable pages identify shareable/pagefile-backed pages; they are not proof "
              "that another process maps the same section\","
              "\"the export weighing uses the real IndexPipeline pooled/owning export and SnapshotPublisher "
              "publish APIs in one process; the UI-side parse here is the in-process published-section read, "
              "not the FastFiles UI process\","
              "\"timings in this mode are single-process measurements and are not the campaign's idle-gated "
              "latency protocol\","
              "\"this executable replaces the global operator new/delete in every mode, so any other mode of this benchmark binary is instrumented and must not be used as uninstrumented latency evidence\","
              "\"all byte quantities are binary (MiB = 1048576 bytes); allocator counts exclude the C-only SQLite allocation plane\"]\n"
           << "  ,\"scales\":[";
    for (size_t index = 0; index < reports.size(); ++index) {
        if (index != 0) output << ',';
        WriteScaleMemoryReport(output, reports[index]);
    }
    output << "]\n}\n";
    output.close();
    std::printf("Wrote %s\n", PathToUtf8(options.memoryOutputPath).c_str());
    return allSucceeded ? 0 : 1;
}
} // namespace
int wmain(int argc, wchar_t** argv) {
    Options options;
    if (!ParseOptions(argc, argv, options)) return 2;
    // performance-closure-and-multisurface-acceleration 8.1: the memory mode is
    // a separate, focused path (fixture -> rebuild cycles -> export weighing ->
    // teardown) that writes its own machine-readable report.
    if (options.memoryReport) {
        if (options.memoryOutputPath.empty()) {
            std::fprintf(stderr, "--memory-report requires an output path\n");
            return 2;
        }
        return RunMemoryReportMode(options);
    }
    std::vector<DatasetResult> datasets;
    std::vector<SearchRun> searchRuns;
    std::vector<SearchSummary> searchSummaries;
    std::vector<IncrementalRow> incremental;
    const std::vector<QueryCase> queryCases{
        {"selective", L"unique_component_7391", 0},
        {"moderate", L"report", 0},
        {"broad", L"common", 0},
        {"no_match", L"zzzz_never_present", 0},
        {"prefix", L"photo", 0},
        {"extension", L"ext:txt", 3},
        {"worst_substring", L"component", 3},
        // performance-closure-and-multisurface-acceleration 7.1 additions:
        {"exact_name", L"unique_component_7391.txt", 0},
        {"short_prefix", L"co", 0},
        {"mixed_case", L"CoMmOn_CoMpOnEnT", 0},
        {"unicode", L"\u0441\u043f\u0438\u0441\u043e\u043a", 0},
        {"high_limit_broad", L"common", 0, 50000},
    };
    ffengine::SnapshotPublisher publisher;
    if (!publisher.Start(GetCurrentProcessId())) {
        std::fprintf(stderr, "snapshot publisher start failed\n");
        return 1;
    }
    std::printf("FastFiles large-index scalability benchmark (%zu dataset scales)\n", options.scales.size());
    std::fflush(stdout);
    for (const size_t scale : options.scales) {
        std::printf("Building %zu synthetic entries...\n", scale);
        std::fflush(stdout);
        const std::filesystem::path temporaryDirectory = MakeTemporaryDirectory(scale);
        if (temporaryDirectory.empty()) {
            std::fprintf(stderr, "unable to create temporary directory for %zu\n", scale);
            return 1;
        }
        const std::filesystem::path database = temporaryDirectory / L"index.db";
        DatasetResult dataset;
        dataset.scale = scale;
        dataset.databaseLocation = database.wstring();
        ffindexstore::VolumeRowId volumeId = 0;
        if (!BuildSyntheticDatabase(database, scale, dataset.generationMs, volumeId,
                                     dataset.beforeFixture, dataset.afterFixture,
                                     options.detailedDiagnostics)) {
            std::fprintf(stderr, "synthetic database generation failed at %zu\n", scale);
            std::error_code ignored;
            std::filesystem::remove_all(temporaryDirectory, ignored);
            return 1;
        }
[250 more lines in file. Use offset=2170 to continue.]
test_index_pipeline.cpp:141: void TestBulkInsertTriggersAnalyzeAtTenThousandRecords() {
test_index_pipeline.cpp:179: void TestVolumeResolutionIsStable() {
test_index_pipeline.cpp:189: void TestApplyMftBatchAndExportSnapshot() {
test_index_pipeline.cpp:222: void TestRebuildAfterRestartMatchesPriorState() {
test_index_pipeline.cpp:248: void TestMftV2BatchIngestsAllocatedSizeAndExportsToSnapshot() {
test_index_pipeline.cpp:313: void TestUsnV3PreservesAndCorrectsAllocatedSize() {
test_index_pipeline.cpp:393: void TestReconciliationRemovesEntryNotSeenInFullScan() {    IndexPipeline pipeline;
test_index_pipeline.cpp:417: void TestUsnDeleteReasonRemovesEntry() {
test_index_pipeline.cpp:441: void TestUsnPartialUpdatesPreserveAuthoritativeMetadataAcrossRestart() {
test_index_pipeline.cpp:548: void TestNewUsnOnlyEntryPersistsMftEnrichedMetadata() {
test_index_pipeline.cpp:668: void TestVolumeAvailabilityAndForget() {
test_index_pipeline.cpp:685: void TestForgetVolumePurgesProjection() {
test_index_pipeline.cpp:718: void TestIndexedSearchUsesProjectionPathsAndScopes() {
test_index_pipeline.cpp:855: void TestIndexedSearchViewFirstCaptureReducesMaterialization() {
test_index_pipeline.cpp:941: void TestIndexedSearchCancellationStopsProjectionWork() {
test_index_pipeline.cpp:1033: void TestIndexedSearchPropagatesAllocatedSize() {
test_index_pipeline.cpp:1108: void TestIndexedSearchOndiskFilter() {
test_index_pipeline.cpp:1276: void TestForcedEvictionSearchEquivalence() {
test_index_pipeline.cpp:1477: void TestCappedRootSearchMatchesColdAndRetainsTopN() {
test_index_pipeline.cpp:1564: void TestRepeatedColdSearchesDoNotThrashMemory() {
test_index_pipeline.cpp:1622: void TestColdMutationsAndRestartEquivalence() {
test_index_pipeline.cpp:1745: void TestColdFolderAggregateServedFromStore() {
test_index_pipeline.cpp:1792: void TestCancellationDuringColdStream() {
test_index_pipeline.cpp:1834: void TestMftSecondaryFailureIsFailClosedAndRetryable() {
test_index_pipeline.cpp:1925: void TestUsnSecondaryFailureIsBoundarySpecificAndRetryable() {
test_index_pipeline.cpp:2024: void TestRebuildSecondaryFailureDoesNotPublishSuccess() {
test_index_pipeline.cpp:2135: void TestReconciliationRetentionOnSecondaryFailure() {
test_index_pipeline.cpp:2210: void TestRepeatedSecondaryFailure() {
test_index_pipeline.cpp:2262: void TestRulePruningFailureRecoversAfterCloseAndOpen() {
test_index_pipeline.cpp:2321: void TestUsnUpsertDuringReconciliationPassSurvivesFinish() {
test_index_pipeline.cpp:2368: void TestRuleExcludedUsnUpsertStaysOutOfSeenSet() {
test_index_pipeline.cpp:2402: void TestAbortReconciliationPassDropsWithoutRemovals() {
test_index_pipeline.cpp:2451: void TestEnforcementEvictionAdvancesQueryVisibleEpochs() {
grep: C:/Users/Michael Roy/Documents/FastFiles/tests/indexstore...
test_projection.cpp:25: void TestSiblingsDoNotDuplicateSharedAncestorPath() {
test_projection.cpp:41: void TestRepeatedNamesAcrossTreeShareOneInternedId() {
test_projection.cpp:59: void TestPathIsReconstructedOnDemand() {
test_projection.cpp:71: void TestChildrenLookupSupportsDirectoryListing() {
test_projection.cpp:82: void TestReparentingUpdatesParentChildIndex() {
test_projection.cpp:97: void TestPartialUpsertPreservesAbsentMetadata() {
test_projection.cpp:122: // allocated-size-storage-accuracy 4.2: full scans set both sizes; a partial
test_projection.cpp:123: // USN-style update without the allocated-size bit preserves the previous
test_projection.cpp:124: // value (and known state); a partial update with the bit corrects it.
test_projection.cpp:125: void TestAllocatedSizeFullPartialPreserveAndReconcile() {
test_projection.cpp:182: // allocated-size-storage-accuracy 4.2/D7: the projection aggregate carries
test_projection.cpp:183: // both totals with an explicit completeness pair and saturating arithmetic;
test_projection.cpp:184: // a partial subtotal is never presented as an exact total.
test_projection.cpp:185: void TestFolderAggregateWithAllocatedSizeAndCompleteness() {
test_projection.cpp:219: // allocated-size-storage-accuracy 4.2/D7: checked arithmetic -- a subtree
test_projection.cpp:220: // whose sums exceed UINT64_MAX must saturate and mark the aggregate
test_projection.cpp:221: // incomplete, never wrap into a plausible-looking total.
test_projection.cpp:222: void TestFolderAggregateOverflowSaturatesAndMarksIncomplete() {
test_projection.cpp:243: void TestRemoveDropsEntryButLeavesSiblingsIntact() {
test_projection.cpp:255: void TestCycleDoesNotCauseInfiniteWalk() {
test_projection.cpp:266: void TestVolumesAreIsolatedFromEachOther() {
test_projection.cpp:279: void TestRebuildFromStoreMatchesPersistedData() {
test_projection.cpp:306: void TestFolderAggregateCountsSubtree() {
test_projection.cpp:329: // Regression (FlatHashMap.h FlatChildrenMap): the live key count_ was
test_projection.cpp:330: // never incremented on insert, so the children map never grew past its
test_projection.cpp:331: // initial 8 buckets and the 9th distinct parent key sent find_slot()'s
test_projection.cpp:332: // probe loop into an infinite spin (bench_projection_memory.cpp hung
test_projection.cpp:333: // here; the insert/grow/remove count bookkeeping is now fixed). Inserting
test_projection.cpp:334: // well past that threshold must resolve normally.
test_projection.cpp:335: void TestManyDistinctParentsDoNotStallGrowth() {
test_projection.cpp:359: void TestFilenameTrigramCandidatesTrackMutations() {
test_projection.cpp:389: void TestProjectionEvictsLruAndReloadsFromStore() {
test_projection.cpp:441: // completion-integrity-hardening-and-release-certification 4.1.2: a durable
test_projection.cpp:442: // stream failure must not be reported as a successful rebuild or leave a
test_projection.cpp:443: // readable partial volume behind; reopening the store must make the cold
test_projection.cpp:444: // volume retryable.
test_projection.cpp:445: void TestProjectionRebuildFailureIsColdAndRetryable() {
test_projection.cpp:489: void TestProjectionMemoryPressureSignal() {
test_projection.cpp:497: // memory-bounded-indexed-scale 3.1: the memory policy is observable --
test_projection.cpp:498: // configured ceiling, estimated bytes, eviction byte deltas, and monotonic
test_projection.cpp:499: // eviction/reload counters.
test_projection.cpp:500: void TestProjectionMemoryDiagnostics() {
test_projection.cpp:568: void TestProjectionMemoryLimitRunsAtBatchBoundary() {
test_projection.cpp:587: void TestNamePoolViewInvalidatesOnArenaGrowth() {
test_projection.cpp:626: void TestNamePoolViewInvalidatesOnCompactStorage() {
test_projection.cpp:658: // performance-closure-and-multisurface-acceleration 8.1: the read-only
test_projection.cpp:659: // memory breakdown must (a) sum to exactly the estimator the memory policy
test_projection.cpp:660: // uses, (b) report every category from live capacities, and (c) report the
test_projection.cpp:661: // structurally absent categories as explicit zeros.
test_projection.cpp:662: void TestProjectionMemoryBreakdownMatchesEstimatorAndCategories() {
test_projection.cpp:735: // performance-closure-and-multisurface-acceleration 8.3: the dense per-slot
test_projection.cpp:736: // LRU timestamps must survive removal, slot reuse, compaction, and eviction
test_projection.cpp:737: // order exactly as the previous hash-map representation did.
test_projection.cpp:738: void TestDenseLruTimestampsTrackAccessOrder() {
test_projection.cpp:775: // performance-closure-and-multisurface-acceleration 8.3: a bulk rebuild must
test_projection.cpp:776: // release the geometric-growth capacity slack in the name arena, the offset
test_projection.cpp:777: // table, the flat child-index array, and the trigram posting vectors, while
test_projection.cpp:778: // leaving the projection's designed id->index load factor untouched.
test_projection.cpp:779: void TestRebuildReleasesBulkCapacitySlack() {
test_projection.cpp:855: // performance-closure-and-multisurface-acceleration 8.3: a bulk rebuild calls
test_projection.cpp:856: // CompactEntriesForIterationOrder(), which permutes dense entry slots into the
test_projection.cpp:857: // id->index probe order. The dense LRU timestamps must be permuted with the
test_projection.cpp:858: // same mapping, or the eviction policy becomes a hash-order choice.
test_projection.cpp:859: void TestLruTimestampsSurviveIterationOrderCompaction() {
test_projection.cpp:921: // large-index-lifecycle-and-search-scalability 4.5: bulk-removal
test_projection.cpp:922: // equivalence. These characterize observable semantics (they pass with both
test_projection.cpp:923: // the legacy per-entry removal and the bulk path); the performance defect
test_projection.cpp:924: // they accompany is evidenced by the rebuild-isolation matrix (E1 vs B1).
test_projection.cpp:954: void TestRemoveBatchMatchesPerEntryRemoval() {
test_projection.cpp:996: void TestRemoveVolumeKeepsFilenameAcceleratorCurrent() {
test_projection.cpp:1015: void TestRemoveVolumeCanLeaveAcceleratorForCallerRebuild() {
test_projection.cpp:1031: // large-index-lifecycle-and-search-scalability 6.2/6.3 + review-remediation
test_projection.cpp:1032: // (P2-A): filename-accelerator planner. The historical accelerator
test_projection.cpp:1033: // unconditionally unioned every non-ASCII-named row into every candidate set,
test_projection.cpp:1034: // so at any scale whose Unicode population exceeds the caller's cap every
test_projection.cpp:1035: // lookup returned nullopt and no query class could ever use the index. The
test_projection.cpp:1036: // posting intersection serves pure-ASCII needles (superset guarantee), merged
test_projection.cpp:1037: // only with the FOLD-EXCEPTIONAL rows (names that could match an ASCII needle
test_projection.cpp:1038: // through a non-ASCII unit under the host's CompareStringOrdinal ordinal
test_projection.cpp:1039: // table, plus every surrogate-bearing name). The pre-review intersection-only
[80 matches limit reached. Use limit=160 for more, or refine pattern]
grep: C:/Users/Michael Roy/Documents/FastFiles/tests/indexstore...
test_projection.cpp:25: void TestSiblingsDoNotDuplicateSharedAncestorPath() {
test_projection.cpp:41: void TestRepeatedNamesAcrossTreeShareOneInternedId() {
test_projection.cpp:59: void TestPathIsReconstructedOnDemand() {
test_projection.cpp:71: void TestChildrenLookupSupportsDirectoryListing() {
test_projection.cpp:82: void TestReparentingUpdatesParentChildIndex() {
test_projection.cpp:97: void TestPartialUpsertPreservesAbsentMetadata() {
test_projection.cpp:125: void TestAllocatedSizeFullPartialPreserveAndReconcile() {
test_projection.cpp:185: void TestFolderAggregateWithAllocatedSizeAndCompleteness() {
test_projection.cpp:222: void TestFolderAggregateOverflowSaturatesAndMarksIncomplete() {
test_projection.cpp:243: void TestRemoveDropsEntryButLeavesSiblingsIntact() {
test_projection.cpp:255: void TestCycleDoesNotCauseInfiniteWalk() {
test_projection.cpp:266: void TestVolumesAreIsolatedFromEachOther() {
test_projection.cpp:279: void TestRebuildFromStoreMatchesPersistedData() {
test_projection.cpp:306: void TestFolderAggregateCountsSubtree() {
test_projection.cpp:335: void TestManyDistinctParentsDoNotStallGrowth() {
test_projection.cpp:359: void TestFilenameTrigramCandidatesTrackMutations() {
test_projection.cpp:389: void TestProjectionEvictsLruAndReloadsFromStore() {
test_projection.cpp:445: void TestProjectionRebuildFailureIsColdAndRetryable() {
test_projection.cpp:489: void TestProjectionMemoryPressureSignal() {
test_projection.cpp:500: void TestProjectionMemoryDiagnostics() {
test_projection.cpp:568: void TestProjectionMemoryLimitRunsAtBatchBoundary() {
test_projection.cpp:587: void TestNamePoolViewInvalidatesOnArenaGrowth() {
test_projection.cpp:626: void TestNamePoolViewInvalidatesOnCompactStorage() {
test_projection.cpp:662: void TestProjectionMemoryBreakdownMatchesEstimatorAndCategories() {
test_projection.cpp:738: void TestDenseLruTimestampsTrackAccessOrder() {
test_projection.cpp:779: void TestRebuildReleasesBulkCapacitySlack() {
test_projection.cpp:859: void TestLruTimestampsSurviveIterationOrderCompaction() {
test_projection.cpp:954: void TestRemoveBatchMatchesPerEntryRemoval() {
test_projection.cpp:996: void TestRemoveVolumeKeepsFilenameAcceleratorCurrent() {
test_projection.cpp:1015: void TestRemoveVolumeCanLeaveAcceleratorForCallerRebuild() {
test_projection.cpp:1076: void TestFilenameCandidatesAreConservativeSuperset() {
test_projection.cpp:1116: void TestFilenameCandidatesNarrowForSelectiveNeedlesDespiteUnicodeRows() {
test_projection.cpp:1164: void TestHostOrdinalFoldSemanticsMatchCampaignMeasurement() {
test_projection.cpp:1188: void TestFilenameCandidatesRetainHostFoldExceptionalNames() {
test_projection.cpp:1227: void TestFilenameCandidatesRetainSurrogateNamesConservatively() {
test_projection.cpp:1257: void TestFilenameCandidateSupersetInvariantHoldsForAdversarialCasing() {
test_projection.cpp:1334: void TestBulkScanCycleRevivesFilenameAccelerator() {
test_projection.cpp:1355: void TestCursorDetectsDisturbedLayout() {
test_projection.cpp:1375: void TestOversizedVolumeReloadFailsFastAndStaysCold() {
test_projection.cpp:1402: int main() {
test_projection.cpp:1403:     TestSiblingsDoNotDuplicateSharedAncestorPath();
test_projection.cpp:1404:     TestRepeatedNamesAcrossTreeShareOneInternedId();
test_projection.cpp:1405:     TestPathIsReconstructedOnDemand();
test_projection.cpp:1406:     TestChildrenLookupSupportsDirectoryListing();
test_projection.cpp:1407:     TestReparentingUpdatesParentChildIndex();
test_projection.cpp:1408:     TestPartialUpsertPreservesAbsentMetadata();
test_projection.cpp:1409:     TestAllocatedSizeFullPartialPreserveAndReconcile();
test_projection.cpp:1410:     TestFolderAggregateWithAllocatedSizeAndCompleteness();
test_projection.cpp:1411:     TestFolderAggregateOverflowSaturatesAndMarksIncomplete();
test_projection.cpp:1412:     TestRemoveDropsEntryButLeavesSiblingsIntact();
test_projection.cpp:1413:     TestCycleDoesNotCauseInfiniteWalk();
test_projection.cpp:1414:     TestVolumesAreIsolatedFromEachOther();
test_projection.cpp:1415:     TestRebuildFromStoreMatchesPersistedData();
test_projection.cpp:1416:     TestFolderAggregateCountsSubtree();
test_projection.cpp:1417:     TestManyDistinctParentsDoNotStallGrowth();
test_projection.cpp:1418:     TestFilenameTrigramCandidatesTrackMutations();
test_projection.cpp:1419:     TestProjectionEvictsLruAndReloadsFromStore();
test_projection.cpp:1420:     TestProjectionRebuildFailureIsColdAndRetryable();
test_projection.cpp:1421:     TestProjectionMemoryPressureSignal();
test_projection.cpp:1422:     TestProjectionMemoryDiagnostics();
[60 matches limit reached. Use limit=120 for more, or refine pattern]
read: C:/Users/Michael Roy/Documents/FastFiles/tests/indexstore...
// large-index-lifecycle-and-search-scalability 6.2/6.3 + review-remediation
// (P2-A): filename-accelerator planner. The historical accelerator
// unconditionally unioned every non-ASCII-named row into every candidate set,
// so at any scale whose Unicode population exceeds the caller's cap every
// lookup returned nullopt and no query class could ever use the index. The
// posting intersection serves pure-ASCII needles (superset guarantee), merged
// only with the FOLD-EXCEPTIONAL rows (names that could match an ASCII needle
// through a non-ASCII unit under the host's CompareStringOrdinal ordinal
// table, plus every surrogate-bearing name). The pre-review intersection-only
// variant dropped that merge and relied on an unproven assumption that the
// host ordinal table has no cross-ASCII fold classes; the tests below pin the
// invariant against the REAL comparison function and exercise the fallback
// both through the host-derived probe and the testing seam.
std::u16string FoldAsciiLower(const std::u16string_view value) {
    std::u16string folded;
    folded.reserve(value.size());
    for (const char16_t ch : value) {
        folded.push_back(ch >= u'A' && ch <= u'Z' ? static_cast<char16_t>(ch + (u'a' - u'A')) : ch);
    }
    return folded;
}
bool ContainsFolded(const std::u16string& haystackFolded, const std::u16string& needleFolded) {
    return haystackFolded.find(needleFolded) != std::u16string::npos;
}
void PopulateUnicodeHeavyFixture(Projection& proj) {
    proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
    proj.Upsert(1, MakeEntry(100, 5, u"ascii_dir", 0x10));
    proj.Upsert(1, MakeEntry(101, 5, u"\u0434\u0438\u0440", 0x10));
    proj.Upsert(1, MakeEntry(1000, 100, u"unique_target.txt", 0));
    proj.Upsert(1, MakeEntry(1001, 101, u"\u0441\u043f\u0438\u0441\u043e\u043a_unique_target.txt", 0));
    proj.Upsert(1, MakeEntry(1002, 101, u"\u0424\u0430\u0439\u043b_UNIQUE_TARGET.txt", 0));
    proj.Upsert(1, MakeEntry(1003, 100, u"common_component_1.txt", 0));
    proj.Upsert(1, MakeEntry(1004, 100, u"common_component_2.txt", 0));
    proj.Upsert(1, MakeEntry(1005, 100, u"notes.md", 0));
    proj.Upsert(1, MakeEntry(1006, 100, u"dotted.name.with.dots.txt", 0));
    proj.Upsert(1, MakeEntry(1007, 100, u"ab", 0));
    proj.Upsert(1, MakeEntry(1008, 101, u"\u65e5\u672c\u8a9e.txt", 0));
    for (uint64_t i = 0; i < 20; ++i) {
        proj.Upsert(1, MakeEntry(2000 + i, 101, u"\u0441\u043f\u0438\u0441\u043e\u043a_" + NumberToUtf16(i) + u".txt", 0));
    }
}
void TestFilenameCandidatesAreConservativeSuperset() {
    Projection proj;
    PopulateUnicodeHeavyFixture(proj);
    const std::u16string needles[] = {
        u"unique_target", u"UNIQUE_TARGET", u"common_comp", u"zzz_missing",
        u"notes", u"dots.txt", u"LIST"  // "\u0424\u0430\u0439\u043b_UNIQUE_TARGET.txt" folds to contain "_unique_target" only;
    };
    for (const std::u16string& raw : needles) {
        const std::u16string folded = FoldAsciiLower(raw);
        const auto candidates = proj.FilenameCandidateIndices(folded, 0);  // uncapped reference
        if (!candidates.has_value()) {
            Check(false, "an uncapped pure-ASCII needle always returns a candidate set");
            continue;
        }
        std::vector<std::pair<int64_t, uint64_t>> expected;
        proj.ForEachEntry([&](const EntryKey& key, const ProjectionEntry& entry) {
            if (ContainsFolded(FoldAsciiLower(std::u16string(proj.Names().Get(entry.nameId))), folded)) {
                expected.push_back({key.volumeRowId, key.frn.low});
            }
        });
        std::vector<std::pair<int64_t, uint64_t>> got;
        for (const uint32_t slot : *candidates) {
            const ProjectionEntry& entry = proj.EntryAt(slot);
            got.push_back({entry.volumeRowId, entry.frn.low});
        }
        bool superset = true;
        for (const auto& id : expected) {
            if (std::find(got.begin(), got.end(), id) == got.end()) {
                superset = false;
            }
        }
        Check(superset, "accelerator candidates are a conservative superset of every fold-matching row");
    }
    // Structural eligibility rules are unchanged.
    Check(!proj.FilenameCandidateIndices(u"ab", 0).has_value(),
          "needles shorter than three folded units remain ineligible");
    Check(!proj.FilenameCandidateIndices(u"\u0441\u043f\u0438\u0441\u043e\u043a", 0).has_value(),
          "non-ASCII folded needles remain ineligible for the ASCII trigram accelerator");
}
void TestFilenameCandidatesNarrowForSelectiveNeedlesDespiteUnicodeRows() {
    Projection proj;
    PopulateUnicodeHeavyFixture(proj);
    // A deliberately small cap: non-ASCII rows that cannot match a pure-ASCII
    // needle must not consume the cap and force the canonical scan.
    const auto candidates = proj.FilenameCandidateIndices(u"unique_target", 8);
    Check(candidates.has_value(),
          "a selective pure-ASCII needle yields candidates regardless of the Unicode row count");
    if (candidates.has_value()) {
        Check(candidates->size() == 3,
              "the candidate set is exactly the three fold-matching rows");
    }
    const auto missing = proj.FilenameCandidateIndices(u"zzz_missing", 8);
    Check(missing.has_value() && missing->empty(),
          "a no-match pure-ASCII needle yields an empty candidate set, not every Unicode row");
}
// review-remediation (P2-A): the exact comparison shape of the canonical
// matcher (ffsearch::OrdinalContains: equal-length spans through
// CompareStringOrdinal(..., TRUE)). The candidate-superset invariant is
// stated against this function -- never against an assumed Unicode casefold.
bool CanonicalOrdinalContains(const std::u16string& name, const std::u16string& needle) {
    if (needle.empty()) return true;
    if (name.size() < needle.size()) return false;
    for (size_t i = 0; i + needle.size() <= name.size(); ++i) {
        if (CompareStringOrdinal(reinterpret_cast<const wchar_t*>(name.data() + i),
                                 static_cast<int>(needle.size()),
                                 reinterpret_cast<const wchar_t*>(needle.data()),
                                 static_cast<int>(needle.size()), TRUE) == CSTR_EQUAL) {
            return true;
        }
    }
    return false;
}
bool OrdinalUnitEqual(const char16_t a, const char16_t b) {
    return CompareStringOrdinal(reinterpret_cast<const wchar_t*>(&a), 1,
                                reinterpret_cast<const wchar_t*>(&b), 1, TRUE) == CSTR_EQUAL;
}
// review-remediation (P2-A): campaign-host characterization of the Windows
// ordinal case table. The PR review's example assumed Unicode casefold
// semantics (U+017F LONG S uppercases to 'S' in Unicode's table); the REAL
// CompareStringOrdinal verdicts on the campaign host are NOT that. If a
// future Windows ordinal table changes any verdict here this test fails
// loudly and the review-remediation evidence must be re-audited. The product
// stays correct either way: the fold-exceptional set is derived from the
// live comparison function at run time (UnitMayFoldToAscii).
void TestHostOrdinalFoldSemanticsMatchCampaignMeasurement() {
    Check(OrdinalUnitEqual(u'A', u'a'),
          "ASCII sanity: ordinal ignore-case folds A/a");
    Check(OrdinalUnitEqual(u'\u00C9', u'\u00E9'),
          "ordinal ignore-case DOES fold non-ASCII case pairs (E-acute): folding is not ASCII-only");
    Check(!OrdinalUnitEqual(u'\u017F', u's') && !OrdinalUnitEqual(u'\u017F', u'S'),
          "campaign host: LONG S is NOT equal to s/S under ordinal ignore-case (review example disproven here)");
    Check(!OrdinalUnitEqual(u'\u212A', u'k') && !OrdinalUnitEqual(u'\u212A', u'K'),
          "campaign host: KELVIN SIGN is NOT equal to k/K under ordinal ignore-case");
    Check(!OrdinalUnitEqual(u'\u0131', u'i') && !OrdinalUnitEqual(u'\u0131', u'I'),
          "campaign host: dotless i is NOT equal to i/I under ordinal ignore-case");
    Check(!OrdinalUnitEqual(u'\u0130', u'i') && !OrdinalUnitEqual(u'\u0130', u'I'),
          "campaign host: dotted I is NOT equal to i/I under ordinal ignore-case");
    Check(!OrdinalUnitEqual(u'\u00DF', u's'),
          "campaign host: sharp s is NOT equal to s under ordinal ignore-case");
    Check(!OrdinalUnitEqual(u'\uFB01', u'f'),
          "campaign host: fi ligature is NOT equal to f under ordinal ignore-case");
}
// review-remediation (P2-A), RED-FIRST: force the host table to exhibit the
// exact equivalence the PR review assumed (U+017F LONG S folds to ASCII 'S').
// The pre-fix intersection-only accelerator omits the long-s row for the
// ASCII needle "soo" (its ASCII fold cannot contain the 'soo' trigram) --
// a data-retrieval false negative. The fold-exceptional merge must retain it.
void TestFilenameCandidatesRetainHostFoldExceptionalNames() {
    Projection::SetTestingFoldExceptionalUnit(u'\u017F', true);
    {
        Projection proj;
        proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
        proj.Upsert(1, MakeEntry(100, 5, u"\u017Foo.txt", 0));
        proj.Upsert(1, MakeEntry(101, 5, u"control.txt", 0));
        proj.Upsert(1, MakeEntry(102, 5, u"soo.txt", 0));
        const auto candidates = proj.FilenameCandidateIndices(u"soo");
        Check(candidates.has_value(), "eligible ASCII query still uses the accelerator");
        if (candidates.has_value()) {
            Check(CandidateContains(proj, *candidates, 100),
                  "a host-fold-exceptional long-s name is retained for the ASCII needle (no false negative)");
            Check(CandidateContains(proj, *candidates, 102),
                  "the ordinary matching row is retained");
            Check(!CandidateContains(proj, *candidates, 101),
                  "the non-matching non-exceptional row is still excluded");
        }
        // Renaming away the exceptional unit drops the row's exceptionality.
        proj.Upsert(1, MakeEntry(100, 5, u"foo.txt", 0));
        const auto afterRename = proj.FilenameCandidateIndices(u"soo");
        Check(afterRename.has_value() && !CandidateContains(proj, *afterRename, 100),
              "renaming away the exceptional unit removes the row from the exceptional merge");
        // Deletion removes rows from the exceptional merge exactly like postings.
        proj.Remove(1, FileId{102, 0});
        const auto afterRemove = proj.FilenameCandidateIndices(u"soo");
        Check(afterRemove.has_value() && !CandidateContains(proj, *afterRemove, 102),
              "deleting a row removes it from candidates");
    }
    Projection::SetTestingFoldExceptionalUnit(u'\u017F', false);
}
// review-remediation (P2-A): surrogate-bearing (astral) names are always
// fold-exceptional (conservative: pair semantics cannot be probed unit-wise
// and false negatives are forbidden), so they are retained in every ASCII
// candidate set even when their ASCII fold cannot contain the needle. RED
// against the pre-fix intersection-only accelerator: for a needle absent
// from every name its candidate set is empty, so the conservative row was
// silently dropped.
void TestFilenameCandidatesRetainSurrogateNamesConservatively() {
    Projection proj;
    proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
    std::u16string emojiName;
    emojiName.push_back(static_cast<char16_t>(0xD83D)); // U+1F600 pair
    emojiName.push_back(static_cast<char16_t>(0xDE00));
    emojiName += u"qz.txt";
    proj.Upsert(1, MakeEntry(100, 5, emojiName, 0));
    proj.Upsert(1, MakeEntry(101, 5, u"qz.txt", 0));
    const auto candidates = proj.FilenameCandidateIndices(u"xyz");
    Check(candidates.has_value(), "eligible ASCII query returns a candidate set");
    if (candidates.has_value()) {
        Check(CandidateContains(proj, *candidates, 100),
              "surrogate-bearing names are conservatively retained in every ASCII candidate set");
        Check(!CandidateContains(proj, *candidates, 101),
              "non-exceptional rows are still served by the posting intersection alone");
    }
}
// review-remediation (P2-A): the correctness invariant, stated against the
// real production comparison function: CanonicalMatches(query) is a subset of
// FilenameCandidateIndices(query). Adversarial cross-casing corpus: long s,
// Kelvin, dotted/dotless I, sharp s, ligatures, fullwidth, micro, Greek and
// Cyrillic lookalikes, combining forms, surrogate pairs, mixed
// ASCII/non-ASCII names, repeated trigrams, query at start/middle/end, and
// mixed-case queries. On the campaign host most of these pairs are NOT
// canonical matches (TestHostOrdinalFoldSemanticsMatchCampaignMeasurement
// records the verdicts); the subset check is what must hold on EVERY host --
// on a host exhibiting the review's equivalence it fails against the pre-fix
// intersection-only accelerator (host-conditional red-first).
void TestFilenameCandidateSupersetInvariantHoldsForAdversarialCasing() {
    struct Case { std::u16string name; std::u16string needle; };
    const Case cases[] = {
        { u"\u017Foo.txt", u"soo" },
        { u"\u017Foo.txt", u"SOO" },
        { u"\u212Aelvin.bin", u"kelvin" },
        { u"\u212Aelvin.bin", u"KELVIN" },
        { u"cap\u0131tal.txt", u"capital" },
        { u"CAP\u0130TAL.txt", u"capital" },
        { u"CAP\u0130TAL.txt", u"CAPITAL" },
        { u"stra\u00DFe.txt", u"strasse" },
        { u"stra\u00DFe.txt", u"strase" },
        { u"\uFB01le.txt", u"file" },
        { u"\uFB03eld.dat", u"ffield" },
        { u"\uFF21bc.txt", u"abc" },
        { u"\u00B5icro.txt", u"micro" },
        { u"\u03C2igma.txt", u"sigma" },
        { u"\u0405lavic.txt", u"slavic" },
        { u"\u0399ota.txt", u"iota" },
        { u"foo\u017Fbar.txt", u"bar" },
        { u"foo\u017Fbar.txt", u"FOO" },
        { u"MiXeD_CaSe.bin", u"mIxEd_cAsE" },
        { u"Report.TXT", u"report.txt" },
        { u"report.txt", u"PORT.t" },
        { u"abcabcabc.txt", u"abcabc" },
        { u"o\u0308verlay.txt", u"verlay" },
        { u"\u6771\u4EAC_target.txt", u"target" },
        { u"plain.txt", u"plain" },
    };
    Projection proj;
    proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
    uint64_t id = 100;
    for (const Case& c : cases) {
        proj.Upsert(1, MakeEntry(id++, 5, c.name, 0));
    }
    // One surrogate-pair row built explicitly (universal-character-names
    // cannot name surrogates).
    std::u16string emojiName;
    emojiName.push_back(static_cast<char16_t>(0xD801));
    emojiName.push_back(static_cast<char16_t>(0xDC00));
    emojiName += u"_target.txt";
    proj.Upsert(1, MakeEntry(id++, 5, emojiName, 0));
    uint64_t checkId = 100;
    for (const Case& c : cases) {
        const std::u16string folded = FoldAsciiLower(c.needle);
        const auto candidates = proj.FilenameCandidateIndices(folded, 0);
        Check(candidates.has_value(), "an eligible ASCII needle always yields an uncapped candidate set");
        if (candidates.has_value() && CanonicalOrdinalContains(c.name, c.needle)) {
            Check(CandidateContains(proj, *candidates, checkId),
                  "a canonical match is always present in the accelerator candidate set");
        }
        // Capped lookups may fall back to nullopt (canonical scan) but may
        // never return a set that omits a canonical match.
        const auto capped = proj.FilenameCandidateIndices(folded, 4096);
        if (capped.has_value() && CanonicalOrdinalContains(c.name, c.needle)) {
            Check(CandidateContains(proj, *capped, checkId),
                  "a capped candidate set still contains every canonical match");
        }
        ++checkId;
    }
    // Surrogate-pair row against an ASCII needle that matches its ASCII tail.
    {
        const std::u16string needle = u"target";
        Check(CanonicalOrdinalContains(emojiName, needle),
              "the surrogate-pair fixture canonically matches its ASCII tail");
        const auto candidates = proj.FilenameCandidateIndices(FoldAsciiLower(needle), 0);
        Check(candidates.has_value() && CandidateContains(proj, *candidates, id - 1),
              "surrogate-pair rows are retained when their ASCII tail canonically matches");
    }
}
} // namespace
// large-index-lifecycle-and-search-scalability review-lane C Finding 3:
// ending a bulk scan must revive the filename accelerator (it previously
// stayed dead after every scan, silently disabling selective acceleration).
void TestBulkScanCycleRevivesFilenameAccelerator() {
    Projection proj;
[114 more lines in file. Use offset=1336 to continue.]
grep: C:/Users/Michael Roy/Documents/FastFiles/tests
read: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/s...
indexstore/test_store_read_session.cpp:236: void TestAsciiNamePrefilterIsConservativeAndPrecise() {
indexstore/test_store_read_session.cpp:271:         const bool streamed = reader->ForEachEntryWithAsciiNameUntil(*volume, needle,
indexstore/test_store_read_session.cpp:329:     TestAsciiNamePrefilterIsConservativeAndPrecise();
setup/test_package_trust_wintrust_integration.cpp:161:     for (const auto& name : names) WritePayload(Join(timestampRoot, name), "native-" + AsciiName(name));
setup/test_package_trust.cpp:43:     for (const auto& name : names) WritePayload(Join(root, name), "package-payload-" + AsciiName(name));
setup/test_package_trust.cpp:191:     for (const auto& name : names) WritePayload(Join(authRoot, name), "auth-" + AsciiName(name));
setup/test_package_trust.cpp:305:     for (const auto& name : names) WritePayload(Join(candidateRoot, name), "rollback-" + AsciiName(name));
setup/PackageTrustFixtureSupport.h:40: std::string AsciiName(const std::wstring& value) {
        if (transactionActive_) {
            ExecSql(db_, "ROLLBACK;");
        }
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
    transactionActive_ = false;
    dbPathUtf8_.clear();
}
bool StoreReadSession::ForEachEntryUntil(VolumeRowId volumeRowId,
                                         const Store::EntryVisitorUntil& visitor) {
    if (!IsOpen() || !visitor) {
        if (!IsOpen()) failed_ = true;
        return false;
    }
    if (ShouldFail(ReadSessionFaultPoint::DatabaseClosed)) {
        failed_ = true;
        Close();
        return false;
    }
    Statement stmt(db_,
        "SELECT frn_low, frn_high, parent_frn_low, parent_frn_high, name, size_bytes, allocated_size_bytes, "
        "creation_time, last_modified_time, last_access_time, attributes FROM entries WHERE volume_id=?;");
    if (!stmt.Valid()) {
        failed_ = true;
        return false;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(volumeRowId));
    int stepResult;
    while ((stepResult = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (ShouldFail(ReadSessionFaultPoint::Stream)) {
            failed_ = true;
            return false;
        }
        if (!visitor(ReadEntryRecord(stmt))) return true;
    }
    if (stepResult != SQLITE_DONE) failed_ = true;
    return stepResult == SQLITE_DONE;
}
namespace {
// large-index-lifecycle-and-search-scalability review-remediation 18.x:
// LIKE pattern for a literal needle: wraps in wildcards and escapes the LIKE
// metacharacters so a needle like "a\b" or "100%" can only ever OVER-match
// (conservative), never lose a row the needle literally appears in.
std::string LikePatternForLiteralNeedle(const std::string_view needle) {
    std::string pattern;
    pattern.reserve(needle.size() + 2 + needle.size() / 4);
    pattern.push_back('%');
    for (const char ch : needle) {
        if (ch == '\\' || ch == '%' || ch == '_') pattern.push_back('\\');
        pattern.push_back(ch);
    }
    pattern.push_back('%');
    return pattern;
}
} // namespace
bool StoreReadSession::ForEachEntryWithAsciiNameUntil(
    const VolumeRowId volumeRowId, const std::string_view asciiFoldedNeedle,
    const Store::EntryVisitorUntil& visitor) {
    // Conservative-superset prefilter; see the header contract. The needle
    // must be ASCII (the caller gates on it): SQLite LIKE folds ASCII case
    // only, which is exactly the fold the superset proof needs, and the GLOB
    // disjunct admits every name carrying non-ASCII bytes (whose canonical
    // match could pass through a non-ASCII unit under any host ordinal table).
    if (!IsOpen() || !visitor) {
        if (!IsOpen()) failed_ = true;
        return false;
    }
    if (ShouldFail(ReadSessionFaultPoint::DatabaseClosed)) {
        failed_ = true;
        Close();
        return false;
    }
    Statement stmt(db_,
        "SELECT frn_low, frn_high, parent_frn_low, parent_frn_high, name, size_bytes, allocated_size_bytes, "
        "creation_time, last_modified_time, last_access_time, attributes FROM entries WHERE volume_id=? "
        "AND (name LIKE ? ESCAPE '\\' OR name GLOB '*[^ -~]*');");
    if (!stmt.Valid()) {
        failed_ = true;
        return false;
    }
    const std::string pattern = LikePatternForLiteralNeedle(asciiFoldedNeedle);
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(volumeRowId));
    sqlite3_bind_text(stmt, 2, pattern.data(), static_cast<int>(pattern.size()), SQLITE_TRANSIENT);
    int stepResult;
    while ((stepResult = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (ShouldFail(ReadSessionFaultPoint::Stream)) {
            failed_ = true;
            return false;
        }
        if (!visitor(ReadEntryRecord(stmt))) return true;
    }
    if (stepResult != SQLITE_DONE) failed_ = true;
    return stepResult == SQLITE_DONE;
}
std::optional<EntryRecord> StoreReadSession::FindEntry(VolumeRowId volumeRowId, FileId frn) {
    if (!IsOpen()) {
        failed_ = true;
        return std::nullopt;
    }
    if (ShouldFail(ReadSessionFaultPoint::FindEntry)) {
        failed_ = true;
        return std::nullopt;
    }
    Statement stmt(db_,
        "SELECT frn_low, frn_high, parent_frn_low, parent_frn_high, name, size_bytes, allocated_size_bytes, "
        "creation_time, last_modified_time, last_access_time, attributes "
        "FROM entries WHERE volume_id=? AND frn_low=? AND frn_high=?;");
    if (!stmt.Valid()) {
        failed_ = true;
        return std::nullopt;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(volumeRowId));
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(frn.low));
[28 more lines in file. Use offset=1270 to continue.]
read: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/i...
grep: C:/Users/Michael Roy/Documents/FastFiles/src/search/src/Q...
[pi-lens: read widened to the enclosing function_definition "IntegrityVerdict" (symbol boundary): you asked for lines 215-294, this shows lines 107-284. Re-request with limit > 100 for the exact range.]
    enum class IntegrityVerdict { Ok, Corrupt, Inconclusive };
    IntegrityVerdict RunIntegrityCheckDetailed();
    // Classification of a non-row sqlite3_step result code from the
    // integrity_check statement. Exposed (and unit-testable) so the
    // corrupt-vs-inconclusive boundary is a testable pure rule rather than
    // an untestable control-flow accident. Corrupt-family primary result
    // codes (SQLITE_CORRUPT=11, SQLITE_NOTADB=26, masking extended codes)
    // classify as Corrupt; *everything* else classifies as Inconclusive --
    // fail-safe in the direction that never deletes data.
    static IntegrityVerdict ClassifyIntegrityStep(int stepCode) noexcept;
    // task 1.6: passive checkpoint, cheap and safe to call frequently
    // (e.g. on an idle timer); does not block writers.
    bool CheckpointPassive();
    // Forces a full checkpoint if the WAL file has grown past
    // thresholdBytes -- the size-triggered forced checkpoint mitigation
    // for unbounded WAL growth under sustained write bursts (design.md
    // "Risks"). Returns false only on an I/O/database error; a WAL file
    // under the threshold is a normal, successful no-op.
    bool CheckpointIfWalExceeds(uint64_t thresholdBytes);
    // address-engineering-audit-findings 5.1: refresh SQLite query-planner
    // statistics after a sufficiently large ingestion batch.
    bool Analyze();
    // address-engineering-audit-findings 5.2: run VACUUM only during an
    // idle window after the configured interval and size-growth gates pass.
    // nowEpochSeconds is injectable so the gating policy is deterministic in
    // tests; the production caller passes current wall-clock seconds.
    bool VacuumIfDue(bool userIdle, uint64_t nowEpochSeconds, uint64_t intervalHours,
                     uint64_t sizeGrowthPercent);
    // --- Volume metadata (tasks.md 1.3, section 7/8's persistence needs) ---
    // Looks up the volume by its durable key, creating a new row (with
    // available=true, no journal/scan state) if this is the first time
    // it's been seen. The returned VolumeRowId is stable for the life of
    // the row (see Identity.h).
    std::optional<VolumeRowId> GetOrCreateVolume(const VolumeKey& key, bool* outCreated = nullptr);
    std::optional<VolumeMetadata> GetVolumeMetadata(VolumeRowId volumeRowId);
    std::vector<VolumeMetadata> GetAllVolumes();
    // Indexed search needs volume identity/availability only; this avoids
    // counting every entry when the caller does not need entryCount.
    std::vector<VolumeMetadata> GetAllVolumesWithoutEntryCounts();
    // tasks.md 7.2/7.3: mark availability without touching entries.
    bool SetVolumeAvailable(VolumeRowId volumeRowId, bool available, uint64_t nowTimestamp,
                            bool* outVisibilityChanged = nullptr);
    // tasks.md 7.5/7.6: persisted journal identity/position, compared
    // against what the service reports on reconnect (design.md D6).
    bool SetJournalPosition(VolumeRowId volumeRowId, uint64_t journalId, uint64_t resumeUsn);
    // tasks.md 6.3/6.4/D8: resumable scan cursor, cleared once the initial
    // scan finishes.
    bool SetScanCursor(VolumeRowId volumeRowId, const std::vector<uint8_t>& cursor);
    bool MarkScanComplete(VolumeRowId volumeRowId);
    bool SetLastReconciliationTime(VolumeRowId volumeRowId, uint64_t nowTimestamp);
    // tasks.md 7.4: explicit, separate user-triggered permanent removal --
    // distinct from the automatic disconnect handling, which never deletes.
    // Refuses missing or currently-available volumes.
    bool ForgetVolume(VolumeRowId volumeRowId);
    // --- Entries (tasks.md 1.2, 1.5) ---
    // Applies the whole batch as one explicit transaction (task 1.5):
    // Upsert changes are written via INSERT ... ON CONFLICT DO UPDATE
    // keyed on (volume, FRN) (spec "Re-ingesting the same physical file
    // updates one row, not two"); Remove changes delete that row. Returns
    // false (and leaves the database unchanged, courtesy of the rolled-
    // back transaction) on any failure, so the caller can retry the same
    // batch (task 3.4) without risking a partial commit.
    bool ApplyBatch(VolumeRowId volumeRowId, const std::vector<EntryChange>& changes);
    // task 3.1: streams every entry belonging to a volume, in an
    // unspecified but stable-for-the-duration-of-the-call order, without
    // materializing the whole result set at once.
    using EntryVisitor = std::function<void(const EntryRecord&)>;
    bool ForEachEntry(VolumeRowId volumeRowId, const EntryVisitor& visitor);
    // memory-bounded-indexed-scale 2.6: cancellable streaming variant. The
    // visitor returns false to stop early (true to continue); the call returns
    // false only on a database error -- an early visitor stop is a normal,
    // successful bounded read. Backs cold indexed-search enumeration so a
    // superseded/cancelled query stops SQLite work immediately.
    using EntryVisitorUntil = std::function<bool(const EntryRecord&)>;
    bool ForEachEntryUntil(VolumeRowId volumeRowId, const EntryVisitorUntil& visitor);
    // memory-bounded-indexed-scale 2.1: durable point lookup of one entry by
    // (volume, FileReferenceNumber). std::nullopt when the row is absent or
    // the store is closed. Backs cold candidate parent-chain reconstruction so
    // a matched row inside an evicted subtree can resolve its folder with
    // O(depth) bounded reads instead of reloading the volume.
    std::optional<EntryRecord> FindEntry(VolumeRowId volumeRowId, FileId frn);
    // task 8.1/8.4: full identity set for a volume, used by reconciliation
    // to anti-join against fresh ground truth without re-streaming every
    // field for entries that haven't changed.
    std::vector<FileId> ListEntryIds(VolumeRowId volumeRowId);
    uint64_t CountEntries(VolumeRowId volumeRowId);
    // file-preview-and-properties §6.2 / storage-analysis §3.1: recursive
    // subtree aggregate from the durable store. Returns {itemCount,
    // totalSizeBytes, ...} for all descendants of `parentFrn` within
    // `volumeRowId`, or {0, 0, ...} if the root is unknown. Uses a recursive
    // CTE so this works without rebuilding the projection.
    // allocated-size-storage-accuracy D7: the allocated-size subtotal covers
    // only descendants whose allocated size is known; the completeness pair
    // (known count / total count) is the explicit state -- `Complete` only
    // when every descendant is known, never a silently partial total.
    struct FolderAggregate {
        uint64_t itemCount = 0;
        uint64_t totalSizeBytes = 0;
        uint64_t totalAllocatedSizeBytes = 0;
        uint64_t allocatedBytesKnownCount = 0;
        uint64_t allocatedBytesTotalCount = 0;
    };
    FolderAggregate GetFolderAggregate(VolumeRowId volumeRowId, FileId parentFrn);
private:
    friend class StoreReadSession;
    std::vector<VolumeMetadata> GetAllVolumesImpl(bool includeEntryCounts);
    sqlite3* db_ = nullptr;
    std::string dbPathUtf8_;
    uint64_t lastVacuumTimeSeconds_ = 0;
    uint64_t lastVacuumSizeBytes_ = 0;
    mutable int cachedPageSize_ = 0;
};
// A durable read snapshot owned by one query. Store itself deliberately stays
// unsynchronized and connection-owned; callers never invoke its writer
// connection concurrently from a query worker. A session's transaction is
// closed deterministically by Close()/destruction, making reader resources
// reusable even when a visitor cancels or a SQLite step fails.
class StoreReadSession {
public:
    StoreReadSession() = delete;
    ~StoreReadSession();
    StoreReadSession(const StoreReadSession&) = delete;
    StoreReadSession& operator=(const StoreReadSession&) = delete;
    StoreReadSession(StoreReadSession&& other) noexcept;
    StoreReadSession& operator=(StoreReadSession&& other) noexcept;
    bool IsOpen() const noexcept { return db_ != nullptr && transactionActive_; }
    bool Failed() const noexcept { return failed_; }
    void Close() noexcept;
    bool ForEachEntryUntil(VolumeRowId volumeRowId, const Store::EntryVisitorUntil& visitor);
    // large-index-lifecycle-and-search-scalability review-remediation 18.x:
    // conservative SQL name prefilter for a pure-ASCII literal needle
    // (already ASCII-case-folded). The returned rows are a SUPERSET of every
    // row whose name could match the needle under the canonical ordinal
    // ignore-case matcher: an all-ASCII match span is found by SQLite's
    // ASCII-only case-insensitive LIKE, while a span containing any non-ASCII
    // UTF-16 unit implies the name carries non-ASCII UTF-8 bytes, which the
    // GLOB disjunct admits unconditionally. False positives reach the
    // canonical C++ matcher unchanged; false negatives are impossible for any
    // host ordinal table. Callers must pass ASCII needles only (non-ASCII
    // needles cannot be conservatively folded by SQLite LIKE).
    bool ForEachEntryWithAsciiNameUntil(VolumeRowId volumeRowId, std::string_view asciiFoldedNeedle,
                                        const Store::EntryVisitorUntil& visitor);
    std::optional<EntryRecord> FindEntry(VolumeRowId volumeRowId, FileId frn);
    Store::FolderAggregate GetFolderAggregate(VolumeRowId volumeRowId, FileId parentFrn);
private:
    friend class Store;
    StoreReadSession(std::string dbPathUtf8, StoreReadSessionOptions options);
    bool Open();
    bool ShouldFail(ReadSessionFaultPoint point) const;
    sqlite3* db_ = nullptr;
    std::string dbPathUtf8_;
    StoreReadSessionOptions options_;
    bool transactionActive_ = false;
    bool failed_ = false;
};
} // namespace ffindexstore
Query.cpp-566-     return true;
Query.cpp-567- }
Query.cpp-568- 
Query.cpp-569- bool MatchSimpleLiteral(const Query& query, const CandidateView& candidate) {
Query.cpp:570:     return OrdinalContains16Prepared(candidate.name, query.simpleLiteralTerm, query.simpleLiteralFolded,
Query.cpp-571-                                      query.simpleLiteralShifts, candidate.nameIsAscii,
Query.cpp-572-                                      query.simpleLiteralAnchor, query.simpleLiteralAnchorIndex);
Query.cpp-573- }
Query.cpp-574- 
Query.cpp-568- 
Query.cpp-569- bool MatchSimpleLiteral(const Query& query, const CandidateView& candidate) {
Query.cpp-570-     return OrdinalContains16Prepared(candidate.name, query.simpleLiteralTerm, query.simpleLiteralFolded,
Query.cpp-571-                                      query.simpleLiteralShifts, candidate.nameIsAscii,
Query.cpp:572:                                      query.simpleLiteralAnchor, query.simpleLiteralAnchorIndex);
Query.cpp-573- }
Query.cpp-574- 
Query.cpp-575- bool EvaluateSimpleLiteral(const Query& query, const CandidateView& candidate,
Query.cpp-576-                            MatchDiagnostics* diagnostics) {
Query.cpp-595- 
Query.cpp-596- } // namespace
Query.cpp-597- 
Query.cpp-598- bool Query::MatchesView(const CandidateView& candidate, MatchDiagnostics* diagnostics) const {
Query.cpp:599:     if (viewCompatible && simpleLiteralName) return EvaluateSimpleLiteral(*this, candidate, diagnostics);
Query.cpp-600-     return viewCompatible && EvaluateViewPredicates(viewPredicates, viewPredicateKinds, candidate, diagnostics);
Query.cpp-601- }
Query.cpp-602- bool Query::MatchesViewWithoutFolder(const CandidateView& candidate, MatchDiagnostics* diagnostics) const {
Query.cpp-603-     if (viewCompatible && simpleLiteralName) return EvaluateSimpleLiteral(*this, candidate, diagnostics);
Query.cpp-599-     if (viewCompatible && simpleLiteralName) return EvaluateSimpleLiteral(*this, candidate, diagnostics);
Query.cpp-600-     return viewCompatible && EvaluateViewPredicates(viewPredicates, viewPredicateKinds, candidate, diagnostics);
Query.cpp-601- }
Query.cpp-602- bool Query::MatchesViewWithoutFolder(const CandidateView& candidate, MatchDiagnostics* diagnostics) const {
Query.cpp:603:     if (viewCompatible && simpleLiteralName) return EvaluateSimpleLiteral(*this, candidate, diagnostics);
Query.cpp-604-     return viewCompatible && EvaluateViewPredicates(viewPredicatesWithoutFolder,
Query.cpp-605-                                                     viewPredicateWithoutFolderKinds, candidate, diagnostics);
Query.cpp-606- }
Query.cpp-607- bool OrdinalContains(std::wstring_view h, std::wstring_view n) { if (n.empty()) return true; for (size_t i = 0; i + n.size() <= h.size(); ++i) if (CompareStringOrdinal(h.data() + i, static_cast<int>(n.size()), n.data(), static_cast<int>(n.size()), TRUE) == CSTR_EQUAL) return true; return false; }
Query.cpp-678- }
Query.cpp-679- 
Query.cpp-680- Query ParseQuery(std::wstring_view text, const FilterRegistry& registry) {
Query.cpp-681-     Query query;
Query.cpp:682:     std::wstring simpleLiteralTerm;
Query.cpp-683-     const auto addPredicate = [&query](Predicate predicate, ViewPredicate viewPredicate, bool requiresFolder,
Query.cpp-684-                                        PredicateKind kind) {
Query.cpp-685-         query.predicates.push_back(predicate);
Query.cpp-686-         if (requiresFolder) {
Query.cpp-714-                         : normalizedKey == L"ondisk" ? PredicateKind::AllocatedSize
Query.cpp-715-                         : normalizedKey == L"modified" ? PredicateKind::Modified
Query.cpp-716-                         : normalizedKey == L"kind" ? PredicateKind::Kind
Query.cpp-717-                         : PredicateKind::Unknown;
Query.cpp:718:                     if (kind == PredicateKind::LiteralName) simpleLiteralTerm = std::get<std::wstring>(*parsed);
Query.cpp-719-                     const ViewPredicate viewPredicate = definition->makeViewPredicate
Query.cpp-720-                         ? definition->makeViewPredicate(*parsed) : ViewPredicate{};
Query.cpp-721-                     addPredicate(definition->makePredicate(*parsed), viewPredicate, requiresFolder, kind);
Query.cpp-722-                 }
Query.cpp-730-                          [token](const CandidateView& candidate) { return GlobMatches16(candidate.name, token); }, false,
Query.cpp-731-                          PredicateKind::GlobName);
Query.cpp-732-         } else {
Query.cpp-733-             if (query.primaryTerm.empty()) query.primaryTerm = token;
Query.cpp:734:             simpleLiteralTerm = token;
Query.cpp-735-             const auto foldedToken = IsAscii(token) ? FoldAscii16(token) : std::u16string{};
Query.cpp-736-             const auto foldedTokenShifts = BuildAsciiShifts(foldedToken);
Query.cpp-737-             const size_t foldedAnchorIndex = ChooseAsciiAnchor(foldedToken);
Query.cpp-738-             const char16_t foldedAnchor = foldedToken.empty() ? 0 : foldedToken[foldedAnchorIndex];
Query.cpp-746-         }
Query.cpp-747-     }
Query.cpp-748-     if (query.viewCompatible && !query.requiresFolder && query.predicates.size() == 1 &&
Query.cpp-749-         query.viewPredicateKinds.size() == 1 && query.viewPredicateKinds.front() == PredicateKind::LiteralName &&
Query.cpp:750:         !simpleLiteralTerm.empty()) {
Query.cpp-751-         query.simpleLiteralName = true;
Query.cpp-752-         query.simpleLiteralTerm = std::move(simpleLiteralTerm);
Query.cpp-753-         query.simpleLiteralFolded = IsAscii(query.simpleLiteralTerm)
Query.cpp-754-             ? FoldAscii16(query.simpleLiteralTerm) : std::u16string{};
Query.cpp-747-     }
Query.cpp-748-     if (query.viewCompatible && !query.requiresFolder && query.predicates.size() == 1 &&
Query.cpp-749-         query.viewPredicateKinds.size() == 1 && query.viewPredicateKinds.front() == PredicateKind::LiteralName &&
Query.cpp-750-         !simpleLiteralTerm.empty()) {
Query.cpp:751:         query.simpleLiteralName = true;
Query.cpp-752-         query.simpleLiteralTerm = std::move(simpleLiteralTerm);
Query.cpp-753-         query.simpleLiteralFolded = IsAscii(query.simpleLiteralTerm)
Query.cpp-754-             ? FoldAscii16(query.simpleLiteralTerm) : std::u16string{};
Query.cpp-755-         query.simpleLiteralAnchorIndex = ChooseAsciiAnchor(query.simpleLiteralFolded);
Query.cpp-748-     if (query.viewCompatible && !query.requiresFolder && query.predicates.size() == 1 &&
Query.cpp-749-         query.viewPredicateKinds.size() == 1 && query.viewPredicateKinds.front() == PredicateKind::LiteralName &&
Query.cpp-750-         !simpleLiteralTerm.empty()) {
Query.cpp-751-         query.simpleLiteralName = true;
Query.cpp:752:         query.simpleLiteralTerm = std::move(simpleLiteralTerm);
Query.cpp-753-         query.simpleLiteralFolded = IsAscii(query.simpleLiteralTerm)
Query.cpp-754-             ? FoldAscii16(query.simpleLiteralTerm) : std::u16string{};
Query.cpp-755-         query.simpleLiteralAnchorIndex = ChooseAsciiAnchor(query.simpleLiteralFolded);
Query.cpp-756-         query.simpleLiteralAnchor = query.simpleLiteralFolded.empty()
Query.cpp-749-         query.viewPredicateKinds.size() == 1 && query.viewPredicateKinds.front() == PredicateKind::LiteralName &&
Query.cpp-750-         !simpleLiteralTerm.empty()) {
Query.cpp-751-         query.simpleLiteralName = true;
Query.cpp-752-         query.simpleLiteralTerm = std::move(simpleLiteralTerm);
Query.cpp:753:         query.simpleLiteralFolded = IsAscii(query.simpleLiteralTerm)
Query.cpp-754-             ? FoldAscii16(query.simpleLiteralTerm) : std::u16string{};
Query.cpp-755-         query.simpleLiteralAnchorIndex = ChooseAsciiAnchor(query.simpleLiteralFolded);
Query.cpp-756-         query.simpleLiteralAnchor = query.simpleLiteralFolded.empty()
Query.cpp-757-             ? 0 : query.simpleLiteralFolded[query.simpleLiteralAnchorIndex];
Query.cpp-750-         !simpleLiteralTerm.empty()) {
Query.cpp-751-         query.simpleLiteralName = true;
Query.cpp-752-         query.simpleLiteralTerm = std::move(simpleLiteralTerm);
Query.cpp-753-         query.simpleLiteralFolded = IsAscii(query.simpleLiteralTerm)
Query.cpp:754:             ? FoldAscii16(query.simpleLiteralTerm) : std::u16string{};
Query.cpp-755-         query.simpleLiteralAnchorIndex = ChooseAsciiAnchor(query.simpleLiteralFolded);
Query.cpp-756-         query.simpleLiteralAnchor = query.simpleLiteralFolded.empty()
Query.cpp-757-             ? 0 : query.simpleLiteralFolded[query.simpleLiteralAnchorIndex];
Query.cpp-758-         query.simpleLiteralShifts = BuildAsciiShifts(query.simpleLiteralFolded);
Query.cpp-751-         query.simpleLiteralName = true;
Query.cpp-752-         query.simpleLiteralTerm = std::move(simpleLiteralTerm);
Query.cpp-753-         query.simpleLiteralFolded = IsAscii(query.simpleLiteralTerm)
Query.cpp-754-             ? FoldAscii16(query.simpleLiteralTerm) : std::u16string{};
Query.cpp:755:         query.simpleLiteralAnchorIndex = ChooseAsciiAnchor(query.simpleLiteralFolded);
Query.cpp-756-         query.simpleLiteralAnchor = query.simpleLiteralFolded.empty()
Query.cpp-757-             ? 0 : query.simpleLiteralFolded[query.simpleLiteralAnchorIndex];
Query.cpp-758-         query.simpleLiteralShifts = BuildAsciiShifts(query.simpleLiteralFolded);
Query.cpp-759-     }
Query.cpp-752-         query.simpleLiteralTerm = std::move(simpleLiteralTerm);
Query.cpp-753-         query.simpleLiteralFolded = IsAscii(query.simpleLiteralTerm)
Query.cpp-754-             ? FoldAscii16(query.simpleLiteralTerm) : std::u16string{};
Query.cpp-755-         query.simpleLiteralAnchorIndex = ChooseAsciiAnchor(query.simpleLiteralFolded);
Query.cpp:756:         query.simpleLiteralAnchor = query.simpleLiteralFolded.empty()
Query.cpp-757-             ? 0 : query.simpleLiteralFolded[query.simpleLiteralAnchorIndex];
Query.cpp-758-         query.simpleLiteralShifts = BuildAsciiShifts(query.simpleLiteralFolded);
Query.cpp-759-     }
Query.cpp-760-     return query;
Query.cpp-753-         query.simpleLiteralFolded = IsAscii(query.simpleLiteralTerm)
Query.cpp-754-             ? FoldAscii16(query.simpleLiteralTerm) : std::u16string{};
Query.cpp-755-         query.simpleLiteralAnchorIndex = ChooseAsciiAnchor(query.simpleLiteralFolded);
Query.cpp-756-         query.simpleLiteralAnchor = query.simpleLiteralFolded.empty()
Query.cpp:757:             ? 0 : query.simpleLiteralFolded[query.simpleLiteralAnchorIndex];
Query.cpp-758-         query.simpleLiteralShifts = BuildAsciiShifts(query.simpleLiteralFolded);
Query.cpp-759-     }
Query.cpp-760-     return query;
Query.cpp-761- }
Query.cpp-754-             ? FoldAscii16(query.simpleLiteralTerm) : std::u16string{};
Query.cpp-755-         query.simpleLiteralAnchorIndex = ChooseAsciiAnchor(query.simpleLiteralFolded);
Query.cpp-756-         query.simpleLiteralAnchor = query.simpleLiteralFolded.empty()
Query.cpp-757-             ? 0 : query.simpleLiteralFolded[query.simpleLiteralAnchorIndex];
Query.cpp:758:         query.simpleLiteralShifts = BuildAsciiShifts(query.simpleLiteralFolded);
Query.cpp-759-     }
Query.cpp-760-     return query;
Query.cpp-761- }
Query.cpp-762- 
grep: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/s...
grep: C:/Users/Michael Roy/Documents/FastFiles/src/search/src/Q...
Projection.cpp-160-         }
Projection.cpp-161-     }
Projection.cpp-162-     bulkScanActive_ = active;
Projection.cpp-163- }
Projection.cpp-164- 
Projection.cpp-165- #ifdef FF_INDEXSTORE_TESTING
Projection.cpp:166: void Projection::SetTestingFoldExceptionalUnit(const char16_t unit, const bool exceptional) {
Projection.cpp-167-     // large-index-lifecycle-and-search-scalability review-remediation (P2-A):
Projection.cpp-168-     // testing-only fold-exceptional override; process-wide and idempotent.
Projection.cpp-169-     gTestingForceFoldExceptional[static_cast<uint16_t>(unit)].store(
Projection.cpp-170-         exceptional ? 1 : 0, std::memory_order_release);
Projection.cpp-171- }
Projection.cpp-172- #endif
Query.cpp-41-     return result;
Query.cpp-42- }
Query.cpp-43- 
Query.cpp:44: bool IsAscii(std::wstring_view value) {
Query.cpp-45-     return std::all_of(value.begin(), value.end(), [](wchar_t ch) { return ch <= 0x7f; });
Query.cpp-46- }
Query.cpp-47- 
Query.cpp-88-     }
Query.cpp-89- }
Query.cpp-90- 
Query.cpp:91: size_t ChooseAsciiAnchor(std::u16string_view needle) {
Query.cpp-92-     size_t anchor = 0;
Query.cpp-93-     for (size_t index = 1; index < needle.size(); ++index) {
Query.cpp-94-         if (AsciiFrequency(needle[index]) < AsciiFrequency(needle[anchor])) anchor = index;
Query.cpp-201- 
Query.cpp-202- // Single dispatch point for prepared literal matching over projection views.
Query.cpp-203- // `shifts` must be built once by the caller via BuildAsciiShifts(foldedNeedle).
Query.cpp:204: bool OrdinalContains16Prepared(std::u16string_view haystack, std::wstring_view needle,
Query.cpp-205-                                std::u16string_view foldedNeedle,
Query.cpp-206-                                const std::array<size_t, 128>& shifts,
Query.cpp-207-                                bool nameIsAscii,
Query.cpp-445-         return value.empty() ? std::nullopt : std::optional<FilterValue>(std::wstring(value));
Query.cpp-446-     }, [](const FilterValue& value) {
Query.cpp-447-         const auto needle = PrepareAsciiFoldNeedle(std::get<std::wstring>(value));
Query.cpp:448:         return [needle](const Candidate& candidate) { return AsciiFoldContains(candidate.name, needle); };
Query.cpp-449-     }, [](const FilterValue& value) {
Query.cpp-450-         const auto text = std::get<std::wstring>(value);
Query.cpp-451-         const auto foldedText = IsAscii(text) ? FoldAscii16(text) : std::u16string{};
Query.cpp-448-         return [needle](const Candidate& candidate) { return AsciiFoldContains(candidate.name, needle); };
Query.cpp-449-     }, [](const FilterValue& value) {
Query.cpp-450-         const auto text = std::get<std::wstring>(value);
Query.cpp:451:         const auto foldedText = IsAscii(text) ? FoldAscii16(text) : std::u16string{};
Query.cpp-452-         const auto foldedShifts = BuildAsciiShifts(foldedText);
Query.cpp-453-         const size_t foldedAnchorIndex = ChooseAsciiAnchor(foldedText);
Query.cpp-454-         const char16_t foldedAnchor = foldedText.empty() ? 0 : foldedText[foldedAnchorIndex];
Query.cpp-450-         const auto text = std::get<std::wstring>(value);
Query.cpp-451-         const auto foldedText = IsAscii(text) ? FoldAscii16(text) : std::u16string{};
Query.cpp-452-         const auto foldedShifts = BuildAsciiShifts(foldedText);
Query.cpp:453:         const size_t foldedAnchorIndex = ChooseAsciiAnchor(foldedText);
Query.cpp-454-         const char16_t foldedAnchor = foldedText.empty() ? 0 : foldedText[foldedAnchorIndex];
Query.cpp-455-         return [text, foldedText, foldedShifts, foldedAnchor, foldedAnchorIndex](const CandidateView& candidate) {
Query.cpp-456-             return OrdinalContains16Prepared(candidate.name, text, foldedText, foldedShifts,
Query.cpp-453-         const size_t foldedAnchorIndex = ChooseAsciiAnchor(foldedText);
Query.cpp-454-         const char16_t foldedAnchor = foldedText.empty() ? 0 : foldedText[foldedAnchorIndex];
Query.cpp-455-         return [text, foldedText, foldedShifts, foldedAnchor, foldedAnchorIndex](const CandidateView& candidate) {
Query.cpp:456:             return OrdinalContains16Prepared(candidate.name, text, foldedText, foldedShifts,
Query.cpp-457-                                              candidate.nameIsAscii, foldedAnchor, foldedAnchorIndex);
Query.cpp-458-         };
Query.cpp-459-     }});
Query.cpp-463-         // Folder paths are long; the prepared fold scan avoids one ordinal
Query.cpp-464-         // API call per alignment on the dominant ASCII path class.
Query.cpp-465-         const auto needle = PrepareAsciiFoldNeedle(std::get<std::wstring>(value));
Query.cpp:466:         return [needle](const Candidate& candidate) { return AsciiFoldContains(candidate.folder, needle); };
Query.cpp-467-     }, [](const FilterValue& value) {
Query.cpp-468-         const auto needle = PrepareAsciiFoldNeedle(std::get<std::wstring>(value));
Query.cpp-469-         return [needle](const CandidateView& candidate) { return AsciiFoldContains(candidate.folder, needle); };
Query.cpp-466-         return [needle](const Candidate& candidate) { return AsciiFoldContains(candidate.folder, needle); };
Query.cpp-467-     }, [](const FilterValue& value) {
Query.cpp-468-         const auto needle = PrepareAsciiFoldNeedle(std::get<std::wstring>(value));
Query.cpp:469:         return [needle](const CandidateView& candidate) { return AsciiFoldContains(candidate.folder, needle); };
Query.cpp-470-     }});
Query.cpp-471-     defaults.Register(L"size", {ParseSizeFilter, MakeSizePredicate, [](const FilterValue& value) {
Query.cpp-472-         const auto filter = std::get<SizeFilter>(value);
Query.cpp-567- }
Query.cpp-568- 
Query.cpp-569- bool MatchSimpleLiteral(const Query& query, const CandidateView& candidate) {
Query.cpp:570:     return OrdinalContains16Prepared(candidate.name, query.simpleLiteralTerm, query.simpleLiteralFolded,
Query.cpp-571-                                      query.simpleLiteralShifts, candidate.nameIsAscii,
Query.cpp-572-                                      query.simpleLiteralAnchor, query.simpleLiteralAnchorIndex);
Query.cpp-573- }
Query.cpp-642- AsciiFoldNeedle PrepareAsciiFoldNeedle(std::wstring_view value) {
Query.cpp-643-     AsciiFoldNeedle needle;
Query.cpp-644-     needle.value = std::wstring(value);
Query.cpp:645:     needle.ascii = IsAscii(value);
Query.cpp-646-     if (!needle.ascii) return needle;
Query.cpp-647-     needle.folded = FoldAscii16(value);
Query.cpp-648-     if (!needle.folded.empty()) {
Query.cpp-646-     if (!needle.ascii) return needle;
Query.cpp-647-     needle.folded = FoldAscii16(value);
Query.cpp-648-     if (!needle.folded.empty()) {
Query.cpp:649:         needle.anchorIndex = ChooseAsciiAnchor(needle.folded);
Query.cpp-650-         needle.anchor = needle.folded[needle.anchorIndex];
Query.cpp-651-         needle.shifts = BuildAsciiShifts(needle.folded);
Query.cpp-652-     }
Query.cpp-653-     return needle;
Query.cpp-654- }
Query.cpp-655- 
Query.cpp:656: bool AsciiFoldContains(std::wstring_view haystack, const AsciiFoldNeedle& needle) {
Query.cpp-657-     // Non-ASCII and empty needles keep exact ordinal semantics via fallback.
Query.cpp-658-     if (!needle.ascii || needle.folded.empty()) return OrdinalContains(haystack, needle.value);
Query.cpp-659-     if (haystack.size() < needle.folded.size()) return false;
Query.cpp-732-         } else {
Query.cpp-733-             if (query.primaryTerm.empty()) query.primaryTerm = token;
Query.cpp-734-             simpleLiteralTerm = token;
Query.cpp:735:             const auto foldedToken = IsAscii(token) ? FoldAscii16(token) : std::u16string{};
Query.cpp-736-             const auto foldedTokenShifts = BuildAsciiShifts(foldedToken);
Query.cpp-737-             const size_t foldedAnchorIndex = ChooseAsciiAnchor(foldedToken);
Query.cpp-738-             const char16_t foldedAnchor = foldedToken.empty() ? 0 : foldedToken[foldedAnchorIndex];
Query.cpp-734-             simpleLiteralTerm = token;
Query.cpp-735-             const auto foldedToken = IsAscii(token) ? FoldAscii16(token) : std::u16string{};
Query.cpp-736-             const auto foldedTokenShifts = BuildAsciiShifts(foldedToken);
Query.cpp:737:             const size_t foldedAnchorIndex = ChooseAsciiAnchor(foldedToken);
Query.cpp-738-             const char16_t foldedAnchor = foldedToken.empty() ? 0 : foldedToken[foldedAnchorIndex];
Query.cpp-739-             const auto asciiNeedle = PrepareAsciiFoldNeedle(token);
Query.cpp-740-             addPredicate([asciiNeedle](const Candidate& candidate) { return AsciiFoldContains(candidate.name, asciiNeedle); },
Query.cpp-737-             const size_t foldedAnchorIndex = ChooseAsciiAnchor(foldedToken);
Query.cpp-738-             const char16_t foldedAnchor = foldedToken.empty() ? 0 : foldedToken[foldedAnchorIndex];
Query.cpp-739-             const auto asciiNeedle = PrepareAsciiFoldNeedle(token);
Query.cpp:740:             addPredicate([asciiNeedle](const Candidate& candidate) { return AsciiFoldContains(candidate.name, asciiNeedle); },
Query.cpp-741-                          [token, foldedToken, foldedTokenShifts, foldedAnchor, foldedAnchorIndex](const CandidateView& candidate) {
Query.cpp-742-                              return OrdinalContains16Prepared(candidate.name, token, foldedToken, foldedTokenShifts,
Query.cpp-743-                                                               candidate.nameIsAscii, foldedAnchor, foldedAnchorIndex);
Query.cpp-739-             const auto asciiNeedle = PrepareAsciiFoldNeedle(token);
Query.cpp-740-             addPredicate([asciiNeedle](const Candidate& candidate) { return AsciiFoldContains(candidate.name, asciiNeedle); },
Query.cpp-741-                          [token, foldedToken, foldedTokenShifts, foldedAnchor, foldedAnchorIndex](const CandidateView& candidate) {
Query.cpp:742:                              return OrdinalContains16Prepared(candidate.name, token, foldedToken, foldedTokenShifts,
Query.cpp-743-                                                               candidate.nameIsAscii, foldedAnchor, foldedAnchorIndex);
Query.cpp-744-                          }, false,
Query.cpp-745-                          PredicateKind::LiteralName);
Query.cpp-750-         !simpleLiteralTerm.empty()) {
Query.cpp-751-         query.simpleLiteralName = true;
Query.cpp-752-         query.simpleLiteralTerm = std::move(simpleLiteralTerm);
Query.cpp:753:         query.simpleLiteralFolded = IsAscii(query.simpleLiteralTerm)
Query.cpp-754-             ? FoldAscii16(query.simpleLiteralTerm) : std::u16string{};
Query.cpp-755-         query.simpleLiteralAnchorIndex = ChooseAsciiAnchor(query.simpleLiteralFolded);
Query.cpp-756-         query.simpleLiteralAnchor = query.simpleLiteralFolded.empty()
Query.cpp-752-         query.simpleLiteralTerm = std::move(simpleLiteralTerm);
Query.cpp-753-         query.simpleLiteralFolded = IsAscii(query.simpleLiteralTerm)
Query.cpp-754-             ? FoldAscii16(query.simpleLiteralTerm) : std::u16string{};
Query.cpp:755:         query.simpleLiteralAnchorIndex = ChooseAsciiAnchor(query.simpleLiteralFolded);
Query.cpp-756-         query.simpleLiteralAnchor = query.simpleLiteralFolded.empty()
Query.cpp-757-             ? 0 : query.simpleLiteralFolded[query.simpleLiteralAnchorIndex];
Query.cpp-758-         query.simpleLiteralShifts = BuildAsciiShifts(query.simpleLiteralFolded);
Query.cpp-770- 
Query.cpp-771- bool FoldedEquals16(std::u16string_view name, std::wstring_view term) {
Query.cpp-772-     if (name.size() != term.size()) return false;
Query.cpp:773:     if (IsAscii(term)) {
Query.cpp-774-         for (size_t index = 0; index < name.size(); ++index) {
Query.cpp-775-             if (FoldAscii16Char(name[index]) != static_cast<char16_t>(FoldAsciiWideChar(term[index]))) {
Query.cpp-776-                 return false;
Query.cpp-783- 
Query.cpp-784- bool FoldedStartsWith16(std::u16string_view name, std::wstring_view term) {
Query.cpp-785-     if (term.size() > name.size()) return false;
Query.cpp:786:     if (IsAscii(term)) {
Query.cpp-787-         for (size_t index = 0; index < term.size(); ++index) {
Query.cpp-788-             if (FoldAscii16Char(name[index]) != static_cast<char16_t>(FoldAsciiWideChar(term[index]))) {
Query.cpp-789-                 return false;
Query.cpp-797- bool FoldedContains16(std::u16string_view name, std::wstring_view term) {
Query.cpp-798-     if (term.empty()) return true;
Query.cpp-799-     if (term.size() > name.size()) return false;
Query.cpp:800:     if (IsAscii(term)) {
Query.cpp-801-         for (size_t start = 0; start + term.size() <= name.size(); ++start) {
Query.cpp-802-             bool equal = true;
Query.cpp-803-             for (size_t index = 0; index < term.size(); ++index) {
read: C:/Users/Michael Roy/Documents/FastFiles/src/search/src/Q...
[pi-lens: read widened to the enclosing function_definition "function_definition" (symbol boundary): you asked for lines 180-279, this shows lines 175-279. Re-request with limit > 100 for the exact range.]
bool AnchorContainsFoldWide(std::wstring_view haystack, const AsciiFoldNeedle& needle) {
    const std::u16string_view folded(needle.folded);
    for (size_t position = needle.anchorIndex; position < haystack.size(); ++position) {
        if (static_cast<char16_t>(FoldAsciiWideChar(haystack[position])) != needle.anchor) continue;
        const size_t start = position - needle.anchorIndex;
        if (start + folded.size() > haystack.size()) return false;
        if (FoldedWindowEqualsWide(haystack, start, folded)) return true;
    }
    return false;
}
bool BoyerMooreHorspoolContainsWide(std::wstring_view haystack, const AsciiFoldNeedle& needle) {
    const std::u16string_view folded(needle.folded);
    const size_t lastIndex = folded.size() - 1;
    size_t position = lastIndex;
    while (position < haystack.size()) {
        const wchar_t rawTail = haystack[position];
        if (static_cast<char16_t>(FoldAsciiWideChar(rawTail)) == folded.back() &&
            FoldedWindowEqualsWide(haystack, position - lastIndex, folded)) {
            return true;
        }
        const size_t raw = static_cast<size_t>(rawTail);
        position += raw < needle.shifts.size() ? needle.shifts[raw] : folded.size();
    }
    return false;
}
// Single dispatch point for prepared literal matching over projection views.
// `shifts` must be built once by the caller via BuildAsciiShifts(foldedNeedle).
bool OrdinalContains16Prepared(std::u16string_view haystack, std::wstring_view needle,
                               std::u16string_view foldedNeedle,
                               const std::array<size_t, 128>& shifts,
                               bool nameIsAscii,
                               char16_t anchor, size_t anchorIndex) {
    if (needle.empty()) return true;
    if (haystack.size() < needle.size()) return false;
    if (!foldedNeedle.empty() && nameIsAscii) {
        return foldedNeedle.size() >= 4
            ? BoyerMooreHorspoolContains(haystack, foldedNeedle, shifts)
            : AnchorContainsFold(haystack, foldedNeedle, anchor, anchorIndex);
    }
    for (size_t index = 0; index + needle.size() <= haystack.size(); ++index) {
        if (CompareOrdinal16(haystack.substr(index, needle.size()), needle) == 0) return true;
    }
    return false;
}
bool OrdinalContains16(std::u16string_view haystack, std::wstring_view needle) {
    if (needle.empty()) return true;
    if (haystack.size() < needle.size()) return false;
    for (size_t index = 0; index + needle.size() <= haystack.size(); ++index) {
        if (CompareOrdinal16(haystack.substr(index, needle.size()), needle) == 0) return true;
    }
    return false;
}
bool GlobMatches16(std::u16string_view text, std::wstring_view pattern) {
    const auto width = [](std::u16string_view value, size_t offset) -> size_t {
        if (offset + 1 < value.size() && IS_HIGH_SURROGATE(value[offset]) && IS_LOW_SURROGATE(value[offset + 1])) return 2;
        return 1;
    };
    size_t textIndex = 0, patternIndex = 0, star = std::wstring_view::npos, saved = 0;
    while (textIndex < text.size()) {
        if (patternIndex < pattern.size() && pattern[patternIndex] == L'?') {
            textIndex += width(text, textIndex);
            ++patternIndex;
        } else if (patternIndex < pattern.size() && pattern[patternIndex] != L'*') {
            const size_t textWidth = width(text, textIndex);
            const size_t patternWidth = pattern[patternIndex] >= 0xD800 && pattern[patternIndex] <= 0xDBFF &&
                patternIndex + 1 < pattern.size() && pattern[patternIndex + 1] >= 0xDC00 && pattern[patternIndex + 1] <= 0xDFFF ? 2 : 1;
            if (textWidth == patternWidth && CompareOrdinal16(text.substr(textIndex, textWidth),
                                                                pattern.substr(patternIndex, patternWidth)) == 0) {
                textIndex += textWidth;
                patternIndex += patternWidth;
            } else if (star != std::wstring_view::npos) {
                patternIndex = star + 1;
                saved += width(text, saved);
                textIndex = saved;
            } else {
                return false;
            }
        } else if (patternIndex < pattern.size() && pattern[patternIndex] == L'*') {
            star = patternIndex++;
            saved = textIndex;
        } else if (star != std::wstring_view::npos) {
            patternIndex = star + 1;
            saved += width(text, saved);
            textIndex = saved;
        } else {
            return false;
        }
    }
    while (patternIndex < pattern.size() && pattern[patternIndex] == L'*') ++patternIndex;
    return patternIndex == pattern.size();
}
bool LowerEquals16(std::u16string_view value, std::wstring_view lowerValue) {
    if (value.size() != lowerValue.size()) return false;
    for (size_t index = 0; index < value.size(); ++index) {
        if (gActiveDiagnostics != nullptr) ++gActiveDiagnostics->caseFoldOperations;
        if (static_cast<wchar_t>(towlower(static_cast<wchar_t>(value[index]))) != lowerValue[index]) return false;
    }
    return true;
}
[560 more lines in file. Use offset=280 to continue.]
grep: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/CMa...
read: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/tes...
CMakeLists.txt:1: add_executable(ffengine_index_pipeline_tests
CMakeLists.txt:2:   test_index_pipeline.cpp
CMakeLists.txt:6: target_include_directories(ffengine_index_pipeline_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/engine/src)
CMakeLists.txt:7: target_link_libraries(ffengine_index_pipeline_tests PRIVATE ffindexstore ffprotocol ffsearch Rpcrt4 Shell32 Ole32)
CMakeLists.txt:10: target_compile_definitions(ffengine_index_pipeline_tests PRIVATE FF_INDEX_PIPELINE_TESTING=1)
CMakeLists.txt:12: add_test(NAME ffengine_index_pipeline_tests COMMAND ffengine_index_pipeline_tests)
CMakeLists.txt:243: add_executable(ffengine_index_pipeline_concurrency_tests
CMakeLists.txt:244:   test_index_pipeline_concurrency.cpp
CMakeLists.txt:248: target_include_directories(ffengine_index_pipeline_concurrency_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/engine/src)
CMakeLists.txt:249: target_link_libraries(ffengine_index_pipeline_concurrency_tests PRIVATE ffindexstore ffprotocol ffsearch Rpcrt4 Shell32 Ole32)
CMakeLists.txt:250: add_test(NAME ffengine_index_pipeline_concurrency_tests COMMAND ffengine_index_pipeline_concurrency_tests)
CMakeLists.txt:254: add_executable(ffengine_bench_large_index_scalability
CMakeLists.txt:255:   bench_large_index_scalability.cpp
CMakeLists.txt:260: target_include_directories(ffengine_bench_large_index_scalability PRIVATE ${CMAKE_SOURCE_DIR}/src/engine/src)
CMakeLists.txt:261: target_link_libraries(ffengine_bench_large_index_scalability PRIVATE ffindexstore ffprotocol ffsearch ffsetup psapi Rpcrt4 Shell32 Ole32)
CMakeLists.txt:264: add_dependencies(ffengine_bench_large_index_scalability fastfiles_build_identity)
CMakeLists.txt:270: add_executable(ffengine_bench_secondary_filename_index
CMakeLists.txt:271:   bench_secondary_filename_index.cpp
CMakeLists.txt:273: target_link_libraries(ffengine_bench_secondary_filename_index PRIVATE ffsearch sqlite3 psapi)
CMakeLists.txt:303: set_tests_properties(ffengine_index_pipeline_tests ffengine_volume_session_manager_tests ffengine_degraded_special_files_tests ffengine_directory_watcher_tests ffengine_idle_manager_tests ffengine_ui_server_lifecycle_tests ffengine_snapshot_publisher_tests ffengine_subtree_gating_tests ffengine_client_handshake_tests ffengine_active_path_resilience_tests ffengine_seeded_indexed_mode_tests ffengine_index_pipeline_concurrency_tests ffengine_pooled_snapshot_lifetime_tests PROPERTIES TIMEOUT 120)
[20 matches limit reached. Use limit=40 for more, or refine pattern]
void TestForcedEvictionSearchEquivalence() {
    IndexPipeline pipeline;
    const std::string dbPath = FreshDbPath("engine_test_eviction_equivalence.db");
    Check(pipeline.Open(dbPath), "the forced-eviction fixture opens");
    VolumeKey coldKey;
    coldKey.serialNumber = 41;
    const auto coldVolume = pipeline.ResolveVolume(coldKey);
    ffprotocol::VolumeSetting coldSetting;
    coldSetting.key = L"D:";
    pipeline.SetVolumeRules(coldVolume, coldSetting);
    // Ingest the cold volume FIRST so its LRU timestamps are the oldest;
    // shrinking the budget then deterministically evicts its subtrees.
    std::u16string emojiName = u"emoji-";
    emojiName.push_back(static_cast<char16_t>(0xD83D)); // U+1F600 pair
    emojiName.push_back(static_cast<char16_t>(0xDE00));
    emojiName += u"tailfrag.txt";
    Check(pipeline.ApplyMftBatch(coldVolume, {
        MakeMftRecord(1, 1, u".", 0x10, 0),
        MakeMftRecord(10, 1, u"ColdRoot", 0x10, 0),
        MakeMftRecord(11, 10, u"SubA", 0x10, 0),
        MakeMftRecord(100, 11, u"alpha-report.txt", 0, 120),
        MakeMftRecord(101, 11, u"photo.jpg", 0, 30),
        MakeMftRecord(102, 11, u"\u0444\u0430\u0439\u043b-\u03a9.txt", 0, 55),
        MakeMftRecord(103, 11, u"dup.txt", 0, 9),
        MakeMftRecord(12, 10, u"SubB", 0x10, 0),
        MakeMftRecord(110, 12, u"dup.txt", 0, 77),
        MakeMftRecord(111, 12, u"beta.txt", 0, 500),
        MakeMftRecord(120, 10, u"top.jpg", 0, 64),
        // large-index-lifecycle-and-search-scalability review-remediation 18.x:
        // cold-prefilter adversarial corpus -- LIKE metacharacters in names,
        // non-ASCII and surrogate names with ASCII tails, and the long-s
        // host-fold case (soaked by the hot/cold signature comparison).
        MakeMftRecord(121, 11, u"100%_done.txt", 0, 5),
        MakeMftRecord(122, 11, u"a_b.txt", 0, 6),
        MakeMftRecord(123, 11, u"axb.txt", 0, 7),
        MakeMftRecord(124, 11, u"\u017Foo.txt", 0, 8),
        MakeMftRecord(125, 11, emojiName, 0, 9),
        // Differential-matrix cells that had no named coverage: NFC vs NFD
        // forms of the same visible name (ordinal compare does NOT normalize,
        // so these are distinct rows) with an ASCII tail.
        MakeMftRecord(126, 11, u"o\u0308verlay.txt", 0, 10),
        MakeMftRecord(127, 11, u"\u00F6verlay.txt", 0, 11),
    }), "the cold volume fixture persists through normal ingestion");
    {
        // Repeated-prefix posting pressure: 20 rows sharing one long prefix.
        std::vector<ffprotocol::MftRecordV1> repeated;
        for (uint64_t rep = 1; rep <= 20; ++rep) {
            repeated.push_back(MakeMftRecord(200 + rep, 12,
                std::u16string(u"repeat-common-") + U16FromWide(std::to_wstring(rep)) + u".txt", 0, 1));
        }
        Check(pipeline.ApplyMftBatch(coldVolume, repeated),
              "the repeated-prefix rows persist through normal ingestion");
    }
    const auto diagnosticsColdOnly = pipeline.GetProjectionMemoryDiagnostics();
    (void)diagnosticsColdOnly;
    VolumeKey hotKey;
    hotKey.serialNumber = 42;
    const auto hotVolume = pipeline.ResolveVolume(hotKey);
    ffprotocol::VolumeSetting hotSetting;
    hotSetting.key = L"E:";
    pipeline.SetVolumeRules(hotVolume, hotSetting);
    Check(pipeline.ApplyMftBatch(hotVolume, {
        MakeMftRecord(1, 1, u".", 0x10, 0),
        MakeMftRecord(20, 1, u"HotRoot", 0x10, 0),
        MakeMftRecord(200, 20, u"hot-alpha.txt", 0, 8),
        MakeMftRecord(201, 20, u"gamma.jpg", 0, 15),
    }), "the hot volume fixture persists through normal ingestion");
    auto makeQuery = [&](const std::wstring& queryText, int scope = 3,
                         const std::wstring& scopePath = L"", int sortField = 0,
                         size_t maxResults = 4096) {
        ffprotocol::IndexedSearchRequest request;
        request.requestId = 700;
        request.scope = static_cast<uint8_t>(scope);
        request.scopePath = scopePath;
        request.sortField = static_cast<uint8_t>(sortField);
        request.maxResults = static_cast<uint32_t>(maxResults);
        request.query = queryText;
        return request;
    };
    struct QueryCase {
        const char* label;
        ffprotocol::IndexedSearchRequest request;
        bool trigramExpectedHot;
    };
    std::vector<QueryCase> cases = {
        {"selective literal", makeQuery(L"alpha"), true},
        {"no-match", makeQuery(L"zzznomatch"), false},
        {"trigram-eligible prefix", makeQuery(L"alph"), true},
        {"short canonical fallback", makeQuery(L"al"), false},
        {"glob filter", makeQuery(L"*.jpg"), false},
        {"extension predicate", makeQuery(L"ext:txt"), false},
        {"size predicate", makeQuery(L"size:>60"), false},
        {"folder predicate", makeQuery(L"FOLDER:SUBA"), false},
        {"unicode filename", makeQuery(L"\u0444\u0430\u0439\u043b"), false},
        {"duplicate basenames filename sort", makeQuery(L"dup.txt", 3, L"", 1), false},
        {"duplicate basenames path sort", makeQuery(L"dup.txt", 3, L"", 2), false},
        {"path-scoped subtree", makeQuery(L"report", 1, L"D:\\ColdRoot\\SubA"), false},
        {"broad capped", makeQuery(L"txt", 3, L"", 0, 3), false},
[1171 more lines in file. Use offset=1376 to continue.]
read: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/tes...
        // large-index-lifecycle-and-search-scalability review-remediation 18.x:
        // cold prefilter adversarial needles (LIKE metacharacters, ASCII case
        // variants, ASCII tails of non-ASCII names, long-s host-fold case).
        {"percent needle", makeQuery(L"100%"), false},
        {"underscore needle", makeQuery(L"a_b"), false},
        {"emoji tail needle", makeQuery(L"tailfrag"), false},
        {"long-s host-fold needle", makeQuery(L"soo"), false},
        {"upper-case selective", makeQuery(L"ALPHA"), true},
        // Differential-matrix cells that had no named coverage.
        {"empty query rejection parity", makeQuery(L""), false},
        {"repeated prefix needle", makeQuery(L"repeat-common"), false},
        {"NFD name ascii tail", makeQuery(L"verlay"), false},
        {"NFC name needle overlay", makeQuery(L"overlay"), false},
    };
    // Reference pass: everything fully resident.
    std::vector<ffprotocol::IndexedSearchResponse> reference;
    reference.reserve(cases.size());
    for (auto& testCase : cases) {
        IndexPipeline::SearchTimings timings;
        auto response = pipeline.SearchIndexed(testCase.request, &timings);
        if (testCase.trigramExpectedHot) {
            Check(timings.filenameIndexUsed,
                  "fully-resident eligible searches use the trigram accelerator");
        }
        // An empty query string is rejected by contract (InvalidRequest);
        // every other case must complete. Either way the same verdict must
        // hold after eviction (the equivalence loop re-checks).
        const bool expectRejected = testCase.request.query.empty();
        Check(response.status == (expectRejected
                                      ? ffprotocol::IndexedSearchStatus::InvalidRequest
                                      : ffprotocol::IndexedSearchStatus::Completed),
              "reference-pass indexed searches complete or reject consistently");
        reference.push_back(std::move(response));
    }
    Check(reference[2].candidates.size() >= reference[0].candidates.size(),
          "the reference prefix query is at least as broad as the selective literal");
    Check(!reference[12].candidates.empty() && reference[12].truncated,
          "the broad capped reference query reports honest truncation");
    Check(reference[9].candidates.size() == 2 && reference[9].candidates[0].folder != reference[9].candidates[1].folder,
          "duplicate-basename filename sorting orders deterministically across folders");
    // Force deterministic eviction of exactly the cold volume: an explicit
    // policy unload drops its resident rows while data stays durable.
    pipeline.UnloadVolumeFromProjection(coldVolume);
    Check(pipeline.IsVolumeEvicted(coldVolume), "unloading marks the large volume evicted");
    Check(!pipeline.IsVolumeEvicted(hotVolume), "the small volume stays projection-resident");
    const auto afterEviction = pipeline.GetProjectionMemoryDiagnostics();
    Check(afterEviction.subtreeEvictionCount > 0, "unload performs subtree evictions");
    Check(afterEviction.coldReloadCount == 0, "no search-driven reloads occurred during setup");
    Check(afterEviction.lastEvictionBytesBefore >= afterEviction.lastEvictionBytesAfter,
          "eviction diagnostics record the reclaimed bytes");
    auto signatures = [](const ffprotocol::IndexedSearchResponse& response) {
        std::vector<std::tuple<std::wstring, std::wstring, uint64_t, uint64_t, uint64_t, bool, bool, int64_t, uint64_t, uint64_t>> out;
        out.reserve(response.candidates.size());
        for (const auto& candidate : response.candidates) {
            out.emplace_back(candidate.name, candidate.folder, candidate.sizeBytes,
                             candidate.allocatedSizeBytes, candidate.modifiedTime,
                             candidate.isDirectory, candidate.allocatedSizeKnown,
                             candidate.volumeId, candidate.id, candidate.parentId);
        }
        return out;
    };
    // Equivalence pass: identical identities AND ordering from mixed sources.
    bool sawColdAttribution = false;
    for (size_t index = 0; index < cases.size(); ++index) {
        IndexPipeline::SearchTimings timings;
        auto response = pipeline.SearchIndexed(cases[index].request, &timings);
        const bool expectRejected = cases[index].request.query.empty();
        Check(response.status == (expectRejected
                                      ? ffprotocol::IndexedSearchStatus::InvalidRequest
                                      : ffprotocol::IndexedSearchStatus::Completed),
              "forced-eviction indexed searches complete or reject consistently");
        Check(response.status == reference[index].status,
              cases[index].label);
        Check(signatures(response) == signatures(reference[index]),
              cases[index].label);
        Check(timings.truncated == reference[index].truncated,
              "truncation flags agree between resident and evicted runs");
        if (timings.coldSearchUsed) {
            sawColdAttribution = true;
            Check(timings.coldVolumesSearched == 1,
                  "exactly one cold volume streams from the durable store");
            Check(timings.coldRowsExamined > 0, "cold stream examines durable rows");
        }
        if (!cases[index].trigramExpectedHot) continue;
        Check(!timings.filenameIndexUsed,
              "an evicted searchable set disables the trigram accelerator (no false negatives)");
    }
    Check(sawColdAttribution, "at least one query exercised the cold-store attribution path");
}
// memory-bounded-indexed-scale 3.3: repeated alternating searches over an
// evicted index must neither grow the projection nor reload/evict thrash.
// completion-review R1: a volume-root row's pre-materialization view name is
// empty while its owning candidate uses the display name `prefix + "\\"`.
// The bounded top-N admission must not judge root rows by that placeholder:
// a capped descending filename search must return the root the durable path
// returns, and hot/cold must agree for every ordering and direction.
void TestCappedRootSearchMatchesColdAndRetainsTopN() {
    IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_capped_root_search.db")), "the capped-root fixture opens");
    VolumeKey firstKey;
    firstKey.serialNumber = 0;
    const auto firstVolume = pipeline.ResolveVolume(firstKey);
    VolumeKey secondKey;
    secondKey.serialNumber = 2;
    const auto secondVolume = pipeline.ResolveVolume(secondKey);
    ffprotocol::VolumeSetting firstSetting;
    firstSetting.key = L"C:";
    firstSetting.enabled = true;
    pipeline.SetVolumeRules(firstVolume, firstSetting);
    ffprotocol::VolumeSetting secondSetting;
[1056 more lines in file. Use offset=1491 to continue.]
grep: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/I...
read: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/ben...
IndexPipeline.cpp:1500:                         const bool filenameEligible = searchRequest.query.simpleLiteralName &&
IndexPipeline.cpp:1502:                         if (filenameEligible) {
IndexPipeline.cpp:1540:     for (size_t restart = 0; restart <= kMaxGenerationRestarts; ++restart) {
IndexPipeline.cpp:1565:         if (context.phaseHook) context.phaseHook(SearchPhase::WorkStarted);
IndexPipeline.cpp:1731:                     if (context.phaseHook) context.phaseHook(SearchPhase::Matching);
IndexPipeline.cpp:1860:                     if (context.phaseHook) context.phaseHook(SearchPhase::Matching);
IndexPipeline.cpp:1879:                     if (context.phaseHook) context.phaseHook(SearchPhase::ColdStreaming);
IndexPipeline.cpp:1887:                 const auto coldNamePrefilter = ColdNamePrefilterNeedle(searchRequest.query);
IndexPipeline.cpp:1907:                     : context.reader->ForEachEntryUntil(coldVolumeId, coldVisitor);
IndexPipeline.cpp:1920:         if (context.phaseHook) context.phaseHook(SearchPhase::GenerationValidation);
IndexPipeline.cpp:1921:         markStaleIfChanged();
IndexPipeline.cpp:1925:             if (restart == kMaxGenerationRestarts) {
IndexPipeline.cpp:1992:             if (context.phaseHook) context.phaseHook(SearchPhase::DurableFallback);
IndexPipeline.cpp:2017:                 const auto durableNamePrefilter = ColdNamePrefilterNeedle(searchRequest.query);
IndexPipeline.cpp:2037:                     : context.reader->ForEachEntryUntil(volumeId, durableVisitor);
    {"medium_literal", L"component_7391"},
    {"long_literal", L"unique_component_7391.txt"},
    {"multi_term_fallback", L"report txt"},
    {"glob_fallback", L"*.txt"},
    {"folder_predicate_fallback", L"folder:folder_1"},
    {"extension_predicate_fallback", L"ext:txt"},
    {"metadata_predicate_fallback", L"size:>1MB"},
    {"kind_predicate_fallback", L"kind:folder"},
}};
bool IsEligible(const ffsearch::Query& query) {
    return query.viewCompatible && query.simpleLiteralName && !query.requiresFolder &&
           IsAscii(query.simpleLiteralTerm) && query.simpleLiteralTerm.size() >= 3;
}
ffsearch::CandidateView MakeView(const Entry& entry) {
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    const std::u16string_view name(reinterpret_cast<const char16_t*>(entry.name.data()), entry.name.size());
    return {name, {}, 0, 0, false, 0, 1, entry.id, 0, true, IsAsciiLower(entry.name)};
}
std::vector<EntryId> CanonicalMatches(const std::vector<Entry>& entries, const ffsearch::Query& query) {
    std::vector<EntryId> result;
    for (const Entry& entry : entries) {
        if (entry.active && query.MatchesView(MakeView(entry))) result.push_back(entry.id);
    }
    return result;
}
bool SameIds(std::vector<EntryId> left, std::vector<EntryId> right) {
    std::sort(left.begin(), left.end());
    std::sort(right.begin(), right.end());
    return left == right;
}
size_t CountFalseNegatives(std::vector<EntryId> candidates, const std::vector<EntryId>& canonical) {
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    size_t missing = 0;
    for (const EntryId id : canonical) {
        if (!std::binary_search(candidates.begin(), candidates.end(), id)) ++missing;
    }
    return missing;
}
uint64_t PackGram(std::u16string_view value, size_t offset) {
    return static_cast<uint64_t>(value[offset]) |
           (static_cast<uint64_t>(value[offset + 1]) << 16) |
           (static_cast<uint64_t>(value[offset + 2]) << 32);
}
std::vector<uint64_t> UniqueGrams(std::u16string_view value) {
    std::vector<uint64_t> result;
    if (value.size() < 3) return result;
    result.reserve(value.size() - 2);
    for (size_t index = 0; index + 2 < value.size(); ++index) result.push_back(PackGram(value, index));
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
struct IndexStats {
    uint64_t estimatedMemoryBytes = 0;
    uint64_t estimatedPersistentBytes = 0;
    size_t postingCount = 0;
    size_t postingReferences = 0;
};
class LinearIndex {
public:
    void Build(const std::vector<Entry>&) {}
    void Add(EntryId, std::wstring_view) {}
    void Remove(EntryId, std::wstring_view) {}
    void Rename(EntryId, std::wstring_view, std::wstring_view) {}
    std::vector<EntryId> Candidates(const ffsearch::Query&, const std::vector<Entry>& entries, bool) const {
[599 more lines in file. Use offset=260 to continue.]
read: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
grep: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/ben...
# index-engine (delta): durable cold-search candidate generation and hybrid residency
Proposal-stage acceptance invariants for `durable-cold-search-index-and-
hybrid-residency`. These requirements pin the correctness and resource
contract of whatever alternative the campaign's measurement phase selects;
they deliberately do not prescribe the implementation (FTS table, durable
trigram postings, hybrid residency, or another measured alternative).
## ADDED Requirements
### Requirement: Cold candidate generation shall be a conservative superset under Windows ordinal folding
Any durable candidate structure consulted by the cold path SHALL return a
conservative superset of every row the canonical matcher would accept under
`CompareStringOrdinal(..., TRUE)` host semantics, including rows whose names
contain units the host uppercase table equates with ASCII (the fold-exceptional
class) and rows containing surrogate units. Where a candidate generator cannot
prove this property structurally, the engine SHALL maintain a conservative
exception side-set equivalent to the in-memory accelerator's
fold-exceptional/surrogate handling, derived from the host table and fail-closed
on API error.
#### Scenario: Fold-exceptional name is not dropped by the candidate generator
- **WHEN** a query whose folded needle matches a fold-exceptional name (for
  example a name containing a unit the host table folds to ASCII) executes on
  the cold path with the durable candidate structure available
- **THEN** the candidate set contains that row
- **AND** the final result set is identical to the canonical scan's result set
#### Scenario: Generator failure falls back to the canonical scan
- **WHEN** the candidate structure is missing, stale, or cannot establish
  superset coverage for a query shape
- **THEN** the engine serves the query from the canonical cold scan
- **AND** results remain correct (possibly slower)
### Requirement: Cold-with-index results shall equal hot results
For every query class, results served with durable-index assistance on the
cold path SHALL be identical in identity and ordering to results served by the
hot projection path. This extends the pinned hot/cold equivalence contract to
the durable generator.
#### Scenario: Differential identity across scales
- **WHEN** the same corpus and query set are executed hot and cold-with-index
  at 200k, 1M, 2M, and 5M rows
- **THEN** result identities and orderings match exactly for all query classes
### Requirement: Index updates shall be crash-consistent with the entries table
The durable candidate structure SHALL never disagree with the entries table
after a process or machine crash. Updates SHALL be transactionally coupled to
entry mutations or carry a validity/version marker that forces rebuild-on-open
after an unclean shutdown.
#### Scenario: Kill during ingestion batch
- **WHEN** the process is terminated in the middle of an ingestion or USN
  batch that mutates both entries and the candidate structure
- **THEN** after restart the index is either fully consistent with the entries
  recorded at the last committed transaction or is rebuilt before use
- **AND** no query observes candidates for rows that do not exist
### Requirement: Schema migration and rollback shall be measured and reversible
Any schema addition SHALL be versioned and additive, with a measured migration
cost at 2M-5M rows, a documented rollback that loses no indexed data, and a
rebuild-on-open fallback when migration is interrupted.
#### Scenario: Interrupted migration
- **WHEN** migration from the previous schema version is interrupted by a crash
- **THEN** the store opens at the previous schema version with all entries
  intact and either resumes or rebuilds the candidate structure
### Requirement: The durable index shall not raise the default memory budget
Planner caches and candidate structures SHALL have documented, bounded memory
footprints. The default 500 MiB projection budget
(`Projection::kDefaultMaximumMemoryBytes`) SHALL NOT be raised by this change;
any future budget change requires its own measured safety case.
#### Scenario: Bounded caches under sustained query load
- **WHEN** sustained query traffic runs against a 5M-row store
- **THEN** documented cache bounds are not exceeded
- **AND** private bytes show no monotonic growth across query cycles
### Requirement: Planner selection shall be cost-based with canonical fallback
The engine SHALL choose between durable-index candidate generation and the
canonical scan by measured cost rules (needle eligibility, candidate cap,
structure availability), preserving the existing fallback-to-scan behavior for
ineligible query shapes.
#### Scenario: Broad query keeps the canonical scan
- **WHEN** a broad or ineligible query shape executes
- **THEN** the planner does not force candidate generation that exceeds its cap
- **AND** the canonical scan serves the query with correct results
bench_large_index_scalability.cpp-488-     std::string windowsVersion;
bench_large_index_scalability.cpp-489-     std::string compiler;
bench_large_index_scalability.cpp-490-     std::string buildConfiguration;
bench_large_index_scalability.cpp-491-     std::string databaseStorage;
bench_large_index_scalability.cpp-492- };
bench_large_index_scalability.cpp-493- 
bench_large_index_scalability.cpp:494: struct QueryCase {
bench_large_index_scalability.cpp-495-     const char* name;
bench_large_index_scalability.cpp-496-     const wchar_t* query;
bench_large_index_scalability.cpp-497-     uint8_t sortField;
bench_large_index_scalability.cpp-498-     uint32_t maxResults = 10000;
bench_large_index_scalability.cpp-499- };
bench_large_index_scalability.cpp-500- 
grep: C:/Users/Michael Roy/Documents/FastFiles/src/search/inclu...
ls: C:/Users/Michael Roy/Documents/FastFiles/tests/search
Search.h-11- 
Search.h-12- namespace ffsearch {
Search.h-13- 
Search.h:14: enum class SearchScope { CurrentFolder, CurrentFolderAndSubfolders, CurrentDrive, AllIndexedLocations };
Search.h-15- enum class MatchTier { Exact, Prefix, Substring, PathOnly };
Search.h-16- // indexed-search-allocated-size D6: `AllocatedSize` (display label "Size on
Search.h-17- // Disk") is appended so every pre-existing value keeps its exact numeric
Search.h-12- namespace ffsearch {
Search.h-13- 
Search.h-14- enum class SearchScope { CurrentFolder, CurrentFolderAndSubfolders, CurrentDrive, AllIndexedLocations };
Search.h:15: enum class MatchTier { Exact, Prefix, Substring, PathOnly };
Search.h-16- // indexed-search-allocated-size D6: `AllocatedSize` (display label "Size on
Search.h-17- // Disk") is appended so every pre-existing value keeps its exact numeric
Search.h-18- // meaning on the engine/UI wire and in the UI sort selector. `Size` remains
Search.h-17- // Disk") is appended so every pre-existing value keeps its exact numeric
Search.h-18- // meaning on the engine/UI wire and in the UI sort selector. `Size` remains
Search.h-19- // logical size.
Search.h:20: enum class SortField { Relevance, Filename, Path, Size, Modified, Created, AllocatedSize };
Search.h-21- 
Search.h-22- struct SearchRequest {
Search.h-23-     Query query;
bench_literal_matcher.cpp
CMakeLists.txt
test_ascii_fold_matcher.cpp
test_history.cpp
test_ondisk_filter.cpp
test_query.cpp
test_search_admission.cpp
test_search.cpp
grep: C:/Users/Michael Roy/Documents/FastFiles/tests/search/tes...
read: C:/Users/Michael Roy/Documents/FastFiles/tests/indexstore...
test_search_admission.cpp:1: // performance-closure-and-multisurface-acceleration 7.3: bounded top-N
test_search_admission.cpp:2: // admission property test.
test_search_admission.cpp:4: // The admission may only change WHETHER a hopeless row is materialized, never
test_search_admission.cpp:5: // WHICH candidates are accepted. For every sort field and direction, gating
test_search_admission.cpp:6: // Add on AdmitView (skipping when it reports the row cannot enter the bounded
test_search_admission.cpp:7: // set) must produce exactly the reference accumulator's results, and
test_search_admission.cpp:8: // AddViewMatched (the caller already evaluated the query predicates on the
test_search_admission.cpp:9: // view) must produce exactly the same results as Add.
[pi-lens: read widened to the enclosing function_definition "function_definition" (symbol boundary): you asked for lines 236-320, this shows lines 236-324. Re-request with limit > 100 for the exact range.]
void TestAsciiNamePrefilterIsConservativeAndPrecise() {
    ffindexstore::Store store;
    const std::string path = FreshDbPath("ffindexstore_ascii_name_prefilter.db");
    Check(store.Open(path), "prefilter store opens");
    const auto volume = store.GetOrCreateVolume({{}, 505});
    Check(volume.has_value(), "prefilter volume is created");
    std::u16string emoji = u"emoji-";
    emoji.push_back(static_cast<char16_t>(0xD83D)); // U+1F600 pair
    emoji.push_back(static_cast<char16_t>(0xDE00));
    emoji += u"tailfrag.txt";
    Check(store.ApplyBatch(*volume, {
        {ffindexstore::EntryChangeKind::Upsert, MakeEntry(5, 5, u".")},
        {ffindexstore::EntryChangeKind::Upsert, MakeEntry(100, 5, u"alpha-report.txt", 4)},
        {ffindexstore::EntryChangeKind::Upsert, MakeEntry(101, 5, u"ALPHA-REPORT.TXT", 4)},
        {ffindexstore::EntryChangeKind::Upsert, MakeEntry(102, 5, u"\u017Foo.txt", 4)},
        {ffindexstore::EntryChangeKind::Upsert, MakeEntry(103, 5, u"\u6771\u4EAC_target.txt", 4)},
        {ffindexstore::EntryChangeKind::Upsert, MakeEntry(104, 5, emoji, 4)},
        {ffindexstore::EntryChangeKind::Upsert, MakeEntry(105, 5, u"100%_done.txt", 4)},
        {ffindexstore::EntryChangeKind::Upsert, MakeEntry(106, 5, u"a_b.txt", 4)},
        {ffindexstore::EntryChangeKind::Upsert, MakeEntry(107, 5, u"axb.txt", 4)},
        {ffindexstore::EntryChangeKind::Upsert, MakeEntry(108, 5, u"a\\b.txt", 4)},
        {ffindexstore::EntryChangeKind::Upsert, MakeEntry(109, 5, u"plain.bin", 4)},
    }), "prefilter fixture commits");
    auto reader = store.OpenReadSession();
    Check(reader != nullptr && reader->IsOpen(), "prefilter read session opens");
    if (!reader || !reader->IsOpen()) return;
    // Ids 102/103/104 carry non-ASCII units and are admitted for EVERY
    // needle (the conservative GLOB disjunct); the class is named once here.
    const std::vector<uint64_t> nonAsciiClass{102, 103, 104};
    auto collect = [&](const std::u16string& needleUtf16) {
        std::string needle;
        needle.reserve(needleUtf16.size()); // ASCII only
        for (const char16_t ch : needleUtf16) needle.push_back(static_cast<char>(ch));
        std::vector<uint64_t> ids;
        const bool streamed = reader->ForEachEntryWithAsciiNameUntil(*volume, needle,
            [&](const ffindexstore::EntryRecord& entry) {
                ids.push_back(entry.id.low);
                return true;
            });
        Check(streamed, "prefiltered stream completes");
        std::sort(ids.begin(), ids.end());
        return ids;
    };
    auto contains = [](const std::vector<uint64_t>& ids, const uint64_t id) {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    };
    {
        const auto ids = collect(u"alpha");
        Check(contains(ids, 100) && contains(ids, 101),
              "ASCII case variants both stream (LIKE folds ASCII case exactly)");
    }
    {
        const auto ids = collect(u"soo");
        Check(contains(ids, 102),
              "the long-s name streams for an ASCII needle (host-fold conservative admission)");
    }
    {
        const auto ids = collect(u"target");
        Check(contains(ids, 103), "a CJK name streams when its ASCII tail matches");
    }
    {
        const auto ids = collect(u"tailfrag");
        Check(contains(ids, 104), "a surrogate-pair name streams when its ASCII tail matches");
    }
    {
        const auto ids = collect(u"100%");
        Check(contains(ids, 105), "a percent needle matches its literal occurrence (escaping)");
    }
    {
        const auto ids = collect(u"a_b");
        Check(contains(ids, 106), "an underscore needle matches its literal occurrence");
        Check(!contains(ids, 107),
              "an underscore needle is NOT a one-char wildcard (escaped LIKE)");
    }
    {
        const auto ids = collect(u"a\\b");
        Check(contains(ids, 108),
              "a backslash needle matches its literal occurrence (escaped LIKE, no under-match)");
    }
    {
        // A no-match ASCII needle streams exactly the conservative
        // non-ASCII class and nothing else (documents the bounded overmatch).
        const auto ids = collect(u"zzz");
        Check(ids == nonAsciiClass,
              "a no-match needle streams only the conservative non-ASCII class");
    }
}
[13 more lines in file. Use offset=325 to continue.]
grep: C:/Users/Michael Roy/Documents/FastFiles/src/search/src/S...
grep: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src
Search.cpp:52: MatchTier ClassifyMatch(const Candidate& candidate, std::wstring_view term) {
Search.cpp:78: bool ResultLess(const SearchResult& left, const SearchResult& right,
Search.cpp:126: void SortResults(std::vector<SearchResult>& results, SortField field, bool descending) {
Search.cpp:148: bool SearchAccumulator::AdmitView(const CandidateView& candidate) {
Search.cpp:216: bool SearchAccumulator::AddImpl(Candidate candidate, const CandidateMaterializer& materialize,
Search.cpp:264: SearchResponse SearchAccumulator::Finish() {
IndexPipeline.h:372:     ffprotocol::IndexedSearchResponse SearchIndexed(const ffprotocol::IndexedSearchRequest& request,
IndexPipeline.cpp:2197: ffprotocol::IndexedSearchResponse IndexPipeline::SearchIndexed(
Main.cpp:262:         return indexPipeline.SearchIndexed(request, nullptr, cancelled);
read: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/M...
[pi-lens: read widened to the enclosing function_definition "function_definition" (symbol boundary): you asked for lines 235-289, this shows lines 127-357. Re-request with limit > 100 for the exact range.]
int wmain() {
#ifndef _DEBUG
    if (!ffsetup::ArePinnedSignaturesConfigured()) {
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                      ffprotocol::DiagnosticCode::FFDIAG_AUTHENTICODE_PINS_UNCONFIGURED,
                                      {}, {}, L"authenticode-pins-unconfigured", ERROR_INVALID_DATA, 0});
        std::fwprintf(stderr,
                      L"FastFilesEngine: refusing non-debug startup because Authenticode pins are not provisioned.\n");
        return 1;
    }
#endif
    const std::wstring ownPath = GetOwnPath();
    const std::wstring installDir = DirectoryOf(ownPath);
    DWORD sessionId = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);
    ffengine::UiServer uiServer;
    if (!uiServer.Start(sessionId)) {
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                      ffprotocol::DiagnosticCode::FFDIAG_ENGINE_UI_SERVER_START_FAILED,
                                      ownPath, {}, L"engine-ui-server-start-failed", GetLastError(), 0});
        return 1;
    }
    // Task 4.1: idempotently (re-)ensure the per-user Scheduled Task
    // registration is current -- best-effort; the installer is the
    // primary place this happens (task 6.1), but self-healing here means
    // a manually-copied or repaired install still ends up registered.
    // Keep this after UiServer::Start: Task Scheduler COM registration can
    // take several seconds on a cold desktop, and the UI control pipe must
    // become reachable immediately so the application does not spend its
    // bounded connection window retrying a helper that is already running.
    ffsetup::RegisterEngineScheduledTask(ownPath);
    ffengine::IndexPipeline indexPipeline;
    const std::string dbPathUtf8 = GetIndexDatabasePathUtf8();
    if (!dbPathUtf8.empty()) {
        // index-durability-false-success-closure D6 (PA-006): the
        // delete-and-rebuild fallback is reserved for a *proven* corrupt
        // verdict. A failed open whose integrity check could not reach a
        // verdict -- an exclusive lock held by a lingering process, an
        // I/O hiccup, BUSY/LOCKED/NOMEM/IOERR -- is retried a bounded
        // number of times with the file preserved; destroying a healthy
        // index because a check was inconclusive is the failure this
        // closes. Corruption remains fatal exactly as before (task 1.7).
        constexpr int kOpenAttempts = 3;
        constexpr DWORD kOpenRetryDelayMs = 500;
        bool opened = false;
        for (int attempt = 0; attempt < kOpenAttempts && !opened; ++attempt) {
            if (attempt > 0) {
                Sleep(kOpenRetryDelayMs);
            }
            bool integrityFailed = false;
            opened = indexPipeline.Open(dbPathUtf8, &integrityFailed);
            if (!opened && integrityFailed) {
                ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::DatabaseError,
                                              ffprotocol::DiagnosticCode::FFDIAG_CORRUPT_DB,
                                              {}, {}, L"index-database-corrupt", ERROR_INVALID_DATA, 0});
                // Task 1.7's defined fallback: an existing database that fails
                // its integrity check is never trusted -- delete it (plus its
                // WAL/SHM siblings) and reopen a fresh, empty store, which the
                // normal scan/reconciliation machinery then rebuilds from a
                // fresh scan.
                std::fwprintf(stderr,
                              L"FastFilesEngine: index database at %hs failed its integrity check -- deleting it and rebuilding from a fresh scan\n",
                              dbPathUtf8.c_str());
                indexPipeline.Close();
                DeleteIndexDatabaseFiles(dbPathUtf8);
                opened = indexPipeline.Open(dbPathUtf8);
                break; // the fresh-reopen result is terminal: never delete again
            }
            if (!opened && attempt + 1 < kOpenAttempts) {
                ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::DatabaseError,
                                              ffprotocol::DiagnosticCode::FFDIAG_DB_INTEGRITY_INCONCLUSIVE,
                                              {}, {}, L"index-database-open-inconclusive-retry", ERROR_RETRY, 0});
            }
        }
        if (!opened) {
            ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::DatabaseError,
                                          ffprotocol::DiagnosticCode::FFDIAG_DATABASE_OPEN_FAILED,
                                          {}, {}, L"index-database-open-failed", ERROR_OPEN_FAILED, 0});
            // An unopenable durable store (for reasons other than
            // integrity, or if the delete-and-reopen fallback itself
            // failed) is not fatal to the process -- the engine still
            // serves degraded-mode browsing; scanning simply has nowhere
            // durable to persist to until the next restart finds a
            // healthy database (e.g. after the file is manually removed).
            std::fwprintf(stderr, L"FastFilesEngine: failed to open index database at %hs -- indexing disabled this session\n",
                          dbPathUtf8.c_str());
        }
    }
    ffengine::IdleManager idleManager;
    ffengine::PrivilegedConnection privilegedConnection;
    ffengine::VolumeSessionManager volumeSessions(indexPipeline, privilegedConnection);
    // The engine owns applying indexing decisions. Reload is notify-then-pull:
    // settings.json is atomically written by the UI before this message.
    uiServer.onReloadIndexingConfig = [&volumeSessions] {
        volumeSessions.ReloadConfiguration(ffprotocol::LoadSettings(false).indexing);
    };
    uiServer.onRequestUnavailableVolumes = [&indexPipeline] {
        std::vector<ffprotocol::UnavailableVolumeRecord> records;
        for (const auto& metadata : indexPipeline.GetAllVolumes()) {
            if (metadata.available) {
                continue;
            }
            ffprotocol::UnavailableVolumeRecord record{};
            record.volumeRowId = metadata.rowId;
            std::memcpy(record.volumeGuid, metadata.key.volumeGuid.data(), metadata.key.volumeGuid.size());
            record.serialNumber = metadata.key.serialNumber;
            record.entryCount = metadata.entryCount;
            records.push_back(record);
        }
        return records;
    };
    uiServer.onForgetUnavailableVolume = [&indexPipeline](int64_t volumeRowId) {
        const auto metadata = indexPipeline.GetVolumeMetadata(volumeRowId);
        if (!metadata) {
            return ffprotocol::ForgetUnavailableVolumeStatus::NotFound;
        }
        if (metadata->available) {
            return ffprotocol::ForgetUnavailableVolumeStatus::VolumeAvailable;
        }
        return indexPipeline.ForgetVolume(volumeRowId)
            ? ffprotocol::ForgetUnavailableVolumeStatus::Removed
            : ffprotocol::ForgetUnavailableVolumeStatus::Failed;
    };
    uiServer.onRequestFolderAggregate = [&indexPipeline](ffindexstore::VolumeRowId volumeId, ffindexstore::FileId parentFrn) -> std::optional<ffindexstore::Projection::FolderAggregate> {
        return indexPipeline.GetFolderAggregate(volumeId, parentFrn);
    };
    uiServer.onRequestIndexedSearch = [&indexPipeline](const ffprotocol::IndexedSearchRequest& request,
                                                       const ffengine::UiServer::SearchCancellation& cancelled) {
        return indexPipeline.SearchIndexed(request, nullptr, cancelled);
    };
    uiServer.onRequestVolumeStatus = [&volumeSessions] {
        return volumeSessions.CollectVolumeStatus();
    };
    uiServer.onSetIndexingPaused = [&volumeSessions](uint8_t scope, bool paused) {
        volumeSessions.SetIndexingPaused(scope, paused);
    };
    const auto initialSettings = ffprotocol::LoadSettings(false);
    volumeSessions.ReloadConfiguration(initialSettings.indexing);
    uiServer.onActivity = [&idleManager] { idleManager.NotifyActivity(); };
    volumeSessions.SetSnapshotReadyCallback([&uiServer](ffindexstore::VolumeRowId,
                                                          ffprotocol::PooledSnapshotMap directories) {
        uiServer.MergeIndexDirectories(std::move(directories));
    });
    volumeSessions.SetVolumeUnavailableCallback(
        [&uiServer](ffindexstore::VolumeRowId, wchar_t driveLetter) { uiServer.RemoveVolumeDirectories(driveLetter); });
    volumeSessions.Start();
    // task 3.1/3.2: rebuild every already-known volume from the durable
    // store before the privileged connection even comes up, so a UI
    // window opened immediately at launch still sees whatever was
    // already persisted from a prior session, per volume, as soon as
    // that volume's rebuild completes.
    indexPipeline.RebuildAll([&uiServer, &indexPipeline](ffindexstore::VolumeRowId volumeId) {
        auto meta = indexPipeline.GetVolumeMetadata(volumeId);
        if (!meta || !meta->available) {
            return;
        }
        if (!meta->scanComplete) {
            // close-independent-validation-gaps 4.6: an interrupted initial
            // scan is rebuilt into the projection for durable correctness,
            // but its complete snapshot export is deferred. Exporting a
            // large incomplete volume synchronously here would block the
            // privileged connection from starting and delay cursor resume;
            // VolumeSessionManager forces the current view at ScanComplete.
            return;
        }
        const wchar_t driveLetter = ffengine::ResolveDriveLetterForVolumeKey(meta->key);
        if (driveLetter == L'\0') {
            return; // not currently mounted -- nothing to publish under yet
        }
        std::wstring prefix;
        prefix.push_back(driveLetter);
        prefix.push_back(L':');
        // memory-bounded-indexed-scale 5.1: bounded top-of-tree working-set
        // export at startup rebuild (see VolumeSessionManager scan-complete
        // publication for the same contract).
        uiServer.MergeIndexDirectories(indexPipeline.ExportDirectorySnapshotBoundedPooled(
            volumeId, prefix, ffengine::IndexPipeline::kInitialIndexedExportDirectories));
    });
    privilegedConnection.Start(installDir, [&uiServer, &volumeSessions](ffengine::ConnectionState state,
                                                                         ffengine::UnavailableReason reason) {
        // Keep the privileged-path failure stage visible in the metadata-only
        // diagnostic log. The UI intentionally exposes only degraded mode;
        // support and verification need to distinguish service absence from
        // mutual-authentication failure without logging peer contents.
        if (state == ffengine::ConnectionState::Disconnected && reason != ffengine::UnavailableReason::None) {
            const std::wstring detail = std::wstring(L"privileged-connection-")
                + ConnectionStateName(state) + L"/" + UnavailableReasonName(reason);
            ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                          ffprotocol::DiagnosticCode::FFDIAG_ENGINE_CONNECT_FAILED,
                                          {}, {}, detail, ERROR_INVALID_DATA, 0});
        }
        uiServer.SetEngineStatus(state == ffengine::ConnectionState::Active);
        volumeSessions.OnConnectionStateChanged(state);
    });
    idleManager.Start(
        kIdleTimeout,
        [&indexPipeline] {
            const auto settings = ffprotocol::LoadSettings(false);
            // address-engineering-audit-findings 5.2: the idle callback is
            // the only production path allowed to attempt expensive VACUUM.
            indexPipeline.RunStoreMaintenance(true, settings.vacuumIntervalHours,
                                               settings.vacuumSizeGrowthPercent);
        },
        [&privilegedConnection] { privilegedConnection.DropForIdle(); },
        [&privilegedConnection] { privilegedConnection.RequestReconnect(); });
    // This process has no message loop or console of its own -- it's a
    // per-session background helper launched by the Scheduled Task (or
    // lazily by the UI); it simply runs until the logon session ends.
    HANDLE parkEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (parkEvent == nullptr) {
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                      ffprotocol::DiagnosticCode::FFDIAG_ENGINE_PARK_EVENT_FAILED,
                                      ownPath, {}, L"engine-park-event-failed", GetLastError(), 0});
        return 1;
    }
    WaitForSingleObject(parkEvent, INFINITE);
    return 0;
}
[1 more lines in file. Use offset=358 to continue.]
grep: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src
IndexPipeline.cpp-2195- }
IndexPipeline.cpp-2196- 
IndexPipeline.cpp-2197- ffprotocol::IndexedSearchResponse IndexPipeline::SearchIndexed(
IndexPipeline.cpp-2198-     const ffprotocol::IndexedSearchRequest& request, SearchTimings* timings,
IndexPipeline.cpp:2199:     const SearchCancellation& cancelled) {
IndexPipeline.cpp-2200-     return SearchIndexedReadPlane(request, timings, cancelled);
IndexPipeline.cpp-2201- }
IndexPipeline.cpp-2202- 
IndexPipeline.cpp-2203- } // namespace ffengine
Main.cpp-257-     uiServer.onRequestFolderAggregate = [&indexPipeline](ffindexstore::VolumeRowId volumeId, ffindexstore::FileId parentFrn) -> std::optional<ffindexstore::Projection::FolderAggregate> {
Main.cpp-258-         return indexPipeline.GetFolderAggregate(volumeId, parentFrn);
Main.cpp-259-     };
Main.cpp-260-     uiServer.onRequestIndexedSearch = [&indexPipeline](const ffprotocol::IndexedSearchRequest& request,
Main.cpp:261:                                                        const ffengine::UiServer::SearchCancellation& cancelled) {
Main.cpp-262-         return indexPipeline.SearchIndexed(request, nullptr, cancelled);
Main.cpp-263-     };
Main.cpp-264-     uiServer.onRequestVolumeStatus = [&volumeSessions] {
Main.cpp-265-         return volumeSessions.CollectVolumeStatus();
IndexPipeline.h-367-     // navigate a result without requiring the entire index over IPC. The
IndexPipeline.h-368-     // projection lock is held while copying lightweight candidate data;
IndexPipeline.h-369-     // matching and sorting happen after that copy, and lazy folder
IndexPipeline.h-370-     // reconstruction reacquires the lock only for path-dependent survivors.
IndexPipeline.h:371:     using SearchCancellation = std::function<bool()>;
IndexPipeline.h-372-     ffprotocol::IndexedSearchResponse SearchIndexed(const ffprotocol::IndexedSearchRequest& request,
IndexPipeline.h-373-                                                     SearchTimings* timings = nullptr,
IndexPipeline.h-374-                                                     const SearchCancellation& cancelled = {});
IndexPipeline.h-375- 
IndexPipeline.h-370-     // reconstruction reacquires the lock only for path-dependent survivors.
IndexPipeline.h-371-     using SearchCancellation = std::function<bool()>;
IndexPipeline.h-372-     ffprotocol::IndexedSearchResponse SearchIndexed(const ffprotocol::IndexedSearchRequest& request,
IndexPipeline.h-373-                                                     SearchTimings* timings = nullptr,
IndexPipeline.h:374:                                                     const SearchCancellation& cancelled = {});
IndexPipeline.h-375- 
IndexPipeline.h-376- private:
IndexPipeline.h-377-     enum class QueryMutationReason : uint8_t {
IndexPipeline.h-378-         EntryMutation,
UiServer.cpp-222-     const auto cancellation = std::make_shared<std::atomic<bool>>(false);
UiServer.cpp-223-     {
UiServer.cpp-224-         std::lock_guard<std::mutex> lock(indexedSearchMutex_);
UiServer.cpp-225-         if (indexedSearchStopping_) return;
UiServer.cpp:226:         if (activeIndexedSearchCancellation_ != nullptr) {
UiServer.cpp-227-             activeIndexedSearchCancellation_->store(true, std::memory_order_release);
UiServer.cpp-228-         }
UiServer.cpp-229-         if (pendingIndexedSearch_.has_value()) {
UiServer.cpp-230-             pendingIndexedSearch_->cancellation->store(true, std::memory_order_release);
UiServer.cpp-223-     {
UiServer.cpp-224-         std::lock_guard<std::mutex> lock(indexedSearchMutex_);
UiServer.cpp-225-         if (indexedSearchStopping_) return;
UiServer.cpp-226-         if (activeIndexedSearchCancellation_ != nullptr) {
UiServer.cpp:227:             activeIndexedSearchCancellation_->store(true, std::memory_order_release);
UiServer.cpp-228-         }
UiServer.cpp-229-         if (pendingIndexedSearch_.has_value()) {
UiServer.cpp-230-             pendingIndexedSearch_->cancellation->store(true, std::memory_order_release);
UiServer.cpp-231-         }
UiServer.cpp-244-             pendingIndexedSearch_->cancellation->store(true, std::memory_order_release);
UiServer.cpp-245-             pendingIndexedSearch_.reset();
UiServer.cpp-246-             cancelled = true;
UiServer.cpp-247-         }
UiServer.cpp:248:         if (activeIndexedSearchRequestId_ == requestId && activeIndexedSearchCancellation_ != nullptr) {
UiServer.cpp-249-             activeIndexedSearchCancellation_->store(true, std::memory_order_release);
UiServer.cpp-250-             cancelled = true;
UiServer.cpp-251-         }
UiServer.cpp-252-     }
UiServer.cpp-245-             pendingIndexedSearch_.reset();
UiServer.cpp-246-             cancelled = true;
UiServer.cpp-247-         }
UiServer.cpp-248-         if (activeIndexedSearchRequestId_ == requestId && activeIndexedSearchCancellation_ != nullptr) {
UiServer.cpp:249:             activeIndexedSearchCancellation_->store(true, std::memory_order_release);
UiServer.cpp-250-             cancelled = true;
UiServer.cpp-251-         }
UiServer.cpp-252-     }
UiServer.cpp-253-     indexedSearchCondition_.notify_one();
UiServer.cpp-257- 
UiServer.cpp-258- void UiServer::ClientConnection::CancelIndexedSearches() {
UiServer.cpp-259-     {
UiServer.cpp-260-         std::lock_guard<std::mutex> lock(indexedSearchMutex_);
UiServer.cpp:261:         if (activeIndexedSearchCancellation_ != nullptr) {
UiServer.cpp-262-             activeIndexedSearchCancellation_->store(true, std::memory_order_release);
UiServer.cpp-263-         }
UiServer.cpp-264-         if (pendingIndexedSearch_.has_value()) {
UiServer.cpp-265-             pendingIndexedSearch_->cancellation->store(true, std::memory_order_release);
UiServer.cpp-258- void UiServer::ClientConnection::CancelIndexedSearches() {
UiServer.cpp-259-     {
UiServer.cpp-260-         std::lock_guard<std::mutex> lock(indexedSearchMutex_);
UiServer.cpp-261-         if (activeIndexedSearchCancellation_ != nullptr) {
UiServer.cpp:262:             activeIndexedSearchCancellation_->store(true, std::memory_order_release);
UiServer.cpp-263-         }
UiServer.cpp-264-         if (pendingIndexedSearch_.has_value()) {
UiServer.cpp-265-             pendingIndexedSearch_->cancellation->store(true, std::memory_order_release);
UiServer.cpp-266-             pendingIndexedSearch_.reset();
UiServer.cpp-280-             });
UiServer.cpp-281-             if (indexedSearchStopping_) return;
UiServer.cpp-282-             task = std::move(*pendingIndexedSearch_);
UiServer.cpp-283-             pendingIndexedSearch_.reset();
UiServer.cpp:284:             activeIndexedSearchCancellation_ = task.cancellation;
UiServer.cpp-285-             activeIndexedSearchRequestId_ = task.request.requestId;
UiServer.cpp-286-         }
UiServer.cpp-287- 
UiServer.cpp-288-         ffprotocol::IndexedSearchResponse response;
UiServer.cpp-286-         }
UiServer.cpp-287- 
UiServer.cpp-288-         ffprotocol::IndexedSearchResponse response;
UiServer.cpp-289-         response.requestId = task.request.requestId;
UiServer.cpp:290:         const SearchCancellation cancellation = [token = task.cancellation] {
UiServer.cpp-291-             return token->load(std::memory_order_acquire);
UiServer.cpp-292-         };
UiServer.cpp-293-         std::optional<SearchCoordinator::Permit> permit;
UiServer.cpp-294-         if (searchCoordinator_ != nullptr) {
UiServer.cpp-307- 
UiServer.cpp-308-         bool stopping = false;
UiServer.cpp-309-         {
UiServer.cpp-310-             std::lock_guard<std::mutex> lock(indexedSearchMutex_);
UiServer.cpp:311:             if (activeIndexedSearchCancellation_ == task.cancellation) {
UiServer.cpp-312-                 activeIndexedSearchCancellation_.reset();
UiServer.cpp-313-                 activeIndexedSearchRequestId_ = 0;
UiServer.cpp-314-             }
UiServer.cpp-315-             stopping = indexedSearchStopping_;
UiServer.cpp-308-         bool stopping = false;
UiServer.cpp-309-         {
UiServer.cpp-310-             std::lock_guard<std::mutex> lock(indexedSearchMutex_);
UiServer.cpp-311-             if (activeIndexedSearchCancellation_ == task.cancellation) {
UiServer.cpp:312:                 activeIndexedSearchCancellation_.reset();
UiServer.cpp-313-                 activeIndexedSearchRequestId_ = 0;
UiServer.cpp-314-             }
UiServer.cpp-315-             stopping = indexedSearchStopping_;
UiServer.cpp-316-         }
UiServer.cpp-351- void UiServer::ClientConnection::Close() {
UiServer.cpp-352-     {
UiServer.cpp-353-         std::lock_guard<std::mutex> lock(indexedSearchMutex_);
UiServer.cpp-354-         indexedSearchStopping_ = true;
UiServer.cpp:355:         if (activeIndexedSearchCancellation_ != nullptr) {
UiServer.cpp-356-             activeIndexedSearchCancellation_->store(true, std::memory_order_release);
UiServer.cpp-357-         }
UiServer.cpp-358-         if (pendingIndexedSearch_.has_value()) {
UiServer.cpp-359-             pendingIndexedSearch_->cancellation->store(true, std::memory_order_release);
UiServer.cpp-352-     {
UiServer.cpp-353-         std::lock_guard<std::mutex> lock(indexedSearchMutex_);
UiServer.cpp-354-         indexedSearchStopping_ = true;
UiServer.cpp-355-         if (activeIndexedSearchCancellation_ != nullptr) {
UiServer.cpp:356:             activeIndexedSearchCancellation_->store(true, std::memory_order_release);
UiServer.cpp-357-         }
UiServer.cpp-358-         if (pendingIndexedSearch_.has_value()) {
UiServer.cpp-359-             pendingIndexedSearch_->cancellation->store(true, std::memory_order_release);
UiServer.cpp-360-             pendingIndexedSearch_.reset();
UiServer.h-101-     std::function<std::vector<ffprotocol::UnavailableVolumeRecord>()> onRequestUnavailableVolumes;
UiServer.h-102-     std::function<ffprotocol::ForgetUnavailableVolumeStatus(int64_t)> onForgetUnavailableVolume;
UiServer.h-103-     using OnRequestFolderAggregate = std::function<std::optional<ffindexstore::Projection::FolderAggregate>(ffindexstore::VolumeRowId, ffindexstore::FileId)>;
UiServer.h-104-     OnRequestFolderAggregate onRequestFolderAggregate;
UiServer.h:105:     using SearchCancellation = std::function<bool()>;
UiServer.h-106-     using OnRequestIndexedSearch = std::function<ffprotocol::IndexedSearchResponse(
UiServer.h-107-         const ffprotocol::IndexedSearchRequest&, const SearchCancellation&)>;
UiServer.h-108-     OnRequestIndexedSearch onRequestIndexedSearch;
UiServer.h-109-     // settings-and-appearance §7.3: supplies the per-volume condition
UiServer.h-103-     using OnRequestFolderAggregate = std::function<std::optional<ffindexstore::Projection::FolderAggregate>(ffindexstore::VolumeRowId, ffindexstore::FileId)>;
UiServer.h-104-     OnRequestFolderAggregate onRequestFolderAggregate;
UiServer.h-105-     using SearchCancellation = std::function<bool()>;
UiServer.h-106-     using OnRequestIndexedSearch = std::function<ffprotocol::IndexedSearchResponse(
UiServer.h:107:         const ffprotocol::IndexedSearchRequest&, const SearchCancellation&)>;
UiServer.h-108-     OnRequestIndexedSearch onRequestIndexedSearch;
UiServer.h-109-     // settings-and-appearance §7.3: supplies the per-volume condition
UiServer.h-110-     // report (VolumeStatusRecords) on demand; the engine derives it from
UiServer.h-111-     // its own connection/scan/reconciliation state. Empty vector == no
UiServer.h-255-         bool closing_ = false;
UiServer.h-256-         std::mutex indexedSearchMutex_;
UiServer.h-257-         std::condition_variable indexedSearchCondition_;
UiServer.h-258-         std::optional<IndexedSearchTask> pendingIndexedSearch_;
UiServer.h:259:         std::shared_ptr<std::atomic<bool>> activeIndexedSearchCancellation_;
UiServer.h-260-         uint64_t activeIndexedSearchRequestId_ = 0;
UiServer.h-261-         bool indexedSearchStopping_ = false;
UiServer.h-262-         std::thread indexedSearchThread_;
UiServer.h-263-     };
## Review
Research lane D complete — read-only; no files touched, no shell used. Deliverables: (1) the full search-path map with file:line evidence, (2) the durable-candidate-generator seam + cost-model, (3) the owned differential-test reference design. This is a research/design deliverable, not a diff review.
---
# 1. Search path map
## 1.1 Canonical matcher (CompareStringOrdinal semantics)
The canonical matcher is `CompareStringOrdinal(<span>, <len>, <span>, <len>, TRUE)` (bIgnoreCase=TRUE, explicit lengths, no terminator dependence) — exactly the semantics named in `openspec/changes/durable-cold-search-index-and-hybrid-residency/specs/index-engine/spec.md`:
| Site | Role |
|---|---|
| `src/search/src/Search.cpp:13-16` `OrdinalEqual`, `18-21` `OrdinalStartsWith`, `35-37` `CompareOrdinal` | scope checks (`InScope` `Search.cpp:23-32`), match-tier classification (`ClassifyMatch` `Search.cpp:52-76`), sort/tie-break (`ResultLess` `Search.cpp:78-124`) |
| `src/search/src/Query.cpp:607` `OrdinalContains` | per-alignment equal-length span compare — the reference semantics the superset proof is stated against |
| `src/search/src/Query.cpp:765` `CompareOrdinal16` (u16↔w wrapper) + `OrdinalContains16Prepared` `Query.cpp:204-224` | view-path literal matcher: pure-ASCII needle **and** `nameIsAscii` haystack take the proven-equivalent ASCII-fold fast path (BMH ≥4 folded units / anchor-fold otherwise); anything else falls to per-alignment `CompareStringOrdinal` |
| `src/search/src/Query.cpp:656-658` `AsciiFoldContains` | non-ASCII/empty needle explicitly falls back to `OrdinalContains` |
| Equivalence proof of the ASCII fast path | `tests/search/test_ascii_fold_matcher.cpp:45` — exhaustive 128×128 unit-pair probe (`ffsearch_ascii_fold_tests`) |
Other `CompareStringOrdinal(..., TRUE)` call sites are path-identity, not match semantics: `src/protocol/include/ffprotocol/WindowsPath.h:14,20`, `src/search/src/Path.cpp:118`, `src/search/src/History.cpp:28`, `src/ui/src/RecentFolders.cpp:64`, `src/ui/src/NavigationWorkspace.cpp:507`, `src/ui/src/FileOperations.cpp:165`, `src/ui/include/ffui/SelectionIdentity.h:57`.
## 1.2 Query classification (how the 12 bench classes map to code paths)
`ParseQuery` (`src/search/src/Query.cpp:680-761`) sets `Query::simpleLiteralName` at `Query.cpp:748-759` **only** when: `viewCompatible && !requiresFolder &&` exactly one predicate of kind `LiteralName` with a non-empty term. `simpleLiteralFolded` is ASCII-lowercase-folded and **empty for any non-ASCII term** (`Query.cpp:753-754`); anchor + BMH shift table built at 755-758. Predicate kinds: `src/search/include/ffsearch/Query.h:82-91`.
The 12 classes live in `tests/engine/bench_large_index_scalability.cpp:494-499` (`QueryCase {name, query, sortField, maxResults=10000}`) and `:2128-2141`:
| # | Class (query, sort) | simpleLiteral? | Hot accelerator (`FilenameCandidateIndices`) | Cold SQL prefilter | Path taken |
|---|---|---|---|---|---|
| 1 | selective `unique_component_7391` (Relevance) | yes, folded 19 ASCII | eligible, posting under cap → **used** | eligible | posting intersection ∪ exceptional, canonical verify |
| 2 | moderate `report` | yes | eligible but candidate set > cap at scale → nullopt | eligible | canonical scan / LIKE stream |
| 3 | broad `common` | yes | cap exceeded → nullopt | eligible | scan (measured floor) |
| 4 | no_match `zzzz_never_present` | yes | missing trigram → empty intersection (`Projection.cpp:960-973`), returns exceptional-set only | eligible | tiny candidate set |
| 5 | prefix `photo` (plain literal — "prefix" is fixture-shape naming, not an operator) | yes | as #2 | eligible | scan/candidates |
| 6 | extension `ext:txt` (sort Modified) | **no** (`PredicateKind::Extension`) | ineligible | ineligible | full scan + extension predicate |
| 7 | worst_substring `component` (Modified) | yes | max-posting adversarial | eligible | cap→scan |
| 8 | exact_name `unique_component_7391.txt` | yes (dotted, trigram-rich) | eligible | eligible | candidates; `ClassifyMatch` Exact tier |
| 9 | short_prefix `co` | yes but folded **2 < 3** | ineligible (`Projection.cpp:949`) | ineligible (`IndexPipeline.cpp:378-379`) | scan |
| 10 | mixed_case `CoMmOn_CoMpOnEnT` | yes | eligible (folded) | eligible | candidates |
| 11 | unicode `список` | non-ASCII → `simpleLiteralFolded` empty | ineligible | ineligible | canonical `CompareStringOrdinal` path |
| 12 | high_limit_broad `common`, maxResults 50000 | yes | cap→scan | eligible | top-N heap sized 50k; `AdmitView` value highest |
## 1.3 Hot planner (projection-resident)
- Contract: `src/indexstore/include/ffindexstore/Projection.h:331-340` — candidate set is a **conservative superset** of canonical matches; slot indices valid only for the current projection generation under its lock.
- Implementation `src/indexstore/src/Projection.cpp:947-1037`:
  - **Eligibility gate** (949): `!filenameIndexValid_ || foldedNeedle.size() < 3 || !IsAscii(foldedNeedle)` → `nullopt` (= "use canonical scan").
  - Trigram construction: `UniqueTrigrams`/`PackTrigram` `Projection.cpp:89-104` (dedup'd sorted).
  - Missing needle trigram ⇒ empty intersection, **not** re-intersection (960-973).
  - **Cap checks** (smallest posting 976-979; each `set_intersection` growth 983-991; final 993-995; merged-with-exceptions 1022-1024) all return `nullopt` rather than a truncated set — fallback is total, never partial.
  - **Fold-exceptional merge** (1011-1021): `set_union` with `filenameExceptionalEntries_`.
  - Any exception (OOM) → `nullopt` (1029-1033).
- **Fold-exceptional machinery** (the correctness heart): `UnitMayFoldToAscii` `Projection.cpp:60-83` — memoized 65,536-slot probe of the **host** `CompareStringOrdinal(&wide,1,&ascii,1,TRUE)` vs all 128 ASCII units; surrogate units (0xD800-0xDFFF) always exceptional (64-65); API failure ⇒ exceptional (fail-closed, 76-78). `NameNeedsExceptionalFallback` 85-90. Maintenance: `AddFilenameIndexEntry` 609-628, remove 630-643, bulk rebuild 645-663 (failure ⇒ `InvalidateFilenameIndex` 603-607), wholesale-drop threshold 256 (`kPerEntryFilenameRemovalThreshold` ~131).
- **Test seam** `SetTestingFoldExceptionalUnit`: declared `Projection.h:314-323`, defined `Projection.cpp:165-172` under `FF_INDEXSTORE_TESTING` (tests/benches only).
- **Consumption / gate**: `src/engine/src/IndexPipeline.cpp:1500-1508` — `filenameEligible = query.simpleLiteralName && simpleLiteralFolded.size() >= 3 && context.coldVolumes.empty()`; then `projection_.FilenameCandidateIndices(simpleLiteralFolded, kMaximumFilenameIndexCandidates)` (1503-1504). Cap `kMaximumFilenameIndexCandidates = 16*1024`, `src/engine/src/IndexPipeline.h:399`.
- Candidate slots are consumed in both hot loops (view-first capture `IndexPipeline.cpp:1672-1678`; owned path `:1802-1808`), but **every candidate is still verified by the canonical matcher** (`accumulator.Add`/`MatchesView`) — the accelerator is never a match oracle.
## 1.4 Cold planner (durable SQLite stream)
- Gate: `ColdNamePrefilterNeedle` `IndexPipeline.cpp:362-383` — `simpleLiteralName && folded ≥ 3 && all-ASCII` (any `ch > 0x7f` → nullopt), needle converted to narrow lowercase.
- Call sites: cold-volume streaming `IndexPipeline.cpp:1887, 1905-1907`; durable fallback `:2017, 2035-2037` — `ForEachEntryWithAsciiNameUntil(...) : ForEachEntryUntil(...)`.
- Implementation `src/indexstore/src/Store.cpp:1210-1260`; SQL (`Store.cpp:1237-1238`): `name LIKE ? ESCAPE '\' OR name GLOB '*[^ -~]*'`. LIKE metacharacters escaped (`LikePatternForLiteralNeedle` `Store.cpp:1190-1203`) so needles like `100%`, `a_b`, `a\b` can only over-match. Superset argument (header contract `Store.h:257-268`): ASCII-only match spans are found by ASCII-folding LIKE; any span through a non-ASCII unit implies non-ASCII UTF-8 bytes ⇒ the GLOB disjunct admits the row unconditionally. False positives reach the canonical C++ matcher unchanged.
- Canonical full stream: `ForEachEntryUntil` `Store.cpp:1160-1188` (cancellable visitor; false return only on DB error).
- Pinned by `tests/indexstore/test_store_read_session.cpp:236-324` `TestAsciiNamePrefilterIsConservativeAndPrecise`.
## 1.5 Top-N admission and sorting
- **Total order** `ResultLess` `Search.cpp:78-124`: primary = requested `SortField` (`Search.h:20`: Relevance/Filename/Path/Size/Modified/Created/AllocatedSize); Relevance = `MatchTier` (`Search.h:15` Exact<Prefix<Substring<PathOnly, per-row `ClassifyMatch` `Search.cpp:52`) → name length → folder length; AllocatedSize sorts unknown **after** known in both directions (D6); tie-break chain folder → volumeId → id makes the order enumeration-source independent (hot vs cold).
- **Bounded heap**: `SearchAccumulator::AddImpl` `Search.cpp:216-263` — `push_heap`/`pop_heap` with root = **worst** (the 7.3 fix, comment 242-250); overflow sets `truncated`; final `SortResults` `Search.cpp:126-131`. Same in `ExecuteSearchCore` 281-330 and `ExecuteSearchViews` 343-430.
- **Conservative pre-admission** `AdmitView` `Search.cpp:148-214`: skips only a *strictly worse* primary key (secondary keys include folder text the view lacks — ties admit); disabled under `candidateFilter`/folder-dependence; `SortField::Path` always admits (211-212). Volume-root rows (`parentFrn == frn`, empty view name vs display `prefix + "\\"`) are never view-admitted — `IndexPipeline.cpp:1770-1778` (completion-review R1).
## 1.6 Stale-query cancellation (epochs/generations)
- **Caller cancel**: `UiServer` per-request `std::atomic<bool>` token, superseded queries flipped (`src/engine/src/UiServer.cpp:222-252, 258-267, 290-292`); polled every `kHotCandidateChunkRows = 256` candidates (`IndexPipeline.h:340-341`; `Search.cpp:222-227`) and per row in cold visitors. Cancel ⇒ `Cancelled` + cleared results, never partial.
- **Epochs**: `queryVisibleEpoch_`, `dependencySetEpoch_`, per-volume data/config epochs (`IndexPipeline.h:436-438`; bumped in `AdvanceQueryVisibleEpochLocked` `IndexPipeline.cpp:400-416`; `ProjectionResidency` bumps for structural change 423-440). Captured at query start (`captureContext` `IndexPipeline.cpp:1405-1515`), validated by `isContextCurrentLocked` 1518-1535 + `markStaleIfChanged` 1580-1591 at the `GenerationValidation` phase 1920-1921.
- **Restart loop**: `for (restart = 0; restart <= kMaxGenerationRestarts /*=3*/ …)` `IndexPipeline.cpp:1540, 1923-1928`; cursor `Stale()` (disturbed layout) also restarts, never truncates (1687-1692, 1812-1817). `lifecycleEpoch_` (Open/Close, 442-444) cancels everything in flight (1310-1313).
- **Durable fallback** `IndexPipeline.cpp:1955-2085`: after 4 stale restarts, ≤ `kMaxDurableFallbackConfigurationRetries+1` (3) attempts stream **all** searchable volumes from one WAL read snapshot (no projection), validating **configuration** epochs only (2055) — data churn is tolerated because the snapshot is consistent. Fallback failure ⇒ `Unavailable` (never a silently partial result).
## 1.7 Fallback ladder (complete)
1. Accelerator ineligible/over-cap → canonical hot cursor scan.
2. Any cold volume in scope (`coldVolumes` non-empty) → accelerator disabled entirely (`IndexPipeline.cpp:1501`); hot rows cursor-walked + cold rows streamed per volume.
3. Cold needle ineligible (<3 / non-ASCII / non-literal) → full `ForEachEntryUntil` stream.
4. Stale thrash > 3 restarts → durable fallback → `Unavailable`.
5. Hot candidate with unresolved parent chain → durable `FindEntry` walk (`MaterializeCandidateFolderDurable` `IndexPipeline.cpp:108-176`); unresolved ⇒ `streamFailed` ⇒ `Unavailable` (a cold row is never silently dropped).
6. Accelerator invalidated (bulk removal / failed rebuild) → nullopt until rebuild; bulk-scan end revives it (`TestBulkScanCycleRevivesFilenameAccelerator` `tests/indexstore/test_projection.cpp:1334`).
---
# 2. Where the durable candidate generator slots in (the seam)
**Seam: `ffindexstore::StoreReadSession`, beside `ForEachEntryWithAsciiNameUntil` (`src/indexstore/include/ffindexstore/Store.h:257-268`, impl `Store.cpp:1210`).** Add one API, e.g. `ForEachCandidateEntryUntil(volumeRowId, asciiFoldedNeedle, maximumCandidates, visitor)` (or `NameCandidateIds(...)` returning rowids), with the *identical contract shape* as the hot generator (`Projection.h:331-336`): conservative superset under host `CompareStringOrdinal(..., TRUE)` semantics (including the fold-exceptional + surrogate side-set, host-derived and fail-closed exactly like `UnitMayFoldToAscii` `Projection.cpp:60-83`), and **refusal (`false`/nullopt) instead of a partial set** — refusal falls to the existing layers.
**Selection points — exactly two, already isolated:** `IndexPipeline.cpp:1905` (cold-volume streaming) and `IndexPipeline.cpp:2035` (durable fallback), behind the existing gate `ColdNamePrefilterNeedle` (`IndexPipeline.cpp:362-383`) promoted to a small `ColdCandidatePlan` decision: **durable index → SQL LIKE prefilter → full stream**, each rung conservative and independently fall-back-able. Crash-consistency must ride `Store::ApplyBatch`'s single transaction (`Store.h` "Entries" section) or carry a validity marker with rebuild-on-open (`specs/index-engine/spec.md`: "Index updates shall be crash-consistent with the entries table").
**Cost-based selection rule must weigh** (each with its code anchor):
1. **Needle length**: eligibility is folded ≥ 3 pure-ASCII (`Projection.cpp:949`, `IndexPipeline.cpp:378-379`, `:1500-1501`); trigram count = len−2 drives posting fetches and intersection cost.
2. **Posting size / candidate count**: smallest posting > cap → refuse (`Projection.cpp:976-979`); intermediate intersection growth → refuse (983-991); final/merged > `kMaximumFilenameIndexCandidates` (16,384, `IndexPipeline.h:399`) → refuse (993-995, 1022-1024). Durable equivalent: estimate candidates from index stats *before* materializing; when estimated candidates ≈ N, sequential streaming beats rowid-random fetch (cold stream floor ≈ 1.05-1.10 µs/row, `durable-cold-search-index…/design.md` §1).
3. **Exception side-set size**: fold-exceptional + surrogate rows are unioned into *every* ASCII candidate set (`Projection.cpp:1011-1021`); if that set alone exceeds the cap the generator is unusable — a durable generator must publish and budget its own side-set.
4. **Index validity**: `filenameIndexValid_` (`Projection.cpp:949`) + generation binding of slot indices (`Projection.h:335-336`); durable equivalent = crash-consistency marker + per-query WAL snapshot binding (`StoreReadSession` is exactly that snapshot).
5. **Volume residency**: hot accelerator only when `coldVolumes.empty()` (`IndexPipeline.cpp:1501`) — a durable generator is precisely what lets cold volumes become candidate-served; mixed sources must each prove superset over their own row domain.
6. **Result limit**: `maxResults` clamped to `kMaxIndexedSearchResultCount` (`IndexPipeline.cpp:1326-1327`); selective needle + small N benefits most; `high_limit_broad` (50k) still benefits from fewer rows visited even though admission can't early-reject.
7. **Sorting / scope / folder predicates**: `SortField::Path`, folder predicates, and rule filters disable `AdmitView` early rejection (`Search.cpp:148-157, 211-212`) and force per-survivor O(depth) path materialization (`MaterializeCandidateFolder*`) — candidate value is *highest* there.
8. **Query class**: only simple literals can be indexed at all (classes 6, 9, 11 in the table are structurally ineligible); multi-term/glob/filter queries get nothing from a name index.
---
# 3. Reference differential-test design (owned)
**Invariants to prove**
- **P1 (completeness):** ∀ rows, queries: `CanonicalMatches(q) ⊆ CandidateSet(q)` — or the generator *refused* (nullopt/fallback). Oracle = the **real production matcher** (`CanonicalOrdinalContains` clone `tests/indexstore/test_projection.cpp:1135-1152`, or `ffsearch::Query::MatchesView`; bench pattern `CanonicalMatches`/`CountFalseNegatives` `tests/engine/bench_secondary_filename_index.cpp:206-232`).
- **P2 (identity + ordering):** cold-with-index results are byte-identical to hot results — full candidate signature incl. ordering and `truncated` (the `signatures` lambda pattern `tests/engine/test_index_pipeline.cpp:1437-1450`).
**Suite A — `tests/indexstore/test_projection.cpp`** (extend the `TestFilenameCandidatesAreConservativeSuperset` family at 1076/1116/1164/1188/1227/1257):
- `TestFoldExceptionalSeamCoverageForLongSKelvinDotlessI` — `SetTestingFoldExceptionalUnit` on U+017F, U+212A, U+0131, U+0130 individually and combined; each name class present in candidates for its ASCII-fold needle. **RED-first** against intersection-only.
- `TestFilenameCandidateSupersetForDottedAndRepeatedTrigramNames` — `a.b.c.d.txt`, `abcabcabcabc.txt`, needles at start/middle/end (repeated-trigram dedup in `UniqueTrigrams` must not weaken intersection).
- `TestFilenameCandidateSupersetForVeryShortLiterals` — folded len 1/2 ⇒ nullopt (never a wrong set); len exactly 3 works; cap 0 vs tiny cap.
- `TestFilenameCandidateCapRefusesNeverTruncates` — needle whose intersection would exceed cap ⇒ nullopt; if uncapped returns S, capped may return S or refuse, never a strict subset containing a missed canonical match.
- `TestFilenameCandidateSupersetUnderMutationChurn` — upsert/rename across exceptionality (long-s → plain), `RemoveBatch` > 256 vs per-entry, `RemoveVolume` with/without accelerator keep (`:996/:1015`).
**Suite B — `tests/indexstore/test_store_read_session.cpp`** (extend `TestAsciiNamePrefilterIsConservativeAndPrecise` `:236`): same exceptional/dotted/repeated/short/mixed corpus through `ForEachEntryWithAsciiNameUntil`, plus an exhaustive differential — every 3..12-char ASCII needle over the fixture asserted `CanonicalOrdinalContains ⊆ streamed ids` (P1 for the SQL rung), incl. LIKE metacharacters and a no-match needle returning exactly the conservative non-ASCII class.
**Suite C — `tests/engine/test_index_pipeline_tests`** (extend the hot/cold identity suites: `TestForcedEvictionSearchEquivalence` `:1276`, `TestCappedRootSearchMatchesColdAndRetainsTopN` `:1477`, `TestColdMutationsAndRestartEquivalence` `:1622`, `TestCancellationDuringColdStream` `:1792`, `TestEnforcementEvictionAdvancesQueryVisibleEpochs` `:2451`, `TestIndexedSearchCancellationStopsProjectionWork` `:941`):
- `TestIndexedVsCanonicalIdentityForAllQueryClasses` — one fixture, the 12 bench classes ported from `bench_large_index_scalability.cpp:2128-2141`, each run in 4 modes: hot+accelerator / hot scan (accelerator suppressed) / evicted cold stream / future cold-with-index. Assert signature equality across all four. Matrix over `SortField` × descending × the 4 `SearchScope` values.
- `TestIndexedIdentityAcrossTiesAndTopN` — rows equal on every primary key (same name diff folders, equal sizes, unknown allocated sizes), `maxResults` < matches, both directions; assert top-N equals full-sort-truncation reference and hot/cold agree (extends the capped-root suite; include volume-root rows `parentFrn == frn`).
- `TestFoldExceptionalIdentityAcrossHotCold` — seam `SetTestingFoldExceptionalUnit(U+017F, true)`, needle `soo`: accelerator path vs cold stream identical. **RED-first** against intersection-only.
- `TestCancellationAndStaleEpochsNeverChangeResults` — cancel at each `SearchPhase` via `SetSearchTestHooks` (`IndexPipeline.h:44-53`): always `Cancelled` + empty, never Completed-with-partial; mid-search `ApplyUsnBatch`/`SetProjectionMemoryBudget` bumping epochs forces ≤ `kMaxGenerationRestarts` restarts then durable fallback, results equal the unchurned reference or honestly `Unavailable`.
- `TestStaleIndexStateFallsBackWithoutFalseNegatives` — invalidate accelerator (bulk removal, `RemoveVolume(..., keepFilenameIndexCurrent=false)`, fault-seam kill-during-update reopen): queries serve from canonical scan with identical results.
- `TestVolumeRootAndPathScopeIdentity` — roots + `CurrentFolder`/`CurrentDrive`/`All` + `SortField::Path` (AdmitView-disabled path) hot vs cold.
**Suite D — scale differential** (the bench matrix, `tests/engine/bench_large_index_scalability.cpp`): add a result-count + ordering-digest per query class to the machine-readable output (embedded `--source-sha`, per lane-E E-8 lesson) and assert **EXACT** equality of counts/digests between indexed and scan modes at 200k/1M/2M/5M (the "scale differential" already practiced in the PR #14 campaign).
**Red-first strategy:** every test is staged against a deliberately-wrong generator first — (a) intersection-only accelerator, (b) prefilter without the GLOB disjunct, (c) candidate set without the exceptional merge, (d) a cap that truncates instead of refusing — demonstrated RED, then GREEN on the correct implementation (the exact P2-A precedent: `TestFilenameCandidatesRetainHostFoldExceptionalNames` `:1188` is documented RED-first). Host-conditional cases (a Windows table where long-s *does* fold) are made deterministic on every host by `SetTestingFoldExceptionalUnit`. `TestHostOrdinalFoldSemanticsMatchCampaignMeasurement` (`:1164`) stays as the tripwire: a future table change fails it loudly while P1/P2 still hold, because the exception set is host-derived and fail-closed (`Projection.cpp:76-78`). Registration: plain `fftest::Check` executables (`tests/TestSupport.h`), added to `tests/indexstore/CMakeLists.txt` / `tests/engine/CMakeLists.txt`; `FF_INDEXSTORE_TESTING` already set for these targets.
---
- Correct: the existing hot/cold correctness machinery is unusually strong — host-derived fail-closed fold-exceptional classification (`Projection.cpp:60-90`), cap-refusal instead of truncation (`Projection.cpp:976-1024`), total-order tie-breaking for enumeration independence (`Search.cpp:106-124`), and the hot/cold signature differential (`test_index_pipeline.cpp:1437-1470`) already pin the invariants the durable generator must inherit.
- Finding: P2 — `docs/agent/WORKSTATE.md:40-50` is the truncated-but-live campaign ledger for exactly this area (P2-A evidence, planner-fix lineage); the differential plan above is deliberately aligned with its RED-first precedent so the future campaign can resume without re-deriving it. (Report-only; no code defect within the review target.)
- Merge verdict: **OK with notes** (research deliverable; no diff to merge. Test commands for any future implementation lane: `ctest --test-dir build/debug -R indexstore --output-on-failure`, `ctest --test-dir build/debug -R ffengine_index_pipeline_tests --output-on-failure`, `ctest --preset debug`.)
