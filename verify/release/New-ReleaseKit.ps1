[CmdletBinding()]
param(
    [string] $BuildRoot,
    [string] $OutputDirectory = (Join-Path ([IO.Path]::GetTempPath()) 'FastFiles-release-kit'),
    [switch] $Force,
    [ValidateSet('human', 'json')]
    [string] $OutputFormat = 'human'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not $BuildRoot) { $BuildRoot = Join-Path $repoRoot 'build\release' }
Import-Module (Join-Path $PSScriptRoot 'ReleaseCommon.psm1') -Force -DisableNameChecking

try {
    $output = [IO.Path]::GetFullPath($OutputDirectory)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
    if ($output -eq [IO.Path]::GetPathRoot($output).TrimEnd('\')) { throw 'KIT_OUTPUT_INVALID: refusing to use a filesystem root as kit output.' }
    if ((Test-Path -LiteralPath $output) -and -not $Force) {
        $foreign = @(Get-ChildItem -LiteralPath $output -Force -ErrorAction Stop | Where-Object { $_.Name -notin @('FastFiles.exe', 'FastFilesEngine.exe', 'FastFilesIndexSvc.exe', 'FastFilesSetup.exe', 'FastFiles.package.manifest', 'ReleaseCommon.psm1', 'derive-pin.ps1', 'sign-release.ps1', 'validate-clean-host.ps1', 'cleanup-clean-host.ps1', 'privileged-release.ps1', 'import-clean-host-result.ps1', 'artifact-manifest.json', 'kit-manifest.json', 'README.md', 'validation') })
        if ($foreign.Count -gt 0) { throw "KIT_OUTPUT_NOT_EMPTY: refusing to overwrite unrelated files under '$output'. Use a new output directory or -Force after reviewing it." }
    }
    New-Item -ItemType Directory -Path $output -Force | Out-Null
    $paths = Get-ReleaseArtifactPaths -ArtifactRoot $BuildRoot
    foreach ($entry in $paths) { Copy-Item -LiteralPath $entry.sourcePath -Destination (Join-Path $output $entry.name) -Force }

    $sourceFiles = @('ReleaseCommon.psm1', 'derive-pin.ps1', 'sign-release.ps1', 'validate-clean-host.ps1', 'cleanup-clean-host.ps1', 'privileged-release.ps1', 'import-clean-host-result.ps1', 'CleanHostKit.README.md')
    foreach ($sourceFile in $sourceFiles) {
        $sourcePath = Join-Path $PSScriptRoot $sourceFile
        if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) { throw "KIT_SOURCE_MISSING: $sourcePath" }
        $destinationName = if ($sourceFile -eq 'CleanHostKit.README.md') { 'README.md' } else { $sourceFile }
        Copy-Item -LiteralPath $sourcePath -Destination (Join-Path $output $destinationName) -Force
    }

    $validationDir = Join-Path $output 'validation'
    New-Item -ItemType Directory -Path $validationDir -Force | Out-Null
    $fftestPath = Join-Path $BuildRoot 'src\fftest\fftest.exe'
    $fftestIncluded = Test-Path -LiteralPath $fftestPath -PathType Leaf
    if ($fftestIncluded) {
        $fftestArchitecture = Get-PeArchitecture -Path $fftestPath
        if ($fftestArchitecture.architecture -ne 'x64') { throw "WRONG_ARCHITECTURE: validation probe is $($fftestArchitecture.architecture), expected x64." }
        Copy-Item -LiteralPath $fftestPath -Destination (Join-Path $validationDir 'fftest.exe') -Force
    }
    $manifest = Get-ReleaseArtifactManifest -ArtifactRoot $output -RepoRoot $repoRoot
    Write-PackageManifest -Manifest $manifest -Path (Join-Path $output 'FastFiles.package.manifest')
    $pinStates = Get-ConfiguredPinStates -RepoRoot $repoRoot -BuildRoot $BuildRoot
    $engineArtifact = $manifest.artifacts | Where-Object filename -eq 'FastFilesEngine.exe' | Select-Object -First 1
    $serviceArtifact = $manifest.artifacts | Where-Object filename -eq 'FastFilesIndexSvc.exe' | Select-Object -First 1
    $enginePinTrusted = $pinStates.engine.state -eq 'POPULATED' -and $engineArtifact.signature.valid -and $engineArtifact.signature.leafThumbprint -eq $pinStates.engine.normalized
    $servicePinTrusted = $pinStates.service.state -eq 'POPULATED' -and $serviceArtifact.signature.valid -and $serviceArtifact.signature.leafThumbprint -eq $pinStates.service.normalized
    $engineKitPin = [pscustomobject]@{ name = 'engine'; state = if ($enginePinTrusted) { 'POPULATED' } elseif ($pinStates.engine.state -in @('PLACEHOLDER', 'MALFORMED', 'MISSING')) { $pinStates.engine.state } else { 'UNVERIFIED' }; length = if ($enginePinTrusted) { 40 } else { 0 }; thumbprint = if ($enginePinTrusted) { $pinStates.engine.normalized } else { $null }; source = if ($enginePinTrusted) { 'matched-to-signed-FastFilesEngine.exe' } else { 'not-exported-until-matched-to-signed-artifact' } }
    $serviceKitPin = [pscustomobject]@{ name = 'service'; state = if ($servicePinTrusted) { 'POPULATED' } elseif ($pinStates.service.state -in @('PLACEHOLDER', 'MALFORMED', 'MISSING')) { $pinStates.service.state } else { 'UNVERIFIED' }; length = if ($servicePinTrusted) { 40 } else { 0 }; thumbprint = if ($servicePinTrusted) { $pinStates.service.normalized } else { $null }; source = if ($servicePinTrusted) { 'matched-to-signed-FastFilesIndexSvc.exe' } else { 'not-exported-until-matched-to-signed-artifact' } }
    Write-ReleaseJson -Value $manifest -Path (Join-Path $output 'artifact-manifest.json')
    $kit = [pscustomobject]@{
        schemaVersion = 1
        kind = 'fastfiles-clean-host-kit'
        commit = $manifest.commit
        architecture = 'x64'
        package = @($manifest.artifacts | ForEach-Object { $_.filename }) + @('FastFiles.package.manifest')
        packageManifest = 'FastFiles.package.manifest'
        validationTool = [pscustomobject]@{ name = 'fftest.exe'; included = $fftestIncluded; path = if ($fftestIncluded) { 'validation/fftest.exe' } else { $null } }
        artifactManifest = 'artifact-manifest.json'
        pinProvisioning = [pscustomobject]@{
            engine = $engineKitPin
            service = $serviceKitPin
            provenanceRequired = $true
        }
        productionSigning = [pscustomobject]@{ required = $true; currentState = if (@($manifest.artifacts | Where-Object { -not $_.signature.valid }).Count -gt 0) { 'BLOCKED_EXTERNAL' } else { 'REQUIRES_PIN_PROVENANCE' } }
        sourceCloneRequired = $false
        buildToolsRequired = $false
    }
    Write-ReleaseJson -Value $kit -Path (Join-Path $output 'kit-manifest.json')
    $result = [pscustomobject]@{ status = 'PASS'; kit = $output; artifactManifest = (Join-Path $output 'artifact-manifest.json'); validationProbeIncluded = $fftestIncluded; productionReady = $false; note = 'The kit is portable preparation only; unsigned artifacts remain blocked for production.' }
    if ($OutputFormat -eq 'json') { $result | ConvertTo-Json -Depth 10 } else { Write-Host "Release kit ready: $output"; Write-Host "Validation probe included: $fftestIncluded"; Write-Host 'Production-ready: NO (authorized signing and pins remain required).' }
    exit 0
} catch {
    if ($OutputFormat -eq 'json') { [pscustomobject]@{ status = 'FAIL'; error = $_.Exception.Message } | ConvertTo-Json -Depth 6 } else { Write-Error $_.Exception.Message }
    exit 1
}
