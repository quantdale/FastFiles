# Resource Soak Certification Summary

- **Disposition**: **PASS**
- **Commit**: `67f06b5203241cf8c6384bc58173fc6eba9bf2fb` (tree: `01dae7925217684c945bcaf90d717fa4448b60c8`, exactTreeBound: True)
- **Duration**: 600.55s (113 steady-state samples)
- **Target**: `build/debug/src/ui/FastFiles.exe` (hash: `89f105f9b867ad353207aa4970152f7ec3dce596e5998daba850411bb0a666aa`)
- **Target PID**: 68200 (Engine: none (Diagnostic mode runs UI process with in-proc/mock engine or on-demand connection))
- **Readiness**: reached in 1608.21ms; warmup 15s

## Resource Metrics

| Metric | Startup Value | Steady Baseline | Median | Final | Steady Slope | Stability |
| :--- | :--- | :--- | :--- | :--- | :--- | :---: |
| **Working Set** | 2.98 MB | 86.48 MB | 87.98 MB | 88.12 MB | 2864.5741 B/s | PASS |
| **Private Bytes** | 0.64 MB | 68.04 MB | 67.89 MB | 67.89 MB | -259.1758 B/s | PASS |
| **Handles** | 40 | 521 | 536 | 536 | 0.025/s | PASS |
| **Threads** | 1 | 19 | 14 | 14 | -0.0083/s | PASS |
| **GDI Objects** | 0 | 108 | 108 | 108 | 0/s | PASS |
| **USER Objects** | 1 | 107 | 109 | 109 | 0.0033/s | PASS |

## Stability Legs

- **Process Survival**: PASS
- **Functional Activity**: SKIPPED (0 real transitions executed)
- **Resource Stability**: PASS
- **Graceful Shutdown**: PASS (graceful in 259.01ms)
- **Residue Census**: PASS (0 orphans)

## Artifact Checksum

- **JSON Artifact**: `resource-soak-summary.json`
- **SHA-256**: `3468ddf1f18120fd795f14d22a0c8657a274bdfa9238060ddff0ce0f1e49e962`
