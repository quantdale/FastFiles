# FastFiles support and validation matrix

This matrix separates what FastFiles supports by design from what this campaign has actually executed. `Tested` means current evidence exists; `Supported by design` is a product/documentation claim without local execution; `Untested` is not a pass; `Unsupported` is outside the product contract.

| Dimension | Tested in this campaign | Supported by design | Untested / external | Unsupported |
| --- | --- | --- | --- | --- |
| Windows 10 1809+ | Not yet re-executed here | Yes, per product prerequisites | Version-specific matrix | — |
| Windows 11 x64 | Baseline host: Windows 11 x64 | Yes | Other editions/builds | — |
| ARM64 | No | — | External architecture validation | If x64-only artifact is supplied |
| NTFS local volume | Degraded-path evidence exists in prior closure | Yes | Signed privileged indexed path | — |
| UNC/SMB | No | Path model is intended to be Win32-based | Requires safe local share | — |
| Removable/disappearing volume | No | Behavior is intended to recover | Requires safe removable/virtual volume | — |
| 100% DPI | Prior closure evidence | Yes | Re-run per campaign | — |
| 125/150/200% DPI | Headless layout matrix only | Intended | Live display validation | — |
| Light/Dark | Prior closure evidence | Yes | Re-run per campaign | — |
| High Contrast | Prior authorized evidence exists; not this campaign run | Intended | Current host setting not changed automatically | — |
| Animations disabled | Prior authorized evidence exists; not this campaign run | Intended | Current host setting not changed automatically | — |
| Single/multiple monitors | Single-host evidence only | Intended | Multi-monitor validation | — |
| Degraded/unprivileged mode | Portable baseline | Permanent supported state | — | — |
| Production-signed service/index | No current claim | Release prerequisite | Needs authorized signer/elevated host | — |
| Clean-host install lifecycle | No current claim | Release procedure prepared | Needs clean elevated host/package | — |

CI on `windows-latest` is build/test validation, not proof of every OS, display, network, removable, installer, or privileged-runtime row.
