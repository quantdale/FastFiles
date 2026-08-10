# Documentation and repository-hygiene acceptance evidence

Recorded 2026-08-11 on `recovery/fast-files-20260809`.

## Service-account documentation search

The acceptance search was run with:

```powershell
rg -n --glob '!verify/runs/**' --glob '!verify/baselines/**' --glob '!verify/.signing/**' \
  "virtual service account|never LocalSystem|stubbed|SeBackupPrivilege only" \
  AGENTS.md CLAUDE.md CODE_INDEX.md README.md docs openspec src tests
```

Current guidance matches the corrected model: `AGENTS.md`, `README.md`, `CLAUDE.md`, and `CODE_INDEX.md` describe `FastFilesIndexSvc` as the LocalSystem constrained broker, with MFT/USN scanning implemented and degraded mode active while pins and privileged-path validation remain incomplete.

Remaining matches are explicitly historical or superseded rather than current guidance:

- `openspec/changes/establish-architecture-foundation/design.md` labels the original virtual-account decision superseded by the constrained-broker decision.
- Closed-change designs/proposals/tasks and the 2026-08-04 audit reports preserve historical claims from the sessions that discovered the correction.
- A small number of source comments retain historical/dev-model context and are not service-account guidance.
- `openspec/changes/autonomous-runtime-verification` uses “virtual service account” as an example in an unsynced delta spec; it is not a current guidance document and is noted for that change's review.

No current guidance hit was found that presents the virtual-account model, “never LocalSystem,” `SeBackupPrivilege only`, or a stubbed privileged scanner as the shipped architecture.

## Repository-hygiene decisions

The prior hygiene commit `52d33da` was checked with:

```powershell
git show --name-status --format=fuller 52d33da -- dist/FastFiles.exe codebase-audit/Audit.txt Audit.txt
git ls-tree -r --name-only HEAD | Select-String '(^|/)(dist/|Audit\.txt$|codebase-audit/Audit\.txt$)'
git status --short
```

Observed result:

- `dist/FastFiles.exe`: deleted from source control in `52d33da`; the current tree has no tracked copy, and `.gitignore` excludes `dist/`. Documented CMake builds continue to emit binaries under ignored `build/` directories. Decision: remove from source control and ignore generated distribution output.
- `Audit.txt`: renamed `Audit.txt -> codebase-audit/Audit.txt` in `52d33da`. The 819-line autonomous-audit prompt is retained as tooling/documentation in the owned `codebase-audit` area rather than at repository root. Decision: retain after the documented move.
- `codebase-audit/sessions/20260804-180500-v7k4p2/`: no files were modified by the hygiene commit or this reconciliation; its eight validation reports remain intact.

## Final reconciliation

The required baseline commands were rerun on 2026-08-11 with the x64 VS 2022 developer shell and the documented process-local user temporary directory:

```powershell
cmake --preset debug
cmake --build --preset debug
cmake -E chdir build\debug ctest -N
cmake -E chdir build\debug ctest --output-on-failure
```

Results: Debug configure/build passed; `ctest -N` discovered 42 tests; serial CTest passed 42/42, including the storage, details, authentication, fuzz, and doc-drift tests. The Analyze configure/build gate also passed in the same campaign.

The later status-chip policy slice (`387228f`) added one deterministic CTest
target. Its x64 Debug/Analyze rebuilds passed, serial CTest discovered and
passed 43/43, and the post-checkpoint `ffdoc_drift_tests` rerun also passed.

Finding reconciliation:

- `MED-20260804-v7k4p2-006`: closed by the existing storage implementation and deterministic tests, reconciled in `3adaf7b`.
- `LOW-20260804-v7k4p2-007`: closed by the existing details-layout implementation and non-overlap tests, reconciled in `3adaf7b`.
- `LOW-20260804-v7k4p2-001`: closed by the guidance search and evidence in this file, committed in `edb951c`/`51cbb24`.
- Pinned-authentication release blockers: constant-time comparison and local regression/fuzz validation are complete in `40ce237`; placeholder pins, expired/untrusted certificate fixtures, signing procedure, elevated signed-service validation, real MFT/USN acceptance, privilege diagnostics, and clean-host matrix remain open. No trusted thumbprint was invented and no privileged path was activated.
- Interactive visual validation remains open because it requires the external desktop matrix and retained screenshots; the existing headless/runtime evidence is not substituted for that gate.

`openspec validate --all --strict` remains open: the `openspec` CLI is not installed in this environment (`openspec` was not recognized). No validation result was fabricated.

Degraded mode remains the active, fail-safe product path per `AGENTS.md`; prior runtime evidence confirms the UI/Engine degraded browsing path remains usable, and this campaign made no change that requires the privileged service.

## External validation availability

The following machine observations were recorded on 2026-08-11 after the
status-chip slice:

- `Get-Command openspec` returned no command. The repository OpenSpec CLI
  validation is therefore `REQUIRED-BUT-UNAVAILABLE`; no schema-validation
  result was invented.
- `src/setup/include/ffsetup/PinnedSignatures.h` still contains
  `kExpectedEngineSignatureThumbprint{}` and
  `kExpectedIndexSvcSignatureThumbprint{}`. Development authentication remains
  fail-closed, so signed privileged-path execution cannot be claimed.
- No `FastFiles` or `FastFilesEngine` process remained after validation. No
  service was installed, started, or activated by this campaign.
- The clean-host privilege-candidate matrix, provisioned release certificate,
  authorized elevation, and retained interactive visual screenshot matrix were
  not available to this session. Tasks 4.2, 4.5–4.8, and 5.1–5.3 remain open;
  their evidence is intentionally pending rather than marked complete.
