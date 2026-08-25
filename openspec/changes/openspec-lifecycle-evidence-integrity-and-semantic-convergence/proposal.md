## Why

The lifecycle-aware frontier now separates raw unchecked tasks from active local
work, but `openspec/lifecycle.json` still accepts arbitrary non-empty evidence
strings. A syntactically valid manifest can therefore name a deleted artifact,
an unreachable commit, or an unbound hosted run, and contradictory task or
supersession metadata can still influence autonomous continuation and release
readiness.

## What Changes

- Version the lifecycle manifest to schema 2 with typed repository-path,
  Git-commit, hosted-run binding, and non-authoritative note evidence.
- Resolve repository paths safely inside the repository, resolve full reachable
  Git commits offline, and require hosted-run records to bind an exact run ID
  and commit in a tracked repository artifact.
- Add semantic validation for authoritative state transitions, supersession
  self-links/cycles, task disposition/category/status/evidence combinations,
  and lifecycle drift while preserving raw task visibility.
- Project evidence authority and separate structural, evidence, and semantic
  errors through project status and release-readiness.
- Migrate the live manifest and add deterministic fixture coverage for invalid
  references, cycles, contradictions, redaction, ordering, and fail-closed
  release composition.

## Non-Goals

- No product/UI/index/security architecture changes.
- No network lookup during ordinary status or release-readiness generation.
- No trust-store, certificate, provider, guest, elevated, or interactive-host
  provisioning.
- No automatic closure of genuine external evidence gates.
