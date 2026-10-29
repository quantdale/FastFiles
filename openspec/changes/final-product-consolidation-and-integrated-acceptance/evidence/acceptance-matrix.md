# Frozen acceptance matrix — journeys A–I and five pillars

Frozen at apply intake (task 1.5) before product edits, from
`specs/integrated-product-acceptance/spec.md`, design D3/D4/D11/D12 and the
predecessor invariants mapped in `integration-ledger.json`. This matrix is the
finite acceptance boundary for the campaign: cells outside it are backlog.

## Result semantics (design D3)

Each cell records exactly one of `PASS`, `FAIL`, `NOT_RUN`, `SKIPPED`,
`BLOCKED_ENVIRONMENT`, `BLOCKED_EXTERNAL` with evidence and scope. Only an
executed matching postcondition supports `PASS`. A later green run never erases
a preserved first failure; the first failure and later observations are both
retained. `BLOCKED_ENVIRONMENT` requires evidence that places the failure
outside the application (design D12) — never suite-skip inheritance.

## Environments and oracles

- **owned fixtures**: marker-named files/directories created and destroyed by
  the test/harness in a campaign-owned scratch root; pre/post filesystem
  inventories plus content hashes are the oracle. Real user files are never
  touched.
- **native app**: `FastFiles.exe` (debug-diagnostic for UI-capable cells),
  real Win32/UIA input paths, real operation handlers.
- **worker/oracle tests**: CTest executables over the actual operation workers
  and stores — complementary, never substituted for a native UI cell.
- **independent reviewers**: fresh-context read-only review lanes (design D6).

## Journey A — first launch / degraded browsing

| Cell | Postcondition | Oracle | Environment | Result |
| --- | --- | --- | --- | --- |
| A1 | Cold first launch reaches Home; no blocking enablement dialog | native app + UIA | ordinary user | CANDIDATE-CELL |
| A2 | Available locations discoverable (sidebar/pinned/known) | native UIA | ordinary user | CANDIDATE-CELL |
| A3 | Service-absent degraded state explained truthfully (no whole-computer claim) | UI copy + status badge | ordinary user, no service | CANDIDATE-CELL |
| A4 | Browsing a permitted owned directory lists real entries | filesystem oracle | owned fixture | CANDIDATE-CELL |

## Journey B — search → reveal → return

| Cell | Postcondition | Oracle | Environment | Result |
| --- | --- | --- | --- | --- |
| B1 | Typing produces results; filter/sort apply without retargeting stale rows | app state + UIA identity | owned fixture | CANDIDATE-CELL |
| B2 | Deep result reveal lands on exact containing folder + selected file, correct pane | filesystem + UIA | owned fixture | CANDIDATE-CELL |
| B3 | Return preserves hierarchy/selection/focus/active pane | UIA | owned fixture | CANDIDATE-CELL |
| B4 | In-panel snapshot refresh replaces rows without silent identity retarget | UIA identity | owned fixture | CANDIDATE-CELL |

## Journey C — Column View keyboard navigation

| Cell | Postcondition | Oracle | Environment | Result |
| --- | --- | --- | --- | --- |
| C1 | Parent/child traversal + horizontal reveal by keyboard | UIA + visual | owned fixture | CANDIDATE-CELL |
| C2 | Selection and Back/Forward preserve real targets | UIA | owned fixture | CANDIDATE-CELL |
| C3 | Missing/inaccessible path shows truthful recoverable state, no stale-target action | app state | owned fixture | CANDIDATE-CELL |

## Journey D — tabs / dual panes / transfer ownership

| Cell | Postcondition | Oracle | Environment | Result |
| --- | --- | --- | --- | --- |
| D1 | Tab create/switch/close and reopen restoration preserve context | UIA + state | owned fixture | CANDIDATE-CELL |
| D2 | Dual-pane copy/move writes exactly the visible source→destination | filesystem hash/count | owned fixture | CANDIDATE-CELL |
| D3 | Unrelated fixture files unchanged; pane ownership preserved across switches | filesystem oracle | owned fixture | CANDIDATE-CELL |

## Journey E — Storage intelligence

| Cell | Postcondition | Oracle | Environment | Result |
| --- | --- | --- | --- | --- |
| E1 | Known nested/duplicate-name fixture ranks expected descendants exactly | ranking output vs fixture manifest | owned fixture | CANDIDATE-CELL |
| E2 | Folder → Treemap entry/drill-down → exact file reveal → return | filesystem + UIA | owned fixture | CANDIDATE-CELL |
| E3 | Partial aggregates labelled as known-lower-bound, no whole-volume overclaim | UI copy + numbers | owned fixture | CANDIDATE-CELL |
| E4 | Narrow/wide controls preserve real selection/focus/shared action target | UIA | owned fixture | CANDIDATE-CELL |

## Journey F — file operations (create/rename/transfer/delete)

