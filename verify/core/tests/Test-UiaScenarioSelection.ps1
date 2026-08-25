<#
    runtime-certification-and-verifier-convergence 5.2/5.3:
    pure selection-contract regression checks. This does not launch FastFiles
    or consume ignored run artifacts.
#>

$ErrorActionPreference = 'Stop'
$failures = 0
function Check {
    param([bool] $Condition, [string] $Description)
    if ($Condition) { Write-Host "PASS: $Description" } else { Write-Host "FAIL: $Description"; $script:failures++ }
}

$modulePath = Join-Path (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')) 'verify\capabilities\windows-ui-automation-validation\UiAutomationValidation.psm1'
Import-Module $modulePath -Force

$single = Resolve-UiaScenarioSelection -Options @{ UiaScenario = 'home-surface-structure' }
Check (@($single.Ids).Count -eq 1 -and $single.Ids[0] -eq 'home-surface-structure') 'single shared selection contains only the requested scenario'

$seedOptions = @{ UiaScenario = 'core'; UiaSeed = '17' }
$seedOne = Resolve-UiaScenarioSelection -Options $seedOptions
$seedTwo = Resolve-UiaScenarioSelection -Options $seedOptions
Check ((@($seedOne.Order) -join '|') -eq (@($seedTwo.Order) -join '|') -and $seedOne.Seed -eq 17) 'identical UIA seed produces identical resolved order'

$executionOne = @(Resolve-UiaExecutionOrder -SelectedIds $seedOne.Ids)
$executionTwo = @(Resolve-UiaExecutionOrder -SelectedIds $seedTwo.Ids)
Check ((@($executionOne) -join '|') -eq (@($executionTwo) -join '|') -and $executionOne[-1] -eq 'collect-evidence') 'execution order is deterministic and ends with evidence collection'

$mixedIds = @('mouse-tab-operation', 'selection-model-interactions', 'home-surface-structure')
$mixedExecution = @(Resolve-UiaExecutionOrder -SelectedIds $mixedIds)
Check ((@($mixedExecution) -join '|') -eq 'mouse-tab-operation|home-surface-structure|selection-model-interactions|collect-evidence') 'mixed execution records fixed shared order before isolated lifecycle order'

$all = Resolve-UiaScenarioSelection -Options @{ UiaScenario = 'all'; UiaSeed = '17' }
$isolated = Resolve-UiaScenarioSelection -Options @{ UiaScenario = 'isolated' }
Check (@($all.Ids).Count -gt @($seedOne.Ids).Count -and @($isolated.Ids).Count -gt 0) 'all and isolated selections resolve non-empty distinct scopes'

$unknownRejected = $false
try { Resolve-UiaScenarioSelection -Options @{ UiaScenario = 'not-a-scenario' } | Out-Null } catch { $unknownRejected = $true }
Check $unknownRejected 'unknown UIA scenario is rejected before lifecycle launch'

if ($failures -gt 0) { exit 1 }
Write-Host 'All UIA scenario-selection checks passed.'
exit 0
