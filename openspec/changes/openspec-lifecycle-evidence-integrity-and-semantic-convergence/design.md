## Context

`verify/core/ProjectStatus.psm1` is an offline status authority consumed by
release-readiness and autonomous continuation. The previous lifecycle change
made lifecycle state explicit, but evidence remained a free-form string list
and the model did not validate semantic relationships between state,
supersession, task disposition, category, status, and evidence.

## Decisions

### D1 — Typed evidence is the lifecycle authority boundary

Evidence is an object with one of four kinds:

- `repository-path`: a repository-relative file or directory that exists and
  has no reparse-point component;
- `git-commit`: a full 40-character commit SHA that resolves and is an
  ancestor of the current `HEAD`;
- `hosted-run`: a numeric run ID, exact commit SHA, and tracked repository
  record whose content binds both values;
- `note`: safe human context that is valid provenance but never authoritative.

The parser returns normalized, redacted evidence objects with an explicit
`authority` field. It never treats schema-v1 strings as evidence.

### D2 — Offline resolution is deterministic and fail-closed

Status generation performs no network access. Repository paths are rejected if
absolute, traversal-bearing, outside the root, missing, or reached through a
reparse point. Git references must be full, resolvable commits reachable from
the local `HEAD`; abbreviated or nonexistent references fail. Hosted-run
records are tracked repository files and must contain the exact run ID and SHA.

### D3 — Semantic invariants are separate from shape validation

The model reports `structuralErrors`, `evidenceErrors`, and `semanticErrors`
alongside the combined `errors` list. Completed, superseded, and
external-evidence-only changes need authoritative evidence. Supersession is an
acyclic graph whose targets cannot be ambiguous or themselves superseded.
Task overrides must match their parent lifecycle: active tasks are local and
not unchecked PASS, external tasks require a non-local category and no local
component, mixed tasks require both local and non-local components, and any
disposition that removes work from active-local selection needs authoritative
evidence. Status/category mismatches are rejected.

### D4 — Invalid metadata never produces a clean frontier

When any lifecycle integrity error exists, raw unchecked tasks remain in the
OpenSpec inventory with `unknown` lifecycle state and project into the
ambiguity bucket. `frontier.localOpen` is empty for those tasks. Release
readiness reports the same lifecycle error and cannot infer production
readiness from a filtered or invalid frontier.

### D5 — Preserve historical provenance during migration

The current manifest is migrated to typed references without deleting external
tasks. Existing abbreviated commit evidence is expanded to immutable full
SHAs. Hosted-run claims are bound to the committed implementation evidence
record; a bare run number is never authoritative.

## Compatibility

Schema 1 is intentionally rejected rather than silently upgraded. This makes
old string evidence visible as a repair requirement and prevents a consumer
from treating legacy arbitrary text as proof. Project-status and
release-readiness retain their existing top-level shapes while exposing the
new lifecycle integrity fields and typed evidence objects.

## Risks and mitigations

- A path can exist but be unsafe: containment and reparse-point checks reject
  it before projection.
- A commit can exist but be unrelated: reachability from `HEAD` is required.
- A hosted run can be real but stale: the committed record must bind its exact
  run ID and SHA offline.
- Explicit metadata can hide local work: disposition/category/status matrices
  fail closed and raw tasks remain visible on any error.
- Existing external gates can be mistaken for implementation debt: their
  lifecycle and evidence remain visible in dedicated blocked buckets.
