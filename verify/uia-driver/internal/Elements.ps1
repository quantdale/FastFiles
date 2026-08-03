<#
    zero-touch-autonomous-engineering (uia-driver): element identity, property
    reads, and tree traversal (Control and Raw views) over either live managed
    UIA elements or recorded-tree mock elements.
#>

function Get-UiaRootElement {
    param($Driver)
    if ($Driver.Provider -eq 'mock' -or (Test-UiaMockElement $Driver.Root)) {
        return $Driver.Root
    }
    Initialize-UiaManagedApi
    return [System.Windows.Automation.AutomationElement]::RootElement
}

function Get-UiaChildren {
    param($Driver, $Element, [string]$View)
    if (Test-UiaMockElement $Element) {
        return @(Get-UiaMockChildren -Driver $Driver -Element $Element)
    }
    Initialize-UiaManagedApi
    $view = Resolve-UiaView -Driver $Driver -View $View
    if ($view -eq 'Raw') {
        $walker = [System.Windows.Automation.TreeWalker]::RawViewWalker
        $children = @()
        $child = $walker.GetFirstChild($Element)
        while ($null -ne $child) {
            $children += $child
            $child = $walker.GetNextSibling($child)
        }
        return $children
    }
    return @($Element.FindAll([System.Windows.Automation.TreeScope]::Children, [System.Windows.Automation.Condition]::TrueCondition))
}

function Get-UiaParent {
    param($Driver, $Element)
    if (Test-UiaMockElement $Element) {
        return Get-UiaMockParent -Driver $Driver -Element $Element
    }
    Initialize-UiaManagedApi
    try {
        $walker = [System.Windows.Automation.TreeWalker]::ControlViewWalker
        return $walker.GetParent($Element)
    } catch {
        return $null
    }
}

function Get-UiaElementProperty {
    param($Driver, $Element, [Parameter(Mandatory)][string] $Name)
    if (Test-UiaMockElement $Element) {
        return Get-UiaMockProperty -Driver $Driver -Element $Element -Name $Name
    }
    Initialize-UiaManagedApi
    try {
        switch ($Name) {
            'Name' { return $Element.Current.Name }
            'AutomationId' { return $Element.Current.AutomationId }
            'ControlType' { return Format-UiaControlType $Element.Current.ControlType.ProgrammaticName }
            'ClassName' { return $Element.Current.ClassName }
            'ProcessId' { return $Element.Current.ProcessId }
            'IsEnabled' { return $Element.Current.IsEnabled }
            'IsOffscreen' { return $Element.Current.IsOffscreen }
            'BoundingRectangle' {
                $rect = $Element.Current.BoundingRectangle
                return [pscustomobject]@{ x = [int]$rect.X; y = [int]$rect.Y; width = [int]$rect.Width; height = [int]$rect.Height }
            }
            'SupportedPatterns' {
                return @($Element.GetSupportedPatterns() | ForEach-Object { $_.ProgrammaticName -replace '^ControlPattern\.', '' })
            }
            default {
                $propId = $Element.GetType().GetProperty("$Name" + 'Property')
                if ($propId) {
                    return $Element.GetCurrentPropertyValue($propId.GetValue($null))
                }
                return $null
            }
        }
    } catch {
        return $null
    }
}

function Test-UiaElementMatches {
    param(
        $Element,
        [string]$AutomationId,
        [string]$Name,
        [string]$NameLike,
        [string]$ControlType,
        [string]$ClassName,
        [int]$ProcessId,
        [switch]$Not
    )
    $ok = $true
    if ($AutomationId -and $Element.AutomationId -ne $AutomationId) { $ok = $false }
    if ($ok -and $Name -and $Element.Name -ne $Name) { $ok = $false }
    if ($ok -and $NameLike -and $Element.Name -notlike $NameLike) { $ok = $false }
    if ($ok -and $ControlType -and $Element.ControlType -ne $ControlType) { $ok = $false }
    if ($ok -and $ClassName -and $Element.ClassName -ne $ClassName) { $ok = $false }
    if ($ok -and $ProcessId -gt 0 -and $Element.ProcessId -ne $ProcessId) { $ok = $false }
    if ($Not) { return -not $ok }
    return $ok
}

