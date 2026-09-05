# Resource Soak Certification Summary

- **Disposition**: **PASS**
- **Commit**: `5dbe234248f630e58aecc621ac3c7a6cacb8e449` (tree: `8f648afcbcd04a0a211c921fd168a37cc1d523c4`, exactTreeBound: True)
- **Duration**: 600.17s (109 steady-state samples)
- **Target**: `build/debug/src/ui/FastFiles.exe` (hash: `e3f90769b4695c9bc5ea582865df7e9084325ea2d4cac20baa3de9651d46f26d`)
- **Target PID**: 6720 (Engine: none (Diagnostic mode runs UI process with in-proc/mock engine or on-demand connection))
- **Readiness**: reached in 331.04ms; warmup 10s

## Resource Metrics

| Metric | Startup Value | Steady Baseline | Median | Final | Steady Slope | Stability |
| :--- | :--- | :--- | :--- | :--- | :--- | :---: |
| **Working Set** | 6.14 MB | 56.92 MB | 74.52 MB | 74.79 MB | 31209.504 B/s | PASS |
| **Private Bytes** | 2.03 MB | 38.04 MB | 57.6 MB | 57.98 MB | 34833.4372 B/s | PASS |
| **Handles** | 46 | 1190 | 1742 | 1742 | 0.9197/s | PASS |
| **Threads** | 4 | 30 | 34 | 35 | 0.0083/s | PASS |
| **GDI Objects** | 0 | 108 | 121 | 121 | 0.0217/s | PASS |
| **USER Objects** | 3 | 114 | 117 | 117 | 0.005/s | PASS |

## Stability Legs

- **Process Survival**: PASS
- **Functional Activity**: PASS (12 real transitions executed)
- **Resource Stability**: PASS
- **Graceful Shutdown**: PASS (graceful in 53.77ms)
- **Residue Census**: PASS (0 orphans)

## Artifact Checksum

- **JSON Artifact**: `resource-soak-summary.json`
- **SHA-256**: `8139cb917dd59ea9dc72e99ba70f3b7c44332288004771402cd34dda84289c21`
