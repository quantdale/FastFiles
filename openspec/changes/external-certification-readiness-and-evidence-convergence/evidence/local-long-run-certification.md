# Local Long-Run Certification Evidence

- **Commit**: `c3c14fac57b343e9deeb793e4e6daf1aaca743b2`
- **Tree**: `69f347a7c7cc66a11b320eecb0a40d1181c28434`
- **Hosted CI Run**: `33807003147` on SHA `7b4315ea267acfc7c29c9c46cdfe29b79437e02d`
- **Execution Strategy**: Re-executed under `Invoke-LongRunCertification.ps1` and `Invoke-ResourceSoak.ps1`
- **Exact-Tree Binding**: Development tree evaluated on clean codebase baseline

## 1. Representative Stress Re-Execution Results

| Test Name | Target Executable | Iterations | PASS | FAIL | Timeout | Median (ms) | P95 (ms) | Disposition | Artifact SHA-256 |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **handshake** | `ffengine_client_handshake_tests` | 100 | 100 | 0 | 0 | 507.16 | 629.76 | **PASS** | `2e9294c25eb0a990...` |
| **framing** | `ffipc_framing_tests` | 100 | 100 | 0 | 0 | 1643.05 | 1788.18 | **PASS** | `c4bc82bfbfc3e43a...` |
| **uiserver** | `ffengine_ui_server_lifecycle_tests` | 25 | 25 | 0 | 0 | 5593.74 | 5633.0 | **PASS** | `7da982c279409dfc...` |
| **concurrency** | `ffengine_index_pipeline_concurrency_tests` | 20 | 20 | 0 | 0 | 197.69 | 290.08 | **PASS** | `d13d1bb0b51459aa...` |
| **dialog-focus** | `ffdialog_focus_return_tests` | 50 | 50 | 0 | 0 | 109.67 | 233.16 | **PASS** | `738c8e493bee311c...` |

## 2. Resource Soak Results (300 Seconds)

- **Target**: `D:\Documents\tryPython\FastFiles\build\debug\src\ui\FastFiles.exe`
- **PID**: `124240`
- **Duration**: `300.38s` (57 samples at 5s intervals)
- **Disposition**: **PASS**
- **Working Set**: Baseline `13.19 MB` → Final `63.37 MB` (drift rate: `175168.8395 B/s`)
- **Handles**: Baseline `147.0` → Final `724.0`
- **Threads**: Baseline `6.0` → Final `47.0`
- **GUI Resources**: `108.0 GDI` / `111.0 USER` objects
- **Transitions Exercised**: Navigation, Search, Settings, Command Palette, Dual Pane, Storage
- **Artifact SHA-256**: `849d9a84fd7c2161db3ac69f14090efe95b2409855be435b85240892040f923e`

## 3. Machine-Readable Evidence Files

- `long-run-handshake-100.json`: 100 consecutive client/server handshake lifecycles
- `long-run-framing-100.json`: 100 consecutive IPC framing lifecycles
- `long-run-uiserver-25.json`: 25 consecutive UiServer startup/shutdown lifecycles
- `long-run-concurrency-20.json`: 20 consecutive index pipeline concurrency stress runs
- `long-run-dialog-focus-50.json`: 50 consecutive dialog focus return transitions
- `resource-soak-summary.json`: 300-second periodic process and GUI resource soak time-series
- `long-run-summary.json`: consolidated machine-readable stress summary
- `external-frontier-summary.json`: consolidated 25-task external frontier classification summary