$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

$failures = 0
function Check {
    param([bool] $Condition, [string] $Description)
    if ($Condition) { Write-Host "ok: $Description" } else { Write-Host "FAIL: $Description"; $script:failures++ }
}
function Get-RepositoryHead {
    try {
        $commit = git -C $repoRoot rev-parse HEAD 2>$null
        if ($LASTEXITCODE -eq 0 -and $commit) {
            $sha = ([string](@($commit) | Select-Object -First 1)).Trim().ToLowerInvariant()
            if ($sha -match '^[0-9a-f]{40}$') { return $sha }
        }
    } catch { }
    return $null
}
function Resolve-BuildIdentityBinaries {
    $candidates = @(
        'build\debug\src\ui\FastFiles.exe',
        'build\debug\src\engine\FastFilesEngine.exe',
        'build\debug\src\indexsvc\FastFilesIndexSvc.exe',
        'build\debug\src\installer\FastFilesSetup.exe',
        'build\debug\src\fftest\fftest.exe'
    )
    $found = @()
    foreach ($relative in $candidates) {
        $path = Join-Path $repoRoot $relative
        if (Test-Path -LiteralPath $path -PathType Leaf) { $found += $path }
    }
    return $found
}

$overrideId = $env:FASTFILES_BUILD_ID
if (-not [string]::IsNullOrWhiteSpace($overrideId)) {
    Write-Host 'skip: FASTFILES_BUILD_ID override is set; binding compares against the override instead of git'
    # Still validate format when override is present and binaries exist.
    $overrideId = $overrideId.Trim().ToLowerInvariant()
}

$expected = Get-RepositoryHead
if ([string]::IsNullOrWhiteSpace($overrideId) -and -not $expected) {
    Write-Host 'skip: repository HEAD is unavailable (not a git worktree or git failed)'
    exit 77
}
if ([string]::IsNullOrWhiteSpace($overrideId)) { $overrideId = $expected }

$binaries = Resolve-BuildIdentityBinaries
if ($binaries.Count -lt 5) {
    Write-Host "skip: only $($binaries.Count)/5 debug identity binaries are present; build the debug preset first"
    exit 77
}

$identities = [ordered]@{}
foreach ($path in $binaries) {
    $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($path)
    $identity = if ([string]::IsNullOrWhiteSpace($info.PrivateBuild)) { '' } else { $info.PrivateBuild.Trim().ToLowerInvariant() }
    $identities[[IO.Path]::GetFileName($path)] = $identity
    Check ($identity -match '^[0-9a-f]{40}$') "$([IO.Path]::GetFileName($path)) embeds a 40-hex PrivateBuild"
    Check ($identity -eq $overrideId) "$([IO.Path]::GetFileName($path)) PrivateBuild matches expected source identity ($overrideId)"
    $special = if ($null -eq $info.SpecialBuild) { '' } else { $info.SpecialBuild }
    Check ($special -match 'tree=(clean|dirty|unbound)') "$([IO.Path]::GetFileName($path)) SpecialBuild records worktree state"
    Check ($special -match 'config=') "$([IO.Path]::GetFileName($path)) SpecialBuild records configuration"
    Check ($special -match 'arch=') "$([IO.Path]::GetFileName($path)) SpecialBuild records architecture"
}

$unique = @($identities.Values | Sort-Object -Unique)
Check ($unique.Count -eq 1) "all $($binaries.Count) binaries share one identity (mixed set count=$($unique.Count))"

if ($failures -gt 0) {
    $observed = ($identities.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join '; '
    Write-Host "PROVENANCE_MISMATCH"
    Write-Host "EXPECTED_SHA=$overrideId"
    Write-Host "OBSERVED=$observed"
    Write-Host "FAILED=$($failures)"
    exit 1
}
Write-Host "All binary identity bindings match source identity $overrideId."
exit 0
