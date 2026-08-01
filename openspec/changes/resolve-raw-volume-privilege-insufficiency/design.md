## Context

The completed diagnostic change observed the production scan call under the
actual `NT SERVICE\FastFilesIndexSvc` token. `SeBackupPrivilege` was present
and enabled, but `CreateFileW("\\\\.\\C:")` returned
`ERROR_ACCESS_DENIED` (`0x5`). The result rules out a missing or merely
disabled token right, but it does not by itself identify whether a narrower
Windows right, a different service identity, or a separate privileged boundary
is the smallest safe fix.

The implementation must preserve the existing engine/service mutual
authentication, fail-closed installer behavior, and metadata-only diagnostic
logging. The current signed-peer pins are intentionally fail-closed; the
unsigned diagnostic switch used to collect evidence is never a production
configuration.

## Goals / Non-Goals

**Goals:**

- Empirically compare documented, narrowly scoped privilege and service
  identity candidates for raw-volume open plus USN journal operations.
- Select and implement the least-privilege candidate that passes under a clean
  installed service, or document the evidence that a separate constrained
  privileged boundary is required.
- Make installer registration, startup ordering, rollback, and recovery match
  the selected identity and rights.
- Re-run the real scan and journal path with the final signed installation and
  retain token identity, privilege state, and exact Win32 error evidence.

**Non-Goals:**

- Silently adding the service to Administrators or switching production to
  LocalSystem without a documented security decision.
- Keeping an unsigned-peer or authentication bypass in any shipped binary.
- Rewriting the index format, scan protocol, or UI behavior unrelated to the
  raw-volume access boundary.
- Choosing a broker design before the documented-rights candidate matrix has
  been evaluated.

## Decisions

### Evidence-driven candidate matrix

The first implementation stage will run a fixed matrix in a disposable clean
host: the current virtual account, each documented candidate right or identity,
and (only as a diagnostic control) LocalSystem. Every row records the account
SID, held/enabled privilege state, raw-volume `CreateFileW` result, USN control
code result, and service startup/registration order. This prevents a broad
grant from being adopted merely because it makes one machine pass.

### Least privilege is the selection rule

The deployed model will be the narrowest candidate that passes all required
raw-volume and USN operations and preserves the existing IPC authorization
boundary. Administrators-group membership and LocalSystem are controls or
last-resort options only; they require an explicit security decision and a
follow-up architecture record.

### Constrained broker is the fallback boundary

If no documented narrow right or service identity can open the volume, the
follow-up will introduce a dedicated privileged boundary rather than weakening
the existing engine authorization. Its command surface will be limited to
volume enumeration, raw-volume scan/journal operations, cancellation, and
structured status; it will not expose arbitrary process creation, path-based
file mutation, or unrestricted handle duplication. The existing signed,
authorized Engine remains the only client.

### Installer and startup are one transaction

Service creation, account/right assignment, security descriptor application,
and service start will be treated as one transaction. A failed grant or
verification prevents startup and rolls back the service configuration. A
successful grant is followed by a fresh service start so the tested token is
the token used by the real scan path.

### Production verification requires signed artifacts

The final candidate is not accepted from an unsigned diagnostic build. The
installer must deploy signed service and Engine binaries with non-placeholder
thumbprints, then the real-service diagnostic must reproduce the selected
outcome under the installed identity. The temporary diagnostic escape hatch is
limited to development evidence collection and is excluded from release
artifacts.

## Risks / Trade-offs

- **[Risk]** A candidate privilege appears to work only because the test host
  has unrelated administrative state. → **Mitigation:** use a clean host,
  capture the token/group context, and compare against the LocalSystem control.
- **[Risk]** A broad identity change fixes scanning but violates least
  privilege. → **Mitigation:** make the candidate matrix and explicit security
  decision prerequisites for installer changes.
- **[Risk]** A broker adds attack surface and IPC complexity. → **Mitigation:**
  keep the command surface narrow, preserve signed-peer authentication, and
  test malformed, unauthorized, and cancellation requests.
- **[Risk]** A failed upgrade leaves a service that cannot start. → **Mitigation:**
  transactionally back up configuration, validate before start, and retain an
  automatic rollback path.

## Migration Plan

1. Provision a clean verification host and collect the current diagnostic
   baseline.
2. Run the candidate matrix without changing the production installation.
3. Record the selected identity/right or approve the constrained-broker
   fallback in the change evidence.
4. Implement service registration, startup, security descriptors, and any IPC
   changes behind the selected design.
5. Build and sign the release artifacts, install them transactionally, and
   verify service recovery and rollback.
6. Run the real scan/journal verification, attach the evidence, and only then
   update the open foundation and scanning tasks.

## Open Questions

- Which documented Windows right, if any, permits this raw-volume operation
  without Administrators or LocalSystem on the supported Windows versions?
- Does the selected right work for both the initial MFT read and USN journal
  control calls, or only one phase?
- If a broker is required, should it replace the current service identity or
  become a second tightly ACL'd service boundary?
- What release-signing and installer migration mechanism will provision the
  final mutual-authentication thumbprints on a clean host?
