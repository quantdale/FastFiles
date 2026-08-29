<#
    Central result composition for the PowerShell verifier.

    runtime-certification-and-verifier-convergence 2.1/2.2: capability modules
    may keep their existing internal payloads, but the harness owns the final
    envelope, child identity, failure precedence, and exit-code projection.
    This module deliberately has no capability-specific imports.
#>

$script:ResultStatuses = @('PASS', 'FAIL', 'SKIPPED')
$script:FailureRanks = @{
    'HARNESS_ERROR'        = 100
    'ASSERTION_FAILURE'    = 80
    'TIMEOUT'              = 70
    'CANCELLED'            = 70
    'PROVIDER_FAILURE'     = 60
    'CLEANUP_INCOMPLETE'   = 50
    'REPORT_FAILURE'       = 40
    'REQUIRED-BUT-UNAVAILABLE' = 10
    'BLOCKED_ENVIRONMENT'  = 10
}

function Limit-VerificationText {
    [CmdletBinding()]
    param(
        [AllowNull()] [object] $Value,
        [int] $Maximum = 4096
    )

    if ($null -eq $Value) { return $null }
    $text = [string]$Value
    if ($text.Length -le $Maximum) { return $text }
    return $text.Substring(0, $Maximum) + '…'
}

function Get-VerificationProperty {
    param(
        [AllowNull()] [object] $InputObject,
        [Parameter(Mandatory)] [string] $Name
    )

    if ($null -eq $InputObject) { return $null }
    $property = $InputObject.PSObject.Properties[$Name]
    if ($null -eq $property) { return $null }
    return $property.Value
}

function Get-VerificationClassification {
    param(
        [string] $Status,
        [string] $Reason,
        [string] $ExplicitClassification
    )

    if ($ExplicitClassification) { return $ExplicitClassification }
    if ($Status -eq 'PASS') { return 'PASS' }
    if ($Status -eq 'SKIPPED') {
        if ($Reason -match '^(interactive-|explorer-|environment-|foreground-)') { return 'BLOCKED_ENVIRONMENT' }
        if ($Reason -match '^(privileged-|engine-connect|ui-exe-not-built|provider-|.*unavailable)') { return 'REQUIRED-BUT-UNAVAILABLE' }
        return 'REQUIRED-BUT-UNAVAILABLE'
    }
    if ($Reason -match 'cleanup') { return 'CLEANUP_INCOMPLETE' }
    if ($Reason -match 'timeout') { return 'TIMEOUT' }
    if ($Reason -match 'cancel') { return 'CANCELLED' }
    if ($Reason -match 'driver-exception|lifecycle-|schema|harness|contract') { return 'HARNESS_ERROR' }
    return 'ASSERTION_FAILURE'
}

