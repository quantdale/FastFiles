<#
    Tier-2 UIA validation registration (task 8.4). Wires precise SKIPPED(reason)
    for a missing interactive context or a missing UI: the availability probe
    requires an interactive desktop session and a loadable UIA driver, and the run
    SKIPs when the FastFiles UI process is not present. There is deliberately no
    pixel-diff fallback - the uia-driver module (zero-touch-autonomous-engineering
    task 2.x) backs the e2e scenarios that land in task 3.x.
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
    $driverModule = Join-Path (Split-Path $PSScriptRoot -Parent) 'uia-driver\UiaDriver.psm1'
    if (Test-Path -LiteralPath $driverModule) {
        try {
            Import-Module $driverModule -Force
            $probe = Get-UiaDriverAvailability
            if (-not $probe.Available) {
                return [pscustomobject]@{ Available = $false; Reason = $probe.Reason; RequiredContext = $probe.RequiredContext }
            }
        } catch {
            return [pscustomobject]@{ Available = $false; Reason = 'uia-driver-unavailable'; RequiredContext = [pscustomobject]@{ needs = 'verify/uia-driver module loadable' } }
        }
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
            SubResults = @([pscustomobject]@{ id = 'ui-process-present'; tier = 2; status = 'SKIPPED'; reason = 'ui-not-running'; requiredContext = $null; durationMs = 0; detail = 'launch the UI first (e2e launch scenarios land in task 3.x)'; diagnostics = @() })
        }
    }

    [pscustomobject]@{
        Status = 'SKIPPED'
        Reason = 'e2e-scenarios-not-implemented'
        Summary = "UI process '$($uiProcess.ProcessName)' (pid $($uiProcess.Id)) is running; the uia-driver module (tasks 2.1-2.4) is landed, but the driver-backed launch/navigation/selection/dialog e2e scenarios land in task 3.x"
        Artifacts = @()
        SubResults = @([pscustomobject]@{ id = 'ui-process-present'; tier = 2; status = 'PASS'; reason = $null; requiredContext = $null; durationMs = 0; detail = "pid=$($uiProcess.Id)"; diagnostics = @() })
    }
}

Export-ModuleMember -Function Test-UiAutomationValidationAvailability, Invoke-UiAutomationValidationCapability, Get-UiAutomationValidationDiagnostics
