## 1. Repair client initialization

- [x] 1.1 Implement installed-provider metadata selection with case-only identity adjustment, path binding and portable rejection/nonmutation tests. Common.ps1; 18 portable checks PASS.
- [x] 1.2 Implement no-output process-local initialization, cached failures and reload-safe reuse while preserving mock operation independence. Common.ps1/AppDomain marker; public RootElement warmup fixes cold registration; fresh client smoke and portable tests PASS.

## 2. Repair capture observation

- [x] 2.1 Correct only ThreadHasCapture to use the returned thread ID and preserve invalid-handle failure and any-source-thread capture semantics. Own native capture/release parent/sibling and zero/destroyed HWND checks PASS.
- [x] 2.2 Add and execute an explicit own-native diagnostic for standard patterns, genuine title events, same-thread child capture/release and normal cleanup. Warm PASS and final source-hashed ColdStart PASS4.44s; normal close/no remaining HWND in both.

## 3. Validate and hand off

- [x] 3.1 Run focused portable UIA tests, strict OpenSpec and documentation/lifecycle checks; preserve compact evidence and request independent review. Focused PASS5.42s; strict92/92,drift/lifecyclePASS; independent source approval/noP1/P2 including cold-start delta and cache boundary.
- [ ] 3.2 Integrator: qualify actual owned FastFiles checkbox/combo and genuine operation title events on combined safe-cleanup source.
- [ ] 3.3 Integrator: rerun affected owned-operation/native acceptance, retaining original failures and honest Explorer capture limitations.

## 4. Repair E2E preflight fidelity

- [x] 4.1 Add a headless actual-runner contract with owned fake provenance module proving correct build root, all five binaries, missing-engine retention and preflight stop; preserve the old runner's red result. Test-E2EProvenance.ps1: old red captures both UI-only sets; corrected20checksPASS/no app execution.
- [x] 4.2 Correct only the E2E runner's build-root parent count and missing-sibling filtering; rerun focused harness/provenance contracts, strict/drift and independent review, then commit a separate milestone. Existing harness plus newcontractPASS; provenancePASS; strict92/92/driftPASS; independent review no actionableP1/P2.
