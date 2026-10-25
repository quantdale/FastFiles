# Native conflict action recovery — 2026-10-26

## Result and source

Immutable validated standalone source **f10654156a5450790e4ab2623a105bcdab9f0236** and disposable combined source **63ffa59532d8c874bc3e0dbe7a9eece95a06d54b** each pass **8/8 native conflict decisions** plus **8/8 full native E2E, zero skips**. The combined source includes PR16/14/15/17/18/19, statuscd1f429 and this conflict/lifecycle-harness change. No shared branch merge or release was executed.

This sibling starts at fresh verified PR18 **981a9ba**; it contains no PR19/20 commits. Product C++ implementation **1dbff32** remains unchanged in later harness/evidence commits. [Compact exact-source provenance, per-binary hashes, all native cases, measured frames and retained failures](conflict-actions-20261026/qualified.json).

| Qualification | Actual result |
| --- | --- |
| Diagnostic all-target build | fresh445/445 plus current-source identity rebuilds PASS |
| Native MSVC Analyze | 67/67 PASS on unchanged C++ implementation |
| Focused policy/selection/drift | 3PASS; diagnostic identity binding1SKIP by policy, not PASS |
| Current headless E2E/drift | 2/2 PASS, 4.78s; earlier driver/drift/harness3/3 PASS |
| Actual standalone full E2E | 8/8 PASS, zero skips |
| Actual standalone native conflict | 8/8 PASS; both cleanup lifecycles PASS |
| Focused actual restore | 1/0/0 PASS; both close observations preserved |
| Combined native conflict / full E2E | 8/8 and8/8 PASS, zero skips |
| Combined normal provenance/doc/harness checks | 4/4 PASS, 40.74s |
| Combined normal semantic/operation checks | 10/10 PASS, 12.43s |

Source-SHA/clean binding passes; embedded tree hash is absent. Earlier combineda3e20ab131/131 Debug/release-test evidence is [separately bound](combined-qualification-20261026.md), not falsely reported as a full-suite execution on this later candidate. No unchanged expensive benchmark was repeated.

## Actual user journey and before/after

Public UIA/palette Copy and Paste create collisions only within fresh marker-owned fixtures. Every decision verifies all five native names and physical bounds, default Skip focus, a complete five-control Tab cycle, exact destination outcome and unchanged incoming/sentinel hashes. Light uses native pointer activation; dark uses real Tab/Shift+Tab, Space and Escape. Replace updates only its intended fixture destination; Keep both preserves the existing destination and creates the incoming copy under a distinct name; Skip and Cancel preserve both contents.

[Before dark](conflict-actions-20261026/before-dark.png): actual a3e20ab UIA cannot find painted Replace because the HWND caption is empty; the custom window remains white and checkbox text is black on dark. [Red metadata](conflict-actions-20261026/before.json) records actual Cancel preserving both hashes. ConflictDialog/UiComponents/UITheme source is identical between verified base981a9ba and a3e20ab; full apps are not claimed equivalent.

[After light](conflict-actions-20261026/after-light.png) and [after dark](conflict-actions-20261026/after-dark.png) are actual source-bound f106541 captures, visually reviewed and accepted. Both measure **120DPI**, DWM809×367 physical pixels. Mean brightness243.25/27.93 proves real theme change. Independent combined63ffa59 captures have the **same SHA-256 bytes**, so these two committed PNGs serve both actual runs without redundant images. Metadata preserves their distinct window positions/handles/source identities. The helper queries window DPI; no DPI value is inferred from a test label.

Keep the established Native Precision Things/shadcn reference lock, Segoe UI/tokens and localized warning around Replace. Supply its native caption/Tab stops, erase the custom HWND with the token brush, reuse Settings' native Explorer/High Contrast helper, and use themed checkbox text/body font. No custom accessibility-only state, operation decisions, conflict policy, control IDs or security/process boundaries change.

## Retained failures and native close evidence

- Prototype audit01 used the wrong cleanup property; actual teardown was clean. Audit02 demonstrates missing Replace; audit03 captures the dialog but encounters this host's missing native Cancel InvokePattern; audit04 reuses the existing real-pointer fallback and preserves the conclusive red plus both content hashes. No product assertion became a fabricated PASS.
- Initial f4c28b6 native conflict run **7/8**: test `+{TAB}` becomes plain Tab under this driver. Correct `{SHIFT}{TAB}` and its explicit four-event dry-run repair the test; later75 and current-source runs pass8/8. Product C++ did not change for this correction.
- Broader75 E2E **7PASS/1FAIL**, Home-close after accepted Home, remains in the compact evidence and original logs. Old route telemetry was absent; its exact cause is **unproven**. Do not claim a later primary-route pass establishes the original cause.
- Independently, the old close fallback `%{F4}` demonstrably emits plain F4. Closed-mock tests execute the real close helper and real translator **-NoSend**, red→green for actual Alt/F4 down/up events, preserving UIA-first routing, ownership checks and the unchanged10s exit bound. No live termination experiment runs. New observations record UIA return/error, fallback attempt/keys/events/error and observed exit.
- First instrumented restore8768554 passed but lost close note properties when serializing an OrderedDictionary. A closed-mock execution of the actual three-lifecycle scenario reproduces the JSON loss; dictionary-key storage passes the round-trip regression. The fixture exports only its wrapper so mocks cannot replace other harness functions.
- Current focused restore and full standalone/combined E2E all preserve two successful closes each: UIA Close returned and exit was observed within10s; **fallback was not attempted** in those native runs. Actual fallback input is qualified only in the non-destructive headless fixture. Initial Home-close attribution remains unproven; production shutdown code is unchanged.

Transient original logs remain under each worktree's ignored build campaign directory, with hashes in the compact artifact. High Contrast/other physical DPI/reduced-motion, multi-conflict apply-all, ongoing-copy cancellation, permanent-delete/error/retry UI and Explorer drag remain separately unqualified. Native conflict Cancel is not a long-running operation-cancellation qualification. No hosted, independent-review, privileged or release approval is inferred.
