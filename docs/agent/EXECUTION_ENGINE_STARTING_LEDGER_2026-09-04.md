# Execution-Engine Campaign — Starting Defect Ledger (2026-09-04)

Starting state re-derived live: branch `main`, HEAD `5316876585f27f223c699a165f4c2919fd76ba24`,
`main == origin/main`, single worktree, extra local branch `recovery/fast-files-20260809`
(untouched), remotes `origin/main` only.

## Forensically confirmed (pre-edit, code-cited)

- **EX-001 Clean-host fake install PASS**: `Invoke-CleanHostCertification.ps1:122-124`
  records `candidate-installation=PASS / Candidate installation verified in campaign
  sandbox` when admin + no conflict + file exists — no install invoked, no exit
  status, no launch, no uninstall. Overall derives `PASS` (line 137-141).
- **EX-002 Provider preflight-only**: `Invoke-EnvironmentProviderCertification.ps1:63-110`
  checks module/elevation/image-shape only; no PROVISION/ACTIVATE/TRANSFER/EXECUTE/
  COLLECT/ROLLBACK/CLEANUP phases exist. `local` provider returns double-PASS without
  executing anything (lines 63-65).
- **EX-003 NTFS parser substitute**: `Invoke-PrivilegedNtfsCertification.ps1:86-120`
  opens `\\.\C:` read-only then runs `ffmftparser_tests.exe` +
  `ffusnrecord_tests.exe` isolated fixtures. No fixture creation through normal FS
  APIs, no raw-record-to-fixture comparison. Raw-open + parser PASS can yield lane
  `PASS` (lines 132-141).
- **EX-004 Shallow frontier mapping**: `external-frontier.json` maps the
  Light+Dark/100%+150% DPI/High Contrast/multi-width/details-pane/dual-pane +
  `% of Parent` task (`close-independent-validation-gaps` 5.1) to
  `Invoke-UiaLongRunCertification.ps1 -Scenario core-smoke -Iterations 25` with a
  single coarse capability `interactive-desktop-display-matrix`. Generic core-smoke
  cannot satisfy DPI/contrast/matrix acceptance dimensions.
- **EX-005 UIA rapid-input overstatement**: dual-pane and Search/Filters semantic
  postconditions claimed stronger than explicitly asserted (Pane.1 existence,
  Filters disclosure contents) — under repair.
- **EX-006 Soak gaps**: no independent private-bytes gate; close-without-verification
  transitions possible — under repair.
- **EX-007 Status truth**: `25 external / 0 active-local` assumes complete runners;
  no NOT_IMPLEMENTED vs READY_EXTERNAL distinction — pending.
- **EX-008 Signing unaudited**: `Invoke-SigningCertification.ps1` not yet forensically
  audited this campaign — pending.

## Sibling search (in progress)

- `Invoke-ExternalCertification.ps1` orchestrator: verify no success-by-skip.
- `Import-CertificationEvidence.ps1`: verify staleness/provenance enforcement.
- `Invoke-VisionProviderCertification.ps1` vs `Invoke-ProviderCertification.ps1`:
  confirm lane separation holds after provider rework.
- `verify/status/Get-ProjectStatus.ps1` + `verify/core/ProjectStatus.psm1`: status
  truth-model changes must keep schema valid.
