[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $TestTarget,

    [int] $Iterations = 25,

    [int] $TimeoutSecondsPerIteration = 60,

    [ValidateSet('debug', 'release-test')]
    [string] $Preset = 'debug',

    [ValidateSet('ipc-lifecycle', 'concurrency', 'application-lifecycle', 'uia', 'rapid-input', 'resource-stress')]
    [string] $Category = 'ipc-lifecycle',

    [string] $OutputPath,

    [string] $MarkdownOutputPath,

    [switch] $ContinueOnFailure,

    [switch] $RequireExactTree,

    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)

$ErrorActionPreference = 'Stop'

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

function Resolve-CTestExecutable {
    param([string] $RepoRoot)
    $cmd = Get-Command ctest.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($cmd) {
        $path = if ($cmd.Path) { $cmd.Path } else { $cmd.Source }
        if ($path -and (Test-Path -LiteralPath $path -PathType Leaf)) { return $path }
    }
    try {
        $toolchainModule = Join-Path $RepoRoot "verify\core\Toolchain.psm1"
        if (Test-Path -LiteralPath $toolchainModule) {
            Import-Module $toolchainModule -ErrorAction SilentlyContinue
            $tc = Find-VSToolchain
            if ($tc -and $tc.CTestExe -and (Test-Path -LiteralPath $tc.CTestExe)) {
                return $tc.CTestExe
            }
        }
    } catch { }
    return $null
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

# 2. Locate Test Executable or CTest command with discovery
$presetDir = Join-Path $RepoRoot "build\$Preset"
if (-not (Test-Path -LiteralPath $presetDir)) {
    throw "Build preset directory build/$Preset does not exist. Configure/build preset first."
}

$exeCandidates = @(Get-ChildItem -LiteralPath $presetDir -Filter "$TestTarget.exe" -Recurse -File -ErrorAction SilentlyContinue)
$targetExe = $null
$executableHash = $null
$useCtest = $false
$ctestExe = Resolve-CTestExecutable -RepoRoot $RepoRoot

if ($exeCandidates.Count -gt 0) {
    $targetExe = $exeCandidates[0].FullName
    $sha256 = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($targetExe)
    $executableHash = [BitConverter]::ToString($sha256.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
    $stream.Close()
    $commandStr = Get-SanitizedPath -FullPath $targetExe -RepoRoot $RepoRoot
} else {
    if (-not $ctestExe) {
        throw "Neither test executable '$TestTarget.exe' in build/$Preset nor ctest.exe was found."
    }

    # Discover target via ctest --show-only=json-v1
    $psiDisc = [System.Diagnostics.ProcessStartInfo]::new()
    $psiDisc.FileName = $ctestExe
    $psiDisc.ArgumentList.Add("--test-dir")
    $psiDisc.ArgumentList.Add($presetDir)
    $psiDisc.ArgumentList.Add("-R")
    $psiDisc.ArgumentList.Add("^$TestTarget$")
    $psiDisc.ArgumentList.Add("--show-only=json-v1")
    $psiDisc.WorkingDirectory = $RepoRoot
    $psiDisc.UseShellExecute = $false
    $psiDisc.RedirectStandardOutput = $true
    $psiDisc.RedirectStandardError = $true
    $psiDisc.CreateNoWindow = $true

    $pDisc = [System.Diagnostics.Process]::Start($psiDisc)
    $discOut = $pDisc.StandardOutput.ReadToEnd()
    $discErr = $pDisc.StandardError.ReadToEnd()
    $pDisc.WaitForExit()

    if ($pDisc.ExitCode -ne 0) {
        throw "INVALID_TARGET: CTest discovery failed with exit code $($pDisc.ExitCode): $discErr"
    }

    $discJson = try { $discOut | ConvertFrom-Json } catch { $null }
    $matchedTests = if ($discJson -and $discJson.tests) { @($discJson.tests) } else { @() }

    if ($matchedTests.Count -eq 0) {
        throw "INVALID_TARGET: Target '$TestTarget' matched 0 tests in build/$Preset (zero-test rejection)."
    }
    if ($matchedTests.Count -gt 1) {
        throw "FAIL_AMBIGUOUS_TARGET: Target '$TestTarget' matched $($matchedTests.Count) tests in build/$Preset. Exactly 1 expected."
    }

    $matchedTest = $matchedTests[0]
    if ($matchedTest.command -and $matchedTest.command.Count -gt 0) {
        $cmdExe = $matchedTest.command[0]
        if (Test-Path -LiteralPath $cmdExe -PathType Leaf) {
            $targetExe = $cmdExe
            $sha256 = [Security.Cryptography.SHA256]::Create()
            $stream = [IO.File]::OpenRead($targetExe)
            $executableHash = [BitConverter]::ToString($sha256.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
            $stream.Close()
            $commandStr = Get-SanitizedPath -FullPath $targetExe -RepoRoot $RepoRoot
        }
    }

    if (-not $targetExe) {
        $useCtest = $true
        $sanitizedPresetDir = Get-SanitizedPath -FullPath $presetDir -RepoRoot $RepoRoot
        $commandStr = "ctest --test-dir $sanitizedPresetDir -R ^$TestTarget$ --output-on-failure"
    }
}

# 3. Execution Setup
if (-not $OutputPath) {
    $runDir = Join-Path $RepoRoot "verify\runs\certification\long-run"
    if (-not (Test-Path -LiteralPath $runDir)) { New-Item -ItemType Directory -Path $runDir -Force | Out-Null }
    $ts = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
    $OutputPath = Join-Path $runDir "long-run-$TestTarget-$ts.json"
} else {
    $parent = Split-Path -Parent $OutputPath
    if ($parent -and -not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
}

$startUtc = [DateTime]::UtcNow
$durations = [System.Collections.Generic.List[double]]::new()
$passedCount = 0
$failedCount = 0
$timeoutCount = 0
$skippedCount = 0
$firstFailure = $null

Write-Host "Starting long-run certification: target='$TestTarget', iterations=$Iterations, preset=$Preset, timeout=${TimeoutSecondsPerIteration}s"
Write-Host "Git: head=$headSha, tree=$treeSha, exactTreeBound=$exactTreeBound"
if ($targetExe) {
    Write-Host "Direct target executable: $(Get-SanitizedPath -FullPath $targetExe -RepoRoot $RepoRoot) (hash: $executableHash)"
} else {
    Write-Host "CTest command: $commandStr"
}

for ($i = 1; $i -le $Iterations; $i++) {
    $iterStart = [System.Diagnostics.Stopwatch]::StartNew()
    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    if ($useCtest) {
        $psi.FileName = $ctestExe
        $psi.ArgumentList.Add("--test-dir")
        $psi.ArgumentList.Add($presetDir)
        $psi.ArgumentList.Add("-R")
        $psi.ArgumentList.Add("^$TestTarget$")
        $psi.ArgumentList.Add("--output-on-failure")
    } else {
        $psi.FileName = $targetExe
    }
    $psi.WorkingDirectory = $RepoRoot
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true

    $proc = [System.Diagnostics.Process]::new()
    $proc.StartInfo = $psi

    $timedOut = $false
    $exitCode = -1
    $stdout = ""
    $stderr = ""

    if (-not $proc.Start()) {
        $failedCount++
        if ($null -eq $firstFailure) {
            $firstFailure = [pscustomobject]@{
                iteration = $i
                error = "Failed to launch process"
                exitCode = -2
                stdoutTail = ""
                stderrTail = ""
            }
        }
        if (-not $ContinueOnFailure) { break }
        continue
    }

    $outTask = $proc.StandardOutput.ReadToEndAsync()
    $errTask = $proc.StandardError.ReadToEndAsync()

    if (-not $proc.WaitForExit($TimeoutSecondsPerIteration * 1000)) {
        $timedOut = $true
        try { $proc.Kill($true) } catch { }
        try { $proc.WaitForExit(3000) } catch { }
        $timeoutCount++
    } else {
        $exitCode = $proc.ExitCode
    }

    $iterStart.Stop()
    $elapsedMs = [Math]::Round($iterStart.Elapsed.TotalMilliseconds, 2)
    $durations.Add($elapsedMs)

    try { $stdout = $outTask.Result } catch { }
    try { $stderr = $errTask.Result } catch { }

    # Execution proof verification
    $hasExecutionProof = $true
    if ($useCtest) {
        if ($stdout -match 'No tests were found' -or $stderr -match 'No tests were found') {
            $hasExecutionProof = $false
            $exitCode = -3
        } elseif ($stdout -notmatch '(1/1 Test|Test\s*#\d+:|Passed)') {
            $hasExecutionProof = $false
            $exitCode = -3
        }
    }

    if ($timedOut) {
        Write-Host "Iteration $i/$Iterations TIMEOUT after ${elapsedMs}ms" -ForegroundColor Red
        if ($null -eq $firstFailure) {
            $tailOut = $(if ($stdout.Length -gt 1000) { $stdout.Substring($stdout.Length - 1000) } else { $stdout })
            $tailErr = $(if ($stderr.Length -gt 1000) { $stderr.Substring($stderr.Length - 1000) } else { $stderr })
            $firstFailure = [pscustomobject]@{
                iteration = $i
                error = "Iteration timed out after ${TimeoutSecondsPerIteration}s"
                exitCode = -1
                stdoutTail = $tailOut
                stderrTail = $tailErr
            }
        }
        if (-not $ContinueOnFailure) { break }
    } elseif ($exitCode -eq 0 -and $hasExecutionProof) {
        $passedCount++
    } else {
        $failedCount++
        $failReason = if (-not $hasExecutionProof) { "CTest reported zero tests or missing execution proof" } else { "Process exited with code $exitCode" }
        Write-Host "Iteration $i/$Iterations FAILED ($failReason) in ${elapsedMs}ms" -ForegroundColor Red
        if ($null -eq $firstFailure) {
            $tailOut = $(if ($stdout.Length -gt 1000) { $stdout.Substring($stdout.Length - 1000) } else { $stdout })
            $tailErr = $(if ($stderr.Length -gt 1000) { $stderr.Substring($stderr.Length - 1000) } else { $stderr })
            $firstFailure = [pscustomobject]@{
                iteration = $i
                error = $failReason
                exitCode = $exitCode
                stdoutTail = $tailOut
                stderrTail = $tailErr
            }
        }
        if (-not $ContinueOnFailure) { break }
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
# Long-Run Certification: $TestTarget

- **Category**: $Category
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
    testId = $TestTarget
    category = $Category
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

# Authoritative sidecar checksum of final JSON file bytes
$finalFileBytes = [IO.File]::ReadAllBytes($OutputPath)
$finalSha256 = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($finalFileBytes)).Replace('-', '').ToLowerInvariant()
$sidecarPath = "$OutputPath.sha256"
Set-Content -LiteralPath $sidecarPath -Value "$finalSha256  $([IO.Path]::GetFileName($OutputPath))" -Encoding utf8NoBOM

$fgColor = $(if ($disposition -eq 'PASS') { 'Green' } else { 'Red' })
Write-Host "Completed: $passedCount passed, $failedCount failed, $timeoutCount timed out out of $completed completed ($Iterations requested). Disposition: $disposition" -ForegroundColor $fgColor
Write-Host "Timing: min=${minMs}ms, median=${medianMs}ms, p95=${p95Ms}ms, max=${maxMs}ms"
Write-Host "Artifact written: $OutputPath (SHA-256: $finalSha256)"

return [pscustomobject]$resultObject
