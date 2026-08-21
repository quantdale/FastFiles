# FastFiles end-user acceptance

This is the repeatable checklist for using FastFiles as a daily Windows file manager. It is intentionally separate from unit/CTest coverage and from the historical production-readiness audit. A scenario is only `PASS` when its stated evidence exists; `BLOCKED-EXTERNAL` is not a pass.

## Run contract

Use a disposable fixture outside the repository:

```powershell
$root = Join-Path $env:TEMP 'FastFiles-dogfood-small'
.\verify\dogfood\New-DogfoodFixture.ps1 -Root $root -Profile Small -Reset
.\verify\dogfood\Compare-DogfoodFixture.ps1 `
  -ExpectedManifest (Join-Path $root 'fixture-manifest.jsonl') -ActualRoot $root
```

For a larger physical directory, choose an explicit budget and record the host:

```powershell
.\verify\dogfood\New-DogfoodFixture.ps1 -Root (Join-Path $env:TEMP 'FastFiles-dogfood-medium') -Profile Medium -ItemCount 10000 -Reset
```

Mutating scenarios MUST use a marked disposable root. After the session, reset it with `Reset-DogfoodFixture.ps1` and independently verify that the root is absent. Never point the reset command at a personal-data directory.

## Scenario register

| ID | Scenario | Preconditions | Automation status | Evidence | Status |
| --- | --- | --- | --- | --- | --- |
| UA-001 | First launch → degraded state → Home → disposable folder | Fresh user state or backed-up profile; fixture available | UIA capability + manual | First-run run `20260821-115745`; final all-scenario UIA run `20260821-131427`; Home screenshot; state files created | PASS-DEGRADED (manual Computer Use blocked) |
| UA-002 | This PC, drive/folder navigation, deep path, Back/Forward, breadcrumb, address entry | Fixture and a browsable NTFS path | UIA/headless navigation tests + manual | Core run `20260821-115745`; address/tab/column PASS | PASS-DEGRADED |
| UA-003 | Tabs, multiple tabs, dual pane, active-pane switching, session restore | Fixture; two safe locations | UIA capability + headless persistence + manual | Final all-scenario UIA run `20260821-131427`; navigation workspace persistence tests PASS; owned UI/engine lifecycle soak PASS | PARTIAL (manual session-restore confirmation pending) |
| UA-004 | Filename search, filters, Unicode, rapid replacement, sort, mutation while searching | Medium fixture for load case | UIA + search tests + indexed benchmark | Search run `20260821-115103`; allocated-size/deep navigation PASS; Medium/Large fixtures compared; 100K repeat p90 recorded | PASS-DEGRADED (manual large-folder mutation pending) |
| UA-005 | Preview, details, storage analysis, treemap semantic navigation | Fixture with content types; degraded limitations recorded | UIA/headless + manual | Core run `20260821-115745`; storage and degraded-state PASS; preview/treemap manual pending | PARTIAL |
| UA-006 | Copy, move, rename, delete, restore, undo, drag/drop | Small fixture; independent expected manifests | File-operation E2E + dogfood truth scripts | `fffileoperations_e2e_tests` PASS, 17.59s; independent Win32 filesystem assertions | PASS-E2E (manual drag/drop pending) |
| UA-007 | Conflicts, cancellation, locked/read-only/vanished source/destination failures | Disposable files; lock helper where available | File-operation E2E + manual | `fffileoperations_e2e_tests` PASS; locked/vanished/cancel/conflict cases covered by test | PASS-E2E (manual pending) |
| UA-008 | First-run defaults, normal restart, forced teardown, engine/UI restart, malformed state recovery | Backed-up disposable profile; no personal state deletion | Headless persistence tests + UIA/manual + owned soak | State backup `FastFiles-state-backup-20260821`; `ffnavigation_tests` malformed/Unicode/version/restore cases PASS; committed core run and soak PASS | PASS-DEGRADED (manual forced teardown during mutation pending) |
| UA-009 | Keyboard-only core workflow | Fixture; visible focus | UIA/SendInput + manual | Core UIA run `20260821-115745`; keyboard/address and selection PASS | PASS-DEGRADED (manual Computer Use blocked) |
| UA-010 | Accessibility names/roles/value/selection/focus/bounds/invoke/scroll | Interactive desktop and UIA available | Headless UIA + live capability | UIA runs `20260821-115745`, `20260821-115103`, `20260821-115432`, `20260821-115534`; semantic PASS | PASS-DEGRADED |
| UA-011 | Light/dark, High Contrast, reduced motion, 100/125/150/200% DPI, narrow/wide | Reversible host settings or an already configured test host | Layout tests + live rows where permitted | Existing DPI/layout seams; invasive display/system-setting changes not authorized in this session | BLOCKED-EXTERNAL |
| UA-012 | 10K/100K/host-bounded large directory, search, scroll, preview, storage, bounded soak | Explicit item/time budget; resource sampling | Benchmarks + bounded runtime sampler | `performance.md`; Medium/Large physical fixture comparisons; 100K indexed repeat; committed 30s UI/engine soak | PARTIAL (large-folder UI latency and interactive preview/storage timing not measured) |
| UA-013 | Fresh install, same/newer upgrade, repair, uninstall, reinstall, app-open/service-running lifecycle | Signed package, elevated clean host, two package versions | Release driver where prerequisites exist | Transaction report and pre/post inventory | BLOCKED-EXTERNAL |
| UA-014 | UNC/network, removable/disappearing volume, privileged indexed path, clean-host validation | Safe share/media/signed service/clean host | Not available on current baseline | Machine-backed external evidence only | BLOCKED-EXTERNAL |

## Core workflow checklists

### UA-001–UA-005: daily browsing and comprehension

- [ ] Launch from a clean or backed-up profile; confirm no unexplained blank/permanent spinner state.
- [ ] Confirm Home is usable without the service and the degraded/indexed state is visible but non-modal.
- [ ] Browse a deep fixture path, return with Back/Forward, use breadcrumb and address entry, then open a second tab.
- [ ] Exercise search, filters, sort, preview/details, storage, and treemap; record any unavailable data explicitly.
- [ ] Repeat in light/dark and narrow/wide layouts where the host permits.

### UA-006–UA-007: file-operation trust

- [ ] Snapshot the disposable source and destination independently before each operation.
- [ ] Run copy/move/rename/delete/restore/undo and conflict choices; include Unicode, long names, duplicate basenames, read-only, locked, and vanished paths.
- [ ] Cancel at least one long disposable operation and verify both roots independently.
- [ ] Treat any UI success whose filesystem truth disagrees as a stop-the-line finding.

### UA-008–UA-010: recovery and accessibility

- [ ] Back up only the disposable profile state; corrupt copies of non-critical JSON and confirm safe fallback.
- [ ] Restart normally and after terminating only disposable FastFiles/engine processes; verify no permanent stale state or orphaned campaign process.
- [ ] Exercise keyboard-only navigation and core commands with visible focus.
- [x] Run semantic UIA scenarios and retain exact PASS/SKIP/FAIL counts; do not replace missing UIA with pixel assertions. Final all-scenario wrapper run `20260821-131427` passed with only its documented prerequisite skips; prior isolated search/selection/error runs also passed.

### UA-011–UA-014: environment and scale

- [ ] Record OS/build, architecture, DPI, theme, High Contrast, animations, monitor topology, service/engine state, fixture profile, and executable hashes.
- [x] Run bounded performance/soak measurements; compare only like-for-like fixture and build configurations. See `codebase-audit/sessions/2026-08-21-dogfood/performance.md`.
- [ ] Mark signing, elevation, clean-host, VM, network, removable, and unavailable display settings as external rather than inferred.

## Evidence and finding rules

Each completed scenario record MUST contain:

- scenario ID, date/time, commit and executable SHA-256;
- OS/build/architecture and relevant display/service state;
- fixture root/profile/manifest hash and cleanup result;
- exact steps and observed result;
- independent filesystem evidence for file mutations;
- automation result and named prerequisite skips;
- finding ID if behavior is incorrect.

Findings belong in `codebase-audit/sessions/2026-08-21-dogfood/`. Severity is P0 data loss/security/destructive corruption, P1 crash/hang/core workflow unusable, P2 significant incorrect behavior/reliability, or P3 polish. Do not add speculative entries.

## Current campaign boundary

The portable baseline is degraded/unprivileged browsing. Production signing, signed service activation, elevated MFT/USN validation, clean-host/VM provisioning, network/removable media, and invasive system-setting changes are not claimed until executed on an appropriate host.

## Executed evidence summary (2026-08-21)

The Computer Use native channel was unavailable after its prescribed retry/reset
sequence, so no pixel-coordinate interactive session is claimed. The repository's
real Windows UIA capability still drove the diagnostic FastFiles executable through
the desktop window and persisted screenshots/tree evidence. The final all-scenario
run `20260821-131427` passed with only its documented prerequisite skips; earlier
core, search, selection-model, and late error-state runs also passed.
`fffileoperations_e2e_tests` independently verified copy, move, rename,
Recycle Bin restore/undo, conflict, cancellation, lock, vanished-path, and long-path
filesystem outcomes. The first-run profile was backed up rather than deleted and
recreated successfully.

The committed measurement wave also exercised real physical fixture scales:
10,023 Medium files and 100,023 Large files both matched independent
path/type/size/attribute snapshots and were reset through marker-guarded
cleanup. A repeat 100K indexed benchmark and a 30-second diagnostic UI/engine
resource soak passed with no process exit or cleanup residue. The native
Computer Use channel remained unavailable, so manual mouse/drag and system
preference claims remain explicitly partial or external.
