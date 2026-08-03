<#
    zero-touch-autonomous-engineering (uia-driver): recorded-tree (mock) element
    support for headless tests. A recorded tree is a JSON file:
    { "root": "<id>", "nodes": [ { "id", "name", "automationId", "controlType",
    "className", "processId", "isEnabled", "isOffscreen", "patterns": [],
    "children": [] } ] }.
#>

function New-UiaMockElement {
    param($Driver, [string]$Id)
    return [pscustomobject]@{ _isMock = $true; _driver = $Driver; _id = $Id }
}

function Import-UiaRecordedTree {
    param([Parameter(Mandatory)] $Driver, [Parameter(Mandatory)] [string] $Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Recorded tree not found: $Path"
    }
    $tree = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    if (-not $tree.root) { throw "Recorded tree at $Path has no root id" }
    if (-not $tree.nodes) { throw "Recorded tree at $Path has no nodes" }
    $map = @{}
    foreach ($node in $tree.nodes) {
        $map[$node.id] = [pscustomobject]@{
            id = $node.id
            name = $(if ($node.PSObject.Properties['name']) { $node.name } else { $null })
            automationId = $(if ($node.PSObject.Properties['automationId']) { $node.automationId } else { $null })
            controlType = $(if ($node.PSObject.Properties['controlType']) { $node.controlType } else { $null })
            className = $(if ($node.PSObject.Properties['className']) { $node.className } else { $null })
            processId = $(if ($node.PSObject.Properties['processId']) { $node.processId } else { 0 })
            isEnabled = $(if ($node.PSObject.Properties['isEnabled']) { $node.isEnabled } else { $true })
            isOffscreen = $(if ($node.PSObject.Properties['isOffscreen']) { $node.isOffscreen } else { $false })
            patterns = @(if ($node.PSObject.Properties['patterns']) { $node.patterns } else { @() })
            clickablePoint = $(if ($node.PSObject.Properties['clickablePoint']) { $node.clickablePoint } else { $null })
            boundingRectangle = $(if ($node.PSObject.Properties['boundingRectangle']) { $node.boundingRectangle } else { $null })
            children = @(if ($node.PSObject.Properties['children']) { $node.children } else { @() })
        }
    }
    if (-not $map.ContainsKey($tree.root)) { throw "Recorded tree root '$($tree.root)' has no node definition" }
    $Driver.MockNodes = $map
    $Driver.Root = New-UiaMockElement -Driver $Driver -Id $tree.root
    return $Driver
}

function Get-UiaMockChildren {
    param($Driver, $Element)
    $rec = Get-UiaMockRecord -Driver $Driver -Element $Element
    $out = @()
    foreach ($childId in $rec.children) {
        if ($Driver.MockNodes.ContainsKey($childId)) {
            $out += New-UiaMockElement -Driver $Driver -Id $childId
        }
    }
    return $out
}

function Get-UiaMockParent {
    param($Driver, $Element)
    $id = $Element._id
    foreach ($node in $Driver.MockNodes.Values) {
        foreach ($childId in $node.children) {
            if ($childId -eq $id) {
                return New-UiaMockElement -Driver $Driver -Id $node.id
            }
        }
    }
    return $null
}

function Get-UiaMockProperty {
    param($Driver, $Element, [string]$Name)
    $rec = Get-UiaMockRecord -Driver $Driver -Element $Element
    switch ($Name) {
        'Name' { return $rec.name }
        'AutomationId' { return $rec.automationId }
        'ControlType' { return $rec.controlType }
        'ClassName' { return $rec.className }
        'ProcessId' { return $rec.processId }
        'IsEnabled' { return $rec.isEnabled }
        'IsOffscreen' { return $rec.isOffscreen }
        'SupportedPatterns' { return $rec.patterns }
        'BoundingRectangle' { return $rec.boundingRectangle }
        'ClickablePoint' { return $rec.clickablePoint }
        default { return $null }
    }
}

function Test-UiaMockPattern {
    param($Driver, $Element, [string]$PatternName)
    $rec = Get-UiaMockRecord -Driver $Driver -Element $Element
    return ($rec.patterns -contains $PatternName)
}

function Get-UiaMockClickablePoint {
    param($Driver, $Element)
    $rec = Get-UiaMockRecord -Driver $Driver -Element $Element
    if ($rec.clickablePoint) {
        return [pscustomobject]@{ x = [int]$rec.clickablePoint.x; y = [int]$rec.clickablePoint.y }
    }
    if ($rec.boundingRectangle) {
        $br = $rec.boundingRectangle
        return [pscustomobject]@{ x = [int]$br.x + [int][Math]::Floor([int]$br.width / 2); y = [int]$br.y + [int][Math]::Floor([int]$br.height / 2) }
    }
    return $null
}
