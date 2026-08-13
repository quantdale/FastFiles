[CmdletBinding()]
param(
    [ValidateSet('report', 'gate', 'privileged')]
    [string] $Mode = 'report',
    [string] $ArtifactRoot,
    [string] $PackageDirectory,
    [string] $OutputDirectory = (Join-Path ([IO.Path]::GetTempPath()) 'FastFiles-release-readiness'),
    [string] $ExpectedSignerThumbprint,
    [ValidateSet('human', 'json')]
    [string] $OutputFormat = 'human'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Import-Module (Join-Path $PSScriptRoot 'ReleaseCommon.psm1') -Force -DisableNameChecking

if ($Mode -eq 'privileged') {
    $driver = Join-Path $PSScriptRoot 'privileged-release.ps1'
    $buildRoot = if ($ArtifactRoot) { $ArtifactRoot } else { Join-Path $repoRoot 'build\release' }
    if ($PackageDirectory) {
        $package = $PackageDirectory
    } elseif ((Test-Path -LiteralPath (Join-Path $buildRoot 'FastFiles.package.manifest') -PathType Leaf) -and
              (Test-Path -LiteralPath (Join-Path $buildRoot 'FastFilesSetup.exe') -PathType Leaf)) {
        $package = $buildRoot
    } else {
        $package = Join-Path $OutputDirectory 'kit'
        $kitScript = Join-Path $PSScriptRoot 'New-ReleaseKit.ps1'
        & (Get-Command pwsh).Source -NoProfile -File $kitScript -BuildRoot $buildRoot -OutputDirectory $package -OutputFormat json | Out-Null
        if ($LASTEXITCODE -ne 0) { Write-Error "RELEASE_KIT_GENERATION_FAILED: New-ReleaseKit.ps1 exited with code $LASTEXITCODE."; exit 1 }
    }
    $args = @('-NoProfile', '-File', $driver, '-PackageDirectory', $package, '-RepoRoot', $repoRoot, '-BuildRoot', $buildRoot, '-OutputDirectory', $OutputDirectory, '-OutputFormat', $OutputFormat)
    if ($ExpectedSignerThumbprint) { $args += @('-ExpectedSignerThumbprint', $ExpectedSignerThumbprint) }
    & (Get-Command pwsh).Source @args
    exit $LASTEXITCODE
}

try {
    if (-not $ArtifactRoot) { $ArtifactRoot = Join-Path $repoRoot 'build\release' }
    $manifest = Get-ReleaseArtifactManifest -ArtifactRoot $ArtifactRoot -RepoRoot $repoRoot
    $versionCheck = Test-ReleaseVersionMetadata -ArtifactRoot $ArtifactRoot -RepoRoot $repoRoot
    $pinStates = Get-ConfiguredPinStates -RepoRoot $repoRoot -BuildRoot (Join-Path $repoRoot 'build\release')
    $matrix = @(Get-ReleaseSignatureMatrix -Manifest $manifest -PinStates $pinStates -ExpectedSignerThumbprint $ExpectedSignerThumbprint)
    $hostPreflight = Get-CleanHostPreflight
    $artifactIssues = @($manifest.artifacts | Where-Object { $_.architecture -ne 'x64' -or $_.sha256.Length -ne 64 }) +
        @($versionCheck.artifacts | Where-Object { -not $_.valid })
    $artifactStatus = if ($artifactIssues.Count -eq 0) { 'PASS' } else { 'FAIL' }
    $signingStatus = if (@($matrix | Where-Object { $_.result -eq 'PASS' }).Count -eq @($matrix).Count) { 'PASS' } else { 'BLOCKED_EXTERNAL' }
    $pinsStatus = if ($pinStates.engine.state -eq 'POPULATED' -and $pinStates.service.state -eq 'POPULATED' -and $signingStatus -eq 'PASS') { 'PASS' } else { 'BLOCKED_EXTERNAL' }
    $privilegedReason = if ($signingStatus -ne 'PASS') { 'AUTHORIZED_SIGNING_REQUIRED' } elseif ($pinsStatus -ne 'PASS') { 'PRODUCTION_PINS_REQUIRED' } elseif (-not $hostPreflight.clean) { 'CLEAN_HOST_PRECONDITION_FAILED' } elseif (-not $hostPreflight.elevated) { 'ELEVATED_EXECUTION_REQUIRED' } else { 'READY_FOR_PRIVILEGED_DRIVER' }
    $result = [pscustomobject]@{
        schemaVersion = 1
        kind = 'fastfiles-release-readiness'
        mode = $Mode
        generatedAtUtc = [DateTime]::UtcNow.ToString('o')
        commit = $manifest.commit
        build = [pscustomobject]@{ status = if ($artifactStatus -eq 'PASS') { 'ARTIFACTS_PRESENT' } else { 'FAIL' }; configuration = $manifest.configuration; expectedArchitecture = 'x64'; evidence = 'This command validates the existing Release artifact set; it does not rebuild.' }
        tests = [pscustomobject]@{ status = 'NOT_RUN'; command = 'ctest --preset debug; ctest --preset release'; evidence = 'Run repository test gates separately; this report never infers tests from artifact presence.' }
        artifactIntegrity = [pscustomobject]@{ status = $artifactStatus; manifest = $manifest; versionMetadata = $versionCheck }
        signatures = [pscustomobject]@{ status = $signingStatus; matrix = $matrix }
        pins = [pscustomobject]@{ status = $pinsStatus; engine = Get-SafePinState -PinState $pinStates.engine; service = Get-SafePinState -PinState $pinStates.service; productionValuesChanged = $false }
        privilegedPath = [pscustomobject]@{ status = if ($privilegedReason -eq 'READY_FOR_PRIVILEGED_DRIVER') { 'READY' } else { 'BLOCKED_EXTERNAL' }; reason = $privilegedReason; driver = 'verify/release/privileged-release.ps1' }
        cleanHost = [pscustomobject]@{ status = if ($hostPreflight.clean) { 'PASS' } else { 'BLOCKED_EXTERNAL' }; preflight = $hostPreflight; kit = 'verify/release/New-ReleaseKit.ps1' }
        visual = [pscustomobject]@{ status = 'VISUAL_ONLY_OUT_OF_SCOPE'; percentOfParentSemantics = 'COMPLETE'; directRenderedAssertion = 'EXTERNAL' }
        openspec = [pscustomobject]@{ status = 'NOT_RUN'; command = 'npx --yes --package=@fission-ai/openspec@1.8.0 openspec validate --all --strict --no-interactive' }
        overall = if ($artifactStatus -eq 'FAIL') { 'FAIL' } else { 'BLOCKED_EXTERNAL' }
        externalPrerequisites = @('authorized production signing material/provider', 'approved production leaf thumbprints', 'elevated signed MFT/USN runtime on a clean Windows host', 'direct rendered visual evidence for OpenSpec task 5.1')
    }
    Write-ReleaseJson -Value $result -Path (Join-Path $OutputDirectory 'release-readiness.json')
    if ($OutputFormat -eq 'json') { $result | ConvertTo-Json -Depth 20 } else {
        Write-Host "Release readiness: $($result.overall)"
        Write-Host "Artifact integrity: $artifactStatus"
        Write-Host "Signing: $signingStatus"
        Write-Host "Production pins: $pinsStatus"
        Write-Host "Privileged path: $($result.privilegedPath.status) ($privilegedReason)"
        Write-Host "Clean host: $($result.cleanHost.status)"
        Write-Host 'Direct rendered visual assertion: VISUAL_ONLY_OUT_OF_SCOPE'
    }
    exit $(if ($result.overall -eq 'FAIL') { 1 } elseif ($result.overall -eq 'BLOCKED_EXTERNAL') { 2 } else { 0 })
} catch {
    if ($OutputFormat -eq 'json') { [pscustomobject]@{ status = 'FAIL'; error = $_.Exception.Message } | ConvertTo-Json -Depth 8 } else { Write-Error $_.Exception.Message }
    exit 1
}
