[CmdletBinding()]
param(
    [string]$RepoRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path,
    [Parameter(Mandatory)][string]$ArtifactsDir
)

$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$repo=(Resolve-Path -LiteralPath $RepoRoot).Path
$artifacts=[IO.Path]::GetFullPath($ArtifactsDir)
if(Test-Path -LiteralPath $artifacts) {throw 'A fresh evidence directory is required; existing files are preserved.'}
$null=New-Item -ItemType Directory -Path $artifacts

Import-Module (Join-Path $repo 'verify/core/Provenance.psm1') -Force -DisableNameChecking
$exe=Join-Path $repo 'build/debug-diagnostic/src/ui/FastFiles.exe'
$binaries=@(Get-DefaultCertificationBinaries -RepoRoot $repo -BuildDirectory (Join-Path $repo 'build/debug-diagnostic'))
$gate=Invoke-ProvenanceGate -RepoRoot $repo -Binaries $binaries -Mode exact-tree -Stage search-reopen-preflight -PassThru
$gate.evidence | ConvertTo-Json -Depth 20 | Set-Content (Join-Path $artifacts 'provenance.json') -Encoding utf8NoBOM
if(-not $gate.ok) {throw $gate.message}

$e2e=Import-Module (Join-Path $repo 'verify/e2e/FastFilesE2E.psm1') -Force -DisableNameChecking -PassThru
Import-Module (Join-Path $repo 'verify/uia-driver/UiaDriver.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $repo 'verify/visual/VisualCapture.psm1') -Force -DisableNameChecking
$validation=Get-Module UiAutomationValidation
$captureRun=New-VisualCaptureRun -Root (Join-Path $artifacts 'images') -ApplicationSha (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant() -RepoRoot $repo
$observations=[Collections.Generic.List[object]]::new()
$captures=[Collections.Generic.List[object]]::new()
$ctx=$null;$cleanup=$null;$failure=$null;$closed=$false;$fixture=$null
$script:phase='startup'

function Check([bool]$Condition,[string]$Message) {
    if(-not $Condition) {throw $Message}
    Write-Host ('PASS: '+$Message)
}
function Phase([string]$Name) {
    $script:phase=$Name
    Write-Host ('PHASE: '+$Name)
    @{status='IN_PROGRESS';phase=$Name;observations=@($observations.ToArray())} |
        ConvertTo-Json -Depth 15 | Set-Content (Join-Path $artifacts 'progress.json') -Encoding utf8NoBOM
}
function Main {
    return & $e2e {param($context) Get-E2EMain -Context $context} $ctx
}
function Navigate([string]$Path,[string]$ExpectedItem) {
    $nav=Invoke-UiaAddressBarNavigate -Driver $ctx.Driver -Main (Main) -ProcessId ([int]$ctx.Launched.Id) `
        -Path $Path -ExpectedLeaf ([IO.Path]::GetFileName($Path))
    Check $nav.Pass ('Navigate actual address bar: '+$nav.Detail)
    $ready=& $e2e {param($context,$name) Wait-E2EItem -Context $context -Name $name -TimeoutMs 15000} $ctx $ExpectedItem
    Check $ready.Pass ('Expected item visible in actual terminal column: '+$ExpectedItem)
    $columns=@(& $validation {param($context)
        Get-UiaColumnListElements -Driver $context.Driver -Main (Get-UiaMainWindowElement -Driver $context.Driver -ProcessId ([int]$context.Launched.Id)) `
            -PaneId 'Pane.0' -ProcessId ([int]$context.Launched.Id)
    } $ctx)
    Check ($columns.Count -gt 0) 'Real terminal column is available'
    $pathName=[string](Get-UiaElementProperty -Driver $ctx.Driver -Element $columns[-1] -Name 'Name')
    Check ([string]::Equals($pathName,$Path,[StringComparison]::OrdinalIgnoreCase)) 'Terminal column identifies the exact requested folder'
    [void]$observations.Add(@{phase=$script:phase;folder=$pathName;visible=$ExpectedItem})
}
function OpenSearch {
    $opened=Open-UiaSearchPanel -Driver $ctx.Driver -Main (Main) -ProcessId ([int]$ctx.Launched.Id)
    Check $opened.Pass ('Open actual Search and Filters: '+$opened.Detail)
}
function CloseSearch {
    $main=Main
    $foreground=& $validation {param($context,$window) Ensure-UiaInputForeground -Driver $context.Driver -Main $window -Attempts 2} $ctx $main
    Check $foreground 'FastFiles owns foreground before Escape'
    $null=Send-UiaInput -Driver $ctx.Driver -Keys '{ESC}'
    $closedResults=Wait-UiaElementGone -Driver $ctx.Driver -AutomationId 'Search.Results' -ProcessId ([int]$ctx.Launched.Id) -TimeoutMs 6000 -PollIntervalMs 150
    Check $closedResults 'Escape closed actual Search.Results surface'
}
function SetQuery([string]$Query) {
    $set=Set-UiaSearchQuery -Driver $ctx.Driver -Main (Main) -ProcessId ([int]$ctx.Launched.Id) -Query $Query
    Check $set.Pass ('Type query into actual Search edit: '+$set.Detail)
}
function ReadSearch {
    $main=Main
    $surface=Get-UiaSearchQuerySurface -Driver $ctx.Driver -Main $main -ProcessId ([int]$ctx.Launched.Id)
    # A missing provider or failed enumeration is an error, not an empty list.
    $list=Find-UiaElement -Driver $ctx.Driver -AutomationId 'Search.Results' -ProcessId ([int]$ctx.Launched.Id) -FromElement $main -TimeoutMs 800
    if(-not $list) {throw 'Search.Results is unavailable'}
    $rows=[Collections.Generic.List[object]]::new()
    foreach($item in @(Get-UiaChildren -Driver $ctx.Driver -Element $list)) {
        $identity=Get-UiaElementIdentity -Driver $ctx.Driver -Element $item
        if($identity.ControlType -ne 'ListItem') {continue}
        $folder=[string](Get-UiaElementProperty -Driver $ctx.Driver -Element $item -Name 'HelpText')
        [void]$rows.Add(@{name=[string]$identity.Name;folder=$folder})
    }
    return [pscustomobject]@{query=[string]$surface.queryText;scope=[string]$surface.scopeName;rows=@($rows.ToArray())}
}
function WaitSearch([string]$Query,[string]$ExpectedName,[string]$ExpectedFolder) {
    $seen=[pscustomobject]@{state=$null}
    $wait=Wait-E2ECondition -TimeoutMs 15000 -PollIntervalMs 200 -Description "Search matches $ExpectedName" -Condition {
        $state=ReadSearch
        $seen.state=$state
        if($state.query -ne $Query -or $state.scope -ne 'Current Folder' -or @($state.rows).Count -ne 1) {return $false}
        $row=$state.rows[0]
        return $row.name -eq $ExpectedName -and [string]::Equals($row.folder.TrimEnd('\'),$ExpectedFolder.TrimEnd('\'),[StringComparison]::OrdinalIgnoreCase)
    }
    [void]$observations.Add(@{phase=$script:phase;search=$seen.state;elapsedMs=$wait.ElapsedMs;pass=$wait.Pass;lastError=$wait.LastError})
    return $wait
}
function Capture([string]$Name, [string]$IntentionalDuplicateReason = '', [string]$ExpectedDuplicateOf = '') {
    $shot=Capture-VisualWindow -Run $captureRun -Element (Main) -ScenarioId $Name -AssertionPass $true `
        -IntentionalDuplicateReason $IntentionalDuplicateReason -ExpectedDuplicateOf $ExpectedDuplicateOf
    Check ($shot.Bounds.Dpi -gt 0) ('Measured DPI for '+$Name)
    [void]$captures.Add($shot)
}

try {
    $ctx=New-UiaScenarioContext -Exe $exe -RepoRoot $repo -ScenarioId 'search-reopen-scope' `
        -InvocationId ([guid]::NewGuid().ToString('N')) -LogFn {param($message) Write-Host $message}
    Check ($ctx -and $ctx.Main -and $ctx.Ready) 'Actual application and engine are ready'
    $fixture=New-E2EFixture -SessionFixtureRoot $ctx.FixtureRoot
    [void]$ctx.Fixtures.Add($fixture.Root)
    $folderA=Assert-E2EPathWithinRoot -Root $fixture.Root -Path (Join-Path $fixture.Root 'ReopenFolderA')
    $folderB=Assert-E2EPathWithinRoot -Root $fixture.Root -Path (Join-Path $fixture.Root 'ReopenFolderB')
    $null=[IO.Directory]::CreateDirectory($folderA)
    $null=[IO.Directory]::CreateDirectory($folderB)
    $anchor='reopen-anchor-a.txt'
    $query='reopenprobe'+([guid]::NewGuid().ToString('N').Substring(0,12))
    $marker=$query+'.txt'
    [IO.File]::WriteAllText((Join-Path $folderA $anchor),'owned folder A search anchor')
    [IO.File]::WriteAllText((Join-Path $folderB $marker),'owned folder B unique search marker')

    Phase 'prebrowse-b-and-prove-searchability'
    Navigate $folderB $marker
    OpenSearch
    Check (Set-UiaSearchFixtureScope -Driver $ctx.Driver -Main (Main) -ProcessId ([int]$ctx.Launched.Id)) 'Select real Current Folder scope'
    SetQuery $query
    $proof=WaitSearch $query $marker $folderB
    Check $proof.Pass ('Folder B unique marker is actually searchable before the reopen test: '+$proof.LastError)
    Capture 'folder-b-search-proof'
    CloseSearch

    Phase 'folder-a-empty-query'
    Navigate $folderA $anchor
    OpenSearch
    $before=ReadSearch
    Check ($before.scope -eq 'Current Folder') 'Current Folder scope remains selected'
    # A positive A result makes the following positive-to-empty transition
    # observable; it also establishes that the query control dispatches.
    SetQuery ([IO.Path]::GetFileNameWithoutExtension($anchor))
    $anchorProof=WaitSearch ([IO.Path]::GetFileNameWithoutExtension($anchor)) $anchor $folderA
    Check $anchorProof.Pass ('Folder A anchor becomes an actual current-scope result: '+$anchorProof.LastError)
    SetQuery $query
    $seen=[pscustomobject]@{state=$null}
    $empty=Wait-E2ECondition -TimeoutMs 15000 -PollIntervalMs 200 -Description 'A shows no matching result rows' -Condition {
        $state=ReadSearch
        $seen.state=$state
        return $state.query -eq $query -and $state.scope -eq 'Current Folder' -and @($state.rows).Count -eq 0
    }
    [void]$observations.Add(@{phase=$script:phase;search=$seen.state;elapsedMs=$empty.ElapsedMs;pass=$empty.Pass;lastError=$empty.LastError})
    Check $empty.Pass ('Folder A has no matching rows for the B-only query: '+$empty.LastError)
    # The owner-painted status has no public semantic text. The capture and
    # empty rows are recorded as observed UI state, not proof of a terminal
    # no-match status or proof that a search worker finished.
    Capture 'folder-a-observed-empty'
    CloseSearch

    Phase 'folder-b-reopen-retained-query'
    Navigate $folderB $marker
    OpenSearch
    $reopened=ReadSearch
    [void]$observations.Add(@{phase='reopen-immediate';search=$reopened})
    Check ($reopened.query -eq $query) 'Reopening preserves the exact query without typing again'
    Check ($reopened.scope -eq 'Current Folder') 'Reopening preserves the exact Current Folder scope'
    $result=WaitSearch $query $marker $folderB
    Check $result.Pass ('Retained query resolves to exact Folder B result/location after reopen: '+$result.LastError)
    # A reopened retained query that resolves to the exact same visible state
    # as the earlier proof capture is expected to be pixel-identical; declare
    # that one intentional duplicate (the guard still rejects duplication with
    # any other frame).
    Capture 'folder-b-reopened-result' 'retained query restored the exact visible proof state' 'folder-b-search-proof'

    Phase 'reveal-exact-result'
    $item=@(Get-UiaSearchResultItems -Driver $ctx.Driver -Main (Main) -ProcessId ([int]$ctx.Launched.Id) -TimeoutMs 3000 |
        Where-Object {[string](Get-UiaElementProperty -Driver $ctx.Driver -Element $_ -Name 'Name') -eq $marker}) | Select-Object -First 1
    Check ($null -ne $item) 'Exact retained-query result remains available for activation'
    $null=Invoke-UiaPattern -Driver $ctx.Driver -Element $item -PatternName 'SelectionItem' -Method 'Select'
    $null=Invoke-UiaPattern -Driver $ctx.Driver -Element $item -PatternName 'Invoke' -Method 'Invoke'
    $gone=Wait-UiaElementGone -Driver $ctx.Driver -AutomationId 'Search.Results' -ProcessId ([int]$ctx.Launched.Id) -TimeoutMs 10000 -PollIntervalMs 200
    Check $gone 'Result activation closes Search'
    $expectedResult=Join-Path $folderB $marker
    $reveal=Wait-E2ECondition -TimeoutMs 15000 -PollIntervalMs 200 -Description 'exact selected Search result is revealed' -Condition {
        $row=Find-UiaTerminalColumnItem -Driver $ctx.Driver -Main (Main) -ProcessId ([int]$ctx.Launched.Id) `
            -Name $marker -PaneId 'Pane.0' -TimeoutMs 700
        if(-not $row) {return $false}
        $actual=[string](Get-UiaElementProperty -Driver $ctx.Driver -Element $row -Name 'HelpText')
        $selected=[bool](Invoke-UiaPattern -Driver $ctx.Driver -Element $row -PatternName 'SelectionItem' -Method 'GetSelection')
        return $selected -and [string]::Equals($actual,$expectedResult,[StringComparison]::OrdinalIgnoreCase)
    }
    Check $reveal.Pass ('Exact B marker is selected in its real folder: '+$reveal.LastError)
    Capture 'folder-b-selected-after-reveal'
    Phase 'complete'
} catch {
    $failure=$_.Exception.Message
    Write-Host ('FAIL ['+$script:phase+']: '+$failure)
    if($ctx -and $ctx.Main) {
        try {
            Write-E2EFailureEvidence -Context $ctx -ArtifactDir (Join-Path $artifacts 'failure') `
                -ScenarioId 'search-reopen-scope' -Stage $script:phase -Expected $failure -Observed '' | Out-Null
        } catch {Write-Host ('Failure evidence unavailable: '+$_.Exception.Message)}
    }
} finally {
    if($ctx) {
        try {$closed=[bool](& $e2e {param($context) Close-E2EContextWindow -Context $context} $ctx)} catch {Write-Host ('Normal close failed: '+$_.Exception.Message)}
        if($closed) {
            $cleanup=Remove-UiaScenarioContext -Context $ctx -ArtifactsDir (Join-Path $artifacts 'cleanup') -LogFn {param($message) Write-Host $message}
        } else {
            Write-Host 'Normal close did not complete; process/session left intact without a manual termination attempt.'
        }
    }
    @{status=$(if($failure -or -not $closed -or -not $cleanup -or -not $cleanup.Pass){'FAIL'}else{'PASS'});phase=$script:phase;failure=$failure;
      observations=@($observations.ToArray());captures=@($captures.ToArray());closed=$closed;cleanup=$cleanup;
      limits=@('Folder A empty rows are observable, but owner-painted status is not independently exposed as a terminal state.',
               'This test does not prove that cancellation occurred during an in-flight query.',
               'Screenshots require visual review and measured DPI; no physical display matrix claim.')} |
        ConvertTo-Json -Depth 20 | Set-Content (Join-Path $artifacts 'result.json') -Encoding utf8NoBOM
}
if($failure -or -not $closed -or -not $cleanup -or -not $cleanup.Pass) {exit 1}
