[CmdletBinding()]
param(
    [string]$RepoRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path,
    [Parameter(Mandatory)][string]$ArtifactsDir
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$repo=(Resolve-Path -LiteralPath $RepoRoot).Path
if(Test-Path -LiteralPath $ArtifactsDir) { throw 'Fresh evidence directory required.' }
$null=New-Item -ItemType Directory -Path $ArtifactsDir
$artifacts=(Resolve-Path -LiteralPath $ArtifactsDir).Path
Import-Module (Join-Path $repo 'verify/core/Provenance.psm1') -Force -DisableNameChecking
$exe=Join-Path $repo 'build/debug-diagnostic/src/ui/FastFiles.exe'
$gate=Invoke-ProvenanceGate -RepoRoot $repo -Binaries @(Get-DefaultCertificationBinaries -RepoRoot $repo -BuildDirectory (Join-Path $repo 'build/debug-diagnostic')) -Mode exact-tree -Stage recursive-storage -PassThru
$gate.evidence | ConvertTo-Json -Depth 20 | Set-Content (Join-Path $artifacts 'provenance.json') -Encoding utf8NoBOM
if(-not $gate.ok) { throw $gate.message }
$e2e=Import-Module (Join-Path $repo 'verify/e2e/FastFilesE2E.psm1') -Force -DisableNameChecking -PassThru
Import-Module (Join-Path $repo 'verify/visual/VisualCapture.psm1') -Force -DisableNameChecking
$captureRun=New-VisualCaptureRun -Root (Join-Path $artifacts 'images') -ApplicationSha (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant() -RepoRoot $repo
$ctx=$null; $cleanup=$null; $failure=$null; $fixture=$null
$observations=[Collections.Generic.List[object]]::new()
$captures=[Collections.Generic.List[object]]::new()
$script:phase='startup'
function Check([bool]$Condition,[string]$Message) { if(-not $Condition) { throw $Message } }
function Phase([string]$Name) {
    $script:phase=$Name
    Write-Host ('PHASE: '+$Name)
    @{status='IN_PROGRESS';phase=$Name;observations=@($observations.ToArray())} | ConvertTo-Json -Depth 14 |
        Set-Content (Join-Path $artifacts 'progress.json') -Encoding utf8NoBOM
}
function Hash([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
function Navigate([string]$Path,[string]$ExpectedName) {
    $nav=Invoke-UiaAddressBarNavigate -Driver $ctx.Driver -Main $ctx.Main -ProcessId $ctx.Launched.Id -Path $Path -ExpectedLeaf ([IO.Path]::GetFileName($Path))
    Check $nav.Pass ('Navigation failed: '+($nav | ConvertTo-Json -Compress -Depth 5))
    $ready=& $e2e {param($c,$name) Wait-E2EItem -Context $c -Name $name -TimeoutMs 12000} $ctx $ExpectedName
    Check $ready.Pass ('Known listing did not expose '+$ExpectedName)
}
function Command([string]$Query,[string]$Name,[string]$Id) {
    $result=& $e2e {param($c,$q,$n,$id) Invoke-E2ENavigationCommand -Context $c -Query $q -Name $n -CommandId $id} $ctx $Query $Name $Id
    Check $result.Pass ('Command failed: '+($result | ConvertTo-Json -Compress -Depth 7))
    [void]$observations.Add(@{commandId=$Id;result=$result})
}
function Mode([string]$Name) {
    $button=Wait-UiaDirectChild -Driver $ctx.Driver -Parent $ctx.Main -Name $Name -ProcessId $ctx.Launched.Id -TimeoutMs 4000
    Check ([bool]$button) ('Storage mode missing: '+$Name)
    $invoked=& $e2e {param($c,$b) Invoke-E2EButton -Context $c -Element $b} $ctx $button
    Check ([bool]$invoked) ('Storage mode not activatable: '+$Name)
}
function Rows {
    $list=Find-UiaDirectChild -Driver $ctx.Driver -Parent $ctx.Main -AutomationId '7201' -ClassName SysListView32 -ProcessId $ctx.Launched.Id
    if(-not $list) { return @() }
    return @(Get-UiaChildren -Driver $ctx.Driver -Element $list | Where-Object {
        Test-UiaPatternAvailable -Driver $ctx.Driver -Element $_ -PatternName SelectionItem
    })
}
function RowEvidence($Row) {
    return [ordered]@{
        name=[string](Get-UiaElementProperty -Driver $ctx.Driver -Element $Row -Name Name)
        folder=[string](Get-UiaElementProperty -Driver $ctx.Driver -Element $Row -Name HelpText)
        description=[string](Get-UiaElementProperty -Driver $ctx.Driver -Element $Row -Name FullDescription)
        identity=(Get-UiaElementIdentity -Driver $ctx.Driver -Element $Row)
        bounds=(Get-UiaElementProperty -Driver $ctx.Driver -Element $Row -Name BoundingRectangle)
    }
}
function Capture([string]$Id) {
    Check (Test-UiaForegroundWindow -Driver $ctx.Driver -Element $ctx.Main) 'Screenshot lacks owned foreground.'
    $shot=Capture-VisualWindow -Run $captureRun -Element $ctx.Main -ScenarioId $Id -AssertionPass $true
    Check ($shot.Bounds.Dpi -gt 0) 'Screenshot lacks measured DPI.'
    [void]$captures.Add(@{id=$Id;image=[IO.Path]::GetFileName($shot.Path);sha256=$shot.Hash;bounds=$shot.Bounds})
}
try {
    $ctx=New-UiaScenarioContext -Exe $exe -RepoRoot $repo -ScenarioId recursive-storage -InvocationId ([guid]::NewGuid().ToString('N')) -LogFn {param($m) Write-Host $m}
    Check ([bool]$ctx -and [bool]$ctx.Main -and $ctx.Ready) 'Owned application context is unavailable.'
    $fixture=New-E2EFixture -SessionFixtureRoot $ctx.FixtureRoot
    # Do not populate Context.Fixtures: the marked session root owns cleanup,
    # never a fixture-name/title guess about an unrelated Explorer window.
    $scope=Assert-E2EPathWithinRoot -Root $fixture.Root -Path (Join-Path $fixture.Root 'RankScope')
    $nested=Join-Path $scope 'Nested'
    $outside=Join-Path $fixture.Root 'RankScope-other'
    foreach($directory in @($scope,$nested,$outside)) { $null=New-Item -ItemType Directory -Path $directory }
    $paths=@(
        @{path=(Join-Path $scope 'shared.bin');bytes=16;fill=11},
        @{path=(Join-Path $scope 'small.txt');bytes=8;fill=22},
        @{path=(Join-Path $nested 'shared.bin');bytes=2MB;fill=33},
        @{path=(Join-Path $nested 'deep-only.bin');bytes=1MB;fill=44},
        @{path=(Join-Path $outside 'shared.bin');bytes=4MB;fill=55},
        @{path=(Join-Path $fixture.Root 'ranking-sentinel.txt');bytes=32;fill=66}
    )
    foreach($entry in $paths) {
        $validated=Assert-E2EPathWithinRoot -Root $fixture.Root -Path $entry.path
        $bytes=[byte[]]::new($entry.bytes)
        [Array]::Fill[byte]($bytes,[byte]$entry.fill)
        [IO.File]::WriteAllBytes($validated,$bytes)
        $entry.hash=Hash $validated
    }
    Phase seed-known-subfolders
    Navigate $outside shared.bin
    Navigate $nested deep-only.bin
    Navigate $scope small.txt
    Phase recursive-ranking
    Command analyze 'Analyze Storage' storage.analyze
    Mode 'Largest Files'
    $ranked=Wait-E2ECondition -TimeoutMs 15000 -Description 'four exact recursive ranked rows' -Condition {
        $rows=@(Rows)
        if($rows.Count -eq 4) { return ,$rows }
        return $false
    }
    Check $ranked.Pass 'Recursive ranking did not expose exactly four scoped known files.'
    $rows=@($ranked.Value)
    $evidence=@($rows | ForEach-Object { RowEvidence $_ })
    Check ($evidence[0].name -eq 'shared.bin' -and [string]::Equals($evidence[0].folder,$nested,[StringComparison]::OrdinalIgnoreCase)) 'Largest nested file did not rank first at its actual location.'
    Check ($evidence[1].name -eq 'deep-only.bin' -and $evidence[2].name -eq 'shared.bin' -and $evidence[3].name -eq 'small.txt') 'Logical-size descending order is incorrect.'
    Check (@($evidence | Where-Object { [string]::Equals($_.folder,$outside,[StringComparison]::OrdinalIgnoreCase) }).Count -eq 0) 'Same-prefix sibling leaked into recursive scope.'
    Check ($evidence[0].identity.AutomationId -ne $evidence[2].identity.AutomationId) 'Duplicate basenames share action identity.'
    $status=Wait-E2ECondition -TimeoutMs 4000 -Description 'actual partial published source label' -Condition {
        foreach($child in @(Get-UiaChildren -Driver $ctx.Driver -Element $ctx.Main)) {
            $id=Get-UiaElementIdentity -Driver $ctx.Driver -Element $child
            if($id.ClassName -eq 'Static' -and $id.Name -like 'Browsed/published files: 4*partial coverage*subfolders included*') { return [string]$id.Name }
        }
        return $false
    }
    Check $status.Pass 'Degraded known-file ranking did not truthfully expose its partial published coverage.'
    $list=Find-UiaDirectChild -Driver $ctx.Driver -Parent $ctx.Main -AutomationId '7201' -ClassName SysListView32 -ProcessId $ctx.Launched.Id
    $headers=@(Get-UiaChildren -Driver $ctx.Driver -Element $list | Where-Object {
        (Get-UiaElementIdentity -Driver $ctx.Driver -Element $_).Name -eq 'Location'
    })
    Check ($headers.Count -eq 1) 'Ranked list lacks the real Location header.'
    $locationBounds=Get-UiaElementProperty -Driver $ctx.Driver -Element $headers[0] -Name BoundingRectangle
    Check ($locationBounds -and $locationBounds.width -gt 0 -and $locationBounds.height -gt 0 -and
        -not (Get-UiaElementProperty -Driver $ctx.Driver -Element $headers[0] -Name IsOffscreen)) 'Ranked Location header is not physically reachable.'
    [void]$observations.Add(@{id='recursive-rank';rows=$evidence;status=$status.Value;scope=$scope;nested=$nested;outside=$outside})
    $null=Invoke-UiaPattern -Driver $ctx.Driver -Element $rows[0] -PatternName SelectionItem -Method Select
    Check ([bool](Invoke-UiaPattern -Driver $ctx.Driver -Element $rows[0] -PatternName SelectionItem -Method GetSelection)) 'Largest nested row could not be selected.'
    Phase exact-ranked-target
    Command 'copy path' 'Copy Path' item.copy-path
    $target=Join-Path $nested 'shared.bin'
    $copied=Wait-E2ECondition -TimeoutMs 3000 -Description 'ranked exact source path copied' -Condition {
        [string]::Equals(([string](Get-Clipboard -Raw)).TrimEnd([char[]]([string][char]13+[char]10)),$target,[StringComparison]::OrdinalIgnoreCase)
    }
    Check $copied.Pass 'Copy Path retargeted an immediate or outside duplicate basename.'
    Capture recursive-largest-files
    $null=Invoke-UiaPattern -Driver $ctx.Driver -Element $rows[0] -PatternName Invoke -Method Invoke
    Phase exact-hierarchical-reveal
    $revealed=Wait-E2ECondition -TimeoutMs 10000 -Description 'actual nested hierarchy and selected duplicate' -Condition {
        $validation=Get-Module UiAutomationValidation
        $columns=@(& $validation {param($c) Get-UiaColumnListElements -Driver $c.Driver -Main $c.Main -PaneId Pane.0 -ProcessId $c.Launched.Id} $ctx)
        if(-not $columns.Count) { return $false }
        $terminal=(Get-UiaElementIdentity -Driver $ctx.Driver -Element $columns[-1]).Name
        if(-not [string]::Equals($terminal.TrimEnd('\'),$nested.TrimEnd('\'),[StringComparison]::OrdinalIgnoreCase)) { return $false }
        $row=Find-UiaTerminalColumnItem -Driver $ctx.Driver -Main $ctx.Main -ProcessId $ctx.Launched.Id -PaneId Pane.0 -Name shared.bin -TimeoutMs 700
        if($row -and [bool](Invoke-UiaPattern -Driver $ctx.Driver -Element $row -PatternName SelectionItem -Method GetSelection)) { return @{terminal=$terminal;selected=(Get-UiaElementIdentity -Driver $ctx.Driver -Element $row)} }
        return $false
    }
    Check $revealed.Pass 'Ranked activation did not reveal/select the actual nested file.'
    Command 'copy path' 'Copy Path' item.copy-path
    Check ([string]::Equals(([string](Get-Clipboard -Raw)).TrimEnd([char[]]([string][char]13+[char]10)),$target,[StringComparison]::OrdinalIgnoreCase)) 'Revealed browse selection targets a different duplicate.'
    Capture ranked-hierarchical-reveal
    [void]$observations.Add(@{id='exact-reveal';copiedPath=$target;reveal=$revealed.Value})
    Phase hierarchical-treemap-continuity
    Navigate $scope small.txt
    Command analyze 'Analyze Storage' storage.analyze
    Mode Treemap
    $group=Wait-E2ECondition -TimeoutMs 15000 -Description 'hierarchical treemap group' -Condition {
        try { return Find-UiaElement -Driver $ctx.Driver -AutomationId Storage.Treemap -FromElement $ctx.Main -TimeoutMs 700 } catch { return $false }
    }
    Check $group.Pass 'Hierarchical treemap did not become available.'
    $groupBounds=Get-UiaElementProperty -Driver $ctx.Driver -Element $group.Value -Name BoundingRectangle
    $mainBounds=Get-UiaElementProperty -Driver $ctx.Driver -Element $ctx.Main -Name BoundingRectangle
    Check ($groupBounds -and $groupBounds.width -gt 0 -and $groupBounds.height -gt 0 -and
        $groupBounds.x -ge $mainBounds.x -and $groupBounds.y -ge $mainBounds.y -and
        $groupBounds.x+$groupBounds.width -le $mainBounds.x+$mainBounds.width -and
        $groupBounds.y+$groupBounds.height -le $mainBounds.y+$mainBounds.height) 'Treemap geometry escapes the live main window.'
    $tiles=Wait-E2ECondition -TimeoutMs 15000 -Description 'root child directory tile' -Condition {
        $items=@(Get-UiaChildren -Driver $ctx.Driver -Element $group.Value | Where-Object { (Get-UiaElementIdentity -Driver $ctx.Driver -Element $_).ControlType -eq 'DataItem' })
        $names=@($items | ForEach-Object { [string](Get-UiaElementProperty -Driver $ctx.Driver -Element $_ -Name Name) })
        if($names -contains 'Nested') { return @{names=$names;items=$items} }
        return $false
    }
    Check $tiles.Pass 'Treemap lost the actual immediate Nested folder.'
    Check ($tiles.Value.names -notcontains 'deep-only.bin') 'Treemap incorrectly visualizes a flat recursive rank as hierarchical data.'
    Capture hierarchical-treemap
    [void]$observations.Add(@{id='hierarchical-treemap';tileNames=$tiles.Value.names})
    Phase unchanged-files
    foreach($entry in $paths) { Check ((Test-Path -LiteralPath $entry.path -PathType Leaf) -and (Hash $entry.path) -eq $entry.hash) ('Fixture content changed: '+$entry.path) }
    [void]$observations.Add(@{id='content-integrity';files=@($paths);allNamesAndHashesUnchanged=$true})
    Phase normal-close
    $closed=& $e2e {param($c) Close-E2EContextWindow -Context $c} $ctx
    Check ([bool]$closed) 'Owned app did not close through the actual normal window helper.'
    Phase verified
} catch {
    $failure=$script:phase+': '+$_.Exception.Message
    Write-Host ('FAIL: '+$failure)
} finally {
    if($ctx) {
        try {
            $cleanup=Remove-UiaScenarioContext -Context $ctx -ArtifactsDir $artifacts -LogFn {param($m) Write-Host $m}
            if(-not $cleanup.Pass -and -not $failure) { $failure='cleanup: '+($cleanup.Residue -join ',') }
        } catch { if(-not $failure) { $failure='cleanup: '+$_.Exception.Message } }
    }
    @{status=$(if($failure){'FAIL'}else{'PASS'});phase=$script:phase;failure=$failure;scope='Seeded degraded known-file recursive ranking; exact public UIA/actions and file hashes; screenshots require separate visual review';observations=@($observations.ToArray());captures=@($captures.ToArray());cleanup=$cleanup;binaryProvenance=$gate.evidence} |
        ConvertTo-Json -Depth 22 | Set-Content (Join-Path $artifacts 'result.json') -Encoding utf8NoBOM
}
if($failure) { throw $failure }
