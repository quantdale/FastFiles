<#
    zero-touch-autonomous-engineering (uia-driver): diagnostic UIA tree dumps
    (machine-readable JSON + indented text) and the structured fail-clearly error
    contract when a semantic target is missing. Dumps are bounded by
    MaxTreeDepth/MaxTreeWidth and the driver TimeoutMs so they never block
    indefinitely.
#>

function ConvertTo-UiaDumpNode {
    param($Driver, $Element, $Level, $Watch, $State)
    $node = [ordered]@{}
    $identity = Get-UiaElementIdentity -Driver $Driver -Element $Element
    $node['name'] = $identity.Name
    $node['automationId'] = $identity.AutomationId
    $node['controlType'] = $identity.ControlType
    $node['className'] = $identity.ClassName
    $node['processId'] = $identity.ProcessId
    $node['isEnabled'] = Get-UiaElementProperty -Driver $Driver -Element $Element -Name 'IsEnabled'
    $node['isOffscreen'] = Get-UiaElementProperty -Driver $Driver -Element $Element -Name 'IsOffscreen'
    $patterns = Get-UiaElementProperty -Driver $Driver -Element $Element -Name 'SupportedPatterns'
    $node['patterns'] = @($patterns | ForEach-Object { $_.ToString() -replace '^ControlPattern\.', '' })
    $children = @()
    $limit = Get-UiaDriverSetting -Driver $Driver -Name 'MaxTreeWidth' -Fallback 2000
    if ($Level -lt $State.Depth -and $State.Nodes -lt $limit -and $Watch.ElapsedMilliseconds -lt $State.TimeoutMs) {
        foreach ($child in @(Get-UiaChildren -Driver $Driver -Element $Element -View $State.View)) {
            $State.Nodes++
            if ($State.Nodes -gt $limit -or $Watch.ElapsedMilliseconds -ge $State.TimeoutMs) { $State.Truncated = $true; break }
            $children += ConvertTo-UiaDumpNode -Driver $Driver -Element $child -Level ($Level + 1) -Watch $Watch -State $State
        }
    } else {
        $State.Truncated = $true
    }
    $node['children'] = $children
    return [pscustomobject]$node
}

function Get-UiaTreeDump {
    param($Driver, $Element, [string]$View)
    if (-not $Element) { $Element = Get-UiaRootElement -Driver $Driver }
    $view = Resolve-UiaView -Driver $Driver -View $View
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $timeout = Get-UiaDriverSetting -Driver $Driver -Name 'TimeoutMs' -Fallback 5000
    $depth = Get-UiaDriverSetting -Driver $Driver -Name 'MaxTreeDepth' -Fallback 30
    $state = [pscustomobject]@{ Nodes = 1; Depth = $depth; TimeoutMs = $timeout; View = $view; Truncated = $false }
    $rootNode = ConvertTo-UiaDumpNode -Driver $Driver -Element $Element -Level 0 -Watch $watch -State $state
    $json = [pscustomobject]@{
        generatedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
        provider = $Driver.Provider
        view = $view
        nodeCount = $state.Nodes
        truncated = $state.Truncated
        root = $rootNode
    } | ConvertTo-Json -Depth 20
    $text = ConvertTo-UiaDumpText -RootNode $rootNode -Truncated $state.Truncated
    return [pscustomobject]@{ json = $json; text = $text; nodeCount = $state.Nodes; truncated = $state.Truncated }
}

function ConvertTo-UiaDumpText {
    param($RootNode, [bool] $Truncated)
    $sb = [System.Text.StringBuilder]::new()
    $indent = '  '
    $walk = {
        param($Node, $Level)
        $label = $Node.controlType
        $parts = @()
        if ($Node.name) { $parts += "`"$($Node.name)`"" }
        if ($Node.automationId) { $parts += "AutomationId=$($Node.automationId)" }
        if ($Node.className) { $parts += "ClassName=$($Node.className)" }
        if ($Node.processId) { $parts += "pid=$($Node.processId)" }
        if ($Node.patterns -and $Node.patterns.Count -gt 0) { $parts += "patterns=$($Node.patterns -join ',')" }
        $line = ($indent * $Level) + $label
        if ($parts.Count -gt 0) { $line += ' ' + ($parts -join ' ') }
        [void]$sb.AppendLine($line)
        foreach ($child in $Node.children) {
            & $walk -Node $child -Level ($Level + 1)
        }
    }
    & $walk -Node $RootNode -Level 0
    if ($Truncated) { [void]$sb.AppendLine('... (dump truncated by depth/width/timeout bounds)') }
    return $sb.ToString().TrimEnd()
}

function Export-UiaTreeDump {
    param($Driver, $Element, [string]$JsonPath, [string]$TextPath, [string]$BaseName, [string]$Directory)
    $dump = Get-UiaTreeDump -Driver $Driver -Element $Element
    $jsonPath = $JsonPath
    $textPath = $TextPath
    if (-not $jsonPath -and $BaseName) {
        $dir = $Directory
        if (-not $dir) { $dir = (Get-Location).Path }
        if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
        $jsonPath = Join-Path $dir "$BaseName.tree.json"
        $textPath = Join-Path $dir "$BaseName.tree.txt"
    }
    if ($jsonPath) { $dump.json | Set-Content -LiteralPath $jsonPath -Encoding utf8 }
    if ($textPath) { $dump.text | Set-Content -LiteralPath $textPath -Encoding utf8 }
    return [pscustomobject]@{ jsonPath = $jsonPath; textPath = $textPath; nodeCount = $dump.nodeCount; truncated = $dump.truncated }
}

function New-UiaTargetNotFoundError {
    param($Driver, [hashtable] $Criteria, $FromElement, [string]$View, [string]$DumpBaseName, [string]$DumpDirectory)
    $timeout = Get-UiaDriverSetting -Driver $Driver -Name 'TimeoutMs' -Fallback 5000
    $from = Format-UiaElementIdentity -Driver $Driver -Element $FromElement
    $criteriaJson = $(if ($Criteria) { $Criteria | ConvertTo-Json -Compress } else { '{}' })
    $message = "UiaTargetNotFound: criteria $criteriaJson not found within ${timeout}ms from [$from]"
    $dumpPath = $null
    if ($DumpBaseName) {
        $exported = Export-UiaTreeDump -Driver $Driver -Element $FromElement -BaseName $DumpBaseName -Directory $DumpDirectory
        $dumpPath = $exported.jsonPath
    }
    return [pscustomobject]@{
        type = 'UiaTargetNotFound'
        criteria = $Criteria
        view = $(if ($View) { $View } else { 'Control' })
        timeoutMs = $timeout
        searchedFrom = $from
        message = $message
        treeDumpPath = $dumpPath
    }
}
