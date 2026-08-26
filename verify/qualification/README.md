# Exact-tree validation qualification

FastFiles distinguishes **development validation** from **exact-tree qualification**.
A command can be useful and green while the working directory contains local edits;
that result describes the files that were actually read and must not be cited as
proof that a different Git commit passed.

## Source-state authority

`verify/core/RepositoryState.psm1` is the single local source-provenance owner. It
records the full HEAD commit SHA, `HEAD^{tree}` SHA, branch/detached state, staged
and unstaged tracked changes, non-ignored untracked inputs, and a deterministic
source-state digest. Ignored build/verifier outputs are not source dirtiness.
No provenance command resets, cleans, stashes, checks out, or discards user work.

An exact-tree qualifying result requires:

- mode `exact-tree`;
- a clean source state at validation start and finish;
- identical HEAD commit/tree and source-state digest throughout the run;
- a result whose recorded commit/tree still equals the candidate being consumed.

Dirty development results remain valid development feedback but are explicitly
non-qualifying for exact-commit handoff and release evidence.

## Qualify the committed tree

Run from the repository root:

```powershell
pwsh ./verify/qualification/Invoke-ExactTreeQualification.ps1
```

The qualifier materializes the current HEAD in a uniquely named temporary Git
worktree on a GUID-owned ephemeral branch whose tip is the exact candidate commit.
The branch exists only because the existing project-status generator requires a
non-empty branch name; commit/tree identity, not that branch name, is the
qualification authority. The qualifier runs the fast repository-state-sensitive
gates, re-samples source state, removes only the worktree whose exact path it created
and verified as owned, then deletes the ephemeral branch only if it still points to
the captured candidate SHA. It does not use a network clone and refuses to reuse or
delete a pre-existing path.

The source-provenance model itself also supports detached HEAD and has a regression
fixture for it; the ephemeral branch is an orchestration compatibility seam, not an
identity requirement.

The emitted `run-result.json`-compatible record contains `candidateSha`,
`candidateTreeSha`, and `sourceProvenance`. Pass it explicitly to the status or
release-frontier generator when that local result is intended as evidence:

```powershell
pwsh ./verify/status/Get-ProjectStatus.ps1 `
  -VerificationRunResultPath ./verify/status/generated/exact-tree-qualification.json

pwsh ./verify/status/New-ReleaseReadinessReport.ps1 `
  -VerificationRunResultPath ./verify/status/generated/exact-tree-qualification.json
```

Those consumers compare the selected result with the current repository HEAD/tree
and require the current source checkout itself to remain clean. A legacy or dirty
development PASS is projected as `DEVELOPMENT_ONLY`; stale, mutated, or otherwise
contradictory exact-tree evidence fails closed.

Hosted GitHub Actions remains separate, stronger remote evidence for the complete CI
matrix: each workflow checkout is bound to its `head_sha`, uses full history, and
runs against the committed tree. Local exact-tree qualification complements that
binding for pre-push/handoff parity; it does not replace production signing,
privileged MFT/USN, clean-host/provider, or interactive evidence gates.
