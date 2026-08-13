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
Check (@($manifest.artifacts | Where-Object { $_.buildIdentity -notmatch '^[0-9a-f]{40}$' }).Count -eq 0) 'all artifacts carry a canonical build identity'
Check (@($manifest.artifacts | Where-Object { $_.componentRole -notin @('ui', 'engine', 'service', 'installer') }).Count -eq 0) 'all artifacts carry a distinct component role'
Check ((@($manifest.artifacts | ForEach-Object { $_ | ConvertTo-Json -Depth 8 }) -join '|') -eq (@($manifestAgain.artifacts | ForEach-Object { $_ | ConvertTo-Json -Depth 8 }) -join '|')) 'manifest artifact records are deterministic'
$expectedProductVersion = Get-RepositoryProductVersion -RepoRoot $RepoRoot
$expectedFileVersion = "$expectedProductVersion.0"
$versionCheck = Test-ReleaseVersionMetadata -ArtifactRoot $BuildRoot -RepoRoot $RepoRoot
Check ($versionCheck.valid) 'all release executables contain canonical VERSIONINFO metadata'
Check (@($manifest.artifacts | Where-Object { $_.productVersion -cne $expectedProductVersion -or $_.fileVersion -cne $expectedFileVersion -or $_.productName -cne 'FastFiles' -or $_.companyName -cne 'FastFiles' -or $_.originalFilename -ine $_.filename -or $_.internalName -ine $_.filename -or $_.architecture -cne 'x64' }).Count -eq 0) 'artifact manifest records canonical version and identity fields'
Check (-not (Test-StrictProductVersion -Value '01.2.3')) 'leading-zero semantic-version components are rejected'
Check (Test-StrictProductVersion -Value '0.2.3') 'zero is accepted as the canonical zero semantic-version component'

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
    $metadataCheck = Test-ExecutableVersionMetadata -Path $malformedPath -ExpectedProductVersion $expectedProductVersion -ExpectedFileVersion $expectedFileVersion -ExpectedOriginalFilename 'malformed.exe'
    Check (-not $metadataCheck.valid -and @($metadataCheck.issues).Count -gt 0) 'missing VERSIONINFO metadata is rejected by final-artifact validation'
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
    $packageCheck = Test-PackageManifest -Path $packageManifestPath -ArtifactManifest $manifest -PackageRoot $packageTemp -AllowUnsignedPreparation
    Check ($packageCheck.valid -and @($packageCheck.entries).Count -eq 4) 'package manifest round-trips the complete artifact inventory'
    Check ((Get-Content -LiteralPath $packageManifestPath -Raw) -match 'FASTFILES_PACKAGE_MANIFEST_V2') 'package manifest has an authenticated-schema versioned header'
    Check ($packageCheck.productVersion -ceq $expectedProductVersion) 'package manifest carries the canonical product version'
    Check ($packageCheck.buildIdentity -ceq ([string]$manifest.artifacts[0].buildIdentity).ToLowerInvariant()) 'package manifest carries the canonical build identity'
    $tamperedVersionManifest = Join-Path $packageTemp 'tampered-version.package.manifest'
    $tamperedVersionText = (Get-Content -LiteralPath $packageManifestPath -Raw) -replace "productVersion=$([regex]::Escape($expectedProductVersion))", 'productVersion=9.9.9'
    [IO.File]::WriteAllText($tamperedVersionManifest, $tamperedVersionText, [Text.UTF8Encoding]::new($false))
    $tamperedVersionCheck = Test-PackageManifest -Path $tamperedVersionManifest -ArtifactManifest $manifest -AllowUnsignedPreparation
    Check (-not $tamperedVersionCheck.valid -and $tamperedVersionCheck.reason -eq 'PACKAGE_MANIFEST_VERSION_MISMATCH') 'package manifest version substitution is rejected'
    $tamperedPackageManifest = Join-Path $packageTemp 'tampered.package.manifest'
    $tamperedText = Get-Content -LiteralPath $packageManifestPath -Raw
    $hashLine = ($tamperedText -split "`n" | Where-Object { $_ -like 'file=FastFiles.exe*' } | Select-Object -First 1)
    $replacementHashLine = $hashLine.Substring(0, $hashLine.LastIndexOf('|') + 1) + ('0' * 64)
    $tamperedText = $tamperedText.Replace($hashLine, $replacementHashLine)
    [IO.File]::WriteAllText($tamperedPackageManifest, $tamperedText, [Text.UTF8Encoding]::new($false))
    $tamperedCheck = Test-PackageManifest -Path $tamperedPackageManifest -ArtifactManifest $manifest -AllowUnsignedPreparation
    Check (-not $tamperedCheck.valid -and $tamperedCheck.reason -eq 'PACKAGE_MANIFEST_HASH_MISMATCH') 'package manifest hash substitution is rejected'
    $tamperedArtifactPath = Join-Path $packageTemp 'FastFiles.exe'
    $originalBytes = [IO.File]::ReadAllBytes($tamperedArtifactPath)
    try {
        [IO.File]::WriteAllBytes($tamperedArtifactPath, [byte[]]::new(1))
        $tamperedArtifactCheck = Test-PackageManifest -Path $packageManifestPath -ArtifactManifest $manifest -PackageRoot $packageTemp -AllowUnsignedPreparation
        Check (-not $tamperedArtifactCheck.valid -and $tamperedArtifactCheck.reason -eq 'PACKAGE_ARTIFACT_HASH_MISMATCH') 'package artifact substitution is rejected even when the manifest is unchanged'
    } finally {
        [IO.File]::WriteAllBytes($tamperedArtifactPath, $originalBytes)
    }
    $missingPackageManifest = Join-Path $packageTemp 'missing.package.manifest'
    $missingCheck = Test-PackageManifest -Path $missingPackageManifest -ArtifactManifest $manifest -AllowUnsignedPreparation
    Check (-not $missingCheck.valid -and $missingCheck.reason -eq 'PACKAGE_MANIFEST_MISSING') 'missing package manifest is rejected'

    $productionUnsignedCheck = Test-PackageManifest -Path $packageManifestPath -ArtifactManifest $manifest
    Check (-not $productionUnsignedCheck.valid -and $productionUnsignedCheck.reason -eq 'PACKAGE_MANIFEST_SIGNATURE_MISSING') 'unsigned package manifest cannot authorize production release'
    $badSignaturePath = Join-Path $packageTemp 'FastFiles.package.manifest.p7s'
    [IO.File]::WriteAllBytes($badSignaturePath, [byte[]](0x01, 0x02, 0x03))
    $badSignatureCheck = Test-PackageManifestSignature -ManifestPath $packageManifestPath -SignaturePath $badSignaturePath -AllowedSignerThumbprints @('A' * 40)
    Check (-not $badSignatureCheck.valid -and $badSignatureCheck.reason -eq 'PACKAGE_MANIFEST_SIGNATURE_INVALID') 'malformed or wrong-signer package signature fails closed'

    $antiMixCases = @(
        [pscustomobject]@{ name = 'same-signer-different-version'; property = 'productVersion'; value = '9.9.9' },
        [pscustomobject]@{ name = 'old-engine-build'; property = 'buildIdentity'; value = '1111111111111111111111111111111111111111' },
        [pscustomobject]@{ name = 'wrong-component-role'; property = 'componentRole'; value = 'service' },
        [pscustomobject]@{ name = 'wrong-original-name'; property = 'originalFilename'; value = 'FastFiles.exe.renamed' },
        [pscustomobject]@{ name = 'wrong-internal-name'; property = 'internalName'; value = 'FastFiles.exe.renamed' },
        [pscustomobject]@{ name = 'wrong-architecture'; property = 'architecture'; value = 'arm64' }
    )
    foreach ($case in $antiMixCases) {
        $mutated = $manifest | ConvertTo-Json -Depth 20 | ConvertFrom-Json
        $target = @($mutated.artifacts | Where-Object filename -eq 'FastFilesEngine.exe')[0]
        $target.($case.property) = $case.value
        $casePath = Join-Path $packageTemp ($case.name + '.manifest')
        $writeFailed = $false
        try { Write-PackageManifest -Manifest $mutated -Path $casePath } catch { $writeFailed = $true }
        $caseCheck = if ($writeFailed) { $null } else { Test-PackageManifest -Path $casePath -ArtifactManifest $manifest -AllowUnsignedPreparation }
        Check ($writeFailed -or ($caseCheck -and -not $caseCheck.valid)) "anti-mix case rejects $($case.name)"
    }

    $duplicateText = (Get-Content -LiteralPath $packageManifestPath -Raw) + ((Get-Content -LiteralPath $packageManifestPath -Raw) -split "`n" | Where-Object { $_ -like 'file=FastFiles.exe*' } | Select-Object -First 1) + "`n"
    $duplicatePath = Join-Path $packageTemp 'duplicate.manifest'
    [IO.File]::WriteAllText($duplicatePath, $duplicateText, [Text.UTF8Encoding]::new($false))
    $duplicateCheck = Test-PackageManifest -Path $duplicatePath -ArtifactManifest $manifest -AllowUnsignedPreparation
    Check (-not $duplicateCheck.valid -and $duplicateCheck.reason -eq 'PACKAGE_MANIFEST_DUPLICATE_FILE') 'duplicate package member is rejected'
} finally {
    $packageTempFull = [IO.Path]::GetFullPath($packageTemp).TrimEnd('\')
    $packageTempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
    if ($packageTempFull.StartsWith($packageTempRoot + '\', [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $packageTempFull)) {
        Remove-Item -LiteralPath $packageTempFull -Recurse -Force
    }
}

$kitTemp = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-generated-kit-test-' + [guid]::NewGuid().ToString('N'))
try {
    $kitScript = Join-Path $RepoRoot 'verify\release\New-ReleaseKit.ps1'
    $kitOutput = & (Get-Command pwsh).Source -NoProfile -File $kitScript -BuildRoot $BuildRoot -OutputDirectory $kitTemp -OutputFormat json 2>&1
    $kitExit = $LASTEXITCODE
    Check ($kitExit -eq 0) 'generated release kit is produced from the release artifact root'
    if ($kitExit -eq 0) {
        $generatedPackageRoot = Resolve-ReleaseKitPackageRoot -Root $kitTemp
        $expectedPackageMembers = @('FastFiles.exe', 'FastFilesEngine.exe', 'FastFilesIndexSvc.exe', 'FastFilesSetup.exe', 'FastFiles.package.manifest')
        $actualPackageMembers = @(Get-ChildItem -LiteralPath $generatedPackageRoot -Force | ForEach-Object Name | Sort-Object)
        Check (($actualPackageMembers -join '|') -eq (($expectedPackageMembers | Sort-Object) -join '|')) 'generated kit package subtree has the exact installable member inventory'
        Check (-not (Test-Path -LiteralPath (Join-Path $kitTemp 'FastFiles.exe') -PathType Leaf)) 'generated kit keeps product payload out of the tooling root'
        $generatedKitManifest = Get-Content -LiteralPath (Join-Path $kitTemp 'kit-manifest.json') -Raw | ConvertFrom-Json
        Check ([string]$generatedKitManifest.packageRoot -ceq 'package') 'kit manifest declares the authoritative package root'
        Check (@($generatedKitManifest.package | Where-Object { $_ -notlike 'package/*' }).Count -eq 0) 'kit manifest package paths are rooted under package/'
        $generatedPackageCheck = Test-PackageManifest -Path (Join-Path $generatedPackageRoot 'FastFiles.package.manifest') -ArtifactManifest $manifest -PackageRoot $generatedPackageRoot -AllowUnsignedPreparation
        Check ($generatedPackageCheck.valid) 'generated kit package passes the exact PowerShell inventory validator'
        $unexpectedPackageMember = Join-Path $generatedPackageRoot 'unexpected.txt'
        [IO.File]::WriteAllText($unexpectedPackageMember, 'unexpected', [Text.UTF8Encoding]::new($false))
        $unexpectedPackageCheck = Test-PackageManifest -Path (Join-Path $generatedPackageRoot 'FastFiles.package.manifest') -ArtifactManifest $manifest -PackageRoot $generatedPackageRoot -AllowUnsignedPreparation
        Check (-not $unexpectedPackageCheck.valid -and $unexpectedPackageCheck.reason -eq 'PACKAGE_UNEXPECTED_MEMBER') 'generated package rejects a non-payload member'
        Remove-Item -LiteralPath $unexpectedPackageMember -Force
        $packageRootItem = Get-Item -LiteralPath $generatedPackageRoot
        Check (($packageRootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) 'generated package root is not a reparse point'
        $malformedKitRoot = Join-Path $kitTemp 'malformed-kit'
        New-Item -ItemType Directory -Path $malformedKitRoot -Force | Out-Null
        [IO.File]::WriteAllText((Join-Path $malformedKitRoot 'kit-manifest.json'), '{"schemaVersion":1,"kind":"fastfiles-clean-host-kit"}', [Text.UTF8Encoding]::new($false))
        $malformedKitRejected = $false
        try { Resolve-ReleaseKitPackageRoot -Root $malformedKitRoot | Out-Null } catch { $malformedKitRejected = $true }
        Check $malformedKitRejected 'malformed kit package-root metadata fails closed'
    }
} catch {
    Check $false "generated kit layout validation completed without exception ($($_.Exception.Message))"
} finally {
    $kitTempFull = [IO.Path]::GetFullPath($kitTemp).TrimEnd('\')
    $kitTempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
    if ($kitTempFull.StartsWith($kitTempRoot + '\', [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $kitTempFull)) {
        Remove-Item -LiteralPath $kitTempFull -Recurse -Force
    }
}

$presets = Get-Content -LiteralPath (Join-Path $RepoRoot 'CMakePresets.json') -Raw | ConvertFrom-Json
$releaseTestPreset = @($presets.configurePresets | Where-Object name -eq 'release-test') | Select-Object -First 1
$releasePreset = @($presets.configurePresets | Where-Object name -eq 'release') | Select-Object -First 1
Check ($releaseTestPreset -and $releaseTestPreset.cacheVariables.FASTFILES_RELEASE_TEST_BUILD -eq 'ON' -and $releaseTestPreset.cacheVariables.CMAKE_BUILD_TYPE -eq 'RelWithDebInfo') 'optimized test-release preset is explicit and non-production labeled'
Check ($releasePreset -and $null -eq $releasePreset.cacheVariables.FASTFILES_RELEASE_TEST_BUILD) 'production release preset does not opt out of pin enforcement'

Write-Host "Failures: $failures"
exit $(if ($failures -eq 0) { 0 } else { 1 })
