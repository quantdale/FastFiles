[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $EvidenceDirectory,

    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)

$ErrorActionPreference = 'Stop'

function Get-SanitizedPath {
    param([string] $FullPath, [string] $RepoRoot)
    if ([string]::IsNullOrWhiteSpace($FullPath)) { return $FullPath }
    $resolvedRepo = (Resolve-Path $RepoRoot).Path.TrimEnd('\', '/')
    $resolvedFull = try { (Resolve-Path -LiteralPath $FullPath -ErrorAction SilentlyContinue).Path } catch { $FullPath }
    $pathStr = if ($resolvedFull) { $resolvedFull } else { $FullPath }
    if ($pathStr.StartsWith($resolvedRepo, [System.StringComparison]::OrdinalIgnoreCase)) {
        $rel = $pathStr.Substring($resolvedRepo.Length).TrimStart('\', '/')
        return ($rel -replace '\\', '/')
    }
    return ($pathStr -replace '\\', '/')
}
if (-not (Test-Path -LiteralPath $EvidenceDirectory)) {
    throw "Evidence directory not found: $EvidenceDirectory"
}

Write-Host "Generating Authoritative Certification Documentation from JSON:"
Write-Host "  Directory: $EvidenceDirectory"

# 1. Process Resource Soak JSON if present
$soakJson = Join-Path $EvidenceDirectory "resource-soak-summary.json"
if (-not (Test-Path -LiteralPath $soakJson)) {
    # Check for any resource-soak-*.json
    $soakCandidates = @(Get-ChildItem -LiteralPath $EvidenceDirectory -Filter "resource-soak-*.json" -File | Where-Object { $_.Name -ne 'resource-soak-summary.json' })
    if ($soakCandidates.Count -gt 0) {
        $soakJson = $soakCandidates[0].FullName
    }
}

if (Test-Path -LiteralPath $soakJson) {
    Write-Host "  Generating soak summary markdown from $([IO.Path]::GetFileName($soakJson))..."
    $s = Get-Content -LiteralPath $soakJson -Raw -Encoding utf8 | ConvertFrom-Json
    $soakFileBytes = [IO.File]::ReadAllBytes($soakJson)
    $soakHash = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($soakFileBytes)).Replace('-', '').ToLowerInvariant()

    $soakMd = @"
# Resource Soak Certification Summary

- **Disposition**: **$($s.disposition)**
- **Commit**: ``$($s.commit)`` (tree: ``$($s.tree)``, exactTreeBound: $($s.exactTreeBound))
- **Duration**: $($s.durationSeconds)s ($($s.sampleCount) steady-state samples)
- **Target**: ``$($s.targetExecutable)`` (hash: ``$($s.executableHash)``)
- **Target PID**: $($s.fastFilesPid) (Engine: $(if ($s.engine -and $s.engine.observed) { "PID $($s.engine.pid)" } else { "none ($($s.engine.reason))" }))
- **Readiness**: reached in $($s.phases.readyDurationMs)ms; warmup $($s.phases.warmupDurationSeconds)s

## Resource Metrics

| Metric | Startup Value | Steady Baseline | Median | Final | Steady Slope | Stability |
| :--- | :--- | :--- | :--- | :--- | :--- | :---: |
| **Working Set** | $([Math]::Round($s.startupMetrics.workingSetBytes/1MB, 2)) MB | $([Math]::Round($s.steadyStateMetrics.workingSetBytes.baseline/1MB, 2)) MB | $([Math]::Round($s.steadyStateMetrics.workingSetBytes.median/1MB, 2)) MB | $([Math]::Round($s.steadyStateMetrics.workingSetBytes.final/1MB, 2)) MB | $($s.steadyStateMetrics.workingSetBytes.slope) B/s | $(if ($s.stabilityLegs.resourceStability -eq 'PASS') { 'PASS' } else { 'FAIL' }) |
| **Private Bytes** | $([Math]::Round($s.startupMetrics.privateBytes/1MB, 2)) MB | $([Math]::Round($s.steadyStateMetrics.privateBytes.baseline/1MB, 2)) MB | $([Math]::Round($s.steadyStateMetrics.privateBytes.median/1MB, 2)) MB | $([Math]::Round($s.steadyStateMetrics.privateBytes.final/1MB, 2)) MB | $($s.steadyStateMetrics.privateBytes.slope) B/s | $(if ($s.stabilityLegs.resourceStability -eq 'PASS') { 'PASS' } else { 'FAIL' }) |
| **Handles** | $($s.startupMetrics.handles) | $($s.steadyStateMetrics.handles.baseline) | $($s.steadyStateMetrics.handles.median) | $($s.steadyStateMetrics.handles.final) | $($s.steadyStateMetrics.handles.slope)/s | $(if ($s.stabilityLegs.resourceStability -eq 'PASS') { 'PASS' } else { 'FAIL' }) |
| **Threads** | $($s.startupMetrics.threads) | $($s.steadyStateMetrics.threads.baseline) | $($s.steadyStateMetrics.threads.median) | $($s.steadyStateMetrics.threads.final) | $($s.steadyStateMetrics.threads.slope)/s | $(if ($s.stabilityLegs.resourceStability -eq 'PASS') { 'PASS' } else { 'FAIL' }) |
| **GDI Objects** | $($s.startupMetrics.gdiObjects) | $($s.steadyStateMetrics.gdiObjects.baseline) | $($s.steadyStateMetrics.gdiObjects.median) | $($s.steadyStateMetrics.gdiObjects.final) | $($s.steadyStateMetrics.gdiObjects.slope)/s | $(if ($s.stabilityLegs.resourceStability -eq 'PASS') { 'PASS' } else { 'FAIL' }) |
| **USER Objects** | $($s.startupMetrics.userObjects) | $($s.steadyStateMetrics.userObjects.baseline) | $($s.steadyStateMetrics.userObjects.median) | $($s.steadyStateMetrics.userObjects.final) | $($s.steadyStateMetrics.userObjects.slope)/s | $(if ($s.stabilityLegs.resourceStability -eq 'PASS') { 'PASS' } else { 'FAIL' }) |

## Stability Legs

- **Process Survival**: $($s.stabilityLegs.survival)
- **Functional Activity**: $($s.stabilityLegs.functionalActivity) ($($s.activityTransitions.Count) real transitions executed)
- **Resource Stability**: $($s.stabilityLegs.resourceStability)
- **Graceful Shutdown**: $($s.stabilityLegs.shutdown) ($($s.phases.shutdownMethod) in $($s.phases.shutdownDurationMs)ms)
- **Residue Census**: $($s.stabilityLegs.residue) ($($s.residue.orphanCount) orphans)

## Artifact Checksum

- **JSON Artifact**: ``$([IO.Path]::GetFileName($soakJson))``
- **SHA-256**: ``$soakHash``
"@
    $soakMdPath = Join-Path $EvidenceDirectory "resource-soak-summary.md"
    Set-Content -LiteralPath $soakMdPath -Value $soakMd -Encoding utf8NoBOM
    Write-Host "  Written: $soakMdPath" -ForegroundColor Green
}

# 2. Process Long-Run Stress Artifacts
$longRunFiles = @(Get-ChildItem -LiteralPath $EvidenceDirectory -Filter "long-run-*.json" -File | Where-Object { $_.Name -ne 'long-run-summary.json' } | Sort-Object Name)

if ($longRunFiles.Count -gt 0) {
    Write-Host "  Generating long-run stress summary table from $($longRunFiles.Count) artifacts..."
    $tableRows = [System.Collections.Generic.List[string]]::new()
    $summaryList = [System.Collections.Generic.List[object]]::new()

    $firstCommit = $null
    $firstTree = $null
    $firstExact = $null

    foreach ($lf in $longRunFiles) {
        $data = Get-Content -LiteralPath $lf.FullName -Raw -Encoding utf8 | ConvertFrom-Json
        $bytes = [IO.File]::ReadAllBytes($lf.FullName)
        $sha = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($bytes)).Replace('-', '').ToLowerInvariant()

        if ($null -eq $firstCommit) {
            $firstCommit = $data.commit
            $firstTree = $data.tree
            $firstExact = $data.exactTreeBound
        }

        $tableRows.Add("| **$($data.testId)** | ``$($data.command)`` | $($data.requestedIterations) | $($data.passed) | $($data.failed) | $($data.timedOut) | $($data.medianDurationMs) | $($data.p95DurationMs) | **$($data.disposition)** | ``$sha`` |")

        $summaryList.Add([ordered]@{
            testId = $data.testId
            category = $data.category
            command = $data.command
            requestedIterations = $data.requestedIterations
            completedIterations = $data.completedIterations
            passed = $data.passed
            failed = $data.failed
            timedOut = $data.timedOut
            medianDurationMs = $data.medianDurationMs
            p95DurationMs = $data.p95DurationMs
            disposition = $data.disposition
            artifactFile = $lf.Name
            sha256 = $sha
        })
    }
    $hostedRunLine = $null
    $longRunMdPath = Join-Path $EvidenceDirectory "local-long-run-certification.md"
    if (Test-Path -LiteralPath $longRunMdPath) {
        $existingLines = Get-Content -LiteralPath $longRunMdPath
        $hostedRunLine = $existingLines | Where-Object { $_ -match 'Hosted CI Run' } | Select-Object -First 1
    }
    $hostedRunInsert = if ($hostedRunLine) { "`n$hostedRunLine" } else { "" }

    $longRunMd = @"
# Local Long-Run Certification Evidence

- **Commit**: ``$firstCommit``
- **Tree**: ``$firstTree``
- **Exact-Tree Binding**: $firstExact
- **Execution Strategy**: Re-executed under hardened ``Invoke-LongRunCertification.ps1`` and ``Invoke-UiaLongRunCertification.ps1``$hostedRunInsert
## Representative Stress Re-Execution Results

| Test ID | Command / Target | Iterations | PASS | FAIL | Timeout | Median (ms) | P95 (ms) | Disposition | Artifact SHA-256 |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
$($tableRows -join "`n")

## Verification Standard

- Every CTest test was discovered prior to run (enforcing exactly 1 matching test; zero-test match fails closed).
- Every iteration verified start and completion proof from process execution output.
- All artifact checksums are verified on closed file bytes.
"@
    $longRunMdPath = Join-Path $EvidenceDirectory "local-long-run-certification.md"
    Set-Content -LiteralPath $longRunMdPath -Value $longRunMd -Encoding utf8NoBOM
    Write-Host "  Written: $longRunMdPath" -ForegroundColor Green

    # Also emit consolidated long-run-summary.json
    $summaryObj = [ordered]@{
        schemaVersion = 1
        generatedAtUtc = [DateTime]::UtcNow.ToString('o')
        commit = $firstCommit
        tree = $firstTree
        exactTreeBound = $firstExact
        totalSuites = $summaryList.Count
        passedSuites = @($summaryList | Where-Object { $_.disposition -eq 'PASS' }).Count
        failedSuites = @($summaryList | Where-Object { $_.disposition -ne 'PASS' }).Count
        suites = @($summaryList)
    }
    $summaryJsonPath = Join-Path $EvidenceDirectory "long-run-summary.json"
    $summaryJsonText = $summaryObj | ConvertTo-Json -Depth 10
    Set-Content -LiteralPath $summaryJsonPath -Value $summaryJsonText -Encoding utf8NoBOM
    Write-Host "  Written: $summaryJsonPath" -ForegroundColor Green
}

