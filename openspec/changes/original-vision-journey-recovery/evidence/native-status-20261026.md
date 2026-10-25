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

[Compact capture metadata](native-status-20261026/captures.json). All files are marker-owned synthetic fixtures. Additional physical DPI/High Contrast/reduced-motion and live unavailable/retry/file-operation-error journeys remain separately unqualified. Current-source committed native qualification and full selected suites are pending.
