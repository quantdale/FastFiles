# UIA driver tests

`pwsh -NoProfile -File verify/uia-driver/tests/run-tests.ps1` runs the portable recorded-tree suite, including metadata/cache policy in `test-standard-proxy-metadata.ps1`. Metadata tests require no Windows UIA assemblies or desktop. Live availability is reported separately; recorded-tree assertions still execute when it is unavailable.

The native diagnostic is explicit and is **not** registered with default CTest:

```powershell
pwsh -NoProfile -File verify/uia-driver/tests/Test-NativeStandardControls.ps1 -OutputPath build/native-standard-controls/result.json
pwsh -NoProfile -File verify/uia-driver/tests/Test-NativeStandardControls.ps1 -ColdStart -OutputPath build/native-standard-controls/cold-result.json
```

Use a fresh disposable PowerShell process in an interactive Windows session, serialized with all other UIA/native validation. It creates only its own raw checkbox and dropdown-list HWNDs on an STA message thread, records before/after bootstrap roles/patterns and genuine title Name events, verifies reload reuse, and uses normal own-thread SetCapture/ReleaseCapture to validate observation from parent and sibling HWNDs. It closes with WM_CLOSE and bounded thread join. It never launches FastFiles, injects input/events, changes another process's capture, edits system configuration, or terminates processes. A before-bootstrap failure of native semantics is recorded, not required on every runtime; after-bootstrap real standard semantics and clean lifecycle are mandatory.

The legacy PID-based GetGUIThreadInfo query is retained only as a read-only diagnostic comparison. Its failure does not establish an OS limitation or certify Explorer drag behavior. Application acceptance must run separately on the safe integrated source.

The second command initializes the real driver before any fixture HWND/UIA query, covering a fresh client's public runtime initialization order separately from the warm before/after comparison. Both commands must run serially in fresh processes.
