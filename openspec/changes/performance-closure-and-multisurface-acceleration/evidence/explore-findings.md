# Explore findings — performance closure and multi-surface acceleration

Explore-only. No product C++ was edited. Historical prompt claims were checked against the live repository on 2026-10-01 and are hypotheses where this file does not confirm them.

## Live repository

| item | live value | prompt hypothesis |
|---|---|---|
| `origin/main` | `bdd7d7b5fb91586df957ce43b348c7fb95cbeae3` | same SHA, confirmed |
| feature branch | `feat/measured-performance-acceleration-20260930` at the same SHA, tracking its remote | confirmed |
| local `main` | `3323fcb1e1c2a79a57386a6482e932b3189655e8`, behind `origin/main` by 7 | not "both pointed at the same SHA" locally |
| worktree | clean primary; `.kilo/worktrees/scythe-cloud` detached at `0dc3356` | confirmed unrelated |
| PR #12 | `MERGED` at 2026-09-30T17:32:14Z | confirmed, including the "not for merge yet" body |
| successor branch | `feat/performance-closure-and-multisurface-acceleration-20261001` created from `bdd7d7b` for this proposal | not an implementation branch yet |

`docs/agent/WORKSTATE.md` still opens on `main = 9e2dddc`, PR #11, and "no local action." That checkpoint is stale. It is preserved as history; the propose session replaces only the current resume pointer after this ledger.

## OpenSpec

`openspec list` shows `measured-performance-acceleration-and-exact-certification` in progress, 30/32. Lifecycle state is `active`. Tasks 6.3 and 6.4 are unchecked. Task 6.1 says release-test was 124/124; task 8.1 and the final report say 123/124. The later statement is the honest one. The change has proposal, spec, and tasks, and no `design.md`. `openspec status` therefore reports planning incomplete. This successor does not invent that missing design.

Other changes with unchecked tasks remain in the lifecycle manifest. This proposal does not supersede them. Raw-volume tasks 5.4 and 2.3 stay unchecked.

## Hosted CI

Run `36786432149` is the latest `main` push for `bdd7d7b`. All six jobs (`repository-hygiene-and-contracts`, `asan-protocol-ipc-lifecycle`, `release-test`, `analyze`, `unsigned-release-candidate-qualification`, `debug`) have conclusion failure, zero steps, and null check-run output. The annotations endpoint returned HTTP 404, so a billing sentence is not confirmed for this run from this session. A sibling branch push run `36786437487` failed the same way. Classification for this tip is a zero-step failure, not a repository-test failure. Apply inspects a later tip once and does not inherit a billing label this session could not read.

## Equivalence gate

`verify/qualification/Test-ExecutableTreeEquivalence.ps1` already:

- hashes `<path>\0<blob id>`;
- matches glued and spaced `-I` and `/I`;
- normalizes include roots to repository-relative paths;
- resolves quoted includes relative to the including file;
- fails closed on an unresolved quoted include;
- includes executable CMake inputs.

`evidence/equivalence-gate-validation.json` still records 228 paths, a documentation pass, and a `.cpp` failure. It has no header-only, unresolved-include, or CMake negative row. The final report's appended correction says a synthetic header control was run and coverage rose to 241, but that rerun is not the durable JSON. The code fix and the durable evidence are different facts. Both can be true.

The gate does not hash compiler version, SDK version, environment definitions, or the full command line. That boundary stays in the claim.

## Allocation evidence is three runs, not one number

| record | what it measured | total |
|---|---|---:|
| `allocation-probe.json` baseline | pre-move benchmark, 7 processes | 129.329 ms |
| `allocation-probe.json` short names | 4–5 character names, not pinned A/B | 73.71 ms |
| `allocation-probe.json` no names | names cleared | 66.651 ms |
| `ab-pinned-name-representation-ceiling.json` | pinned interleaved A/B after the move repair | 91.027 ms to 66.097 ms |

The probe note "isolates per-entry `std::wstring` heap allocation" is false. The pinned report's 27.39% is an upper bound because name length and payload size changed together. Do not average these runs. Do not edit the old numbers.

## Product path the next experiment has to hold constant

```text
NamePool interned view
    -> ToWString at IndexPipeline export (two sites)
    -> SnapshotDirectoryEntry::name as std::wstring
    -> length-prefixed snapshot bytes
    -> mapped generation
    -> ParseSnapshot owning map
    -> UI
```

`NamePool` is already an offset/length arena. The first-generation cost under test is the later owning copies and the bytes they cause, not the absence of an intern pool. Packed serialization and in-place row construction were already reverted at about -0.99% and -0.70% total. They are not the next candidate.

Projection `EstimatedMemoryBytes` sums entry capacity, maps, the name pool, and filename-trigram postings. That is an estimator. The historical 329.2 bytes/entry was not remeasured on `bdd7d7b`.

Search already has `FilenameCandidateIndices` for a simple literal of at least three folded characters when the query has no cold volume. A broad or short query may not take that path. The fold-free matcher remains an unproven end-to-end idea.

## What this explore does not decide

- Whether idle-host release-test is 124/124 or the 30-second test is a real defect.
- Whether allocation-only cost exceeds noise once payload bytes match.
- Whether the UIA empty-query failure on AutomationId 7101 reproduces.
- Whether a later hosted run still dies before steps.

Those are apply measurements. The proposal routes them; it does not pre-answer them.

## Proposal consequence

Successor change: `performance-closure-and-multisurface-acceleration`.
Predecessor stays `active` until its own open tasks are closed or explicitly unverified.
No product source change belongs in the propose commit.

## Apply-time correction — 2026-10-01 planning repair

The "Proposal consequence" line above ("Predecessor stays `active` until its own open tasks are closed or explicitly unverified") is the propose-time wording and is preserved as history. The reviewed planning repair superseded it: a locally executable failed or unrun gate remains open and blocks production optimization; only current successful local results or an evidenced, genuinely external classification close the predecessor's debt. The corrected contract lives in the change's `proposal.md`, `specs/performance-closure-and-multisurface-acceleration/spec.md`, `design.md`, and `tasks.md` (task 2.7), repaired and re-reviewed before any production optimization per `APPLY_PROMPT.md` section 2.
