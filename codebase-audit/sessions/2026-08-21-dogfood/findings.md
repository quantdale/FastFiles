# Dogfood finding ledger

| ID | Severity | Scenario | Reproduction rate | Evidence | Root cause | Fix commit | Regression coverage | Status |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| RLS-001 | P2 | Release-candidate reproducibility | 2/2 before fix | Two unsigned-validation candidates with identical payloads produced different ZIP SHA-256 values; `kit-manifest.json` differed only by the absolute temporary path of `fftest.exe`. | The existing kit embedded the output directory in detailed version-metadata evidence. | `3238f81` | `verify/release/tests/Test-ReleaseCandidate.ps1`: two fresh candidates, per-file checksum verification, and identical archive SHA-256. | RESOLVED |

Only observed or strongly source-proven current defects belong here. Fixture/tooling defects discovered before acceptance are tracked in the coordinator workstate and corrected before scenario evidence is counted.
