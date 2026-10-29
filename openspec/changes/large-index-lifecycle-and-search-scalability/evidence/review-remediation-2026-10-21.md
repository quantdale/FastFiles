# Review remediation — PR #14 P2 findings (2026-10-21)

Fixing commit series: `68de2bd` (P2-A + P2-B repairs) and the follow-up
delivery-skew loop fix. Gates at the fixed tree: Debug CTest **128/128**,
release-test CTest **128/128** (the historical 127 grew by the new
`ffclipboard_classifier_tests` target). Focused:
`ffindexstore_projection_tests` (4 new functions), `ffclipboard_classifier_tests`
(16 checks), `ffquick_actions_tests` (10/10 standalone repeats + suites).

## P2-A — "Retain non-ASCII ordinal matches in the candidate set"

**Reproduction attempt = DISPROVEN on this host, defect CLASS closed anyway.**

The finding's concrete claim: `CompareStringOrdinal(..., TRUE)` equates
U+017F LONG S with `s` (via the Unicode uppercase mapping), so the reviewed
intersection-only candidate set would drop `ſoo.txt` for needle `soo`.

Measured ground truth (`build/campaign/probe_ordinal/probe.cpp`, `probe2.cpp`;
raw CSTR_* codes printed):

| comparison (ignoreCase=TRUE) | verdict |
|---|---|
| `A` vs `a` (ASCII sanity) | `CSTR_EQUAL` |
| `É` vs `é` (non-ASCII pair) | `CSTR_EQUAL` (folding is NOT ASCII-only) |
| U+017F vs `s` / `S` | `CSTR_GREATER` — NOT equal |
| U+212A (Kelvin) vs `k` / `K` | `CSTR_GREATER` — NOT equal |
| U+0131 (dotless i) vs `i` / `I` | `CSTR_GREATER` — NOT equal |
| U+0130 (dotted I) vs `i` / `I` | `CSTR_GREATER` — NOT equal |
| U+00DF ß vs `s`; U+FB01 ﬁ vs `f`; U+00B5 µ vs `m`; U+FF21 Ａ vs `A` | `CSTR_GREATER` — NOT equal |
| `ſoo.txt` contains `soo` (production span-compare shape) | **no-match** |

Exhaustive scan: all 65,280 non-ASCII BMP units × all 128 ASCII units =
**zero** cross-ASCII fold classes; surrogate units: zero; sampled astral
pairs vs ASCII pairs: zero. So on this host the pre-fix candidate set already
satisfied `CanonicalMatches ⊆ CandidateSet` and the review example cannot
reproduce. The review's theoretical grounding (Unicode casefold) is not what
`CompareStringOrdinal` does on this Windows build.

**But the class was real**: completeness rested on an unverified assumption
about the host ordinal table. Closed by construction (Option A, host-derived
exceptional fallback):

- `Projection.cpp`: a row is **fold-exceptional** iff its name contains a
  UTF-16 unit that the *host* `CompareStringOrdinal(..., TRUE)` equates with
  any ASCII unit (memoized 65,536-slot probe, fail-closed on API error) or
  any surrogate unit (conservative: pair semantics cannot be probed
  unit-wise). `FilenameCandidateIndices` merges exactly those rows into the
  posting intersection, cap-gated as before.
- Invariant: any canonical match either (a) matches through ASCII-only units
  → its ASCII-folded trigrams are in the name's fold → retained by the
  intersection, or (b) matches through a non-ASCII unit → that unit is
  fold-exceptional → the row is in the merged set. False positives are
  verified away by the authoritative canonical matcher; false negatives are
  impossible for any host ordinal table (unit-wise folds measured; expansion
  folds cannot equate equal-length spans).
- Performance: the exceptional set is empty on this host (all adversarial
  units measured), so the 56–189x selective speedups are preserved bit-for-bit
  in behavior; corpora whose exceptional population exceeds
  `kMaximumFilenameIndexCandidates` fall back to the canonical scan (correct,
  slower) exactly as any cap overflow does.
- Renames for honesty: `filenameNonAsciiEntries_` → `filenameExceptionalEntries_`,
  breakdown `filenameIndexExceptional*`, bench JSON keys
  `filename_index_exceptional_*` (historical evidence files untouched).

**Tests (red-first):**

