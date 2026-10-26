[CmdletBinding()]
param(
    [string]$RepoRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path,
    [Parameter(Mandatory)][string]$ArtifactsDir,
    [ValidateSet('all','cancellation')][string]$Filter='all',
    [ValidateSet(512,4096)][int]$BatchItems=512
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$repo=(Resolve-Path -LiteralPath $RepoRoot).Path
if (Test-Path -LiteralPath $ArtifactsDir) { throw 'Fresh evidence directory required.' }
$null=New-Item -ItemType Directory -Path $ArtifactsDir
$artifacts=(Resolve-Path -LiteralPath $ArtifactsDir).Path
Import-Module (Join-Path $repo 'verify/core/Provenance.psm1') -Force -DisableNameChecking
$exe=Join-Path $repo 'build/debug-diagnostic/src/ui/FastFiles.exe'
$gate=Invoke-ProvenanceGate -RepoRoot $repo -Binaries @(Get-DefaultCertificationBinaries -RepoRoot $repo -BuildDirectory (Join-Path $repo 'build/debug-diagnostic')) -Mode exact-tree -Stage owned-operations -PassThru
$gate.evidence | ConvertTo-Json -Depth 20 | Set-Content (Join-Path $artifacts 'provenance.json') -Encoding utf8NoBOM
if (-not $gate.ok) { throw $gate.message }
$e2e=Import-Module (Join-Path $repo 'verify/e2e/FastFilesE2E.psm1') -Force -DisableNameChecking -PassThru
Import-Module (Join-Path $repo 'verify/visual/VisualCapture.psm1') -Force -DisableNameChecking
$captureRun=New-VisualCaptureRun -Root (Join-Path $artifacts 'images') -ApplicationSha (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant() -RepoRoot $repo
$cases=[Collections.Generic.List[object]]::new()
$captures=[Collections.Generic.List[object]]::new()
$terminalObservations=[Collections.Generic.List[object]]::new()
$copySamples=[Collections.Generic.List[object]]::new()
$copyObservation=[pscustomobject]@{polls=0;last=$null}
$cancelInput=$null
$ctx=$null; $failure=$null; $lock=$null; $cleanup=$null; $recyclePending=$false
$titleObserver=$null; $script:operationTitleVersion=0L
$script:phase='startup'
function Set-Phase([string]$Name) {
    $script:phase=$Name
    Write-Host ('PHASE: '+$Name)
    @{status='IN_PROGRESS';phase=$Name;utc=[DateTime]::UtcNow.ToString('o');cases=@($cases.ToArray());captures=@($captures.ToArray());terminalObservations=@($terminalObservations.ToArray())} |
        ConvertTo-Json -Depth 14 | Set-Content (Join-Path $artifacts 'progress.json') -Encoding utf8NoBOM
}
function Check($Condition,[string]$Message) { if (-not $Condition) { throw $Message } }
function Navigate([string]$Path) {
    $r=Invoke-UiaAddressBarNavigate -Driver $ctx.Driver -Main $ctx.Main -ProcessId $ctx.Launched.Id -Path $Path -ExpectedLeaf ([IO.Path]::GetFileName($Path))
    if (-not $r.Pass) { throw ('Navigate: '+($r | ConvertTo-Json -Compress -Depth 4)) }
}
function Navigate-Bulk([string]$Path) {
    # The shared navigation helper recursively searches for absent tool nodes
    # and tab buttons. Keep this large-directory journey on direct native
    # children plus the existing pane/column seam; never walk thousands of rows.
    $null=Reset-UiaModifierKeys -Driver $ctx.Driver
    $null=Set-UiaForeground -Driver $ctx.Driver -Element $ctx.Main
    Check (Test-UiaForegroundWindow -Driver $ctx.Driver -Element $ctx.Main) 'Bulk navigation lacks owned foreground.'
    $null=Send-UiaInput -Driver $ctx.Driver -Keys '{CTRL}l'
    $chrome=Wait-UiaDirectChild -Driver $ctx.Driver -Parent $ctx.Main -ClassName FastFilesNavigationChrome -TimeoutMs 3000
    Check ([bool]$chrome) 'No native navigation chrome.'
    $children=@(Get-UiaChildren -Driver $ctx.Driver -Element $chrome | ForEach-Object {
        Get-UiaElementIdentity -Driver $ctx.Driver -Element $_
    })
    $children | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $artifacts ('address-'+[IO.Path]::GetFileName($Path)+'-children.json')) -Encoding utf8NoBOM
    $address=Wait-UiaDirectChild -Driver $ctx.Driver -Parent $chrome -ClassName Edit -TimeoutMs 3000
    Check ([bool]$address) 'No direct native address edit.'
    $addressIdentity=Get-UiaElementIdentity -Driver $ctx.Driver -Element $address
    Check ([string]::IsNullOrEmpty($addressIdentity.AutomationId) -or $addressIdentity.AutomationId -eq '0') 'Unexpected native edit identity.'
    $set=Set-UiaElementValue -Driver $ctx.Driver -Element $address -Value $Path
    Check $set.ok 'Cannot set actual address value.'
    $null=Send-UiaInput -Driver $ctx.Driver -Keys '{ENTER}'
    $validation=Get-Module UiAutomationValidation
    $landed=Wait-E2ECondition -TimeoutMs 10000 -Description 'exact bulk terminal column' -Condition {
        $columns=@(& $validation {param($c) Get-UiaColumnListElements -Driver $c.Driver -Main $c.Main -PaneId 'Pane.0' -ProcessId $c.Launched.Id} $ctx)
        if($columns.Count -eq 0) { return $false }
        $name=(Get-UiaElementIdentity -Driver $ctx.Driver -Element $columns[-1]).Name
        return [string]::Equals($name.TrimEnd('\'),$Path.TrimEnd('\'),[StringComparison]::OrdinalIgnoreCase)
    }
    Check $landed.Pass 'Bulk navigation did not reach exact path.'
}
function Select-One([string]$Name) {
    $r=& $e2e { param($c,$n) Select-E2EItem -Context $c -Name $n } $ctx $Name
    if (-not $r.Pass) { throw ('Select: '+($r | ConvertTo-Json -Compress -Depth 4)) }
}
function Command([string]$Query,[string]$Name,[string]$Id) {
    if($Id -in @('file.paste','file.delete','file.delete-permanently','file.undo')) {
        $script:operationTitleVersion=$titleObserver.Version
    }
    $r=& $e2e { param($c,$q,$n,$id) Invoke-E2ENavigationCommand -Context $c -Query $q -Name $n -CommandId $id } $ctx $Query $Name $Id
    if (-not $r.Pass) { throw ('Command: '+($r | ConvertTo-Json -Compress -Depth 4)) }
}
function Wait-Terminal {
    $r=Wait-E2ECondition -TimeoutMs 15000 -Description 'actual operation terminal title' -Condition {
        $title=$titleObserver.TerminalAfter($script:operationTitleVersion)
        if ($title) { return $title }
        return $false
    }
    Check $r.Pass 'No terminal operation title.'
    [void]$terminalObservations.Add(@{afterVersion=$script:operationTitleVersion;terminalVersion=$titleObserver.TerminalVersion;title=[string]$r.Value})
    return [string]$r.Value
}
function Hash([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
function Capture-Dialog($Dialog,[string]$Id) {
    $shot=Capture-VisualWindow -Run $captureRun -Element $Dialog -ScenarioId $Id -AssertionPass $true
    Check ($shot.Bounds.Dpi -gt 0) 'Capture lacks measured DPI.'
    [void]$captures.Add(@{id=$Id;image=[IO.Path]::GetFileName($shot.Path);sha256=$shot.Hash;bounds=$shot.Bounds})
}
function Get-OwnedRecycleMatch([string]$OriginalPath) {
    $full=[IO.Path]::GetFullPath($OriginalPath)
    $leaf=[IO.Path]::GetFileName($full)
    # Never enumerate details or act on unrelated Recycle Bin entries.
    Check ($leaf -match '^owned-[a-f0-9]{32}\.txt$') 'Recycle query requires a GUID-owned fixture.'
    $parent=[IO.Path]::GetDirectoryName($full).TrimEnd('\')
    $shell=New-Object -ComObject Shell.Application
    $bin=$null; $items=$null
    try {
        $bin=$shell.NameSpace(10)
        Check ([bool]$bin) 'Recycle Bin namespace unavailable.'
        $items=$bin.Items()
        for($i=0;$i -lt $items.Count;$i++) {
            $item=$items.Item($i)
            try {
                if(-not [string]::Equals([string]$item.Name,$leaf,[StringComparison]::OrdinalIgnoreCase)) { continue }
                $from=[string]$item.ExtendedProperty('System.Recycle.DeletedFrom')
                Check (-not [string]::IsNullOrWhiteSpace($from)) 'Owned recycle entry has no original parent.'
                if([string]::Equals($from.TrimEnd('\'),$parent,[StringComparison]::OrdinalIgnoreCase)) {
                    [pscustomobject]@{name=[string]$item.Name;originalParent=$from;originalPath=$full}
                }
            } finally { if($item) { [void][Runtime.InteropServices.Marshal]::ReleaseComObject($item) } }
        }
    } finally {
        foreach($com in @($items,$bin,$shell)) { if($com) { [void][Runtime.InteropServices.Marshal]::ReleaseComObject($com) } }
    }
}
try {
    $ctx=New-UiaScenarioContext -Exe $exe -RepoRoot $repo -ScenarioId owned-operations -LogFn {param($m) Write-Host $m}
    Check ($ctx -and $ctx.Main) 'Actual application unavailable.'
    if(-not ('FastFilesOperationTitleObserver' -as [type])) {
        Add-Type -Path (Join-Path $PSScriptRoot 'OperationTitleObserver.cs') -ReferencedAssemblies @(
            [System.Windows.Automation.AutomationElement].Assembly.Location,
            [System.Windows.Automation.AutomationProperty].Assembly.Location,
            [System.Windows.Point].Assembly.Location,
            'System.Threading'
        )
    }
    $titleObserver=[FastFilesOperationTitleObserver]::new($ctx.Main)
    $fixture=New-E2EFixture -SessionFixtureRoot $ctx.FixtureRoot
    # This scenario opens no Explorer windows. Leave Explorer cleanup's title
    # list empty; the marked, uniquely owned SessionRoot owns all fixture data.
    $source=Assert-E2EPathWithinRoot -Root $fixture.Root -Path (Join-Path $fixture.Root 'Source')
    $dest=Assert-E2EPathWithinRoot -Root $fixture.Root -Path (Join-Path $fixture.Root 'Target')
    $null=New-Item -ItemType Directory -Path $source,$dest
    $sentinel=Join-Path $dest 'sentinel.txt'
    [IO.File]::WriteAllText($sentinel,'Owned destination sentinel')
    $sentinelHash=Hash $sentinel
    $names=@('alpha.txt','beta.txt','unicode-λ.txt')
    $hashes=@{}
    foreach($name in $names) {
        [IO.File]::WriteAllText((Join-Path $source $name),('Distinct owned bytes '+$name))
        $hashes[$name]=Hash (Join-Path $source $name)
    }
    if($Filter -eq 'all') {
    foreach($mode in @('copy','move')) {
        Set-Phase ('multi-'+$mode)
        Navigate $source
        Select-One $names[0]
        foreach($name in $names[1..2]) {
            $item=& $e2e {param($c,$n) Get-E2EItem -Context $c -Name $n} $ctx $name
            $null=Invoke-UiaPattern -Driver $ctx.Driver -Element $item -PatternName SelectionItem -Method AddToSelection
        }
        foreach($name in $names) {
            $item=& $e2e {param($c,$n) Get-E2EItem -Context $c -Name $n} $ctx $name
            Check (Invoke-UiaPattern -Driver $ctx.Driver -Element $item -PatternName SelectionItem -Method GetSelection) ('Not selected: '+$name)
        }
        $validation=Get-Module UiAutomationValidation
        $columns=@(& $validation {param($c) Get-UiaColumnListElements -Driver $c.Driver -Main $c.Main -PaneId 'Pane.0' -ProcessId $c.Launched.Id} $ctx)
        Check ($columns.Count -gt 0) 'No terminal column.'
        $selected=@(Invoke-UiaPattern -Driver $ctx.Driver -Element $columns[-1] -PatternName Selection -Method GetSelection)
        $selectedNames=@($selected | ForEach-Object {(Get-UiaElementIdentity -Driver $ctx.Driver -Element $_).Name} | Sort-Object)
        Check (($selectedNames -join '|') -eq (($names | Sort-Object) -join '|')) 'Selected set is not exactly the owned three files.'
        if ($mode -eq 'copy') { Command copy Copy file.copy } else { Command cut Cut file.cut }
        $target=if($mode -eq 'copy') { $dest } else { Join-Path $fixture.Root 'Moved' }
        if($mode -eq 'move') { $null=New-Item -ItemType Directory -Path $target }
        Navigate $target
        Command paste Paste file.paste
        $terminal=Wait-Terminal
        Check ($terminal -match '3 of 3.*0 failed') ('Wrong batch result: '+$terminal)
        foreach($name in $names) {
            Check ((Hash (Join-Path $target $name)) -eq $hashes[$name]) ('Wrong destination: '+$name)
            if($mode -eq 'copy') { Check ((Hash (Join-Path $source $name)) -eq $hashes[$name]) 'Copy changed source.' }
            else { Check (-not(Test-Path -LiteralPath (Join-Path $source $name))) 'Move retained source.' }
        }
        Check ((Hash $sentinel) -eq $sentinelHash) 'Sentinel changed.'
        [void]$cases.Add(@{id='multi-'+$mode;pass=$true;terminal=$terminal;names=$names;selectedNames=$selectedNames;hashes=$hashes.Clone();sentinelPreserved=$true})
        Write-Host ('PASS: multi-'+$mode)
    }
    $recycled=Join-Path $source ('owned-'+[guid]::NewGuid().ToString('N')+'.txt')
    Set-Phase recycle-delete-undo
    [IO.File]::WriteAllText($recycled,'Owned recycle positive control')
    $recycleHash=Hash $recycled
    Check (@(Get-OwnedRecycleMatch $recycled).Count -eq 0) 'Positive control already exists in bin.'
    Navigate $source
    Select-One ([IO.Path]::GetFileName($recycled))
    $recyclePending=$true
    Command delete Delete file.delete
    $terminal=Wait-Terminal
    Check (-not(Test-Path -LiteralPath $recycled)) 'Recycle delete left source.'
    $binWait=Wait-E2ECondition -TimeoutMs 5000 -Description 'positive owned recycle entry' -Condition {
        return (@(Get-OwnedRecycleMatch $recycled).Count -eq 1)
    }
    Check $binWait.Pass 'Owned Recycle Bin positive control not observable; exclusion cannot be qualified.'
    Command undo 'Undo Last File Operation' file.undo
    $undoTerminal=Wait-Terminal
    Check ((Hash $recycled) -eq $recycleHash) 'Native undo did not restore exact bytes.'
    $recyclePending=$false
    Check (@(Get-OwnedRecycleMatch $recycled).Count -eq 0) 'Native undo left owned entry in bin.'
    [void]$cases.Add(@{id='recycle-delete-undo';pass=$true;terminal=$terminal;undo=$undoTerminal;hash=$recycleHash;namespacePositiveControl=$true})
    $permanent=Join-Path $source ('owned-'+[guid]::NewGuid().ToString('N')+'.txt')
    [IO.File]::WriteAllText($permanent,'Owned permanent delete fixture')
    $permanentHash=Hash $permanent
    Check (@(Get-OwnedRecycleMatch $permanent).Count -eq 0) 'Permanent fixture already exists in bin.'
    Navigate $source
    foreach($confirm in @($false,$true)) {
        Set-Phase ('permanent-confirm-'+$confirm)
        Select-One ([IO.Path]::GetFileName($permanent))
        Command permanent 'Delete permanently' file.delete-permanently
        $dialog=Find-UiaElement -Driver $ctx.Driver -Name 'Delete permanently' -ClassName '#32770' -ProcessId $ctx.Launched.Id -TimeoutMs 5000
        if(-not $confirm) { Capture-Dialog $dialog permanent-confirmation }
        $cancel=Find-UiaElement -Driver $ctx.Driver -Name Cancel -FromElement $dialog -TimeoutMs 1000
        Check (Get-UiaElementProperty -Driver $ctx.Driver -Element $cancel -Name HasKeyboardFocus) 'Permanent delete default is not Cancel.'
        $null=Set-UiaForeground -Driver $ctx.Driver -Element $dialog
        if($confirm) {
            $ok=Find-UiaElement -Driver $ctx.Driver -Name OK -FromElement $dialog -TimeoutMs 1000
            $r=& $e2e {param($c,$b,$d) Invoke-E2EButton -Context $c -Element $b -Owner $d} $ctx $ok $dialog
            Check $r 'Cannot activate permanent confirmation.'
        } else { $null=Send-UiaInput -Driver $ctx.Driver -Keys '{ENTER}' }
        Check (Wait-UiaElementGone -Driver $ctx.Driver -Name 'Delete permanently' -ClassName '#32770' -ProcessId $ctx.Launched.Id -TimeoutMs 5000) 'Permanent confirmation remains.'
        if($confirm) {
            $terminal=Wait-Terminal
            Check (-not(Test-Path -LiteralPath $permanent)) 'Confirmed permanent file still exists.'
            $observations=@()
            for($i=0;$i -lt 3;$i++) {
                $count=@(Get-OwnedRecycleMatch $permanent).Count
                Check ($count -eq 0) 'Permanent fixture was recycled.'
                $observations+=@{utc=[DateTime]::UtcNow.ToString('o');matches=$count}
                if($i -lt 2) { Start-Sleep -Milliseconds 300 }
            }
            Check ((Hash $recycled) -eq $recycleHash) 'Permanent operation changed restored positive control.'
            [void]$cases.Add(@{id='permanent-confirm';pass=$true;terminal=$terminal;calibratedShellExclusion=$observations})
        } else {
            Check ((Hash $permanent) -eq $permanentHash) 'Default Enter deleted or changed file.'
            [void]$cases.Add(@{id='permanent-default-cancel';pass=$true;hash=$permanentHash})
        }
        Check ((Hash $sentinel) -eq $sentinelHash) 'Sentinel changed.'
    }
    $lockedName='locked-source.txt'
    Set-Phase error-manual-recovery
    $healthyName='healthy-sibling.txt'
    foreach($name in @($lockedName,$healthyName)) {
        [IO.File]::WriteAllText((Join-Path $source $name),('Recovery fixture '+$name))
        $hashes[$name]=Hash (Join-Path $source $name)
    }
    $errorDest=Join-Path $fixture.Root 'Recovery'
    $null=New-Item -ItemType Directory -Path $errorDest
    $lock=[IO.FileStream]::new((Join-Path $source $lockedName),[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    Navigate $source
    Select-One $healthyName
    $item=& $e2e {param($c,$n) Get-E2EItem -Context $c -Name $n} $ctx $lockedName
    $null=Invoke-UiaPattern -Driver $ctx.Driver -Element $item -PatternName SelectionItem -Method AddToSelection
    Command copy Copy file.copy
    Navigate $errorDest
    Command paste Paste file.paste
    $terminal=Wait-Terminal
    Check ($terminal -match '1 failed') ('Missing truthful failure: '+$terminal)
    Check ($terminal -match 'stopped with errors|complete with errors') 'Failure is presented as ordinary completion or user cancellation.'
    Check ((Hash (Join-Path $errorDest $healthyName)) -eq $hashes[$healthyName]) 'Healthy sibling failed.'
    Check (-not(Test-Path -LiteralPath (Join-Path $errorDest $lockedName))) 'Locked file produced destination.'
    Capture-Dialog $ctx.Main operation-error-status
    Command errors 'Last Operation Errors' app.last-operation-errors
    $dialog=Find-UiaElement -Driver $ctx.Driver -Name 'Last operation errors' -ClassName '#32770' -ProcessId $ctx.Launched.Id -TimeoutMs 5000
    Capture-Dialog $dialog operation-error
    $children=@(Get-UiaChildren -Driver $ctx.Driver -Element $dialog | ForEach-Object {
        Get-UiaElementIdentity -Driver $ctx.Driver -Element $_
    })
    $children | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $artifacts 'error-dialog-children.json') -Encoding utf8NoBOM
    $texts=@($children | ForEach-Object { $_.Name }) -join [Environment]::NewLine
    Check ($texts.Contains((Join-Path $source $lockedName))) 'Error dialog lacks failed source path.'
    Check ($texts -match '0x[0-9A-Fa-f]{8}') 'Error dialog lacks HRESULT.'
    Check ($texts.Contains('source file is in use') -and $texts.Contains('Close the app')) 'Error dialog lacks actionable sharing guidance.'
    $null=Set-UiaForeground -Driver $ctx.Driver -Element $dialog
    $null=Send-UiaInput -Driver $ctx.Driver -Keys '{ENTER}'
    $lock.Dispose(); $lock=$null
    Navigate $source
    Select-One $lockedName
    Command copy Copy file.copy
    Navigate $errorDest
    Command paste Paste file.paste
    $recovery=Wait-Terminal
    Check ($recovery -match '1 of 1.*0 failed') ('Manual recovery failed: '+$recovery)
    Check ((Hash (Join-Path $errorDest $lockedName)) -eq $hashes[$lockedName]) 'Recovery bytes differ.'
    foreach($name in @($lockedName,$healthyName)) { Check ((Hash (Join-Path $source $name)) -eq $hashes[$name]) 'Recovery altered source.' }
    Check ((Hash $sentinel) -eq $sentinelHash) 'Recovery altered sentinel.'
    [void]$cases.Add(@{id='error-manual-recovery';pass=$true;terminal=$terminal;errorText=$texts;recovery=$recovery;hashes=$hashes})
    }
    # A real top-level batch, not a conflict-dialog Cancel or a pre-start cancel.
    # Bounded64MiB payload; no large benchmark fixture or production files.
    $bulkSource=Join-Path $fixture.Root 'BulkSource'
    Set-Phase in-flight-copy-fixture
    $bulkTarget=Join-Path $fixture.Root 'BulkTarget'
    $null=New-Item -ItemType Directory -Path $bulkSource,$bulkTarget
    $payload=[byte[]]::new(64MB / $BatchItems)
    for($i=0;$i -lt $payload.Length;$i++) { $payload[$i]=[byte]($i%251) }
    $payloadHash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($payload))
    $bulkCount=$BatchItems
    for($i=0;$i -lt $bulkCount;$i++) { [IO.File]::WriteAllBytes((Join-Path $bulkSource ('item-{0:D5}.bin' -f $i)),$payload) }
    Set-Phase in-flight-copy-source
    Navigate-Bulk $bulkSource
    $ready=Wait-E2ECondition -TimeoutMs 10000 -Description 'last bulk source identity is loaded' -Condition {
        try {
            return Find-UiaTerminalColumnItem -Driver $ctx.Driver -Main $ctx.Main -ProcessId $ctx.Launched.Id -Name ('item-{0:D5}.bin' -f ($bulkCount-1)) -PaneId 'Pane.0' -TimeoutMs 900
        } catch { return $false }
    }
    Check $ready.Pass 'Bulk source is not ready for Select All.'
    # Public Windows shortcuts avoid recursive palette-dismissal searches.
    $null=Send-UiaInput -Driver $ctx.Driver -Keys '{CTRL}a'
    $null=Send-UiaInput -Driver $ctx.Driver -Keys '{CTRL}c'
    Set-Phase in-flight-copy-destination
    Navigate-Bulk $bulkTarget
    $script:operationTitleVersion=$titleObserver.Version
    $null=Send-UiaInput -Driver $ctx.Driver -Keys '{CTRL}v'
    Set-Phase in-flight-copy-observation
    $samplingClock=[Diagnostics.Stopwatch]::StartNew()
    $started=Wait-E2ECondition -TimeoutMs 30000 -PollIntervalMs 25 -Description 'actual partially copied batch' -Condition {
        $count=[IO.Directory]::GetFiles($bulkTarget).Length
        # Read the existing managed public-Name observation; a synchronous UIA
        # query here can wait behind the worker's thousands of progress events.
        $title=$titleObserver.LatestTitle
        $sample=@{elapsedMs=$samplingClock.ElapsedMilliseconds;copied=$count;title=$title}
        $copyObservation.polls++
        $copyObservation.last=$sample
        if($copySamples.Count -lt 64) { [void]$copySamples.Add($sample) }
        if($count -gt 0 -and $count -lt $bulkCount -and $title -notmatch 'complete|cancelled') {
            return @{copied=$count;title=$title;utc=[DateTime]::UtcNow.ToString('o')}
        }
        return $false
    }
    Check $started.Pass 'Could not observe an actual in-flight partial copy; cancellation is not qualified.'
    Set-Phase in-flight-copy-cancel-input
    $inputClock=[Diagnostics.Stopwatch]::StartNew()
    $activated=Set-UiaForeground -Driver $ctx.Driver -Element $ctx.Main
    $foreground=Test-UiaForegroundWindow -Driver $ctx.Driver -Element $ctx.Main
    $focus=Get-UiaElementIdentity -Driver $ctx.Driver -Element ([System.Windows.Automation.AutomationElement]::FocusedElement)
    $cancelInput=@{activationReturned=$activated;foreground=$foreground;focusedElement=$focus;beforeTitle=$titleObserver.LatestTitle;beforeUtc=[DateTime]::UtcNow.ToString('o');afterInputMs=$null;result=$null;terminalMs=$null}
    Check ($activated -and $foreground) 'Cancellation lacks verified owned foreground.'
    $cancelInput.result=Send-UiaInput -Driver $ctx.Driver -Keys '{ESC}'
    $cancelInput.afterInputMs=$inputClock.ElapsedMilliseconds
    Check $cancelInput.result.ok 'Cancellation input was rejected.'
    $cancelled=Wait-Terminal
    $cancelInput.terminalMs=$inputClock.ElapsedMilliseconds
    Check ($cancelled -match 'cancelled') ('In-flight Escape did not cancel: '+$cancelled)
    Check ($cancelled -match (' of '+$bulkCount+' ')) 'Cancellation did not target the complete selected batch.'
    $copied=@([IO.Directory]::GetFiles($bulkTarget))
    Check ($copied.Count -gt 0 -and $copied.Count -lt $bulkCount) 'Cancellation did not leave an observed partial set.'
    foreach($path in $copied) {
        $leaf=[IO.Path]::GetFileName($path)
        Check ($leaf -match '^item-(\d{5})\.bin$' -and [int]$Matches[1] -lt $bulkCount -and
            (Test-Path -LiteralPath (Join-Path $bulkSource $leaf))) 'Cancelled copy produced an unexpected filename.'
        Check ((Hash $path) -eq $payloadHash) 'Cancelled copy left damaged destination bytes.'
    }
    $originals=@([IO.Directory]::GetFiles($bulkSource))
    Check ($originals.Count -eq $bulkCount) 'Cancellation lost source files.'
    for($i=0;$i -lt $bulkCount;$i++) {
        Check ((Hash (Join-Path $bulkSource ('item-{0:D5}.bin' -f $i))) -eq $payloadHash) 'Cancellation changed a source name or bytes.'
    }
    Check ((Hash $sentinel) -eq $sentinelHash) 'Cancellation changed unrelated sentinel.'
    [void]$cases.Add(@{id='in-flight-copy-cancel';pass=$true;observed=$started.Value;prepareElapsedMs=$started.ElapsedMs;terminal=$cancelled;copied=$copied.Count;total=$bulkCount;payloadHash=$payloadHash;allSourceAndRemainingDestinationHashesMatch=$true})
    Check ($cases.Count -eq $(if($Filter -eq 'all') {7} else {1})) 'Requested case coverage is incomplete.'
    Set-Phase verified
} catch {
    $failure=$script:phase+': '+$_.Exception.Message
    Write-Host ('FAIL: '+$failure)
} finally {
    if($lock) { $lock.Dispose() }
    if($ctx) {
        if($recyclePending) {
            # Only this scenario's most recent captured recycle operation is
            # outstanding. Restore it through the normal session Undo command.
            try {
                $matches=@(Get-OwnedRecycleMatch $recycled).Count
                if(-not(Test-Path -LiteralPath $recycled) -and $matches -eq 1) {
                    Command undo 'Undo Last File Operation' file.undo
                    $null=Wait-Terminal
                }
                Check ((Hash $recycled) -eq $recycleHash) 'Positive-control cleanup failed.'
                Check (@(Get-OwnedRecycleMatch $recycled).Count -eq 0) 'Positive-control cleanup left an owned bin entry.'
            } catch { $failure=([string]$failure+'; owned recycle cleanup: '+$_.Exception.Message) }
        }
        # Dismiss only a possible dialog belonging to this owned application.
        try { $null=Set-UiaForeground -Driver $ctx.Driver -Element $ctx.Main; $null=Send-UiaInput -Driver $ctx.Driver -Keys '{ESC}' } catch {}
        if($titleObserver) {
            try { $titleObserver.Dispose() }
            catch { $failure=([string]$failure+'; title observer cleanup: '+$_.Exception.Message) }
        }
        $cleanup=Remove-UiaScenarioContext -Context $ctx -ArtifactsDir $artifacts -LogFn {param($m) Write-Host $m}
    }
    @{filter=$Filter;cases=@($cases.ToArray());captures=@($captures.ToArray());terminalObservations=@($terminalObservations.ToArray());copySamples=@($copySamples.ToArray());copyObservation=$copyObservation;cancelInput=$cancelInput;failure=$failure;cleanup=$cleanup;scope='Only requested cases are certified; actual owned filesystem operations with public input/UIA and exact content/identity checks'} |
        ConvertTo-Json -Depth 14 | Set-Content (Join-Path $artifacts 'result.json') -Encoding utf8NoBOM
}
if($failure) { throw $failure }
Check $cleanup.Pass 'Owned cleanup failed.'
