# Design: exact-tree validation provenance

## Context

Three evidence identities already exist and remain authoritative in their domains:

1. verifier `runId` / `invocationId` bind capability results to one harness run;
2. schema-v2 lifecycle `git-commit` / `hosted-run` evidence binds durable history
   to full reachable commits and exact hosted `head_sha` values;
3. repository truth contracts bind current guidance to executable sources.

None identifies the working-tree contents a local PASS actually read. The design
extends, rather than replaces, those systems.

## D1 — One Git/source-state authority

`verify/core/RepositoryState.psm1` owns local repository provenance. Consumers do
not reimplement porcelain parsing or SHA/tree comparison. It records full commit and
commit-tree SHA, branch/detached state, staged/unstaged tracked paths, non-ignored
untracked paths, and a deterministic digest of that state.

Ignored outputs are intentionally absent because `git status --porcelain` obeys the
repository's ignore rules. This keeps normal build/run output from invalidating
source qualification without reading ignored verifier/signing directories.

## D2 — Development is not qualification

Dirty-tree tests remain allowed and useful. `development` mode is always
non-qualifying for exact-commit evidence. `exact-tree` requires a clean source state
at start and finish plus identical commit/tree/digest throughout the run.

Detached HEAD is supported by the source-state model and fixtures. Qualification is
content/commit-bound, not branch-name-bound.

## D3 — Detect mutation, not merely final cleanliness

A clean tree at the end is insufficient. Start and finish states are compared. Any
HEAD change, commit-tree change, or source-state digest change produces a stable
reason code and prevents exact-tree qualification.

The provenance record stores identities and repository-relative path lists only; it
never stores raw diffs or absolute host paths.

## D4 — Validate HEAD from an isolated local materialization

`verify/qualification/Invoke-ExactTreeQualification.ps1` captures the candidate
HEAD/tree, creates a GUID-owned temporary worktree on a GUID-owned ephemeral branch
at exactly that SHA, verifies that materialization is clean and identity-equal, runs
the fast repository-sensitive checks, re-samples source state, and removes only the
exact worktree path it created and re-identifies through `git worktree
list --porcelain`. It then deletes the ephemeral branch only if the ref still points
to the captured candidate SHA.

The first implementation attempted a detached worktree. Hosted run `32955810413`
proved that the existing project-status generator rejects an empty detached branch
name before status generation. The source-provenance model itself handles detached
HEAD correctly; the temporary branch is therefore a compatibility seam for existing
status generation, not a qualification identity. The commit/tree remain the
exclusive content authority.

The qualifier never calls reset, clean, stash, or checkout on the caller's worktree
and needs no network. Full multi-minute compile/test duplication is intentionally
left to the normal local build ladder and hosted CI; this lane targets the failure
class where repository-sensitive checks read a dirty tree different from the pushed
commit.

## D5 — Selected local evidence is re-bound at consumption

The exact-tree result is `run-result.json` compatible and adds `candidateSha`,
`candidateTreeSha`, and `sourceProvenance`. Status/release-frontier wrappers call the
same provenance module before treating an explicitly selected PASS as evidence.
They also require the current checkout to remain source-clean when the result is
consumed.

- exact same SHA/tree and qualifying provenance -> PASS;
- legacy/dirty development PASS -> `SKIPPED / DEVELOPMENT_ONLY`;
- stale candidate, dirty current source, source mutation, or contradictory
  provenance -> `FAIL / HARNESS_ERROR`.

This prevents filename-based replay. It does not make status generation perform a
network lookup.

## D6 — Hosted CI remains independent exact-SHA evidence

GitHub Actions already checks out full history and runs each job against the event's
`head_sha`. The local qualifier is pre-push/handoff parity evidence, not a substitute
for hosted build/ASAN/release qualification or external production gates.

## D7 — Adversarial Git fixtures are first-class

A dedicated PowerShell test creates disposable Git repositories and proves clean,
staged, unstaged, deletion, untracked, ignored-output, stale-result, commit advance,
mid-run mutation, detached-HEAD, missing-repository, deterministic diagnostic,
path-redaction, and pre-existing-destination safety. The historical dirty-working-
copy/committed-stale-tree pattern is reproduced explicitly.

## Risks and mitigations

- **Untracked generated files falsely block qualification:** only non-ignored paths
  are considered; generated paths remain governed by `.gitignore`.
- **Untracked executable/config input escapes:** every non-ignored untracked path is
  conservatively treated as source input.
- **Temporary worktree cleanup destroys user data:** destinations are GUID-unique;
  pre-existing paths fail; cleanup occurs only after exact ownership is revalidated.
- **Temporary branch cleanup deletes unrelated work:** branch names are GUID-unique
  and deletion occurs only if the ref still points to the captured candidate SHA.
- **Branch names used as identity:** SHA/tree are authoritative; branch is diagnostic.
- **Old green result replayed:** consumers compare full recorded SHA/tree to current
  HEAD/tree at use time and require current source cleanliness.
- **Source changes during validation:** start/finish digest comparison fails closed.
- **New provenance becomes a duplicate evidence system:** module is a local source
  identity seam only; lifecycle and hosted-run evidence remain unchanged.
