# External-frontier inventory reread — apply session 1

Source inventory: `verify/certification/external-frontier.json`
(`generatedAtUtc 2026-09-04T04:59:23.280889Z`, `schemaVersion 2`, 25 tasks,
5 frontier groups, 6 lanes, `byStatus` 10 `BLOCKED_ENVIRONMENT` + 15 `REQUIRED-BUT-UNAVAILABLE`,
`byReadiness` 25 `READY_EXTERNAL`, 0 `NOT_IMPLEMENTED`).

Reread at apply start. Nothing in this change alters billing, spending limits, account plan, branch
protection or rulesets, production signing, or trust-store configuration, and no privileged volume
scan was requested. Every classification below is derived from the inventory plus live commands run
in this session; it is not inherited from the proposal session.

## Classification legend

- `still-blocked` — the prerequisite input is absent on this host and in this account.
- `newly-testable` — the harness that would close it runs locally on this host's interactive desktop; the
  classification is provisional until the section 3.8 execution records the live outcome.
- `not-applicable` — no local or authorized action in this change can carry the prerequisite; the row is
  retained so the residual is still visible.

## 2.3.1 Lanes and prerequisites

### Lane `environment-provider` (8 tasks) — `still-blocked`

`autonomous-runtime-verification` 2.3 and 2.4, `phase8-release-readiness-convergence` 5.1 and 5.3,
`runtime-certification-and-verifier-convergence` 5.3, `zero-touch-autonomous-engineering` 6.3, 6.4, 6.6.

- **Missing prerequisite:** an isolated/provider execution target — a lawful Windows guest or template
  image plus a provider API key or guest credentials. The sandbox probe is `SKIPPED`
  (`windows-sandbox-not-found`) in `verify/verify.ps1 doctor`, while the host platform reports the
  hypervisor runtime as available.
- **Harness that would close it:** the provider preflight (guest provisioning, guest execution,
  provider rollback) run before the task capability.
- **Why not closed here:** the campaign forbids changing account or provider configuration, and no guest
  image or credential is authorized in this session.

### Lane `production-signing` (5 tasks) — `still-blocked`

`close-independent-validation-gaps` 4.2, 4.2.2, 4.5, and 4.6, `phase8-release-readiness-convergence` 5.2,
`ship-ready-ui-ux-hardening-convergence` 6.2.

- **Missing prerequisite:** an authorized code-signing certificate or an already-signed candidate, plus
  recorded published pins.
- **Harness that would close it:** the task-specific release validation command (the unsigned release
  candidate qualification command `Test-ReleaseCandidate.ps1 -BuildRoot .\build\release-test` runs locally
  today; its signed-path sibling requires the signer).
- **Why not closed here:** no signer or pin material is authorized; the campaign prohibits touching
  production signing.

### Lane `privileged-ntfs` (2 tasks) — `still-blocked`

`close-independent-validation-gaps` 4.6 and 4.7.

- **Missing prerequisite:** an elevated privileged validation context holding `SeBackupPrivilege` and
  `SeRestorePrivilege` on a disposable or authorized host with raw-volume access.
- **Harness that would close it:** `verify/run_privileged_trust_tests.ps1` and the raw MFT/USN
  ground-truth capability (`raw-volume-read`, `mft-ground-truth`, `usn-ground-truth`,
  `mft-observer-semantics`, `usn-observer-semantics`).
- **Why not closed here:** the campaign does not request a privileged volume scan; the pre-existing
  installed `FastFilesIndexSvc` on this developer host is left untouched.

### Lane `clean-host` (3 tasks) — `still-blocked`

`close-independent-validation-gaps` 4.8, `runtime-certification-and-verifier-convergence` 8.3,
`completion-integrity-hardening-and-release-certification` 4.5.3.

- **Missing prerequisite:** an uncontaminated supported Windows host with elevation.
- **Harness that would close it:** the clean-host certification package (`clean-install`,
  `installed-runtime`, `uninstall-cleanup`) run in live mode on the clean host.
- **Why not closed here:** this machine is the long-lived development host, not a clean host.

### Lane `interactive-uia` (5 tasks) — `newly-testable` (provisional; confirmed or refuted in 3.8)

`runtime-certification-and-verifier-convergence` 1.3, 7.2, 8.2,
`completion-integrity-hardening-and-release-certification` 4.5.1, 4.5.2, and 6.15.

- **Missing prerequisite:** the specific capability configuration. `uia-core-smoke` and
  `uia-keyboard-navigation` are within reach of an interactive desktop; the interactive-foreground
  drag and search-input variants are not, because the host's synthetic input cannot move a real window
  foreground reliably.
- **Harness that would close it:** `dev.ps1 test-uia` → `verify/uia-driver/UiAutomationValidation.ps1`
  (`UiaScenario=core` and `UiaScenario=all`).
- **Provisional classification:** these rows stay open as external residuals until the 3.8 execution
  records the live per-scenario verdicts. A skip recorded in 3.8 is an environment obstruction, not a
  product pass and not a closure of the frontier row.

### Lane `physical-display` (1 task) — `newly-testable` in part (provisional)

`close-independent-validation-gaps` 5.1 (light/dark themes, 100 % and 150 % DPI, High Contrast,
narrow/normal/wide widths, details pane open and closed, dual pane, `storage-parent-percent`
drill-down tolerance).

- **Missing prerequisite:** a supported physical display configuration. The earlier session recorded this
  host at 120 DPI / 125 % scale with the live DPI variants classified unavailable; that value is
  historical and is re-derived live in 3.8.
- **Harness that would close it:** `verify/visual/Invoke-VisualCatalog.ps1` with the semantic visual
  registry, plus `dev.ps1 test-uia`.
- **Provisional classification:** the theme, width, pane, and drill-down slices are locally exercisable;
  the DPI-150 and High-Contrast slices remain unavailable unless the live probe finds them.

## 2.3.2 Result

| Lane | Tasks | Classification |
| --- | --- | --- |
| `environment-provider` | 8 | still blocked |
| `production-signing` | 5 | still blocked |
| `privileged-ntfs` | 2 | still blocked |
| `clean-host` | 3 | still blocked |
| `interactive-uia` | 5 | newly testable (provisional; confirmed in 3.8) |
| `physical-display` | 1 | newly testable in part (provisional) |
| `not-applicable` | 0 | — |

No row becomes `not-applicable`: each of the 25 still belongs to a change that is `active` or
`external-evidence-only` in `openspec/lifecycle.json`, and each still needs the external input it names.
Two lifecycle `active` entries are the local campaign (this change) and the unproven raw-volume residual
(`legacy-protocol-correctness-and-raw-volume-cancellation-closure` task 5.4 with
`final-residual-hardening-and-hosted-ci-recovery` task 2.3), which stay unchecked.

No billing, spending-limit, account-plan, branch-rule, protection, signing, or trust-store setting was
changed to produce this classification, and no privileged volume scan was requested.
