<#
    zero-touch-autonomous-engineering (uia-driver): event subscription, wait, and
    unsubscription. Live events use Automation.AddAutomationEventHandler with a
    pwsh scriptblock-to-delegate callback that sets a per-driver EventState flag;
    Wait-UiaEvent pumps queued messages with a WinForms DoEvents loop so UIA
    delivers the event on this thread. Mock elements fire the same flags through
    Invoke-UiaPattern so the subscription/wait/timeout logic is testable headless.
#>

function Register-UiaEvent {
    param($Driver, [Parameter(Mandatory)][string] $EventName, $Element, [string] $Scope = 'Descendants')
    if ($EventName -notin $script:UiaEventIdResolver.Keys) {
        throw "Unknown UIA event '$EventName' (supported: $($script:UiaEventIdResolver.Keys -join ', '))"
    }
    if (-not $Element) { $Element = Get-UiaRootElement -Driver $Driver }
    $id = [guid]::NewGuid().ToString('N')
    if (-not $Driver.EventState) { $Driver.EventState = @{} }
    $Driver.EventState[$id] = $false

    $descriptor = [pscustomobject]@{
        id = $id
        eventName = $EventName
        element = $Element
        scope = $Scope
        isMock = (Test-UiaMockElement $Element)
        handler = $null
    }

    if (-not $descriptor.isMock) {
        Initialize-UiaManagedApi
        $capturedId = $id
        $capturedState = $Driver.EventState
        $eventObj = & $script:UiaEventIdResolver[$EventName]
        $scopeObj = [System.Windows.Automation.TreeScope]::$Scope
        $handler = [System.Windows.Automation.AutomationEventHandler]{
            param($sender, $eventArgs) $capturedState[$capturedId] = $true
        }
        [System.Windows.Automation.Automation]::AddAutomationEventHandler($eventObj, $Element, $scopeObj, $handler)
        $descriptor.handler = $handler
    } else {
        if ($EventName -eq 'Invoked') { $Driver.EventState[$id] = $false }
        if ($EventName -eq 'SelectionChanged') { $Driver.EventState[$id] = $false }
    }

    [void]$Driver.Events.Add($descriptor)
    return $id
}

function Wait-UiaEvent {
    param($Driver, [Parameter(Mandatory)][string] $Id, [int] $TimeoutMs)
    $timeout = Resolve-UiaTimeout -Driver $Driver -TimeoutMs $TimeoutMs
    $descriptor = $Driver.Events | Where-Object { $_.id -eq $Id } | Select-Object -First 1
    if (-not $descriptor) { throw "No registered event with id '$Id'" }

    $pump = {
        try {
            if (-not $script:UiaWinFormsLoaded) {
                Add-Type -AssemblyName System.Windows.Forms
                $script:UiaWinFormsLoaded = $true
            }
            [System.Windows.Forms.Application]::DoEvents()
        } catch { }
    }

    $stateRef = $Driver.EventState
    $eventId = $Id
    $ok = Wait-UiaCondition -Driver $Driver -TimeoutMs $timeout -ScriptBlock {
        if ($stateRef.ContainsKey($eventId) -and $stateRef[$eventId]) { return $true }
        return $false
    } -Pump $pump
    return $ok
}

function Unregister-UiaEvent {
    param($Driver, [Parameter(Mandatory)][string] $Id)
    $descriptor = $Driver.Events | Where-Object { $_.id -eq $Id } | Select-Object -First 1
    if (-not $descriptor) { throw "No registered event with id '$Id'" }
    if (-not $descriptor.isMock -and $descriptor.handler) {
        Initialize-UiaManagedApi
        $eventObj = & $script:UiaEventIdResolver[$descriptor.eventName]
        $scopeObj = [System.Windows.Automation.TreeScope]::$($descriptor.scope)
        try {
            [System.Windows.Automation.Automation]::RemoveAutomationEventHandler($eventObj, $descriptor.element, $descriptor.handler)
        } catch { }
    }
    $Driver.Events.Remove($descriptor) | Out-Null
    return $true
}
