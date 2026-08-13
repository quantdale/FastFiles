# FastFiles Deep Audit Remediation ExecPlan

## Objective

Remediate the ten supplied deep-audit findings and the associated CI/release
validation gaps, including regression coverage, measured large-index behavior,
and honest separation of locally validated work from production-signing and
clean-host evidence that requires external capabilities. Do not push, rewrite
history, or disturb unrelated work.

## Durable state

- Mission-specific workstate: `.agent/workstate/fastfiles-audit-remediation-state.md`
- Repository-wide historical workstate: `docs/agent/WORKSTATE.md`
- This plan and the mission-specific workstate are the authoritative resume
  pair for this remediation. `docs/agent/WORKSTATE.md` remains intact and is
  reconciled on every resume.

## Guardrails

- Preserve the three-process privilege boundary, closed command protocol,
  symmetric pinned authentication, degraded-mode behavior, and fail-closed
  release checks.
- Never add a generic privileged path/handle primitive, bypass signature pins,
  embed signing secrets, or claim external PASS evidence.
- Inspect exact file identity and reparse behavior before security-sensitive
  changes; use targeted commits containing only remediation-owned files.
- Update the workstate before switching workstreams, after meaningful tests and
  commits, and before long-running validation.

## Execution slices

1. Reconcile repository state, OpenSpec/design constraints, current audit
   evidence, and baseline build/test/verification results.
2. Build a first-class installer security test seam and model package/file
   identity, signature, manifest, staging, promotion, uninstall, and rollback
   decisions before changing privileged code.
3. Implement fail-closed installer payload verification and protected staging;
   remove elevated execution from user-writable `%TEMP%`; authenticate rollback
   candidates. Add adversarial setup tests.
4. Reconcile release/signing/pin tooling and CI policy. Automate all local
   checks and retain `BLOCKED_EXTERNAL` only for authorized signing, real
   production pins, and clean elevated-host runtime evidence.
5. Measure current snapshot/projection/serialization behavior at deterministic
   10K–2M tiers, then implement a versioned bounded publication design with
   compatibility and failure tests.
6. Replace zero-as-unknown USN updates with explicit partial-update semantics
   across store and projection; add restart/deletion/reconciliation tests.
7. Add scoped indexed-search cancellation with loop checkpoints, teardown
   cancellation, ownership/UAF tests, and supersession performance evidence.
8. Introduce/reuse a shared bounded overlapped named-pipe framing abstraction
   for UI/engine and service/engine where compatible; eliminate polling and
   helper-thread hard-timeout joins; add timeout/churn/stall tests.
9. Add one canonical Windows VERSIONINFO source and final-artifact metadata
   validation for every shipped executable.
10. Close applicable ASAN/CI/benchmark/drift validation gaps, synchronize
    relevant OpenSpec documentation, run the full validation matrix, and
    perform a read-only re-audit of every finding.

## Commit policy

Prefer focused commits by coherent theme: installer tests/trust, uninstall,
rollback, release tooling/CI, delta correctness, snapshot scale, search
cancellation, IPC, version resources, and validation. Do not push.

## Exit criteria

- Every finding has an evidence-backed final status in the workstate.
- All locally actionable code and regression tests are implemented and focused
  gates pass; external signing/clean-host limits are explicitly reported.
- Debug, Release, Analyze, applicable ASAN, verifier, doctor, OpenSpec, drift,
  focused subsystem tests, and benchmark diagnostics have exact recorded
  results, including known failures.
- `git diff --check` passes and unrelated user work remains untouched.
