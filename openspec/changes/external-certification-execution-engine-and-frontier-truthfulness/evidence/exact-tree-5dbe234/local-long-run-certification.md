# Local Long-Run Certification Evidence

- **Commit**: `5dbe234248f630e58aecc621ac3c7a6cacb8e449`
- **Tree**: `8f648afcbcd04a0a211c921fd168a37cc1d523c4`
- **Exact-Tree Binding**: True
- **Execution Strategy**: Re-executed under hardened `Invoke-LongRunCertification.ps1` and `Invoke-UiaLongRunCertification.ps1`
## Representative Stress Re-Execution Results

| Test ID | Command / Target | Iterations | PASS | FAIL | Timeout | Median (ms) | P95 (ms) | Disposition | Artifact SHA-256 |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **ffengine_index_pipeline_concurrency_tests** | `build/debug/tests/engine/ffengine_index_pipeline_concurrency_tests.exe` | 20 | 20 | 0 | 0 | 692.96 | 805 | **PASS** | `976de995940d83cc593f0fa09683906b2ecef3be88398eaffdc7e801ab81d98b` |
| **ffdialog_focus_return_tests** | `build/debug/tests/ui/ffdialog_focus_return_tests.exe` | 50 | 50 | 0 | 0 | 46.13 | 55.73 | **PASS** | `361ba497323c3599e2342c1933f4804aedb61eb6fbd291e9066e9f16f337228c` |
| **ffipc_framing_tests** | `build/debug/tests/ipc/ffipc_framing_tests.exe` | 100 | 100 | 0 | 0 | 1659.83 | 3595.94 | **PASS** | `cf80fbbee68256fe048b0ca4b7c67cc7d6e22f9ca70ebad32c1a015af6f7b785` |
| **ffengine_client_handshake_tests** | `build/debug/tests/engine/ffengine_client_handshake_tests.exe` | 100 | 100 | 0 | 0 | 499.39 | 516.75 | **PASS** | `7520511d8031d63b25d32c419ab92195904c5d194f70c897316ef3301621f44e` |
| **ffuia_application-lifecycle** | `build/debug/src/ui/FastFiles.exe` | 100 | 100 | 0 | 0 | 450.14 | 476.81 | **PASS** | `cdd953da5c3c1ac51aeb51fa0fec7297d792e54d294149a81bebffdc2fdf9eed` |
| **ffuia_rapid-input** | `build/debug/src/ui/FastFiles.exe` | 100 | 100 | 0 | 0 | 11504.44 | 11658.07 | **PASS** | `eb3a32bf3365c028a7e23b77363bc1559589111cad7fb2a3e2ac0ca420550139` |
| **ffengine_ui_server_lifecycle_tests** | `build/debug/tests/engine/ffengine_ui_server_lifecycle_tests.exe` | 25 | 25 | 0 | 0 | 5722.41 | 12970.77 | **PASS** | `ec782982eaed8d5c6b29d7e4b6f67552a0ff14f21a5cf0efbda489e7c8e63949` |

## Verification Standard

- Every CTest test was discovered prior to run (enforcing exactly 1 matching test; zero-test match fails closed).
- Every iteration verified start and completion proof from process execution output.
- All artifact checksums are verified on closed file bytes.
