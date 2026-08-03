<#
    zero-touch-autonomous-engineering (uia-driver): headless unit tests for the
    driver's identity, traversal, pattern-selection, property-validation, event,
    timeout, and dump logic using a recorded (mock) UIA tree — no interactive
    desktop and no live UI process required. Registered with CTest as
    ffuia_driver_ps_tests. Exit 0 = PASS, non-zero = FAIL.
#>
$ErrorActionPreference = 'Stop'
$script:failures = 0

function Check {
    param([bool] $Condition, [string] $Description)
    if ($Condition) {
        Write-Host "  ok: $Description"
    } else {
        $script:failures++
        Write-Host "FAIL: $Description"
    }
}

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$module = Join-Path $repo 'verify\uia-driver\UiaDriver.psm1'
$treePath = Join-Path $repo 'verify\uia-driver\tests\recorded-trees\sample-files-window.json'
Import-Module $module -Force

Write-Host "== availability probe (2.2) =="
$avail = Get-UiaDriverAvailability
Check ($null -ne $avail) 'availability probe returns an envelope'
Check ($avail.PSObject.Properties['Available'] -ne $null) 'envelope has Available'
Check ($avail.Available -eq $true) 'interactive desktop session reports available'

Write-Host "== driver construction =="
$driver = New-UiaDriver
Check ($driver.TimeoutMs -eq 5000) 'default TimeoutMs'
Check ($driver.Provider -eq 'managed') 'default provider is managed'
$mock = New-UiaDriver -Provider mock -TimeoutMs 1000
Check ($mock.Provider -eq 'mock' -and $mock.TimeoutMs -eq 1000) 'mock driver honors parameters'

Write-Host "== recorded tree import + identity (2.1) =="
$null = Import-UiaRecordedTree -Driver $mock -Path $treePath
$root = Get-UiaRootElement -Driver $mock
$rid = Get-UiaElementIdentity -Driver $mock -Element $root
Check ($rid.Name -eq 'FastFiles') "root identity name, got '$($rid.Name)'"
Check ($rid.AutomationId -eq 'MainWindow') 'root AutomationId'
Check ($rid.ControlType -eq 'Window') 'root ControlType'
Check ($rid.ProcessId -eq 4242) 'root ProcessId'

Write-Host "== find by identity (2.1) =="
$nav1 = Find-UiaElement -Driver $mock -AutomationId 'Nav1'
Check ($nav1 -ne $null) 'find Nav1 by AutomationId'
$nav1Id = Get-UiaElementIdentity -Driver $mock -Element $nav1
Check ($nav1Id.Name -eq 'Pictures') 'Nav1 resolves to Pictures'
$docs = Find-UiaElement -Driver $mock -Name 'Docs'
Check ($docs -ne $null) 'find by Name'
$multi = Find-UiaElement -Driver $mock -AutomationId 'Nav0' -ControlType 'ListItem'
Check ($multi -ne $null) 'multi-criteria find (AutomationId + ControlType)'
$like = Find-UiaElement -Driver $mock -NameLike 'Pict*'
Check ($like -ne $null) 'find by NameLike wildcard'
$all = @(Find-UiaElement -Driver $mock -ControlType 'ListItem' -All)
Check ($all.Count -eq 3) "find -All ListItems returns 3, got $($all.Count)"

Write-Host "== traversal + parent (2.1) =="
$rootKids = @(Get-UiaChildren -Driver $mock -Element $root)
Check ($rootKids.Count -eq 5) "root children count, got $($rootKids.Count)"
$list = Find-UiaElement -Driver $mock -AutomationId 'EntryList'
$listKids = @(Get-UiaChildren -Driver $mock -Element $list)
Check ($listKids.Count -eq 3) "EntryList children count, got $($listKids.Count)"
$parent = Get-UiaParent -Driver $mock -Element $nav1
$parentId = Get-UiaElementIdentity -Driver $mock -Element $parent
Check ($parentId.AutomationId -eq 'EntryList') 'parent of Nav0 is EntryList'
$rawKids = @(Get-UiaChildren -Driver $mock -Element $root -View 'Raw')
Check ($rawKids.Count -eq 5) 'Raw view traversal parity on mock tree'

