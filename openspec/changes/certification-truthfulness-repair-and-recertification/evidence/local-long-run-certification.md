# Local Long-Run Certification Evidence

- **Commit**: `67f06b5203241cf8c6384bc58173fc6eba9bf2fb`
- **Tree**: `01dae7925217684c945bcaf90d717fa4448b60c8`
- **Exact-Tree Binding**: True
- **Execution Strategy**: Re-executed under hardened `Invoke-LongRunCertification.ps1` and `Invoke-UiaLongRunCertification.ps1`
- **Hosted CI Run**: [33833816000](https://github.com/quantdale/FastFiles/actions/runs/33833816000) on SHA `f635cc5327e10d6305c499278a7d8a291acd9be6` (6/6 SUCCESS)
## Representative Stress Re-Execution Results

| Test ID | Command / Target | Iterations | PASS | FAIL | Timeout | Median (ms) | P95 (ms) | Disposition | Artifact SHA-256 |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **ffuia_application-lifecycle** | `build/debug/src/ui/FastFiles.exe` | 100 | 100 | 0 | 0 | 1897.52 | 2707.62 | **PASS** | `fe8fda97bd94b62de538016460be9d824a149409dbebb5ad233c89bc37a625ad` |
| **ffengine_index_pipeline_concurrency_tests** | `build/debug/tests/engine/ffengine_index_pipeline_concurrency_tests.exe` | 20 | 20 | 0 | 0 | 219.88 | 302.87 | **PASS** | `dcf410e1119448180d1b8b32223ab376d1bab6b3181b9ca8471b1f83cbc739dd` |
| **ffdialog_focus_return_tests** | `build/debug/tests/ui/ffdialog_focus_return_tests.exe` | 50 | 50 | 0 | 0 | 118.41 | 203.82 | **PASS** | `6fa5fb98425cc956361f835f505324738f2eeaafbe47c6cb0a95c1f82254cd5d` |
| **ffipc_framing_tests** | `build/debug/tests/ipc/ffipc_framing_tests.exe` | 100 | 100 | 0 | 0 | 1652.39 | 1819.83 | **PASS** | `0921f8f8b7fade0666108e78d4faa1399a4285da59a69d2a10f22aca492297a7` |
| **ffengine_client_handshake_tests** | `build/debug/tests/engine/ffengine_client_handshake_tests.exe` | 100 | 100 | 0 | 0 | 506.96 | 626.94 | **PASS** | `b4ce72b510d1238788c81c7eb19d61330494fc8025821ecd7cb6cb74848cd345` |
| **ffuia_rapid-input** | `build/debug/src/ui/FastFiles.exe` | 50 | 50 | 0 | 0 | 12616.77 | 13898.75 | **PASS** | `bd84f466de3e4e332c37f135a173f74a717ab36901ecb0cf9c9ccb800f53423b` |
| **ffengine_ui_server_lifecycle_tests** | `build/debug/tests/engine/ffengine_ui_server_lifecycle_tests.exe` | 25 | 25 | 0 | 0 | 5632.28 | 5682.28 | **PASS** | `8fe25c485dcd85e41b499a7d795885589b0f3197baab2323ea9ca843ea60cf05` |

## Verification Standard

- Every CTest test was discovered prior to run (enforcing exactly 1 matching test; zero-test match fails closed).
- Every iteration verified start and completion proof from process execution output.
- All artifact checksums are verified on closed file bytes.
