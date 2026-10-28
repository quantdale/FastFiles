[CmdletBinding()]
param(
    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string] $Exe = '',
    [string] $Filter = 'all',
    [string] $ArtifactsDir = '',
    [int] $LifecycleIterations = 2,
    [switch] $RequireExactTree,
    [switch] $SkipProvenanceGate
)

$ErrorActionPreference = 'Stop'
$resolvedRepo = (Resolve-Path -LiteralPath $RepoRoot).Path
if ([string]::IsNullOrWhiteSpace($Exe)) {
    $Exe = Join-Path $resolvedRepo 'build\debug-diagnostic\src\ui\FastFiles.exe'
}
$resolvedExe = [IO.Path]::GetFullPath($Exe)
if (-not (Test-Path -LiteralPath $resolvedExe -PathType Leaf)) {
    Write-Error "FastFiles executable was not found at '$resolvedExe'. Build it with .\dev.ps1 build -Preset debug-diagnostic -Target FastFiles."
    exit 3
}
if ([string]::IsNullOrWhiteSpace($ArtifactsDir)) {
    $runId = '{0:yyyyMMdd-HHmmss}-{1}' -f (Get-Date), ([guid]::NewGuid().ToString('N').Substring(0, 8))
    $ArtifactsDir = Join-Path $resolvedRepo "build\e2e-artifacts\$runId"
}
$resolvedArtifacts = [IO.Path]::GetFullPath($ArtifactsDir)
if (Test-Path -LiteralPath $resolvedArtifacts) {
    if (-not (Test-Path -LiteralPath $resolvedArtifacts -PathType Container) -or
        @(Get-ChildItem -LiteralPath $resolvedArtifacts -Force).Count -gt 0) {
        throw "Refusing non-empty or non-directory E2E artifact root: $resolvedArtifacts"
    }
} else {
    New-Item -ItemType Directory -Path $resolvedArtifacts -ErrorAction Stop | Out-Null
}

# Certification provenance gate: refuse to run functional E2E against binaries
# that do not embed the current source identity. A mismatch is terminal.
$provenance = $null
if (-not $SkipProvenanceGate) {
    Import-Module (Join-Path $resolvedRepo 'verify\core\Provenance.psm1') -Force -DisableNameChecking
    # repair-native-ui-verification-fidelity 4.2: <preset>/src/ui/app.exe
    # is three parents below the preset root. Missing siblings must reach the
    # existing provenance gate so it can fail closed rather than omit them.
    $buildRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $resolvedExe))
    $relatedBinaries = @($resolvedExe)
    foreach ($sibling in (Get-DefaultCertificationBinaries -RepoRoot $resolvedRepo -BuildDirectory $buildRoot)) {
        if ($relatedBinaries -notcontains $sibling) {
            $relatedBinaries += $sibling
        }
    }
    $gateMode = if ($RequireExactTree) { 'exact-tree' } else { 'development' }
    $gate = Invoke-ProvenanceGate -RepoRoot $resolvedRepo -Binaries $relatedBinaries -Mode $gateMode -Stage 'e2e-preflight' -PassThru
    $provenance = $gate.evidence
    if (-not $gate.ok) {
        $provenance | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $resolvedArtifacts 'provenance-gate-failure.json') -Encoding utf8NoBOM
        $failure = [pscustomobject]@{
            status        = 'FAIL'
            stage         = 'provenance-gate'
            detail        = $gate.message
            binaryProvenance = $provenance
        }
        $failure | ConvertTo-Json -Depth 20
        Write-Error $gate.message
        exit 1
    }
} else {
    Write-Warning 'Provenance gate skipped by operator (-SkipProvenanceGate); results are unbound to source identity.'
}

Import-Module (Join-Path $PSScriptRoot 'FastFilesE2E.psm1') -Force
$result = Invoke-FastFilesE2E -Exe $resolvedExe -RepoRoot $resolvedRepo -ArtifactsDir $resolvedArtifacts `
    -Filter $Filter -LifecycleIterations $LifecycleIterations
if ($provenance) {
    $result | Add-Member -NotePropertyName 'binaryProvenance' -NotePropertyValue $provenance -Force
}
$result | ConvertTo-Json -Depth 20
switch ([string]$result.status) {
    'PASS' { exit 0 }
    'SKIPPED' { exit 2 }
    default { exit 1 }
}
