# Atomic Column View accessibility checkpoint

## Source and isolation

Successor starts at `981a9ba678a191107c78eec22a60c9620eb5848e`, verified equal to fetched `origin/feat/original-vision-product-20261025`. Sole root writer uses `column-uia-20261025`; original dirty checkout is preserved. New source has no engine/indexstore/protocol or privileged-process changes.

## Defect and repair

The real provider resolves stable item identity, then called ordinal-only mutation methods. A deterministic headless surface replaces that row after Resolve returns its identity. Old provider: **12 failed assertions**, covering Select, AddToSelection, RemoveFromSelection, SetFocus, Invoke and ScrollIntoView: each returns success and mutates a replacement. New provider retains original path/item/owner/fragment identity in a closed request; focused tests pass.

Native execution synchronously marshals to the owner window. Validation shares `columnsMutex_` with selection/focus/scroll mutation or canonical activation's copied target. Add/Remove evaluate membership under that lock. Shared production helpers retain selection and scroll math. Notifications, navigation, HWND focus and SendMessage run outside the model lock. Root and item HWND focus run on the owner thread. No accessibility-only model is introduced.

Additional dispatch-boundary tests reject retired fragment/owner tokens, hidden Browse, Home replacement and changed column path, and verify Add/Remove idempotence when membership changes during dispatch.

## Development-source checks

- `dev.ps1 build -Preset debug -Parallel 8`: all targets built warning-clean; incremental rebuild after final focus change passes.
- Focused CTest: **7/7**, including ColumnView/Home/Search providers, selection identity/model, geometry and event routing (1.26s).
- `openspec validate column-uia-identity-dispatch --strict`: PASS.
- Real native journeys and clean committed-source wider checks remain pending; these headless results do not certify a live race or visual behavior by themselves.

Ignored session evidence: `build/column-uia-20261025/{red-test,green-test,native-build,native-incremental,focused}.log`.

## Parent checkpoint qualification

Unchanged parent product981a9ba selected normal Debug **126/126** (231.85s) and release-test **126/126** (226.04s), footer-aware native catalog **21/21** at measured120DPI; restricted `ffverify_process_reaper_ps_tests` explicitly excluded. Prior full native fc2fab7 E2E **8/8** remains inherited evidence until successor runs. Hosted run40200176964 for981a9ba: six jobs, zero steps; annotation confirms account payments/spending limit. No rerun/billing modification.

Disposable integration3179eb4 combines PR16/14/15/17 with parent981a9ba; only WORKSTATE textual conflict resolved preserving both histories. No shared branch merge. Build/qualification of that integration is pending.

## Qualification boundaries

Independent review is unavailable after reviewer credit exhaustion and a fail-closed Hy3 doctor (required route alias missing); older approvals do not cover this successor. Explorer drag-in remains unqualified, not PASS. Physical High Contrast, additional DPI/reduced-motion and privileged/release requirements remain separately unexecuted. No task4.1 residency acceptance waiver or result-correctness relaxation.

## Committed item-action checkpoint d9f0a1f

- Clean committed-source selected Debug **126/126**,333.03s; restricted reaper explicitly excluded.
- Actual native E2E **8 passed /0 failed /0 skipped**, all ordinary scenarios; source identity d9f0a1f4dcdc3746d4180a1b2a0c114e06949fb1, clean start, `boundToHead=true`. Tree hash embedding is absent (`boundToTree=false`, `NOT_EMBEDDED`); this is SHA/clean-checkout provenance, not embedded-tree certification.
- Native catalog **21/21**, actual120DPI; cleanup passes. Privacy redaction obscures much of Browse in public captures, so those masked regions do not establish visual focus quality. Local semantic bounds and real navigation remain separate evidence.
- Analyze native target **67/67** warning-clean; release-test all-target build **445/445** passes. Release CTest not yet run on this checkpoint.

Follow-up: stale Column SetFocus still returned success and focused a replacement folder. Two additional red assertions fail on d9f0a1f. Column and pane focus now use closed original-lifetime dispatch; column path validation shares the native mutation lock. Native navigation adds explicit public root/row focus and unchanged-selection checks. Current follow-up qualification is pending.

Follow-up development-source build **59/59**, focused provider/geometry/selection/E2E-harness/drift CTests **9/9** (6.01s), PowerShell parse **0 errors**. Native focus journey still pending.

## Visible focus and pane isolation — 2026-10-26

The checkpoint d34198a completed selected normal Debug **126/126** (220.21s) and release-test **126/126** (214.72s). Restricted reaper remained excluded. Actual E2E **8/8**, catalog **21/21** and native Analyze **13/13** passed, but masked screenshots and focus flags missed a real visible-navigation defect.

A fresh clean-source red probe on d34198a waits five seconds: **focus=True / offscreen=True**, row `1226,466,300,48`, pane `540,384,686,503`; row begins at the pane's right edge. Screenshot [before](images/focus-offscreen-before.png) shows the missing child. The old scratch capture incorrectly used a Git SHA in its applicationSha field; that field is not executable-hash evidence. Source observation uses the separately recorded clean d34198a build/checkout, and this image is diagnostic evidence rather than embedded-tree certification.

