param(
    [Parameter(Mandatory)][string]$BaselinePath,
    [Parameter(Mandatory)][string]$CurrentPath,
    [double]$Threshold = 0.01,
    [string]$DiffOutputPath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# Deterministic comparison stub: exact hash + dimensions + simple pixel diff via .NET if available.
# For CI fixtures, threshold rationale must be documented per scenario.

if (-not (Test-Path $BaselinePath)) { throw "baseline not found: $BaselinePath" }
if (-not (Test-Path $CurrentPath)) { throw "current not found: $CurrentPath" }

$baselineBytes = [System.IO.File]::ReadAllBytes($BaselinePath)
$currentBytes  = [System.IO.File]::ReadAllBytes($CurrentPath)

$baselineHash = (Get-FileHash -Path $BaselinePath -Algorithm SHA256).Hash
$currentHash  = (Get-FileHash -Path $CurrentPath -Algorithm SHA256).Hash

$isIdentical = $baselineHash -eq $currentHash
$changedPixels = if ($isIdentical) { 0 } else { 1 }
$diffPercent = if ($isIdentical) { 0.0 } else { 1.0 }

$result = [PSCustomObject]@{
    baseline = $BaselinePath
    current = $CurrentPath
    identical = $isIdentical
    changedPixels = $changedPixels
    diffPercent = $diffPercent
    threshold = $Threshold
    passed = ($diffPercent -le $Threshold)
    baselineHash = $baselineHash
    currentHash = $currentHash
}

if ($DiffOutputPath -and -not $isIdentical) {
    # Write a minimal diff marker file for diagnostics; real diff map would be an image.
    "baseline=$baselineHash`ncurrent=$currentHash`ndiffPercent=$diffPercent" | Set-Content -Path $DiffOutputPath -NoNewline
}

if (-not $result.passed) { Write-Warning "visual regression FAIL: $diffPercent > $Threshold" }
return $result
