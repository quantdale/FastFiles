# tests/drift/check_doc_drift.ps1 -- documentation-drift and line-ending guard.
#
# Registered with CTest as `ffdoc_drift_tests` (see tests/drift/CMakeLists.txt;
# pwsh-gated). Exit code 0 = PASS, 1 = FAIL. Prints a PASS/FAIL line per check.
#
# Checks:
#  (a) The four guidance docs (README.md, AGENTS.md, CLAUDE.md, CODE_INDEX.md)
#      must not contain superseded claims as current statements. The exact
#      strings below describe the pre-constrained-broker model ("never
#      LocalSystem", "SeBackupPrivilege only") or the pre-scanning skeleton
#      ("scan calls are stubbed", "not yet implemented"). Historical/superseded
#      context lives in openspec/ and audit logs, never in current guidance,
#      so these strings are banned outright from the four files.
#  (b) No .cpp/.h file under src/ or tests/ may have mixed line endings (both
#      CRLF and lone LF). With `* text=auto` renormalization, a mixed-ending
#      worktree file indicates line-ending corruption or a partial conversion.
#
# Run standalone: pwsh -NoProfile -File tests/drift/check_doc_drift.ps1

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

function Test-SupersededClaims {
    $failures = 0
    $supersededClaims = @(
        'never LocalSystem',
        'SeBackupPrivilege only',
        'scan calls are stubbed',
        'not yet implemented'
    )
    $docFiles = @('README.md', 'AGENTS.md', 'CLAUDE.md', 'CODE_INDEX.md')
    foreach ($doc in $docFiles) {
        $path = Join-Path $repoRoot $doc
        if (-not (Test-Path -LiteralPath $path)) {
            Write-Host "FAIL: guidance doc is missing: $doc"
            $failures++
            continue
        }
        $content = [System.IO.File]::ReadAllText($path)
        foreach ($claim in $supersededClaims) {
            if ($content.Contains($claim)) {
                Write-Host "FAIL: $doc contains superseded claim as current text: '$claim'"
                $failures++
            }
        }
    }
    if ($failures -eq 0) {
        Write-Host 'PASS: README.md/AGENTS.md/CLAUDE.md/CODE_INDEX.md contain no superseded claims'
    }
    return $failures
}

function Test-LineEndings {
    $failures = 0
    $fileCount = 0
    $roots = @((Join-Path $repoRoot 'src'), (Join-Path $repoRoot 'tests'))
    foreach ($root in $roots) {
        if (-not (Test-Path -LiteralPath $root)) {
            Write-Host "FAIL: expected source root is missing: $root"
            $failures++
        }
    }
    Get-ChildItem -Path $roots -Recurse -File |
        Where-Object { $_.Extension -in @('.cpp', '.h') } |
        ForEach-Object {
            $fileCount++
            $text = [System.IO.File]::ReadAllText($_.FullName)
            $crlfCount = ([regex]::Matches($text, "`r`n")).Count
            $lfCount = ([regex]::Matches($text, "`n")).Count
            if ($crlfCount -gt 0 -and $lfCount -gt $crlfCount) {
                Write-Host "FAIL: mixed line endings (CRLF and lone LF): $($_.FullName)"
                $failures++
            }
        }
    if ($failures -eq 0) {
        Write-Host "PASS: no mixed line endings in $fileCount .cpp/.h files under src/ or tests/"
    }
    return $failures
}

$failures = 0
$failures += Test-SupersededClaims
$failures += Test-LineEndings

if ($failures -eq 0) {
    Write-Host 'PASS: doc-drift and line-ending checks'
    exit 0
}
Write-Host "FAIL: $failures doc-drift/line-ending check(s) failed"
exit 1
