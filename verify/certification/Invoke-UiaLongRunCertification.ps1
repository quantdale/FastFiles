[CmdletBinding()]
param(
    [ValidateSet('application-lifecycle', 'rapid-input', 'core-smoke')]
    [string] $Scenario = 'core-smoke',

    [int] $Iterations = 0,

    [int] $TimeoutSecondsPerIteration = 60,

    [ValidateSet('debug', 'release-test')]
    [string] $Preset = 'debug',

    [string] $OutputPath,

    [string] $MarkdownOutputPath,

    [switch] $ContinueOnFailure,

    [switch] $RequireExactTree,

    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)

$ErrorActionPreference = 'Stop'

# Default iterations if not explicitly passed
if ($Iterations -le 0) {
    switch ($Scenario) {
        'application-lifecycle' { $Iterations = 100 }
        'rapid-input'          { $Iterations = 50 }
        'core-smoke'           { $Iterations = 25 }
    }
}

function Get-Percentile {
    param([double[]] $Values, [double] $Percentile)
    if ($Values.Count -eq 0) { return 0.0 }
    $sorted = @($Values | Sort-Object)
    $idx = [Math]::Floor(($sorted.Count - 1) * $Percentile)
    return [Math]::Round($sorted[[int]$idx], 2)
}

function Get-SanitizedPath {
    param([string] $FullPath, [string] $RepoRoot)
    if ([string]::IsNullOrWhiteSpace($FullPath)) { return $FullPath }
    $resolvedRepo = (Resolve-Path $RepoRoot).Path.TrimEnd('\', '/')
    $resolvedFull = try { (Resolve-Path -LiteralPath $FullPath -ErrorAction SilentlyContinue).Path } catch { $FullPath }
    $pathStr = if ($resolvedFull) { $resolvedFull } else { $FullPath }
    if ($pathStr.StartsWith($resolvedRepo, [System.StringComparison]::OrdinalIgnoreCase)) {
        $rel = $pathStr.Substring($resolvedRepo.Length).TrimStart('\', '/')
        return ($rel -replace '\\', '/')
    }
    return ($pathStr -replace '\\', '/')
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

# 2. Locate FastFiles Target Executable
$presetDir = Join-Path $RepoRoot "build\$Preset"
$targetCandidates = @(
    (Join-Path $presetDir "src\ui\FastFiles.exe"),
    (Join-Path $presetDir "bin\FastFiles.exe"),
    (Join-Path $presetDir "FastFiles.exe")
)
$targetExe = $null
$executableHash = $null
foreach ($cand in $targetCandidates) {
    if (Test-Path -LiteralPath $cand -PathType Leaf) {
        $targetExe = $cand
        break
    }
}

if (-not $targetExe) {
    throw "Target FastFiles.exe not found in build/$Preset. Build preset first."
}

$sha256 = [Security.Cryptography.SHA256]::Create()
$stream = [IO.File]::OpenRead($targetExe)
$executableHash = [BitConverter]::ToString($sha256.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
$stream.Close()
$commandStr = Get-SanitizedPath -FullPath $targetExe -RepoRoot $RepoRoot

# 3. Import UIA Driver and Capability Modules
$uiaDriverModule = Join-Path $RepoRoot "verify\uia-driver\UiaDriver.psm1"
$uiaCapDir = Join-Path $RepoRoot "verify\capabilities\windows-ui-automation-validation"
$uiaWin32Module = Join-Path $uiaCapDir "UiAutomationWin32.psm1"
$uiaValidationModule = Join-Path $uiaCapDir "UiAutomationValidation.psm1"

Import-Module $uiaDriverModule -Global -DisableNameChecking -ErrorAction Stop
Import-Module $uiaWin32Module -Global -DisableNameChecking -ErrorAction Stop
Import-Module $uiaValidationModule -Global -DisableNameChecking -ErrorAction Stop

# 4. Check Interactive Desktop & UIA Availability
$driverAvail = Get-UiaDriverAvailability
$isInteractive = $driverAvail.Available

# 5. Output Setup
if (-not $OutputPath) {
    $runDir = Join-Path $RepoRoot "verify\runs\certification\uia-long-run"
    if (-not (Test-Path -LiteralPath $runDir)) { New-Item -ItemType Directory -Path $runDir -Force | Out-Null }
    $ts = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
    $OutputPath = Join-Path $runDir "long-run-uia-$Scenario-$ts.json"
} else {
    $parent = Split-Path -Parent $OutputPath
    if ($parent -and -not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
}

$testCategory = switch ($Scenario) {
    'application-lifecycle' { 'application-lifecycle' }
    'rapid-input'          { 'rapid-input' }
    Default                { 'uia' }
}

$startUtc = [DateTime]::UtcNow
$durations = [System.Collections.Generic.List[double]]::new()
$passedCount = 0
$failedCount = 0
$timeoutCount = 0
$skippedCount = 0
$firstFailure = $null

Write-Host "Starting UIA long-run certification: scenario='$Scenario', iterations=$Iterations, preset=$Preset"
Write-Host "Git: head=$headSha, tree=$treeSha, exactTreeBound=$exactTreeBound"
Write-Host "Executable: $commandStr (hash: $executableHash)"

if (-not $isInteractive) {
    Write-Host "Host lacks interactive desktop context: $($driverAvail.Reason)" -ForegroundColor Yellow
    $endUtc = [DateTime]::UtcNow
    $resultObject = [ordered]@{
        schemaVersion = 1
        testId = "ffuia_$Scenario"
        category = $testCategory
        command = $commandStr
        commit = $headSha
        tree = $treeSha
        exactTreeBound = $exactTreeBound
        executableHash = $executableHash
        environment = [ordered]@{
            os = [Environment]::OSVersion.ToString()
            architecture = [Environment]::GetEnvironmentVariable('PROCESSOR_ARCHITECTURE')
            computer = "<local-host>"
            powershell = $PSVersionTable.PSVersion.ToString()
        }
        startUtc = $startUtc.ToString('o')
        endUtc = $endUtc.ToString('o')
        requestedIterations = $Iterations
        completedIterations = 0
        passed = 0
        failed = 0
        timedOut = 0
        skipped = $Iterations
        minDurationMs = 0.0
        medianDurationMs = 0.0
        p95DurationMs = 0.0
        maxDurationMs = 0.0
        firstFailure = [pscustomobject]@{
            iteration = 0
            error = "Host lacks interactive desktop context ($($driverAvail.Reason)). Classified BLOCKED_ENVIRONMENT."
            exitCode = $null
            stdoutTail = ""
            stderrTail = ""
        }
        artifactHashes = [ordered]@{}
        disposition = 'BLOCKED_ENVIRONMENT'
    }
    $jsonText = $resultObject | ConvertTo-Json -Depth 10
    Set-Content -LiteralPath $OutputPath -Value $jsonText -Encoding utf8NoBOM
    $fileBytes = [IO.File]::ReadAllBytes($OutputPath)
    $finalSha256 = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($fileBytes)).Replace('-', '').ToLowerInvariant()
    Set-Content -LiteralPath "$OutputPath.sha256" -Value "$finalSha256  $([IO.Path]::GetFileName($OutputPath))" -Encoding utf8NoBOM
    return [pscustomobject]$resultObject
}

# 6. Execute Scenario Iterations
if ($Scenario -eq 'application-lifecycle') {
    for ($i = 1; $i -le $Iterations; $i++) {
        $iterSw = [System.Diagnostics.Stopwatch]::StartNew()
        $tempSandbox = Join-Path ([IO.Path]::GetTempPath()) "FastFiles-uialifecycle-$([guid]::NewGuid().ToString('n'))"
        New-Item -ItemType Directory -Path $tempSandbox -Force | Out-Null

        $proc = $null
        $isGraceful = $false
        $hasOrphans = $false
        $errorMsg = $null

        try {
            $psi = [System.Diagnostics.ProcessStartInfo]::new()
            $psi.FileName = $targetExe
            $psi.WorkingDirectory = $RepoRoot
            $psi.UseShellExecute = $false
            $psi.Environment['FASTFILES_DIAGNOSTIC'] = '1'
            $psi.Environment['FASTFILES_SOAK_MODE'] = '1'
            $psi.Environment['LOCALAPPDATA'] = $tempSandbox

            $proc = [System.Diagnostics.Process]::Start($psi)
            $appPid = $proc.Id

            # Poll for semantic readiness: window HWND and responding state
            $readySw = [System.Diagnostics.Stopwatch]::StartNew()
            $isReady = $false
            while ($readySw.ElapsedMilliseconds -lt ($TimeoutSecondsPerIteration * 1000)) {
                if ($proc.HasExited) { break }
                $proc.Refresh()
                if ($proc.MainWindowHandle -ne [IntPtr]::Zero -and $proc.Responding) {
                    $isReady = $true
                    break
                }
                Start-Sleep -Milliseconds 50
            }

            if (-not $isReady) {
                throw "Semantic readiness not reached within timeout (pid=$appPid)"
            }

            # Graceful shutdown request
            $proc.CloseMainWindow() | Out-Null
            if ($proc.WaitForExit(5000)) {
                $isGraceful = $true
            } else {
                $isGraceful = $false
                try { $proc.Kill($true); $proc.WaitForExit(2000) } catch { }
                throw "Process pid=$appPid failed graceful shutdown (forced kill required)"
            }

            # Post-shutdown residue check
            $orphans = @(Get-CimInstance Win32_Process -Filter "Name = 'FastFiles.exe' or Name = 'FastFilesEngine.exe'" -ErrorAction SilentlyContinue | Where-Object { $_.ProcessId -eq $appPid -or $_.ParentProcessId -eq $appPid })
            if ($orphans.Count -gt 0) {
                $hasOrphans = $true
                throw "Post-shutdown residue detected: $($orphans.Count) orphan process(es) remaining"
            }
        } catch {
            $errorMsg = $_.Exception.Message
        } finally {
            if ($proc -and -not $proc.HasExited) {
                try { $proc.Kill($true); $proc.WaitForExit(2000) } catch { }
            }
            if (Test-Path -LiteralPath $tempSandbox) {
                Remove-Item -LiteralPath $tempSandbox -Recurse -Force -ErrorAction SilentlyContinue
            }
        }

        $iterSw.Stop()
        $elapsedMs = [Math]::Round($iterSw.Elapsed.TotalMilliseconds, 2)
        $durations.Add($elapsedMs)

        if ($isGraceful -and -not $hasOrphans -and -not $errorMsg) {
            $passedCount++
            if ($i % 10 -eq 0 -or $i -eq $Iterations) {
                Write-Host "  Iteration $i/$Iterations PASS (${elapsedMs}ms)" -ForegroundColor Green
            }
        } else {
            $failedCount++
            Write-Host "  Iteration $i/$Iterations FAIL: $errorMsg (${elapsedMs}ms)" -ForegroundColor Red
            if ($null -eq $firstFailure) {
                $firstFailure = [pscustomobject]@{
                    iteration = $i
                    error = $errorMsg
                    exitCode = if ($proc) { $proc.ExitCode } else { -1 }
                    stdoutTail = ""
                    stderrTail = ""
                }
            }
            if (-not $ContinueOnFailure) { break }
        }
    }
} elseif ($Scenario -eq 'rapid-input') {
    # Launch persistent scenario context for rapid input transitions
    $quietLog = { param($m) }
    $ctx = $null
    try {
        $ctx = New-UiaScenarioContext -Exe $targetExe -RepoRoot $RepoRoot -ScenarioId 'rapid-input' -LogFn $quietLog
        if (-not $ctx -or -not $ctx.Main) { throw "Main window did not appear for rapid input scenario" }

        $driver = $ctx.Driver
        $main = $ctx.Main
        $appPid = [int]$ctx.Launched.Id

        Invoke-UiaActivateAppWindow -Driver $driver -Main $main
        $null = Keep-UiaWindowTopmost -Driver $driver -Element $main -Topmost $true
        $null = Wait-UiaStableGeometry -Driver $driver -Element $main -TimeoutMs 5000

        for ($i = 1; $i -le $Iterations; $i++) {
            $iterSw = [System.Diagnostics.Stopwatch]::StartNew()
            $iterError = $null

            try {
                # Transition 1: Command palette open -> check visible -> close
                $null = Send-UiaInput -Driver $driver -Keys '{CTRL}{SHIFT}p'
                $paletteEl = $false
                $palSw = [Diagnostics.Stopwatch]::StartNew()
                while (-not $paletteEl -and $palSw.ElapsedMilliseconds -lt 2500) {
                    try { $null = Find-UiaElement -Driver $driver -AutomationId '6301' -FromElement $main -TimeoutMs 300; $paletteEl = $true } catch { }
                }
                if (-not $paletteEl) { throw "Command palette edit 6301 not visible after Ctrl+Shift+P" }

                $null = Send-UiaInput -Driver $driver -Keys '{ESC}'
                Start-Sleep -Milliseconds 100

                # Transition 2: Dual pane toggle -> verify Pane.1 -> toggle back
                $dualResult = Invoke-UiaPaletteCommand -Driver $driver -Main $main -ProcessId $appPid -Query 'dual pane' -ExpectedNames @() -ExpectedCommandName 'Toggle Dual Pane' -ExpectedCommandId 'navigation.toggle-dual-pane'
                if (-not $dualResult.Pass) { throw "Dual pane command failed: $($dualResult.Detail)" }

                # Toggle back to single pane
                $null = Invoke-UiaPaletteCommand -Driver $driver -Main $main -ProcessId $appPid -Query 'dual pane' -ExpectedNames @() -ExpectedCommandName 'Toggle Dual Pane' -ExpectedCommandId 'navigation.toggle-dual-pane'

                # Transition 3: Search panel focus -> close
                $searchResult = Invoke-UiaPaletteCommand -Driver $driver -Main $main -ProcessId $appPid -Query 'search' -ExpectedNames @('Filters') -ExpectedCommandName 'Search' -ExpectedCommandId 'search.focus'
                $null = Close-UiaSearchPanelIfOpen -Driver $driver -Main $main -ProcessId $appPid
            } catch {
                $iterError = $_.Exception.Message
            }

            $iterSw.Stop()
            $elapsedMs = [Math]::Round($iterSw.Elapsed.TotalMilliseconds, 2)
            $durations.Add($elapsedMs)

            if (-not $iterError) {
                $passedCount++
                if ($i % 10 -eq 0 -or $i -eq $Iterations) {
                    Write-Host "  Iteration $i/$Iterations PASS (${elapsedMs}ms)" -ForegroundColor Green
                }
            } else {
                $failedCount++
                Write-Host "  Iteration $i/$Iterations FAIL: $iterError (${elapsedMs}ms)" -ForegroundColor Red
                if ($null -eq $firstFailure) {
                    $firstFailure = [pscustomobject]@{
                        iteration = $i
                        error = $iterError
                        exitCode = 0
                        stdoutTail = ""
                        stderrTail = ""
                    }
                }
                if (-not $ContinueOnFailure) { break }
            }
        }
    } finally {
        if ($ctx) {
            $null = Remove-UiaScenarioContext -Context $ctx -LogFn $quietLog
        }
    }
} elseif ($Scenario -eq 'core-smoke') {
    for ($i = 1; $i -le $Iterations; $i++) {
        $iterSw = [System.Diagnostics.Stopwatch]::StartNew()
        $smoke = Invoke-UiaCoreSmoke -Exe $targetExe -RepoRoot $RepoRoot -ScenarioId "core-smoke-$i"
        $iterSw.Stop()
        $elapsedMs = [Math]::Round($iterSw.Elapsed.TotalMilliseconds, 2)
        $durations.Add($elapsedMs)

        if ($smoke.Failed -eq 0) {
            $passedCount++
            Write-Host "  Iteration $i/$Iterations PASS: $($smoke.Passed)/$($smoke.Checks.Count) checks (${elapsedMs}ms)" -ForegroundColor Green
        } else {
            $failedCount++
            $failedChecks = @($smoke.Checks | Where-Object { -not $_.Pass } | ForEach-Object { "$($_.Name): $($_.Detail)" }) -join '; '
            Write-Host "  Iteration $i/$Iterations FAIL: $failedChecks (${elapsedMs}ms)" -ForegroundColor Red
            if ($null -eq $firstFailure) {
                $firstFailure = [pscustomobject]@{
                    iteration = $i
                    error = $failedChecks
                    exitCode = 0
                    stdoutTail = ""
                    stderrTail = ""
                }
            }
            if (-not $ContinueOnFailure) { break }
        }
    }
}

$endUtc = [DateTime]::UtcNow
$completed = $passedCount + $failedCount + $timeoutCount + $skippedCount

$minMs = $(if ($durations.Count -gt 0) { ($durations | Measure-Object -Minimum).Minimum } else { 0.0 })
$maxMs = $(if ($durations.Count -gt 0) { ($durations | Measure-Object -Maximum).Maximum } else { 0.0 })
$medianMs = Get-Percentile -Values $durations.ToArray() -Percentile 0.50
$p95Ms = Get-Percentile -Values $durations.ToArray() -Percentile 0.95

$disposition = 'FAIL'
if ($failedCount -gt 0) {
    $disposition = 'FAIL'
} elseif ($timeoutCount -gt 0) {
    $disposition = 'TIMEOUT'
} elseif ($passedCount -eq $Iterations) {
    $disposition = 'PASS'
}

$artifactHashes = [ordered]@{}

if ($MarkdownOutputPath) {
    $parentMd = Split-Path -Parent $MarkdownOutputPath
    if ($parentMd -and -not (Test-Path -LiteralPath $parentMd)) { New-Item -ItemType Directory -Path $parentMd -Force | Out-Null }
    $md = @"
# UIA Long-Run Certification: $Scenario

- **Category**: $testCategory
- **Disposition**: **$disposition**
- **Commit**: ``$headSha`` (tree: ``$treeSha``, exact: $exactTreeBound)
- **Requested Iterations**: $Iterations
- **Completed Iterations**: $completed
- **Passed**: $passedCount
- **Failed**: $failedCount
- **Timed Out**: $timeoutCount
- **Min / Median / P95 / Max**: ${minMs}ms / ${medianMs}ms / ${p95Ms}ms / ${maxMs}ms
"@
    Set-Content -LiteralPath $MarkdownOutputPath -Value $md -Encoding utf8NoBOM
    $mdBytes = [IO.File]::ReadAllBytes($MarkdownOutputPath)
    $mdHash = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($mdBytes)).Replace('-', '').ToLowerInvariant()
    $artifactHashes[([IO.Path]::GetFileName($MarkdownOutputPath))] = $mdHash
}

$resultObject = [ordered]@{
    schemaVersion = 1
    testId = "ffuia_$Scenario"
    category = $testCategory
    command = $commandStr
    commit = $headSha
    tree = $treeSha
    exactTreeBound = $exactTreeBound
    executableHash = $executableHash
    environment = [ordered]@{
        os = [Environment]::OSVersion.ToString()
        architecture = [Environment]::GetEnvironmentVariable('PROCESSOR_ARCHITECTURE')
        computer = "<local-host>"
        powershell = $PSVersionTable.PSVersion.ToString()
    }
    startUtc = $startUtc.ToString('o')
    endUtc = $endUtc.ToString('o')
    requestedIterations = $Iterations
    completedIterations = $completed
    passed = $passedCount
    failed = $failedCount
    timedOut = $timeoutCount
    skipped = $skippedCount
    minDurationMs = [Math]::Round($minMs, 2)
    medianDurationMs = [Math]::Round($medianMs, 2)
    p95DurationMs = [Math]::Round($p95Ms, 2)
    maxDurationMs = [Math]::Round($maxMs, 2)
    firstFailure = $firstFailure
    artifactHashes = $artifactHashes
    disposition = $disposition
}

$jsonText = $resultObject | ConvertTo-Json -Depth 10
Set-Content -LiteralPath $OutputPath -Value $jsonText -Encoding utf8NoBOM

# Authoritative sidecar checksum
$finalFileBytes = [IO.File]::ReadAllBytes($OutputPath)
$finalSha256 = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($finalFileBytes)).Replace('-', '').ToLowerInvariant()
$sidecarPath = "$OutputPath.sha256"
Set-Content -LiteralPath $sidecarPath -Value "$finalSha256  $([IO.Path]::GetFileName($OutputPath))" -Encoding utf8NoBOM

$fgColor = $(if ($disposition -eq 'PASS') { 'Green' } else { 'Red' })
Write-Host "UIA long-run completed: $passedCount/$Iterations passed. Disposition: $disposition" -ForegroundColor $fgColor
Write-Host "Timing: min=${minMs}ms, median=${medianMs}ms, p95=${p95Ms}ms, max=${maxMs}ms"
Write-Host "Artifact written: $OutputPath (SHA-256: $finalSha256)"

return [pscustomobject]$resultObject
