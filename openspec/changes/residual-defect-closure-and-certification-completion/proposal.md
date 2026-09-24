# Residual Defect Closure and Certification Completion

## Status

Active implementation campaign. The user-provided residual-closure prompt is the mission contract. Historical campaign records remain evidence, not instructions to reopen unrelated work.

## Problem

The current `main` branch has three local residual-campaign commits that address scan failure truth, strict OpenSpec invocation, and shared-session navigation classification, but independent review found material gaps:

- a service frame write can partially reach the byte stream before a second terminal is attempted;
- a delivered `ScanFailed` can leave an incomplete session without a bounded retry;
- local CI parity can report `PASS` when a required OpenSpec step is unavailable, and missing `npx` is not fail-closed;
- the current visual evidence is a one-record lifecycle failure and the visual runner has no exact scenario-set guard;
- UIA skip conversion can confuse an unavailable foreground observation with a proven ownerless desktop, input injection results are discarded, and one failed Explorer drag is over-classified;
- a navigation-helper exception can escape the two Search scenario boundaries, and dependent setup continues after a failed navigation precondition.

## Goals

1. Make scan write failure transport-fatal and observable without corrupting the frame stream.
2. Preserve scan authority and retry failed scans/reconciliations through a bounded scheduler path.
3. Make local parity classify success, validation failure, tool unavailability, and required skips without false `PASS` results.
4. Make the 16-core plus conditional 4-gallery visual catalog complete, provenance-bound, and guarded against missing or duplicate scenarios.
5. Make UIA skips require observed evidence, propagate input/geometry failures, and retain cleanup/evidence truth.
6. Keep the two named shared-session navigation exception paths inside local, tested boundaries.
7. Run the applicable local certification ladder and record hosted/external blockers without fabricating evidence.

## Non-goals

- No change to the three-process security model, closed command allowlist, pipe-only IPC, signing policy, or production trust stores.
- No privileged MFT/USN execution, certificate provisioning, service installation, display reconfiguration, or host security changes.
- No cosmetic product refactor or broad UI redesign.
- No attempt to make unsupported host foreground, Explorer OLE, DPI, or clean-host conditions look like product passes.

## Completion

The change is locally complete only when confirmed findings have focused regression coverage, the executable tree is rebuilt and identity-bound, strict OpenSpec and local parity are truthful, the visual/UIA/E2E gates are either passed or explicitly classified from fresh evidence, and an adversarial review finds no material locally actionable defect. External certification blockers remain separately classified.
