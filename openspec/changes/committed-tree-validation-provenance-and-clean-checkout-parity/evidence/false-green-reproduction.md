# False-green reproduction

## Historical observed incident

The preceding repository-truth campaign had a local semantic-documentation PASS after
`progress.md` was corrected in the working directory. The first pushed implementation
commit (`80b24ffa37185e90a02af798dd9245ce6894fb7b`) did not contain that final tracked
correction. Hosted CI run `32934249659` therefore evaluated different source content
and failed `ffdoc_drift_tests`. The correction was committed as
`72db280e9b41723a571c0f1367aa4ba8bb0be15b`; run `32935961728` then passed.

The local result was not lying about the files it read. The handoff risk was that it
could be narrated as evidence for a commit whose tree was different.

## Hermetic reproduction

`tests/drift/test_source_provenance_fixtures.ps1` creates a disposable Git repository:

1. commit `tracked.txt` with `committed-stale`;
2. modify only the working copy to `locally-corrected-but-uncommitted`;
3. sample repository state in development mode;
4. prove the working copy and `git show HEAD:tracked.txt` differ while HEAD/tree remain
   the original committed identities;
5. build a development provenance record and assert `exactTreeQualified = false`.

This is the same structural failure class without touching real repository state.

## Prevention

The exact-tree lane validates `HEAD` from a separate detached worktree, so it reads the
committed tree rather than caller-local corrections. It also samples the materialized
source state at both start and finish. Consumers then require its recorded full
commit/tree to match the candidate at the moment evidence is selected.

Thus:

- dirty development PASS -> development-only, never exact-tree qualification;
- stale result from earlier commit -> provenance mismatch;
- source mutation during exact-tree run -> non-qualifying/failure;
- exact clean committed tree -> qualifying PASS if all selected gates pass.
