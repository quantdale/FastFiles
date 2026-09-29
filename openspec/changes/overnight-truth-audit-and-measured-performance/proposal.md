## Why

Current `main` is `ea3d64f7c144343ec6faf849143d3f545e6fab36`, and that fact is not the same as a current-head certification. The last recorded executable candidate is `6c40ab35793f1f9c03837f4b4dfa84b5964a032b`, `docs/agent/WORKSTATE.md` still says PR #10 is unmerged, and hosted run `36588334920` for the actual tip failed all six jobs before any step because account payments failed or the spending limit needs to be increased. Optimizing or declaring the repository finished from those labels would repeat the stale-truth failure this campaign exists to stop.

## What Changes

- Add one evidence-gated campaign capability. It requires a durable truth ledger, a fresh recertification of the implementation SHA under test, and a repeatable performance baseline before any speed claim.
- Permit product changes only when current source, a deterministic reproducer, or a same-method before/after measurement proves a locally executable defect or hotspot. A change that does not improve the measured metric is reverted unless it has a separate proven benefit.
- Keep the synchronous raw-volume `DeviceIoControl` completion bound `UNPROVEN`. Do not check `legacy-protocol-correctness-and-raw-volume-cancellation-closure` task 5.4 or `final-residual-hardening-and-hosted-ci-recovery` task 2.3.
- Land the work on `feat/overnight-truth-performance-hardening-20260929` and a reviewable pull request. Do not merge that branch into `main` from this campaign.
- Do not change GitHub billing, spending limits, account plan, branch protection, production signing, or the Windows trust store.

## Capabilities

### New Capabilities

- `current-head-truth-and-measured-performance`: classify live claims, recertify the actual implementation candidate, measure before optimizing, and continue only while the next successor is evidence-backed and locally executable.

### Modified Capabilities

- None. No existing product requirement is changed by this proposal. A later defect may add a delta only after a reproducer or measurement proves the current requirement is wrong.

## Impact

- Campaign evidence lives under this change, not in a second planning system. `docs/agent/WORKSTATE.md` is updated only to replace claims this ledger has classified, starting with the false "PR #10 remains unmerged" checkpoint.
- Recertification uses the existing verifier, CTest, identity, provenance, E2E, UIA, and visual commands. It does not inherit the historical `6c40ab3` matrix.
- Performance work starts from the existing informational benches (`ffengine_bench_large_index_scalability`, `ffengine_bench_secondary_filename_index`, `ffindexstore_bench_projection_memory`, `ffsearch_bench_literal_matcher`) and the advisory `windows-performance-baselines` capability. A new harness is allowed only for a user-visible path those tools do not measure.
- Any later edit to the privileged broker, named pipes, or command surface must preserve `establish-architecture-foundation` decisions D1–D6. This proposal adds no protocol message and no open-path or open-handle command.
- The detached `.kilo/worktrees/scythe-cloud` worktree at `0dc3356` is unrelated and stays untouched.

## Goals

- A reader can tell which historical claims are verified, limited, stale, false, unproven, or externally blocked, with the evidence named.
- Local certification names the implementation SHA embedded in the binaries. A documentation tip is not reported as that candidate.
- No performance improvement is claimed without same-machine, same-method before/after numbers.
- The highest-value locally executable successor is implemented inside this change only when the ledger or baseline proves it. External boundaries stay external.

## Non-goals

- Implementing product code in the proposal session.
- Merging the overnight branch, force-pushing, or rewriting `main`.
- Unmerging PR #10. Its merge at `ea3d64f7c144343ec6faf849143d3f545e6fab36` is recorded; it is not reversed here.
- Proving a finite completion bound for arbitrary synchronous raw-volume `DeviceIoControl`, including by a cooperative fixture or `TerminateThread`.
- Changing billing, branch rules, production signing, or trust-store configuration.
- Calling local CI parity, a zero-step hosted run, or an advisory duration envelope hosted certification or product-performance proof.
- Creating additional OpenSpec changes for speculative lanes.

## Open Questions

- Which user-visible path, if any, lacks a repeatable measurement after the existing benches and performance capability are inventoried? The baseline task answers this; a harness is not added before that inventory.
- Does the fresh current-head matrix fail? If it does, that failure becomes the first implementation successor. If it passes, the performance baseline selects the next successor, or records that no safe local improvement was found.
