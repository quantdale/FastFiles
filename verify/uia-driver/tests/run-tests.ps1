<#
    zero-touch-autonomous-engineering (uia-driver): headless unit tests for the
    driver's identity, traversal, pattern-selection, property-validation, event,
    timeout, and dump logic using a recorded (mock) UIA tree — no interactive
    desktop and no live UI process required. Registered with CTest as
    ffuia_driver_ps_tests. Exit 0 = PASS, non-zero = FAIL.
    CI-safety: when the UIA driver's availability probe reports the session
    cannot drive UIA (non-interactive host/CI), the suite SKIPs (exit 0) with a
    clear message instead of failing — real assertion failures still fail.
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
& (Join-Path $PSScriptRoot 'Test-PaletteSiblingDiscovery.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Palette direct-sibling discovery regression failed' }
$module = Join-Path $repo 'verify\uia-driver\UiaDriver.psm1'
$treePath = Join-Path $repo 'verify\uia-driver\tests\recorded-trees\sample-files-window.json'
Import-Module $module -Force

Write-Host "== availability probe (2.2) =="
$avail = Get-UiaDriverAvailability
Check ($null -ne $avail) 'availability probe returns an envelope'
Check ($avail.PSObject.Properties['Available'] -ne $null) 'envelope has Available'
if (-not $avail.Available) {
    # Recorded-tree tests are deliberately independent of the live UIA
    # availability probe. Only live-provider capabilities need an interactive
    # desktop; driver mechanics and stale-provider semantics remain testable in
    # CI and service-session hosts.
    Write-Host "  note: live UIA unavailable ($($avail.Reason)); continuing with recorded-tree tests"
} else {
    Check ($avail.Available -eq $true) 'interactive desktop session reports available'
}

Write-Host "== driver construction =="
$driver = New-UiaDriver
Check ($driver.TimeoutMs -eq 5000) 'default TimeoutMs'
Check ($driver.Provider -eq 'managed') 'default provider is managed'
$mock = New-UiaDriver -Provider mock -TimeoutMs 1000
Check ($mock.Provider -eq 'mock' -and $mock.TimeoutMs -eq 1000) 'mock driver honors parameters'

Write-Host "== interactive-ui-runtime-determinism: exact process ownership (9.1) =="
$validationPath = Join-Path $repo 'verify\capabilities\windows-ui-automation-validation\UiAutomationValidation.psm1'
Import-Module $validationPath -Force
$validationModule = Get-Module UiAutomationValidation
$fakeProcess = [pscustomobject]@{
    Id = 4242
    ProcessName = 'FastFiles'
    Path = (Join-Path $repo 'build\debug\src\ui\FastFiles.exe')
    StartTime = [DateTime]::Now
    SessionId = 1
}
$ownedIdentity = & $validationModule { param($process, $root) Get-UiaProcessIdentity -Process $process -RepoRoot $root } $fakeProcess $repo
Check ($ownedIdentity -and $ownedIdentity.Id -eq 4242 -and $ownedIdentity.Path -eq $fakeProcess.Path) 'repo-build process identity records exact PID and executable path'
$foreignProcess = [pscustomobject]@{
    Id = 4243
    ProcessName = 'FastFiles'
    Path = (Join-Path $env:TEMP 'FastFiles.exe')
    StartTime = [DateTime]::Now
    SessionId = 1
}
$foreignIdentity = & $validationModule { param($process, $root) Get-UiaProcessIdentity -Process $process -RepoRoot $root } $foreignProcess $repo
Check ($null -eq $foreignIdentity) 'process identity rejects same-name executable outside repository build tree'
$sameIdentity = & $validationModule { param($left, $right) Test-UiaSameProcessIdentity -Left $left -Right $right } $ownedIdentity $ownedIdentity
Check $sameIdentity 'same PID/path/start-time identity compares equal'
$pidReuse = [pscustomobject]@{ Id = 4242; Path = $ownedIdentity.Path; StartTimeUtc = $ownedIdentity.StartTimeUtc.AddSeconds(5) }
$reuseRejected = & $validationModule { param($left, $right) Test-UiaSameProcessIdentity -Left $left -Right $right } $ownedIdentity $pidReuse
Check (-not $reuseRejected) 'PID reuse with a different start time is rejected'
$newIdentities = & $validationModule { param($before, $after) @(Get-UiaNewProcessIdentities -Before $before -After $after) } @($ownedIdentity) @($ownedIdentity, $pidReuse)
Check ($newIdentities.Count -eq 1 -and $newIdentities[0].Id -eq 4242) 'new-process adoption is based on exact identity rather than name alone'