function ConvertTo-VerificationChildResult {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [object] $Child,
        [Parameter(Mandatory)] [pscustomobject] $RunContext,
        [Parameter(Mandatory)] [string] $CapabilityId,
        [Parameter(Mandatory)] [string] $InvocationId,
        [Parameter(Mandatory)] [string] $DefaultStartedAtUtc,
        [Parameter(Mandatory)] [string] $DefaultFinishedAtUtc
    )

    $id = Get-VerificationProperty -InputObject $Child -Name 'id'
    if (-not $id) { $id = Get-VerificationProperty -InputObject $Child -Name 'scenarioId' }
    if (-not $id) { throw "Capability '$CapabilityId' emitted a child result without a stable scenario id." }

    $status = [string](Get-VerificationProperty -InputObject $Child -Name 'status')
    if ($script:ResultStatuses -notcontains $status) {
        throw "Capability '$CapabilityId' scenario '$id' emitted unsupported status '$status'."
    }

    $childRunId = [string](Get-VerificationProperty -InputObject $Child -Name 'runId')
    if ($childRunId -and $RunContext.RunId -and $childRunId -ne [string]$RunContext.RunId) {
        throw "Capability '$CapabilityId' scenario '$id' belongs to run '$childRunId', expected '$($RunContext.RunId)'."
    }
    $childInvocationId = [string](Get-VerificationProperty -InputObject $Child -Name 'invocationId')
    if ($childInvocationId -and $childInvocationId -ne $InvocationId) {
        throw "Capability '$CapabilityId' scenario '$id' belongs to invocation '$childInvocationId', expected '$InvocationId'."
    }

    $reason = [string](Get-VerificationProperty -InputObject $Child -Name 'reason')
    $classification = Get-VerificationClassification -Status $status -Reason $reason `
        -ExplicitClassification ([string](Get-VerificationProperty -InputObject $Child -Name 'classification'))
    $duration = Get-VerificationProperty -InputObject $Child -Name 'durationMs'
    if ($null -eq $duration) { $duration = 0 }
    try { $duration = [math]::Max(0, [double]$duration) } catch { throw "Scenario '$id' has invalid durationMs '$duration'." }

    $detail = Limit-VerificationText -Value (Get-VerificationProperty -InputObject $Child -Name 'detail')
    if (-not $detail) { $detail = Limit-VerificationText -Value (Get-VerificationProperty -InputObject $Child -Name 'summary') }
    $diagnostics = @((Get-VerificationProperty -InputObject $Child -Name 'diagnostics'))
    $evidencePaths = @((Get-VerificationProperty -InputObject $Child -Name 'evidencePaths'))
    $cleanup = Get-VerificationProperty -InputObject $Child -Name 'cleanup'
    $lifecycleMode = [string](Get-VerificationProperty -InputObject $Child -Name 'lifecycleMode')
    if (-not $lifecycleMode) { $lifecycleMode = 'unknown' }

    [pscustomobject]@{
        scenarioId       = [string]$id
        id               = [string]$id
        capabilityId     = $CapabilityId
        runId            = [string]$RunContext.RunId
        invocationId     = $InvocationId
        lifecycleMode    = $lifecycleMode
        startedAtUtc     = [string]((Get-VerificationProperty -InputObject $Child -Name 'startedAtUtc') ?? $DefaultStartedAtUtc)
        finishedAtUtc    = [string]((Get-VerificationProperty -InputObject $Child -Name 'finishedAtUtc') ?? $DefaultFinishedAtUtc)
        status           = $status
        classification   = $classification
        reason           = $reason
        durationMs       = $duration
        detail           = $detail
        requiredContext  = Get-VerificationProperty -InputObject $Child -Name 'requiredContext'
        evidencePaths    = $evidencePaths
        diagnostics      = $diagnostics
        cleanup          = $cleanup
    }
}

function New-VerificationFailureRecord {
    param(
        [Parameter(Mandatory)] [string] $Source,
        [Parameter(Mandatory)] [string] $Classification,
        [string] $Reason,
        [string] $Summary,
        [string] $ScenarioId,
        [string] $DeclaredReason
    )

    [pscustomobject]@{
        source         = $Source
        scenarioId     = $ScenarioId
        classification = $Classification
        reason         = Limit-VerificationText $Reason 512
        summary        = Limit-VerificationText $Summary
        declaredReason = Limit-VerificationText $DeclaredReason 512
    }
}

function Resolve-VerificationOutcome {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [ValidateSet('PASS', 'FAIL', 'SKIPPED')] [string] $DeclaredStatus,
        [string] $DeclaredReason,
        [string] $DeclaredClassification,
        [string] $DeclaredSummary,
        [array] $SubResults = @(),
        [object] $Lifecycle,
        [object[]] $AdditionalFailures = @()
    )

    $failures = [System.Collections.Generic.List[object]]::new()
    foreach ($sub in @($SubResults)) {
        if ($sub.status -eq 'FAIL') {
            $classification = Get-VerificationClassification -Status 'FAIL' -Reason ([string]$sub.reason) `
                -ExplicitClassification ([string]$sub.classification)
            $failures.Add((New-VerificationFailureRecord -Source 'scenario' -ScenarioId ([string]$sub.scenarioId) `
                    -Classification $classification -Reason ([string]$sub.reason) -Summary ([string]$sub.detail)))
        }
    }

    if ($Lifecycle) {
        $cleanup = Get-VerificationProperty -InputObject $Lifecycle -Name 'cleanup'
        if ($cleanup) {
            $cleanupPass = Get-VerificationProperty -InputObject $cleanup -Name 'pass'
            if ($null -eq $cleanupPass) { $cleanupPass = Get-VerificationProperty -InputObject $cleanup -Name 'Pass' }
            $cleanupSucceeded = Get-VerificationProperty -InputObject $cleanup -Name 'succeeded'
            if ($null -eq $cleanupSucceeded) { $cleanupSucceeded = Get-VerificationProperty -InputObject $cleanup -Name 'Succeeded' }
            $cleanupStatus = [string](Get-VerificationProperty -InputObject $cleanup -Name 'status')
            if (($null -ne $cleanupPass -and -not [bool]$cleanupPass) -or
                ($null -ne $cleanupSucceeded -and -not [bool]$cleanupSucceeded) -or
                $cleanupStatus -eq 'FAIL') {
                $residue = @((Get-VerificationProperty -InputObject $cleanup -Name 'residue')) -join ', '
                $failures.Add((New-VerificationFailureRecord -Source 'cleanup' -Classification 'CLEANUP_INCOMPLETE' `
                        -Reason 'cleanup-incomplete' -Summary "Owned-resource cleanup was not proven complete$(if ($residue) { ": $residue" })"))
            }
        }
        $lifecycleFailures = @((Get-VerificationProperty -InputObject $Lifecycle -Name 'failures') | Where-Object { $null -ne $_ })
        foreach ($failure in $lifecycleFailures) {
            $failureClassification = [string](Get-VerificationProperty -InputObject $failure -Name 'classification')
            if (-not $failureClassification) { $failureClassification = 'HARNESS_ERROR' }
            $failures.Add((New-VerificationFailureRecord -Source 'lifecycle' -Classification $failureClassification `
                    -Reason ([string](Get-VerificationProperty -InputObject $failure -Name 'reason')) `
                    -Summary ([string](Get-VerificationProperty -InputObject $failure -Name 'summary'))))
        }
    }

    foreach ($failure in @($AdditionalFailures)) {
        if ($failure) { $failures.Add($failure) }
    }

    if ($DeclaredStatus -eq 'FAIL' -and $failures.Count -eq 0) {
        $hasExplicitClassification = -not [string]::IsNullOrWhiteSpace($DeclaredClassification)
        $opaqueDeclared = [string]::IsNullOrWhiteSpace($DeclaredReason) -or
            $DeclaredReason -in @('scenario-failed', 'capability-failed')
        $declaredClassification = if ($hasExplicitClassification -and -not $opaqueDeclared) {
            Get-VerificationClassification -Status 'FAIL' -Reason $DeclaredReason -ExplicitClassification $DeclaredClassification
        } else {
            'HARNESS_ERROR'
        }
        $failureReason = if ($hasExplicitClassification -and $DeclaredReason -and -not $opaqueDeclared) {
            $DeclaredReason
        } else {
            'capability-failed-without-attributed-cause'
        }
        $failureSummary = if ($opaqueDeclared -or -not $hasExplicitClassification) {
            "declared: $DeclaredReason$(if ($DeclaredSummary) { "; $DeclaredSummary" })"
        } else {
            $DeclaredSummary
        }
        $failures.Add((New-VerificationFailureRecord -Source 'capability' -Classification $declaredClassification `
                -Reason $failureReason -Summary $failureSummary -DeclaredReason $DeclaredReason))
    }

    if ($failures.Count -gt 0) {
        $ordered = @($failures | Sort-Object -Property @{ Expression = { if ($script:FailureRanks.ContainsKey($_.classification)) { $script:FailureRanks[$_.classification] } else { 90 } }; Descending = $true }, source, scenarioId)
        $primary = $ordered[0]
        $secondary = if ($ordered.Count -gt 1) { @($ordered | Select-Object -Skip 1) } else { @() }
        $declaredSummary = Limit-VerificationText $DeclaredSummary
        $summary = $declaredSummary
        $declaredReason = [string](Get-VerificationProperty -InputObject $primary -Name 'declaredReason')
        $declaredReasonAlreadyRecorded = $declaredReason -and ([string]$primary.summary -match [regex]::Escape($declaredReason))
        if (-not $summary -or $summary -eq 'scenario-failed' -or $primary.classification -in @('HARNESS_ERROR', 'TIMEOUT', 'CANCELLED', 'CLEANUP_INCOMPLETE', 'PROVIDER_FAILURE', 'REPORT_FAILURE')) {
            $summary = if ($declaredSummary -and $declaredSummary -ne $primary.summary -and -not $declaredReasonAlreadyRecorded) {
                "$($primary.reason): $($primary.summary); declared: $declaredSummary"
            } else {
                "$($primary.reason): $($primary.summary)"
            }
            $summary = Limit-VerificationText $summary
        }
        [pscustomobject]@{
            status           = 'FAIL'
            classification   = $primary.classification
            reason           = $primary.reason
            summary          = $summary
            primaryFailure   = $primary
            secondaryFailures = $secondary
        }
        return
    }

    if ($DeclaredStatus -eq 'SKIPPED') {
        $classification = Get-VerificationClassification -Status 'SKIPPED' -Reason $DeclaredReason -ExplicitClassification $DeclaredClassification
        return [pscustomobject]@{
            status           = 'SKIPPED'
            classification   = $classification
            reason           = $DeclaredReason
            summary          = Limit-VerificationText $DeclaredSummary
            primaryFailure   = $null
            secondaryFailures = @()
        }
    }

    return [pscustomobject]@{
        status           = 'PASS'
        classification   = 'PASS'
        reason           = $null
        summary          = Limit-VerificationText $DeclaredSummary
        primaryFailure   = $null
        secondaryFailures = @()
    }
}

function Resolve-VerificationRunOutcome {
    [CmdletBinding()]
    param(
        [array] $CapabilityEnvelopes = @(),
        [object[]] $RunFailures = @(),
        [object[]] $LifecycleFailures = @(),
        [object[]] $AdditionalFailures = @()
    )

    $failures = [System.Collections.Generic.List[object]]::new()
    foreach ($envelope in @($CapabilityEnvelopes | Where-Object { $null -ne $_ })) {
        if ([string]$envelope.status -eq 'FAIL') {
            $classification = Get-VerificationClassification -Status 'FAIL' -Reason ([string]$envelope.reason) `
                -ExplicitClassification ([string]$envelope.classification)
            $failures.Add((New-VerificationFailureRecord -Source 'capability' `
                    -Classification $classification -Reason ([string]$envelope.reason) `
                    -Summary ([string]$envelope.summary) -ScenarioId ([string]$envelope.capabilityId)))
        } elseif ([string]$envelope.classification -in @('HARNESS_ERROR', 'REPORT_FAILURE')) {
            $failures.Add((New-VerificationFailureRecord -Source 'capability' `
                    -Classification ([string]$envelope.classification) -Reason ([string]$envelope.reason) `
                    -Summary ([string]$envelope.summary) -ScenarioId ([string]$envelope.capabilityId)))
        }
    }

    foreach ($failure in @($RunFailures + $LifecycleFailures + $AdditionalFailures)) {
        if ($null -eq $failure) { continue }
        $status = [string](Get-VerificationProperty -InputObject $failure -Name 'status')
        $classification = [string](Get-VerificationProperty -InputObject $failure -Name 'classification')
        if ($status -ne 'FAIL' -and $classification -notin @('HARNESS_ERROR', 'REPORT_FAILURE', 'ASSERTION_FAILURE', 'TIMEOUT', 'CANCELLED', 'PROVIDER_FAILURE', 'CLEANUP_INCOMPLETE')) {
            continue
        }
        if (-not $classification) { $classification = Get-VerificationClassification -Status 'FAIL' -Reason ([string](Get-VerificationProperty -InputObject $failure -Name 'reason')) -ExplicitClassification $null }
        $failures.Add((New-VerificationFailureRecord -Source 'run' -Classification $classification `
                -Reason ([string](Get-VerificationProperty -InputObject $failure -Name 'reason')) `
                -Summary ([string](Get-VerificationProperty -InputObject $failure -Name 'summary'))))
    }

    if ($failures.Count -gt 0) {
        $ordered = @($failures | Sort-Object -Property @{ Expression = { if ($script:FailureRanks.ContainsKey($_.classification)) { $script:FailureRanks[$_.classification] } else { 90 } }; Descending = $true }, source, scenarioId, reason)
        $primary = $ordered[0]
        $secondary = if ($ordered.Count -gt 1) { @($ordered | Select-Object -Skip 1) } else { @() }
        return [pscustomobject]@{
            status            = 'FAIL'
            classification    = $primary.classification
            reason            = $primary.reason
            summary           = Limit-VerificationText $primary.summary
            primaryFailure    = $primary
            secondaryFailures = $secondary
        }
    }

    $passCount = @($CapabilityEnvelopes | Where-Object { $_ -and $_.status -eq 'PASS' }).Count
    $skipCount = @($CapabilityEnvelopes | Where-Object { $_ -and $_.status -eq 'SKIPPED' }).Count
    $runSkip = @($RunFailures | Where-Object { $_ -and $_.status -eq 'SKIPPED' }) | Select-Object -First 1
    if ($passCount -eq 0) {
        $skipEnvelopes = @($CapabilityEnvelopes | Where-Object { $_ -and $_.status -eq 'SKIPPED' })
        $selectedSkip = if ($runSkip) { $runSkip } elseif ($skipEnvelopes.Count -gt 0) {
            @($skipEnvelopes | Sort-Object -Property @{ Expression = {
                    switch ([string]$_.classification) {
                        'BLOCKED_ENVIRONMENT' { 20; break }
                        'REQUIRED-BUT-UNAVAILABLE' { 10; break }
                        default { 0; break }
                    }
                }; Descending = $true }, capabilityId | Select-Object -First 1)
        } else { $null }
        $reason = if ($selectedSkip) { [string](Get-VerificationProperty -InputObject $selectedSkip -Name 'reason') } else { 'no-required-capability-executed' }
        $summary = if ($selectedSkip) {
            $selectedSummary = [string](Get-VerificationProperty -InputObject $selectedSkip -Name 'summary')
            if ($skipEnvelopes.Count -gt 1) { "$($skipEnvelopes.Count) capability(s) skipped; first=$selectedSummary" } else { $selectedSummary }
        } else { 'No selected capability produced a PASS result.' }
        $classification = if ($selectedSkip) { [string](Get-VerificationProperty -InputObject $selectedSkip -Name 'classification') } else { 'REQUIRED-BUT-UNAVAILABLE' }
        if (-not $classification) { $classification = 'REQUIRED-BUT-UNAVAILABLE' }
        return [pscustomobject]@{
            status            = 'SKIPPED'
            classification    = $classification
            reason            = $reason
            summary           = Limit-VerificationText $summary
            primaryFailure    = $null
            secondaryFailures = @()
        }
    }

    return [pscustomobject]@{
        status            = 'PASS'
        classification    = 'PASS'
        reason            = $null
        summary           = "$passCount capability(s) passed; $skipCount skipped"
        primaryFailure    = $null
        secondaryFailures = @()
    }
}