Owner actions now pass through the shell's normal post-input selection, location, preview and animated reveal boundary. Mouse/keyboard input shares the same helper. A pure reveal seam preserves the filename edge in an oversized column. Development green: **focus=True / offscreen=False**, row `734,274,301,48`, pane `348,192,686,503`; [actual linked hierarchy](images/focus-revealed-development.png) now includes the focused file and correct Project breadcrumb.

Actual narrow dual-pane validation requested620x780 and measured a **604x772 DWM frame at120DPI**. The first two probe failures used a wrong assertion: a pane deliberately reports HasKeyboardFocus=false while its child row owns focus (provider property comment documents this). Corrected row-based assertions on the same source confirm inactive-pane ScrollIntoView preserves the original focused row. The test now waits for complete leading-edge alignment rather than accepting a sliver during animation.

Visual review then rejected the semantically passing [before pane clip](images/pane-spill-before.png): a partly scrolled column in pane1 painted over pane0. Native Browse panes had no per-pane clip. Rendering now clips each pane's entire background/rows/focus to its actual viewport, and UIA geometry is published after scroll offsets synchronize, using the same frozen pane1 offset as that frame. Shared pane1 offset access is locked. [Development correction](images/pane-clip-development.png) shows the focused report in pane0 and the independent report in pane1. Narrow bounds: focused row `324,242,300,48`, pane0 `324,160,141,643`; inactive row `465,242,301,48`, pane1 `465,160,141,643`; original row focus remains true, other row focus false. Teardown and pipe cleanup pass.

These green captures were made from uncommitted development source and are not labeled exact-SHA certification. The reusable strict-provenance native regression is `verify/e2e/Test-ColumnFocusVisibility.ps1`, modes normal/narrow-dual; committed-source execution remains pending. Native E2E now asserts physical focus visibility after child-folder Invoke. Remaining observed visual polish: narrow status/badge text wrapping; it is separate from the corrected focus/pane isolation frontier. No claim of product completion or blanket physical display coverage.

Visible-focus/pane-clip development checkpoint: focused normal Debug **9/9** (7.16s), warning-clean native Analyze **13/13**, strict change validation PASS. Dedicated committed-source native regression and full current-source qualification remain pending.

## Final local implementation qualification — 93cd2b6

Immutable implementation: `93cd2b604641dc38c56940e3330ec2acb74962af`, pushed as draft [PR19](https://github.com/quantdale/FastFiles/pull/19), stacked on PR18. Clean-checkout/source-SHA binding passes; embedded tree hash remains NOT_EMBEDDED and is not claimed.

| Gate | Actual result |
| --- | --- |
| Normal Debug selected CTest | **126/126**,246.03s |
| Normal release-test selected CTest | **126/126**,217.85s |
| Restricted process reaper | Explicitly excluded from both; NOT_EXECUTED |
| Native E2E | **8/8**, including physical focus visibility after public child Invoke |
| Strict native focus checks | **normal PASS**, **narrow-dual PASS**, cleanup PASS |
| Native visual catalog | **21/21**, light/dark, measured120DPI; privacy-masked regions are not pixel approval |
| Analyze native target | Warning-clean current implementation/identity build |
| Focused regression development source | **9/9**,7.16s; prior identity replacement red12assertions and stale-column-focus red2assertions retained |

The exact-source unmasked [normal](images/focus-certified-normal-93cd2b6.png) and [narrow dual-pane](images/focus-certified-narrow-dual-93cd2b6.png) captures were visually inspected: original file visible, correct breadcrumb, focus preserved, independent pane clipping. Compact provenance is [certified-focus-93cd2b6.json](certified-focus-93cd2b6.json). Owned-sidebar tooltips occlude small sidebar regions; those regions are not claimed unoccluded. The native test's original generic normal-mode detail incorrectly mentioned inactive-pane preservation; reporting is corrected to reflect actual mode coverage. Only narrow-dual mode executes that assertion.

### Combined integration

Disposable local `a0afd5f0d7ee4dd2a211cf53108b76cff52fd3b1` combines PR16/14/15/17/18/19. Diagnostic build **72/72** incremental nodes; combined native E2E **8/8**; strict normal/narrow-dual focus both PASS with clean teardown. Focused combined frontend and Store/projection/pipeline **12/12**,11.75s. No C++ merge conflict; earlier textual registration/lifecycle/workstate conflicts were resolved preserving both histories. This is an unpushed simulation, not an owner-controlled main merge or full normal combined release certification.

### External and unqualified boundaries

Hosted40222586704 for93cd2b6: **six jobs,zero steps**, payment/spending-limit annotation. No rerun/billing changes. Independent source re-review remains unavailable after native reviewer credit exhaustion and one fail-closed Hy3 doctor; no historical approval is transferred to this source. Physical additional DPI/High Contrast/reduced-motion, privileged signer/NTFS/clean-host and release gates remain unqualified. Explorer drag-in remains skipped, and native conflict/permanent-delete/cancellation journeys are still unexecuted work rather than external passes.

The next separately isolated slice fixes the visibly wrapped narrow status/badge text. Its worktree starts from PR18 because those paint changes do not depend on this action/identity correction. All PRs remain unmerged and no production release is made.
