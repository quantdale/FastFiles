<#
    Capability execution boundary.

    runtime-certification-and-verifier-convergence 4.1/4.2: capability code is
    executed in a killable PowerShell worker. The parent is the sole owner of
    the authoritative envelope and never infers a result from worker console
    text or an incomplete child counter.
#>

Import-Module (Join-Path $PSScriptRoot 'RunTree.psm1') -Force -Global
Import-Module (Join-Path $PSScriptRoot 'ResultModel.psm1') -Force -Global

function ConvertTo-WorkerArgument {
    param([Parameter(Mandatory)] [string] $Value)
    '"' + ($Value -replace '(\\*)"', '$1$1\"') + '"'
}

function Get-BoundedWorkerOutput {
    param([string] $Path, [string] $Label)
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    try {
        $raw = Get-Content -LiteralPath $Path -Raw -ErrorAction Stop
        if ($null -eq $raw) { return $null }
        $text = [string]$raw
        if ($null -eq $text) { return $null }
        $text = $text.Trim()
        if ([string]::IsNullOrWhiteSpace($text)) { return $null }
        return "${Label}: $(Limit-VerificationText $text 2048)"
    } catch {
        $message = if ($_.Exception -and $_.Exception.Message) { $_.Exception.Message } else { 'worker-output-read-failed' }
        return "${Label}: unavailable ($message)"
    }
}

function Get-VerificationRepositoryRoot {
    param([Parameter(Mandatory)] [pscustomobject] $RunContext)
    return [IO.Path]::GetFullPath((Join-Path $RunContext.VerifyRoot '..'))
}

