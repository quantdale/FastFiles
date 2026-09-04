# Final Local Certification Closure — Defect Ledger (2026-09-04)

Campaign: narrow closure of the four remaining local gaps blocking
`LOCAL CERTIFICATION EXECUTION ENGINE COMPLETE — EXTERNAL EXECUTION PENDING`.
Starting SHA: `bae9ca2bcc61bdd6d488d2f0f97ca8323f768762` (= origin/main, clean
except pre-existing unrelated `M .opencode/package.json[.lock]` which is
preserved untouched).

Forensic method: subagent evidence probes against exact tree (no code changes
before ledger). Status values: CONFIRMED / NOT_REPRODUCED / INVALIDATED /
FIXED / BLOCKED_EXTERNAL.

## FC-001 rapid-input command-palette focus race — CONFIRMED, FIXED (product)

- Retained failure: `openspec/changes/external-certification-execution-engine-and-frontier-truthfulness/evidence/long-run-uia-rapid-input-50-attempt1-FAIL.json`
  (`completed:22 passed:21 failed:1`, iteration 22:
  "palette edit was present but did not own keyboard focus before text entry",
  foreground FastFiles, no foreign popup, 4 bounded focus retries exhausted).
  Companion 50/50 PASS (`evidence/long-run-uia-rapid-input-50.json`) is
  unchanged-code evidence of intermittency, not a fix.
- Root cause (mixed, product-primary): `CommandPalette::Show()`
  (`src/ui/src/CommandPalette.cpp`) positioned edit/list with
  `kPaletteOverlayLayoutFlags = SWP_SHOWWINDOW | SWP_NOACTIVATE` and issued a
  single **unchecked** `SetFocus(edit_)`. When the shell had just completed a
  dual-pane transition (owner not yet foreground-active), the OS refused the
  focus change silently; focus stayed on the ColumnView pane while the harness
  entered text. No `WM_ACTIVATE`/`WM_SETFOCUS` handling exists on the palette
  path (`DimmerSubclassProc` handles only button/erase messages).
- Fix (this campaign): `Show()` now calls `EnsureEditFocus()` — verify via
  `GetFocus()`, re-assert visibility, one more `SetFocus`, return verified
  result; new `EditHasFocus()` observer; new pure seam
  `src/ui/include/ffui/PaletteFocus.h` (`PaletteEditOwnsFocus`,
  `PaletteFocusReady`: text entry allowed only when edit present + enabled +
  visible + verified focus owner). `Hide()` focus return guarded against
  dead/null owner. Harness retry counts **unchanged** (its HasKeyboardFocus
  gate was already the correct semantic observation, not the defect).
- Regression: `tests/ui/test_palette_focus.cpp` (`ffpalette_focus_tests`,
  98th CTest target) — handle-identity + full readiness matrix including the
  exact iteration-22 shape (present/enabled/visible but focus unverified →
  NOT ready). Non-interactive safe (no foreground manipulation).
- Evidence: full Debug build warning-clean; `ffpalette_focus_tests`,
  `ffdialog_focus_return_tests`, `ffaddress_bar_focus_tests`,
  `ffdoc_drift_tests` PASS; full Debug CTest 98/98 PASS on this tree (see
  WORKSTATE entry). Post-fix 100×/200× rapid-input stress NOT yet run —
  required before FC-001 can be called closed.

## FC-002 Hyper-V guest channel not implemented — CONFIRMED, NOT FIXED

- `verify/certification/Invoke-EnvironmentProviderCertification.ps1:378-384`:
  real Hyper-V `ESTABLISH_SECURE_GUEST_CHANNEL` returns
  `BLOCKED / GUEST_CHANNEL_CREDENTIAL_ABSENT` unconditionally; `:435` gates all
  downstream phases on the channel. No PowerShell Direct / WinRM / PSSession /
  Copy-VMFile implementation anywhere under `verify/`; no credential parameter
  exists on the runner (`Provider, SourcePath, OutputPath, RequireExactTree,
  RepoRoot, ProviderAdapter, TimeoutSec, CandidatePath` only). Credentials
  appear solely as plan metadata (`HyperV.psm1:313-316`, `provider.json:17`).
- Local work remaining: credential input contract (PSCredential, never
  committed/logged), PS-Direct channel, payload transfer + digest check,
  guest execution + structured result contract, full 23-step lifecycle,
  failure-matrix tests. Lawful VHDX + approved credential stay external.

## FC-003 real MFT observation backend absent — CONFIRMED, NOT FIXED

- `verify/certification/Invoke-PrivilegedNtfsCertification.ps1:20-21`:
  `$MftReader = $null` by default ("no reader available", fail-closed
  BLOCKED); `:264-332` raw-open alone cannot PASS. Only production-code reuse
  is parser-level (`ffmftparser_tests.exe` over `MftParser.h`); nothing invokes
  the live scanner (`src/indexsvc/src/VolumeScanner.cpp`) as a certification
  probe. Fixture-truth comparison architecture itself is sound.

## FC-004 real USN observation backend absent — CONFIRMED, NOT FIXED

- Same seam as FC-003 (`$UsnReader = $null`); parser tests
  (`ffusnrecord_tests.exe` over `UsnRecordParser.h`) only; no live
  `UsnJournalReader.cpp` probe, no FSCTL query/read path in the runner.

## FC-005 external-frontier readiness overstates Hyper-V readiness — CONFIRMED

- All 8 `external-provider` tasks in `verify/certification/external-frontier.json`
  claim `READY_EXTERNAL` with no `implementationGap`, although FC-002 proves a
  local implementation gap (credential/channel machinery) remains. Truthful
  value until fixed: `NOT_IMPLEMENTED` with gap text, or keep
  `READY_EXTERNAL` only after the channel machinery lands.

## FC-006 external-frontier readiness overstates privileged NTFS readiness — CONFIRMED

- Both `privileged-ntfs` tasks (`close-independent-validation-gaps` 4.6/4.7)
  claim `READY_EXTERNAL` with no `implementationGap`, although FC-003/FC-004
  prove the default backend is null. Same reclassification requirement.

## FC-007 visual-matrix task 5.1 remains NOT_IMPLEMENTED — CONFIRMED

- Sole `NOT_IMPLEMENTED` of 25 frontier tasks. Runner
  (`Invoke-UiaVisualMatrix`) proves theme/DPI/contrast/viewport/dual-pane and
  details-reservation dynamics + treemap percent-sum on fixtures, but
  storage-contents/percent assertions need the engine index: sandbox contexts
  never establish the UI/engine connection (EX-009; every
  `New-UiaScenarioContext` logs `engine-connect-not-observed`). No
  engine-connected UIA context helper exists (`Start-UiaFastFiles` takes no
  engine parameter). Local work: campaign-owned disposable indexed scope +
  engine-connected launch, or truthful `READY_EXTERNAL` reclassification with
  the runner proven otherwise complete.

## FC-008 active campaign/lifecycle not closed — CONFIRMED

- `external-certification-execution-engine-and-frontier-truthfulness` is
  `active`; tasks 6.3, 7.1, 7.2, 7.3 unchecked. Cannot close until FC-001
  stress + FC-002/003/004/007 local work + full validation ladder complete.

## FC-009 WORKSTATE contradicts project status — CONFIRMED

- `docs/agent/WORKSTATE.md` head claims `NEXT EXACT ACTION: none locally`
  while the active campaign holds unchecked local tasks (6.3/7.1/7.2/7.3) and
  this ledger proves remaining local implementation (FC-002/003/004/007).
  Fix requires completing the work first then regenerating the statement —
  not editing prose to match either side prematurely.