function Wait-UiaCondition {
    param(
        $Driver,
        [Parameter(Mandatory)][scriptblock] $ScriptBlock,
        [int] $TimeoutMs,
        [int] $PollIntervalMs,
        [scriptblock] $Pump = $null
    )
    $timeout = Resolve-UiaTimeout -Driver $Driver -TimeoutMs $TimeoutMs
    $poll = $PollIntervalMs
    if ($poll -le 0) { $poll = Get-UiaDriverSetting -Driver $Driver -Name 'PollIntervalMs' -Fallback 50 }
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    while ($watch.ElapsedMilliseconds -lt $timeout) {
        $result = & $ScriptBlock
        if ($result) { return $true }
        if ($Pump) { & $Pump }
        Start-Sleep -Milliseconds $poll
    }
    return $false
}

function Find-UiaElement {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] $Driver,
        [string] $AutomationId,
        [string] $Name,
        [string] $NameLike,
        [string] $ControlType,
        [string] $ClassName,
        [int] $ProcessId,
        [string] $View,
        [int] $TimeoutMs,
        $FromElement,
        [switch] $All
    )
    $root = if ($FromElement) { $FromElement } else { Get-UiaRootElement -Driver $Driver }
    $criteria = @{}
    if ($AutomationId) { $criteria['AutomationId'] = $AutomationId }
    if ($Name) { $criteria['Name'] = $Name }
    if ($NameLike) { $criteria['NameLike'] = $NameLike }
    if ($ControlType) { $criteria['ControlType'] = $ControlType }
    if ($ClassName) { $criteria['ClassName'] = $ClassName }
    if ($ProcessId -gt 0) { $criteria['ProcessId'] = $ProcessId }
    if ($criteria.Count -eq 0) {
        throw "Find-UiaElement requires at least one identity criterion"
    }
    $view = Resolve-UiaView -Driver $Driver -View $View
    $timeout = Resolve-UiaTimeout -Driver $Driver -TimeoutMs $TimeoutMs
    $width = Get-UiaDriverSetting -Driver $Driver -Name 'MaxTreeWidth' -Fallback 2000
    $depth = Get-UiaDriverSetting -Driver $Driver -Name 'MaxTreeDepth' -Fallback 30
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $found = @()
    $visited = 0
    $truncated = $false
    $stack = [System.Collections.Stack]::new()
    $stack.Push([pscustomobject]@{ e = $root; lvl = 0 })
    while ($stack.Count -gt 0 -and $watch.ElapsedMilliseconds -lt $timeout) {
        $frame = $stack.Pop()
        $visited++
        if ($visited -gt $width) { $truncated = $true; break }
        $identity = Get-UiaElementIdentity -Driver $Driver -Element $frame.e
        if (Test-UiaElementMatches -Element $identity -AutomationId $AutomationId -Name $Name -NameLike $NameLike -ControlType $ControlType -ClassName $ClassName -ProcessId $ProcessId) {
            $found += $frame.e
            if (-not $All) { break }
        }
        if ($frame.lvl -lt $depth) {
            $children = @(Get-UiaChildren -Driver $Driver -Element $frame.e -View $view)
            for ($i = $children.Count - 1; $i -ge 0; $i--) {
                $stack.Push([pscustomobject]@{ e = $children[$i]; lvl = $frame.lvl + 1 })
            }
        }
    }
    if ($found.Count -eq 0) {
        $searched = Format-UiaElementIdentity -Driver $Driver -Element $root
        $suffix = $(if ($truncated) { ' (tree width cap reached)' } else { '' })
        throw [UiaOperationTimeoutException]::new(
            "UiaTargetNotFound: criteria $($criteria | ConvertTo-Json -Compress) not found within ${timeout}ms from [$searched]$suffix",
            $criteria, $root, $view, $timeout)
    }
    if ($All) { return $found }
    return $found[0]
}

