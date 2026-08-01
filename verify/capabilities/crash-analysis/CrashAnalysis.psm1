<#
    Crash Analysis capability (task 3.2 / D13). It consumes run-local crash-context
    records and dumps, preserves them in its own artifact subtree, and emits a stable
    structured verdict for reports and the repair loop. CDB is optional: without it,
    the capability deliberately degrades to a dump-only result.
#>

function Test-CrashAnalysisCapabilityAvailability {
    param([Parameter(Mandatory)] $Fingerprint)
    if (-not $IsWindows) {
        return [pscustomobject]@{ Available = $false; Reason = 'windows-required'; RequiredContext = [pscustomobject]@{ needs = 'Windows crash dumps' } }
    }
    return [pscustomobject]@{ Available = $true; Reason = $null; RequiredContext = $null }
}

function Get-CrashAnalysisCapabilityDiagnostics {
    return @('wer-dump', 'crash-context', 'debugger-output', 'faulting-thread', 'symbolized-stack', 'classification-bucket', 'reproduction-context')
}

function Get-RunRelativePath {
    param([Parameter(Mandatory)] [string] $RunPath, [Parameter(Mandatory)] [string] $Path)
    return ([IO.Path]::GetRelativePath($RunPath, $Path) -replace '\\', '/')
}

function Find-CrashDebugger {
    $command = Get-Command cdb.exe, cdb -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $command) { return $null }
    $path = if ($command.Path) { $command.Path } else { $command.Source }
    [pscustomobject]@{
        Path = $path
        Version = try { (Get-Item -LiteralPath $path).VersionInfo.ProductVersion } catch { $null }
    }
}

function Invoke-NativeProcess {
    param(
        [Parameter(Mandatory)] [string] $FilePath,
        [Parameter(Mandatory)] [string[]] $ArgumentList,
        [ValidateRange(1, 900)] [int] $TimeoutSeconds = 120
    )

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $FilePath
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($argument in $ArgumentList) { $startInfo.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) { throw "Failed to start '$FilePath'." }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill($true)
        $process.WaitForExit()
        return [pscustomobject]@{ ExitCode = $process.ExitCode; TimedOut = $true; Output = $stdoutTask.Result + $stderrTask.Result }
    }
    [pscustomobject]@{ ExitCode = $process.ExitCode; TimedOut = $false; Output = $stdoutTask.Result + $stderrTask.Result }
}

function Get-SymbolPath {
    param([string] $RepoRoot)
    $paths = [System.Collections.Generic.List[string]]::new()
    foreach ($relative in @('build\debug', 'build\release', 'build\analyze')) {
        if ($RepoRoot) {
            $candidate = Join-Path $RepoRoot $relative
            if (Test-Path -LiteralPath $candidate) { $paths.Add($candidate) }
        }
    }
    if ($env:_NT_SYMBOL_PATH) { $paths.Add($env:_NT_SYMBOL_PATH) }
    return $paths -join ';'
}

function Get-DebuggerAnalysis {
    param(
        [Parameter(Mandatory)] [string] $DumpPath,
        [Parameter(Mandatory)] $Debugger,
        [string] $SymbolPath
    )

    $arguments = @('-z', $DumpPath, '-lines')
    if ($SymbolPath) { $arguments += @('-y', $SymbolPath) }
    $arguments += @('-c', '!analyze -v; .ecxr; kv; q')
    $result = Invoke-NativeProcess -FilePath $Debugger.Path -ArgumentList $arguments
    $output = $result.Output
    $exception = [regex]::Match($output, '(?im)^\s*ExceptionCode:\s*(?:0x)?([0-9a-f]+)').Groups[1].Value
    if (-not $exception) { $exception = [regex]::Match($output, '(?im)^\s*Exception code:\s*(?:0x)?([0-9a-f]+)').Groups[1].Value }
    $module = [regex]::Match($output, '(?im)^\s*(?:MODULE_NAME|IMAGE_NAME):\s*(\S+)').Groups[1].Value
    $thread = [regex]::Match($output, '(?im)^\s*FAULTING_THREAD:\s*([0-9a-f`]+)').Groups[1].Value
    $frames = @()
    foreach ($line in ($output -split "`r?`n")) {
        $match = [regex]::Match($line, '^\s*[0-9a-f`]+\s+[0-9a-f`]+\s+([^\s+]+![^\s+]+)')
        if ($match.Success) {
            $frame = $match.Groups[1].Value -replace '\+0x[0-9a-f]+$', '' -replace '<.*?>', '<>'
            if ($frames -notcontains $frame) { $frames += $frame }
            if ($frames.Count -ge 10) { break }
        }
    }
    [pscustomobject]@{
        ExitCode = $result.ExitCode
        TimedOut = $result.TimedOut
        Output = $output
        ExceptionCode = if ($exception) { "0x$($exception.ToUpperInvariant())" } else { $null }
        FaultingModule = if ($module) { $module } else { $null }
        FaultingThread = if ($thread) { $thread } else { $null }
        Frames = $frames
    }
}

