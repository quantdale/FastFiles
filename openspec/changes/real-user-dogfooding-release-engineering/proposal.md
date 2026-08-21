## Why

FastFiles has cleared its portable engineering gates, but those gates do not yet provide a durable answer to the end-user questions of repeatable daily use, safe file-operation validation, recovery, and distributable release generation. This campaign turns the new product-readiness phase into evidence-backed acceptance and release tooling without reopening the completed security and correctness audit.

## What Changes

- Add a deterministic, disposable Windows filesystem fixture generator with small, medium, and host-bounded large profiles; generated data stays outside the repository.
- Add a durable end-user acceptance specification and campaign ledger covering first run, degraded browsing, navigation, search, file operations, persistence/restart, accessibility, performance, and failure injection.
- Add repeatable automation for stable acceptance scenarios using existing semantic/UIA and filesystem-truth seams where practical.
- Add measured dogfood/performance evidence capture for launch, large-directory browsing, search, resource behavior, and bounded soak loops; record limitations honestly when interactive or privileged prerequisites are unavailable.
- Add a deterministic release-candidate entry point that validates repository/version state, builds and tests the candidate, verifies signing prerequisites fail closed, collects artifacts, emits SHA-256 checksums and provenance metadata, and distinguishes unsigned validation artifacts from production-signed releases.
- Add release notes, supported-environment/clean-host guidance, and version-consistency checks without fabricating signing certificates, pins, privileged validation, or clean-host evidence.

## Capabilities

### New Capabilities

- `user-acceptance`: Disposable fixture generation, end-user acceptance scenarios, evidence ledger, repeatable acceptance automation, and measured dogfood/compatibility evidence.
- `release-candidate-packaging`: Fail-closed release-candidate orchestration, artifact manifest/checksum/provenance output, version consistency, release notes, and production-signing readiness.

### Modified Capabilities

- None. Existing product behavior is changed only when a new dogfood run produces a reproducible user-facing defect; the primary scope is acceptance and release operations.

## Impact

- Adds campaign fixtures/tools, acceptance documentation and tests, release-verification scripts, release metadata/templates, and focused CI/policy integration as needed.
- Touches `verify/`, `docs/`, `tests/`, and possibly installer/build metadata; production trust pins and privileged service boundaries remain unchanged.
- No production-signed artifact is produced without authorized certificate and pin inputs. Unsigned local artifacts remain explicitly labeled and non-production.
