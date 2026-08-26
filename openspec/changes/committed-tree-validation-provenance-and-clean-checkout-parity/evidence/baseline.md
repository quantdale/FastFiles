# Baseline — 2026-08-26

## Git / hosted state

- Planner parent: `c7088aa2b6d58babe66553bba3e9f2cb52e803af`.
- Parent tree was the completed repository-truth/doc-drift handoff.
- Hosted CI run `32939084333` completed successfully for exactly `c7088aa2b6d58babe66553bba3e9f2cb52e803af`.
- Active planner commit: `36f98597102f5c7f67350ed1d9869b518ddab61f`.
- Planner commit tree: `5aad88f65ec4517913c4608b48d0cdbe1461e546`.
- Baseline lifecycle projection reported PASS with 24 raw unchecked tasks and 0 active-local tasks before this campaign implementation.

## Independently reproduced gap

The verifier result model binds capability/run identity using `runId` and
`invocationId`, and lifecycle hosted evidence binds exact full Git SHAs. However,
`manifest.json` did not record HEAD/tree/source cleanliness, and project status
accepted an explicitly selected `run-result.json` based on result consistency only;
it did not prove that the selected PASS belonged to the current candidate tree.

The prior campaign supplies a real historical instance of the failure class:

- local semantic doc validation passed after `progress.md` had been corrected in the
  working directory;
- implementation commit `80b24ffa37185e90a02af798dd9245ce6894fb7b` did not contain that correction;
- hosted run `32934249659` evaluated the committed tree and failed the new stale
  current-claim guard;
- reconciliation commit `72db280e9b41723a571c0f1367aa4ba8bb0be15b`
  then passed hosted run `32935961728` and fresh-checkout validation.

The new hermetic source-provenance fixture reproduces the same structural condition:
a committed fixture file retains stale content while the working copy contains a
local correction. The development state keeps the old HEAD/tree identity but is
source-dirty and therefore cannot become exact-tree qualification evidence.

## Preserved boundaries

Production signer/pins, privileged MFT/USN runtime validation, privileged trust-store
integration, clean-host/provider/guest validation, physical-display and
foreground-owned interactive UI evidence remain external/opt-in. PR #6 remains
superseded historical evidence. No product behavior or security boundary is changed
by this campaign.
