<#
    zero-touch-autonomous-engineering (uia-driver): reusable UI Automation driver.

    Semantic-first UI discovery and control: elements are located by UIA identity
    (Name/AutomationId/ControlType/ClassName/process), never by whole-frame pixel
    matching. Provides tree traversal (Control and Raw views), control patterns
    (Invoke/SelectionItem/Selection/Scroll/Value/Window), event subscriptions,
    SendInput keyboard/pointer as an explicitly-recorded fallback (never a pixel
    verify), per-operation timeouts, and diagnostic tree dumps (JSON + indented
    text). The same logic runs against live managed UIA elements or recorded-tree
    mock elements (see Import-UiaRecordedTree) so the driver is headless-testable.

    Usage:
        $driver = New-UiaDriver
        $window = Find-UiaElement -Driver $driver -AutomationId MainWindow -TimeoutMs 10000
        $button = Find-UiaElement -Driver $driver -Name 'New Column'
        Invoke-UiaElementAction -Driver $driver -Element $button
#>

Set-StrictMode -Version 3.0

$script:UiaManagedApiLoaded = $false
$script:UiaWinFormsLoaded = $false

foreach ($internal in @(
        'Constants.ps1', 'Common.ps1', 'Mock.ps1', 'Elements.ps1',
        'Patterns.ps1', 'Input.ps1', 'Events.ps1', 'Dump.ps1')) {
    . (Join-Path $PSScriptRoot (Join-Path 'internal' $internal))
}

function Get-UiaDriverAvailability {
    <#
        Probe: is a UIA-driveable interactive context available? Returns the
        envelope { Available, Reason, RequiredContext } used by capability
        availability checks. No pixel fallback is ever implied.
    #>
    $isWindows = [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([System.Runtime.InteropServices.OSPlatform]::Windows)
    if (-not $isWindows) {
        return [pscustomobject]@{ Available = $false; Reason = 'windows-required'; RequiredContext = [pscustomobject]@{ needs = 'Windows UI Automation' } }
    }
    if (-not [Environment]::UserInteractive) {
        return [pscustomobject]@{ Available = $false; Reason = 'requires-interactive-context'; RequiredContext = [pscustomobject]@{ needs = 'Interactive desktop session' } }
    }
    $sessionId = (Get-Process -Id $PID).SessionId
    if ($sessionId -eq 0) {
        return [pscustomobject]@{ Available = $false; Reason = 'requires-interactive-context'; RequiredContext = [pscustomobject]@{ needs = 'Interactive desktop session (not session 0)' } }
    }
    try {
        Initialize-UiaManagedApi
        $null = [System.Windows.Automation.AutomationElement]::RootElement
    } catch {
        return [pscustomobject]@{ Available = $false; Reason = 'uia-provider-unavailable'; RequiredContext = [pscustomobject]@{ needs = 'UIAutomationClient/UIAutomationTypes managed assemblies loadable' } }
    }
    return [pscustomobject]@{ Available = $true; Reason = $null; RequiredContext = $null }
}

function New-UiaDriver {
    <#
        Create a driver context. Defaults: TimeoutMs=5000, PollIntervalMs=50,
        MaxTreeDepth=30, MaxTreeWidth=2000, View=Control, Provider=managed.
        For headless tests create -Provider Mock and attach a recorded tree with
        Import-UiaRecordedTree.
    #>
    param(
        [ValidateSet('managed', 'mock')][string] $Provider,
        [int] $TimeoutMs,
        [int] $PollIntervalMs,
        [int] $MaxTreeDepth,
        [int] $MaxTreeWidth,
        [ValidateSet('Control', 'Raw')][string] $View
    )
    $driver = [pscustomobject]@{
        Provider       = $(if ($Provider) { $Provider } else { $script:UiaDriverDefaults.Provider })
        TimeoutMs      = $(if ($TimeoutMs -gt 0) { $TimeoutMs } else { $script:UiaDriverDefaults.TimeoutMs })
        PollIntervalMs = $(if ($PollIntervalMs -gt 0) { $PollIntervalMs } else { $script:UiaDriverDefaults.PollIntervalMs })
        MaxTreeDepth   = $(if ($MaxTreeDepth -gt 0) { $MaxTreeDepth } else { $script:UiaDriverDefaults.MaxTreeDepth })
        MaxTreeWidth   = $(if ($MaxTreeWidth -gt 0) { $MaxTreeWidth } else { $script:UiaDriverDefaults.MaxTreeWidth })
        View           = $(if ($View) { $View } else { $script:UiaDriverDefaults.View })
        Root           = $null
        MockNodes      = @{}
        Events         = [System.Collections.ArrayList]::new()
        EventState     = @{}
    }
    return $driver
}

Export-ModuleMember -Function `
    Get-UiaDriverAvailability, New-UiaDriver, Import-UiaRecordedTree, New-UiaMockElement, Get-UiaMockRecord, `
    Get-UiaRootElement, Find-UiaElement, Get-UiaChildren, Get-UiaParent, `
    Get-UiaElementProperty, Get-UiaElementIdentity, `
    Get-UiaPattern, Invoke-UiaPattern, Test-UiaPatternAvailable, Invoke-UiaElementAction, `
    Get-UiaClickablePoint, Send-UiaInput, Send-UiaMouseInput, `
    Register-UiaEvent, Wait-UiaEvent, Unregister-UiaEvent, `
    Wait-UiaCondition, Get-UiaTreeDump, Export-UiaTreeDump, New-UiaTargetNotFoundError
