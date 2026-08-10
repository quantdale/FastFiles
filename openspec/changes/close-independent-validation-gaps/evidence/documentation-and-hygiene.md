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
