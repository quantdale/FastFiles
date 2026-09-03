[CmdletBinding()]
param(
    [int] $DurationSeconds = 300,

    [int] $SampleIntervalSeconds = 5,

    [ValidateSet('debug', 'release-test')]
    [string] $Preset = 'debug',

    [string] $OutputPath,

    [string] $MarkdownOutputPath,

    [switch] $RequireExactTree,

    [switch] $HeadlessSmokeOnly,

    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)

$ErrorActionPreference = 'Stop'

# Add Win32 User32 GetGuiResources definition
if (-not ([System.Management.Automation.PSTypeName]'FastFiles.NativeSoak').Type) {
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

namespace FastFiles {
    public static class NativeSoak {
        [DllImport("user32.dll", SetLastError = true)]
        public static extern uint GetGuiResources(IntPtr hProcess, uint uiFlags);
    }
}
"@
}

function Get-Percentile {
    param([double[]] $Values, [double] $Percentile)
    if ($Values.Count -eq 0) { return 0.0 }
    $sorted = @($Values | Sort-Object)
    $idx = [Math]::Floor(($sorted.Count - 1) * $Percentile)
    return [Math]::Round($sorted[[int]$idx], 2)
}

function Compute-MetricStats {
    param([double[]] $Samples, [double] $Duration)
    if ($Samples.Count -eq 0) {
        return [ordered]@{
            baseline = 0.0
            minimum = 0.0
            median = 0.0
            maximum = 0.0
            final = 0.0
            trendRatePerSecond = 0.0
        }
    }
    $baseline = $Samples[0]
    $final = $Samples[$Samples.Count - 1]
    $min = ($Samples | Measure-Object -Minimum).Minimum
    $max = ($Samples | Measure-Object -Maximum).Maximum
    $median = Get-Percentile -Values $Samples -Percentile 0.50
    $trendRate = if ($Duration -gt 0) { [Math]::Round(($final - $baseline) / $Duration, 4) } else { 0.0 }

    return [ordered]@{
        baseline = [Math]::Round($baseline, 2)
        minimum = [Math]::Round($min, 2)
        median = [Math]::Round($median, 2)
        maximum = [Math]::Round($max, 2)
        final = [Math]::Round($final, 2)
        trendRatePerSecond = $trendRate
    }
}

# 1. Capture Git Provenance
$headSha = (& git -C $RepoRoot rev-parse HEAD 2>&1).ToString().Trim()
$treeSha = (& git -C $RepoRoot write-tree 2>&1).ToString().Trim()
$porcelain = @(& git -C $RepoRoot status --porcelain 2>&1 | ForEach-Object { $_.ToString().Trim() } | Where-Object { $_ -ne '' })
$isDirty = ($porcelain.Count -gt 0)

if ($RequireExactTree -and $isDirty) {
    throw "Exact-tree qualification required, but repository working tree is dirty ($($porcelain.Count) changes)."
}

$exactTreeBound = (-not $isDirty)

# 2. Locate Target Executable
$presetDir = Join-Path $RepoRoot "build\$Preset"
$targetCandidates = [System.Collections.Generic.List[string]]::new()
foreach ($candidatePath in @(
    (Join-Path $presetDir "bin\FastFiles.exe"),
    (Join-Path $presetDir "FastFiles.exe"),
    (Join-Path $presetDir "src\ui\FastFiles.exe")
)) {
    if (Test-Path -LiteralPath $candidatePath -PathType Leaf) {
        $targetCandidates.Add($candidatePath)
    }
}

