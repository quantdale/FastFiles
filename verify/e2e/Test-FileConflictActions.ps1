# original-vision-journey-recovery 2.10: public native conflict decisions on owned fixtures.
[CmdletBinding()]
param(
    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path,
    [Parameter(Mandatory)][string] $ArtifactsDir,
    [ValidateSet('single','apply-all')][string] $Filter = 'single'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = (Resolve-Path -LiteralPath $RepoRoot).Path
$artifacts = [IO.Path]::GetFullPath($ArtifactsDir)
if (Test-Path -LiteralPath $artifacts) { throw 'A fresh artifact directory is required; existing evidence is preserved.' }
New-Item -ItemType Directory -Path $artifacts | Out-Null
Import-Module (Join-Path $repo 'verify/core/Provenance.psm1') -Force -DisableNameChecking
$exe = Join-Path $repo 'build/debug-diagnostic/src/ui/FastFiles.exe'
$binaries = @(Get-DefaultCertificationBinaries -RepoRoot $repo -BuildDirectory (Split-Path (Split-Path (Split-Path $exe))))
$gate = Invoke-ProvenanceGate -RepoRoot $repo -Binaries $binaries -Mode exact-tree -Stage conflict-actions -PassThru
$gate.evidence | ConvertTo-Json -Depth 20 | Set-Content (Join-Path $artifacts 'provenance.json') -Encoding utf8NoBOM
if (-not $gate.ok) { throw $gate.message }
$e2e = Import-Module (Join-Path $repo 'verify/e2e/FastFilesE2E.psm1') -Force -DisableNameChecking -PassThru
$validation = Get-Module UiAutomationValidation
# This driver uses braced modifiers, not WinForms SendKeys '+' notation.
$backTabKeys = '{SHIFT}{TAB}'
$backTab = Send-UiaInput -Driver $null -Keys $backTabKeys -NoSend
if ($backTab.sequence.Count -ne 4 -or $backTab.sequence[0].vk -ne 0x10 -or -not $backTab.sequence[0].down -or
    $backTab.sequence[1].vk -ne 9 -or -not $backTab.sequence[1].down -or
    $backTab.sequence[2].vk -ne 9 -or $backTab.sequence[2].down -or
    $backTab.sequence[3].vk -ne 0x10 -or $backTab.sequence[3].down) {
    throw 'Native input preflight did not generate a real Shift+Tab chord.'
}
Import-Module (Join-Path $repo 'verify/visual/VisualCapture.psm1') -Force -DisableNameChecking
$run = New-VisualCaptureRun -Root (Join-Path $artifacts 'images') -ApplicationSha (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant() -RepoRoot $repo
$results = [Collections.Generic.List[object]]::new()
$cleanups = [Collections.Generic.List[object]]::new()
$captures = [Collections.Generic.List[object]]::new()
$means = @{}
$failure = $null
function Invoke-ApplyAllQualification {
    $ctx=$null; $observer=$null; $dialog=$null
    function Require([bool]$Condition,[string]$Message) { if(-not $Condition) { throw $Message } }
    function FileHash([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
    function Inventory([string]$Root) {
        $map=@{}
        foreach($file in @(Get-ChildItem -LiteralPath $Root -Recurse -File)) {
            $map[[IO.Path]::GetRelativePath($Root,$file.FullName)]=FileHash $file.FullName
        }
        return $map
    }
    function Assert-Inventory([string]$Root,$Expected) {
        $actual=Inventory $Root
        Require ($actual.Count -eq $Expected.Count) ('Unexpected complete file count under '+$Root)
        foreach($key in $Expected.Keys) {
            Require ($actual.ContainsKey($key) -and $actual[$key] -eq $Expected[$key]) ('Wrong or missing file bytes: '+$key)
        }
    }
    function Navigate-All([string]$Path) {
        $nav=Invoke-UiaAddressBarNavigate -Driver $ctx.Driver -Main $ctx.Main -ProcessId $ctx.Launched.Id -Path $Path -ExpectedLeaf ([IO.Path]::GetFileName($Path))
        Require $nav.Pass ('Batch navigation failed: '+$nav.Detail)
    }
    function Command-All([string]$Query,[string]$Name,[string]$Id) {
        $r=& $e2e {param($c,$q,$n,$id) Invoke-E2ENavigationCommand -Context $c -Query $q -Name $n -CommandId $id} $ctx $Query $Name $Id
        Require $r.Pass ('Batch command failed: '+$r.Detail)
        return $r
    }
    function Select-AllNames([string[]]$Names) {
        for($index=0;$index -lt $Names.Count;$index++) {
            $wait=& $e2e {param($c,$n) Wait-E2EItem -Context $c -Name $n -TimeoutMs 10000} $ctx $Names[$index]
            Require $wait.Pass ('Missing exact batch source: '+$Names[$index])
            if($index -eq 0) { $null=Invoke-UiaPattern -Driver $ctx.Driver -Element $wait.Value -PatternName SelectionItem -Method Select }
            else { $null=Invoke-UiaPattern -Driver $ctx.Driver -Element $wait.Value -PatternName SelectionItem -Method AddToSelection }
        }
        $columns=@(& $validation {param($c) Get-UiaColumnListElements -Driver $c.Driver -Main $c.Main -PaneId Pane.0 -ProcessId $c.Launched.Id} $ctx)
        Require ($columns.Count -gt 0) 'No actual batch source column.'
        $selected=@(Invoke-UiaPattern -Driver $ctx.Driver -Element $columns[-1] -PatternName Selection -Method GetSelection)
        $selectedNames=@($selected | ForEach-Object {(Get-UiaElementIdentity -Driver $ctx.Driver -Element $_).Name} | Sort-Object)
        Require (($selectedNames -join '|') -ceq (($Names | Sort-Object) -join '|')) 'Batch selected names differ from the exact owned set.'
    }
    function Begin-Batch([string]$Mode,[string]$Source,[string]$Target,[string[]]$Names) {
        Navigate-All $Source
        Select-AllNames $Names
        if($Mode -eq 'Copy') { $null=Command-All copy Copy file.copy } else { $null=Command-All cut Cut file.cut }
        Navigate-All $Target
        $script:batchTitleVersion=$observer.Version
        $null=Command-All paste Paste file.paste
    }
    function Conflict-Controls {
        $found=Find-UiaElement -Driver $ctx.Driver -ClassName FastFilesConflictDialog -ProcessId $ctx.Launched.Id -TimeoutMs 7000
        Require ([bool]$found) 'Owned conflict dialog not shown.'
        $controls=@{dialog=$found}
        $controls.bodyNames=@(Get-UiaChildren -Driver $ctx.Driver -Element $found | ForEach-Object {
            $identity=Get-UiaElementIdentity -Driver $ctx.Driver -Element $_
            if($identity.ClassName -eq 'Static') { $identity.Name }
        })
        foreach($name in @('Replace','Skip','Keep both','Cancel','Do this for all remaining conflicts')) {
            $controls[$name]=Find-UiaElement -Driver $ctx.Driver -FromElement $found -Name $name -TimeoutMs 1500
        }
        Require ([bool](Get-UiaElementProperty -Driver $ctx.Driver -Element $controls.Skip -Name HasKeyboardFocus)) 'A fresh conflict must initially focus Skip.'
        $state=Get-UiaElementProperty -Driver $ctx.Driver -Element $controls['Do this for all remaining conflicts'] -Name ToggleState
        Require ($null -ne $state -and [string]$state -eq 'Off') 'Fresh request did not expose an unchecked apply-all checkbox.'
        return $controls
    }
    function Click-Control($Control) {
        $bounds=Get-UiaElementProperty -Driver $ctx.Driver -Element $Control -Name BoundingRectangle
        Require ($bounds -and $bounds.width -gt 0 -and $bounds.height -gt 0 -and
            -not (Get-UiaElementProperty -Driver $ctx.Driver -Element $Control -Name IsOffscreen)) 'Actual conflict control is not reachable.'
        $r=Send-UiaMouseInput -Driver $ctx.Driver -X ([int]($bounds.x+$bounds.width/2)) -Y ([int]($bounds.y+$bounds.height/2)) -Button Left -Action Click
        Require $r.ok 'Real conflict pointer input rejected.'
        return $r
    }
    function Choose-All($Controls,[string]$Choice,[bool]$Apply,[string]$CaptureId='') {
        $null=Set-UiaForeground -Driver $ctx.Driver -Element $Controls.dialog
        Require (Test-UiaForegroundWindow -Driver $ctx.Driver -Element $Controls.dialog) 'Conflict decision lacks owned foreground.'
        $toggleInput=$null
        if($Apply) {
            $toggleInput=Click-Control $Controls['Do this for all remaining conflicts']
            $checked=Wait-E2ECondition -TimeoutMs 2000 -Description 'real apply-all checkbox checked' -Condition {
                [string](Get-UiaElementProperty -Driver $ctx.Driver -Element $Controls['Do this for all remaining conflicts'] -Name ToggleState) -eq 'On'
            }
            Require $checked.Pass 'Real checkbox input did not produce checked state.'
            if($CaptureId) {
                $shot=Capture-VisualWindow -Run $run -Element $Controls.dialog -ScenarioId $CaptureId -AssertionPass $true
                Require ($shot.Bounds.Dpi -gt 0) 'Checked apply-all capture lacks measured DPI.'
                [void]$captures.Add(@{id=$CaptureId;image=[IO.Path]::GetFileName($shot.Path);sha256=$shot.Hash;bounds=$shot.Bounds})
            }
        }
        $decisionInput=Click-Control $Controls[$Choice]
        return @{choice=$Choice;apply=$Apply;checkboxInput=$toggleInput;decisionInput=$decisionInput}
    }
    function Terminal-All {
        $done=Wait-E2ECondition -TimeoutMs 15000 -Description 'new terminal outcome after all remaining conflicts' -Condition {
            $text=$observer.TerminalAfter($script:batchTitleVersion)
            if($text) { return [string]$text }; return $false
        }
        Require $done.Pass 'No terminal outcome; an unanswered subsequent conflict cannot count as apply-all success.'
        return [string]$done.Value
    }
    function Scope-Reset([string]$Mode,[string]$CaseRoot) {
        $source=Join-Path $CaseRoot 'reset-source'; $target=Join-Path $CaseRoot 'reset-target'
        $null=New-Item -ItemType Directory -Path $source,$target
        [IO.File]::WriteAllText((Join-Path $source 'reset.txt'),'new reset bytes')
        [IO.File]::WriteAllText((Join-Path $source 'fresh.txt'),'new noncollision bytes')
        [IO.File]::WriteAllText((Join-Path $target 'reset.txt'),'existing reset bytes')
        $expected=Inventory $CaseRoot
        Begin-Batch $Mode $source $target @('fresh.txt','reset.txt')
        $controls=Conflict-Controls
        $decisionEvidence=Choose-All $controls Cancel $false
        $terminal=Terminal-All
        Require ($terminal -match 'cancelled') 'Fresh request did not cancel at its own prompt.'
        Assert-Inventory $CaseRoot $expected
        return @{freshUncheckedPrompt=$true;input=$decisionEvidence;terminal=$terminal;allNamesAndHashesUnchanged=$true}
    }
    try {
        $ctx=New-UiaScenarioContext -Exe $exe -RepoRoot $repo -ScenarioId conflict-apply-all -InvocationId ([guid]::NewGuid().ToString('N')) -LogFn {param($m) Write-Host $m}
        Require ([bool]$ctx -and [bool]$ctx.Main -and $ctx.Ready) 'Owned apply-all application unavailable.'
        if(-not ('FastFilesOperationTitleObserver' -as [type])) {
            Add-Type -Path (Join-Path $PSScriptRoot 'OperationTitleObserver.cs') -ReferencedAssemblies @(
                [System.Windows.Automation.AutomationElement].Assembly.Location,
                [System.Windows.Automation.AutomationProperty].Assembly.Location,
                [System.Windows.Point].Assembly.Location,'System.Threading')
        }
        $observer=[FastFilesOperationTitleObserver]::new($ctx.Main)
        $fixture=New-E2EFixture -SessionFixtureRoot $ctx.FixtureRoot
        foreach($mode in @('Copy','Move')) {
            foreach($choice in @('Replace','Skip','Keep both')) {
                $id=$mode.ToLowerInvariant()+'-'+$choice.Replace(' ','-').ToLowerInvariant()
                $caseRoot=Assert-E2EPathWithinRoot -Root $fixture.Root -Path (Join-Path $fixture.Root ('apply-'+$id))
                $source=Join-Path $caseRoot Source; $target=Join-Path $caseRoot Target
                $null=New-Item -ItemType Directory -Path $source,$target
                $names=@('alpha.txt','beta.txt','gamma.txt','fresh.txt')
                $incoming=@{}; $existing=@{}
                foreach($name in $names) {
                    [IO.File]::WriteAllText((Join-Path $source $name),('incoming '+$id+' '+$name))
                    $incoming[$name]=FileHash (Join-Path $source $name)
                    if($name -ne 'fresh.txt') {
                        [IO.File]::WriteAllText((Join-Path $target $name),('existing '+$id+' '+$name))
                        $existing[$name]=FileHash (Join-Path $target $name)
                    }
                }
                [IO.File]::WriteAllText((Join-Path $target 'alpha (2).txt'),'suffix sentinel')
                [IO.File]::WriteAllText((Join-Path $target 'unrelated-sentinel.txt'),'unrelated owned sentinel')
                $expected=Inventory $caseRoot
                Begin-Batch $mode $source $target $names
                $controls=Conflict-Controls
                $decisionEvidence=Choose-All $controls $choice $true $(if($id -eq 'copy-replace') {'apply-all-checked'} else {''})
                $terminal=Terminal-All
                Require ($terminal -match 'complete' -and $terminal -match '0 failed' -and $terminal -match ' of 4') 'Apply-all did not finish the complete selected batch.'
                if($choice -eq 'Skip') { Require ($terminal -match '3 skipped') 'Three skipped collisions were not counted truthfully.' }
                foreach($name in $names) {
                    $collision=$name -ne 'fresh.txt'
                    $skipped=$collision -and $choice -eq 'Skip'
                    if(-not $skipped) {
                        $output=if($collision -and $choice -eq 'Keep both') {
                            [IO.Path]::GetFileNameWithoutExtension($name)+$(if($name -eq 'alpha.txt') {' (3).txt'} else {' (2).txt'})
                        } else { $name }
                        $expected[(Join-Path Target $output)]=$incoming[$name]
                        if($mode -eq 'Move') { [void]$expected.Remove((Join-Path Source $name)) }
                    }
                }
                Assert-Inventory $caseRoot $expected
                $reset=Scope-Reset $mode $caseRoot
                [void]$results.Add(@{id=$id;pass=$true;input=$decisionEvidence;terminal=$terminal;expectedInventory=$expected;allNamesAndHashesMatch=$true;scopeReset=$reset})
                Write-Host ('PASS: '+$id+'; real checked decision, all collision/noncollision outcomes, next-request reset')
            }
        }
        # Default Skip must affect one collision only. A second prompt followed
        # by Cancel preserves even the noncolliding source, since preflight is
        # complete before any mutation is submitted.
        $boundary=Join-Path $fixture.Root 'unchecked-boundary'; $source=Join-Path $boundary Source; $target=Join-Path $boundary Target
        $null=New-Item -ItemType Directory -Path $source,$target
        foreach($name in @('one.txt','two.txt','fresh.txt')) { [IO.File]::WriteAllText((Join-Path $source $name),('incoming '+$name)) }
        foreach($name in @('one.txt','two.txt')) { [IO.File]::WriteAllText((Join-Path $target $name),('existing '+$name)) }
        $expected=Inventory $boundary
        Begin-Batch Copy $source $target @('one.txt','two.txt','fresh.txt')
        $first=Conflict-Controls
        Require (($first.bodyNames -join '|') -like '*one.txt*') 'First unchecked conflict did not identify one.txt.'
        $null=Set-UiaForeground -Driver $ctx.Driver -Element $first.dialog
        Require (Test-UiaForegroundWindow -Driver $ctx.Driver -Element $first.dialog) 'Default Skip lacks owned foreground.'
        $defaultInput=Send-UiaInput -Driver $ctx.Driver -Keys '{ENTER}'
        Require $defaultInput.ok 'Default Skip Enter rejected.'
        $second=Wait-E2ECondition -TimeoutMs 5000 -Description 'distinct second unchecked collision' -Condition {
            try {
                $controls=Conflict-Controls
                if(($controls.bodyNames -join '|') -like '*two.txt*' -and ($controls.bodyNames -join '|') -notlike '*one.txt*') { return $controls }
            } catch { }
            return $false
        }
        Require $second.Pass 'Default Skip did not advance to the distinct two.txt conflict.'
        $null=Choose-All $second.Value Cancel $false
        $terminal=Terminal-All
        Require ($terminal -match 'cancelled') 'Second unchecked collision did not cancel the plan.'
        Assert-Inventory $boundary $expected
        [void]$results.Add(@{id='unchecked-skip-then-cancel';pass=$true;defaultInput=$defaultInput;firstPromptBody=$first.bodyNames;secondPromptBody=$second.Value.bodyNames;secondPrompt=$true;terminal=$terminal;allNamesAndHashesUnchanged=$true})
        Require ($results.Count -eq 7) 'Apply-all requested coverage incomplete.'
        $closed=& $e2e {param($c) Close-E2EContextWindow -Context $c} $ctx
        Require ([bool]$closed) 'Owned apply-all app did not close normally.'
    } finally {
        if($observer) { $observer.Dispose() }
        if($ctx) {
            $cleanup=Remove-UiaScenarioContext -Context $ctx -ArtifactsDir $artifacts -LogFn {param($m) Write-Host $m}
            [void]$cleanups.Add($cleanup)
            Require $cleanup.Pass 'Owned apply-all lifecycle cleanup failed.'
        }
    }
}
try {
    if($Filter -eq 'apply-all') { Invoke-ApplyAllQualification }
    else {
    foreach ($theme in @('Light', 'Dark')) {
        $ctx = $null
        $dialog = $null
        try {
            # Seed ordinary persisted settings only inside a fresh session sandbox.
            # No system preference, production profile or hidden application hook changes.
            $sandbox = & $validation { New-UiaSessionSandbox -ScenarioId conflict-actions -InvocationId ([guid]::NewGuid().ToString('N')) }
            $settingsDir = Join-Path $sandbox.SessionRoot 'LocalAppData/FastFiles'
            New-Item -ItemType Directory -Path $settingsDir -Force | Out-Null
            @{schemaVersion=1;appearance=@{theme=$theme;density='Compact'}} | ConvertTo-Json -Depth 3 |
                Set-Content (Join-Path $settingsDir 'settings.json') -Encoding utf8NoBOM
            $ctx = New-UiaScenarioContext -Exe $exe -RepoRoot $repo -ScenarioId ('conflict-'+$theme) -SessionRoot $sandbox.SessionRoot -LogFn {param($m) Write-Host $m}
            if (-not $ctx -or -not $ctx.Main) { throw 'The actual application window is unavailable.' }
            $fixture = New-E2EFixture -SessionFixtureRoot $ctx.FixtureRoot
            # Marked SessionRoot owns fixture cleanup; names never authorize
            # closing unrelated Explorer windows.
            $sentinel = Join-Path $fixture.Destination 'unrelated-sentinel.txt'
            Set-Content -LiteralPath $sentinel -Value 'untouched owned sentinel' -Encoding utf8NoBOM
            $sentinelHash = (Get-FileHash -LiteralPath $sentinel -Algorithm SHA256).Hash
            foreach ($choice in @('Cancel', 'Skip', 'Keep both', 'Replace')) {
                $id = $theme.ToLowerInvariant()+'-'+$choice.Replace(' ', '-').ToLowerInvariant()
                $name = 'collision-'+$id+'.txt'
                $source = Assert-E2EPathWithinRoot -Root $fixture.Root -Path (Join-Path $fixture.Operations $name)
                $destination = Assert-E2EPathWithinRoot -Root $fixture.Root -Path (Join-Path $fixture.Destination $name)
                Set-Content -LiteralPath $source -Value ('incoming '+$id) -Encoding utf8NoBOM
                Set-Content -LiteralPath $destination -Value ('existing '+$id) -Encoding utf8NoBOM
                $sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
                $destinationHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
                & $e2e {param($context,$fixture,$name)
                    $nav = Invoke-UiaAddressBarNavigate -Driver $context.Driver -Main $context.Main -ProcessId $context.Launched.Id -Path $fixture.Operations -ExpectedLeaf Operations
                    if (-not $nav.Pass) { throw $nav.Detail }
                    $selected = Select-E2EItem -Context $context -Name $name -TimeoutMs 10000
                    if (-not $selected.Pass) { throw $selected.Detail }
                    $copy = Invoke-E2ENavigationCommand -Context $context -Query copy -Name Copy -CommandId file.copy
                    if (-not $copy.Pass) { throw $copy.Detail }
                    $nav = Invoke-UiaAddressBarNavigate -Driver $context.Driver -Main $context.Main -ProcessId $context.Launched.Id -Path $fixture.Destination -ExpectedLeaf Destination
                    if (-not $nav.Pass) { throw $nav.Detail }
                    $paste = Invoke-E2ENavigationCommand -Context $context -Query paste -Name Paste -CommandId file.paste
                    if (-not $paste.Pass) { throw $paste.Detail }
                } $ctx $fixture $name
                $dialog = Find-UiaElement -Driver $ctx.Driver -ClassName FastFilesConflictDialog -ProcessId $ctx.Launched.Id -TimeoutMs 8000
                $bounds = Get-UiaElementProperty -Driver $ctx.Driver -Element $dialog -Name BoundingRectangle
                $controls = @{}
                $missing = @()
                foreach ($label in @('Replace', 'Skip', 'Keep both', 'Cancel', 'Do this for all remaining conflicts')) {
                    try { $controls[$label] = Find-UiaElement -Driver $ctx.Driver -Name $label -FromElement $dialog -TimeoutMs 1000 }
                    catch { if ($_.Exception.Message -notlike '*UiaTargetNotFound*') { throw }; $missing += $label }
                }
                # Capturing the real visible dialog is independent of action readiness.
                # Missing names still fail below; this frame never certifies their presence.
                if ($choice -eq 'Cancel') {
                    $shot = Capture-VisualWindow -Run $run -Element $dialog -ScenarioId ('conflict-'+$id) -AssertionPass ([bool]$dialog)
                    if ($shot.Bounds.Dpi -le 0) { throw 'Conflict capture has no measured window DPI.' }
                    [void]$captures.Add(@{id=$id;theme=$theme;image=[IO.Path]::GetFileName($shot.Path);sha256=$shot.Hash;bounds=$shot.Bounds})
                    Add-Type -AssemblyName System.Drawing
                    $bitmap = [Drawing.Bitmap]::new($shot.Path)
                    try {
                        $sum = 0.0; $samples = 0
                        for ($y=45; $y -lt $bitmap.Height-10; $y+=11) {
                            for ($x=10; $x -lt $bitmap.Width-10; $x+=11) {
                                $pixel=$bitmap.GetPixel($x,$y);$sum+=($pixel.R+$pixel.G+$pixel.B)/3.0;$samples++
                            }
                        }
                        $means[$theme] = $sum / $samples
                    } finally { $bitmap.Dispose() }
                }
                if ($missing.Count -gt 0) { throw ('Named conflict actions missing: '+($missing -join ', ')) }
                foreach ($control in $controls.Values) {
                    $rect = Get-UiaElementProperty -Driver $ctx.Driver -Element $control -Name BoundingRectangle
                    if ($rect.width -le 0 -or $rect.height -le 0 -or $rect.x -lt $bounds.x -or $rect.y -lt $bounds.y -or
                        $rect.x+$rect.width -gt $bounds.x+$bounds.width -or $rect.y+$rect.height -gt $bounds.y+$bounds.height) {
                        throw 'A conflict decision has clipped or empty physical bounds.'
                    }
                }
                $initial = Wait-E2ECondition -TimeoutMs 1500 -Description 'safe default Skip focus' -Condition {
                    [bool](Get-UiaElementProperty -Driver $ctx.Driver -Element $controls['Skip'] -Name HasKeyboardFocus)
                }
                if (-not $initial.Pass) { throw 'Skip did not retain initial keyboard focus.' }
                $tabOrder = @('Keep both','Cancel','Do this for all remaining conflicts','Replace','Skip')
                foreach ($label in $tabOrder) {
                    $null = Send-UiaInput -Driver $ctx.Driver -Keys '{TAB}'
                    $focused = Wait-E2ECondition -TimeoutMs 1500 -Description ('Tab focus '+$label) -Condition {
                        [bool](Get-UiaElementProperty -Driver $ctx.Driver -Element $controls[$label] -Name HasKeyboardFocus)
                    }
                    if (-not $focused.Pass) { throw ('Keyboard Tab did not reach '+$label) }
                }
                $inputRoute = 'native Invoke or real pointer fallback'
                if ($theme -eq 'Light') {
                    $clicked = & $e2e {param($context,$element,$owner) Invoke-E2EButton -Context $context -Element $element -Owner $owner} $ctx $controls[$choice] $dialog
                    if (-not $clicked) { throw ('Native input could not activate '+$choice) }
                } else {
                    $inputRoute = 'real keyboard'
                    if ($choice -eq 'Keep both' -or $choice -eq 'Replace') {
                        $keys = if ($choice -eq 'Replace') { $backTabKeys } else { '{TAB}' }
                        $null = Send-UiaInput -Driver $ctx.Driver -Keys $keys
                        $focused = Wait-E2ECondition -TimeoutMs 1500 -Description ('keyboard activation target '+$choice) -Condition {
                            [bool](Get-UiaElementProperty -Driver $ctx.Driver -Element $controls[$choice] -Name HasKeyboardFocus)
                        }
                        if (-not $focused.Pass) { throw ('Keyboard activation target was not focused: '+$choice) }
                    }
                    $keys = if ($choice -eq 'Cancel') { '{ESC}' } else { ' ' }
                    $null = Send-UiaInput -Driver $ctx.Driver -Keys $keys
                }
                if (-not (Wait-UiaElementGone -Driver $ctx.Driver -ClassName FastFilesConflictDialog -ProcessId $ctx.Launched.Id -TimeoutMs 5000 -PollIntervalMs 100)) { throw 'Conflict dialog did not close.' }
                $dialog = $null
                $copyPath = Join-Path $fixture.Destination ([IO.Path]::GetFileNameWithoutExtension($name)+' (2).txt')
                $expectedHash = if ($choice -eq 'Replace') { $sourceHash } else { $destinationHash }
                $outcome = Wait-E2ECondition -TimeoutMs 8000 -Description ('filesystem decision '+$id) -Condition {
                    ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -eq $expectedHash) -and
                    ($choice -ne 'Keep both' -or ((Test-Path -LiteralPath $copyPath) -and (Get-FileHash -LiteralPath $copyPath -Algorithm SHA256).Hash -eq $sourceHash))
                }
                Start-Sleep -Milliseconds 300
                $preserved = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -eq $sourceHash -and
                    (Get-FileHash -LiteralPath $sentinel -Algorithm SHA256).Hash -eq $sentinelHash
                if (-not $outcome.Pass -or -not $preserved) { throw ('Wrong filesystem result for '+$id) }
                if ($choice -ne 'Keep both' -and (Test-Path -LiteralPath $copyPath)) { throw 'Unexpected duplicate after a different conflict decision.' }
                [void]$results.Add(@{id=$id;pass=$true;inputRoute=$inputRoute;initialSkipFocus=$true;tabOrder=$tabOrder;sourceAndSentinelPreserved=$preserved;bounds=$bounds})
                Write-Host ('PASS: '+$id+'; named controls, physical bounds, Tab traversal, exact content outcome')
            }
        } finally {
            if ($dialog -and $ctx) { $null=Send-UiaInput -Driver $ctx.Driver -Keys '{ESC}' }
            if ($ctx) {
                $cleanup=Remove-UiaScenarioContext -Context $ctx -LogFn {param($m) Write-Host $m}
                [void]$cleanups.Add($cleanup)
                if (-not $cleanup.Pass) { throw 'Owned lifecycle cleanup failed.' }
            }
        }
    }
    if ($means['Light'] -le 128 -or $means['Dark'] -ge 128) { throw 'Actual conflict pixels did not establish light and dark themes.' }
    }
} catch {
    $failure=$_.Exception.Message
} finally {
    @{filter=$Filter;passed=$results.Count;failure=$failure;cases=@($results.ToArray());cleanup=@($cleanups.ToArray());captures=@($captures.ToArray());brightness=$means;shiftTabInputPreflight=@($backTab.sequence);pixelReviewRequired=$true} |
        ConvertTo-Json -Depth 14 | Set-Content (Join-Path $artifacts 'result.json') -Encoding utf8NoBOM
}
if ($failure) { throw $failure }
Write-Host ('PASS: requested '+$Filter+' conflict decisions; actual screenshot review is still required.')
