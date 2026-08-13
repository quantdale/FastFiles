[CmdletBinding()]
param(
    [string] $RepoRoot = (Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent),
    [string] $BuildRoot
)

$ErrorActionPreference = 'Stop'
if (-not $BuildRoot) { $BuildRoot = Join-Path $RepoRoot 'build\release' }
Import-Module (Join-Path (Split-Path $PSScriptRoot -Parent) 'ReleaseCommon.psm1') -Force -DisableNameChecking

$failures = 0
function Check {
    param([bool] $Condition, [string] $Description)
    if ($Condition) { Write-Host "PASS: $Description" } else { Write-Host "FAIL: $Description"; $script:failures++ }
}

$manifest = Get-ReleaseArtifactManifest -ArtifactRoot $BuildRoot -RepoRoot $RepoRoot
$manifestAgain = Get-ReleaseArtifactManifest -ArtifactRoot $BuildRoot -RepoRoot $RepoRoot
Check (@($manifest.artifacts).Count -eq 4) 'manifest enumerates exactly four release artifacts'
Check (@($manifest.artifacts | Where-Object architecture -ne 'x64').Count -eq 0) 'all release artifacts are x64'
Check ((@($manifest.artifacts | ForEach-Object sha256 | Where-Object { $_ -notmatch '^[0-9A-F]{64}$' }).Count) -eq 0) 'all artifact hashes are uppercase SHA-256'
Check (($manifest.commit -match '^[0-9a-f]{40}$')) 'manifest records the current Git commit'
Check ((@($manifest.artifacts | ForEach-Object { $_ | ConvertTo-Json -Depth 8 }) -join '|') -eq (@($manifestAgain.artifacts | ForEach-Object { $_ | ConvertTo-Json -Depth 8 }) -join '|')) 'manifest artifact records are deterministic'

Check ((Get-PinState -Name 'zero' -Value ('0' * 40)).state -eq 'PLACEHOLDER') 'all-zero pin is classified as PLACEHOLDER'
Check ((Get-PinState -Name 'bad' -Value '1234').state -eq 'MALFORMED') 'short pin is classified as MALFORMED'
Check ((Get-PinState -Name 'good' -Value ('A' * 40)).state -eq 'POPULATED') '40-hex pin is classified as POPULATED'
Check ((Test-ThumbprintFormat -Value 'aa-aa-aa-aa-aa-aa-aa-aa-aa-aa-aa-aa-aa-aa-aa-aa-aa-aa-aa-aa')) 'separator normalization accepts a 20-byte thumbprint'
Check (-not (Test-ThumbprintFormat -Value 'not-a-thumbprint')) 'invalid thumbprint is rejected'
Check (-not (Test-ProductionCertificate -Certificate ([pscustomobject]@{ Subject = 'CN=FastFiles Dev'; Issuer = 'CN=FastFiles Dev' }))) 'self-signed development certificate is rejected for Release signing'
Check (Test-ProductionCertificate -Certificate ([pscustomobject]@{ Subject = 'CN=FastFiles Production'; Issuer = 'CN=Approved Code Signing CA' })) 'chained production certificate shape is accepted for provider validation'

$pins = Get-ConfiguredPinStates -RepoRoot $RepoRoot -BuildRoot $BuildRoot
$matrix = @(Get-ReleaseSignatureMatrix -Manifest $manifest -PinStates $pins)
Check (@($matrix | Where-Object result -eq 'UNSIGNED — PRODUCTION SIGNING REQUIRED').Count -eq 4) 'unsigned Release binaries produce explicit production-signing blockers'
Check ($pins.engine.state -in @('PLACEHOLDER', 'POPULATED', 'MALFORMED', 'MISSING')) 'engine pin validator emits a known state'
Check ($pins.service.state -in @('PLACEHOLDER', 'POPULATED', 'MALFORMED', 'MISSING')) 'service pin validator emits a known state'

