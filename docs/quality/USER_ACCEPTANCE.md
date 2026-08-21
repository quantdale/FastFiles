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
| UA-001 | First launch → degraded state → Home → disposable folder | Fresh user state or backed-up profile; fixture available | UIA capability + manual | Run ID, engine state, screenshot/UIA summary | PENDING |
| UA-002 | This PC, drive/folder navigation, deep path, Back/Forward, breadcrumb, address entry | Fixture and a browsable NTFS path | UIA/headless navigation tests + manual | Scenario log and final location | PENDING |
| UA-003 | Tabs, multiple tabs, dual pane, active-pane switching, session restore | Fixture; two safe locations | UIA capability + manual | Tab/pane identities before/after restart | PENDING |
| UA-004 | Filename search, filters, Unicode, rapid replacement, sort, mutation while searching | Medium fixture for load case | UIA + search tests | First/full result timings and result evidence | PENDING |
| UA-005 | Preview, details, storage analysis, treemap semantic navigation | Fixture with content types; degraded limitations recorded | UIA/headless + manual | Preview/status/treemap evidence | PENDING |
| UA-006 | Copy, move, rename, delete, restore, undo, drag/drop | Small fixture; independent expected manifests | File-operation E2E + dogfood truth scripts | Before/after manifests and operation result | PENDING |
| UA-007 | Conflicts, cancellation, locked/read-only/vanished source/destination failures | Disposable files; lock helper where available | File-operation E2E + manual | Error text, cancellation, independent filesystem state | PENDING |
| UA-008 | First-run defaults, normal restart, forced teardown, engine/UI restart, malformed state recovery | Backed-up disposable profile; no personal state deletion | Headless persistence tests + UIA/manual | State copies, process IDs, restored state | PENDING |
| UA-009 | Keyboard-only core workflow | Fixture; visible focus | UIA/SendInput + manual | Key sequence and focused-control evidence | PENDING |
| UA-010 | Accessibility names/roles/value/selection/focus/bounds/invoke/scroll | Interactive desktop and UIA available | Headless UIA + live capability | Capability ledger and skipped prerequisites | PENDING |
| UA-011 | Light/dark, High Contrast, reduced motion, 100/125/150/200% DPI, narrow/wide | Reversible host settings or an already configured test host | Layout tests + live rows where permitted | Environment fingerprint and screenshots | PENDING |
| UA-012 | 10K/100K/host-bounded large directory, search, scroll, preview, storage, bounded soak | Explicit item/time budget; resource sampling | Benchmarks + bounded runtime sampler | p50/p90/max and resource series | PENDING |
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
- [ ] Run semantic UIA scenarios and retain exact PASS/SKIP/FAIL counts; do not replace missing UIA with pixel assertions.

### UA-011–UA-014: environment and scale

- [ ] Record OS/build, architecture, DPI, theme, High Contrast, animations, monitor topology, service/engine state, fixture profile, and executable hashes.
- [ ] Run bounded performance/soak measurements; compare only like-for-like fixture and build configurations.
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
