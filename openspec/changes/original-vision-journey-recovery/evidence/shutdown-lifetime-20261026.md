# Native shutdown lifetime — 2026-10-26

The ordinary native suite on clean implementation9195788352a7bdba1996e42bda3c48570b794037 is seven PASS / one FAIL. Home navigation is accepted, UIA Close returns without error, but process exit is not observed within the unchanged ten-second deadline; the outer failure later sees no window/process. The failed receipt remains in [PR #23](https://github.com/quantdale/FastFiles/pull/23). A successful retry does not identify its cause.

This independent successor starts at verified PR21b028e1165e7d6199bc3cc04d8fece0a9b5f443aa. Source review confirms three concrete hazards:

- Main calls OleUninitialize while WindowShell and its COM members still exist, including initialization failure.
- Icon shutdown drains its pending queue instead of abandoning work that can no longer be displayed.
- File-type admission erases an unfinished async shared future at capacity. Its final release can wait; the old comments claiming otherwise are false.

Implementation a25633e2a3ea6332feccbf2260ecf7f51de36bf5 corrects OLE/shell scope, drops pending icons and post-stop results while retaining the active join, and admits new file-type work only when capacity exists after reclaiming completed states. Full-path cache keys and captured-state ownership remain. Active Shell calls and preview reads can still block; no new arbitrary close bound is claimed.

Append-only diagnostic47 emits metadata-only shutdown phases only when FASTFILES_UIA_DIAGNOSTICS is exactly1. Focused real async-gate admission and diagnostic tests pass2/2, helper follow-up1/1; full diagnostic build447/447. Independent final_backend_review approves the source delta, no P1/P2, with no native execution by that reviewer.

The [initial instrumented restore](shutdown-lifetime-20261026/a25633e/native-summary.json) passes one targeted scenario on clean a25633e. Its [source binding](shutdown-lifetime-20261026/a25633e/native-source-binding.json) records source-SHA/clean identity, with embedded tree identity unavailable. The [deduplicated phases](shutdown-lifetime-20261026/a25633e/phases.json) show both explicitly closed PIDs: WM_DESTROY to OLE end44/67ms, engine stop2/4ms, icon join1/3ms, shell destruction5/10ms. Shell destruction ends before OLE teardown in both.

Those are **two explicit natural closes**, not three: the old scenario's third restored Home instance went through owned fixture teardown without a natural-close observation. A failed local phase-collector assumption expecting three was corrected to scope the existing receipt honestly. The stronger actual scenario now requires third Home close through the same public helper/deadline and serializes all three observations. A fully closed-mock execution of that actual scenario is [RED](shutdown-lifetime-20261026/a25633e/third-close-red.log) against the old two-close contract, then [GREEN](shutdown-lifetime-20261026/a25633e/third-close-green.log), including explicit third-close-failure rejection. It invokes no live process/input/termination.

Additional independent inspection confirms a C++ data race: WM_DESTROY retires raw hwnd_ while preview, engine, file-type and aggregate workers read it. The follow-up uses atomic HWND publication and one loaded target per background post, retaining payload ownership on failure. Null repaint targets are ignored instead of invalidating the desktop. Atomic access does not prevent an already loaded HWND from being destroyed or reused, and does not identify the predecessor close timeout's cause. UI compilation with the atomic member passes; fresh native three-close and broader qualification are pending.

Task4.8 remains open until current-source native and regression qualification. No timeout, ownership, signing, privileged process or security control is weakened. All existing PRs/main and the original dirty checkout remain preserved.

## Strengthened three-close milestone

Clean dbc3858aec612c5076355218c42a4c3acb0bf5eb passes both the [targeted restore](shutdown-lifetime-20261026/dbc3858/restore-summary.json) and [ordinary native suite8/8](shutdown-lifetime-20261026/dbc3858/native-summary.json). Preserve the adjacent source-binding files: summary alone omits that gate. All three close observations return successfully and observe exit without fallback. The unchanged ten-second process poll starts after the synchronous UIA Close call; it is not a hard bound on that call or active Shell operations.

Independent final_evidence_review matches all three PIDs to deduplicated actual phase records (78 cumulative records →39 unique,13 per PID). [Observed phase timings](shutdown-lifetime-20261026/dbc3858/phases.json): WM_DESTROY through OLE end50/37/52ms; shell destruction7/8/5ms and always before OLE begins. The reviewer checks actual source/UI binary hashes and finds no P1/P2 within that bounded evidence. Embedded tree identity remains unavailable. This does not identify the predecessor timeout's cause or qualify arbitrary Shell-call completion.

The reviewer notes that the third lifecycle's final framework cleanup result is not surfaced by the old scenario. Strengthen the actual scenario to check cleanup before PASS and retain all three cleanup observations, with a closed-mock negative proving a failed final cleanup is rejected. [Red](shutdown-lifetime-20261026/dbc3858/final-cleanup-red.log) reproduces the false-green gap; [green](shutdown-lifetime-20261026/dbc3858/final-cleanup-green.log) passes four focused tests including the registered-window ownership regression.

This qualification branch now includes published PR22's title-only Explorer cleanup removal as a real test dependency. Future UI runs close only explicitly registered Explorer windows; fixture directory names never authorize closing desktop windows. Target the successor PR at PR22 to keep its review diff free of duplicate safety changes. No shared branch or existing PR is merged by this local simulation. Compiled UI implementation remains dbc3858; new cleanup/harness qualification requires a fresh clean source run before task4.8 closes.