function Get-VerificationExitCode {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [array] $Envelopes)

    $items = @($Envelopes | Where-Object { $null -ne $_ })
    if (@($items | Where-Object { $_.classification -in @('HARNESS_ERROR', 'REPORT_FAILURE') }).Count -gt 0) { return 3 }
    if (@($items | Where-Object { $_.status -eq 'FAIL' }).Count -gt 0) { return 1 }
    if (@($items | Where-Object { $_.status -eq 'PASS' }).Count -eq 0) { return 2 }
    return 0
}

function New-VerificationHarnessFailure {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $CapabilityId,
        [Parameter(Mandatory)] [string] $Reason,
        [string] $Summary,
        [string] $Classification = 'HARNESS_ERROR'
    )

    [pscustomobject]@{
        Status = 'FAIL'
        Reason = $Reason
        Classification = $Classification
        Summary = Limit-VerificationText $Summary
        Artifacts = @()
        SubResults = @([pscustomobject]@{
            id = 'capability-worker'
            scenarioId = 'capability-worker'
            status = 'FAIL'
            classification = $Classification
            reason = $Reason
            detail = Limit-VerificationText $Summary
            durationMs = 0
            lifecycleMode = 'worker'
        })
        Lifecycle = [pscustomobject]@{ cleanup = [pscustomobject]@{ pass = $true; residue = @() } }
    }
}

Export-ModuleMember -Function Limit-VerificationText, Get-VerificationProperty, Get-VerificationClassification,
    ConvertTo-VerificationChildResult, Resolve-VerificationOutcome, Resolve-VerificationRunOutcome, Get-VerificationExitCode,
    New-VerificationHarnessFailure
