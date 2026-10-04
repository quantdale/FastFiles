# performance-closure-and-multisurface-acceleration 8.1: portable reproduction
# runner for the projection-memory decomposition.
#
# It refuses to run on a dirty tree (the artifact records the clean revision),
# rebuilds the release-test benchmark, and writes the machine-readable reports
# into this directory next to this script.
#
#   pwsh ./openspec/changes/performance-closure-and-multisurface-acceleration/evidence/projection-memory/run-decomposition.ps1
[CmdletBinding()]
param(
    [string] $Configuration = 'release-test',
    [int] $RebuildCycles = 4,
    [int] $ExportBoundDirectories = 4096
)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..\..')).Path
Set-Location $repoRoot

# Only this script's own report outputs are allowed to be untracked: a
# re-run must not be blocked by the artifacts it previously produced.
$status = (git status --porcelain) | Where-Object {
    $_ -notmatch 'evidence/projection-memory/projection-memory-(200k|1m)\.json$'
}
if ($status.Count -ne 0) {
    throw "Refusing to measure a dirty tree:`n$($status -join "`n")"
}
$sha = (git rev-parse HEAD).Trim()
$lineEndingCheck = git diff --check
if ($LASTEXITCODE -ne 0) { throw "git diff --check failed: $lineEndingCheck" }

Import-Module (Join-Path $repoRoot 'verify/core/Toolchain.psm1') -Force
$toolchain = Find-VSToolchain
if (-not $toolchain) { throw 'No VC-capable Visual Studio toolchain found.' }
Enter-DevEnvironment -Toolchain $toolchain -Arch x64

& $toolchain.CMakeExe --build --preset $Configuration --target ffengine_bench_large_index_scalability
if ($LASTEXITCODE -ne 0) { throw "benchmark build failed ($LASTEXITCODE)" }

$binary = Join-Path $repoRoot "build/$Configuration/tests/engine/ffengine_bench_large_index_scalability.exe"
if (-not (Test-Path $binary)) { throw "benchmark binary not found: $binary" }

foreach ($scale in 200000, 1000000) {
    $output = Join-Path $PSScriptRoot ("projection-memory-{0}.json" -f $(if ($scale -ge 1000000) { '1m' } else { '200k' }))
    $arguments = @(
        '--memory-report', $output,
        '--scales', $scale,
        '--rebuild-cycles', $RebuildCycles,
        '--export-bound-directories', $ExportBoundDirectories,
        '--source-state', 'clean',
        '--source-sha', $sha,
        '--preset', $Configuration
    )
    & $binary @arguments
    if ($LASTEXITCODE -ne 0) { throw "memory report failed at scale $scale ($LASTEXITCODE)" }
    Write-Host "wrote $output"
}
