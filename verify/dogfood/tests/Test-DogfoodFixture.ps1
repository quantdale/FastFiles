[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$modulePath = Join-Path $PSScriptRoot '..\DogfoodCommon.psm1'
$newFixture = Join-Path $PSScriptRoot '..\New-DogfoodFixture.ps1'
Import-Module $modulePath -Force

$failures = [Collections.Generic.List[string]]::new()
function Check-Dogfood {
    param([bool] $Condition, [string] $Description)
    if (-not $Condition) { $failures.Add($Description) }
}
function Expect-DogfoodThrow {
    param([scriptblock] $Action, [string] $Description)
    $threw = $false
    try { & $Action } catch { $threw = $true }
    Check-Dogfood -Condition $threw -Description $Description
}

$suffix = [Guid]::NewGuid().ToString('N')
$rootA = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-dogfood-test-a-' + $suffix)
$rootB = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-dogfood-test-b-' + $suffix)
$unmarked = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-dogfood-unmarked-' + $suffix)
try {
    & $newFixture -Root $rootA -Profile Small -OutputFormat json | Out-Null
    & $newFixture -Root $rootB -Profile Small -OutputFormat json | Out-Null
    $manifestA = @(Get-DogfoodManifestEntries -ManifestPath (Join-Path $rootA (Get-DogfoodManifestName)))
    $manifestB = @(Get-DogfoodManifestEntries -ManifestPath (Join-Path $rootB (Get-DogfoodManifestName)))
    $serializedA = ($manifestA | Sort-Object relativePath | ConvertTo-Json -Depth 6 -Compress)
    $serializedB = ($manifestB | Sort-Object relativePath | ConvertTo-Json -Depth 6 -Compress)
    if ($serializedA -ne $serializedB) {
        $firstDifference = $null
        for ($index = 0; $index -lt [Math]::Min($manifestA.Count, $manifestB.Count); $index++) {
            $left = $manifestA[$index] | ConvertTo-Json -Depth 6 -Compress
            $right = $manifestB[$index] | ConvertTo-Json -Depth 6 -Compress
            if ($left -ne $right) {
                $firstDifference = "first differing entry '$($manifestA[$index].relativePath)': $left <> $right"
                break
            }
        }
        $failures.Add('same profile produces the same relative manifest; ' + $firstDifference)
    }

    $actual = @(Get-DogfoodSnapshot -Root $rootA -HashFiles)
    $comparison = Compare-DogfoodEntries -Expected $manifestA -Actual $actual
    Check-Dogfood -Condition $comparison.passed -Description 'generated fixture matches its independent snapshot'
    Check-Dogfood -Condition ($manifestA.Count -ge 100) -Description 'small profile contains at least 100 manifest entries'

    Expect-DogfoodThrow -Action { Assert-DogfoodRootSafe -Root (Split-Path -Parent $PSScriptRoot | Split-Path -Parent) } -Description 'repository root is rejected as fixture root'
    [IO.Directory]::CreateDirectory($unmarked) | Out-Null
    Expect-DogfoodThrow -Action { Remove-DogfoodRoot -Root $unmarked } -Description 'unmarked root cannot be reset'
} finally {
    foreach ($path in @($rootA, $rootB)) {
        if (Test-Path -LiteralPath (Join-Path $path (Get-DogfoodMarkerName)) -PathType Leaf) {
            Remove-DogfoodRoot -Root $path
        }
    }
    if (Test-Path -LiteralPath $unmarked) { [IO.Directory]::Delete($unmarked, $true) }
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Error $_ }
    exit 1
}
Write-Output 'FastFiles dogfood fixture tests: PASS'