function Get-CrashBucket {
    param([string] $Module, [string] $ExceptionCode, [string[]] $Frames)
    $parts = @(
        if ($Module) { $Module.ToLowerInvariant() } else { 'module-unknown' }
        if ($ExceptionCode) { $ExceptionCode.ToLowerInvariant() } else { 'exception-unknown' }
    ) + @($Frames | Select-Object -First 5 | ForEach-Object { $_.ToLowerInvariant() })
    $signature = $parts -join '|'
    $bytes = [Text.Encoding]::UTF8.GetBytes($signature)
    $hash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).Substring(0, 16).ToLowerInvariant()
    [pscustomobject]@{ Id = "crash-$hash"; Signature = $signature }
}

function Get-CrashInputs {
    param([Parameter(Mandatory)] [pscustomobject] $RunContext)

    $ownRoot = Join-Path $RunContext.ArtifactsRoot 'crash-analysis'
    $contexts = @(Get-ChildItem -LiteralPath $RunContext.ArtifactsRoot -Filter 'crash-context-*.json' -File -Recurse -ErrorAction SilentlyContinue |
        Where-Object { -not $_.FullName.StartsWith($ownRoot, [StringComparison]::OrdinalIgnoreCase) })
    $dumps = @(Get-ChildItem -LiteralPath $RunContext.ArtifactsRoot -Filter '*.dmp' -File -Recurse -ErrorAction SilentlyContinue |
        Where-Object { -not $_.FullName.StartsWith($ownRoot, [StringComparison]::OrdinalIgnoreCase) })
    [pscustomobject]@{ Contexts = $contexts; Dumps = $dumps }
}

