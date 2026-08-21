[CmdletBinding()]
param(
    [string] $RepoRoot = (Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent),
    [string] $BuildRoot
)

$ErrorActionPreference = 'Stop'
if (-not $BuildRoot) { $BuildRoot = Join-Path $RepoRoot 'build\release-test' }
$candidateScript = Join-Path $RepoRoot 'verify\release\Build-ReleaseCandidate.ps1'
$failures = 0

function Check {
    param([bool] $Condition, [string] $Description)
    if ($Condition) { Write-Host "PASS: $Description" } else { Write-Host "FAIL: $Description"; $script:failures++ }
}

function Invoke-Checked {
    param([string] $FilePath, [string[]] $Arguments)
    & $FilePath @Arguments
    $exitCode = $LASTEXITCODE
    Check ($exitCode -eq 0) "command succeeds: $([IO.Path]::GetFileName($FilePath)) $($Arguments -join ' ')"
    return $exitCode
}

function Remove-TempDirectory {
    param([string] $Path)
    if (-not $Path -or -not (Test-Path -LiteralPath $Path)) { return }
    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    $temp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
    if (-not $full.StartsWith($temp + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "TEST_TEMP_ROOT_ESCAPE: $full" }
    Remove-Item -LiteralPath $full -Recurse -Force
}

$outputA = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-release-candidate-test-a-' + [guid]::NewGuid().ToString('N'))
$outputB = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-release-candidate-test-b-' + [guid]::NewGuid().ToString('N'))
try {
    Check (Test-Path -LiteralPath $candidateScript -PathType Leaf) 'release-candidate entry point exists'
    $verifyText = Get-Content -LiteralPath (Join-Path $RepoRoot 'verify\verify.ps1') -Raw
    Check ($verifyText -match "release-candidate") 'verify.ps1 exposes release-candidate'
    Check ((Get-Content -LiteralPath $candidateScript -Raw) -match 'RELEASE_CLEAN_TREE_REQUIRED') 'candidate rejects dirty trees'
    Check ((Get-Content -LiteralPath $candidateScript -Raw) -match 'PRODUCTION_PIN_REQUIRED') 'candidate keeps production pins fail-closed'

    $cmake = (Get-Command cmake -ErrorAction Stop).Source
    Invoke-Checked -FilePath $cmake -Arguments @('--preset', 'release-test') | Out-Null
    Invoke-Checked -FilePath $cmake -Arguments @('--build', '--preset', 'release-test') | Out-Null
    $pwsh = (Get-Command pwsh -ErrorAction Stop).Source
    $candidateArgs = @('-NoProfile', '-NonInteractive', '-File', $candidateScript, '-Mode', 'unsigned-validation', '-BuildRoot', $BuildRoot, '-OutputDirectory', $outputA, '-SkipVerification', '-OutputFormat', 'human')
    Invoke-Checked -FilePath $pwsh -Arguments $candidateArgs | Out-Null
    $candidateArgs[$candidateArgs.IndexOf($outputA)] = $outputB
    Invoke-Checked -FilePath $pwsh -Arguments $candidateArgs | Out-Null

    foreach ($output in @($outputA, $outputB)) {
        foreach ($name in @('candidate-manifest.json', 'provenance.json', 'SHA256SUMS.txt', 'RELEASE_NOTES.md', 'dependency-inventory.json')) {
            Check (Test-Path -LiteralPath (Join-Path $output $name) -PathType Leaf) "candidate emits $name"
        }
        $manifest = Get-Content -LiteralPath (Join-Path $output 'candidate-manifest.json') -Raw | ConvertFrom-Json
        Check ([string]$manifest.classification -ceq 'unsigned-validation') 'candidate manifest classifies unsigned output explicitly'
        Check (-not [bool]$manifest.productionReady -and -not [bool]$manifest.signed) 'unsigned candidate is not production-ready'
        Check ([string]$manifest.version -match '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') 'candidate manifest carries strict product version'
        $checksumLines = @(Get-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') | Where-Object { $_ -and $_ -notmatch '^\s*#' })
        Check ($checksumLines.Count -gt 0) 'candidate checksum list is non-empty'
        foreach ($line in $checksumLines) {
            if ($line -notmatch '^([0-9A-F]{64})  (.+)$') { Check $false 'checksum row has standard SHA-256 format'; continue }
            $hash = (Get-FileHash -LiteralPath (Join-Path $output $Matches[2]) -Algorithm SHA256).Hash.ToUpperInvariant()
            Check ($hash -ceq $Matches[1]) "checksum matches $($Matches[2])"
        }
    }
    $archiveA = (Get-FileHash -LiteralPath (Get-ChildItem -LiteralPath $outputA -Filter '*.zip' -File | Select-Object -First 1).FullName -Algorithm SHA256).Hash
    $archiveB = (Get-FileHash -LiteralPath (Get-ChildItem -LiteralPath $outputB -Filter '*.zip' -File | Select-Object -First 1).FullName -Algorithm SHA256).Hash
    Check ($archiveA -ceq $archiveB) 'repeated candidate archives have identical SHA-256'

    $productionOutput = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-production-candidate-blocked-' + [guid]::NewGuid().ToString('N'))
    try {
        $production = & $pwsh -NoProfile -NonInteractive -File $candidateScript -Mode production -BuildRoot $BuildRoot -OutputDirectory $productionOutput -SkipVerification -OutputFormat json 2>&1
        $productionExit = $LASTEXITCODE
        Check ($productionExit -eq 1 -and (($production | Out-String) -match 'PRODUCTION_MANIFEST_SIGNATURE_REQUIRED')) 'production candidate rejects missing detached manifest signature'
    } finally {
        Remove-TempDirectory -Path $productionOutput
    }
} catch {
    Check $false "release-candidate test completed without exception ($($_.Exception.Message))"
} finally {
    Remove-TempDirectory -Path $outputA
    Remove-TempDirectory -Path $outputB
}

Write-Host "Failures: $failures"
exit $(if ($failures -eq 0) { 0 } else { 1 })
