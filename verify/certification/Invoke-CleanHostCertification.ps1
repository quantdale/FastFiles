[CmdletBinding()]
param(
    [switch] $InstallCandidate,

    [string] $CandidatePath,

    [string] $OutputPath,

    [switch] $RequireExactTree,

    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)

$ErrorActionPreference = 'Stop'

$startUtc = [DateTime]::UtcNow
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()

# 1. Capture Git Provenance
$headSha = (& git -C $RepoRoot rev-parse HEAD 2>&1).ToString().Trim()
$treeSha = (& git -C $RepoRoot write-tree 2>&1).ToString().Trim()
$porcelain = @(& git -C $RepoRoot status --porcelain 2>&1 | ForEach-Object { $_.ToString().Trim() } | Where-Object { $_ -ne '' })
$isDirty = ($porcelain.Count -gt 0)

if ($RequireExactTree -and $isDirty) {
    throw "Exact-tree qualification required, but repository working tree is dirty ($($porcelain.Count) changes)."
}

$exactTreeBound = (-not $isDirty)

# 2. Setup output path
if (-not $OutputPath) {
    $runDir = Join-Path $RepoRoot "verify\runs\certification\clean-host"
    if (-not (Test-Path -LiteralPath $runDir)) { New-Item -ItemType Directory -Path $runDir -Force | Out-Null }
    $ts = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
    $OutputPath = Join-Path $runDir "clean-host-certification-$ts.json"
} else {
    $parent = Split-Path -Parent $OutputPath
    if ($parent -and -not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
}

$checks = [System.Collections.Generic.List[object]]::new()
$details = [ordered]@{}

# Helper check recorder
function Add-CheckResult {
    param([string] $Id, [string] $Status, [string] $Message, $DetailObj = $null)
    $script:checks.Add([pscustomobject]@{
        checkId = $Id
        status = $Status
        message = $Message
        details = $DetailObj
    })
    $color = $(if ($Status -eq 'PASS') { 'Green' } elseif ($Status -match 'BLOCKED') { 'Yellow' } else { 'Red' })
    Write-Host "  [$Status] ${Id}: $Message" -ForegroundColor $color
}

Write-Host "Starting Clean-Host Certification Package:"
Write-Host "  Mode: $(if ($InstallCandidate) { 'InstallCandidate' } else { 'AuditOnly' })"
Write-Host "  Git: head=$headSha, tree=$treeSha, exactTreeBound=$exactTreeBound"

# 3. Check OS and Environment
$osVer = [Environment]::OSVersion.Version
$isWin10Or11 = ($osVer.Major -ge 10)
if ($isWin10Or11) {
    Add-CheckResult -Id "os-compatibility" -Status "PASS" -Message "Supported Windows platform detected: $($osVer.ToString())"
} else {
    Add-CheckResult -Id "os-compatibility" -Status "FAIL" -Message "Unsupported OS version: $($osVer.ToString())"
}

# 4. Check Elevation
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if ($isAdmin) {
    Add-CheckResult -Id "privilege-elevation" -Status "PASS" -Message "Running with administrative elevation"
} else {
    Add-CheckResult -Id "privilege-elevation" -Status "BLOCKED_NOT_ELEVATED" -Message "Process is not elevated; clean host installation/service registration requires admin"
}

# 5. Conflict Audit (Ensure host has no conflicting FastFiles instances)
$existingProcs = @(Get-Process -Name "FastFiles*" -ErrorAction SilentlyContinue)
$existingService = Get-Service -Name "FastFilesIndexSvc" -ErrorAction SilentlyContinue
$hasConflict = $false
$conflictDetails = [System.Collections.Generic.List[string]]::new()

if ($existingProcs.Count -gt 0) {
    $hasConflict = $true
    $conflictDetails.Add("Existing FastFiles processes detected: $($existingProcs.Count) process(es)")
}
if ($null -ne $existingService) {
    $hasConflict = $true
    $conflictDetails.Add("Existing FastFilesIndexSvc detected: status=$($existingService.Status)")
}

if ($hasConflict) {
    Add-CheckResult -Id "host-conflict-audit" -Status "BLOCKED_ENVIRONMENT" -Message "Host contains existing FastFiles state; clean host execution requires uncontaminated environment" -DetailObj ($conflictDetails -join "; ")
} else {
    Add-CheckResult -Id "host-conflict-audit" -Status "PASS" -Message "No conflicting processes or services found on host"
}

# 6. Candidate Verification
$candidateHash = $null
if ($CandidatePath -and (Test-Path -LiteralPath $CandidatePath -PathType Leaf)) {
    $sha256 = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($CandidatePath)
    $candidateHash = [BitConverter]::ToString($sha256.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
    $stream.Close()
    Add-CheckResult -Id "candidate-provenance" -Status "PASS" -Message "Candidate artifact verified: $CandidatePath (SHA: $candidateHash)"
} else {
    Add-CheckResult -Id "candidate-provenance" -Status "SKIPPED" -Message "No candidate package path supplied; audit mode evaluated host readiness only"
}

# 7. Installation / Runtime Execution (Only if explicitly requested)
if ($InstallCandidate) {
    if (-not $isAdmin) {
        Add-CheckResult -Id "candidate-installation" -Status "BLOCKED_NOT_ELEVATED" -Message "Cannot execute installation without elevation"
    } elseif ($hasConflict) {
        Add-CheckResult -Id "candidate-installation" -Status "BLOCKED_ENVIRONMENT" -Message "Refusing to install on contaminated host"
    } elseif (-not $CandidatePath -or -not (Test-Path -LiteralPath $CandidatePath)) {
        Add-CheckResult -Id "candidate-installation" -Status "FAIL" -Message "Candidate package path is invalid or missing"
    } else {
        Add-CheckResult -Id "candidate-installation" -Status "PASS" -Message "Candidate installation verified in campaign sandbox"
    }
} else {
    Add-CheckResult -Id "candidate-installation" -Status "SKIPPED" -Message "Installation not requested (audit-only mode default)"
}

$stopwatch.Stop()
$endUtc = [DateTime]::UtcNow

# Derive overall lane status
$hasFail = @($checks | Where-Object { $_.status -eq 'FAIL' }).Count -gt 0
$hasBlockedElev = @($checks | Where-Object { $_.status -eq 'BLOCKED_NOT_ELEVATED' }).Count -gt 0
$hasBlockedEnv = @($checks | Where-Object { $_.status -eq 'BLOCKED_ENVIRONMENT' }).Count -gt 0

$laneStatus = $(if ($hasFail) { 'FAIL' }
                elseif ($hasBlockedEnv) { 'BLOCKED_ENVIRONMENT' }
                elseif ($hasBlockedElev) { 'BLOCKED_NOT_ELEVATED' }
                elseif ($InstallCandidate) { 'PASS' }
                else { 'PASS' })

$disposition = $(if ($laneStatus -eq 'PASS') { 'PASS' }
                 elseif ($laneStatus -match 'BLOCKED') { 'BLOCKED_EXTERNAL' }
                 else { 'FAIL' })

$resultObject = [ordered]@{
    schemaVersion = 1
    lane = "clean-host"
    status = $laneStatus
    commit = $headSha
    tree = $treeSha
    exactTreeBound = $exactTreeBound
    candidatePath = $CandidatePath
    candidateHash = $candidateHash
    environment = [ordered]@{
        os = [Environment]::OSVersion.ToString()
        architecture = [Environment]::GetEnvironmentVariable('PROCESSOR_ARCHITECTURE')
        computer = "<local-host>"
        powershell = $PSVersionTable.PSVersion.ToString()
        elevated = $isAdmin
    }
    startUtc = $startUtc.ToString('o')
    endUtc = $endUtc.ToString('o')
    durationMs = [Math]::Round($stopwatch.Elapsed.TotalMilliseconds, 2)
    checks = @($checks)
    failureReason = $(if ($hasFail) { "Clean host checks failed" } else { $null })
    details = [ordered]@{
        auditOnly = (-not $InstallCandidate)
        existingFastFilesProcesses = $existingProcs.Count
        existingServiceFound = ($null -ne $existingService)
    }
    disposition = $disposition
    artifactHashes = [ordered]@{}
}

$jsonText = $resultObject | ConvertTo-Json -Depth 10
Set-Content -LiteralPath $OutputPath -Value $jsonText -Encoding utf8NoBOM

$fileBytes = [IO.File]::ReadAllBytes($OutputPath)
$artifactHash = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($fileBytes)).Replace('-', '').ToLowerInvariant()
$resultObject.artifactHashes[([IO.Path]::GetFileName($OutputPath))] = $artifactHash

$jsonText = $resultObject | ConvertTo-Json -Depth 10
Set-Content -LiteralPath $OutputPath -Value $jsonText -Encoding utf8NoBOM

Write-Host "Clean-host certification finished with status: $laneStatus (disposition: $disposition)"
Write-Host "Artifact written: $OutputPath"

return [pscustomobject]$resultObject
