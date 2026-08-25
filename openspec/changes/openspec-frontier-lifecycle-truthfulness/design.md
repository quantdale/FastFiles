## Context

`verify/core/ProjectStatus.psm1` is the machine-readable source used by the
status command and release-readiness projection. Its current OpenSpec collector
recursively parses every `tasks.md`, then classifies unchecked lines from prose.
That is useful as a diagnostic fallback but is not sufficient to distinguish a
current implementation campaign from a completed campaign with deferred
external evidence or a stale administrative handoff line.

The existing OpenSpec directory layout and Markdown task ledgers remain the
human-facing acceptance authority. This change adds a small repository-owned
JSON model beside them; it does not move or archive changes.

## Decisions

### D1 — Keep raw task truth and active-frontier truth separate

**Choice:** Continue reporting every unchecked task under `openSpec.changes`,
but derive `frontier.localOpen` only from lifecycle-aware task dispositions.
Expose raw counts and lifecycle counts separately.

**Why:** An unchecked box is valuable provenance even when it is not current
local work. Hiding it would make release readiness look healthier than the
ledger actually is.

### D2 — Use one schema-validated JSON lifecycle manifest

**Choice:** Store explicit change state and optional task overrides in
`openspec/lifecycle.json`, validated by
`verify/schemas/openspec-lifecycle.schema.json` and deterministic PowerShell
model validation.

**Why:** JSON avoids adding a YAML parser dependency to the offline verifier and
does not risk changing OpenSpec's own `.openspec.yaml` parser. A single model
also makes missing or duplicate entries diagnosable.

### D3 — Fail closed on lifecycle model errors

**Choice:** Malformed, missing, unknown, or contradictory lifecycle metadata
produces a status model failure. Raw tasks remain visible as `ambiguousOpen`
and are never promoted to `localOpen`.

**Why:** A status error is safer than silently guessing that historical work is
actionable or silently dropping it from release readiness.

### D4 — Explicit metadata wins over prose classification

**Choice:** Task overrides may provide a category list, status, disposition,
and evidence. The text classifier remains available as a low-confidence
fallback and its source is retained in the task record.

**Why:** A task can contain both local implementation and external validation
clauses, and words such as `signed` or `interactive` can be historical context
rather than the next action.

### D5 — Preserve mixed and administrative work as first-class states

**Choice:** `mixedOpen` and `administrativeBlocked` are separate frontier
buckets. Mixed tasks are not flattened into a single local or external result;
administrative Git/CI/PR work is not treated as product implementation.

**Why:** Future agents need to see what remains without selecting the wrong
kind of work.

### D6 — Reconcile only evidenced historical delivery tasks

**Choice:** Check stale remote-push/workstate tasks only when later commits,
current Git parity, and recorded hosted-run evidence support the acceptance
claim. External signing/provider/clean-host/interactive tasks remain unchecked.

**Why:** The goal is truthful state, not zero unchecked boxes.

### D7 — Release readiness consumes the same lifecycle projection

**Choice:** The release-readiness frontier embeds the exact status lifecycle
model and fails if that model fails. Filtering historical local work cannot turn
an unsigned or externally blocked candidate into production readiness.

**Why:** Separate parsers would recreate the same drift at the release gate.

### D8 — Deterministic ordering and safe projection are invariants

**Choice:** Sort changes by name and tasks by numeric task ID/source line;
retain source and redacted evidence in JSON/Markdown; reuse existing path/text
redaction.

**Why:** Stable output is required for fixture tests, CI artifacts, and safe
autonomous handoff.

## Compatibility and migration

Existing changes without unchecked tasks need no lifecycle entry. Any change
with an unchecked task must have a valid manifest entry; a newly opened change
therefore fails status generation until its lifecycle is declared. Historical
task ledgers are not archived. The project-status schema increments to version
2 and the release-readiness schema follows it.

## Risks

- A stale manifest entry could misstate a change. → Validate names, states,
  supersession targets, task IDs, categories, statuses, and evidence fields;
  retain source provenance so the error is reviewable.
- A task override could hide a local clause. → Mixed dispositions remain a
  distinct bucket, and external evidence is never auto-closed.
- Historical reconciliation could overclaim delivery. → Require exact Git/CI
  evidence in the ledger annotation and leave ambiguous tasks open.
