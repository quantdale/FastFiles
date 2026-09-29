## 1. Registration

- [x] 1.1 Seed the propose-time truth ledger at `evidence/explore-findings.md`. — Evidence: that file classifies the live tip, the merged pull request, the stale workstate hold, hosted run `36588334920`, and the unproven current-head matrix.
- [x] 1.2 Register this change in `openspec/lifecycle.json` as `active` beside `legacy-protocol-correctness-and-raw-volume-cancellation-closure`. Do not supersede that change, do not check its task 5.4, and do not check `final-residual-hardening-and-hosted-ci-recovery` task 2.3. — Evidence: lifecycle entry added in the proposal commit; both changes remain active.

## 2. Truth reconciliation

- [x] 2.1 At apply start, re-derive branch, HEAD, `origin/main`, pull requests, worktrees, and the latest hosted run for the pushed SHA. Extend the ledger. Do not dispatch a workflow when the latest run for that SHA already failed before any step with the account-payment or spending-limit annotation. — Evidence: the `## Apply-start ledger extension` section of `evidence/explore-findings.md`. Branch `feat/overnight-truth-performance-hardening-20260929`, clean tree, `HEAD` `90339b9bc4b8e22324c05092e0c1e328acc65920`, `origin/main` `ea3d64f7c144343ec6faf849143d3f545e6fab36` (1 ahead / 0 behind), PR #10 `MERGED`, PR #11 `OPEN`, worktree `.kilo/worktrees/scythe-cloud` on `0dc3356`. Pushed SHA `90339b9b` hosted runs `36595140981` (push) and `36595149447` (pull_request): six jobs, zero steps in each, billing/spending-limit annotation present -> `BLOCKED_EXTERNAL`. No workflow dispatched or relaunched.
- [x] 2.2 Correct current instructions in `docs/agent/WORKSTATE.md` that disagree with the ledger, including any instruction that PR #10 is still unmerged or that no local action remains. Leave older checkpoints labeled historical. Do not rewrite them into false agreement. — Evidence: the resume checkpoint at the top of `docs/agent/WORKSTATE.md` is replaced with `APPLY IN PROGRESS` and records the merged PR #10, the open PR #11, and the apply-start hostile-hosted facts. The stale "PR #10 remains unmerged" and "NEXT EXACT ACTION: none" sentences survive only inside the sections already headed `Historical checkpoint — …`; they are not rewritten into agreement.
- [x] 2.3 Reread the external-frontier inventory and classify each prerequisite as still blocked, newly testable, or not applicable. Record the missing prerequisite and the harness that would close it. Do not change account billing, branch rules, or machine trust configuration, and do not request a privileged volume scan. — Evidence: `evidence/external-frontier-classification.md`. Eight provider, five signing, two privileged-NTFS, and three clean-host prerequisites remain `still-blocked`; the five interactive-UIA rows and the single physical-display row are `newly-testable` on this host and are confirmed or refuted by the 3.8 execution. No row becomes `not-applicable`. No billing, branch-rule, protection, signing, or trust-store setting was changed and no privileged volume scan was requested.

## 3. Current-head recertification

- [ ] 3.1 Identify every identity-bearing executable. Compare embedded identity with the implementation SHA. Reject a stale, empty, or overridden identity. Keep a documentation-only tip separate from the executable candidate.
- [ ] 3.2 Rebuild the implementation candidate and run the full Debug test suite. Record the actual passed, failed, and skipped counts. Do not copy the historical `124/124` result forward.
- [ ] 3.3 Rebuild and run the full release-test suite. Record the actual counts.
- [ ] 3.4 Run static analysis and record whether it is warning-clean.
- [ ] 3.5 Run the authoritative address-sanitizer slice and record the actual count.
- [ ] 3.6 Run strict OpenSpec validation and record the actual count.
- [ ] 3.7 Run provenance, governance, documentation-drift, and local parity checks. Label parity `LOCAL-PARITY-NOT-HOSTED-CI`.
- [ ] 3.8 Run the existing end-to-end, semantic-automation, catalog, and many-tab capabilities when the host can launch the shell. Record skips and environment obstructions separately from product failures.
- [ ] 3.9 If a failure reproduces as a product or test defect, keep the red result and fix it under section 5 before calling certification complete. If it is only an environment obstruction, classify it and do not edit production source.

## 4. Performance baseline

- [ ] 4.1 Inventory the existing informational benches and the advisory run-envelope capability. Record what each measures, the build type it requires, and which user-visible paths it does not measure.
- [ ] 4.2 Run the applicable existing benches on the certified non-debug build with warm-up and at least five measured repetitions. Record median and a tail. Store the record under this change's evidence, not under ignored run trees.
- [ ] 4.3 Add a focused harness only for a user-visible path that 4.1 shows is unmeasured. Use disposable fixtures, warm-up, and at least five measured repetitions. Do not register it as a wall-clock pass/fail test.
- [ ] 4.4 Choose the next successor from the baseline, or record that no measured path meets the importance bar. Do not add a production edit in this group.

## 5. Evidence-gated successor

- [ ] 5.1 Implement only the defect or hotspot proven by section 3 or section 4. Record the reproducer or baseline first. Preserve durability, accessibility, provenance, and the closed privileged-command surface. Add no protocol message and no open-path command.
- [ ] 5.2 Add or extend a deterministic test for the product edit. A measurement-only change still needs the same-method comparison from the spec.
- [ ] 5.3 Repeat the affected measurement and the surrounding regression checks. Revert the edit when the targeted metric does not improve and no other proven benefit remains.
- [ ] 5.4 Run a separate read-only adversarial review of correctness, races, security, memory, test validity, and claim scope. Fix a material finding and retest it. Do not adopt an unmeasured suggestion.
- [ ] 5.5 Reassess. If another locally executable successor is proven, append the next numbered group and finish it before checking this task. Otherwise record which termination condition applies: local work exhausted, external boundary, or remaining ideas speculative or harmful.

## 6. Final record and review

- [ ] 6.1 After the last production edit, rebuild from the committed implementation SHA and repeat the affected certification gates. Do not call a later documentation tip exact-head certified unless those binaries were rebuilt from it.
- [ ] 6.2 Publish the before/after table for every optimized metric, including baseline SHA, candidate SHA, fixture, repetitions, and regression guards. Omit metrics that were not measured.
- [ ] 6.3 Inspect the staged diff for secrets, host-private paths, and unrelated files. Run `git diff --check`. Commit coherent slices. Push the overnight branch without force. Open or update a reviewable pull request. Do not merge it.
- [ ] 6.4 Inspect the hosted run for the pushed SHA once. If the jobs fail before any step with the known account-payment or spending-limit annotation, record `BLOCKED_EXTERNAL` with the run id and do not relaunch that workflow.
- [ ] 6.5 Write the handoff under this change's evidence: executive status, truth-audit deltas, performance table, actual validation counts and commands, branch, SHAs, pull request, and only the remaining external or speculative work.
