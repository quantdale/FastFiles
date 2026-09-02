<#
    Headless contract test for the promoted UIA core-smoke entry point.
    Live execution needs an interactive desktop and never runs in CI;
    this gate proves the promotion exists, is exported, and keeps its
    documented contract (params, result shape, no host exit).
#>
$ErrorActionPreference='Stop'
$script:failures=0
function Check{param([bool]$c,[string]$d) if($c){Write-Host "  ok: $d"} else{$script:failures++; Write-Host "FAIL: $d"} }
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
Import-Module (Join-Path $repo 'verify\capabilities\windows-ui-automation-validation\UiAutomationValidation.psm1') -Force -DisableNameChecking

$cmd=Get-Command Invoke-UiaCoreSmoke -ErrorAction SilentlyContinue
Check ($null -ne $cmd) 'Invoke-UiaCoreSmoke is exported by UiAutomationValidation'
if ($cmd) {
    Check ($cmd.CommandType -eq 'Function') 'smoke entry point is a function'
    $params=@($cmd.Parameters.Keys)
    Check ($params -contains 'Exe') 'smoke requires an Exe parameter'
    Check ($params -contains 'RepoRoot') 'smoke accepts RepoRoot'
    Check ($params -contains 'ScenarioId') 'smoke accepts ScenarioId'
    $body=$cmd.ScriptBlock.ToString()
    Check ($body -notmatch '\bexit\s+\d') 'smoke never exits the host process'
    foreach ($scenario in @('launch-main-window','home-activation','address-bar-navigate','dual-pane-toggle','search-open-filters','storage-analysis-open','settings-open','command-palette')) {
        Check ($body -match [regex]::Escape($scenario)) "smoke covers $scenario"
    }
}
if($script:failures -gt 0){ Write-Host "RESULT: $script:failures FAILED"; exit 1 }
Write-Host 'RESULT: all smoke promotion tests passed'; exit 0