Write-Host "== interactive-ui-runtime-determinism: semantic palette outcomes (4.1/4.2) =="
$paletteReady = [pscustomobject]@{
    listPresent = $true; listVisible = $true; listEnabled = $true
    listItems = @('Toggle Dual Pane'); expectedMatches = @('Toggle Dual Pane')
    expectedNameLikeMatches = @(); selectedName = 'Toggle Dual Pane'
}
$paletteReadyResult = & $validationModule { param($state) Test-UiaPaletteSelectionObservation -State $state -ExpectedCommandName 'Toggle Dual Pane' } $paletteReady
Check ($paletteReadyResult.Pass -and $paletteReadyResult.Reason -eq 'semantic-selection-ready') 'palette helper accepts one visible enabled selected command'
$paletteMissing = $paletteReady.PSObject.Copy(); $paletteMissing.listPresent = $false; $paletteMissing.listItems = @()
$paletteMissingResult = & $validationModule { param($state) Test-UiaPaletteSelectionObservation -State $state -ExpectedCommandName 'Toggle Dual Pane' } $paletteMissing
Check (-not $paletteMissingResult.Pass -and $paletteMissingResult.Reason -eq 'palette-result-list-unavailable') 'palette helper classifies missing/dismissed result list'
$paletteAmbiguous = $paletteReady.PSObject.Copy(); $paletteAmbiguous.expectedMatches = @('Toggle Dual Pane', 'Toggle Dual Pane (legacy)')
$paletteAmbiguousResult = & $validationModule { param($state) Test-UiaPaletteSelectionObservation -State $state -ExpectedCommandName 'Toggle Dual Pane' } $paletteAmbiguous
Check (-not $paletteAmbiguousResult.Pass -and $paletteAmbiguousResult.Reason -eq 'command-result-ambiguous') 'palette helper classifies ambiguous command results'
$paletteWrong = $paletteReady.PSObject.Copy(); $paletteWrong.selectedName = 'Search'
$paletteWrongResult = & $validationModule { param($state) Test-UiaPaletteSelectionObservation -State $state -ExpectedCommandName 'Toggle Dual Pane' } $paletteWrong
Check (-not $paletteWrongResult.Pass -and $paletteWrongResult.Reason -eq 'wrong-command-selected') 'palette helper classifies wrong selected command'
$paletteDisabled = $paletteReady.PSObject.Copy(); $paletteDisabled.listEnabled = $false
$paletteDisabledResult = & $validationModule { param($state) Test-UiaPaletteSelectionObservation -State $state -ExpectedCommandName 'Toggle Dual Pane' } $paletteDisabled
Check (-not $paletteDisabledResult.Pass -and $paletteDisabledResult.Reason -eq 'palette-result-disabled') 'palette helper classifies disabled result surface'

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
$entryListAnchor = Find-UiaElement -Driver $mock -AutomationId 'EntryList'
$fromList = @(Find-UiaElement -Driver $mock -ControlType 'ListItem' -FromElement $entryListAnchor -All)
Check ($fromList.Count -eq 3) "find -FromElement returns subtree-only matches, got $($fromList.Count)"
$buttonAnchor = Find-UiaElement -Driver $mock -AutomationId 'NewColumnButton'
$fromButton = Find-UiaElement -Driver $mock -AutomationId 'NewColumnButton' -FromElement $buttonAnchor
Check ($fromButton -ne $null) 'find -FromElement finds the anchor itself'
$missingFrom = $false
try {
    $null = Find-UiaElement -Driver $mock -AutomationId 'SearchBox' -FromElement $entryListAnchor -TimeoutMs 300
} catch {
    $missingFrom = $true
}
Check $missingFrom 'find -FromElement excludes elements outside the subtree'

