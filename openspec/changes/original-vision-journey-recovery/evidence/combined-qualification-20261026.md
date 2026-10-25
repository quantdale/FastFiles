# Combined native product qualification — 2026-10-26

Immutable local integration **a3e20abb2cf5f97247ae632751743ce518ace69c** combines PR16 → PR14 → PR15 → PR17/18 → PR19, then status implementationcd1f429. Source merges without C++ conflict; only workstate history required reconciliation. This is a disposable, unpushed integration branch, not a shared main merge or release. All existing PRs remain unmerged.

| Gate | Actual result |
| --- | --- |
| Diagnostic all-target incremental build | 56/56 PASS |
| Normal Debug all-target build | 480/480 PASS |
| Unsigned release-test all-target build | 480/480 PASS |
| Debug selected full suite | **131/131 PASS, 262.73s** |
| release-test selected full suite | **131/131 PASS, 212.88s** |
| Native full E2E | **8/8 PASS, zero skips** |
| Strict native focus | normal and narrow dual PASS, both actual screenshots reviewed |
| Native visual catalog | **21/21 assertions PASS, zero skips; not global visual approval** |

Both full suites explicitly exclude the restricted live `ffverify_process_reaper_ps_tests`; they include the new non-destructive actual-image fixture regression. No live termination experiment or privileged trust-store test was executed. Source-SHA/clean binding passes for normal, release-test and diagnostic sets; embedded tree hash is absent. [Compact provenance, per-binary hashes, log hashes and capture identity](combined-qualification-20261026/qualification.json).

## Actual pixel review

[Normal focus](combined-qualification-20261026/focus-normal-a3e20ab.png) and [narrow dual focus](combined-qualification-20261026/focus-narrow-dual-a3e20ab.png) at measured120DPI show the selected report.txt in view, independent pane paint/selection, a fully readable compact degraded badge and single-line ellipsized footer. These combine PR19's hierarchy/paint repairs with the status sibling, rather than inferring combined behavior from separate unit tests.

PR16 resolves the status sibling's three Gallery theme assertion failures: all four Gallery states pass on this combined source. Pixel review nevertheless rejects [narrow debug Gallery](combined-qualification-20261026/gallery-narrow-rejected.png) for horizontal content clipping. WindowShell supplies a whole physical client rectangle to a Gallery render already translated into the content's DIP coordinates; this is a separate debug-surface geometry finding. Brightness/title assertions do not certify layout. It remains open for remediation.

The broader file-operation audit subsequently found an unnamed native Replace action and incoherent dark conflict dialog. Its baseline is recorded in the isolated conflict-actions slice; this combined certification does not falsely certify the unfinished modal conflict, permanent-delete, cancellation or retry UI journeys.

## Boundaries retained

No repeated2M/5M benchmark, no new performance claim, no result-correctness waiver and no closure of residency task4.1 follow from CTest. Previously bound canonical/Store evidence remains separately scoped. Explorer drag-in remains skipped/unqualified. Independent review resources are unavailable. Physical High Contrast/other DPI/reduced-motion, privileged signer/pins/raw-volume, installation/clean-host and release qualification remain unexecuted. The combined source has no hosted run; PR20's separately pushed documentary tip received six zero-step hosted failures with payment/spending annotation, not executed test failures. No billing changes or reruns.
