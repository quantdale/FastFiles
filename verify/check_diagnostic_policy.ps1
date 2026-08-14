<#[
.SYNOPSIS
    Verifies that the unsigned-authentication diagnostic escape hatch is
    explicitly limited to disposable Debug builds.

.DESCRIPTION
    The CMake guard is the enforcement point. This source/configuration check
    makes the shipped-build policy reviewable in CI and catches an accidentally
    enabled option in an existing Release or release-test cache.
#>

[CmdletBinding()]
param(
    [string] $RepoRoot = (Split-Path $PSScriptRoot -Parent)
)

$ErrorActionPreference = 'Stop'
$failures = 0

function Check {
    param([bool] $Condition, [string] $Description)
    if ($Condition) {
        Write-Host "PASS: $Description"
    } else {
        Write-Host "FAIL: $Description"
        $script:failures++
    }
}

$setupPath = Join-Path $RepoRoot 'src\setup\CMakeLists.txt'
$setupText = Get-Content -LiteralPath $setupPath -Raw
$uiTestPath = Join-Path $RepoRoot 'tests\ui\CMakeLists.txt'
$uiTestText = Get-Content -LiteralPath $uiTestPath -Raw
$rootCMakeText = Get-Content -LiteralPath (Join-Path $RepoRoot 'CMakeLists.txt') -Raw
Check ($rootCMakeText -match 'FASTFILES_DIAGNOSTIC_ALLOW_UNSIGNED may only be enabled') `
    'top-level CMake rejects the diagnostic escape hatch before release-pin evaluation'
Check ($setupText -match 'FASTFILES_DIAGNOSTIC_ALLOW_UNSIGNED may only be enabled') `
    'CMake has an explicit shipped-build diagnostic escape-hatch rejection'
Check ($setupText -match 'CMAKE_BUILD_TYPE STREQUAL "Debug"' -and $setupText -match 'FASTFILES_RELEASE_TEST_BUILD') `
    'the escape hatch is limited to Debug and excluded from release-test builds'
Check ($setupText -match 'target_compile_definitions\(ffsetup PRIVATE FASTFILES_DIAGNOSTIC_ALLOW_UNSIGNED\)') `
    'the production setup seam is the only shipped target carrying the diagnostic definition'
Check ($uiTestText -match 'if\(FASTFILES_DIAGNOSTIC_ALLOW_UNSIGNED\)' -and
       $uiTestText -match 'target_compile_definitions\(ffengine_discovery_tests PRIVATE FASTFILES_DIAGNOSTIC_ALLOW_UNSIGNED\)') `
    'the only additional definition is explicitly scoped to the disposable Debug discovery test'
Check ($rootCMakeText -notmatch 'target_compile_definitions\([^)]*FASTFILES_DIAGNOSTIC_ALLOW_UNSIGNED' -and
       @($uiTestText -split "`r?`n" | Where-Object { $_ -match 'target_compile_definitions\(' -and $_ -notmatch 'ffengine_discovery_tests' -and $_ -match 'FASTFILES_DIAGNOSTIC_ALLOW_UNSIGNED' }).Count -eq 0) `
    'no unrelated application target receives the diagnostic definition'

$presets = Get-Content -LiteralPath (Join-Path $RepoRoot 'CMakePresets.json') -Raw | ConvertFrom-Json
foreach ($presetName in @('release', 'release-test')) {
    $preset = @($presets.configurePresets | Where-Object name -eq $presetName) | Select-Object -First 1
    $diagnosticProperty = if ($preset) {
        @($preset.cacheVariables.PSObject.Properties | Where-Object Name -eq 'FASTFILES_DIAGNOSTIC_ALLOW_UNSIGNED')
    } else {
        @([pscustomobject]@{ Name = 'missing-preset' })
    }
    Check ($preset -and $diagnosticProperty.Count -eq 0) "$presetName preset does not enable the diagnostic escape hatch"
}

$workflowPath = Join-Path $RepoRoot '.github\workflows\ci.yml'
$workflowText = Get-Content -LiteralPath $workflowPath -Raw
Check ($workflowText -match 'verify/check_diagnostic_policy\.ps1') 'hosted CI invokes the diagnostic policy source gate'

foreach ($buildName in @('release', 'release-test')) {
    $cachePath = Join-Path $RepoRoot ("build\$buildName\CMakeCache.txt")
    if (Test-Path -LiteralPath $cachePath -PathType Leaf) {
        $cacheText = Get-Content -LiteralPath $cachePath -Raw
        Check ($cacheText -notmatch 'FASTFILES_DIAGNOSTIC_ALLOW_UNSIGNED:BOOL=ON') `
            "$buildName cache does not enable the diagnostic escape hatch"
    } else {
        Write-Host "INFO: $buildName cache is absent; CMake source guard remains the enforcement evidence."
    }
}

Write-Host "Failures: $failures"
exit $(if ($failures -eq 0) { 0 } else { 1 })