Write-Host "== properties + patterns (2.1) =="
$search = Find-UiaElement -Driver $mock -AutomationId 'SearchBox'
$searchName = Get-UiaElementProperty -Driver $mock -Element $search -Name 'Name'
Check ($searchName -eq 'Search') 'property read Name'
$patterns = @(Get-UiaElementProperty -Driver $mock -Element $search -Name 'SupportedPatterns')
Check ($patterns -contains 'Value') 'SearchBox exposes Value pattern'
$btn = Find-UiaElement -Driver $mock -AutomationId 'NewColumnButton'
Check (Test-UiaPatternAvailable -Driver $mock -Element $btn -PatternName 'Invoke') 'button exposes Invoke'
Check (Test-UiaPatternAvailable -Driver $mock -Element $nav1 -PatternName 'SelectionItem') 'item exposes SelectionItem'
Check (-not (Test-UiaPatternAvailable -Driver $mock -Element $nav1 -PatternName 'Invoke')) 'item does not expose Invoke'
$scroll = Find-UiaElement -Driver $mock -AutomationId 'EntryScrollBar'
Check (Test-UiaPatternAvailable -Driver $mock -Element $scroll -PatternName 'Scroll') 'scrollbar exposes Scroll'

Write-Host "== pattern invocation + action selection (2.1) =="
$action = Invoke-UiaElementAction -Driver $mock -Element $btn
Check ($action.used -eq 'InvokePattern' -and -not $action.fallback) 'action uses Invoke pattern, no fallback'
$rec = Get-UiaMockRecord -Driver $mock -Element $btn
Check ($rec.patternCalls.Count -ge 1 -and $rec.patternCalls[0].pattern -eq 'Invoke') 'mock records the Invoke call'
$select = Invoke-UiaElementAction -Driver $mock -Element $nav1 -Action 'Select'
Check ($select.used -eq 'SelectionItemPattern' -and -not $select.fallback) 'action uses SelectionItem pattern'
$badge = Find-UiaElement -Driver $mock -AutomationId 'ConnectionBadge'
$fb = Invoke-UiaElementAction -Driver $mock -Element $badge -NoSend
Check ($fb.used -eq 'SendInput' -and $fb.fallback) 'SendInput fallback recorded only when no pattern exposed'
try {
    $null = Get-UiaPattern -Driver $mock -Element (Find-UiaElement -Driver $mock -AutomationId 'StatusBar') -PatternName 'Invoke'
    Check $false 'unsupported pattern should throw'
} catch {
    $ex = $_.Exception
    Check ($ex.GetType().Name -eq 'UiaPatternNotSupportedException') "unsupported pattern throws UiaPatternNotSupportedException, got $($ex.GetType().Name)"
    Check ($ex.PatternName -eq 'Invoke') 'exception carries pattern name'
}

Write-Host "== timeout + fail-clearly contract (2.2/2.4) =="
$timedOut = $false
try {
    $null = Find-UiaElement -Driver $mock -AutomationId 'NoSuchElement' -TimeoutMs 300
} catch {
    $timedOut = $true
    $ex = $_.Exception
    Check ($ex.GetType().Name -eq 'UiaOperationTimeoutException') "timeout throws UiaOperationTimeoutException, got $($ex.GetType().Name)"
    Check ($ex.Criteria['AutomationId'] -eq 'NoSuchElement') 'timeout exception carries criteria'
    Check ($ex.Message -like '*UiaTargetNotFound*') 'timeout exception message names the failure'
    Check ($ex.TimeoutMs -eq 300) 'timeout exception carries timeoutMs'
}
Check $timedOut 'missing semantic target times out with structured failure'

