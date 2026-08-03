<#
    Repair loop coordinator (tasks 9.1-9.5). Drives build -> install -> run -> verify
    -> diagnose -> root-cause -> fix -> re-run with a hard iteration cap. Fix
    classification (task 9.3): Class A (harness/config/environment) fixes are
    auto-applied and logged; Class B (product source) fixes are flagged for review,
    never silently accepted. The no-progress detector (task 9.4) fingerprints each
    failure (normalized error + capability + phase) and escalates on recurrence.
    Every iteration is appended to repair-log.jsonl (task 9.5) in the run tree.
#>

$script:DefaultMaxIterations = 3

function Get-FailureSignature {
    <# Task 9.4: failure signature = SHA256 of the normalized error + capability
       id + phase, so an identical failure reappearing after a "repair" is
       detected and escalates instead of looping forever. #>
    param([Parameter(Mandatory)] [string] $CapabilityId, [Parameter(Mandatory)] [string] $NormalizedError)
    $input = "$CapabilityId|repair|$($NormalizedError.ToLowerInvariant())"
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $hash = $sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($input))
    ($hash | ForEach-Object { $_.ToString('x2') }) -join ''
}

function Write-RepairLogRecord {
    param(
        [Parameter(Mandatory)] [pscustomobject] $RunContext,
        [Parameter(Mandatory)] [int] $Iteration,
        [Parameter(Mandatory)] [string] $CapabilityId,
        [string] $StatusBefore,
        [string] $RootCause,
        [string] $FixClass,
        [string] $Action,
        [string] $Outcome,
        [string] $FailureSignature
    )
    $record = [pscustomobject]@{
        timestampUtc     = (Get-Date).ToUniversalTime().ToString('o')
        iteration        = $Iteration
        capabilityId     = $CapabilityId
        statusBefore     = $StatusBefore
        rootCause        = $RootCause
        fixClass         = $FixClass
        action           = $Action
        outcome          = $Outcome
        failureSignature = $FailureSignature
    }
    Add-Content -LiteralPath $RunContext.RepairLogPath -Value ($record | ConvertTo-Json -Compress) -Encoding utf8
}

