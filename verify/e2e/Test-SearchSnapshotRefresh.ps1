<# Search in-panel snapshot refresh probe (follow-on to search-reopen-scope).
   With the Search panel OPEN and a query active, matching files are created
   and deleted directly on disk (no app input). The visible results must
   converge without retyping. Owned fixtures, public UIA controls only. #>
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
$gate=Invoke-ProvenanceGate -RepoRoot $repo -Binaries $binaries -Mode exact-tree -Stage search-refresh-preflight -PassThru
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
}
function OpenSearch {
    $opened=Open-UiaSearchPanel -Driver $ctx.Driver -Main (Main) -ProcessId ([int]$ctx.Launched.Id)
    Check $opened.Pass ('Open actual Search and Filters: '+$opened.Detail)
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
    $rows=[Collections.Generic.List[string]]::new()
    foreach($item in @(Get-UiaChildren -Driver $ctx.Driver -Element $list)) {
        $identity=Get-UiaElementIdentity -Driver $ctx.Driver -Element $item
        if($identity.ControlType -ne 'ListItem') {continue}
        [void]$rows.Add([string]$identity.Name)
    }
    $sorted=@($rows.ToArray() | Sort-Object)
    return [pscustomobject]@{query=[string]$surface.queryText;scope=[string]$surface.scopeName;rows=$sorted}
}
function WaitRows([string]$Query,[string[]]$ExpectedNames,[int]$TimeoutMs,[string]$Description) {
    $expected=@($ExpectedNames | Sort-Object)
    $seen=[pscustomobject]@{state=$null}
    $wait=Wait-E2ECondition -TimeoutMs $TimeoutMs -PollIntervalMs 250 -Description $Description -Condition {
        $state=ReadSearch
        $seen.state=$state
        if($state.query -ne $Query -or $state.scope -ne 'Current Folder') {return $false}
        if(@($state.rows).Count -ne $expected.Count) {return $false}
        for($i=0;$i -lt $expected.Count;$i++) {if($state.rows[$i] -ne $expected[$i]) {return $false}}
        return $true
    }
    [void]$observations.Add(@{phase=$script:phase;description=$Description;expected=$expected;search=$seen.state;elapsedMs=$wait.ElapsedMs;pass=$wait.Pass;lastError=$wait.LastError})
    return $wait
}
function Capture([string]$Name,[string]$IntentionalDuplicateReason='',[string]$ExpectedDuplicateOf='') {
    $shot=Capture-VisualWindow -Run $captureRun -Element (Main) -ScenarioId $Name -AssertionPass $true `
        -IntentionalDuplicateReason $IntentionalDuplicateReason -ExpectedDuplicateOf $ExpectedDuplicateOf
    Check ($shot.Bounds.Dpi -gt 0) ('Measured DPI for '+$Name)
    [void]$captures.Add($shot)
}

try {
    $ctx=New-UiaScenarioContext -Exe $exe -RepoRoot $repo -ScenarioId 'search-snapshot-refresh' `
        -InvocationId ([guid]::NewGuid().ToString('N')) -LogFn {param($message) Write-Host $message}
    Check ($ctx -and $ctx.Main -and $ctx.Ready) 'Actual application and engine are ready'
    $fixture=New-E2EFixture -SessionFixtureRoot $ctx.FixtureRoot
    [void]$ctx.Fixtures.Add($fixture.Root)
    $folder=Assert-E2EPathWithinRoot -Root $fixture.Root -Path (Join-Path $fixture.Root 'RefreshFolder')
    $null=[IO.Directory]::CreateDirectory($folder)
    $query='refreshprobe'+([guid]::NewGuid().ToString('N').Substring(0,12))
    $first=$query+'-first.txt'
    $second=$query+'-second.txt'
    $firstPath=Join-Path $folder $first
    $secondPath=Join-Path $folder $second
    [IO.File]::WriteAllText($firstPath,'owned first refresh marker')

    Phase 'baseline-single-result'
    Navigate $folder $first
    OpenSearch
    Check (Set-UiaSearchFixtureScope -Driver $ctx.Driver -Main (Main) -ProcessId ([int]$ctx.Launched.Id)) 'Select real Current Folder scope'
    SetQuery $query
    $baseline=WaitRows $query @($first) 15000 'baseline result converges to the single existing marker'
    Check $baseline.Pass ('Baseline single result without retyping: '+$baseline.LastError)
    Capture 'baseline-single'

    Phase 'hidden-create-refresh'
    $createdUtc=(Get-Date).ToUniversalTime().ToString('o')
    [IO.File]::WriteAllText($secondPath,'owned second refresh marker')
    $refresh=WaitRows $query @($first,$second) 20000 'hidden create appears in the open panel without retyping'
    [void]$observations.Add(@{phase=$script:phase;createdUtc=$createdUtc;refreshed=($refresh.Pass);elapsedMs=$refresh.ElapsedMs})
    Check $refresh.Pass ('Open panel refreshed to both markers after hidden create (no app input): '+$refresh.LastError)
    Check ((& $e2e {param($context) (Get-UiaSearchQuerySurface -Driver $context.Driver -Main (Get-E2EMain -Context $context) -ProcessId ([int]$context.Launched.Id)).queryText} $ctx) -eq $query) 'Query text is unchanged through the refresh (no retype)'
    Capture 'hidden-create-two-rows'

    Phase 'hidden-delete-refresh'
    $deletedUtc=(Get-Date).ToUniversalTime().ToString('o')
    Remove-Item -LiteralPath $firstPath -Force
    $removal=WaitRows $query @($second) 20000 'hidden delete disappears from the open panel without retyping'
    [void]$observations.Add(@{phase=$script:phase;deletedUtc=$deletedUtc;refreshed=($removal.Pass);elapsedMs=$removal.ElapsedMs})
    Check $removal.Pass ('Open panel refreshed to the surviving marker after hidden delete (no app input): '+$removal.LastError)
    Capture 'hidden-delete-one-row'
    Phase 'complete'
} catch {
    $failure=$_.Exception.Message
    Write-Host ('FAIL ['+$script:phase+']: '+$failure)
    if($ctx -and $ctx.Main) {
        try {
            Write-E2EFailureEvidence -Context $ctx -ArtifactDir (Join-Path $artifacts 'failure') `
                -ScenarioId 'search-snapshot-refresh' -Stage $script:phase -Expected $failure -Observed '' | Out-Null
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
      limits=@('Degraded-mode browsed-folder coverage only; indexed/persistent-coverage refresh is not exercised.',
               'Refresh latency figures are per-run observations on this host, not a product SLA.',
               'Subfolder (descendant-watch) propagation is not exercised; Current Folder scope only.',
               'Screenshots require visual review and measured DPI; no physical display matrix claim.')} |
        ConvertTo-Json -Depth 20 | Set-Content (Join-Path $artifacts 'result.json') -Encoding utf8NoBOM
}
if($failure -or -not $closed -or -not $cleanup -or -not $cleanup.Pass) {exit 1}
