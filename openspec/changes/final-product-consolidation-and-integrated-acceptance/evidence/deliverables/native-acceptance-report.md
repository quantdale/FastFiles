# Native acceptance report — journeys A–I on the integrated candidate (2026-10-29)

Real native executable (`build/debug-diagnostic/src/ui/FastFiles.exe`), public
UIA/keyboard/pointer routes, owned marker fixtures, independent filesystem
oracles where the harness provides them. Result semantics per
`../acceptance-matrix.md`. "E2E" = `verify/e2e` suite scenario; "UIA" =
`verify/certification/Invoke-UiaLongRunCertification.ps1`; "probe" = the
bounded diagnosis probes.

## Journey results

| Journey | Cells | Evidence | Result |
| --- | --- | --- | --- |
| A first launch / degraded | A1–A4 | E2E `launch-ready` (main window + engine handshake, ready=engine-connected); `navigation-browse` (permitted-directory browsing of owned fixtures) | **PASS** (service-absent degraded copy is E2E-observed in `process-lifecycle`/`launch-ready` paths; no whole-computer claim made) |
| B search → reveal → return | B1–B4 | E2E `search-user-flow`; probe `Test-SearchReopenScope.ps1` (exact retained-query result, exact folder selection); probe `Test-SearchSnapshotRefresh.ps1` (open-panel refresh on hidden create/delete without retype or identity retarget) | **PASS** |
| C Column View keyboard | C1–C3 | E2E `navigation-browse`; UIA core-smoke (uia-keyboard-navigation 25/25); missing/inaccessible recovery cells exercised by `cold-restore` + `process-lifecycle` | **PASS** |
| D tabs / dual panes | D1–D3 | UIA core-smoke (dual-pane capability 25/25); visual-matrix pane-width regimes (686/731px details open/closed); E2E `file-operations` filesystem oracles | **PASS** |
| E Storage intelligence | E1–E4 | E2E `storage-terminal-geometry`; visual-matrix semantic postconditions (top-level percents 10/20/30/40, sum 100, drilled 100%); imported `ffengine_storage_ranking_residency_tests` (hot/cold read planes) 1/1; `ffstorage_*` CTest families | **PASS (scoped: known/pinned coverage; no whole-volume overclaim)** |
| F file operations | F1–F11 | E2E `file-operations` (owned fixtures + hashes); `ffcreation_collision_tests`, `ffnew_file_write_tests`, `fffileoperations_e2e_tests`, conflict/restore/shortcut/cancel CTest families (143/143); shortcut late-collision sentinel preserved byte-exact | **PASS** |
| F Recycle Bin Undo (real, multi-item) | F12 | Holding-folder worker seam covered by tests; real Recycle Bin journey NOT executed | **UNQUALIFIED** (seam evidence retained; distinct journey open) |
| G recovery | G1–G3 | E2E `process-lifecycle`, `cold-restore`, `preview-terminal-state` | **PASS** |
| H accessibility / polish | H1–H3 | UIA core 25/25, rapid-input, application-lifecycle; visual matrix light/dark; stale-identity rejection covered by `ffcolumnview_uia_tests`/`ffuia_events_tests` | **PASS (synthetic DPI/host scope; physical display matrix, High Contrast and reduced-motion profiles NOT exercised — explicitly outside the claim)** |
| I Explorer interop | I1–I2 | Inherited bounded non-delivery diagnosis (below) | **UNQUALIFIED** |
| I non-delivery diagnosis gate | I3 | `large-index-lifecycle-and-search-scalability/evidence/uia-explorer-drag-in-investigation.md` | **PASS as a diagnosis gate** (full chain evidence; classification never automatic) |

## Journey I — bounded non-delivery diagnosis (task 8.6 / design D12)

Inherited from the predecessor investigation (4 harness runs × 3 attempts + 7
probe variants on the same host class): valid target entry observed; injected
Explorer→Explorer drags deliver (probe2 — the input boundary works);
Explorer→FastFiles drop feedback evidence shows the drop target is never
reached — FastFiles' `IDropTarget::Drop` never runs, so no product-side
destination I/O is ever initiated and no bytes/refresh can follow. The chain
was diagnosed to the handler-invocation boundary (target entry ✓, drop
feedback ✓, handler invocation ✗, destination I/O n/a, bytes n/a, refresh n/a).
The predecessor's own harness targeting defect was fixed and re-tested before
this conclusion. Classification: **UNQUALIFIED with preserved cause class**
(drop-delivery to the FastFiles window fails deterministically on this host;
an application-side registration/routing cause is NOT excluded — the favored
co-equal hypothesis class per the predecessor review). `BLOCKED_ENVIRONMENT`
is deliberately NOT assigned. Per design D12 the cells stay open until a
second interactive host, physical input instrumentation, or product-side drop
instrumentation resolves the cause class; the suite's
SKIP/BLOCKED_ENVIRONMENT inheritance is not used as the cell result.

## Bounded diagnoses (task 8.1/8.2) — closed on this candidate

- 8.1 cold-restore ColumnView omission: E2E `cold-restore` **PASS** on the
  integrated candidate (historical first-failure preserved; later pass does
  not erase it).
- 8.2 preview terminal-state + in-panel search refresh: E2E
  `preview-terminal-state` **PASS**; `Test-SearchSnapshotRefresh.ps1` **PASS**
  (both hidden-create and hidden-delete refresh an open panel with unchanged
  query text and no stale-row retarget). Historical first failures preserved.
- 8.3 demonstrated material defects repaired during integration: two
  lifecycle-state inconsistencies (see reconciliation); three /analyze C6001
  false positives. No other material integration defect surfaced in any gate.
- 8.4 no consequential UX defect demonstrated during acceptance; the Refero
  grounded direction was preserved and no UX rewrite was required (recorded
  per task 8.4's no-defect branch).

## Five-pillar verdict (task 8.5)

See `five-pillar-alignment.md`. All five pillars meet their frozen required
product cells within the stated scopes; degraded browsed/pinned coverage is
never presented as whole-computer discovery; journey I and F12 remain the
named unqualified cells.
