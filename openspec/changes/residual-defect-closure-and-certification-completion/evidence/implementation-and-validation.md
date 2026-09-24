# Implementation and Validation Evidence

## Candidate

- Source/executable candidate: `9231d4b` (`close residual certification defects`).
- Exact visual evidence was generated from that candidate in a clean detached worktree. The subsequent tracked evidence-only commit does not alter executable source.
- The primary tree was clean before final parity.

## Local gates

- Debug build and CTest: 121/121 PASS.
- Release-test build and CTest: 121/121 PASS.
- Analyze configuration: warning-clean.
- ASan protocol/IPC/lifecycle slice: 3/3 PASS in the discovered Visual Studio developer environment.
- Strict OpenSpec: 52/52 PASS.
- Full UIA capability: 22 passed, 6 recorded skips, 0 failed.
- Real E2E: 8/8 PASS, including create/rename/copy/move/delete, preview, cold restore, and lifecycle cleanup.
- Visual catalog: core 16/16 PASS; gallery-inclusive 20/20 PASS; observed host DPI 120 (125%).
- Nonprivileged UIA repeat matrices: drag-in, drag-out, drag-between-panes, and selection-model-interactions each completed 10/10 process-level trials; all attempts were retained in ignored `build/uia-trial-*.json` ledgers.
- Governance/provenance/visual/CI child contracts pass, including missing npx, validator ENOENT, validator exit 2, unknown classifier states, and expected visual scenario states.
- Final clean-tree local CI parity: 11 PASS / 0 FAIL / 0 SKIP. This is local parity evidence only, not hosted GitHub Actions evidence.

## External boundaries

Hosted Actions remains blocked by the account billing failure. Production signing/pins, clean-host installation, elevated MFT/USN execution, disposable guest certification, and unsupported DPI/display/High-Contrast environments remain external and were not represented as passes.

## Residual review notes

- The service now prevents a second frame after a failed write and aborts the connection; the available behavioral fixture covers cancellation and terminal-write failure. A raw partial-frame injection fixture remains a future test-hardening item.
- Build identity remains commit-level; exact executable-tree digest embedding is not yet part of the binary provenance channel. The final report must distinguish commit-bound executable evidence from any stronger future tree-digest claim.
- The local CI contract pins the current job/command vocabulary; a future workflow expansion should add a machine-readable full-step manifest before relying on parity as a complete hosted replay.
