<#
    Bridges the registry (a loaded capability's declared entry-point function names) to
    the run tree (envelope construction + persistence). The capability's Run function
    returns only the common envelope fields (Status/Reason/Summary/Artifacts/SubResults)
    plus whatever capability-specific payload it already wrote under ArtifactsDir itself
    — this module never inspects that payload (D16).
#>

Import-Module (Join-Path $PSScriptRoot 'RunTree.psm1') -Force -Global

function Invoke-Capability {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [pscustomobject] $Capability,
        [Parameter(Mandatory)] [pscustomobject] $RunContext,
        [Parameter(Mandatory)] [pscustomobject] $Fingerprint,
        [hashtable] $Options = @{}
    )

    $startedAt = Get-Date
    $availability = & $Capability.Fn.Availability $Fingerprint

    if (-not $availability.Available) {
        $finishedAt = Get-Date
        $envelope = New-ResultEnvelope -CapabilityId $Capability.Id -InterfaceVersion $Capability.InterfaceVersion `
            -Tier $Capability.Tier -Status 'SKIPPED' -StartedAtUtc $startedAt -FinishedAtUtc $finishedAt `
            -Reason $availability.Reason -RequiredContext $availability.RequiredContext `
            -Summary "Capability unavailable: $($availability.Reason)" -Artifacts @() -SubResults @()
        Save-CapabilityResult -RunContext $RunContext -Result $envelope | Out-Null
        return $envelope
    }

    $artifactsDir = New-CapabilityArtifactsDir -RunContext $RunContext -CapabilityId $Capability.Id
    $runOutcome = & $Capability.Fn.Run $RunContext $Fingerprint $artifactsDir $Options
    $finishedAt = Get-Date

    $envelope = New-ResultEnvelope -CapabilityId $Capability.Id -InterfaceVersion $Capability.InterfaceVersion `
        -Tier $Capability.Tier -Status $runOutcome.Status -StartedAtUtc $startedAt -FinishedAtUtc $finishedAt `
        -Reason $runOutcome.Reason -RequiredContext $null -Summary $runOutcome.Summary `
        -Artifacts $runOutcome.Artifacts -SubResults $runOutcome.SubResults
    Save-CapabilityResult -RunContext $RunContext -Result $envelope | Out-Null
    return $envelope
}

Export-ModuleMember -Function Invoke-Capability
