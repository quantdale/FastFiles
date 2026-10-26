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