- `TestFilenameCandidatesRetainHostFoldExceptionalNames` — seam-injected host
  table (U+017F folds to ASCII) reproduces the review's exact scenario; RED
  against the reviewed intersection-only merge (`build/campaign/red-run.log`:
  `FAIL: a host-fold-exceptional long-s name is retained for the ASCII needle`),
  GREEN after the fix. Also covers rename-away and deletion.
- `TestFilenameCandidatesRetainSurrogateNamesConservatively` — RED-first for
  the conservative surrogate rule (same log).
- `TestFilenameCandidateSupersetInvariantHoldsForAdversarialCasing` — 26
  adversarial name/needle pairs (long s, Kelvin, dotted/dotless I, ß, ﬁ/ﬃ,
  fullwidth, µ, ς, Ѕ, Ι, combining diaeresis, Deseret pair, mixed scripts,
  repeated trigrams, mixed-case queries) checked against the real
  `CompareStringOrdinal` span compare, capped and uncapped.
- `TestHostOrdinalFoldSemanticsMatchCampaignMeasurement` — tripwire: fails
  loudly if a future Windows ordinal table changes any recorded verdict
  (production stays correct via the runtime probe; evidence would need
  re-audit).

Seam: `Projection::SetTestingFoldExceptionalUnit` (FF_INDEXSTORE_TESTING only;
`ffindexstore_projection_tests` compiles its own `Projection.cpp` with the
define, same pattern as `ffindexstore_bench_rebuild_isolation`; production
builds contain no seam).

## P2-B — "Fail clipboard round trips that return incorrect data"

**Reproduced and root-caused — the old discriminator AND the old skip reason
were both wrong.**

Raw probes (`build/campaign/probe_clipboard/probe*.cpp`) against the real
product functions established:

1. The historical "session-level read interception (GetClipboardData returns
   null for every format)" is a **late-delivery race**, not interception:
   the first `GetClipboardData` after `SetClipboardData` returns NULL while
   the identical read milliseconds later returns the exact payload
   (`probe5.exe`: `OpenClipboard` succeeds on attempt 1, no holder window,
   first `GetClipboardData(CF_HDROP)` = null, next product read = 2 exact
   paths + correct cut). The old skip condition
   (`IsClipboardFormatAvailable(CF_HDROP)`) proved nothing and swallowed
   every genuine data failure class.
2. Formats can also land with **skew** (observed under CTest: CF_HDROP exact
   while Preferred DropEffect had not landed yet).

**Repair:**

- `tests/ui/ClipboardRoundTripClassifier.h` — deterministic, host-independent
  classifier over raw observations: `Pass` (exact paths AND exact cut intent)
  / `DataWrongOrIncomplete` = FAIL / `EnvironmentInterception` = SKIP (only
  when every advertised format — independent formats included — delivered
  null handles across the whole window while the write stuck) /
  `PublicationUnavailable` = SKIP (refused write/open; the pre-existing
  locked-down-session accommodation) / `Undetermined` = FAIL. An interleaved
  external clipboard change is Undetermined (never a skip).
- `tests/ui/test_clipboard_classifier.cpp` (`ffclipboard_classifier_tests`,
  16 checks) — the mission's 10 negative-control cases (wrong paths;
  malformed HDROP; missing Preferred DropEffect; wrong effect; copy-read-as-
  cut; cut-read-as-copy; confirmed interception; refused open; interleaved
  change; partial read failure) plus non-vacuity proofs that the legacy
  `IsClipboardFormatAvailable` rule misclassified the data-failure cases.
- `test_quick_actions.cpp` real round trip: bounded read window (80 × 25 ms);
  PASS only on exact match; the last delivered observation decides FAIL at
  exhaustion (a persistent wrong/missing/flip delivery fails, never skipped);
  never-delivered runs classify through the raw-handle probes. **The round
  trip now genuinely verifies exact paths + cut intent (3/3 standalone,
  green under both CTest presets) instead of skipping.**

## Gates at the fixed tree

- Debug CTest: **128/128** (`build/campaign/ctest-debug2.log`)
- release-test CTest: **128/128** (`build/campaign/ctest-rt.log`)
- `ffclipboard_classifier_tests`: 16/16 deterministic
- `ffquick_actions_tests`: 10/10 standalone repeats; real clipboard round
  trip PASSes with exact data verification
- `ffindexstore_projection_tests`: all pre-existing + 4 new, red log
  (`build/campaign/red-run.log`, exit 1 with exactly the two expected FAILs)
  vs green log (`build/campaign/green-run.log`, exit 0)