function Get-VerificationBuildProcessSnapshot {
    <# Exact path/PID/start-time identities; executable names are only a cheap filter. #>
    [CmdletBinding()]
    param([Parameter(Mandatory)] [pscustomobject] $RunContext)

    $repoRoot = Get-VerificationRepositoryRoot -RunContext $RunContext
    $buildRoot = ([IO.Path]::GetFullPath((Join-Path $repoRoot 'build'))).TrimEnd('\') + '\'
    $records = [System.Collections.Generic.List[object]]::new()
    $errors = [System.Collections.Generic.List[string]]::new()
    try {
        foreach ($process in @(Get-CimInstance -ClassName Win32_Process -ErrorAction Stop)) {
            $path = [string]$process.ExecutablePath
            if ([string]::IsNullOrWhiteSpace($path)) { continue }
            $fullPath = [IO.Path]::GetFullPath($path)
            if (-not $fullPath.StartsWith($buildRoot, [StringComparison]::OrdinalIgnoreCase)) { continue }
            if ([IO.Path]::GetFileName($fullPath) -notin @('FastFiles.exe', 'FastFilesEngine.exe')) { continue }
            $startUtc = $null
            try { $startUtc = [Management.ManagementDateTimeConverter]::ToDateTime([string]$process.CreationDate).ToUniversalTime().ToString('o') } catch { $errors.Add("pid=$($process.ProcessId): creation time unavailable") }
            $records.Add([pscustomobject]@{
                    pid = [int]$process.ProcessId
                    path = $fullPath
                    startTimeUtc = $startUtc
                    identityKey = "$($process.ProcessId)|$($fullPath.ToLowerInvariant())|$startUtc"
                })
        }
    } catch {
        $errors.Add("process snapshot failed: $($_.Exception.Message)")
    }
    [pscustomobject]@{ identities = @($records); errors = @($errors); verified = $errors.Count -eq 0 }
}

function Test-VerificationProcessIdentityCurrent {
    param([Parameter(Mandatory)] [pscustomobject] $Identity)
    try {
        $process = Get-Process -Id ([int]$Identity.pid) -ErrorAction Stop
        $path = [IO.Path]::GetFullPath([string]$process.Path)
        if (-not $path.Equals([string]$Identity.path, [StringComparison]::OrdinalIgnoreCase)) { return $false }
        $actualStart = $process.StartTime.ToUniversalTime()
        $expectedStart = [DateTime]::Parse([string]$Identity.startTimeUtc).ToUniversalTime()
        return [math]::Abs(($actualStart - $expectedStart).TotalSeconds) -lt 2
    } catch { return $false }
}

function Stop-VerificationOwnedBuildProcesses {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [pscustomobject] $RunContext,
        [Parameter(Mandatory)] [pscustomobject] $Before,
        [Parameter(Mandatory)] [pscustomobject] $After
    )

    $beforeKeys = @{}
    foreach ($identity in @($Before.identities)) { $beforeKeys[$identity.identityKey] = $true }
    $terminated = [System.Collections.Generic.List[object]]::new()
    $residue = [System.Collections.Generic.List[object]]::new()
    foreach ($identity in @($After.identities)) {
        if ($beforeKeys.ContainsKey($identity.identityKey)) { continue }
        if (-not (Test-VerificationProcessIdentityCurrent -Identity $identity)) {
            $residue.Add([pscustomobject]@{ pid = $identity.pid; path = $identity.path; reason = 'identity-no-longer-verifiable' })
            continue
        }
        try {
            $process = Get-Process -Id ([int]$identity.pid) -ErrorAction Stop
            $process.Kill()
            [void]$process.WaitForExit(5000)
            if (Test-VerificationProcessIdentityCurrent -Identity $identity) {
                $residue.Add([pscustomobject]@{ pid = $identity.pid; path = $identity.path; reason = 'owned-process-did-not-exit' })
            } else {
                $terminated.Add([pscustomobject]@{ pid = $identity.pid; path = $identity.path; startTimeUtc = $identity.startTimeUtc })
            }
        } catch {
            if (Test-VerificationProcessIdentityCurrent -Identity $identity) {
                $residue.Add([pscustomobject]@{ pid = $identity.pid; path = $identity.path; reason = "termination-failed: $($_.Exception.Message)" })
            } else {
                # The exact original identity is gone (including a PID reuse or
                # an exit between snapshot and Kill); do not report a live
                # residue merely because the cleanup observation raced exit.
                $terminated.Add([pscustomobject]@{ pid = $identity.pid; path = $identity.path; startTimeUtc = $identity.startTimeUtc; observation = 'identity-absent-after-termination-race' })
            }
        }
    }
    [pscustomobject]@{
        pass = $Before.verified -and $After.verified -and $residue.Count -eq 0
        verified = $Before.verified -and $After.verified
        terminated = @($terminated)
        residue = @($residue)
        snapshotErrors = @($Before.errors + $After.errors)
    }
}

function Stop-VerificationOwnedBuildProcessesUntilStable {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [pscustomobject] $RunContext,
        [Parameter(Mandatory)] [pscustomobject] $Before,
        [int] $TimeoutSeconds = 10
    )

    $terminated = [System.Collections.Generic.List[object]]::new()
    $residue = [System.Collections.Generic.List[object]]::new()
    $snapshotErrors = [System.Collections.Generic.List[string]]::new()
    $afterVerified = $true
    $deadline = [DateTime]::UtcNow.AddSeconds([math]::Max(1, $TimeoutSeconds))
    do {
        $after = Get-VerificationBuildProcessSnapshot -RunContext $RunContext
        $afterVerified = $afterVerified -and $after.verified
        foreach ($error in @($after.errors)) { $snapshotErrors.Add([string]$error) }
        $attempt = Stop-VerificationOwnedBuildProcesses -RunContext $RunContext -Before $Before -After $after
        foreach ($item in @($attempt.terminated)) { $terminated.Add($item) }
        foreach ($item in @($attempt.residue)) { $residue.Add($item) }

        $remaining = @($after.identities | Where-Object {
                $beforeKeys = @($Before.identities | ForEach-Object { $_.identityKey })
                $_.identityKey -notin $beforeKeys -and (Test-VerificationProcessIdentityCurrent -Identity $_)
            })
        if ($remaining.Count -eq 0 -and $residue.Count -eq 0) {
            return [pscustomobject]@{
                pass = $Before.verified -and $afterVerified
                verified = $Before.verified -and $afterVerified
                terminated = @($terminated | Select-Object -Unique -Property pid, path, startTimeUtc, observation)
                residue = @()
                snapshotErrors = @($Before.errors + $snapshotErrors)
            }
        }
        if ([DateTime]::UtcNow -ge $deadline) { break }
        Start-Sleep -Milliseconds 100
    } while ($true)

    $finalAfter = Get-VerificationBuildProcessSnapshot -RunContext $RunContext
    $finalAttempt = Stop-VerificationOwnedBuildProcesses -RunContext $RunContext -Before $Before -After $finalAfter
    foreach ($item in @($finalAttempt.terminated)) { $terminated.Add($item) }
    foreach ($item in @($finalAttempt.residue)) { $residue.Add($item) }
    [pscustomobject]@{
        pass = $false
        verified = $Before.verified -and $afterVerified -and $finalAfter.verified
        terminated = @($terminated | Select-Object -Unique -Property pid, path, startTimeUtc, observation)
        residue = @($residue)
        snapshotErrors = @($Before.errors + $snapshotErrors + $finalAfter.errors)
    }
}