function Invoke-RepairForCapability {
    <#
        Task 9.1: per-capability repair entry point. Class A (harness/config/
        environment) fixes are auto-applied and logged. Class B (product source)
        fixes are NEVER applied here: they are flagged for review with the
        recorded root cause - the repair loop must not silently accept them.
        Returns the repair record.
    #>
    param(
        [Parameter(Mandatory)] [pscustomobject] $Capability,
        [Parameter(Mandatory)] [pscustomobject] $RunContext,
        [Parameter(Mandatory)] [pscustomobject] $Fingerprint,
        [Parameter(Mandatory)] [string] $ArtifactsDir,
        [Parameter(Mandatory)] [hashtable] $Options,
        [Parameter(Mandatory)] [int] $Iteration,
        [Parameter(Mandatory)] [string] $StatusBefore,
        [Parameter(Mandatory)] [string] $FailureSignature
    )

    $repairDir = Join-Path $ArtifactsDir "repair"
    New-Item -ItemType Directory -Path $repairDir -Force | Out-Null

    try {
        $result = & $Capability.Fn.Repair $RunContext $Fingerprint $repairDir $Options
    } catch {
        Write-RepairLogRecord -RunContext $RunContext -Iteration $Iteration -CapabilityId $Capability.Id `
            -StatusBefore $StatusBefore -RootCause 'repair-entry-point-threw' -FixClass 'unknown' `
            -Action "repair entry point '$($Capability.Fn.Repair)' threw: $($_.Exception.Message)" `
            -Outcome 'failed' -FailureSignature $FailureSignature
        return [pscustomobject]@{ Outcome = 'failed'; Reason = 'repair-entry-point-threw' }
    }

    $fixClass = $result.fixClass
    if ($fixClass -notin @('harness', 'config', 'environment', 'product-source')) {
        $fixClass = 'unknown'
    }
    $rootCause = if ($result.rootCause) { $result.rootCause } else { 'unspecified' }
    $action = if ($result.action) { $result.action } else { 'no action described' }

    if ($fixClass -eq 'product-source') {
        # Class B: never silently accepted. Flag for review; do not claim a fix.
        Write-RepairLogRecord -RunContext $RunContext -Iteration $Iteration -CapabilityId $Capability.Id `
            -StatusBefore $StatusBefore -RootCause $rootCause -FixClass 'product-source' -Action $action `
            -Outcome 'flagged-for-review' -FailureSignature $FailureSignature
        return [pscustomobject]@{ Outcome = 'flagged-for-review'; Reason = $result.reason }
    }

    # Class A: auto-apply.
    Write-RepairLogRecord -RunContext $RunContext -Iteration $Iteration -CapabilityId $Capability.Id `
        -StatusBefore $StatusBefore -RootCause $rootCause -FixClass $fixClass -Action $action `
        -Outcome $(if ($result.outcome) { $result.outcome } else { 'applied' }) -FailureSignature $FailureSignature
    return [pscustomobject]@{ Outcome = $(if ($result.outcome) { $result.outcome } else { 'applied' }); Reason = $result.reason }
}

function Invoke-RepairLoop {
    <#
        Task 9.2/9.4: loop driver with a hard iteration cap and the no-progress
        detector. After the round of repairs, each repaired capability is re-run
        via the standard capability runner; the loop stops when nothing fails,
        when the cap is hit, or when a failure signature recurs (escalation).
    #>
    param(
        [Parameter(Mandatory)] [pscustomobject] $RunContext,
        [Parameter(Mandatory)] [pscustomobject] $Fingerprint,
        [Parameter(Mandatory)] [pscustomobject] $Discovery,
        [Parameter(Mandatory)] [array] $Envelopes,
        [Parameter(Mandatory)] [hashtable] $Options,
        [int] $MaxIterations = $script:DefaultMaxIterations
    )

    Import-Module (Join-Path $PSScriptRoot 'CapabilityRunner.psm1') -Force -Global

    $envelopes = @($Envelopes)
    $seenSignatures = @{}
    $iteration = 1
    $repairAttempted = @()
    $escalation = $null

    while ($iteration -le $MaxIterations) {
        $failures = @($envelopes | Where-Object { $_.status -eq 'FAIL' })
        if ($failures.Count -eq 0) { break }

        $repairable = @($Discovery.Capabilities | Where-Object {
            $_.RepairPosture -eq 'repair-supported' -and
            ($failures.capabilityId -contains $_.Id) -and
            ($_.Id -notin $repairAttempted) })

        if ($repairable.Count -eq 0) {
            foreach ($failure in $failures) {
                $signature = Get-FailureSignature -CapabilityId $failure.capabilityId -NormalizedError $(if ($failure.reason) { $failure.reason } else { 'unspecified' })
                Write-RepairLogRecord -RunContext $RunContext -Iteration $iteration -CapabilityId $failure.capabilityId `
                    -StatusBefore 'FAIL' -RootCause 'no-repairable-capability' -FixClass 'unknown' `
                    -Action "no capability declares repairPosture repair-supported for this failure" `
                    -Outcome 'not-applied' -FailureSignature $signature
            }
            break
        }

        foreach ($capability in $repairable) {
            $failure = $failures | Where-Object { $_.capabilityId -eq $capability.Id } | Select-Object -First 1
            $normalizedError = if ($failure.reason) { $failure.reason } else { 'unspecified' }
            $signature = Get-FailureSignature -CapabilityId $capability.Id -NormalizedError $normalizedError

            if ($seenSignatures.ContainsKey($signature)) {
                # Task 9.4: recurrence after an attempted repair -> stop and escalate.
                $escalation = [pscustomobject]@{
                    escalated       = $true
                    reason          = 'no-progress-recurrence'
                    capabilityId    = $capability.Id
                    iteration       = $iteration
                    firstSeenAt     = $seenSignatures[$signature]
                    failureSignature = $signature
                }
                Write-RepairLogRecord -RunContext $RunContext -Iteration $iteration -CapabilityId $capability.Id `
                    -StatusBefore 'FAIL' -RootCause 'no-progress-recurrence' -FixClass 'unknown' `
                    -Action 'repair loop stopped; recurrence of identical failure signature after an attempted fix' `
                    -Outcome 'escalated' -FailureSignature $signature
                break
            }
            $seenSignatures[$signature] = $iteration

            Write-Host "==> Repairing capability: $($capability.Id) (iteration $iteration)"
            $repairOutcome = Invoke-RepairForCapability -Capability $capability -RunContext $RunContext -Fingerprint $Fingerprint `
                -ArtifactsDir $RunContext.ArtifactsRoot -Options $Options -Iteration $iteration `
                -StatusBefore 'FAIL' -FailureSignature $signature
            $repairAttempted += $capability.Id

            if ($repairOutcome.Outcome -eq 'flagged-for-review') {
                # Class B: the source fix must be made by a developer; re-running
                # the capability now can only fail again. Leave the envelope failed.
                $escalation = [pscustomobject]@{
                    escalated    = $true
                    reason       = 'product-source-fix-flagged-for-review'
                    capabilityId = $capability.Id
                    iteration    = $iteration
                }
                break
            }

            if ($repairOutcome.Outcome -ne 'applied') {
                # Repair itself failed (e.g. clean rebuild still broken): do not
                # re-run, do not retry the same repair. Recorded in repair-log.jsonl.
                Write-Host "    repair did not apply: $($repairOutcome.Reason)"
                continue
            }

            $reRun = Invoke-Capability -Capability $capability -RunContext $RunContext -Fingerprint $Fingerprint -Options $Options
            $envelopes = @($envelopes | Where-Object { $_.capabilityId -ne $capability.Id }) + $reRun
            if ($reRun.status -eq 'FAIL') {
                # Task 9.4: the identical failure recurring right after an applied
                # repair is no-progress - stop and escalate.
                $recurSignature = Get-FailureSignature -CapabilityId $capability.Id -NormalizedError $(if ($reRun.reason) { $reRun.reason } else { 'unspecified' })
                if ($seenSignatures.ContainsKey($recurSignature)) {
                    $escalation = [pscustomobject]@{
                        escalated        = $true
                        reason           = 'no-progress-recurrence'
                        capabilityId     = $capability.Id
                        iteration        = $iteration
                        firstSeenAt      = $seenSignatures[$recurSignature]
                        failureSignature = $recurSignature
                    }
                    Write-RepairLogRecord -RunContext $RunContext -Iteration $iteration -CapabilityId $capability.Id `
                        -StatusBefore 'FAIL' -RootCause 'no-progress-recurrence' -FixClass 'unknown' `
                        -Action 'identical failure recurred immediately after an applied repair' `
                        -Outcome 'escalated' -FailureSignature $recurSignature
                    break
                }
            }
        }

        if ($escalation) { break }
        $iteration++
    }

    [pscustomobject]@{
        Envelopes   = $envelopes
        Escalation  = $escalation
        Iterations  = $iteration
        RepairedIds = @($repairAttempted)
    }
}

Export-ModuleMember -Function Get-FailureSignature, Write-RepairLogRecord, Invoke-RepairForCapability, Invoke-RepairLoop