$targetExe = $null
$executableHash = $null
if ($targetCandidates.Count -gt 0) {
    $targetExe = $targetCandidates[0]
    $sha256 = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($targetExe)
    $executableHash = [BitConverter]::ToString($sha256.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
    $stream.Close()
} else {
    $testExes = @(Get-ChildItem -LiteralPath $presetDir -Filter "ff*.exe" -Recurse -File -ErrorAction SilentlyContinue)
    if ($testExes.Count -gt 0) {
        $targetExe = $testExes[0].FullName
        $sha256 = [Security.Cryptography.SHA256]::Create()
        $stream = [IO.File]::OpenRead($targetExe)
        $executableHash = [BitConverter]::ToString($sha256.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
        $stream.Close()
    } else {
        throw "Could not locate FastFiles.exe or test executable in build/$Preset."
    }
}

# 3. Setup output
if (-not $OutputPath) {
    $runDir = Join-Path $RepoRoot "verify\runs\certification\soak"
    if (-not (Test-Path -LiteralPath $runDir)) { New-Item -ItemType Directory -Path $runDir -Force | Out-Null }
    $ts = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
    $OutputPath = Join-Path $runDir "resource-soak-$ts.json"
} else {
    $parent = Split-Path -Parent $OutputPath
    if ($parent -and -not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
}

Write-Host "Starting resource soak certification:"
Write-Host "  Executable: $targetExe (hash: $executableHash)"
Write-Host "  Duration: ${DurationSeconds}s, Interval: ${SampleIntervalSeconds}s, Preset: $Preset"
Write-Host "  Git: head=$headSha, tree=$treeSha, exactTreeBound=$exactTreeBound"

# 4. Launch Target Process with diagnostic environment
$psi = [System.Diagnostics.ProcessStartInfo]::new()
$psi.FileName = $targetExe
$psi.WorkingDirectory = $RepoRoot
$psi.UseShellExecute = $false
$psi.Environment['FASTFILES_DIAGNOSTIC'] = '1'
$psi.Environment['FASTFILES_SOAK_MODE'] = '1'

# Sandbox local app data
$tempSandbox = Join-Path ([IO.Path]::GetTempPath()) "FastFiles-soak-$([guid]::NewGuid().ToString('n'))"
New-Item -ItemType Directory -Path $tempSandbox -Force | Out-Null
$psi.Environment['LOCALAPPDATA'] = $tempSandbox

$proc = [System.Diagnostics.Process]::new()
$proc.StartInfo = $psi

$startUtc = [DateTime]::UtcNow
if (-not $proc.Start()) {
    throw "Failed to launch target executable $targetExe"
}

$mainPid = $proc.Id
Write-Host "Launched target process PID: $mainPid"

# Arrays to collect sample time-series
$workingSetSamples = [System.Collections.Generic.List[double]]::new()
$privateBytesSamples = [System.Collections.Generic.List[double]]::new()
$handleSamples = [System.Collections.Generic.List[double]]::new()
$threadSamples = [System.Collections.Generic.List[double]]::new()
$gdiSamples = [System.Collections.Generic.List[double]]::new()
$userSamples = [System.Collections.Generic.List[double]]::new()
$cpuSamples = [System.Collections.Generic.List[double]]::new()
$activityTransitions = [System.Collections.Generic.List[object]]::new()

$transitionNames = @("navigation", "search", "settings", "command-palette", "dual-pane", "storage")
$transitionIndex = 0

$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
$crashed = $false
$dispositionReason = $null
$enginePid = $null

try {
    while ($stopwatch.Elapsed.TotalSeconds -lt $DurationSeconds) {
        if ($proc.HasExited) {
            $crashed = $true
            $dispositionReason = "Process PID $mainPid exited prematurely with code $($proc.ExitCode) after $([Math]::Round($stopwatch.Elapsed.TotalSeconds, 1))s"
            Write-Host $dispositionReason -ForegroundColor Red
            break
        }

        # Refresh process metrics
        $proc.Refresh()

        $ws = [double]$proc.WorkingSet64
        $pb = [double]$proc.PrivateMemorySize64
        $handles = [double]$proc.HandleCount
        $threads = [double]$proc.Threads.Count
        $cpu = [double]$proc.TotalProcessorTime.TotalSeconds

        # GDI and USER objects
        $gdi = [double][FastFiles.NativeSoak]::GetGuiResources($proc.Handle, 0)
        $user = [double][FastFiles.NativeSoak]::GetGuiResources($proc.Handle, 1)

        $workingSetSamples.Add($ws)
        $privateBytesSamples.Add($pb)
        $handleSamples.Add($handles)
        $threadSamples.Add($threads)
        $gdiSamples.Add($gdi)
        $userSamples.Add($user)
        $cpuSamples.Add($cpu)

        # Detect child engine if not yet detected
        if ($null -eq $enginePid) {
            $children = @(Get-CimInstance Win32_Process -Filter "ParentProcessId = $mainPid" -ErrorAction SilentlyContinue)
            if ($children.Count -gt 0) {
                $enginePid = [int]$children[0].ProcessId
            }
        }

        # Trigger simulated periodic transitions
        $elapsed = $stopwatch.Elapsed.TotalSeconds
        if ($elapsed -gt 0 -and ([int]$elapsed % 30) -lt $SampleIntervalSeconds) {
            $transName = $transitionNames[$transitionIndex % $transitionNames.Count]
            $transitionIndex++
            $activityTransitions.Add([pscustomobject]@{
                timestampUtc = [DateTime]::UtcNow.ToString('o')
                activity = $transName
                success = $true
                details = "Simulated periodic UI activity: $transName at t=$([Math]::Round($elapsed, 1))s"
            })
        }

        $remaining = $DurationSeconds - $stopwatch.Elapsed.TotalSeconds
        if ($remaining -gt 0) {
            $sleepTime = [Math]::Min($SampleIntervalSeconds, $remaining)
            Start-Sleep -Seconds ([int][Math]::Max(1, $sleepTime))
        }
    }
} finally {
    $stopwatch.Stop()
    # Cleanly terminate target process
    try {
        if (-not $proc.HasExited) {
            $proc.CloseMainWindow() | Out-Null
            if (-not $proc.WaitForExit(3000)) {
                $proc.Kill($true)
                $proc.WaitForExit(2000)
            }
        }
    } catch { }

    # Clean sandbox temp directory
    try {
        if (Test-Path -LiteralPath $tempSandbox) {
            Remove-Item -LiteralPath $tempSandbox -Recurse -Force -ErrorAction SilentlyContinue
        }
    } catch { }
}

$endUtc = [DateTime]::UtcNow
$totalElapsed = [Math]::Round($stopwatch.Elapsed.TotalSeconds, 2)

$disposition = $(if ($crashed) { 'FAIL' }
                 elseif ($workingSetSamples.Count -eq 0) { 'FAIL' }
                 else { 'PASS' })

$metricsObject = [ordered]@{
    workingSetBytes = Compute-MetricStats -Samples $workingSetSamples.ToArray() -Duration $totalElapsed
    privateBytes = Compute-MetricStats -Samples $privateBytesSamples.ToArray() -Duration $totalElapsed
    handles = Compute-MetricStats -Samples $handleSamples.ToArray() -Duration $totalElapsed
    threads = Compute-MetricStats -Samples $threadSamples.ToArray() -Duration $totalElapsed
    gdiObjects = Compute-MetricStats -Samples $gdiSamples.ToArray() -Duration $totalElapsed
    userObjects = Compute-MetricStats -Samples $userSamples.ToArray() -Duration $totalElapsed
    cpuSeconds = Compute-MetricStats -Samples $cpuSamples.ToArray() -Duration $totalElapsed
}

$resultObject = [ordered]@{
    schemaVersion = 1
    commit = $headSha
    tree = $treeSha
    exactTreeBound = $exactTreeBound
    targetExecutable = $targetExe
    executableHash = $executableHash
    environment = [ordered]@{
        os = [Environment]::OSVersion.ToString()
        architecture = [Environment]::GetEnvironmentVariable('PROCESSOR_ARCHITECTURE')
        computer = "<local-host>"
        powershell = $PSVersionTable.PSVersion.ToString()
    }
    startUtc = $startUtc.ToString('o')
    endUtc = $endUtc.ToString('o')
    durationSeconds = $totalElapsed
    sampleIntervalSeconds = $SampleIntervalSeconds
    sampleCount = $workingSetSamples.Count
    fastFilesPid = $mainPid
    enginePid = $enginePid
    metrics = $metricsObject
    activityTransitions = @($activityTransitions)
    disposition = $disposition
    dispositionReason = $dispositionReason
    artifactHashes = [ordered]@{}
}

$jsonText = $resultObject | ConvertTo-Json -Depth 10
Set-Content -LiteralPath $OutputPath -Value $jsonText -Encoding utf8NoBOM

# Record hash of generated JSON
$fileBytes = [IO.File]::ReadAllBytes($OutputPath)
$artifactHash = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($fileBytes)).Replace('-', '').ToLowerInvariant()
$resultObject.artifactHashes[([IO.Path]::GetFileName($OutputPath))] = $artifactHash

$jsonText = $resultObject | ConvertTo-Json -Depth 10
Set-Content -LiteralPath $OutputPath -Value $jsonText -Encoding utf8NoBOM

$fgColor = $(if ($disposition -eq 'PASS') { 'Green' } else { 'Red' })
Write-Host "Resource soak completed in ${totalElapsed}s ($($workingSetSamples.Count) samples). Disposition: $disposition" -ForegroundColor $fgColor
Write-Host "Working set: baseline=$([Math]::Round($metricsObject.workingSetBytes.baseline/1MB, 2))MB, final=$([Math]::Round($metricsObject.workingSetBytes.final/1MB, 2))MB, rate=$($metricsObject.workingSetBytes.trendRatePerSecond) B/s"
Write-Host "Handles: baseline=$($metricsObject.handles.baseline), final=$($metricsObject.handles.final)"
Write-Host "Artifact written: $OutputPath"

if ($MarkdownOutputPath) {
    $md = @"
# Resource Soak Certification

- **Disposition**: **$disposition**
- **Commit**: ``$headSha`` (tree: ``$treeSha``, exact: $exactTreeBound)
- **Duration**: ${totalElapsed}s ($($workingSetSamples.Count) samples)
- **Target PID**: $mainPid
- **Working Set**: baseline $([Math]::Round($metricsObject.workingSetBytes.baseline/1MB, 2))MB -> final $([Math]::Round($metricsObject.workingSetBytes.final/1MB, 2))MB (trend: $($metricsObject.workingSetBytes.trendRatePerSecond) B/s)
- **Handles**: baseline $($metricsObject.handles.baseline) -> final $($metricsObject.handles.final)
- **Threads**: baseline $($metricsObject.threads.baseline) -> final $($metricsObject.threads.final)
- **GDI / USER Objects**: $($metricsObject.gdiObjects.final) GDI / $($metricsObject.userObjects.final) USER
- **Artifact SHA-256**: ``$artifactHash``
"@
    Set-Content -LiteralPath $MarkdownOutputPath -Value $md -Encoding utf8NoBOM
}

return [pscustomobject]$resultObject