$temp = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-release-tooling-test-' + [guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $temp -Force | Out-Null
    $missingRejected = $false
    try { Get-ReleaseArtifactPaths -ArtifactRoot $temp | Out-Null } catch { $missingRejected = $true }
    Check $missingRejected 'incomplete artifact root is rejected before hashing'
    $x86Path = Join-Path $temp 'fixture-x86.exe'
    $x86Bytes = [byte[]]::new(70)
    $x86Bytes[0] = 0x4d; $x86Bytes[1] = 0x5a; $x86Bytes[60] = 64
    $x86Bytes[64] = 0x50; $x86Bytes[65] = 0x45; $x86Bytes[68] = 0x4c; $x86Bytes[69] = 0x01
    [IO.File]::WriteAllBytes($x86Path, $x86Bytes)
    Check ((Get-PeArchitecture -Path $x86Path).architecture -eq 'x86') 'PE architecture parser rejects a non-x64 image for release signing'
    $malformedPath = Join-Path $temp 'malformed.exe'
    [IO.File]::WriteAllBytes($malformedPath, [byte[]]::new(3))
    $malformedRejected = $false
    try { Get-PeArchitecture -Path $malformedPath | Out-Null } catch { $malformedRejected = $true }
    Check $malformedRejected 'malformed PE input is rejected before signing or hashing'
} finally {
    $tempFull = [IO.Path]::GetFullPath($temp).TrimEnd('\')
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
    if ($tempFull.StartsWith($tempRoot + '\', [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $tempFull)) {
        Remove-Item -LiteralPath $tempFull -Recurse -Force
    }
}

$hostPreflight = Get-CleanHostPreflight
Check ($null -ne $hostPreflight.resources.service -and $null -ne $hostPreflight.resources.installDirectory) 'clean-host preflight reports exact FastFiles resources'
Check ($null -ne $hostPreflight.trustStore -and -not $hostPreflight.trustStore.checked) 'clean-host preflight does not mutate or claim trust-store provenance'

$packageTemp = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-package-manifest-test-' + [guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $packageTemp -Force | Out-Null
    foreach ($artifactPath in @(Get-ReleaseArtifactPaths -ArtifactRoot $BuildRoot)) {
        Copy-Item -LiteralPath $artifactPath.sourcePath -Destination (Join-Path $packageTemp $artifactPath.name) -Force
    }
    $packageManifestPath = Join-Path $packageTemp 'FastFiles.package.manifest'
    Write-PackageManifest -Manifest $manifest -Path $packageManifestPath
    $packageCheck = Test-PackageManifest -Path $packageManifestPath -ArtifactManifest $manifest -PackageRoot $packageTemp
    Check ($packageCheck.valid -and @($packageCheck.entries).Count -eq 4) 'package manifest round-trips the complete artifact inventory'
    Check ((Get-Content -LiteralPath $packageManifestPath -Raw) -match 'FASTFILES_PACKAGE_MANIFEST_V1') 'package manifest has an explicit versioned header'
    $tamperedPackageManifest = Join-Path $packageTemp 'tampered.package.manifest'
    $tamperedText = Get-Content -LiteralPath $packageManifestPath -Raw
    $tamperedText = $tamperedText -replace 'file=FastFiles\.exe\|[0-9]+\|[0-9A-F]{64}', 'file=FastFiles.exe|1|0000000000000000000000000000000000000000000000000000000000000000'
    [IO.File]::WriteAllText($tamperedPackageManifest, $tamperedText, [Text.UTF8Encoding]::new($false))
    $tamperedCheck = Test-PackageManifest -Path $tamperedPackageManifest -ArtifactManifest $manifest
    Check (-not $tamperedCheck.valid -and $tamperedCheck.reason -eq 'PACKAGE_MANIFEST_HASH_MISMATCH') 'package manifest hash substitution is rejected'
    $tamperedArtifactPath = Join-Path $packageTemp 'FastFiles.exe'
    $originalBytes = [IO.File]::ReadAllBytes($tamperedArtifactPath)
    try {
        [IO.File]::WriteAllBytes($tamperedArtifactPath, [byte[]]::new(1))
        $tamperedArtifactCheck = Test-PackageManifest -Path $packageManifestPath -ArtifactManifest $manifest -PackageRoot $packageTemp
        Check (-not $tamperedArtifactCheck.valid -and $tamperedArtifactCheck.reason -eq 'PACKAGE_ARTIFACT_HASH_MISMATCH') 'package artifact substitution is rejected even when the manifest is unchanged'
    } finally {
        [IO.File]::WriteAllBytes($tamperedArtifactPath, $originalBytes)
    }
    $missingPackageManifest = Join-Path $packageTemp 'missing.package.manifest'
    $missingCheck = Test-PackageManifest -Path $missingPackageManifest -ArtifactManifest $manifest
    Check (-not $missingCheck.valid -and $missingCheck.reason -eq 'PACKAGE_MANIFEST_MISSING') 'missing package manifest is rejected'
} finally {
    $packageTempFull = [IO.Path]::GetFullPath($packageTemp).TrimEnd('\')
    $packageTempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
    if ($packageTempFull.StartsWith($packageTempRoot + '\', [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $packageTempFull)) {
        Remove-Item -LiteralPath $packageTempFull -Recurse -Force
    }
}

$presets = Get-Content -LiteralPath (Join-Path $RepoRoot 'CMakePresets.json') -Raw | ConvertFrom-Json
$releaseTestPreset = @($presets.configurePresets | Where-Object name -eq 'release-test') | Select-Object -First 1
$releasePreset = @($presets.configurePresets | Where-Object name -eq 'release') | Select-Object -First 1
Check ($releaseTestPreset -and $releaseTestPreset.cacheVariables.FASTFILES_RELEASE_TEST_BUILD -eq 'ON' -and $releaseTestPreset.cacheVariables.CMAKE_BUILD_TYPE -eq 'RelWithDebInfo') 'optimized test-release preset is explicit and non-production labeled'
Check ($releasePreset -and $null -eq $releasePreset.cacheVariables.FASTFILES_RELEASE_TEST_BUILD) 'production release preset does not opt out of pin enforcement'

Write-Host "Failures: $failures"
exit $(if ($failures -eq 0) { 0 } else { 1 })
