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

# Pattern-scoped UIA property ids live as static fields on their pattern
# types (e.g. ScrollPattern.VerticallyScrollableProperty), not on
# AutomationElement. Built lazily after the managed API assembly is loaded.
# (A plain `$script:` variable cannot be null-tested in a module scope before
# its first assignment, so the map is accessed via Get-Variable/Set-Variable.)
function Get-UiaPatternPropertyId {
    param([string] $Name)
    if (-not (Get-Variable -Name UiaPatternPropertyMap -Scope Script -ErrorAction SilentlyContinue)) {
        Initialize-UiaManagedApi
        Set-Variable -Name UiaPatternPropertyMap -Scope Script -Value @{
            'HorizontalScrollPercent'  = [System.Windows.Automation.ScrollPattern]::HorizontalScrollPercentProperty
            'VerticalScrollPercent'    = [System.Windows.Automation.ScrollPattern]::VerticalScrollPercentProperty
            'HorizontalViewSize'       = [System.Windows.Automation.ScrollPattern]::HorizontalViewSizeProperty
            'VerticalViewSize'         = [System.Windows.Automation.ScrollPattern]::VerticalViewSizeProperty
            'HorizontallyScrollable'   = [System.Windows.Automation.ScrollPattern]::HorizontallyScrollableProperty
            'VerticallyScrollable'     = [System.Windows.Automation.ScrollPattern]::VerticallyScrollableProperty
            'CanSelectMultiple'        = [System.Windows.Automation.SelectionPattern]::CanSelectMultipleProperty
            'IsSelectionRequired'      = [System.Windows.Automation.SelectionPattern]::IsSelectionRequiredProperty
            'IsSelected'               = [System.Windows.Automation.SelectionItemPattern]::IsSelectedProperty
            'ToggleState'              = [System.Windows.Automation.TogglePattern]::ToggleStateProperty
        }
    }
    $map = Get-Variable -Name UiaPatternPropertyMap -Scope Script -ValueOnly
    return $map[$Name]
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
                # UIA property ids live on different classes: scroll/selection
                # value properties are static fields of their pattern types
                # (e.g. ScrollPattern.VerticallyScrollableProperty), while
                # element-level ids are static fields of AutomationElement
                # (e.g. IsKeyboardFocusableProperty). Resolve the id against
                # the pattern map first, then the AutomationElement fields,
                # then a real property (wrapper types).
                $propId = $null
                $patternProp = Get-UiaPatternPropertyId -Name $Name
                if ($null -ne $patternProp) { $propId = $patternProp }
                if (-not $propId) {
                    $type = $Element.GetType()
                    $flags = [System.Reflection.BindingFlags]::Public -bor [System.Reflection.BindingFlags]::Static
                    $propId = $type.GetField("$Name" + 'Property', $flags)
                    if (-not $propId) { $propId = $type.GetProperty("$Name" + 'Property', $flags) }
                    if ($propId) { $propId = $propId.GetValue($null) }
                }
                if ($propId) {
                    return $Element.GetCurrentPropertyValue($propId)
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

