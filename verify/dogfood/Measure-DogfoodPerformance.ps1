[CmdletBinding()]
param(
    [string] $RepoRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
    [ValidateSet('debug', 'debug-diagnostic', 'release-test', 'release')]
    [string] $Configuration = 'debug',
    [string] $OutputPath = (Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-dogfood-performance-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) + '.json'),
    [string] $Scales = '10000,100000,500000',
    [switch] $SkipBuild,
    [switch] $SkipLaunch,
    [switch] $Diagnostics
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function ConvertTo-FullPath {
    param([Parameter(Mandatory)] [string] $Path)
    return [IO.Path]::GetFullPath($Path).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
}

function Assert-ExternalOutputPath {
    param(
        [Parameter(Mandatory)] [string] $Path,
        [Parameter(Mandatory)] [string] $Repository
    )

    $fullPath = ConvertTo-FullPath -Path $Path
    $repoPath = ConvertTo-FullPath -Path $Repository
    if ($fullPath.Equals($repoPath, [StringComparison]::OrdinalIgnoreCase) -or
        $fullPath.StartsWith($repoPath + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "DOGFOOD_OUTPUT_INSIDE_REPOSITORY: '$fullPath'"
    }
    return $fullPath
}

function Get-FastFilesProcessIds {
    return @(
        Get-Process -Name 'FastFiles', 'FastFilesEngine' -ErrorAction SilentlyContinue |
            ForEach-Object { [int]$_.Id }
    )
}

function Stop-NewFastFilesProcesses {
    param([Parameter(Mandatory)] [AllowEmptyCollection()] [int[]] $BeforeIds)

    $before = [Collections.Generic.HashSet[int]]::new()
    foreach ($id in $BeforeIds) { $null = $before.Add($id) }
    $newIds = @(
        Get-FastFilesProcessIds |
            Where-Object { -not $before.Contains([int]$_) } |
            Sort-Object -Descending
    )
    foreach ($id in $newIds) {
        Stop-Process -Id $id -Force -ErrorAction SilentlyContinue
    }
    if ($newIds.Count -gt 0) {
        Start-Sleep -Milliseconds 500
    }
    return $newIds
}

function Measure-FastFilesLaunch {
    param(
        [Parameter(Mandatory)] [string] $Executable,
        [Parameter(Mandatory)] [string] $Label
    )

    $beforeIds = @(Get-FastFilesProcessIds)
    if ($beforeIds.Count -gt 0) {
        return [pscustomobject]@{
            label = $Label
            status = 'SKIPPED'
            reason = 'existing-fastfiles-process'
            launchMs = $null
            processId = $null
            newlyObservedProcessIds = @()
        }
    }

    $stopwatch = [Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath $Executable -WorkingDirectory (Split-Path -Parent $Executable) -PassThru
    $ready = $false
    try {
        while ($stopwatch.Elapsed.TotalSeconds -lt 15) {
            $process.Refresh()
            if ($process.HasExited) { break }
            if ($process.MainWindowHandle -ne [IntPtr]::Zero) {
                $ready = $true
                break
            }
            Start-Sleep -Milliseconds 100
        }
        $newIds = @(Get-FastFilesProcessIds | Where-Object { $beforeIds -notcontains [int]$_ })
        return [pscustomobject]@{
            label = $Label
            status = if ($ready) { 'PASS' } else { 'FAIL' }
            reason = if ($ready) { $null } elseif ($process.HasExited) { 'process-exited-before-window' } else { 'window-timeout' }
            launchMs = [math]::Round($stopwatch.Elapsed.TotalMilliseconds, 1)
            processId = [int]$process.Id
            newlyObservedProcessIds = @($newIds)
        }
    } finally {
        $stopwatch.Stop()
        $null = Stop-NewFastFilesProcesses -BeforeIds $beforeIds
    }
}

$repoFull = ConvertTo-FullPath -Path $RepoRoot
$outputFull = Assert-ExternalOutputPath -Path $OutputPath -Repository $repoFull
$outputDirectory = Split-Path -Parent $outputFull
if ($outputDirectory) {
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
}

$benchmark = Join-Path $repoFull ("build\$Configuration\tests\engine\ffengine_bench_large_index_scalability.exe")
$uiExecutable = Join-Path $repoFull ("build\$Configuration\src\ui\FastFiles.exe")
if (-not $SkipBuild) {
    if ($Configuration -eq 'debug-diagnostic') {
        throw 'DOGFOOD_DIAGNOSTIC_BUILD_REQUIRES_SKIP_BUILD: debug-diagnostic has no stable CMake preset; build it with its diagnostic workflow first'
    }
    & cmake --build --preset $Configuration --target ffengine_bench_large_index_scalability
    if ($LASTEXITCODE -ne 0) { throw "DOGFOOD_BENCHMARK_BUILD_FAILED: exit $LASTEXITCODE" }
}
if (-not (Test-Path -LiteralPath $benchmark -PathType Leaf)) {
    throw "DOGFOOD_BENCHMARK_MISSING: '$benchmark'"
}

$launchResults = [Collections.Generic.List[object]]::new()
if (-not $SkipLaunch) {
    if (-not (Test-Path -LiteralPath $uiExecutable -PathType Leaf)) {
        throw "DOGFOOD_UI_EXECUTABLE_MISSING: '$uiExecutable'"
    }
    $launchResults.Add((Measure-FastFilesLaunch -Executable $uiExecutable -Label 'cold-process-launch'))
    $launchResults.Add((Measure-FastFilesLaunch -Executable $uiExecutable -Label 'warm-process-launch'))
} else {
    $launchResults.Add([pscustomobject]@{ label = 'cold-process-launch'; status = 'SKIPPED'; reason = 'skip-launch'; launchMs = $null; processId = $null; newlyObservedProcessIds = @() })
    $launchResults.Add([pscustomobject]@{ label = 'warm-process-launch'; status = 'SKIPPED'; reason = 'skip-launch'; launchMs = $null; processId = $null; newlyObservedProcessIds = @() })
}

$benchmarkArguments = @('--scales', $Scales, '--output', $outputFull)
if ($Diagnostics) { $benchmarkArguments += '--diagnostics' }
& $benchmark @benchmarkArguments
if ($LASTEXITCODE -ne 0) { throw "DOGFOOD_BENCHMARK_FAILED: exit $LASTEXITCODE" }

$benchmarkJson = Get-Content -LiteralPath $outputFull -Raw | ConvertFrom-Json
if (@($benchmarkJson.datasets).Count -eq 0) { throw 'DOGFOOD_BENCHMARK_NO_DATASETS' }

$operatingSystem = Get-CimInstance -ClassName Win32_OperatingSystem
$computer = Get-CimInstance -ClassName Win32_ComputerSystem
$result = [ordered]@{
    schema = 'fastfiles-dogfood-performance-v1'
    status = if (@($launchResults | Where-Object status -eq 'FAIL').Count -eq 0) { 'PASS' } else { 'FAIL' }
    measuredAtUtc = (Get-Date).ToUniversalTime().ToString('o')
    repository = [ordered]@{ configuration = $Configuration; commit = (git -C $repoFull rev-parse HEAD) }
    host = [ordered]@{
        osVersion = [string]$operatingSystem.Version
        osBuild = [string]$operatingSystem.BuildNumber
        architecture = [string]$computer.SystemType
        logicalProcessors = [int]$computer.NumberOfLogicalProcessors
        memoryBytes = [UInt64]$computer.TotalPhysicalMemory
        powershell = [string]$PSVersionTable.PSVersion
    }
    launches = @($launchResults.ToArray())
    indexedBenchmark = [ordered]@{
        output = $outputFull
        scales = $Scales
        datasetCount = @($benchmarkJson.datasets).Count
        schema = [string]$benchmarkJson.schema
    }
    interactiveMeasurements = [ordered]@{
        folderOpen = 'requires semantic UIA timing scenario; not inferred from engine benchmark'
        preview = 'requires semantic UIA timing scenario; not inferred from engine benchmark'
        storage = 'requires semantic UIA timing scenario; not inferred from engine benchmark'
    }
}
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath ($outputFull + '.summary.json') -Encoding utf8
Write-Output ($result | ConvertTo-Json -Depth 8)
