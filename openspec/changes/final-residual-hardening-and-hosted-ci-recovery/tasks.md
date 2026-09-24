## 1. Live baseline and independent investigations

- [ ] 1.1 Record live branch, HEAD, upstream, `origin/main`, tracked/untracked files, worktrees, concurrent activity, relevant lifecycle/tasks, and recent commits; reconcile safely without resetting to the supplied checkpoint.
- [ ] 1.2 Launch the transport, provenance, and hosted investigations in parallel; assign transport ownership of framing, scan writes, current IPC/service/engine tests, and a deterministic byte-boundary fixture design while keeping edits isolated from other lanes.
- [ ] 1.3 Assign an initially read-only provenance investigation to map embedded identity, source/tree state, binary hashes, release metadata, manifests, exact-tree qualification, and concrete bypass candidates.
- [ ] 1.4 Assign an independent read-only hosted CI investigation to inspect the live workflow, recent run annotations, billing/availability state, six required jobs, artifacts, and local parity mapping without changing account or repository policy.

## 2. Partial-frame transport proof

- [ ] 2.1 Reproduce the missing partial-frame behavior and add the narrowest deterministic write-operation injection seam that keeps production serialization, real named-pipe byte-stream I/O, deadlines, and overlapped lifetime intact; confine any test adapter to a focused target.
- [ ] 2.2 Add focused fixtures for zero bytes, partial header, header only, partial payload, and all-but-final-byte writes; inspect actual emitted bytes and production frame-read results, including incomplete-frame rejection.
- [ ] 2.3 Reach the real scan batch-output branch through a portable synthetic scan source or equivalent narrow seam, without raw-volume privilege; prove batch-write and terminal-write failures are transport-fatal, append no later terminal, and cause bounded worker/connection teardown. Add behavioral cancellation during active work and a controlled worker/dispatch exception before closing predecessor task 1.4.
- [ ] 2.4 Close the failed connection so the engine's active read unblocks; prove no completion callback, cleared ephemeral state and pending retries, and a fresh service authorization decision on reconnect. Keep any injected verifier test-only, assert production linkage to the real pinned verifier, and keep live signed-peer success classified as external. Measure repeated delivered `ScanFailed` outcomes for bounded retry rate and queue/state, and decide explicitly whether the intended policy also requires a finite attempt count.
- [ ] 2.5 Register and run focused IPC, protocol, service-lifecycle, engine, and sanitizer coverage; include a new fixture target in both hosted and local ASan slices or demonstrate that the existing ASan framing target exercises it. Fix reproducible transport defects without extending the wire protocol unless required by evidence.

## 3. Provenance decision and conditional change

- [ ] 3.1 Execute a threat-model matrix against the actual authoritative gates: same claimed commit with changed sources, dirty tree, mixed/copied/stale binaries, generated or missing inputs, selected build root, build configuration/toolchain differences, and malformed/incomplete manifests. Reproduce and classify missing binary, hash/version/PE read failures, and embedded dirty/unbound state; distinguish collection or binding defects from a source-digest gap.
- [ ] 3.2 Repair every confirmed strict-verifier fail-open path for required executable records and build-time cleanliness claims; add negative tests through `Invoke-ProvenanceGate` and rerun the affected provenance and release gates regardless of the optional digest decision. Correct or qualify `boundToTree` if it does not prove binary-to-tree binding.
- [ ] 3.3 Record one evidence-backed digest decision (`IMPLEMENT`, `ALREADY COVERED`, or `NOT JUSTIFIED`) after direct verifier repairs, distinguishing source-tree digest, binary hash, package consistency, build-input coverage, and reproducible-build attestation.
- [ ] 3.4 If and only if the decision is `IMPLEMENT`, specify deterministic included/excluded inputs, path normalization, stable order, content hash, dirty/untracked/generated-file and configuration/toolchain treatment, noncircular embedding, and independent verification behavior; implement the minimum justified change.
- [ ] 3.5 If a digest is implemented, add and run negative cases for changed/missing inputs, malformed/wrong digest, mixed/stale artifacts, and dirty-state mismatch; otherwise retain the decision evidence without adding digest code or overstated security claims.

