# Dogfood finding ledger

| ID | Severity | Scenario | Reproduction rate | Evidence | Root cause | Fix commit | Regression coverage | Status |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| RLS-001 | P2 | Release-candidate reproducibility | 2/2 before fix | Two unsigned-validation candidates with identical payloads produced different ZIP SHA-256 values; `kit-manifest.json` differed only by the absolute temporary path of `fftest.exe`. | The existing kit embedded the output directory in detailed version-metadata evidence. | `3238f81` | `verify/release/tests/Test-ReleaseCandidate.ps1`: two fresh candidates, per-file checksum verification, and identical archive SHA-256. | RESOLVED |
| UX-001 | P3 | First-run Home with long recent-folder paths | Reproduced in the first-run and follow-up Home screenshots | Recent-folder card labels painted beyond their two-column card bounds and overlapped adjacent labels; UIA names remained correct. Evidence: UIA run `20260821-115745` Home screenshot; fixed-state run `20260821-120225` shows bounded ellipsis/clipping. | Home used direct no-wrap `DrawText` for unbounded path strings without a trimming layout. | `3fa37c5` | `ffhome_layout_tests`, `ffhome_interaction_tests`, `ffhome_uia_tests` PASS; rebuilt diagnostic UIA Home scenario PASS and visual inspection of `3.x-home-surface.png`. | RESOLVED |

Only observed or strongly source-proven current defects belong here. Fixture/tooling defects discovered before acceptance are tracked in the coordinator workstate and corrected before scenario evidence is counted.

## Campaign disposition

No new P0, P1, or P2 defect was observed in the committed measurement and
acceptance wave. The benchmark capacity-label correction in `ca2a3ff` is
measurement-harness evidence, not a product finding: the theoretical row
estimate is now distinct from the largest actually published snapshot. The
unsigned release-test launch refusal is expected fail-closed behavior when
production pins are absent, and was not treated as a defect.