Write-Host "== direct-child lifecycle discovery (interactive-ui-runtime-determinism 5.1) =="
$dualTreePath = Join-Path $repo 'verify\uia-driver\tests\recorded-trees\dual-pane-reconstruction.json'
$dualMock = New-UiaDriver -Provider mock -TimeoutMs 1000 -PollIntervalMs 20
$null = Import-UiaRecordedTree -Driver $dualMock -Path $dualTreePath
$dualRoot = Get-UiaRootElement -Driver $dualMock
$dualGroup = Find-UiaDirectChild -Driver $dualMock -Parent $dualRoot -AutomationId 'ColumnView'
Check ($dualGroup -ne $null) 'direct child finds ColumnView group'
$directPanes = @(Find-UiaDirectChild -Driver $dualMock -Parent $dualGroup -AutomationId 'Pane.1' -All)
Check ($directPanes.Count -eq 1) "direct child finds exactly one Pane.1, got $($directPanes.Count)"
$directPaneId = Get-UiaElementIdentity -Driver $dualMock -Element $directPanes[0]
Check ($directPaneId.AutomationId -eq 'Pane.1' -and $directPaneId.ControlType -eq 'Pane') 'direct Pane.1 identity is exact'
$pane0 = Find-UiaDirectChild -Driver $dualMock -Parent $dualGroup -AutomationId 'Pane.0'
$pane0RuntimeId = @(Get-UiaElementRuntimeId -Driver $dualMock -Element $pane0)
$pane1RuntimeId = @(Get-UiaElementRuntimeId -Driver $dualMock -Element $directPanes[0])
Check (($pane0RuntimeId -join ',') -eq '42,100,0' -and ($pane1RuntimeId -join ',') -eq '42,100,1') 'Pane.0/Pane.1 RuntimeIds are captured and distinct'
$recursivePanes = @(Find-UiaElement -Driver $dualMock -AutomationId 'Pane.1' -FromElement $dualRoot -All)
Check ($recursivePanes.Count -eq 1) 'recursive search still sees the same recorded Pane.1'
$waitedDirect = Wait-UiaDirectChild -Driver $dualMock -ParentProvider { return (Find-UiaDirectChild -Driver $dualMock -Parent $dualRoot -AutomationId 'ColumnView') } `
    -AutomationId 'Pane.1' -TimeoutMs 300 -PollIntervalMs 20
Check ($waitedDirect -ne $null) 'direct-child wait reacquires parent and returns Pane.1'
$ambiguous = @(Find-UiaDirectChild -Driver $dualMock -Parent $dualGroup -ControlType 'Pane' -All)
Check ($ambiguous.Count -eq 2) "direct-child all-results exposes ambiguity for generic Pane identity, got $($ambiguous.Count)"

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

Write-Host "== ExpandCollapse + Filters disclosure (completion-integrity 4.5.2) =="
$filtersTree = Join-Path $repo 'verify\uia-driver\tests\recorded-trees\search-filters-disclosure.json'
$filtersDriver = New-UiaDriver -Provider mock -TimeoutMs 1000
$null = Import-UiaRecordedTree -Driver $filtersDriver -Path $filtersTree
$filtersBtn = Find-UiaElement -Driver $filtersDriver -AutomationId 'Search.Filters'
Check ($null -ne $filtersBtn) 'Search.Filters is discoverable by AutomationId'
Check (Test-UiaPatternAvailable -Driver $filtersDriver -Element $filtersBtn -PatternName 'ExpandCollapse') 'Filters overlay exposes ExpandCollapse'
$collapsedState = Invoke-UiaPattern -Driver $filtersDriver -Element $filtersBtn -PatternName 'ExpandCollapse' -Method 'GetState'
Check ($collapsedState -eq 'Collapsed') 'recorded Filters disclosure starts Collapsed'
$null = Invoke-UiaPattern -Driver $filtersDriver -Element $filtersBtn -PatternName 'ExpandCollapse' -Method 'Expand'
$expandedState = Invoke-UiaPattern -Driver $filtersDriver -Element $filtersBtn -PatternName 'ExpandCollapse' -Method 'GetState'
Check ($expandedState -eq 'Expanded') 'ExpandCollapse.Expand is idempotent state, not a toggle'
$null = Invoke-UiaPattern -Driver $filtersDriver -Element $filtersBtn -PatternName 'ExpandCollapse' -Method 'Expand'
$stillExpanded = Invoke-UiaPattern -Driver $filtersDriver -Element $filtersBtn -PatternName 'ExpandCollapse' -Method 'GetState'
Check ($stillExpanded -eq 'Expanded') 'second Expand leaves an expanded disclosure expanded'
$filtersRec = Get-UiaMockRecord -Driver $filtersDriver -Element $filtersBtn
$expandCalls = @($filtersRec.patternCalls | Where-Object { $_.pattern -eq 'ExpandCollapse' -and $_.method -eq 'Expand' })
$invokeCalls = @($filtersRec.patternCalls | Where-Object { $_.pattern -eq 'Invoke' })
Check ($expandCalls.Count -eq 2) 'Expand was recorded twice and never inverted the state'
Check ($invokeCalls.Count -eq 0) 'Filters Expand path does not fall back to Invoke toggle'

Import-Module $validationPath -Force
$validationModule = Get-Module UiAutomationValidation
$collapsedDriver = New-UiaDriver -Provider mock -TimeoutMs 1000
$null = Import-UiaRecordedTree -Driver $collapsedDriver -Path $filtersTree
$main = $collapsedDriver.Root
$opened = & $validationModule { param($driver, $main, $processId)
    Expand-UiaSearchFilters -Driver $driver -Main $main -ProcessId $processId -TimeoutMs 2000
} $collapsedDriver $main 4242
Check ($opened.Pass) "Expand-UiaSearchFilters passes on ExpandCollapse; detail=$($opened.Detail)"
$openedBtn = Find-UiaElement -Driver $collapsedDriver -AutomationId 'Search.Filters'
$openedRec = Get-UiaMockRecord -Driver $collapsedDriver -Element $openedBtn
$openedExpand = @($openedRec.patternCalls | Where-Object { $_.pattern -eq 'ExpandCollapse' -and $_.method -eq 'Expand' })
$openedInvoke = @($openedRec.patternCalls | Where-Object { $_.pattern -eq 'Invoke' })
Check ($openedExpand.Count -eq 1) 'capability Expand-UiaSearchFilters posts Expand once'
Check ($openedInvoke.Count -eq 0) 'capability Expand-UiaSearchFilters does not toggle via Invoke'
$already = & $validationModule { param($driver, $main, $processId)
    Expand-UiaSearchFilters -Driver $driver -Main $main -ProcessId $processId -TimeoutMs 1000
} $collapsedDriver $main 4242
Check ($already.Pass) 'already-expanded Filters disclosure is a no-op success'
$afterRec = Get-UiaMockRecord -Driver $collapsedDriver -Element $openedBtn
$afterExpand = @($afterRec.patternCalls | Where-Object { $_.pattern -eq 'ExpandCollapse' -and $_.method -eq 'Expand' })
Check ($afterExpand.Count -eq 1) 'already-expanded rerun does not post a second Expand'
$startFn = Get-Command Start-UiaFastFiles -ErrorAction SilentlyContinue
if (-not $startFn) {
    $startFn = & $validationModule { Get-Command Start-UiaFastFiles }
}
$startSrc = if ($startFn) { $startFn.ScriptBlock.ToString() } else { '' }
Check ($startSrc -match 'WindowStyle Normal') 'Start-UiaFastFiles forces WindowStyle Normal so hidden workers cannot launch FastFiles hidden'
$querySet = & $validationModule { param($driver, $main, $processId)
    Set-UiaSearchQuery -Driver $driver -Main $main -ProcessId $processId -Query 'alloc'
} $collapsedDriver $main 4242
Check ($querySet.Pass) "Set-UiaSearchQuery targets AutomationId 7101; detail=$($querySet.Detail)"
$queryRec = Get-UiaMockRecord -Driver $collapsedDriver -Element (Find-UiaElement -Driver $collapsedDriver -AutomationId '7101')
Check ($queryRec.value -eq 'alloc') 'Set-UiaSearchQuery ValuePattern write is the query combo, not the scope combo'

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
Check ($kb.used -eq 'SendInput' -and $kb.dryRun -and $kb.ok) 'keyboard dry-run records accepted-input contract'
$chord = Send-UiaInput -Driver $mock -Keys '{CTRL}{SHIFT}p' -NoSend
$chordVks = @($chord.sequence | ForEach-Object { [int]$_.vk })
$expect = @(17, 16, 80, 80, 16, 17)
Check (($chordVks -join ',') -eq ($expect -join ',')) "modifier chord applies ctrl+shift to bare key, got $($chordVks -join ',')"
$ctrlL = Send-UiaInput -Driver $mock -Keys '{CTRL}l' -NoSend
$ctrlLVks = @($ctrlL.sequence | ForEach-Object { [int]$_.vk })
Check (($ctrlLVks -join ',') -eq '17,76,76,17') 'single-char token after CTRL receives the modifier'
$plain = Send-UiaInput -Driver $mock -Keys 'abc' -NoSend
Check ($plain.sequence.Count -eq 6) "plain text has no stray modifier keys, got $($plain.sequence.Count)"
$ms = Send-UiaMouseInput -Driver $mock -X 10 -Y 10 -Action 'Click' -NoSend
Check ($ms.used -eq 'SendInput' -and $ms.dryRun -and $ms.ok) 'mouse dry-run records accepted-input contract'
$dragDry = Send-UiaMouseDrag -Driver $mock -FromX 1 -FromY 1 -ToX 2 -ToY 2 -NoSend
Check ($dragDry.ok -and $dragDry.inputAccepted) 'drag dry-run records accepted-input contract'
$tx = Send-UiaText -Driver $mock -Text 'C:\Temp\sub dir' -NoSend
Check ($tx.used -eq 'SendInput' -and $tx.kind -eq 'unicode-text' -and $tx.chars -eq 15 -and $tx.ok) 'unicode text dry-run records accepted-input contract'

Write-Host "== original-vision-journey-recovery: foreground preserves active workspaces =="
. (Join-Path $PSScriptRoot 'test-foreground-activation.ps1')

Write-Host "== stabilize-live-uia-e2e: bounded semantic waits =="
$onTimeoutRan = $false
$condTimeout = Wait-UiaCondition -Driver $mock -TimeoutMs 300 -PollIntervalMs 30 -Description 'never true' -OnTimeout { $script:onTimeoutRan = $true; return $true } -ScriptBlock { return $false }
Check (-not $condTimeout) 'bounded wait times out returning false'
Check $onTimeoutRan 'timeout diagnostics scriptblock runs on timeout'
$waitEl = Wait-UiaElement -Driver $mock -AutomationId 'Nav1' -FromElement $root -TimeoutMs 500
Check ($null -ne $waitEl) 'Wait-UiaElement finds an existing element'
$waitMissing = Wait-UiaElement -Driver $mock -AutomationId 'NoSuchElement' -FromElement $root -TimeoutMs 300 -PollIntervalMs 30
Check ($null -eq $waitMissing) 'Wait-UiaElement returns null for a missing element'
$gone = Wait-UiaElementGone -Driver $mock -AutomationId 'NoSuchElement' -FromElement $root -TimeoutMs 300 -PollIntervalMs 30
Check $gone 'Wait-UiaElementGone returns true when element is absent'
$gonePresent = Wait-UiaElementGone -Driver $mock -AutomationId 'Nav1' -FromElement $root -TimeoutMs 300 -PollIntervalMs 30
Check (-not $gonePresent) 'Wait-UiaElementGone returns false while element exists'

Write-Host "== stabilize-live-uia-e2e: ValuePattern assignment =="
$valSet = Set-UiaElementValue -Driver $mock -Element $search -Value 'C:\Users\Test\Documents'
Check ($valSet.ok -and $valSet.used -eq 'ValuePattern') "ValuePattern assignment ok (used=$($valSet.used))"
Check ($valSet.current -eq 'C:\Users\Test\Documents') "Value read-back matches, got '$($valSet.current)'"
$valRec = Get-UiaMockRecord -Driver $mock -Element $search
$setCall = @($valRec.patternCalls | Where-Object { $_.pattern -eq 'Value' -and $_.method -eq 'SetValue' })
Check ($setCall.Count -ge 1 -and $setCall[0].args[0] -eq 'C:\Users\Test\Documents') 'mock records the SetValue call with the argument'
$noValue = Find-UiaElement -Driver $mock -AutomationId 'ConnectionBadge'
$valFail = Set-UiaElementValue -Driver $mock -Element $noValue -Value 'x'
Check (-not $valFail.ok -and $valFail.used -eq 'unavailable') 'element without ValuePattern fails cleanly as unavailable'
$textVal = Get-UiaElementTextValue -Driver $mock -Element $search
Check ($textVal -eq 'C:\Users\Test\Documents') "text value falls back to element Name, got '$textVal'"
$badgeText = Get-UiaElementTextValue -Driver $mock -Element $noValue
Check ($badgeText -eq 'Connection: Degraded') 'text value reads the element Name when no Value pattern exists'
$entryList = Find-UiaElement -Driver $mock -AutomationId 'EntryList'
$nav0 = Find-UiaElement -Driver $mock -AutomationId 'Nav0'
$nav0Rec = Get-UiaMockRecord -Driver $mock -Element $nav0
$nav0Rec.patterns = @('SelectionItem', 'Invoke', 'ScrollItem', 'ItemContainer')
$icFind = Invoke-UiaPattern -Driver $mock -Element $nav0 -PatternName 'ItemContainer' -Method 'FindItemByProperty' -Arguments @('Name', 'Docs')
Check ($icFind -eq $true) 'ItemContainer FindItemByProperty records on mock'
$icCall = @($nav0Rec.patternCalls | Where-Object { $_.pattern -eq 'ItemContainer' -and $_.method -eq 'FindItemByProperty' })
Check ($icCall.Count -ge 1 -and $icCall[0].args[0] -eq 'Name' -and $icCall[0].args[1] -eq 'Docs') 'ItemContainer call recorded with property name and value'

Write-Host "== stabilize-live-uia-e2e: modifier reset + interactive state =="
$modReset = Reset-UiaModifierKeys -Driver $mock -NoSend
Check ($modReset.kind -eq 'modifier-reset' -and $modReset.released -contains 0x11 -and $modReset.released -contains 0x5B) 'modifier reset lists ctrl and win key-up vks'
$state = Get-UiaInteractiveState -Driver $mock -ProcessId 4242
Check ($state.PSObject.Properties['TimestampUtc'] -ne $null -and $state.Windows.Count -eq 0) 'interactive state envelope is safe on mock drivers'
Check (-not (Test-UiaInputBlockingPopup -ClassName 'tooltips_class32')) 'tooltips do not block search-close / address-bar navigation'
Check (-not (Test-UiaInputBlockingPopup -ClassName 'SysShadow')) 'SysShadow does not block search-close / address-bar navigation'
Check (Test-UiaInputBlockingPopup -ClassName 'ComboLBox') 'combo dropdowns still block keyboard input'
$fgProbe = Test-UiaForegroundWindow -Driver $mock -Element $mock.Root
Check ($fgProbe -eq $true) 'foreground ownership probe is safe on mock drivers'
$topAt = Get-UiaTopmostWindowAt -Driver $mock -X 100 -Y 100
Check ($null -eq $topAt) 'topmost-window probe returns null on mock drivers'
$inWin = Test-UiaPointInWindow -Driver $mock -Element $mock.Root -X 100 -Y 100
Check ($inWin -eq $true) 'point-in-window probe is safe on mock drivers'
$stable = Wait-UiaStableRect -Driver $mock -Element $mock.Root -StableReads 2 -IntervalMs 10 -TimeoutMs 500
Check ($stable -eq $true) 'rect-stability wait is safe on mock drivers'

Write-Host "== stabilize-live-uia-e2e: Explorer window discovery =="
$explorerTree = Join-Path $repo 'verify\uia-driver\tests\recorded-trees\explorer-windows.json'
$expMock = New-UiaDriver -Provider mock -TimeoutMs 1000
$null = Import-UiaRecordedTree -Driver $expMock -Path $explorerTree
$found = Get-UiaExplorerWindow -Driver $expMock -ExpectedPath 'C:\Users\Test\ffui-drag-42\target' -CandidateProcessIds @(4242) -TimeoutMs 1000 -PollIntervalMs 50
$foundId = if ($found) { (Get-UiaElementIdentity -Driver $expMock -Element $found).Name } else { $null }
Check ($foundId -eq 'ffui-drag-42-target') "Explorer discovery binds the window with the matching address value, got '$foundId'"
$found2 = Get-UiaExplorerWindow -Driver $expMock -ExpectedPath 'C:\Users\Test\Downloads' -CandidateProcessIds @(4242) -TimeoutMs 1000 -PollIntervalMs 50
$found2Id = if ($found2) { (Get-UiaElementIdentity -Driver $expMock -Element $found2).Name } else { $null }
Check ($found2Id -eq 'Downloads - File Explorer') 'Explorer discovery matches the second window by its own address'
$foundNone = Get-UiaExplorerWindow -Driver $expMock -ExpectedPath 'C:\Users\Test\ffui-drag-42\source' -CandidateProcessIds @(4242) -TimeoutMs 300 -PollIntervalMs 50
Check ($null -eq $foundNone) 'Explorer discovery returns null for an unmatched path'

Write-Host ""
if ($script:failures -gt 0) {
    Write-Host "RESULT: $script:failures check(s) FAILED"
    exit 1
}
Write-Host "RESULT: all uia-driver headless tests passed"
exit 0