## 4. Impact validation and adversarial review

- [ ] 4.1 Run focused tests for each changed seam and record candidate SHA/tree and exact commands; avoid reusing evidence invalidated by code changes.
- [ ] 4.2 Have an independent transport reviewer attack false success, duplicate terminals, deadlocks, stale connection reuse, unbounded retries, and worker leaks with reproductions or source-backed findings.
- [ ] 4.3 Have an independent provenance reviewer attack stale, dirty, mixed, copied, malformed, and omitted-input artifacts against the actual certification gate.
- [ ] 4.4 Have independent certification and regression/scope reviewers challenge exact-SHA claims, skipped jobs, artifact binding, implementation complexity, and any regression to the three-process security model.
- [ ] 4.5 Correct confirmed material findings and rerun the affected focused checks; disposition unsupported or out-of-scope suggestions explicitly.
- [ ] 4.6 Remove or replace `verify/Invoke-CiParity.ps1`'s fixed hosted billing/last-green claim with a neutral local-only field or independently verified, run-bound hosted evidence; update its contract tests without weakening local/hosted separation.

## 5. Final candidate and local certification

- [ ] 5.1 Commit coherent implementation slices, establish the exact executable candidate SHA/tree, and confirm embedded identity and clean-tree binding before the full ladder.
- [ ] 5.2 Run Debug and release-test builds/full CTest, Analyze, the protocol/IPC/lifecycle ASan slice, strict OpenSpec, and full local CI parity on that candidate; record counts, failures, skips, and evidence locations.
- [ ] 5.3 Run applicable provenance negative/binding tests, read-only governance contracts, UIA aggregate, E2E, 16-core and conditional 20-scenario visual catalogs, repository hygiene, and documentation drift; distinguish process-level execution from scenario results.
- [ ] 5.4 Reconcile every required gate against retained evidence and exact executable-tree identity; rerun only evidence made stale by any corrective commit.
- [ ] 5.5 Inspect the staged implementation diff and secrets/scope, push the validated executable candidate without force, and verify local HEAD equals `origin/main` before requesting hosted certification. Track any later documentation-only tip separately; do not claim strict binary binding at that new HEAD without rebuilding and rerunning the binding gate.

## 6. Hosted certification and external handoff

- [ ] 6.1 Recheck hosted availability for the final pushed candidate. If jobs can start, trigger or observe one appropriate exact-SHA run and inspect workflow/run IDs, trigger, all six job results, skips, artifacts, and failures; fix in-scope repository defects and refresh affected certification.
- [ ] 6.2 If GitHub rejects jobs before execution, retain the exact current annotation/run and `HOSTED_CI = BLOCKED_EXTERNAL`, avoid known-unstartable repeated runs, and identify the repository owner's billing/availability unlock action.
- [ ] 6.3 Inventory each remaining signer/pins, clean-host, elevated MFT/USN, disposable guest, DPI/High Contrast, and physical or mixed-monitor dependency with its prerequisite, existing canonical harness command, expected evidence, success criterion, and honest classification.
- [ ] 6.4 After the planned phases, conduct one focused residual review of modified transport, provenance, and certification surfaces; fix material reproductions, rerun affected evidence, and stop on genuinely external, speculative, or out-of-scope items.
- [ ] 6.5 Produce the requested report sections A–I: repository state, partial-frame proof, provenance decision, hosted CI, validation, adversarial review, external certification, classified remaining work, and final verdict.
- [ ] 6.6 Update predecessor task 1.4 only when pre-write, partial-frame, cancellation, terminal-delivery, and worker-exception behavior are all proven, and task 5.4 only after independent review is complete; reconcile lifecycle and workstate, inspect staged scope and `git diff --check`, commit and push the final handoff without force, verify local/remote HEAD, and inspect the resulting hosted status for the pushed SHA while distinguishing any documentation-only tip from the executable candidate.
