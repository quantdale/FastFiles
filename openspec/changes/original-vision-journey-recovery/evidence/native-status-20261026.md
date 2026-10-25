# Bounded native status feedback — 2026-10-26

## Evidence and decision

Actual PR19 [narrow before](https://github.com/quantdale/FastFiles/blob/93cd2b604641dc38c56940e3330ec2acb74962af/openspec/changes/column-uia-identity-dispatch/evidence/images/focus-certified-narrow-dual-93cd2b6.png) shows the engine badge wrapping below its chip and a long footer path painting a clipped second line. The affected status drawing is unchanged from this sibling's verified PR18 base981a9ba; the full applications are not claimed equivalent. This branch intentionally contains no PR19 commits. Combine both siblings for overall hierarchy/paint behavior.

The fix creates cached DirectWrite one-line layouts with character ellipsis, using the native [ellipsis API](https://learn.microsoft.com/en-us/windows/win32/api/dwrite/nf-dwrite-idwritefactory-createellipsistrimmingsign). Layouts rebuild only when status text or available width changes and reset with existing theme/device resource invalidation. The shared wrapping formats used for details/loading/recovery are untouched. Transient completion/recovery feedback precedes long selection/path metadata. Full status/model descriptions and filesystem identities remain unchanged.

Compact badge text says Basic search (degraded), Indexed search only in a connected privileged presentation, and Engine unavailable · Retry in unavailable/fallback states. These labels derive from the existing connection-state policy; they do not authorize or simulate a privileged path. No live privileged indexing qualification is claimed.

## Refero research and reference lock

Used live `refero_get_style` for Things0796cd74-edc2-4e12-9c71-25fac02a1cb2 and shadcnc14c0a94-1037-449e-bf5b-4cb972656ac7; `refero_search_screens` for compact file/status metadata; `refero_get_screen` for [file metadata f4eced6d](https://refero.design/pages/f4eced6d-0f61-411a-a273-4261dac50176) and [indexing status1a805882](https://refero.design/pages/1a805882-3dae-4ef8-9b03-4b66a30f2e4f). Retain Native Precision's Segoe UI, calm surfaces and semantic colors. Adapt the hierarchy that keeps compact status secondary to files and separates detailed explanations. No source demonstrated Windows ellipsis behavior; that decision comes from the measured native clipping defect. No third-party font, branding or asset is copied.

## Development validation

- Initial Debug all-target build445/445 and focused status/style/text/metadata/drift5/5 pass; diagnostic445/445 build passes.
- Real DirectWrite red: NO_WRAP alone still produces a second line for explicit CR/LF in long Unicode/error text (one failed assertion). Display-only line-separator flattening repairs it. The actual layout reports one trimmed line, preserves the original shared WRAP format, and rejects zero-width resize by clearing stale layout.
- Latest focused5/5 (2.70s), native Analyze67/67 warning-clean, strict original-vision change validation PASS.
- Four actual native captures: light/dark at requested1280 and620 widths; measured DWM widths1264 and604, height772, **actual120DPI**. Mean brightness235.59 versus32.54 establishes real light/dark change. Clean lifecycle/pipe teardown passes. These are development-source observations, not exact-commit certification.
- Visually inspected [light narrow](native-status-20261026/status-light-620.png) and [dark narrow](native-status-20261026/status-dark-620.png): full degraded caption and single-line footer with ellipsis, no clipped second line. Other parent-branch navigation/paint behavior belongs to PR19; no complete-product approval follows from these captures.

[Compact capture metadata](native-status-20261026/captures.json). All files are marker-owned synthetic fixtures. Additional physical DPI/High Contrast/reduced-motion and live unavailable/retry/file-operation-error journeys remain separately unqualified. See the committed-source qualification below; no complete-product approval is inferred.

## Committed-source qualification

Immutable implementation **cd1f429bd6159da2615aa452bdcf97fef950c909**:

- Unsigned release-test all-target build446/446; selected suite **126/126 PASS, 227.84s**, restricted live process-reaper explicitly excluded.
- Normal Debug initial suite **125/126, 260.40s**: only build-identity binding fails because its five binaries still embed parent981a9ba. Rebuild19/19 repairs the stale identity; clean-source provenance preflight and the failed binding regression **1/1 PASS, 0.48s**. Retain the initial failure rather than report a nonexistent all-green Debug rerun.
- Actual native full E2E **8/8 PASS, zero skips**, clean-source preflight and lifecycle cleanup. Browsing, Search destination, Storage, preview, ordinary operations and restore remain covered by the existing registry; modal conflict/permanent-delete/cancellation UI journeys are not implied.
- Fresh four-frame Settings-driven status matrix PASS at measured120DPI: requested1280/620, actual DWM1264/604x772. Light/dark normal brightness235.09/32.32. Both narrow images visually reviewed: compact degraded label and single-line ellipsized footer remain legible. [Light](native-status-20261026/certified-status-light-620.png), [dark](native-status-20261026/certified-status-dark-620.png), [compact source/capture metadata](native-status-20261026/certified-captures.json). Source-SHA/clean binding passes; embedded tree hash is absent.
- Standalone catalog **18 PASS / 3 FAIL**, zero skips. Gallery dark/narrow/wide reproduce the F9 FollowSystem defect already owned by unmerged PR16/e75a230d; that commit is intentionally absent from this PR18-based sibling. Failed evidence remains under build/native-text-20261026/committed-visual. Existing Column View clipping/focus repairs belong to sibling PR19, not this change. Combined-source qualification is separately recorded after it runs.

Transient logs and all four original captures are preserved under build/native-text-20261026. Full High Contrast/other physical DPI/reduced-motion, live unavailable/retry/error notices and Explorer drag-in remain unqualified. No privileged-path, hosted or release certification follows from local development evidence.

Combined disposable integration **a3e20abb2cf5f97247ae632751743ce518ace69c** merges PR16/14/15/17/18/19 and statuscd1f429 with no C++ conflict. Diagnostic build56/56 and clean-source gate pass; actual catalog **21/21 assertions PASS, zero skips**, at120DPI. This confirms the existing PR16 dependency resolves the sibling's three theme failures. Pixel review still finds horizontal content clipping in debug Gallery narrow; catalog brightness/title assertions do not certify all visual geometry. Production status text and Column View focus are separate journeys.