function Invoke-CapabilityDirect {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [pscustomobject] $Capability,
        [Parameter(Mandatory)] [pscustomobject] $RunContext,
        [Parameter(Mandatory)] [pscustomobject] $Fingerprint,
        [hashtable] $Options = @{}
    )

    $availabilityOutputs = @(& $Capability.Fn.Availability $Fingerprint)
    if ($availabilityOutputs.Count -ne 1) {
        throw "Capability '$($Capability.Id)' availability entry point emitted $($availabilityOutputs.Count) result objects; exactly one is required."
    }
    $availability = $availabilityOutputs[0]
    if ($null -eq $availability -or $null -eq $availability.PSObject.Properties['Available']) {
        throw "Capability '$($Capability.Id)' availability entry point returned no valid availability envelope."
    }
    if (-not [bool]$availability.Available) {
        return [pscustomobject]@{
            Status = 'SKIPPED'
            Reason = [string]$availability.Reason
            Classification = if ($availability.Classification) { [string]$availability.Classification } else { 'REQUIRED-BUT-UNAVAILABLE' }
            Summary = "Capability unavailable: $($availability.Reason)"
            Artifacts = @()
            SubResults = @()
            Lifecycle = [pscustomobject]@{ cleanup = [pscustomobject]@{ pass = $true; verified = $true; residue = @() } }
        }
    }

    $artifactsDir = New-CapabilityArtifactsDir -RunContext $RunContext -CapabilityId $Capability.Id
    $runOutputs = @(& $Capability.Fn.Run $RunContext $Fingerprint $artifactsDir $Options)
    if ($runOutputs.Count -ne 1) {
        throw "Capability '$($Capability.Id)' run entry point emitted $($runOutputs.Count) result objects; exactly one is required."
    }
    $outcome = $runOutputs[0]
    if ($null -eq $outcome -or $null -eq $outcome.PSObject.Properties['Status']) {
        throw "Capability '$($Capability.Id)' run entry point returned no valid result envelope."
    }
    return $outcome
}

