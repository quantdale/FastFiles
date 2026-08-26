# Provenance authority map

## Repository/source state

Owner: `verify/core/RepositoryState.psm1`.

Authoritative inputs:

- `git rev-parse --verify HEAD` — full candidate commit identity;
- `git rev-parse --verify HEAD^{tree}` — full committed-tree identity;
- `git symbolic-ref --quiet --short HEAD` — diagnostic branch/detached state;
- `git status --porcelain=v1 --untracked-files=all` — non-ignored source deltas.

The source-state digest is a deterministic SHA-256 over the commit/tree and sorted
staged, unstaged, and non-ignored untracked path lists. It is an invocation-state
identity, not a replacement for Git's commit/tree hashes.

## Verification run identity

Owner remains `verify/core/RunTree.psm1` / `ResultModel.psm1`:
`runId` and `invocationId` bind capability/result composition. Exact-tree output is
run-result compatible and adds source provenance; it does not mint another run
identity system.

## OpenSpec/lifecycle evidence

Owner remains schema-v2 `openspec/lifecycle.json` plus
`verify/core/ProjectStatus.psm1`. Full reachable `git-commit` and exact-SHA
`hosted-run` evidence are unchanged.

## Hosted CI identity

Owner remains GitHub Actions event checkout/result metadata (`head_sha`). Every
current workflow checkout uses full history. Local exact-tree qualification is an
offline parity layer, not a replacement for hosted evidence.

## Current-candidate consumption

`verify/status/Get-ProjectStatus.ps1` and
`verify/status/New-ReleaseReadinessReport.ps1` use
`Resolve-VerificationRunQualification` before an explicitly selected PASS can be
projected as exact-tree local evidence. Candidate binding is commit/tree based, never
branch-name based.

## Destructive operations

None are authoritative or permitted. The provenance module performs read-only Git
inspection. The exact-tree qualifier creates a GUID-owned detached worktree and
removes only that exact worktree after ownership revalidation. It never resets,
cleans, stashes, or checks out caller work.