function Resolve-CrashInputPath {
    param([Parameter(Mandatory)] [pscustomobject] $RunContext, [string] $Path)
    if (-not $Path) { return $null }
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $RunContext.RunPath ($Path -replace '/', '\')))
}

function Invoke-CrashAnalysisCapability {
    param($RunContext, $Fingerprint, [string] $ArtifactsDir, [hashtable] $Options)

    $inputs = Get-CrashInputs -RunContext $RunContext
    if ($inputs.Contexts.Count -eq 0 -and $inputs.Dumps.Count -eq 0) {
        return [pscustomobject]@{
            Status = 'SKIPPED'; Reason = 'no-crash-observed'; Summary = 'No crash context or dump was present in the run tree.'; Artifacts = @()
            SubResults = @([pscustomobject]@{ id = 'crash-observation'; tier = 0; status = 'SKIPPED'; reason = 'no-crash-observed'; requiredContext = $null; durationMs = 0; detail = 'Crash Analysis runs only when another capability records a crash or dump.'; diagnostics = @() })
        }
    }

    $dumpDir = Join-Path $ArtifactsDir 'dumps'
    $reproductionDir = Join-Path $ArtifactsDir 'reproduction'
    New-Item -ItemType Directory -Path $dumpDir, $reproductionDir -Force | Out-Null
    $debugger = Find-CrashDebugger
    $symbolPath = Get-SymbolPath -RepoRoot $Options.RepoRoot
    $verdicts = @()
    $artifacts = @()
    $processedDumps = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)

    $candidates = @()
    foreach ($contextFile in $inputs.Contexts) {
        try {
            $context = Get-Content -LiteralPath $contextFile.FullName -Raw | ConvertFrom-Json
            $contextDumps = @($context.dumpPaths | Where-Object { $_ })
            if ($contextDumps.Count -eq 0) { $contextDumps = @($null) }
            foreach ($dump in $contextDumps) { $candidates += [pscustomobject]@{ Context = $context; ContextPath = $contextFile.FullName; DumpPath = $dump } }
        } catch {
            $candidates += [pscustomobject]@{ Context = $null; ContextPath = $contextFile.FullName; DumpPath = $null; ParseError = $_.Exception.Message }
        }
    }
    foreach ($dump in $inputs.Dumps) {
        if (-not ($candidates | Where-Object { $_.DumpPath -and (Resolve-CrashInputPath -RunContext $RunContext -Path $_.DumpPath) -eq $dump.FullName })) {
            $candidates += [pscustomobject]@{ Context = $null; ContextPath = $null; DumpPath = $dump.FullName }
        }
    }

    $ordinal = 0
    foreach ($candidate in $candidates) {
        $ordinal++
        $context = $candidate.Context
        $sourceDump = Resolve-CrashInputPath -RunContext $RunContext -Path ([string]$candidate.DumpPath)
        $preservedDump = $null
        if ($sourceDump -and (Test-Path -LiteralPath $sourceDump)) {
            $dumpName = '{0:D3}-{1}' -f $ordinal, [IO.Path]::GetFileName($sourceDump)
            $preservedDump = Join-Path $dumpDir $dumpName
            Copy-Item -LiteralPath $sourceDump -Destination $preservedDump -Force
            $processedDumps.Add($sourceDump) | Out-Null
            $artifacts += [pscustomobject]@{ path = Get-RunRelativePath -RunPath $RunContext.RunPath -Path $preservedDump; type = 'crash-dump' }
        }

        $reproductionPath = Join-Path $reproductionDir ('crash-{0:D3}.json' -f $ordinal)
        [pscustomobject]@{
            sourceContextPath = if ($candidate.ContextPath) { Get-RunRelativePath -RunPath $RunContext.RunPath -Path $candidate.ContextPath } else { $null }
            executable = $context.executable
            processId = $context.processId
            exitCode = $context.exitCode
            sourceCapability = $context.sourceCapability
            phase = $context.phase
            inputs = $context.inputs
            environmentFingerprint = $Fingerprint
        } | ConvertTo-Json -Depth 15 | Set-Content -LiteralPath $reproductionPath -Encoding utf8
        $artifacts += [pscustomobject]@{ path = Get-RunRelativePath -RunPath $RunContext.RunPath -Path $reproductionPath; type = 'crash-reproduction-context' }

        $analysis = $null
        $debuggerOutputPath = $null
        if ($preservedDump -and $debugger) {
            $analysis = Get-DebuggerAnalysis -DumpPath $preservedDump -Debugger $debugger -SymbolPath $symbolPath
            $debuggerOutputPath = Join-Path $ArtifactsDir ('debugger-{0:D3}.txt' -f $ordinal)
            $analysis.Output | Set-Content -LiteralPath $debuggerOutputPath -Encoding utf8
            $artifacts += [pscustomobject]@{ path = Get-RunRelativePath -RunPath $RunContext.RunPath -Path $debuggerOutputPath; type = 'debugger-output' }
        }

        $exceptionCode = if ($analysis -and $analysis.ExceptionCode) { $analysis.ExceptionCode } elseif ($context.exceptionCode) { [string]$context.exceptionCode } else { $null }
        $module = if ($analysis -and $analysis.FaultingModule) { $analysis.FaultingModule } elseif ($context.executable) { [IO.Path]::GetFileName([string]$context.executable) } elseif ($preservedDump) { [IO.Path]::GetFileNameWithoutExtension($preservedDump) } else { $null }
        $frames = if ($analysis) { @($analysis.Frames) } else { @() }
        $bucket = Get-CrashBucket -Module $module -ExceptionCode $exceptionCode -Frames $frames
        $symbolizationStatus = if (-not $preservedDump) { 'SKIPPED' } elseif (-not $debugger) { 'SKIPPED' } elseif ($analysis.TimedOut) { 'SKIPPED' } elseif ($frames.Count -eq 0) { 'SKIPPED' } else { 'PASS' }
        $symbolizationReason = if (-not $preservedDump) { 'dump-not-captured' } elseif (-not $debugger) { 'postmortem-debugger-not-found' } elseif ($analysis.TimedOut) { 'debugger-timeout' } elseif ($frames.Count -eq 0) { 'symbols-or-stack-unavailable' } else { $null }
        $analysisStatus = if (-not $preservedDump) { 'CAPTURE_MISSING' } elseif ($symbolizationStatus -eq 'PASS') { 'COMPLETE' } else { 'DUMP_ONLY' }

        $verdicts += [pscustomobject]@{
            schemaVersion = '1.0.0'
            crashObserved = $true
            analysisStatus = $analysisStatus
            executable = if ($context.executable) { $context.executable } else { $module }
            processId = $context.processId
            faultingThread = if ($analysis) { $analysis.FaultingThread } else { $null }
            exceptionCode = $exceptionCode
            faultingModule = $module
            normalizedTopFrames = @($frames | Select-Object -First 5)
            bucket = [pscustomobject]@{ id = $bucket.Id; signature = $bucket.Signature }
            symbolization = [pscustomobject]@{ status = $symbolizationStatus; reason = $symbolizationReason; debuggerPath = if ($debugger) { $debugger.Path } else { $null }; debuggerVersion = if ($debugger) { $debugger.Version } else { $null }; symbolPath = $symbolPath }
            artifacts = [pscustomobject]@{
                dump = if ($preservedDump) { Get-RunRelativePath -RunPath $RunContext.RunPath -Path $preservedDump } else { $null }
                debuggerOutput = if ($debuggerOutputPath) { Get-RunRelativePath -RunPath $RunContext.RunPath -Path $debuggerOutputPath } else { $null }
                reproductionContext = Get-RunRelativePath -RunPath $RunContext.RunPath -Path $reproductionPath
            }
            source = [pscustomobject]@{ capability = $context.sourceCapability; phase = $context.phase }
            diagnostic = if ($candidate.ParseError) { "Crash context could not be parsed: $($candidate.ParseError)" } elseif (-not $preservedDump) { 'A crash was recorded, but no dump was captured. Confirm elevated WER capture or install ProcDump.' } else { $null }
        }
    }

    $analysisPath = Join-Path $ArtifactsDir 'crash-analysis.json'
    [pscustomobject]@{
        schemaVersion = '1.0.0'
        generatedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
        tool = [pscustomobject]@{ id = 'cdb'; path = if ($debugger) { $debugger.Path } else { $null }; version = if ($debugger) { $debugger.Version } else { $null } }
        verdicts = $verdicts
    } | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $analysisPath -Encoding utf8
    $artifacts = @([pscustomobject]@{ path = Get-RunRelativePath -RunPath $RunContext.RunPath -Path $analysisPath; type = 'crash-analysis-verdict' }) + $artifacts
    $toolVersionsPath = Join-Path $ArtifactsDir 'tool-version-metadata.json'
    [pscustomobject]@{
        tools = @([pscustomobject]@{ id = 'cdb'; version = if ($debugger) { $debugger.Version } else { $null }; path = if ($debugger) { $debugger.Path } else { $null }; status = if ($debugger) { 'available' } else { 'unavailable' } })
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $toolVersionsPath -Encoding utf8
    $artifacts += [pscustomobject]@{ path = Get-RunRelativePath -RunPath $RunContext.RunPath -Path $toolVersionsPath; type = 'tool-version-metadata' }

    $captureMissing = @($verdicts | Where-Object { $_.analysisStatus -eq 'CAPTURE_MISSING' }).Count
    $complete = @($verdicts | Where-Object { $_.analysisStatus -eq 'COMPLETE' }).Count
    $dumpOnly = @($verdicts | Where-Object { $_.analysisStatus -eq 'DUMP_ONLY' }).Count
    $status = if ($captureMissing -gt 0) { 'FAIL' } else { 'PASS' }
    $reason = if ($captureMissing -gt 0) { 'crash-dump-not-captured' } else { $null }
    $subResults = @($verdicts | ForEach-Object {
        [pscustomobject]@{
            id = $_.bucket.id; tier = 0; status = if ($_.analysisStatus -eq 'CAPTURE_MISSING') { 'FAIL' } else { 'PASS' }
            reason = if ($_.analysisStatus -eq 'CAPTURE_MISSING') { 'dump-not-captured' } else { $null }; requiredContext = $null; durationMs = 0
            detail = "executable=$($_.executable); exception=$($_.exceptionCode); analysis=$($_.analysisStatus); symbolization=$($_.symbolization.status)"
            diagnostics = @($_.diagnostic | Where-Object { $_ })
        }
    })
    return [pscustomobject]@{
        Status = $status; Reason = $reason; Summary = "$($verdicts.Count) crash(es): $complete complete, $dumpOnly dump-only, $captureMissing missing dump"
        Artifacts = $artifacts; SubResults = $subResults
    }
}

Export-ModuleMember -Function Test-CrashAnalysisCapabilityAvailability, Invoke-CrashAnalysisCapability, Get-CrashAnalysisCapabilityDiagnostics
