## Why

PR #9 is merged and two scan-authority interleavings from its review are still unproven: a pause/resume of an incomplete scan can clear a coverage-hole flag before an older terminal is applied, and an interrupted reconciliation of an already-indexed volume never enters the five-second retry queue. The same line of work still has an unresolved many-tabs visual count, an unproven targeted-build identity claim, and a hosted gate that fails before any job step.

## What Changes

- Require a deterministic reproducer, or a source-backed proof that the ordering is impossible, before any scan-authority or retry-scheduling edit.
- Require that a stale terminal cannot authorize replacement-scan state, that an incomplete initial scan stays incomplete, and that missing coverage is retried without deleting valid rows.
- Require an interrupted already-indexed reconciliation to move onto the replacement connection's new volume id at the five-second floor, with one outstanding retry and no use of the retired connection-scoped id.
- Require one combined pause, disconnect, and reconnect regression before either of those findings is treated as resolved.
- Require the many-tabs scenario to prove logical tab count and overflow reachability. The expected count stays 8. Hidden overflow tabs are not deleted tabs.
- Require a targeted-build identity sequence before any CMake edit, and allow a no-code closure when current dependencies already refresh certification binaries. Identity-override validation stays fail-closed.
- Hold merge to `main` while required hosted jobs cannot run. Local parity is not hosted certification, and a documentation tip is not the certified executable.
- Leave the raw-volume synchronous `DeviceIoControl` completion bound unproven. Do not mark inherited completion-bound tasks finished.
- This change does not replace `legacy-protocol-correctness-and-raw-volume-cancellation-closure`.

## Capabilities

### New Capabilities

- `targeted-build-provenance`: targeted builds must embed the source revision they were built from, or the campaign records a no-code proof that existing edges already do that, without weakening fail-closed identity overrides.
- `hosted-certification-hold`: hosted GitHub Actions and repository protection are an external integration hold. Local results are labeled separately, and a blocked six-job workflow is not a pass and not permission to merge.
- `raw-volume-completion-bound`: synchronous raw-volume `DeviceIoControl` cancellation has no proven finite completion bound and is outside this campaign's design.

### Modified Capabilities

- `index-engine`: scan completion authority across pause, resume, replacement, and disconnect, and the retry cadence for an interrupted reconciliation after a new ephemeral volume id is issued.
- `privileged-index-service`: supersede and terminal behavior must stay inside the closed command surface from `establish-architecture-foundation`. No generic open-path or open-handle command. A local attempt counter is not treated as wire identity.
- `tabs`: narrow-width overflow keeps every logical tab reachable. The many-tabs check must not count only visible buttons and must not lower its expectation from 8 to 4.

## Impact

- Engine session ownership in `VolumeSessionManager`, the privileged connection's terminal dispatch, and the service scan worker's supersede path. Index durability and reconciliation passes are in scope only where a stale terminal or a missed retry would mark, skip, or delete the wrong rows.
- The closed engine-to-service command set in `openspec/changes/establish-architecture-foundation/design.md` (D4) and `privileged-index-service`. Any later additive terminal discriminator has to be a new negotiated message, not a resized `ScanComplete` and not an open-path primitive.
- Tab chrome (`NavigationChrome`, `ChromeLayout`) and the visual harness scenario `shell-many-tabs-overflow`. Responsive overflow stays.
- CMake identity (`fastfiles_build_identity`, `fastfiles_add_version_resource`, `WriteBuildIdentity.cmake`) only if the targeted-build reproducer shows a stale certification binary.
- GitHub Actions billing and branch protection are observed, not edited. The live check for this proposal is recorded in `evidence/explore-findings.md`.
- Predecessor tasks that stay open elsewhere: `legacy-protocol-correctness-and-raw-volume-cancellation-closure` task 5.4 and `final-residual-hardening-and-hosted-ci-recovery` task 2.3.

## Goals

- A stale `ScanComplete` or `ScanFailed` cannot authorize replacement-scan state.
- An incomplete initial scan stays incomplete until a terminal that belongs to a scan which actually covered the volume.
- Missing coverage is retried. An incomplete reconciliation does not delete valid records.
- A failed replacement does not inherit false authority. Ordinary pause/resume and successful scans still work. Disconnect cannot restore stale authority.
- An interrupted already-indexed reconciliation survives a new ephemeral volume id, honors the five-second floor, wakes something other than the ten-minute ordinary poll, keeps one outstanding retry, and does not issue a stale connection-scoped volume id.
- Neither finding is resolved until a combined pause, disconnect, reconnect, stale-terminal, and recovery case passes.
- Logical tab count, overflow reachability, and harness-versus-product are proven separately.
- Targeted-build identity is either reproduced and fixed or closed with no code. Hosted certification stays honest.

## Non-goals

- Implementing product, CMake, harness, or UI fixes in the proposal session.
- Restarting completed campaigns, or checking predecessor raw-volume completion-bound tasks because this change exists.
- Redesigning privileged-worker cancellation, overlapped FSCTL, `CancelIoEx` policy, or helper-process isolation.
- Adding a generic "open this path or handle" command.
- Treating a local generation counter as sufficient while old and new terminals are indistinguishable on the wire.
- Lowering the many-tabs expectation from 8 to 4, or treating hidden overflow tabs as deleted.
- Changing GitHub billing, spending limits, plans, branch protection, or rulesets.
- Merging to `main` while required hosted checks are blocked, or calling local CI parity a hosted result.
- Calling a documentation-only tip the certified executable.
- Weakening fail-closed `FASTFILES_BUILD_ID` validation.
- Production signing, trust-store edits, clean-host install, or live privileged MFT/USN validation.

## Open Questions

- Is the stale-terminal order reachable on the real privileged pipe, or only as a direct callback sequence? The reproducer or the impossibility proof answers this before a fix is chosen.
- Can retirement be expressed with the existing `StopVolumeScan` and same-pipe ordering, or does the reproducer force a connection teardown or an additive terminal message? A wire bump is not selected here.
- Does the Control-view walk used by `Get-VisualTabCount` omit `SW_HIDE` tab buttons at width 720? The visual reproducer answers this. The inherited count of 4 is not re-measured in the proposal.
- Which certification-relevant binaries, if any, keep a stale embedded identity after a targeted build of only that target?
- When account billing will allow the six hosted jobs to start. Until then the integration state is blocked, not failed product code.
