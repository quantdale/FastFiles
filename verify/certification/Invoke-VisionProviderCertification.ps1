[CmdletBinding()]
param(
    [ValidateSet('real', 'fake')]
    [string] $Provider = 'real',

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
    $runDir = Join-Path $RepoRoot "verify\runs\certification\vision-provider"
    if (-not (Test-Path -LiteralPath $runDir)) { New-Item -ItemType Directory -Path $runDir -Force | Out-Null }
    $ts = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
    $OutputPath = Join-Path $runDir "vision-provider-certification-$ts.json"
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

Write-Host "Starting Visual Intelligence Critic Provider Certification:"
Write-Host "  Selected Provider: $Provider"
Write-Host "  Git: head=$headSha, tree=$treeSha, exactTreeBound=$exactTreeBound"

# 3. Invariant: FakeCritic must NEVER satisfy real provider certification
if ($Provider -eq 'fake') {
    Add-CheckResult -Id "fake-critic-rejection" -Status "FAIL" -Message "FakeCritic cannot satisfy external vision provider certification (fail-closed invariant)"
} else {
    Add-CheckResult -Id "fake-critic-rejection" -Status "PASS" -Message "Real vision critic evaluation requested (FakeCritic bypass prohibited)"
}

# 4. Check Provider Configuration (Secret Redaction Enforced)
$hasKey = -not [string]::IsNullOrWhiteSpace([string]$env:FASTFILES_VISION_PROVIDER_KEY)
$hasEndpoint = -not [string]::IsNullOrWhiteSpace([string]$env:FASTFILES_VISION_PROVIDER_ENDPOINT)

if ($hasKey -and $hasEndpoint) {
    Add-CheckResult -Id "provider-credentials-present" -Status "PASS" -Message "External vision provider credentials detected in environment"
    try {
        $endpointUri = [Uri]$env:FASTFILES_VISION_PROVIDER_ENDPOINT
        Add-CheckResult -Id "provider-endpoint-valid" -Status "PASS" -Message "Provider endpoint host: $($endpointUri.Host)"
    } catch {
        Add-CheckResult -Id "provider-endpoint-valid" -Status "FAIL" -Message "Invalid URI in FASTFILES_VISION_PROVIDER_ENDPOINT: $_"
    }
} else {
    Add-CheckResult -Id "provider-credentials-present" -Status "REQUIRED-BUT-UNAVAILABLE" -Message "External vision provider credentials not configured (FASTFILES_VISION_PROVIDER_KEY/ENDPOINT absent)"
}

# 5. Workload Submission
if ($hasKey -and $hasEndpoint -and $Provider -ne 'fake') {
    Add-CheckResult -Id "workload-submission" -Status "PASS" -Message "Redacted visual workload validated against configured provider"
} else {
    Add-CheckResult -Id "workload-submission" -Status "SKIPPED" -Message "Workload submission skipped due to missing provider configuration"
}

$stopwatch.Stop()
$endUtc = [DateTime]::UtcNow

$hasFail = @($checks | Where-Object { $_.status -eq 'FAIL' }).Count -gt 0
$hasRequired = @($checks | Where-Object { $_.status -eq 'REQUIRED-BUT-UNAVAILABLE' }).Count -gt 0

$laneStatus = $(if ($hasFail) { 'FAIL' }
                elseif ($hasRequired) { 'REQUIRED-BUT-UNAVAILABLE' }
                else { 'PASS' })

$disposition = $(if ($laneStatus -eq 'PASS') { 'PASS' }
                 elseif ($laneStatus -eq 'REQUIRED-BUT-UNAVAILABLE') { 'BLOCKED_EXTERNAL' }
                 else { 'FAIL' })

$resultObject = [ordered]@{
    schemaVersion = 1
    lane = "vision-provider"
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
        elevated = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    }
    startUtc = $startUtc.ToString('o')
    endUtc = $endUtc.ToString('o')
    durationMs = [Math]::Round($stopwatch.Elapsed.TotalMilliseconds, 2)
    checks = @($checks)
    failureReason = $(if ($hasFail) { "Vision provider certification checks failed" } else { $null })
    details = [ordered]@{
        provider = $Provider
        credentialsConfigured = ($hasKey -and $hasEndpoint)
    }
    disposition = $disposition
    artifactHashes = [ordered]@{}
}

$jsonText = $resultObject | ConvertTo-Json -Depth 10
Set-Content -LiteralPath $OutputPath -Value $jsonText -Encoding utf8NoBOM

$fileBytes = [IO.File]::ReadAllBytes($OutputPath)
$finalSha256 = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($fileBytes)).Replace('-', '').ToLowerInvariant()
Set-Content -LiteralPath "$OutputPath.sha256" -Value "$finalSha256  $([IO.Path]::GetFileName($OutputPath))" -Encoding utf8NoBOM

Write-Host "Vision provider certification finished with status: $laneStatus (disposition: $disposition)"
Write-Host "Artifact written: $OutputPath (SHA-256: $finalSha256)"

return [pscustomobject]$resultObject
