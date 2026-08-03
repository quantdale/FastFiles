<#
    Tier-3 stress capability registration (task 7.6). Declares the stress scenario
    surface and registers it with an availability probe that always reports
    context-absent until a disposable Tier-3 execution host (VM/Sandbox/runner)
    exists - the scheduler then records SKIPPED(context-absent), never a silent
    pass. No scenario executes on this host by design.
#>

function Test-StressValidationAvailability {
    param([Parameter(Mandatory)] $Fingerprint)
    return [pscustomobject]@{
        Available = $false
        Reason = 'requires-tier-3-context'
        RequiredContext = [pscustomobject]@{ tier = 3; needs = 'Disposable isolated host (Hyper-V/VMware/VirtualBox/Windows Sandbox/GitHub Actions runner) with FastFiles installed' }
    }
}

function Get-StressValidationDiagnostics {
    return @('startup-shutdown-cycles', 'reconnect-storms', 'pipe-pressure', 'snapshot-generation-pressure', 'large-filesystem-traversal', 'long-indexing-sessions', 'memory-pressure', 'cpu-pressure')
}

function Invoke-StressValidationCapability {
    param($RunContext, $Fingerprint, [string] $ArtifactsDir, [hashtable] $Options)
    [pscustomobject]@{
        Status = 'SKIPPED'
        Reason = 'requires-tier-3-context'
        Summary = 'Stress scenarios are registered but gated to a Tier-3 disposable context; nothing was executed'
        Artifacts = @()
        SubResults = @()
    }
}

Export-ModuleMember -Function Test-StressValidationAvailability, Invoke-StressValidationCapability, Get-StressValidationDiagnostics