# 3. Generate Authoritative Evidence Manifest
Write-Host "  Generating evidence-manifest.json..."
$allArtifacts = @(Get-ChildItem -LiteralPath $EvidenceDirectory -File | Where-Object { $_.Name -ne 'evidence-manifest.json' } | Sort-Object Name)
$manifestEntries = [ordered]@{}

foreach ($f in $allArtifacts) {
    $bytes = [IO.File]::ReadAllBytes($f.FullName)
    $hash = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($bytes)).Replace('-', '').ToLowerInvariant()
    $manifestEntries[$f.Name] = [ordered]@{
        sha256 = $hash
        bytes = $bytes.Length
        lastModifiedUtc = $f.LastWriteTimeUtc.ToString('o')
    }
}

$manifestObj = [ordered]@{
    schemaVersion = 1
    generatedAtUtc = [DateTime]::UtcNow.ToString('o')
    directory = (Get-SanitizedPath -FullPath $EvidenceDirectory -RepoRoot $RepoRoot)
    artifactCount = $manifestEntries.Count
    artifacts = $manifestEntries
}

$manifestPath = Join-Path $EvidenceDirectory "evidence-manifest.json"
$manifestJsonText = $manifestObj | ConvertTo-Json -Depth 10
Set-Content -LiteralPath $manifestPath -Value $manifestJsonText -Encoding utf8NoBOM
Write-Host "  Written: $manifestPath ($($manifestEntries.Count) artifacts hashed)" -ForegroundColor Green

Write-Host "Evidence documentation and manifest generation COMPLETE." -ForegroundColor Green
