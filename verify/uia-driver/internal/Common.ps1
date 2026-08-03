<#
    zero-touch-autonomous-engineering (uia-driver): error contract classes and
    shared element helpers. Classes are guarded so module re-import in one session
    does not throw "type already exists".
#>

if (-not ('UiaOperationTimeoutException' -as [type])) {
    class UiaOperationTimeoutException : System.Exception {
        [hashtable]$Criteria
        [object]$SearchedFrom
        [string]$View
        [int]$TimeoutMs
        UiaOperationTimeoutException([string]$message, [hashtable]$criteria, [object]$searchedFrom, [string]$view, [int]$timeoutMs)
            : base($message) {
            $this.Criteria = $criteria
            $this.SearchedFrom = $searchedFrom
            $this.View = $view
            $this.TimeoutMs = $timeoutMs
        }
    }
}

if (-not ('UiaPatternNotSupportedException' -as [type])) {
    class UiaPatternNotSupportedException : System.Exception {
        [string]$PatternName
        UiaPatternNotSupportedException([string]$message, [string]$patternName) : base($message) {
            $this.PatternName = $patternName
        }
    }
}

if (-not ('UiaDriverUnavailableException' -as [type])) {
    class UiaDriverUnavailableException : System.Exception {
        [string]$Reason
        UiaDriverUnavailableException([string]$message, [string]$reason) : base($message) {
            $this.Reason = $reason
        }
    }
}

function Initialize-UiaManagedApi {
    if (-not $script:UiaManagedApiLoaded) {
        Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
        $script:UiaManagedApiLoaded = $true
    }
}

function Test-UiaMockElement {
    param($Element)
    return ($null -ne $Element -and $Element.PSObject.Properties['_isMock'] -and $Element._isMock -eq $true)
}

function Get-UiaMockRecord {
    param($Driver, $Element)
    if (-not (Test-UiaMockElement $Element)) { return $null }
    return $Driver.MockNodes[$Element._id]
}

function Format-UiaControlType {
    param($ControlType)
    if ($null -eq $ControlType) { return $null }
    $name = $ControlType.ToString()
    if ($name.StartsWith('ControlType.')) { $name = $name.Substring('ControlType.'.Length) }
    return $name
}

function Get-UiaElementIdentity {
    param($Driver, $Element)
    if (Test-UiaMockElement $Element) {
        $rec = Get-UiaMockRecord -Driver $Driver -Element $Element
        return [pscustomobject]@{
            Name = $rec.name
            AutomationId = $rec.automationId
            ControlType = $rec.controlType
            ClassName = $rec.className
            ProcessId = $rec.processId
        }
    }
    Initialize-UiaManagedApi
    try {
        $ct = $null
        if ($null -ne $Element.Current.ControlType) { $ct = Format-UiaControlType $Element.Current.ControlType.ProgrammaticName }
        return [pscustomobject]@{
            Name = $Element.Current.Name
            AutomationId = $Element.Current.AutomationId
            ControlType = $ct
            ClassName = $Element.Current.ClassName
            ProcessId = $Element.Current.ProcessId
        }
    } catch {
        return [pscustomobject]@{ Name = $null; AutomationId = $null; ControlType = $null; ClassName = $null; ProcessId = $null }
    }
}

function Format-UiaElementIdentity {
    param($Driver, $Element)
    $id = Get-UiaElementIdentity -Driver $Driver -Element $Element
    $parts = @()
    if ($id.Name) { $parts += "`"$($id.Name)`"" }
    if ($id.AutomationId) { $parts += "AutomationId=$($id.AutomationId)" }
    if ($id.ControlType) { $parts += $id.ControlType }
    if ($id.ClassName) { $parts += "ClassName=$($id.ClassName)" }
    if ($id.ProcessId) { $parts += "pid=$($id.ProcessId)" }
    return $parts -join ' '
}

function Get-UiaDriverSetting {
    param($Driver, [string]$Name, $Fallback)
    if ($Driver.PSObject.Properties[$Name]) { return $Driver.$Name }
    return $Fallback
}

function Resolve-UiaView {
    param($Driver, [string]$View)
    if ($View) { return $View }
    return Get-UiaDriverSetting -Driver $Driver -Name 'View' -Fallback 'Control'
}

function Resolve-UiaTimeout {
    param($Driver, $TimeoutMs)
    if ($null -ne $TimeoutMs -and $TimeoutMs -gt 0) { return $TimeoutMs }
    return Get-UiaDriverSetting -Driver $Driver -Name 'TimeoutMs' -Fallback 5000
}
