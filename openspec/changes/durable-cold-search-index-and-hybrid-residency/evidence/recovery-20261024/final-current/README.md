# Current qualified execution bundle

Implementation:4c1c4501e5b1f0579a2d34f4f7afea5f83708979.
Clean execution/measurement source:269d1c36036cb84b40c3ef01baee3c2b6c990579.
These revisions have identical executable/test/benchmark inputs. Final evidence
HEAD is derived externally after the commit; it is not a new binary execution.

- `identities.json`: actual Debug/release-test/diagnostic executable SHA256s,
  oracle identity, preflight-only ingestion identity and combined-source binding.
- `current-readonly-runner.json` plus current-{2m,5m}-oracle JSON/logs: unchanged
  source fixture hashes, process/query timings and exact cold/canonical13-cell
  results.2M has3 processes;5M has1. Normal12 cells scan canonicalN rows;
  cancellation independently verifies no result. No physical-cache cold or p95.
- `benchmark-compile-commands.json` / `allocator-binding.txt`: actual optimized
  flags/compiler and CRT new/delete resolution; no tracked allocation override.
- `current-5m-ingestion-preflight*`: exit3 / insufficient free space / zero rows.
  This is NOT a5M ingestion result. Earlier measured200k/1M are in historical
  checkpoint-3a41481 and bound todab2128, not silently relabeled current.
- `current-ordinary-errors.log`: actual SQLite VFS error returns and bounded
  pinned-reader checks, no drive fill or deliberate process-termination test.
- `parity/`: terminal11/11 report and every step stdout/stderr; complete Debug/
  release-test130/130, Analyze and ASan4/4. Initial Analyze prebuild timeout
  output remains separate from serial completion and terminal parity.
- E2E/UIA logs and JSON:8/8,25/25,25/25 and full23PASS/5SKIP/0FAIL. `visual/`
  contains PR15 manifest/cases17PASS/3GalleryFAIL; screenshots remain in ignored
  execution artifacts. Manifest hardcoded125% wording is not DPI certification;
  captured windows are96 DPI.
- Combined focused tests: current6d3de0e Debug/release3/3 each; empty production
  UI input diff versus PR16. No combined full-app/runtime verdict.
- `review-availability.json`: exhausted native review requests and failed-closed
  fixed-free-adapter static preflight; no final source approval manufactured.
- `diagnostics-note.json`: strict MSVC/Analyze pass is separate from incomplete
  standalone LSP coverage.
- Final docs logs: strict OpenSpec93/93, focused doc/diagnostic CTests3/3. First
  strict invocation omitted mandatory RepoRoot and did not execute validation;
  retained separately, corrected invocation passes.

`hashes.json` excludes itself. It records both original copied execution bytes
and canonical Git blob hashes. Text checkout EOL conversion can change the first
without changing the latter. It is not a PE-reproducibility claim.

See `../../ordinary-recovery-owner-report.md`,
`../../deferred-ordinary-acceptance.md` and `../../../tasks.md` for open scope.
No merge, privileged operation, user-data mutation or safety-policy override is
authorized by any PASS in this directory. Final source-equivalence and hosted
results are emitted after the evidence commit, outside the tree checked by
that gate, and reported in PR15/session handoff (no self-reference loop).
