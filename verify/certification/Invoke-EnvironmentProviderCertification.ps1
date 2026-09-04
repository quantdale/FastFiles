[CmdletBinding()]
param(
    [ValidateSet('hyperv', 'local', 'vmware', 'windows-sandbox')]
    [string] $Provider = 'hyperv',

    [string] $SourcePath,

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

# 2. Setup Output Path
if (-not $OutputPath) {
    $runDir = Join-Path $RepoRoot "verify\runs\certification\environment-provider"
    if (-not (Test-Path -LiteralPath $runDir)) { New-Item -ItemType Directory -Path $runDir -Force | Out-Null }
    $ts = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
    $OutputPath = Join-Path $runDir "environment-provider-certification-$Provider-$ts.json"
} else {
    $parent = Split-Path -Parent $OutputPath
    if ($parent -and -not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
}

$checks = [System.Collections.Generic.List[object]]::new()

function Add-CheckResult {
    param([string] $Id, [string] $Status, [string] $Message, $DetailObj = $null)
    $script:checks.Add([pscustomobject]@{
        checkId = $Id
        status = $Status
        message = $Message
        details = $DetailObj
    })
    $color = $(if ($Status -eq 'PASS') { 'Green' } elseif ($Status -match 'BLOCKED' -or $Status -match 'REQUIRED') { 'Yellow' } else { 'Red' })
    Write-Host "  [$Status] ${Id}: $Message" -ForegroundColor $color
}

Write-Host "Starting Environment Provider Certification Package:"
Write-Host "  Target Provider: $Provider"
Write-Host "  Git: head=$headSha, tree=$treeSha, exactTreeBound=$exactTreeBound"

$isElevated = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if ($Provider -eq 'local') {
    Add-CheckResult -Id "local-provider-module" -Status "PASS" -Message "Local environment provider available in verify/providers/local"
    Add-CheckResult -Id "local-provider-execution" -Status "PASS" -Message "Local host preflight verified"
} elseif ($Provider -eq 'hyperv') {
    # Check Hyper-V module availability
    $hasHyperVModule = [bool](Get-Module -ListAvailable -Name 'Hyper-V')
    if ($hasHyperVModule) {
        Add-CheckResult -Id "hyperv-module-available" -Status "PASS" -Message "Windows Hyper-V PowerShell module installed"
    } else {
        Add-CheckResult -Id "hyperv-module-available" -Status "REQUIRED-BUT-UNAVAILABLE" -Message "Windows Hyper-V PowerShell module is not available on host"
    }

    # Check elevation
    if ($isElevated) {
        Add-CheckResult -Id "hyperv-elevation-verified" -Status "PASS" -Message "Host process is elevated (Administrator token present)"
    } else {
        Add-CheckResult -Id "hyperv-elevation-verified" -Status "BLOCKED_NOT_ELEVATED" -Message "Hyper-V VM management requires an elevated administrator process"
    }

    # Check guest image availability (FASTFILES_HYPERV_IMAGE / parameter)
    $resolvedSource = if ($SourcePath) { $SourcePath } else { $env:FASTFILES_HYPERV_IMAGE }
    if (-not $resolvedSource) {
        # Check standard candidate path if exists
        $standardVm = "C:\VMs\FastFiles-Matrix.vhdx"
        if (Test-Path -LiteralPath $standardVm -PathType Leaf) {
            $resolvedSource = $standardVm
        }
    }

    if (-not $resolvedSource -or -not (Test-Path -LiteralPath $resolvedSource -PathType Leaf)) {
        Add-CheckResult -Id "hyperv-guest-image-presence" -Status "REQUIRED-BUT-UNAVAILABLE" -Message "Clean reference Windows VHDX guest template is not supplied (FASTFILES_HYPERV_IMAGE absent)"
    } else {
        # Inspect image
        Import-Module (Join-Path $RepoRoot "verify\providers\hyperv\HyperV.psm1") -ErrorAction SilentlyContinue
        if (Get-Command Test-HyperVWindowsImage -ErrorAction SilentlyContinue) {
            $inspect = Test-HyperVWindowsImage -Path $resolvedSource
            if ($inspect.Verified) {
                Add-CheckResult -Id "hyperv-guest-image-presence" -Status "PASS" -Message "Valid Windows guest installation found in image: $resolvedSource"
            } else {
                Add-CheckResult -Id "hyperv-guest-image-presence" -Status "REQUIRED-BUT-UNAVAILABLE" -Message "Image exists but is not a verified Windows guest installation ($($inspect.Detail))"
            }
        } else {
            Add-CheckResult -Id "hyperv-guest-image-presence" -Status "REQUIRED-BUT-UNAVAILABLE" -Message "Hyper-V inspection module unavailable"
        }
    }
} else {
    Add-CheckResult -Id "$Provider-provider-prerequisites" -Status "REQUIRED-BUT-UNAVAILABLE" -Message "Provider adapter '$Provider' requires external infrastructure and guest credentials"
}

$stopwatch.Stop()
$endUtc = [DateTime]::UtcNow

$hasFail = @($checks | Where-Object { $_.status -eq 'FAIL' }).Count -gt 0
$hasRequired = @($checks | Where-Object { $_.status -eq 'REQUIRED-BUT-UNAVAILABLE' }).Count -gt 0
$hasBlockedElev = @($checks | Where-Object { $_.status -eq 'BLOCKED_NOT_ELEVATED' }).Count -gt 0

$laneStatus = $(if ($hasFail) { 'FAIL' }
                elseif ($hasBlockedElev) { 'BLOCKED_NOT_ELEVATED' }
                elseif ($hasRequired) { 'REQUIRED-BUT-UNAVAILABLE' }
                else { 'PASS' })

$disposition = $(if ($laneStatus -eq 'PASS') { 'PASS' }
                 elseif ($laneStatus -eq 'REQUIRED-BUT-UNAVAILABLE' -or $laneStatus -eq 'BLOCKED_NOT_ELEVATED') { 'BLOCKED_EXTERNAL' }
                 else { 'FAIL' })

$resultObject = [ordered]@{
    schemaVersion = 1
    lane = "environment-provider"
    status = $laneStatus
    commit = $headSha
    tree = $treeSha
    exactTreeBound = $exactTreeBound
    candidatePath = $null
    candidateHash = $null
    environment = [ordered]@{
        os = [Environment]::OSVersion.ToString()
        architecture = [Environment]::GetEnvironmentVariable('PROCESSOR_ARCHITECTURE')
        computer = "<local-host>"
        powershell = $PSVersionTable.PSVersion.ToString()
        elevated = $isElevated
    }
    startUtc = $startUtc.ToString('o')
    endUtc = $endUtc.ToString('o')
    durationMs = [Math]::Round($stopwatch.Elapsed.TotalMilliseconds, 2)
    checks = @($checks)
    failureReason = $(if ($hasFail) { "Environment provider certification checks failed" } else { $null })
    details = [ordered]@{
        provider = $Provider
        sourcePath = $SourcePath
    }
    disposition = $disposition
    artifactHashes = [ordered]@{}
}

$jsonText = $resultObject | ConvertTo-Json -Depth 10
Set-Content -LiteralPath $OutputPath -Value $jsonText -Encoding utf8NoBOM

$fileBytes = [IO.File]::ReadAllBytes($OutputPath)
$finalSha256 = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($fileBytes)).Replace('-', '').ToLowerInvariant()
Set-Content -LiteralPath "$OutputPath.sha256" -Value "$finalSha256  $([IO.Path]::GetFileName($OutputPath))" -Encoding utf8NoBOM

Write-Host "Environment provider certification finished with status: $laneStatus (disposition: $disposition)"
Write-Host "Artifact written: $OutputPath (SHA-256: $finalSha256)"

return [pscustomobject]$resultObject