| Cell | Postcondition | Oracle | Environment | Result |
| --- | --- | --- | --- | --- |
| F1 | new file → inline rename → rename again → Undo (exact leaf casing, history) | filesystem | owned fixture | CANDIDATE-CELL |
| F2 | new folder → inline rename → enter | filesystem | owned fixture | CANDIDATE-CELL |
| F3 | rename collision → native error → recovery; occupied destination bytes preserved | filesystem hash | owned fixture | CANDIDATE-CELL |
| F4 | case-only rename preserves accepted casing/history/cancellation semantics | filesystem | owned fixture | CANDIDATE-CELL |
| F5 | Copy/Move/Link multi-item with explicit per-item conflict choices | filesystem hash/count | owned fixture | CANDIDATE-CELL |
| F6 | apply-to-all defaults unchecked, request-scoped, reset on next request | behavior + inventories | owned fixture | CANDIDATE-CELL |
| F7 | Skip vs Cancel distinguished; per-item success/skip/failure counts truthful | counts vs inventory | owned fixture | CANDIDATE-CELL |
| F8 | Keep-both reserves distinct names across multi-item plan | filesystem | owned fixture | CANDIDATE-CELL |
| F9 | generated incoming results survive later replacement work; partial completion accounted | filesystem hash | owned fixture | CANDIDATE-CELL |
| F10 | accepted in-flight cancellation = neither false success nor suppressed IO error | counts + inventory | owned fixture | CANDIDATE-CELL |
| F11 | safe owned delete + supported Restore/Undo (holding-folder seam) | filesystem | owned fixture | CANDIDATE-CELL |
| F12 | real multi-item Recycle Bin Undo/conflict/history | filesystem | real Recycle Bin | SEPARATE-SEAM (worker evidence retained; real journey unqualified until safely demonstrated) |

## Journey G — recovery

| Cell | Postcondition | Oracle | Environment | Result |
| --- | --- | --- | --- | --- |
| G1 | engine-unavailable and permission-error states truthful and non-destructive | app state | ordinary user | CANDIDATE-CELL |
| G2 | search cancellation + index rebuild recovery | app state | owned fixture | CANDIDATE-CELL |
| G3 | shutdown/reopen restores workspace without loss | state compare | owned fixture | CANDIDATE-CELL |

## Journey H — accessibility / native polish

| Cell | Postcondition | Oracle | Environment | Result |
| --- | --- | --- | --- | --- |
| H1 | keyboard/UIA labels, stale-identity rejection, dialog focus | UIA | available host | CANDIDATE-CELL |
| H2 | selected row + active pane exposed truthfully | UIA | available host | CANDIDATE-CELL |
| H3 | live light/dark check; synthetic DPI vs physical DPI/High Contrast/reduced-motion scope recorded separately | captures | available host | CANDIDATE-CELL |

## Journey I — Explorer interoperability (design D12)

| Cell | Postcondition | Oracle | Environment | Result |
| --- | --- | --- | --- | --- |
| I1 | Explorer → FastFiles marker transfer delivers exact destination bytes + visible destination refresh + source-state truth (move empties source / copy preserves) | independent pre/post hash+count oracles | owned Explorer + FastFiles fixtures, serialized foreground | CANDIDATE-CELL |
| I2 | FastFiles → Explorer same contract | independent oracles | owned fixtures | CANDIDATE-CELL |
| I3 | Non-delivery diagnosed across the full chain (target entry, drop feedback, IDropTarget invocation, destination I/O, bytes, refresh) before any disposition | probe evidence | owned fixtures | DIAGNOSIS-GATE (never automatic BLOCKED_ENVIRONMENT) |

## Five pillars (design D11 `ORIGINAL_VISION_ALIGNED`)

1. discovery (A, B), 2. hierarchy (C, D), 3. storage intelligence (E),
4. daily operations (F, G), 5. native polish (H). Degraded browsed/pinned
coverage never establishes whole-computer discovery.

## Frozen exclusions / known intermittents

- Restricted live process-reaper termination test: registered, never executed
  without a separately permitted environment (PR #17 lineage).
- Physical display matrix, High Contrast/reduced-motion physical profiles:
  environment-blocked unless a physical host is available; synthetic geometry
  is never a physical-display claim.
- Hosted CI: pre-step payment failure documented (`HOSTED_CI=BLOCKED_EXTERNAL`);
  one post-publication inspection of the exact pushed SHA (task 12.3).
- Production signing/pins, clean-host install, elevated MFT/USN: external to
  this campaign (`PRODUCTION_RELEASE_READY` remains owner/external).
- Known intermittents preserved from intake: search-deep-result-navigation flake
  family (UIA run variance), palette-precondition visual flake, cold-restore
  ColumnView UIA omission and preview terminal-state questions (task 8.1/8.2
  diagnosis cells).
