<#
    Tier-2 UIA validation registration (task 8.4). Wires precise SKIPPED(reason)
    for a missing interactive context or a missing UI: the availability probe
    requires an interactive desktop session, and the run SKIPs when the FastFiles
    UI process is not present. There is deliberately no pixel-diff fallback - the
    UIA driver itself (task 8.1) is the follow-on capability work.
#>

function Test-UiAutomationValidationAvailability {
    param([Parameter(Mandatory)] $Fingerprint)
    if (-not $IsWindows) {
        return [pscustomobject]@{ Available = $false; Reason = 'windows-required'; RequiredContext = [pscustomobject]@{ needs = 'Windows UI Automation' } }
    }
    if ($Fingerprint.SessionId -eq 0 -or $Fingerprint.SessionKind -eq 'session-0' -or $Fingerprint.SessionKind -eq 'services') {
        return [pscustomobject]@{ Available = $false; Reason = 'requires-interactive-context'; RequiredContext = [pscustomobject]@{ needs = 'Interactive desktop session with UI Automation available' } }
    }
    if (-not [Environment]::UserInteractive) {
        return [pscustomobject]@{ Available = $false; Reason = 'requires-interactive-context'; RequiredContext = [pscustomobject]@{ needs = 'Interactive desktop session with UI Automation available' } }
    }
    return [pscustomobject]@{ Available = $true; Reason = $null; RequiredContext = $null }
}

function Get-UiAutomationValidationDiagnostics {
    return @('ui-process-present', 'column-view-elements', 'selection-and-keyboard-navigation', 'dialog-verification')
}

function Invoke-UiAutomationValidationCapability {
    param($RunContext, $Fingerprint, [string] $ArtifactsDir, [hashtable] $Options)

    $uiProcess = Get-Process -Name 'FastFiles' -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $uiProcess) {
        return [pscustomobject]@{
            Status = 'SKIPPED'
            Reason = 'ui-not-running'
            Summary = 'The FastFiles UI process is not running in this session; UIA validation has nothing to attach to'
            Artifacts = @()
            SubResults = @([pscustomobject]@{ id = 'ui-process-present'; tier = 2; status = 'SKIPPED'; reason = 'ui-not-running'; requiredContext = $null; durationMs = 0; detail = 'launch the UI first (blocked on task 8.1 UIA driver for unattended launch)'; diagnostics = @() })
        }
    }

    [pscustomobject]@{
        Status = 'SKIPPED'
        Reason = 'uia-driver-not-implemented'
        Summary = "UI process '$($uiProcess.ProcessName)' (pid $($uiProcess.Id)) is running, but the UIA driver (task 8.1) is not implemented yet; navigation/selection/dialog verification remains"
        Artifacts = @()
        SubResults = @([pscustomobject]@{ id = 'ui-process-present'; tier = 2; status = 'PASS'; reason = $null; requiredContext = $null; durationMs = 0; detail = "pid=$($uiProcess.Id)"; diagnostics = @() })
    }
}

Export-ModuleMember -Function Test-UiAutomationValidationAvailability, Invoke-UiAutomationValidationCapability, Get-UiAutomationValidationDiagnostics
