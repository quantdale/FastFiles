# Implementation and validation record

## Implementation slice

The implementation introduces:

- `verify/core/RepositoryState.psm1` — single local Git/source-state authority;
- `verify/qualification/Invoke-ExactTreeQualification.ps1` — detached exact-HEAD
  materialization and fast committed-tree parity lane;
- `tests/drift/test_source_provenance_fixtures.ps1` plus CTest registration;
- provenance re-binding in explicit selected-result status and release-frontier
  wrappers;
- current qualification guidance and this OpenSpec ledger.

No product, protocol, signing, privileged service, UI, or external-provider behavior
is changed.

## Validation contract

The implementation commit is required to pass the existing hosted CI matrix. Debug
and release-test CTest naturally include `ffsource_provenance_fixture_tests`; Analyze,
ASAN, hygiene/OpenSpec/status/provider, and unsigned release qualification remain
unchanged gates. The dedicated exact-tree script additionally executes the fast
repository-sensitive policy/doc/status/frontier slice from a detached materialization
of the exact candidate commit.

The final implementation SHA, commit-tree SHA, exact-tree result, and hosted Actions
run are intentionally recorded from observed Git/CI state after this implementation
is committed. They are not predeclared here to avoid self-referential or fabricated
evidence.

## Preserved external boundaries

The following remain external/opt-in and are not inferred complete by local source
qualification:

- authorized production signer and mutual-auth pins;
- signed privileged install/runtime and real MFT/USN validation;
- privileged trust-store integration where the host permits it;
- clean supported host and snapshot-restorable provider/guest evidence;
- physical-display, foreground-owned interactive UIA, DPI/HC, and drag/drop matrix.
