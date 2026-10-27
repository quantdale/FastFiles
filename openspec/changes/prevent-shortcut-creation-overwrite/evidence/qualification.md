# Shortcut safety qualification — 2026-10-27

## Proven defect and repair
Fresh verified base PR30 430b7111cb3acf91aadc28472d6fdf3146ee4f4d equals origin/fix/execution-conflict-consent-20261027 after fetch. Sole writer/root uses isolated shortcut-safety worktree. Existing owner dirty files and other PRs preserved.

Real controlled worker on original code fails four assertions: late destination overwritten; false completion; manual retry finds damaged sentinel; accepted cancellation creates 256/256. Raw cancel-baseline.log.gz is retained, not relabeled. Generated fixtures clean normally, no shortcut executed and no user data touched.

IShellLink IPersistStream serialization now round-trips through native IPersistFile.Load and IShellLink.GetPath. CREATE_NEW exclusively creates output. Handle-bound disposition cleanup removes only newly created files after stream/write failure; original and cleanup errors remain independently reportable. Cooperative cancellation checks each item, posts bounded progress and one truthful terminal event. It cannot interrupt a single serialization/write.

Focused real-worker shortcut test PASS (0.31s); separate writer PASS (0.09s): exact 12,000-byte multi-chunk/final short read; existing/locked bytes preserved; seek failure, failed second read after 4KiB and malformed oversized count leave no partial file; null stream; actual unprefixed >MAX_PATH output and checked owned cleanup. UI target warning-clean 68/68. No test forces filesystem cleanup failure or CloseHandle failure; reporting exists without claiming those OS failures reproduced.

## Boundaries
Worker tests use a real unprivileged STA and own message-only event window. They do not qualify Explorer drag, Alt-drag UI, the rare cleanup-error dialog's visual layout, storage, privileged NTFS, installer, physical displays or Recycle Bin recovery. Existing UI cleanup safety dependencies remain necessary before native application execution. New independent review unavailable after provider quota; root reviews source and evidence without claiming independence. Broader certification pending.

Native API references: [Shell Links](https://learn.microsoft.com/en-us/windows/win32/shell/links), [CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew), [SetFileInformationByHandle](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle). IPersistFile.Save second argument is remember-state, not an overwrite permission.

## Committed-source standalone certification

Implementation27daa1760009113e9bb5570c6c2ab199a0d6fdc1; test-only Unicode/ownership follow-up eda462a481181c6cc16596810799fd7d62a3709f. Post-commit full Debug build23steps PASS and selected CTest130/130 PASS, zero skips/failures,250.46s. Only ffverify_process_reaper_ps_tests excluded; no process termination experiment. Real shortcut source filename includes Chinese Unicode characters and native Load/GetPath verifies it.

The first full run is preserved as full-debug-tests.log.gz:129/130pass, one ffbuild_identity_binding_tests failure302.39s. Binaries were built before the implementation commit and embedded430b instead of expected27daa17; its package fixture binding assertions are part of the same test, not a separate failed CTest. The subsequent post-commit full rebuild fixes provenance; the original failure remains recorded.

Safe combined composition b868b4e81636475368d7074b0f7a1d4f03f120f3 has matching five whole component/test files and identical complete Link execution body (source-equivalence.json). No whole-executable equivalence is claimed. Combined Debug139/139PASS275.06s; release certification pending. Analyze of unchanged product source at328c7b4 PASS8steps; strict100/100. b868 only changes tests/workstate from328.

Actual combined owned-operations run fails before first copy: address helper reports missing Source tab label. Diagnostic Source request and refresh exist, cleanup passes, zero file-operation cases certified. Do not attribute this to shortcut code, declare it an OS limitation, or claim application-level file-operation PASS. Original result is under integration-20261028/shortcut-b868-owned; bounded discovery observation probe prepared, not yet executed while portable suites run.

## Combined certification and unresolved native boundary

Combined b868 Debug139/139PASS275.06s and release-test139/139PASS245.86s, zero failures/skips. Only restricted reaper excluded. Full builds and unchanged-source Analyze/strict logs retained. Original native-owned-b868-failure.json records zero operation cases and successful cleanup.

Two fresh single-navigation probes then passed on unchanged product/helper code. The first diagnostic injection did not reach the exported function and yielded no observations; no instrumentation proof is claimed. The corrected module-context probe records recursive label1: Source, direct native Button8300 label1: Source, and exact owned Source terminal column9; normal cleanup passes. These do not explain or waive the first failure, or certify full file-operation journeys.

Source inspection identifies bounded recursive navigation-label lookup (1500ms,2000nodes) despite existing direct chrome helpers elsewhere, and an unconditional second Enter after label lookup fails even when focus has left the address edit. A separately isolated native-verification hardening slice is the next frontier; actual failure causality remains unproven. No further unchanged-source full UI retry loop.
