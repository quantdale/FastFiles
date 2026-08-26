# Implementation and validation record

## Implementation slice

The implementation introduces:

- `verify/core/RepositoryState.psm1` — single local Git/source-state authority;
- `verify/qualification/Invoke-ExactTreeQualification.ps1` — isolated exact-HEAD
  materialization and fast committed-tree parity lane;
- `tests/drift/test_source_provenance_fixtures.ps1` plus CTest registration;
- provenance re-binding in explicit selected-result status and release-frontier
  wrappers;
- run-result schema fields for candidate/source provenance;
- current qualification guidance and this OpenSpec ledger.

No product, protocol, signing, privileged service, UI, or external-provider behavior
is changed.

## First committed-tree execution — run 32955810413

Implementation SHA `656acb8b8ebdc69da97501f34917df484213bf49`
(tree `c91fee2ca82f8ff1fa08f3c65535b298a7ea623f`) reached the new hygiene
qualification step after source/diagnostic policy, strict OpenSpec 73/73, and the
existing status/provider contract suite all passed.

The exact-tree step then failed for two deterministic harness reasons:

1. the fixture helper function was named `Git`, so its internal `& git` resolved to
   the PowerShell function itself and overflowed call depth;
2. the isolated checkout was detached, while existing `ProjectStatus.psm1` requires
   a non-empty branch argument and failed before status/release-frontier generation.

The qualifier still proved the captured commit/tree binding itself (`Bound: True`),
and source-marker, diagnostic-policy, documentation-truth, and all 19 doc-truth
fixtures passed in the isolated committed tree. This failure is retained as evidence
rather than rerun away.

## Corrective design

- rename the fixture helper to `Invoke-FixtureGit`, eliminating command shadowing;
- keep detached-HEAD support in the source-provenance model and fixture coverage;
- materialize the qualifier on a GUID-owned ephemeral branch at the exact captured
  SHA so the pre-existing project-status branch contract can execute;
- delete that branch only after the worktree is removed and only when the branch ref
  still equals the captured SHA;
- add an adversarial pre-existing destination test proving the qualifier fails
  without deleting caller content.

## Validation contract

The corrected implementation must pass the existing hosted CI matrix. Debug and
release-test CTest naturally include `ffsource_provenance_fixture_tests`; Analyze,
ASAN, hygiene/OpenSpec/status/provider, and unsigned release qualification remain
unchanged gates. The hygiene job additionally runs the exact-tree script and then
re-selects its result through project-status and release-frontier provenance checks.

The final corrected SHA/tree and successful hosted run are recorded only after they
exist; no self-referential future result is fabricated here.

## Preserved external boundaries

Authorized production signer/pins, signed privileged install/runtime, real MFT/USN,
privileged trust-store integration, clean-host/provider/guest validation, and the
physical-display/foreground-owned interactive UI matrix remain external/opt-in.