$err = New-UiaTargetNotFoundError -Driver $mock -Criteria @{ AutomationId = 'NoSuchElement' } -FromElement $root -DumpBaseName 'missing' -DumpDirectory (Join-Path $env:TEMP 'ff-uia-tests')
Check ($err.type -eq 'UiaTargetNotFound') 'target-not-found error has type'
Check ($null -ne $err.treeDumpPath -and (Test-Path $err.treeDumpPath)) 'target-not-found error wrote a tree dump'

Write-Host "== tree dumps (2.4) =="
$dump = Get-UiaTreeDump -Driver $mock -Element $root
$parsed = $dump.json | ConvertFrom-Json
Check ($parsed.root.automationId -eq 'MainWindow') 'dump JSON root has automationId'
Check ($parsed.nodeCount -eq 11) "dump nodeCount, got $($parsed.nodeCount)"
Check (-not $parsed.truncated) 'dump not truncated for small tree'
Check ($dump.text -match '^\S+.*FastFiles') 'dump text starts with root line'
Check (($dump.text -split "`n").Count -ge 11) 'dump text has one line per node'
$tmpDir = Join-Path $env:TEMP 'ff-uia-dumps'
if (-not (Test-Path $tmpDir)) { New-Item -ItemType Directory -Path $tmpDir | Out-Null }
$exported = Export-UiaTreeDump -Driver $mock -Element $root -BaseName 'rootdump' -Directory $tmpDir
Check ((Test-Path $exported.jsonPath) -and (Test-Path $exported.textPath)) 'export writes json + text files'

Write-Host "== events (2.1) =="
$evId = Register-UiaEvent -Driver $mock -EventName 'Invoked' -Element $btn
$null = Invoke-UiaPattern -Driver $mock -Element $btn -PatternName 'Invoke'
Check (Wait-UiaEvent -Driver $mock -Id $evId -TimeoutMs 500) 'mock Invoked event delivered to Wait-UiaEvent'
$selId = Register-UiaEvent -Driver $mock -EventName 'SelectionChanged' -Element $nav1
$null = Invoke-UiaPattern -Driver $mock -Element $nav1 -PatternName 'SelectionItem' -Method 'Select'
Check (Wait-UiaEvent -Driver $mock -Id $selId -TimeoutMs 500) 'mock SelectionChanged event delivered'
$neverId = Register-UiaEvent -Driver $mock -EventName 'WindowOpened' -Element $root
Check (-not (Wait-UiaEvent -Driver $mock -Id $neverId -TimeoutMs 300)) 'never-fired event times out cleanly'
Check (Unregister-UiaEvent -Driver $mock -Id $evId) 'unregister removes a subscription'

Write-Host "== Wait-UiaCondition + input dry-run (2.1) =="
$flag = $false
$cond = Wait-UiaCondition -Driver $mock -TimeoutMs 500 -ScriptBlock {
    Start-Sleep -Milliseconds 100
    return $flag
}
Check (-not $cond) 'condition not met returns false'
$flag = $true
$cond2 = Wait-UiaCondition -Driver $mock -TimeoutMs 500 -ScriptBlock { return $flag }
Check $cond2 'condition met returns true'
$kb = Send-UiaInput -Driver $mock -Keys '{ENTER}{DOWN}' -NoSend
Check ($kb.used -eq 'SendInput' -and $kb.dryRun) 'keyboard dry-run records SendInput fallback'
$ms = Send-UiaMouseInput -Driver $mock -X 10 -Y 10 -Action 'Click' -NoSend
Check ($ms.used -eq 'SendInput' -and $ms.dryRun) 'mouse dry-run records SendInput fallback'

Write-Host ""
if ($script:failures -gt 0) {
    Write-Host "RESULT: $script:failures check(s) FAILED"
    exit 1
}
Write-Host "RESULT: all uia-driver headless tests passed"
exit 0
