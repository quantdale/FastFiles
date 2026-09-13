<#
    Release-readiness composition layer.

    This module deliberately orchestrates the existing verifier and release
    tools. It does not replace the capability runner, infer test results from
    artifact presence, or turn an unavailable privileged prerequisite into a
    pass. Child run trees remain the authoritative detailed evidence; the
    composition result is the compact release-facing index over that evidence.
#>

Import-Module (Join-Path $PSScriptRoot 'ReleaseCommon.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $PSScriptRoot '..\core\Toolchain.psm1') -Force -Global
Import-Module (Join-Path $PSScriptRoot '..\core\Fingerprint.psm1') -Force -Global

$script:ReadinessWorkingDirectory = $null

function New-ReadinessGate {
    param(
        [Parameter(Mandatory)] [string] $Id,
        [Parameter(Mandatory)] [ValidateSet('PASS', 'FAIL', 'SKIPPED', 'REQUIRED-BUT-UNAVAILABLE')] [string] $Status,
        [string] $Reason,
        [double] $DurationMs = 0,
        [bool] $Required = $true,
        [string] $Detail,
        [array] $Artifacts = @(),
        [array] $Diagnostics = @()
    )

    [pscustomobject]@{
        id = $Id
        status = $Status
        reason = $Reason
        required = $Required
        durationMs = [math]::Round($DurationMs, 0)
        detail = $Detail
        artifacts = if ($null -eq $Artifacts) { @() } else { @($Artifacts) }
        diagnostics = if ($null -eq $Diagnostics) { @() } else { @($Diagnostics) }
    }
}

function Get-ReadinessOutputTail {
    param([string] $Text, [int] $MaximumLength = 3000)
    if ([string]::IsNullOrWhiteSpace($Text)) { return $null }
    $value = $Text.Trim()
    if ($value.Length -le $MaximumLength) { return $value }
    return '…' + $value.Substring($value.Length - $MaximumLength)
}

function Ensure-ReadinessDirectory {
    param([Parameter(Mandatory)] [string] $Path)
    New-Item -ItemType Directory -Path $Path -Force | Out-Null
    return [IO.Path]::GetFullPath($Path)
}

function Invoke-ReadinessProcess {
    param(
        [Parameter(Mandatory)] [string] $FilePath,
        [Parameter(Mandatory)] [string[]] $Arguments,
        [Parameter(Mandatory)] [string] $LogPath,
        [int] $TimeoutSeconds = 1800
    )

    $started = Get-Date
    $psi = [Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $FilePath
    $psi.WorkingDirectory = if ($script:ReadinessWorkingDirectory) { $script:ReadinessWorkingDirectory } else { (Get-Location).Path }
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    foreach ($argument in $Arguments) {
        [void]$psi.ArgumentList.Add([string]$argument)
    }

    $stdout = ''
    $stderr = ''
    $exitCode = -1
    $timedOut = $false
    $startError = $null
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $psi
    try {
        if (-not $process.Start()) {
            throw "Process did not start: $FilePath"
        }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            $timedOut = $true
            try { $process.Kill($true) } catch { try { $process.Kill() } catch { } }
            $process.WaitForExit()
        }
        $stdout = $stdoutTask.GetAwaiter().GetResult()
        $stderr = $stderrTask.GetAwaiter().GetResult()
        $exitCode = if ($timedOut) { -2 } else { $process.ExitCode }
    } catch {
        $startError = $_.Exception.Message
    } finally {
        $process.Dispose()
    }

    $finished = Get-Date
    $combined = @($stdout, $stderr) -join [Environment]::NewLine
    $logParent = Split-Path -Parent $LogPath
    Ensure-ReadinessDirectory -Path $logParent | Out-Null
    [IO.File]::WriteAllText($LogPath, $combined)
    [pscustomobject]@{
        status = if ($startError -or $timedOut -or $exitCode -ne 0) { 'FAIL' } else { 'PASS' }
        exitCode = $exitCode
        timedOut = $timedOut
        startError = $startError
        stdout = $stdout
        stderr = $stderr
        combined = $combined
        durationMs = [math]::Round(($finished - $started).TotalMilliseconds, 0)
        logPath = $LogPath
    }
}

function Invoke-ReadinessPowerShell {
    param(
        [Parameter(Mandatory)] [string] $ScriptPath,
        [Parameter(Mandatory)] [string[]] $Arguments,
        [Parameter(Mandatory)] [string] $LogPath,
        [int] $TimeoutSeconds = 1800
    )
    $pwsh = (Get-Command pwsh -ErrorAction Stop).Source
    Invoke-ReadinessProcess -FilePath $pwsh `
        -Arguments (@('-NoProfile', '-NonInteractive', '-File', $ScriptPath) + $Arguments) `
        -LogPath $LogPath -TimeoutSeconds $TimeoutSeconds
}

function Get-ProcessGateStatus {
    param($ProcessResult)
    if ($ProcessResult.status -eq 'PASS') { return 'PASS' }
    if ($ProcessResult.timedOut) { return 'FAIL' }
    if ($ProcessResult.startError) { return 'REQUIRED-BUT-UNAVAILABLE' }
    if ($ProcessResult.exitCode -eq 2) { return 'SKIPPED' }
    return 'FAIL'
}

function New-ProcessGate {
    param(
        [Parameter(Mandatory)] [string] $Id,
        [Parameter(Mandatory)] $ProcessResult,
        [bool] $Required = $true,
        [string] $Detail,
        [string] $Reason
    )
    $status = Get-ProcessGateStatus -ProcessResult $ProcessResult
    $effectiveReason = if ($Reason) { $Reason } elseif ($ProcessResult.timedOut) { 'timeout' } elseif ($ProcessResult.startError) { 'process-unavailable' } elseif ($ProcessResult.exitCode -ne 0) { 'process-exited-nonzero' } else { $null }
    $effectiveDetail = if ($Detail) { $Detail } else { "exitCode=$($ProcessResult.exitCode)" }
    if ($ProcessResult.startError) { $effectiveDetail += "; $($ProcessResult.startError)" }
    $tail = Get-ReadinessOutputTail -Text $ProcessResult.combined
    if ($tail) { $effectiveDetail += "; outputTail=$tail" }
    New-ReadinessGate -Id $Id -Status $status -Reason $effectiveReason `
        -DurationMs $ProcessResult.durationMs -Required $Required -Detail $effectiveDetail `
        -Artifacts @($ProcessResult.logPath)
}

function Get-VerificationRunPath {
    param([string] $Output)
    $match = [regex]::Match($Output, '(?m)^Run tree:\s*(?<path>.+?)\s*$')
    if (-not $match.Success) { return $null }
    return $match.Groups['path'].Value.Trim()
}

function Add-VerificationResultGates {
    param(
        [Parameter(Mandatory)] [System.Collections.Generic.List[object]] $Gates,
        [Parameter(Mandatory)] [string] $Prefix,
        [string] $RunPath
    )
    if ([string]::IsNullOrWhiteSpace($RunPath) -or -not (Test-Path -LiteralPath $RunPath -PathType Container)) {
        return
    }
    $indexPath = Join-Path $RunPath 'index.json'
    if (-not (Test-Path -LiteralPath $indexPath -PathType Leaf)) { return }
    $index = Get-Content -LiteralPath $indexPath -Raw | ConvertFrom-Json
    foreach ($entry in @($index.capabilities)) {
        $resultPath = Join-Path $RunPath ([string]$entry.resultPath -replace '/', '\')
        $envelope = if (Test-Path -LiteralPath $resultPath -PathType Leaf) {
            Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
        } else { $null }
        $detail = if ($envelope) { $envelope.summary } else { "result=$resultPath" }
        $capabilityGate = New-ReadinessGate -Id "$Prefix/$($entry.capabilityId)" `
            -Status ([string]$entry.status) -Reason ([string]$entry.reason) -DurationMs ([double]$entry.durationMs) `
            -Required $false -Detail $detail -Artifacts @($resultPath)
        [void]$Gates.Add($capabilityGate)
        if ($envelope) {
            foreach ($sub in @($envelope.subResults)) {
                $subStatus = [string]$sub.status
                if ($subStatus -notin @('PASS', 'FAIL', 'SKIPPED')) { continue }
                $subGate = New-ReadinessGate -Id "$Prefix/$($entry.capabilityId)/$($sub.id)" `
                    -Status $subStatus -Reason ([string]$sub.reason) -DurationMs ([double]$sub.durationMs) `
                    -Required $false -Detail ([string]$sub.detail) -Artifacts @()
                [void]$Gates.Add($subGate)
            }
        }
    }
}

function Invoke-VerificationCapabilityGate {
    param(
        [Parameter(Mandatory)] [string] $VerifyScript,
        [Parameter(Mandatory)] [string] $CapabilityList,
        [Parameter(Mandatory)] [string] $Id,
        [Parameter(Mandatory)] [string] $LogPath,
        [bool] $Required = $false,
        [string] $Change = 'release-readiness',
        [switch] $Elevated
    )
    $args = @('-Verb', 'run', '-Change', $Change, '-Capability', $CapabilityList, '-Format', 'human')
    if ($Elevated) { $args += '-Elevate' }
    $processResult = Invoke-ReadinessPowerShell -ScriptPath $VerifyScript -Arguments $args -LogPath $LogPath
    $runPath = Get-VerificationRunPath -Output $processResult.combined
    $gate = New-ProcessGate -Id $Id -ProcessResult $processResult -Required $Required `
        -Detail "capabilities=$CapabilityList; runPath=$runPath"
    [pscustomobject]@{ Gate = $gate; RunPath = $runPath; Process = $processResult }
}

function Invoke-DirectCMakeAsanGate {
    param(
        [Parameter(Mandatory)] [string] $RepoRoot,
        [Parameter(Mandatory)] [string] $LogDirectory,
        [Parameter(Mandatory)] [string] $CMakePath,
        [Parameter(Mandatory)] [string] $CTestPath
    )
    $configure = Invoke-ReadinessProcess -FilePath $CMakePath -Arguments @('--preset', 'asan') `
        -LogPath (Join-Path $LogDirectory 'asan-configure.log') -TimeoutSeconds 1800
    if ($configure.status -ne 'PASS') {
        return New-ReadinessGate -Id 'asan-protocol-ipc' -Status 'FAIL' -Reason 'asan-configure-failed' `
            -DurationMs $configure.durationMs -Required $true -Detail 'ASAN configure failed' `
            -Artifacts @($configure.logPath)
    }
    $build = Invoke-ReadinessProcess -FilePath $CMakePath -Arguments @('--build', '--preset', 'asan', '--target', 'ffprotocol_fuzz_tests', 'ffipc_framing_tests', 'ffipc_listener_lifecycle_tests') `
        -LogPath (Join-Path $LogDirectory 'asan-build.log') -TimeoutSeconds 1800
    if ($build.status -ne 'PASS') {
        return New-ReadinessGate -Id 'asan-protocol-ipc' -Status 'FAIL' -Reason 'asan-build-failed' `
            -DurationMs ($configure.durationMs + $build.durationMs) -Required $true -Detail 'ASAN protocol/IPC fuzz target build failed' `
            -Artifacts @($configure.logPath, $build.logPath)
    }
    $tests = Invoke-ReadinessProcess -FilePath $CTestPath `
        -Arguments @('--test-dir', (Join-Path $RepoRoot 'build\asan'), '-R', '^(ffprotocol_fuzz_tests|ffipc_framing_tests|ffipc_listener_lifecycle_tests)$', '--output-on-failure') `
        -LogPath (Join-Path $LogDirectory 'asan-tests.log') -TimeoutSeconds 600
    $asanReason = if ($tests.status -eq 'PASS') { $null } else { 'asan-fuzz-tests-failed' }
    New-ReadinessGate -Id 'asan-protocol-ipc' -Status (Get-ProcessGateStatus -ProcessResult $tests) `
        -Reason $asanReason `
        -DurationMs ($configure.durationMs + $build.durationMs + $tests.durationMs) -Required $true `
        -Detail 'MSVC AddressSanitizer protocol, IPC framing, and listener lifecycle targets' `
        -Artifacts @($configure.logPath, $build.logPath, $tests.logPath)
}

function Get-ReadinessGitState {
    param([Parameter(Mandatory)] [string] $RepoRoot)
    $sha = ((& git -C $RepoRoot rev-parse HEAD 2>&1) | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) { throw "Unable to resolve HEAD: $sha" }
    $branch = ((& git -C $RepoRoot branch --show-current 2>&1) | Out-String).Trim()
    $statusLines = @(git -C $RepoRoot status --porcelain=v1 2>&1)
    if ($LASTEXITCODE -ne 0) { throw 'Unable to inspect git status.' }
    [pscustomobject]@{
        sha = $sha
        branch = $branch
        dirty = $statusLines.Count -gt 0
        changedPaths = @($statusLines | Select-Object -First 40)
    }
}

function Get-ReadinessVersion {
    param([Parameter(Mandatory)] [string] $RepoRoot)
    $path = Join-Path $RepoRoot 'VERSION'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $null }
    return (Get-Content -LiteralPath $path -Raw).Trim()
}

function Get-OverallReadinessStatus {
    param([Parameter(Mandatory)] [array] $Gates)
    if (@($Gates | Where-Object status -eq 'FAIL').Count -gt 0) { return 'FAIL' }
    if (@($Gates | Where-Object status -eq 'REQUIRED-BUT-UNAVAILABLE').Count -gt 0) { return 'REQUIRED-BUT-UNAVAILABLE' }
    if (@($Gates | Where-Object status -eq 'SKIPPED').Count -gt 0) { return 'SKIPPED' }
    return 'PASS'
}

function Invoke-ReleaseReadinessComposition {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RepoRoot,
        [Parameter(Mandatory)] [string] $VerifyRoot,
        [Parameter(Mandatory)] [string] $OutputDirectory,
        [string] $ArtifactRoot,
        [switch] $IncludePrivileged,
        [switch] $IncludeInteractive
    )

    $repoRootFull = [IO.Path]::GetFullPath($RepoRoot).TrimEnd('\')
    $script:ReadinessWorkingDirectory = $repoRootFull
    $outputRoot = Ensure-ReadinessDirectory -Path $OutputDirectory
    $logDirectory = Ensure-ReadinessDirectory -Path (Join-Path $outputRoot 'logs')
    $gates = [System.Collections.Generic.List[object]]::new()
    $gitState = Get-ReadinessGitState -RepoRoot $repoRootFull
    $version = Get-ReadinessVersion -RepoRoot $repoRootFull
    $started = Get-Date

    $cleanStatus = if ($gitState.dirty) { 'FAIL' } else { 'PASS' }
    $cleanReason = if ($gitState.dirty) { 'dirty-worktree' } else { $null }
    $cleanDetail = if ($gitState.dirty) { "changedPaths=$($gitState.changedPaths -join ', ')" } else { 'tracked worktree is clean' }
    [void]$gates.Add((New-ReadinessGate -Id 'clean-worktree' -Status $cleanStatus `
        -Reason $cleanReason -Required $true -Detail $cleanDetail))
    $diffCheck = Invoke-ReadinessProcess -FilePath 'git.exe' -Arguments @('-C', $repoRootFull, 'diff', '--check') `
        -LogPath (Join-Path $logDirectory 'git-diff-check.log') -TimeoutSeconds 120
    [void]$gates.Add((New-ProcessGate -Id 'git-diff-check' -ProcessResult $diffCheck -Required $true -Detail 'git diff --check'))

    $versionStatus = if ($version -match '^\d+\.\d+\.\d+$') { 'PASS' } else { 'FAIL' }
    $versionReason = if ($versionStatus -eq 'PASS') { $null } else { 'version-file-invalid-or-missing' }
    [void]$gates.Add((New-ReadinessGate -Id 'version-consistency' -Status $versionStatus -Reason $versionReason `
        -Required $true -Detail "VERSION=$version"))

    $sourceMarker = Invoke-ReadinessPowerShell -ScriptPath (Join-Path $VerifyRoot 'check_source_markers.ps1') `
        -Arguments @('-RepoRoot', $repoRootFull) -LogPath (Join-Path $logDirectory 'source-markers.log') -TimeoutSeconds 120
    [void]$gates.Add((New-ProcessGate -Id 'source-marker-policy' -ProcessResult $sourceMarker -Required $true -Detail 'production source marker policy'))
    $diagnosticPolicy = Invoke-ReadinessPowerShell -ScriptPath (Join-Path $VerifyRoot 'check_diagnostic_policy.ps1') `
        -Arguments @('-RepoRoot', $repoRootFull) -LogPath (Join-Path $logDirectory 'diagnostic-policy.log') -TimeoutSeconds 120
    [void]$gates.Add((New-ProcessGate -Id 'diagnostic-policy' -ProcessResult $diagnosticPolicy -Required $true -Detail 'diagnostic unsigned-bypass policy'))

    $openspec = Get-Command openspec -ErrorAction SilentlyContinue
    if ($openspec -and $openspec.CommandType -eq 'ExternalScript') {
        $openspecResult = Invoke-ReadinessPowerShell -ScriptPath $openspec.Source `
            -Arguments @('validate', '--all', '--strict', '--no-interactive') `
            -LogPath (Join-Path $logDirectory 'openspec.log') -TimeoutSeconds 600
        [void]$gates.Add((New-ProcessGate -Id 'openspec-strict-validation' -ProcessResult $openspecResult -Required $true `
            -Detail 'openspec validate --all --strict --no-interactive'))
    } else {
        [void]$gates.Add((New-ReadinessGate -Id 'openspec-strict-validation' -Status 'REQUIRED-BUT-UNAVAILABLE' `
            -Reason 'openspec-cli-not-found' -Required $true -Detail 'openspec CLI is not installed or resolvable'))
    }

    $verifyScript = Join-Path $VerifyRoot 'verify.ps1'
    $buildRun = Invoke-VerificationCapabilityGate -VerifyScript $verifyScript `
        -CapabilityList 'windows-build-validation' -Id 'debug-release-test-build-and-ctest' `
        -LogPath (Join-Path $logDirectory 'build-capability.log') -Required $true
    [void]$gates.Add($buildRun.Gate)
    Add-VerificationResultGates -Gates $gates -Prefix 'build' -RunPath $buildRun.RunPath

    $protocolRun = Invoke-VerificationCapabilityGate -VerifyScript $verifyScript `
        -CapabilityList 'windows-protocol-robustness' -Id 'protocol-robustness' `
        -LogPath (Join-Path $logDirectory 'protocol-robustness.log') -Required $true
    [void]$gates.Add($protocolRun.Gate)
    Add-VerificationResultGates -Gates $gates -Prefix 'protocol' -RunPath $protocolRun.RunPath

    $runtimeCapabilities = 'windows-filesystem-validation,windows-performance-baselines,windows-ipc-validation,windows-engine-service-validation,windows-object-security-validation,windows-privilege-validation,windows-resource-leak-validation'
    $runtimeRun = Invoke-VerificationCapabilityGate -VerifyScript $verifyScript `
        -CapabilityList $runtimeCapabilities -Id 'runtime-capability-suite' `
        -LogPath (Join-Path $logDirectory 'runtime-capabilities.log') -Required $false
    [void]$gates.Add($runtimeRun.Gate)
    Add-VerificationResultGates -Gates $gates -Prefix 'runtime' -RunPath $runtimeRun.RunPath

    $toolchain = Find-VSToolchain
    if ($toolchain) {
        try {
            Enter-DevEnvironment -Toolchain $toolchain
            $asanGate = Invoke-DirectCMakeAsanGate -RepoRoot $repoRootFull -LogDirectory $logDirectory `
                -CMakePath $toolchain.CMakeExe -CTestPath $toolchain.CTestExe
            [void]$gates.Add($asanGate)
        } catch {
            [void]$gates.Add((New-ReadinessGate -Id 'asan-protocol-ipc' -Status 'FAIL' -Reason 'asan-harness-exception' `
                -Required $true -Detail $_.Exception.Message))
        }
    } else {
        [void]$gates.Add((New-ReadinessGate -Id 'asan-protocol-ipc' -Status 'REQUIRED-BUT-UNAVAILABLE' `
            -Reason 'vs-toolchain-not-found' -Required $true -Detail 'VC-capable Visual Studio toolchain is unavailable'))
    }

    $docDrift = if ($buildRun.Gate.status -eq 'PASS' -and $toolchain) {
        Invoke-ReadinessProcess -FilePath $toolchain.CTestExe -Arguments @('--test-dir', (Join-Path $repoRootFull 'build\debug'), '-R', '^ffdoc_drift_tests$', '--output-on-failure') `
            -LogPath (Join-Path $logDirectory 'doc-drift.log') -TimeoutSeconds 180
    } else { $null }
    if ($docDrift) {
        [void]$gates.Add((New-ProcessGate -Id 'documentation-drift-and-line-endings' -ProcessResult $docDrift -Required $true -Detail 'ffdoc_drift_tests'))
    } else {
        [void]$gates.Add((New-ReadinessGate -Id 'documentation-drift-and-line-endings' -Status 'SKIPPED' -Reason 'build-or-toolchain-unavailable' `
            -Required $true -Detail 'doc-drift test depends on the Debug build gate'))
    }

    $candidateRoot = if ($ArtifactRoot) { $ArtifactRoot } else { Join-Path $repoRootFull 'build\release-test' }
    $candidateOutput = Join-Path $outputRoot 'release-candidate'
    $candidateScript = Join-Path $VerifyRoot 'release\Build-ReleaseCandidate.ps1'
    if (Test-Path -LiteralPath $candidateRoot -PathType Container) {
        # -Force is safe by construction: Build-ReleaseCandidate refuses to clear
        # a non-empty directory unless it carries this tool's own marker, so a
        # repeated compose run refreshes only its own candidate output and can
        # never delete operator evidence (CANDIDATE_OUTPUT_MARKER_REQUIRED).
        $candidate = Invoke-ReadinessPowerShell -ScriptPath $candidateScript `
            -Arguments @('-Mode', 'unsigned-validation', '-BuildRoot', $candidateRoot, '-OutputDirectory', $candidateOutput, '-SkipVerification', '-Force', '-OutputFormat', 'json') `
            -LogPath (Join-Path $logDirectory 'release-candidate.log') -TimeoutSeconds 900
        [void]$gates.Add((New-ProcessGate -Id 'release-candidate-packaging-and-provenance' -ProcessResult $candidate -Required $true `
            -Detail "unsigned-validation kit; output=$candidateOutput"))
    } else {
        [void]$gates.Add((New-ReadinessGate -Id 'release-candidate-packaging-and-provenance' -Status 'REQUIRED-BUT-UNAVAILABLE' `
            -Reason 'release-test-artifacts-not-built' -Required $true -Detail "missing build root: $candidateRoot"))
    }

    $reportOutput = Join-Path $outputRoot 'production-report'
    $productionRoot = Join-Path $repoRootFull 'build\release'
    if (Test-Path -LiteralPath $productionRoot -PathType Container) {
        $productionReport = Invoke-ReadinessPowerShell -ScriptPath (Join-Path $VerifyRoot 'release\ReleaseReadiness.ps1') `
            -Arguments @('-Mode', 'report', '-ArtifactRoot', $productionRoot, '-OutputDirectory', $reportOutput, '-OutputFormat', 'json') `
            -LogPath (Join-Path $logDirectory 'production-artifact-report.log') -TimeoutSeconds 300
        [void]$gates.Add((New-ProcessGate -Id 'production-release-artifact-readiness' -ProcessResult $productionReport -Required $false `
            -Detail 'existing production release artifact/signature/pin report; external prerequisites remain fail-closed'))
    } else {
        [void]$gates.Add((New-ReadinessGate -Id 'production-release-artifact-readiness' -Status 'REQUIRED-BUT-UNAVAILABLE' `
            -Reason 'production-release-artifacts-not-built' -Required $false -Detail 'production release artifacts were not present'))
    }

    if ($IncludePrivileged) {
        if (-not (Test-IsElevated)) {
            [void]$gates.Add((New-ReadinessGate -Id 'privileged-install-and-service-validation' -Status 'REQUIRED-BUT-UNAVAILABLE' `
                -Reason 'elevation-required' -Required $true -Detail 'rerun with -Elevate only when a disposable marker-owned install is authorized'))
        } else {
            $installRun = Invoke-VerificationCapabilityGate -VerifyScript $verifyScript `
                -CapabilityList 'windows-install-service-validation' -Id 'privileged-install-and-service-validation' `
                -LogPath (Join-Path $logDirectory 'privileged-install.log') -Required $true -Elevated
            [void]$gates.Add($installRun.Gate)
            Add-VerificationResultGates -Gates $gates -Prefix 'privileged-install' -RunPath $installRun.RunPath
        }
    } else {
        [void]$gates.Add((New-ReadinessGate -Id 'privileged-install-and-service-validation' -Status 'SKIPPED' `
            -Reason 'explicit-privileged-mode-required' -Required $false -Detail 'not requested; no install/service mutation was attempted'))
    }

    if ($IncludeInteractive) {
        $interactiveCapabilities = 'windows-ui-automation-validation,windows-ui-visual-validation'
        $interactiveRun = Invoke-VerificationCapabilityGate -VerifyScript $verifyScript `
            -CapabilityList $interactiveCapabilities -Id 'interactive-uia-and-visual-validation' `
            -LogPath (Join-Path $logDirectory 'interactive-validation.log') -Required $false
        [void]$gates.Add($interactiveRun.Gate)
        Add-VerificationResultGates -Gates $gates -Prefix 'interactive' -RunPath $interactiveRun.RunPath
    } else {
        [void]$gates.Add((New-ReadinessGate -Id 'interactive-uia-and-visual-validation' -Status 'SKIPPED' `
            -Reason 'interactive-mode-not-requested' -Required $false -Detail 'requires an attached interactive desktop and semantic UIA provider'))
    }

    [void]$gates.Add((New-ReadinessGate -Id 'test-pki-privileged-path' -Status 'SKIPPED' -Reason 'explicit-diagnostic-only' `
        -Required $false -Detail 'test-PKI signing and trust-store mutation are never implicit in production release readiness'))
    [void]$gates.Add((New-ReadinessGate -Id 'tier3-disposable-stress-and-guest-matrix' -Status 'REQUIRED-BUT-UNAVAILABLE' `
        -Reason 'disposable-guest-provider-not-available' -Required $false -Detail 'stress, clean-host, DPI, and multi-machine evidence require an isolated provider'))

    $finished = Get-Date
    $overall = Get-OverallReadinessStatus -Gates @($gates)
    $artifactPaths = @()
    if (Test-Path -LiteralPath $candidateOutput -PathType Container) { $artifactPaths += [IO.Path]::GetFullPath($candidateOutput) }
    $artifactPaths += [IO.Path]::GetFullPath($logDirectory)
    [pscustomobject]@{
        schemaVersion = 2
        kind = 'fastfiles-release-readiness-composition'
        generatedAtUtc = [DateTime]::UtcNow.ToString('o')
        durationMs = [math]::Round(($finished - $started).TotalMilliseconds, 0)
        commit = [pscustomobject]@{ sha = $gitState.sha; branch = $gitState.branch; dirty = $gitState.dirty; changedPaths = $gitState.changedPaths }
        version = $version
        architecture = 'x64'
        configuration = @('debug', 'release-test', 'analyze', 'asan')
        toolchain = if ($toolchain) { [pscustomobject]@{ displayName = $toolchain.DisplayName; installationPath = $toolchain.InstallationPath; installationVersion = $toolchain.InstallationVersion; windowsSdkVersion = $toolchain.WindowsSdkVersion; cmake = $toolchain.CMakeExe; ninja = $toolchain.NinjaExe; ctest = $toolchain.CTestExe } } else { $null }
        host = [pscustomobject]@{ machine = $env:COMPUTERNAME; os = [Environment]::OSVersion.VersionString; processArchitecture = [Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString(); sessionId = [Diagnostics.Process]::GetCurrentProcess().SessionId; elevated = Test-IsElevated; fingerprint = "$env:COMPUTERNAME/$([Environment]::OSVersion.VersionString)/$([Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture)" }
        gates = @($gates)
        artifacts = $artifactPaths
        failureDiagnostics = @($gates | Where-Object { $_.status -eq 'FAIL' } | ForEach-Object { [pscustomobject]@{ gate = $_.id; reason = $_.reason; detail = $_.detail; artifacts = $_.artifacts } })
        externalPrerequisites = @('authorized production signing provider and pins', 'clean-host production install/runtime', 'real MFT/USN production matrix', 'disposable Windows guest or multi-machine provider', 'physical DPI/monitor visual matrix')
        overall = $overall
        outputDirectory = $outputRoot
    }
}

Export-ModuleMember -Function Invoke-ReleaseReadinessComposition
