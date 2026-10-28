# Native compact palette qualification

Actual safe combined implementation `b0cf4bbf98a053fadddcf785901f1b680356d560` integrates compact8a916999, transfer safety6c924879 and the foreground regression7e37f185 onto07fda936. Exact five diagnostic binary provenance gate PASS. Component equivalence is recorded separately; this is not whole standalone-executable equivalence.

Initial visible run:12combinations(620/1008/1600 x Light/Dark x Compact/Comfortable),36observations, four normal profile cleanups PASS. Actual window DPI120, native physical row47px Compact/55px Comfortable. Query anchor stable on every filter; singleton/no-match lists are exactly one complete row, many-result lists complete rows. UIA edit/list rectangles are contained and non-overlapping, edit focus is retained, query clearing recovers commands, Down/Up retain query focus, no-match Enter inert, Escape closes controls. Explicit unavailable cue remains visible in actual idle cancel captures.

Extended tracked harness adds actual resizing with query open and enabled Home keyboard dispatch. Final44observations/four normal cleanups PASS, same b0cf source. Live resize preserves query/selection/focus and restores anchor. Intentional resize captures may exactly equal earlier same-size/same-query frames; exact ExpectedDuplicateOf and reason are declared only for these states.

Two extension fixture errors are preserved: capture registry first rejects an undeclared identical frame (normal cleanup PASS), then PowerShell rejects the reserved HOME variable (normal cleanup PASS). Corrected explicit duplicate declaration and homeCommandResult variable require no product change. No failure was relabeled PASS.

Before screenshot/valid old native36failures are linked in ../qualification.md. Sparse native list401px→47px Compact (same actual120DPI); visible new no/one/many and both densities/themes match intended native geometry. Read-only independent visual review found no correction needed in clear inspected frames; microphone overlay partially obscures some unreviewed pixels. Warm-tint/intended palette fidelity, physical HC/otherDPI/reduced-motion matrix remain unverified. No global settings or unrelated window was altered.

Full combined release142/142PASS262.73s, UI Analyze23stepsPASS and Debug build103stepsPASS; current combined Debug/E2E and final strict corrected-heading qualification remain pending publication task2.3. The initial combined strict102/103 failure concerns only the PR31 follow-up missing section heading; correction is published separately0fbaee3 and will be integrated after ongoing tests. No benchmark repeats, main/PR merge, signing/release or hosted CI pass claim.

## Later combined observation

On changed combined `90e6cdf4ecc0927672b96bedfb14edefbf966a08`, exact diagnostic provenance and ordinary native E2E8/8PASS0skip, including Search, Storage, file operations and three observed normal cold-restore close/cleanup lifecycles. Same palette source, theme-token and fixture bytes match the isolated implementation; no whole-executable equality is claimed. Previous b0cf ordinary E2E7/8FAIL remains a separate unresolved result: its screenshot showed the restored marker while its Control UIA tree omitted ColumnView. On90e, a targeted same-HWND observation through original/fresh managed and independent native clients saw the ColumnView provider in both first and restored windows and normal cleanup; this does not establish the earlier cause. Sanitized per-scenario outcomes, three close/cleanup observations and client summary are retained. Full portable/Analyze checks on90e remain pending task2.3.

Published draft PR34 on PR25 at d51d5af. Hosted run40646383345 on that head failed before all six jobs executed any steps; the actual annotation reports payment/spending limitation. No rerun or account changes.

## Closure (tasks 2.2/2.3) — 2026-10-28

The previously pending portable/Analyze checks on `90e6cdf` completed:
combined Debug CTest 142/142 PASS (289.18s), release-test CTest 142/142
PASS, affected-UI MSVC Analyze clean, strict OpenSpec 103/103 (corrected
heading lineage) and drift PASS. Ordinary native E2E on the same source:
run 1 7/8 (`preview-terminal-state` intermittent, preserved), focused
preview re-run PASS, full re-run 8/8 PASS on identical binaries — retained
as three separate results. Same palette/theme/fixture source bytes as the
isolated implementation (component equivalence, not whole-executable
identity). Publication: draft PR34 on PR25 at `7583388` (head updated with
the journey record); hosted runs externally blocked (six jobs, zero steps,
payment/spending annotation), inspected once, not rerun.

Task 2.2 is closed on the 36+44 actual native observations (narrow/normal/
wide x Light/Dark x Compact/Comfortable; zero/one/many results; live query
resize; enabled Home dispatch; measured 120 DPI; before/after 401px->47px
frames) plus the bounded independent visual review. Task 2.3 is closed on
the certification above and the unmerged draft publication. Explicitly NOT
claimed: warm-tint palette fidelity, physical High Contrast / other-DPI /
reduced-motion matrix, whole-executable identity, hosted CI pass, or any
merge.