function New-CapabilityWorkerEnvelope {
    param(
        [Parameter(Mandatory)] [pscustomobject] $Capability,
        [Parameter(Mandatory)] [pscustomobject] $RunContext,
        [Parameter(Mandatory)] [datetime] $StartedAt,
        [Parameter(Mandatory)] [datetime] $FinishedAt,
        [Parameter(Mandatory)] [object] $RawOutcome,
        [Parameter(Mandatory)] [pscustomobject] $WorkerLifecycle,
        [string] $ProviderId
    )

    $defaultStart = $StartedAt.ToUniversalTime().ToString('o')
    $defaultFinish = $FinishedAt.ToUniversalTime().ToString('o')
    $rawSubResults = @()
    $rawSubValue = Get-VerificationProperty -InputObject $RawOutcome -Name 'SubResults'
    if ($null -ne $rawSubValue) { $rawSubResults = @($rawSubValue | Where-Object { $null -ne $_ }) }
    $children = [System.Collections.Generic.List[object]]::new()
    $seen = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($child in $rawSubResults) {
        $normalized = ConvertTo-VerificationChildResult -Child $child -RunContext $RunContext -CapabilityId $Capability.Id `
            -InvocationId $RunContext.InvocationId -DefaultStartedAtUtc $defaultStart -DefaultFinishedAtUtc $defaultFinish
        if (-not $seen.Add([string]$normalized.scenarioId)) { throw "Capability '$($Capability.Id)' emitted duplicate scenario id '$($normalized.scenarioId)'." }
        $children.Add($normalized)
    }

    $declaredStatus = [string](Get-VerificationProperty -InputObject $RawOutcome -Name 'Status')
    if (@('PASS', 'FAIL', 'SKIPPED') -notcontains $declaredStatus) { throw "Capability '$($Capability.Id)' emitted unsupported status '$declaredStatus'." }
    $composition = Resolve-VerificationOutcome -DeclaredStatus $declaredStatus `
        -DeclaredReason ([string](Get-VerificationProperty -InputObject $RawOutcome -Name 'Reason')) `
        -DeclaredClassification ([string](Get-VerificationProperty -InputObject $RawOutcome -Name 'Classification')) `
        -DeclaredSummary ([string](Get-VerificationProperty -InputObject $RawOutcome -Name 'Summary')) `
        -SubResults @($children) -Lifecycle $WorkerLifecycle

    $workerArtifacts = @()
    $workerRecord = Get-VerificationProperty -InputObject $WorkerLifecycle -Name 'worker'
    if ($workerRecord -and $null -ne (Get-VerificationProperty -InputObject $workerRecord -Name 'pid')) {
        $workerArtifacts = @(
            [pscustomobject]@{ path = "artifacts/$($Capability.Id)/worker.stdout.log"; type = 'worker-stdout' }
            [pscustomobject]@{ path = "artifacts/$($Capability.Id)/worker.stderr.log"; type = 'worker-stderr' }
        )
    }
    $rawArtifacts = @((Get-VerificationProperty -InputObject $RawOutcome -Name 'Artifacts')) | Where-Object { $null -ne $_ }
    New-ResultEnvelope -CapabilityId $Capability.Id -InterfaceVersion $Capability.InterfaceVersion -Tier $Capability.Tier `
        -Status $composition.status -StartedAtUtc $StartedAt -FinishedAtUtc $FinishedAt -RunContext $RunContext `
        -Reason $composition.reason -Classification $composition.classification -RequiredContext $null `
        -Summary $composition.summary -Artifacts @($rawArtifacts + $workerArtifacts) -SubResults @($children) `
        -Lifecycle $WorkerLifecycle -ProviderIdentity ([pscustomobject]@{ id = $ProviderId; capabilityTimeoutSeconds = $Capability.TimeoutSeconds }) `
        -Metadata (Get-VerificationProperty -InputObject $RawOutcome -Name 'Metadata') `
        -PrimaryFailure $composition.primaryFailure -SecondaryFailures @($composition.secondaryFailures)
}

function Invoke-Capability {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [pscustomobject] $Capability,
        [Parameter(Mandatory)] [pscustomobject] $RunContext,
        [Parameter(Mandatory)] [pscustomobject] $Fingerprint,
        [hashtable] $Options = @{}
    )

    $startedAt = Get-Date
    $artifactsDir = New-CapabilityArtifactsDir -RunContext $RunContext -CapabilityId $Capability.Id
    $requestPath = Join-Path $artifactsDir 'worker-request.json'
    $candidatePath = Join-Path $artifactsDir 'worker-candidate.json'
    $stdoutPath = Join-Path $artifactsDir 'worker.stdout.log'
    $stderrPath = Join-Path $artifactsDir 'worker.stderr.log'
    $before = Get-VerificationBuildProcessSnapshot -RunContext $RunContext
    if ($Capability.RequiresCleanBuildProcessBaseline -and @($before.identities).Count -gt 0) {
        $startedAt = Get-Date
        $preexisting = @($before.identities | ForEach-Object { "pid=$($_.pid) path=$($_.path) start=$($_.startTimeUtc)" }) -join '; '
        $rawOutcome = New-VerificationHarnessFailure -CapabilityId $Capability.Id -Reason 'stale-build-process-before-run' `
            -Summary "Capability requires an empty repository-build process baseline; pre-existing exact identities were not adopted or killed: $preexisting"
        $lifecycle = [pscustomobject]@{
            worker = [pscustomobject]@{ pid = $null; exitCode = $null; exited = $true; timedOut = $false }
            cleanup = [pscustomobject]@{ pass = $true; verified = $before.verified; residue = @() }
            preflight = [pscustomobject]@{ preexistingProcesses = @($before.identities) }
        }
        $envelope = New-CapabilityWorkerEnvelope -Capability $Capability -RunContext $RunContext -StartedAt $startedAt -FinishedAt (Get-Date) `
            -RawOutcome $rawOutcome -WorkerLifecycle $lifecycle -ProviderId ([string]$Fingerprint.ProviderId)
        Save-CapabilityResult -RunContext $RunContext -Result $envelope | Out-Null
        return $envelope
    }
    $request = [pscustomobject]@{
        protocolVersion = 1
        capabilityId = $Capability.Id
        capabilitiesRoot = (Join-Path $RunContext.VerifyRoot 'capabilities')
        manifestSchemaPath = (Join-Path $RunContext.VerifyRoot 'schemas\capability-manifest.schema.json')
        runContext = $RunContext
        fingerprint = $Fingerprint
        options = $Options
        candidatePath = $candidatePath
    }
    $request | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $requestPath -Encoding utf8

    $worker = $null
    $failureReason = $null
    $failureClassification = 'HARNESS_ERROR'
    $workerFailureSummary = $null
    $workerExited = $false
    try {
        $pwsh = (Get-Command pwsh.exe -ErrorAction Stop).Source
        $workerScript = Join-Path $PSScriptRoot 'CapabilityWorker.ps1'
        $arguments = @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $workerScript, '-RequestPath', $requestPath)
        $argumentString = ($arguments | ForEach-Object { ConvertTo-WorkerArgument -Value ([string]$_) }) -join ' '
        $worker = Start-Process -FilePath $pwsh -ArgumentList $argumentString -WorkingDirectory (Get-VerificationRepositoryRoot -RunContext $RunContext) `
            -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath -PassThru
        $timeoutSeconds = [int]$Capability.TimeoutSeconds
        if ($timeoutSeconds -le 0) { throw "Capability '$($Capability.Id)' has no positive manifest timeoutSeconds." }
        $waitMilliseconds = [int][math]::Min(([long]$timeoutSeconds * 1000), [int]::MaxValue)
        if (-not $worker.WaitForExit($waitMilliseconds)) {
            $failureReason = 'capability-timeout'
            $failureClassification = 'TIMEOUT'
            try { $worker.Kill($true) } catch { try { $worker.Kill() } catch { } }
            try { $worker.WaitForExit(5000) } catch { }
            try { $workerExited = $worker.HasExited } catch { $workerExited = $false }
        } else {
            $workerExited = $true
            if ($worker.ExitCode -ne 0) {
                $failureReason = 'capability-worker-exited-without-result'
                if (Test-Path -LiteralPath $candidatePath) {
                    try { $workerFailureSummary = [string]((Get-Content -LiteralPath $candidatePath -Raw | ConvertFrom-Json).error) } catch { }
                }
            }
        }
    } catch {
        $failureReason = 'capability-worker-launch-failed'
        $workerFailureSummary = $_.Exception.Message
    } finally {
        # A wait/launch exception must not orphan the worker. This is the exact
        # Process object returned by Start-Process, never a name-wide kill.
        try {
            if ($worker -and -not $worker.HasExited) {
                try { $worker.Kill($true) } catch { try { $worker.Kill() } catch { } }
                try { $worker.WaitForExit(5000) } catch { }
                $workerExited = $worker.HasExited
            }
        } catch { $workerExited = $false }
    }

    $processCleanup = Stop-VerificationOwnedBuildProcessesUntilStable -RunContext $RunContext -Before $before -TimeoutSeconds 10
    if ($worker -and -not $workerExited) {
        $processCleanup.pass = $false
        $processCleanup.verified = $false
        $processCleanup.residue = @($processCleanup.residue) + @([pscustomobject]@{ pid = $worker.Id; path = 'pwsh.exe'; reason = 'worker-process-did-not-exit' })
    }
    $finishedAt = Get-Date
    $workerLifecycle = [pscustomobject]@{
        worker = [pscustomobject]@{
            pid = if ($worker) { $worker.Id } else { $null }
            exitCode = if ($worker -and $workerExited) { $worker.ExitCode } else { $null }
            exited = $workerExited
            timedOut = $failureClassification -eq 'TIMEOUT'
            candidatePath = ("artifacts/$($Capability.Id)/worker-candidate.json")
            stdoutPath = ("artifacts/$($Capability.Id)/worker.stdout.log")
            stderrPath = ("artifacts/$($Capability.Id)/worker.stderr.log")
        }
        cleanup = $processCleanup
    }

    $rawOutcome = $null
    if (-not $failureReason -and (Test-Path -LiteralPath $candidatePath)) {
        try {
            $candidate = Get-Content -LiteralPath $candidatePath -Raw | ConvertFrom-Json
            if ([int]$candidate.protocolVersion -ne 1 -or [string]$candidate.capabilityId -ne $Capability.Id -or
                [string]$candidate.runId -ne [string]$RunContext.RunId -or [string]$candidate.invocationId -ne [string]$RunContext.InvocationId) {
                throw 'worker candidate identity or protocol mismatch'
            }
            $rawOutcome = $candidate.result
            if ($null -eq $rawOutcome) { throw 'worker candidate has no result object' }
        } catch {
            $failureReason = 'malformed-capability-output'
        }
    } elseif (-not $failureReason) {
        $failureReason = 'missing-capability-result'
    }

    if ($failureReason) {
        if (-not $workerFailureSummary) {
            $diagnosticLines = @(
                Get-BoundedWorkerOutput -Path $stderrPath -Label 'stderr'
                Get-BoundedWorkerOutput -Path $stdoutPath -Label 'stdout'
            ) | Where-Object { $_ }
            $workerFailureSummary = if ($diagnosticLines.Count -gt 0) { $diagnosticLines -join ' | ' } else { 'no worker stderr/stdout was captured' }
        }
        $failureSummary = if ($workerFailureSummary) { Limit-VerificationText $workerFailureSummary } else { 'Capability worker did not produce one authoritative result.' }
        $rawOutcome = New-VerificationHarnessFailure -CapabilityId $Capability.Id -Reason $failureReason `
            -Classification $failureClassification -Summary $failureSummary
    }

    try {
        $envelope = New-CapabilityWorkerEnvelope -Capability $Capability -RunContext $RunContext -StartedAt $startedAt -FinishedAt $finishedAt `
            -RawOutcome $rawOutcome -WorkerLifecycle $workerLifecycle -ProviderId ([string]$Fingerprint.ProviderId)
        Save-CapabilityResult -RunContext $RunContext -Result $envelope | Out-Null
        return $envelope
    } catch {
        $harnessRaw = New-VerificationHarnessFailure -CapabilityId $Capability.Id -Reason 'result-envelope-composition-failed' -Summary $_.Exception.Message
        $harnessEnvelope = New-CapabilityWorkerEnvelope -Capability $Capability -RunContext $RunContext -StartedAt $startedAt -FinishedAt (Get-Date) `
            -RawOutcome $harnessRaw -WorkerLifecycle $workerLifecycle -ProviderId ([string]$Fingerprint.ProviderId)
        Save-CapabilityResult -RunContext $RunContext -Result $harnessEnvelope | Out-Null
        return $harnessEnvelope
    }
}

Export-ModuleMember -Function Invoke-Capability, Invoke-CapabilityDirect
