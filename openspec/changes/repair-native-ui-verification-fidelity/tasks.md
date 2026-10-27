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
