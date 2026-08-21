[CmdletBinding()]
param(
    [string] $RepoRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
    [ValidateSet('debug', 'debug-diagnostic', 'release-test', 'release')]
    [string] $Configuration = 'debug-diagnostic',
    [ValidateRange(10, 3600)]
    [int] $DurationSeconds = 30,
    [ValidateRange(1, 60)]
    [int] $IntervalSeconds = 5,
    [string] $OutputPath = (Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-dogfood-soak-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) + '.json')
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

function Get-TargetProcesses {
    return @(
        Get-Process -Name 'FastFiles', 'FastFilesEngine' -ErrorAction SilentlyContinue |
            ForEach-Object { [int]$_.Id }
    )
}

function Get-ProcessSample {
    param(
        [Parameter(Mandatory)] [int] $ProcessId,
        [Parameter(Mandatory)] [hashtable] $PreviousCpu,
        [Parameter(Mandatory)] [double] $ElapsedSeconds
    )

    $process = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
    if ($null -eq $process) {
        return [pscustomobject]@{ processId = $ProcessId; processName = $null; state = 'exited'; workingSetBytes = $null; privateBytes = $null; handles = $null; threads = $null; cpuPercent = $null; responding = $false }
    }

    $cpuSeconds = $null
    try { $cpuSeconds = $process.TotalProcessorTime.TotalSeconds } catch { }
    $cpuPercent = $null
    if ($null -ne $cpuSeconds) {
        if ($PreviousCpu.ContainsKey($ProcessId)) {
            $delta = [double]$cpuSeconds - [double]$PreviousCpu[$ProcessId]
            if ($ElapsedSeconds -gt 0) {
                $cpuPercent = [math]::Round(($delta / $ElapsedSeconds) / [Environment]::ProcessorCount * 100, 2)
            }
        }
        $PreviousCpu[$ProcessId] = $cpuSeconds
    }
    return [pscustomobject]@{
        processId = $ProcessId
        processName = [string]$process.ProcessName
        state = 'running'
        workingSetBytes = [UInt64]$process.WorkingSet64
        privateBytes = [UInt64]$process.PrivateMemorySize64
        handles = [int]$process.HandleCount
        threads = [int]$process.Threads.Count
        cpuPercent = $cpuPercent
        responding = if ($process.ProcessName -eq 'FastFiles') { [bool]$process.Responding } else { $null }
    }
}

function Stop-TrackedProcesses {
    param([Parameter(Mandatory)] [AllowEmptyCollection()] [int[]] $ProcessIds)
    foreach ($id in ($ProcessIds | Sort-Object -Descending -Unique)) {
        Stop-Process -Id $id -Force -ErrorAction SilentlyContinue
    }
    if ($ProcessIds.Count -gt 0) { Start-Sleep -Milliseconds 500 }
    $remaining = @($ProcessIds | Where-Object { Get-Process -Id $_ -ErrorAction SilentlyContinue })
    return $remaining
}

$repoFull = ConvertTo-FullPath -Path $RepoRoot
$outputFull = Assert-ExternalOutputPath -Path $OutputPath -Repository $repoFull
$outputDirectory = Split-Path -Parent $outputFull
if ($outputDirectory) { New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null }

$uiExecutable = Join-Path $repoFull ("build\$Configuration\src\ui\FastFiles.exe")
if (-not (Test-Path -LiteralPath $uiExecutable -PathType Leaf)) {
    throw "DOGFOOD_UI_EXECUTABLE_MISSING: '$uiExecutable'"
}

$initialIds = @(Get-TargetProcesses)
if ($initialIds.Count -gt 0) {
    throw 'DOGFOOD_SOAK_EXISTING_PROCESS: close FastFiles and FastFilesEngine before starting a resource-owned soak'
}

$startedAt = Get-Date
$stopwatch = [Diagnostics.Stopwatch]::StartNew()
$uiProcess = Start-Process -FilePath $uiExecutable -WorkingDirectory (Split-Path -Parent $uiExecutable) -PassThru
$trackedIds = [Collections.Generic.HashSet[int]]::new()
$null = $trackedIds.Add([int]$uiProcess.Id)
$samples = [Collections.Generic.List[object]]::new()
$previousCpu = @{}
$engineObserved = $false
$uiReady = $false
$failureReason = $null

