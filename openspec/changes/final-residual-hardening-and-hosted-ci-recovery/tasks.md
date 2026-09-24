## 1. Live baseline and independent investigations

- [ ] 1.1 Record live branch, HEAD, upstream, `origin/main`, tracked/untracked files, worktrees, concurrent activity, relevant lifecycle/tasks, and recent commits; reconcile safely without resetting to the supplied checkpoint.
- [ ] 1.2 Launch the transport, provenance, and hosted investigations in parallel; assign transport ownership of framing, scan writes, current IPC/service/engine tests, and a deterministic byte-boundary fixture design while keeping edits isolated from other lanes.
- [ ] 1.3 Assign an initially read-only provenance investigation to map embedded identity, source/tree state, binary hashes, release metadata, manifests, exact-tree qualification, and concrete bypass candidates.
- [ ] 1.4 Assign an independent read-only hosted CI investigation to inspect the live workflow, recent run annotations, billing/availability state, six required jobs, artifacts, and local parity mapping without changing account or repository policy.

## 2. Partial-frame transport proof

- [ ] 2.1 Reproduce the missing partial-frame behavior and add the narrowest deterministic write-operation injection seam that keeps production serialization, byte-stream I/O, deadlines, and overlapped lifetime intact.
- [ ] 2.2 Add focused fixtures for zero bytes, partial header, header only, partial payload, and all-but-final-byte writes; inspect actual emitted bytes and production frame-read results, including incomplete-frame rejection.
- [ ] 2.3 Prove scan batch-write and terminal-write failures are transport-fatal, append no later terminal on the invalidated connection, and cause bounded worker/connection teardown with no duplicate outcome.
- [ ] 2.4 Prove engine state is not marked complete by a partial frame, delivered `ScanFailed` retries stay bounded, and a newly authenticated connection recovers without stale connection-scoped IDs or an authentication bypass.
- [ ] 2.5 Register and run focused IPC, protocol, service-lifecycle, engine, and sanitizer coverage; fix any reproducible transport defect exposed by the fixture without extending the wire protocol unless required by evidence.

## 3. Provenance decision and conditional change

- [ ] 3.1 Execute a threat-model matrix against current authoritative gates: same claimed commit with changed sources, dirty tree, mixed/copied/stale binaries, generated or missing inputs, build configuration/toolchain differences, and malformed/incomplete manifests.
- [ ] 3.2 Record one evidence-backed decision (`IMPLEMENT`, `ALREADY COVERED`, or `NOT JUSTIFIED`) that distinguishes source-tree digest, binary hash, build-input coverage, and reproducible-build attestation.
- [ ] 3.3 If and only if the decision is `IMPLEMENT`, specify deterministic included/excluded inputs, path normalization, stable order, content hash, dirty/untracked/generated-file and configuration/toolchain treatment, noncircular embedding, and authoritative verification behavior; implement the minimum justified change.
- [ ] 3.4 If a digest is implemented, add and run negative cases for changed/missing inputs, malformed/wrong digest, mixed/stale artifacts, and dirty-state mismatch; otherwise retain the decision evidence without adding digest code or overstated security claims.

## 4. Impact validation and adversarial review

- [ ] 4.1 Run focused tests for each changed seam and record candidate SHA/tree and exact commands; avoid reusing evidence invalidated by code changes.
- [ ] 4.2 Have an independent transport reviewer attack false success, duplicate terminals, deadlocks, stale connection reuse, unbounded retries, and worker leaks with reproductions or source-backed findings.
- [ ] 4.3 Have an independent provenance reviewer attack stale, dirty, mixed, copied, malformed, and omitted-input artifacts against the actual certification gate.
- [ ] 4.4 Have independent certification and regression/scope reviewers challenge exact-SHA claims, skipped jobs, artifact binding, implementation complexity, and any regression to the three-process security model.
- [ ] 4.5 Correct confirmed material findings and rerun the affected focused checks; disposition unsupported or out-of-scope suggestions explicitly.

## 5. Final candidate and local certification

- [ ] 5.1 Commit coherent implementation slices, establish the exact executable candidate SHA/tree, and confirm embedded identity and clean-tree binding before the full ladder.
- [ ] 5.2 Run Debug and release-test builds/full CTest, Analyze, the protocol/IPC/lifecycle ASan slice, strict OpenSpec, and full local CI parity on that candidate; record counts, failures, skips, and evidence locations.
- [ ] 5.3 Run applicable provenance negative/binding tests, read-only governance contracts, UIA aggregate, E2E, 16-core and conditional 20-scenario visual catalogs, repository hygiene, and documentation drift; distinguish process-level execution from scenario results.
- [ ] 5.4 Reconcile every required gate against retained evidence and exact executable-tree identity; rerun only evidence made stale by any corrective commit.
- [ ] 5.5 Inspect the staged implementation diff and secrets/scope, push the validated executable candidate without force, and verify local HEAD equals `origin/main` before requesting hosted certification.

## 6. Hosted certification and external handoff

- [ ] 6.1 Recheck hosted availability for the final pushed candidate. If jobs can start, trigger or observe one appropriate exact-SHA run and inspect workflow/run IDs, trigger, all six job results, skips, artifacts, and failures; fix in-scope repository defects and refresh affected certification.
- [ ] 6.2 If GitHub rejects jobs before execution, retain the exact current annotation/run and `HOSTED_CI = BLOCKED_EXTERNAL`, avoid known-unstartable repeated runs, and identify the repository owner's billing/availability unlock action.
- [ ] 6.3 Inventory each remaining signer/pins, clean-host, elevated MFT/USN, disposable guest, DPI/High Contrast, and physical or mixed-monitor dependency with its prerequisite, existing canonical harness command, expected evidence, success criterion, and honest classification.
- [ ] 6.4 Produce the requested report sections A–I: repository state, partial-frame proof, provenance decision, hosted CI, validation, adversarial review, external certification, classified remaining work, and final verdict.
- [ ] 6.5 Update the predecessor's task 1.4 and review task 5.4 only when their acceptance is actually proven; reconcile lifecycle and workstate, inspect staged scope and `git diff --check`, commit and push the final handoff without force, verify local/remote HEAD, and inspect the resulting hosted status for the pushed SHA while distinguishing any documentation-only tip from the executable candidate.
