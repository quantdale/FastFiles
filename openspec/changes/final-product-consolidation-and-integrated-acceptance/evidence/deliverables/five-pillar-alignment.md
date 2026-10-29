# Five-pillar alignment — integrated candidate (2026-10-29)

Evidence-bound per pillar from `native-acceptance-report.md` and the frozen
matrix (`../acceptance-matrix.md`). Classifications: functional-and-verified /
implemented-but-defective / incomplete / not-implemented / environment-blocked.

| Pillar | Required cells | Classification | Scope / limits |
| --- | --- | --- | --- |
| 1. Discovery (Home, Fast Search, reveal) | A1–A4, B1–B4 | **functional-and-verified** | Browsed/pinned/known coverage only; degraded state truthful (E2E + both search probes PASS) |
| 2. Hierarchy (Column View, tabs, panes) | C1–C3, D1–D3 | **functional-and-verified** | Keyboard + UIA routes; owned fixtures |
| 3. Storage intelligence | E1–E4 | **functional-and-verified (scoped)** | Known-file ranking, partial lower bounds, exact reveal; no whole-volume completeness claim |
| 4. Daily operations | F1–F11, G1–G3 | **functional-and-verified** | Owned-fixture create/rename/transfer/conflict/cancel/delete; Restore = holding-folder seam; real Recycle Bin (F12) UNQUALIFIED |
| 5. Native polish (a11y/theme/DPI) | H1–H3 | **functional-and-verified (host scope)** | UIA/keyboard/stale-identity on this host; physical display matrix, High Contrast and reduced-motion profiles environment-blocked; Explorer interop (I1–I2) UNQUALIFIED with diagnosed cause class |

No pillar is `implemented-but-defective` or `incomplete` on the frozen matrix.
The unqualified cells (F12, I1–I2, physical profiles) are explicitly outside
the qualified claims and are carried into the owner handoff with exact owner
actions. Degraded browsed/pinned coverage cannot complete whole-computer
discovery; it is not claimed to.
