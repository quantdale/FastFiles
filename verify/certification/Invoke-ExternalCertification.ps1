[CmdletBinding()]
param(
    [switch] $IncludeCleanHostInstall,

    [switch] $IncludePrivilegedNtfs,

    [switch] $IncludeProductionSigning,

    [switch] $IncludeProvider,

    [string] $CandidatePath,

    [string] $SignerThumbprint,

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

# 2. Output Setup
if (-not $OutputPath) {
    $runDir = Join-Path $RepoRoot "verify\runs\certification\orchestrator"
    if (-not (Test-Path -LiteralPath $runDir)) { New-Item -ItemType Directory -Path $runDir -Force | Out-Null }
    $ts = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
    $OutputPath = Join-Path $runDir "external-certification-$ts.json"
} else {
    $parent = Split-Path -Parent $OutputPath
    if ($parent -and -not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
}

Write-Host "================================================================="
Write-Host "FastFiles External Certification Orchestrator"
Write-Host "================================================================="
Write-Host "Git: head=$headSha, tree=$treeSha, exactTreeBound=$exactTreeBound"

# 3. Host Capability Detection
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
$osVer = [Environment]::OSVersion.Version
$hasInteractive = ([Environment]::UserInteractive)
$hasSigningInput = (-not [string]::IsNullOrEmpty($CandidatePath) -and -not [string]::IsNullOrEmpty($SignerThumbprint))
$hasProviderEnv = (-not [string]::IsNullOrEmpty($env:FASTFILES_VISION_PROVIDER_KEY))

$volC = Get-CimInstance -ClassName Win32_Volume -Filter "DriveLetter = 'C:'" -ErrorAction SilentlyContinue
$isNtfs = if ($volC) { $volC.FileSystem -eq 'NTFS' } else { $false }

$capabilityMatrix = [ordered]@{
    administrativeElevation = $isAdmin
    supportedWindowsVersion = ($osVer.Major -ge 10)
    ntfsVolumeAvailable = $isNtfs
    interactiveDesktop = $hasInteractive
    productionCandidateSupplied = $hasSigningInput
    providerConfigured = $hasProviderEnv
}

Write-Host "`nHost Capability Matrix:" -ForegroundColor Cyan
foreach ($k in $capabilityMatrix.Keys) {
    $val = $capabilityMatrix[$k]
    $c = $(if ($val) { 'Green' } else { 'Yellow' })
    Write-Host "  $k : $val" -ForegroundColor $c
}

$laneResults = [ordered]@{}
$checks = [System.Collections.Generic.List[object]]::new()

# Helper to run lane
function Invoke-Lane {
    param([string] $LaneName, [string] $ScriptPath, [hashtable] $ArgsList)
    Write-Host "`n--- Executing Lane: $LaneName ---" -ForegroundColor Cyan
    $laneOutput = Join-Path (Split-Path -Parent $OutputPath) "lane-$LaneName.json"
    $ArgsList['OutputPath'] = $laneOutput
    $ArgsList['RepoRoot'] = $RepoRoot

    $laneRes = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $ScriptPath @ArgsList
    $laneJson = $null
    if (Test-Path -LiteralPath $laneOutput) {
        $laneJson = Get-Content -LiteralPath $laneOutput -Raw | ConvertFrom-Json
    }

    $script:laneResults[$LaneName] = $laneJson
    $script:checks.Add([pscustomobject]@{
        checkId = "lane-$LaneName"
        status = if ($laneJson) { $laneJson.status } else { "FAIL" }
        message = "Lane '$LaneName' completed with status $(if ($laneJson) { $laneJson.status } else { 'FAIL' })"
        details = if ($laneJson) { $laneJson.details } else { $null }
    })
}

# 4. Dispatch Lanes
$certDir = Join-Path $RepoRoot 'verify\certification'

# Lane 1: Clean-Host
$cleanHostScript = Join-Path $certDir 'Invoke-CleanHostCertification.ps1'
$cleanHostArgs = @{}
if ($IncludeCleanHostInstall) { $cleanHostArgs['InstallCandidate'] = $true }
if ($CandidatePath) { $cleanHostArgs['CandidatePath'] = $CandidatePath }
Invoke-Lane -LaneName "clean-host" -ScriptPath $cleanHostScript -ArgsList $cleanHostArgs

# Lane 2: Privileged NTFS
$privNtfsScript = Join-Path $certDir 'Invoke-PrivilegedNtfsCertification.ps1'
$privNtfsArgs = @{}
Invoke-Lane -LaneName "privileged-ntfs" -ScriptPath $privNtfsScript -ArgsList $privNtfsArgs

# Lane 3: Production Signing
$signingScript = Join-Path $certDir 'Invoke-SigningCertification.ps1'
$signingArgs = @{}
if ($CandidatePath) { $signingArgs['CandidatePath'] = $CandidatePath }
if ($SignerThumbprint) { $signingArgs['SignerThumbprint'] = $SignerThumbprint }
Invoke-Lane -LaneName "production-signing" -ScriptPath $signingScript -ArgsList $signingArgs

# Lane 4: Provider
$providerScript = Join-Path $certDir 'Invoke-ProviderCertification.ps1'
$providerArgs = @{ Provider = 'real' }
Invoke-Lane -LaneName "external-provider" -ScriptPath $providerScript -ArgsList $providerArgs

# 5. Synthesize Aggregate State (No "Success by Skip")
$stopwatch.Stop()
$endUtc = [DateTime]::UtcNow

$allStatuses = @($laneResults.Values | Where-Object { $null -ne $_ } | ForEach-Object { $_.status })

$hasFail = @($allStatuses | Where-Object { $_ -eq 'FAIL' }).Count -gt 0
$hasBlockedOrReq = @($allStatuses | Where-Object { $_ -match 'BLOCKED' -or $_ -match 'REQUIRED' }).Count -gt 0
$allPass = (@($allStatuses | Where-Object { $_ -eq 'PASS' }).Count -eq $allStatuses.Count) -and ($allStatuses.Count -gt 0)

$overallStatus = $(if ($hasFail) { 'FAIL' }
                   elseif ($allPass) { 'PASS' }
                   elseif ($hasBlockedOrReq -and (@($allStatuses | Where-Object { $_ -eq 'PASS' }).Count -gt 0)) { 'PARTIAL' }
                   else { 'BLOCKED_EXTERNAL' })

$disposition = $(if ($overallStatus -eq 'PASS') { 'PASS' }
                 elseif ($overallStatus -eq 'PARTIAL') { 'PARTIAL' }
                 elseif ($overallStatus -eq 'FAIL') { 'FAIL' }
                 else { 'BLOCKED_EXTERNAL' })

$resultObject = [ordered]@{
    schemaVersion = 1
    lane = "orchestrator"
    status = $overallStatus
    commit = $headSha
    tree = $treeSha
    exactTreeBound = $exactTreeBound
    candidatePath = $CandidatePath
    candidateHash = $null
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
    failureReason = $(if ($hasFail) { "One or more certification lanes failed" } else { $null })
    details = [ordered]@{
        capabilityMatrix = $capabilityMatrix
        laneResults = $laneResults
    }
    disposition = $disposition
    artifactHashes = [ordered]@{}
}

$jsonText = $resultObject | ConvertTo-Json -Depth 10
Set-Content -LiteralPath $OutputPath -Value $jsonText -Encoding utf8NoBOM

$fileBytes = [IO.File]::ReadAllBytes($OutputPath)
$finalSha256 = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($fileBytes)).Replace('-', '').ToLowerInvariant()
Set-Content -LiteralPath "$OutputPath.sha256" -Value "$finalSha256  $([IO.Path]::GetFileName($OutputPath))" -Encoding utf8NoBOM
Write-Host "`n================================================================="
Write-Host "Overall External Certification Result: $overallStatus (disposition: $disposition)" -ForegroundColor $(if ($disposition -eq 'PASS') { 'Green' } elseif ($disposition -eq 'PARTIAL' -or $disposition -eq 'BLOCKED_EXTERNAL') { 'Yellow' } else { 'Red' })
Write-Host "Artifact written: $OutputPath"
Write-Host "================================================================="

return [pscustomobject]$resultObject
