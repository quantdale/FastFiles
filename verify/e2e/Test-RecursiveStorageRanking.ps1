[CmdletBinding()]
param(
    [string]$RepoRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path,
    [Parameter(Mandatory)][string]$ArtifactsDir,
    [switch]$PartialDescendant,
    [ValidateSet('System','Light','Dark')][string]$Theme='System',
    [ValidateSet(0,620,1008,1600)][int]$WindowWidth=0
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
    $selector=Find-UiaDirectChild -Driver $ctx.Driver -Parent $ctx.Main -AutomationId '7213' -ClassName ComboBox -ProcessId $ctx.Launched.Id
    if($selector -and -not (Get-UiaElementProperty -Driver $ctx.Driver -Element $selector -Name IsOffscreen)) {
        $modes=@('Overview','Drill Down','Largest Folders','Largest Files','By Category','Treemap')
        $index=[Array]::IndexOf($modes,$Name)
        Check ($index -ge 0) 'Unknown native Storage view.'
        $null=Set-UiaForeground -Driver $ctx.Driver -Element $ctx.Main
        $bounds=Get-UiaElementProperty -Driver $ctx.Driver -Element $selector -Name BoundingRectangle
        Check ($bounds -and $bounds.width -gt 0 -and $bounds.height -gt 0) 'Native view selector has no reachable bounds.'
        $focusInput=Send-UiaMouseInput -Driver $ctx.Driver -X ([int]($bounds.x+$bounds.width/2)) -Y ([int]($bounds.y+$bounds.height/2)) -Button Left -Action Click
        Check $focusInput.ok 'Real native selector focus input rejected.'
        $handle=[IntPtr]$selector.Current.NativeWindowHandle
        [uint32]$ownerPid=0
        $thread=[FfNativeInput]::GetWindowThreadProcessId($handle,[ref]$ownerPid)
        Check ($ownerPid -eq $ctx.Launched.Id) 'Native selector no longer belongs to the owned process.'
        $focusSettled=Wait-E2ECondition -TimeoutMs 2500 -Description 'native selector processes real focus input' -Condition {
            $probe=[FfNativeInput+GUITHREADINFO]::new()
            $probe.cbSize=[Runtime.InteropServices.Marshal]::SizeOf([type][FfNativeInput+GUITHREADINFO])
            if([FfNativeInput]::GetGUIThreadInfo($thread,[ref]$probe) -and $probe.hwndFocus -eq $handle) { return @{nativeFocus=$probe.hwndFocus.ToInt64()} }; return $false
        }
        Check $focusSettled.Pass 'Actual native selector did not acquire keyboard focus after accepted pointer input.'
        $keys='{HOME}'+('{DOWN}'*$index)+'{ENTER}'
        $input=Send-UiaInput -Driver $ctx.Driver -Keys $keys
        Check $input.ok 'Native view selector keyboard input rejected.'
        $identity=Get-UiaElementIdentity -Driver $ctx.Driver -Element $selector
        $handle=[IntPtr]$selector.Current.NativeWindowHandle
        Check ($handle -ne [IntPtr]::Zero -and $identity.ProcessId -eq $ctx.Launched.Id -and $identity.AutomationId -eq '7213') 'Cannot bind view selection observation to owned native combo.'
        $settled=Wait-E2ECondition -TimeoutMs 2500 -Description 'native selector processes accepted keyboard input' -Condition {
            if([FfButtonDefault]::SendMessage($handle,0x0147,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64() -eq $index) { return $true }; return $false
        }
        $selected=[FfButtonDefault]::SendMessage($handle,0x0147,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64()
        [void]$observations.Add(@{id='storage-view';name=$Name;route='real pointer focus then native keyboard';focusInput=$focusInput;nativeFocus=$focusSettled.Value.nativeFocus;focusSettled=$focusSettled;input=$input;selectedIndex=$selected;expectedIndex=$index;settled=$settled;identity=$identity})
        Check $settled.Pass ('Actual native view selector retained item '+$selected+' instead of '+$index)
        return
    }
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
        runtimeId=@($Row.GetRuntimeId())
        bounds=(Get-UiaElementProperty -Driver $ctx.Driver -Element $Row -Name BoundingRectangle)
    }
}
function ExactRankedRow([string]$Folder,[string]$Name) {
    return Wait-E2ECondition -TimeoutMs 5000 -Description 'fresh exact ranked file' -Condition {
        foreach($row in @(Rows)) {
            $identity=Get-UiaElementIdentity -Driver $ctx.Driver -Element $row
            if($identity.ProcessId -eq $ctx.Launched.Id -and $identity.Name -eq $Name -and
                [string]::Equals([string](Get-UiaElementProperty -Driver $ctx.Driver -Element $row -Name HelpText),$Folder,[StringComparison]::OrdinalIgnoreCase)) { return $row }
        }
        return $false
    }
}
function Capture([string]$Id,[bool]$AssertionPass=$true) {
    Check (Test-UiaForegroundWindow -Driver $ctx.Driver -Element $ctx.Main) 'Screenshot lacks owned foreground.'
    $shot=Capture-VisualWindow -Run $captureRun -Element $ctx.Main -ScenarioId $Id -AssertionPass $AssertionPass
    Check ($shot.Bounds.Dpi -gt 0) 'Screenshot lacks measured DPI.'
    [void]$captures.Add(@{id=$Id;image=[IO.Path]::GetFileName($shot.Path);sha256=$shot.Hash;bounds=$shot.Bounds})
}
try {
    $invocation=[guid]::NewGuid().ToString('N')
    $sessionRoot=''
    if($Theme -ne 'System') {
        $validation=Get-Module UiAutomationValidation
        $sandbox=& $validation {param($id) New-UiaSessionSandbox -ScenarioId recursive-storage -InvocationId $id} $invocation
        $sessionRoot=$sandbox.SessionRoot
        $settingsDir=Join-Path $sessionRoot 'LocalAppData/FastFiles'
        $null=New-Item -ItemType Directory -Path $settingsDir -Force
        @{schemaVersion=1;appearance=@{theme=$Theme;density='Compact'}} | ConvertTo-Json -Depth 3 |
            Set-Content (Join-Path $settingsDir 'settings.json') -Encoding utf8NoBOM
    }
    $ctx=New-UiaScenarioContext -Exe $exe -RepoRoot $repo -ScenarioId recursive-storage -InvocationId $invocation -SessionRoot $sessionRoot -LogFn {param($m) Write-Host $m}
    Check ([bool]$ctx -and [bool]$ctx.Main -and $ctx.Ready) 'Owned application context is unavailable.'
    if($WindowWidth -gt 0) {
        if(-not ('FfStorageQualificationWindow' -as [type])) {
            Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class FfStorageQualificationWindow {
    [DllImport("user32.dll", SetLastError=true)]
    public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int width, int height, uint flags);
}
'@
        }
        Check ([FfStorageQualificationWindow]::SetWindowPos([IntPtr]$ctx.Main.Current.NativeWindowHandle,[IntPtr]::Zero,40,32,$WindowWidth,780,0x44)) 'Owned window resize failed.'
    }
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
    if($PartialDescendant) {
        $unbrowsed=Join-Path $nested 'Unbrowsed'
        $null=New-Item -ItemType Directory -Path $unbrowsed
        $paths+=@{path=(Join-Path $unbrowsed 'unpublished-largest.bin');bytes=5MB;fill=77}
    }
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
    [void]$observations.Add(@{id='ranked-row-observation';rows=$evidence})
    $evidence | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $artifacts 'ranked-rows.json') -Encoding utf8NoBOM
    Check ($evidence[0].name -eq 'shared.bin' -and [string]::Equals($evidence[0].folder,$nested,[StringComparison]::OrdinalIgnoreCase)) 'Largest nested file did not rank first at its actual location.'
    Check ($evidence[1].name -eq 'deep-only.bin' -and $evidence[2].name -eq 'shared.bin' -and $evidence[3].name -eq 'small.txt') 'Logical-size descending order is incorrect.'
    Check (@($evidence | Where-Object { [string]::Equals($_.folder,$outside,[StringComparison]::OrdinalIgnoreCase) }).Count -eq 0) 'Same-prefix sibling leaked into recursive scope.'
    Check ($evidence[0].runtimeId.Count -gt 0 -and $evidence[2].runtimeId.Count -gt 0 -and
        ($evidence[0].runtimeId -join ',') -ne ($evidence[2].runtimeId -join ',')) 'Duplicate basenames share runtime action identity.'
    $status=Wait-E2ECondition -TimeoutMs 4000 -Description 'actual partial published source label' -Condition {
        foreach($child in @(Get-UiaChildren -Driver $ctx.Driver -Element $ctx.Main)) {
            $id=Get-UiaElementIdentity -Driver $ctx.Driver -Element $child
            if($id.ClassName -eq 'Static' -and $id.Name -like 'Partial coverage — known files: 4*subfolders included*') { return [string]$id.Name }
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
    $selectionTarget=ExactRankedRow $nested shared.bin
    Check $selectionTarget.Pass 'The exact nested ranking target is unavailable before selection.'
    try {
        $null=Invoke-UiaPattern -Driver $ctx.Driver -Element $selectionTarget.Value -PatternName SelectionItem -Method Select
    } catch {
        @{error=$_.Exception.ToString();hresult=$_.Exception.HResult;inner=$(if($_.Exception.InnerException){$_.Exception.InnerException.ToString()});currentRows=@(Rows | ForEach-Object {RowEvidence $_})} |
            ConvertTo-Json -Depth 12 | Set-Content (Join-Path $artifacts 'selection-rejection.json') -Encoding utf8NoBOM
        throw
    }
    Check ([bool](Invoke-UiaPattern -Driver $ctx.Driver -Element $selectionTarget.Value -PatternName SelectionItem -Method GetSelection)) 'Largest nested row could not be selected.'
    Phase exact-ranked-target
    Command 'copy path' 'Copy Path' item.copy-path
    $target=Join-Path $nested 'shared.bin'
    $copied=Wait-E2ECondition -TimeoutMs 3000 -Description 'ranked exact source path copied' -Condition {
        [string]::Equals(([string](Get-Clipboard -Raw)).TrimEnd([char[]]([string][char]13+[char]10)),$target,[StringComparison]::OrdinalIgnoreCase)
    }
    Check $copied.Pass 'Copy Path retargeted an immediate or outside duplicate basename.'
    # Snapshot refresh and palette focus transitions may retire client-held
    # providers. Reacquire by the original full target, never by row position.
    $fresh=ExactRankedRow $nested shared.bin
    Check $fresh.Pass 'Actual nested ranking target is no longer available.'
    Check ([bool](Invoke-UiaPattern -Driver $ctx.Driver -Element $fresh.Value -PatternName SelectionItem -Method GetSelection)) 'Palette transition lost the actual ranked selection.'
    [void]$observations.Add(@{id='fresh-ranked-target';row=(RowEvidence $fresh.Value)})
    Capture recursive-largest-files
    $null=Invoke-UiaPattern -Driver $ctx.Driver -Element $fresh.Value -PatternName Invoke -Method Invoke
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
    $nestedTile=@($tiles.Value.items | Where-Object { (Get-UiaElementIdentity -Driver $ctx.Driver -Element $_).Name -eq 'Nested' })
    Check ($nestedTile.Count -eq 1) 'Treemap has an ambiguous Nested tile.'
    $tileStatus=Wait-E2ECondition -TimeoutMs 10000 -Description 'settled expected logical completeness' -Condition {
        $state=[string](Get-UiaElementProperty -Driver $ctx.Driver -Element $nestedTile[0] -Name ItemStatus)
        if($PartialDescendant) { if($state -eq 'Partial') { return @{value=$state} } }
        elseif([string]::IsNullOrEmpty($state)) { return @{value=$state} }
        return $false
    }
    Check $tileStatus.Pass 'Treemap did not retain the expected complete/partial aggregate state.'
    $coverage=Wait-E2ECondition -TimeoutMs 4000 -Description 'visible Treemap coverage status' -Condition {
        foreach($child in @(Get-UiaChildren -Driver $ctx.Driver -Element $ctx.Main)) {
            $id=Get-UiaElementIdentity -Driver $ctx.Driver -Element $child
            if($id.ClassName -eq 'Static' -and $id.Name -eq 'Known data only — coverage may be partial; logical sizes shown.' -and
                -not (Get-UiaElementProperty -Driver $ctx.Driver -Element $child -Name IsOffscreen)) { return $id.Name }
        }
        return $false
    }
    Check $coverage.Pass 'Treemap coverage status is not visibly available.'
    $windowBounds=Get-UiaElementProperty -Driver $ctx.Driver -Element $ctx.Main -Name BoundingRectangle
    Check ([FfNativeInput]::MouseMove($windowBounds.x+30,$windowBounds.y+18)) 'Pointer-away input was rejected.'
    Capture hierarchical-treemap
    [void]$observations.Add(@{id='hierarchical-treemap';tileNames=$tiles.Value.names;itemStatus=$tileStatus.Value.value;coverage=$coverage.Value;pointerAway=$true;partialDescendantRequested=[bool]$PartialDescendant})
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
    try { if($ctx -and $ctx.Main -and (Test-UiaForegroundWindow -Driver $ctx.Driver -Element $ctx.Main)) { Capture ('failure-'+$script:phase) $false } } catch { }
} finally {
    if($ctx) {
        try {
            $cleanup=Remove-UiaScenarioContext -Context $ctx -ArtifactsDir $artifacts -LogFn {param($m) Write-Host $m}
            if(-not $cleanup.Pass -and -not $failure) { $failure='cleanup: '+($cleanup.Residue -join ',') }
        } catch { if(-not $failure) { $failure='cleanup: '+$_.Exception.Message } }
    }
    @{status=$(if($failure){'FAIL'}else{'PASS'});phase=$script:phase;failure=$failure;requestedTheme=$Theme;requestedWindowWidth=$WindowWidth;partialDescendantRequested=[bool]$PartialDescendant;scope='Seeded degraded known-file recursive ranking; exact public UIA/actions and file hashes; screenshots require separate visual review';observations=@($observations.ToArray());captures=@($captures.ToArray());cleanup=$cleanup;binaryProvenance=$gate.evidence} |
        ConvertTo-Json -Depth 22 | Set-Content (Join-Path $artifacts 'result.json') -Encoding utf8NoBOM
}
if($failure) { throw $failure }