try {
    while ($stopwatch.Elapsed.TotalSeconds -lt [math]::Min(15, $DurationSeconds)) {
        $uiProcess.Refresh()
        if ($uiProcess.HasExited) { $failureReason = 'ui-exited-before-soak'; break }
        if ($uiProcess.MainWindowHandle -ne [IntPtr]::Zero) { $uiReady = $true; break }
        Start-Sleep -Milliseconds 100
    }
    if (-not $uiReady -and $null -eq $failureReason) { $failureReason = 'ui-window-timeout' }

    $deadline = [DateTime]::UtcNow.AddSeconds($DurationSeconds)
    $lastSampleAt = [DateTime]::UtcNow
    while ($true) {
        $now = [DateTime]::UtcNow
        $elapsed = ($now - $lastSampleAt).TotalSeconds
        $currentIds = @(Get-TargetProcesses)
        foreach ($id in $currentIds) {
            if ($initialIds -notcontains [int]$id) { $null = $trackedIds.Add([int]$id) }
        }
        $processSamples = @($trackedIds | Sort-Object | ForEach-Object {
            $sample = Get-ProcessSample -ProcessId ([int]$_) -PreviousCpu $previousCpu -ElapsedSeconds $elapsed
            if ($sample.processName -eq 'FastFilesEngine' -and $sample.state -eq 'running') { $engineObserved = $true }
            if ($sample.processName -eq 'FastFiles' -and $sample.state -eq 'exited' -and $null -eq $failureReason) { $failureReason = 'ui-exited-during-soak' }
            if ($sample.processName -eq 'FastFilesEngine' -and $sample.state -eq 'exited' -and $engineObserved -and $null -eq $failureReason) { $failureReason = 'engine-exited-during-soak' }
            $sample
        })
        $samples.Add([pscustomobject]@{ atUtc = $now.ToString('o'); elapsedSeconds = [math]::Round($stopwatch.Elapsed.TotalSeconds, 2); processes = $processSamples })
        $lastSampleAt = $now
        if ($now -ge $deadline) { break }
        $remainingMilliseconds = [int][math]::Max(100, [math]::Min($IntervalSeconds * 1000, ($deadline - $now).TotalMilliseconds))
        Start-Sleep -Milliseconds $remainingMilliseconds
    }
} finally {
    $stopwatch.Stop()
    $trackedArray = @($trackedIds | ForEach-Object { [int]$_ })
    $remaining = @(Stop-TrackedProcesses -ProcessIds $trackedArray)
}

$os = Get-CimInstance -ClassName Win32_OperatingSystem
$computer = Get-CimInstance -ClassName Win32_ComputerSystem
$result = [ordered]@{
    schema = 'fastfiles-dogfood-soak-v1'
    status = if ($null -eq $failureReason -and $uiReady -and $samples.Count -gt 0 -and $remaining.Count -eq 0) { 'PASS' } else { 'FAIL' }
    measuredAtUtc = $startedAt.ToUniversalTime().ToString('o')
    repository = [ordered]@{ configuration = $Configuration; commit = (git -C $repoFull rev-parse HEAD) }
    host = [ordered]@{ osVersion = [string]$os.Version; osBuild = [string]$os.BuildNumber; architecture = [string]$computer.SystemType; logicalProcessors = [int]$computer.NumberOfLogicalProcessors }
    requestedDurationSeconds = $DurationSeconds
    observedDurationSeconds = if ($samples.Count -gt 0) { [math]::Round(($samples | Select-Object -Last 1).elapsedSeconds, 2) } else { 0 }
    intervalSeconds = $IntervalSeconds
    uiReady = $uiReady
    engineObserved = $engineObserved
    failureReason = $failureReason
    trackedProcessIds = $trackedArray
    samples = @($samples.ToArray())
    cleanupRemainingProcessIds = $remaining
}
$result | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $outputFull -Encoding utf8
Write-Output ($result | ConvertTo-Json -Depth 10)
if ($result.status -ne 'PASS') { exit 1 }
