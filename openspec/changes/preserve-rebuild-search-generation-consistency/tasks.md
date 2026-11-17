## 1. Reproduce

- [x] 1.1 Reconcile live Git/CI and trace current projection rebuild/read-dependency behavior; retain existing completed repairs.
- [x] 1.2 Reproduce exact scoped search result loss after failed rebuild through actual IndexPipeline overlap; preserve red evidence before production changes. `debug-rebuild-slots-red-test.log`: exact result and structural-restart assertions fail.

## 2. Repair

- [x] 2.1 Retire all potentially repacked dependencies before nonempty rebuild mutation, without enabling failed-volume success callbacks or discarding retry obligations.
- [x] 2.2 Prove single/shared-volume overlap, exact result identity/metadata/path, successful-volume callback positive control and empty-rebuild negative control. Focused production engine suite passes in `debug-rebuild-slots-green-test.log`.

## 3. Qualify

- [x] 3.1 Full Debug/optimized CTests, affected engine ASan and full MSVC analysis, active changed-path diagnostics and policy/strict/drift/lifecycle gates; review production delta and existing failure/retention/retry assertions. Debug/optimized 147/147 each, engine ASan 1/1, full incremental /analyze warning-clean; policies/strict PASS, active LSP inconclusive. See evidence/qualification.md.
- [ ] 3.2 Commit/push normally, refresh identity gates, obtain exact-SHA terminal hosted evidence and record immutable implementation baseline before lifecycle closure.
