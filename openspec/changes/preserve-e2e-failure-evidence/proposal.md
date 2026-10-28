## Why
An actual cold-restore failure captured a UIA tree and screenshot inside the scenario, then its outer wrapper replaced `failure.json` with an incorrect claim that no evidence could be captured. Reusing an artifact directory could also make old files appear to belong to a new run.

## What Changes
- Reserve fresh scenario evidence directories and reject reused non-empty artifact roots before executing tests.
- Preserve current inner failure evidence and write outer result or exception context separately.
- Reject missing, malformed, mismatched or unknown-status scenario results instead of reporting accidental success.
- Cover absent evidence, collisions, exceptions, existing empty roots and real runner provenance handoff with headless tests.

## Capabilities
### New Capabilities
- `e2e-evidence-ownership`: keep failure evidence associated with the invocation that created it and preserve richer inner observations.
### Modified Capabilities
None.

## Impact
PowerShell E2E runner, evidence wrapper, headless contract tests and testing documentation. No product C++, IPC, security boundary, UI behavior, privileged service or release changes.
