[CmdletBinding()]
param(
    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string] $Exe = '',
    [string] $Filter = 'all',
    [string] $ArtifactsDir = '',
    [int] $LifecycleIterations = 2
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
New-Item -ItemType Directory -Path $resolvedArtifacts -Force | Out-Null

Import-Module (Join-Path $PSScriptRoot 'FastFilesE2E.psm1') -Force
$result = Invoke-FastFilesE2E -Exe $resolvedExe -RepoRoot $resolvedRepo -ArtifactsDir $resolvedArtifacts `
    -Filter $Filter -LifecycleIterations $LifecycleIterations
$result | ConvertTo-Json -Depth 20
switch ([string]$result.status) {
    'PASS' { exit 0 }
    'SKIPPED' { exit 2 }
    default { exit 1 }
}
