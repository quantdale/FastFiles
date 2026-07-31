<#
    Suite descriptors (task 1.6): the extensibility surface *inside* a capability.
    A descriptor maps an id to a tier requirement, a predicate scriptblock (the actual
    check), and an optional on-failure-diagnostics scriptblock. A capability builds an
    array of descriptors and hands it to Invoke-SuiteDescriptors; adding a new check
    inside a capability means adding a descriptor entry, not editing this runner.
#>

Import-Module (Join-Path $PSScriptRoot 'TierGate.psm1') -Force -Global

function New-SuiteDescriptor {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $Id,
        [Parameter(Mandatory)] [int] $Tier,
        [Parameter(Mandatory)] [scriptblock] $Predicate,
        [string[]] $RequiredPrivileges = @(),
        [scriptblock] $OnFailureDiagnostics
    )
    [pscustomobject]@{
        Id                    = $Id
        Tier                  = $Tier
        RequiredPrivileges    = $RequiredPrivileges
        Predicate             = $Predicate
        OnFailureDiagnostics  = $OnFailureDiagnostics
    }
}

function Invoke-SuiteDescriptors {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [array] $Descriptors,
        [Parameter(Mandatory)] [pscustomobject] $Fingerprint,
        $Context
    )

    $subResults = @()
    foreach ($descriptor in $Descriptors) {
        $startedAt = Get-Date
        $availability = Test-TierAvailability -RequiredTier $descriptor.Tier -Fingerprint $Fingerprint

        if (-not $availability.Available) {
            $subResults += [pscustomobject]@{
                id              = $descriptor.Id
                tier            = $descriptor.Tier
                status          = 'SKIPPED'
                reason          = $availability.Reason
                requiredContext = $availability.RequiredContext
                durationMs      = 0
                detail          = $null
                diagnostics     = @()
            }
            continue
        }

        try {
            $outcome = & $descriptor.Predicate $Context
            $status = if ($outcome.Status) { $outcome.Status } else { 'FAIL' }
            $detail = $outcome.Detail
            $diagArtifacts = @()

            if ($status -eq 'FAIL' -and $descriptor.OnFailureDiagnostics) {
                try {
                    $diagArtifacts = & $descriptor.OnFailureDiagnostics $Context $outcome
                } catch {
                    $diagArtifacts = @([pscustomobject]@{ error = "diagnostics-collector-failed: $($_.Exception.Message)" })
                }
            }
        } catch {
            $status = 'FAIL'
            $detail = "descriptor threw: $($_.Exception.Message)"
            $diagArtifacts = @()
            if ($descriptor.OnFailureDiagnostics) {
                try {
                    $diagArtifacts = & $descriptor.OnFailureDiagnostics $Context $null
                } catch { }
            }
        }

        $finishedAt = Get-Date
        $subResults += [pscustomobject]@{
            id              = $descriptor.Id
            tier            = $descriptor.Tier
            status          = $status
            reason          = if ($status -eq 'SKIPPED') { $detail } else { $null }
            requiredContext = $null
            durationMs      = [math]::Round(($finishedAt - $startedAt).TotalMilliseconds, 0)
            detail          = $detail
            diagnostics     = $diagArtifacts
        }
    }

    return $subResults
}

Export-ModuleMember -Function New-SuiteDescriptor, Invoke-SuiteDescriptors
