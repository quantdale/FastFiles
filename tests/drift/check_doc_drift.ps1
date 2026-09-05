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
#  (c) Excluded vendor-specific VM-provider terminology must not appear in
#      tracked documentation prose (.md under docs/, openspec/, .agent/,
#      verify/, tests/, codebase-audit/, plus root guidance .md). Owner
#      directive: vendor-specific VM-provider execution/testing is out of
#      scope, and the excluded provider technology must not be named in docs.
#      Generic replacements ("vendor-specific VM provider", "external
#      environment provider") are required instead. Implementation symbols
#      (.ps1/.psm1/.json provider ids, adapter paths, execution commands) are
#      intentionally out of scope and unscanned.
#  (d) Semantic truth contracts (DocTruth.psm1): fenced machine-derived claims
#      in current-facing guidance must match their authoritative executable
#      seams (verifier verbs/exit codes, CMake presets/targets, lifecycle
#      manifest states, snapshot publication source seam); stale
#      current-campaign claims fail closed; orphaned fences fail closed.
#      See openspec/changes/repository-truth-surface-and-doc-drift-convergence/.
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

function Test-ExcludedProviderTerminology {
    $failures = 0
    # hyperv(?!isor) keeps the generic English word "hypervisor" legal while
    # forbidding the vendor technology name in all hyphenations/casings.
    # "VMware"/"VirtualBox" are vendor names outright.
    $pattern = 'hyper[\s-]*v(?!isor)|vmware|virtualbox'
    $scopeDirs = @('docs', 'openspec', '.agent', 'verify', 'tests', 'codebase-audit')
    $rootDocs = @('README.md', 'AGENTS.md', 'CLAUDE.md', 'CODE_INDEX.md', 'AUTONOMOUS.md', 'progress.md')
    $tracked = @()
    try {
        $tracked = @(git -C $repoRoot ls-files 2>$null | ForEach-Object { $_.ToString() })
    } catch {
        Write-Host 'FAIL: excluded-provider scan could not enumerate tracked files (git ls-files failed)'
        return 1
    }
    if ($tracked.Count -eq 0) {
        Write-Host 'FAIL: excluded-provider scan enumerated zero tracked files (fail-closed, not vacuous PASS)'
        return 1
    }
    $candidates = @($tracked | Where-Object {
        $f = $_.Replace('\', '/')
        if ($f -notmatch '\.md$') { return $false }
        foreach ($d in $scopeDirs) { if ($f -eq "$d" -or $f.StartsWith("$d/")) { return $true } }
        foreach ($r in $rootDocs) { if ($f -eq $r) { return $true } }
        return $false
    })
    foreach ($rel in $candidates) {
        $path = Join-Path $repoRoot ($rel -replace '/', [System.IO.Path]::DirectorySeparatorChar)
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
        $content = [System.IO.File]::ReadAllText($path)
        $hits = @([regex]::Matches($content, $pattern, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase))
        foreach ($hit in $hits) {
            $line = ($content.Substring(0, $hit.Index) -split "`n").Count
            Write-Host ("FAIL: {0}:{1} names excluded vendor-specific VM technology: '{2}'" -f ($rel -replace '\\', '/'), $line, $hit.Value)
            $failures++
        }
    }
    if ($failures -eq 0) {
        Write-Host 'PASS: tracked documentation prose names no excluded vendor-specific VM technology'
    }
    return $failures
}

function Test-TruthContracts {
    Import-Module (Join-Path $PSScriptRoot 'DocTruth.psm1') -Force
    $findings = @(Get-DocTruthFindings -RepoRoot $repoRoot)
    foreach ($finding in $findings) {
        Write-Host ("FAIL: [truth:{0}] {1} :: {2}" -f $finding.Code, $finding.Surface, $finding.Detail)
    }
    if ($findings.Count -eq 0) {
        Write-Host 'PASS: semantic truth contracts over current-facing guidance'
    }
    return $findings.Count
}

$failures = 0
$failures += Test-SupersededClaims
$failures += Test-LineEndings
$failures += Test-ExcludedProviderTerminology
$failures += Test-TruthContracts

if ($failures -eq 0) {
    Write-Host 'PASS: doc-drift, line-ending, and truth checks'
    exit 0
}
Write-Host "FAIL: $failures doc-drift/line-ending/truth check(s) failed"
exit 1
