[CmdletBinding()]
param(
    [string] $KitRoot = $PSScriptRoot,
    [string] $OutputDirectory = (Join-Path $PSScriptRoot 'evidence'),
    [ValidateSet('human', 'json')]
    [string] $OutputFormat = 'human'
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'ReleaseCommon.psm1') -Force -DisableNameChecking

function Test-KitManifest {
    param([Parameter(Mandatory)] [string] $Root)
    $manifestPath = Join-Path $Root 'artifact-manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw "KIT_MANIFEST_REQUIRED: $manifestPath" }
    $packageRoot = Resolve-ReleaseKitPackageRoot -Root $Root
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $rows = @($manifest.artifacts | ForEach-Object {
        $path = Join-Path $packageRoot $_.filename
        $present = Test-Path -LiteralPath $path -PathType Leaf
        $hash = if ($present) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToUpperInvariant() } else { $null }
        $version = if ($present) { Get-FileVersionRecord -Path $path } else { $null }
        $versionMatches = $present -and
            $version.productVersion -ceq $_.productVersion -and
            $version.fileVersion -ceq $_.fileVersion -and
            $version.productName -ceq $_.productName -and
            $version.companyName -ceq $_.companyName -and
            $version.fileDescription -ceq $_.fileDescription -and
            $version.internalName -ieq $_.internalName -and
            $version.originalFilename -ieq $_.originalFilename -and
            $version.buildIdentity -ieq $_.buildIdentity
        [pscustomobject]@{ path = $_.path; present = $present; sha256Matches = $present -and $hash -eq $_.sha256; architecture = if ($present) { (Get-PeArchitecture -Path $path).architecture } else { $null }; versionMetadataMatches = $versionMatches; version = $version; signatureValid = if ($present) { (Get-SignatureRecord -Path $path).valid } else { $false } }
    })
    [pscustomobject]@{ path = $manifestPath; packageRoot = $packageRoot; valid = @($rows | Where-Object { -not $_.present -or -not $_.sha256Matches -or $_.architecture -ne 'x64' -or -not $_.versionMetadataMatches }).Count -eq 0; manifest = $manifest; artifacts = $rows }
}

try {
    $root = [IO.Path]::GetFullPath($KitRoot)
    $preflight = Get-CleanHostPreflight
    $artifactCheck = Test-KitManifest -Root $root
    $packageRoot = [string]$artifactCheck.packageRoot
    $signaturesReady = @($artifactCheck.artifacts | Where-Object { -not $_.signatureValid }).Count -eq 0
    $manifest = $artifactCheck.manifest
    $packageCheck = Test-PackageManifest -Path (Join-Path $packageRoot 'FastFiles.package.manifest') -ArtifactManifest $manifest -PackageRoot $packageRoot
    $status = if ($preflight.clean -and $preflight.architectureCompatible -and $artifactCheck.valid -and $packageCheck.valid -and $signaturesReady) { 'PASS' } else { 'BLOCKED_EXTERNAL' }
    $reason = if (-not $preflight.clean) { 'CLEAN_HOST_PRECONDITION_FAILED' } elseif (-not $preflight.architectureCompatible) { 'WRONG_HOST_ARCHITECTURE' } elseif (-not $artifactCheck.valid) { 'KIT_ARTIFACT_INTEGRITY_OR_VERSION_METADATA_FAILED' } elseif (-not $packageCheck.valid) { 'KIT_PACKAGE_MANIFEST_INVALID' } elseif (-not $signaturesReady) { 'AUTHORIZED_SIGNING_REQUIRED' } else { $null }
    $artifactCheck | Add-Member -NotePropertyName packageManifest -NotePropertyValue $packageCheck
    $result = [pscustomobject]@{
        schemaVersion = 1
        kind = 'fastfiles-clean-host-preflight'
        status = $status
        reason = $reason
        host = $preflight
        artifactCheck = $artifactCheck
        packageRoot = $packageRoot
        installExecuted = $false
        cleanupExecuted = $false
        signatureReady = $signaturesReady
        note = 'This command is read-only. It never removes contamination or installs over an existing FastFiles state.'
    }
    Write-ReleaseJson -Value $preflight -Path (Join-Path $OutputDirectory 'host.json')
    Write-ReleaseJson -Value $artifactCheck -Path (Join-Path $OutputDirectory 'artifact-validation.json')
    Write-ReleaseJson -Value $result -Path (Join-Path $OutputDirectory 'clean-host-result.json')
    Copy-Item -LiteralPath (Join-Path $root 'artifact-manifest.json') -Destination (Join-Path $OutputDirectory 'artifact-manifest.json') -Force
    if (Test-Path -LiteralPath (Join-Path $packageRoot 'FastFiles.package.manifest') -PathType Leaf) {
        Copy-Item -LiteralPath (Join-Path $packageRoot 'FastFiles.package.manifest') -Destination (Join-Path $OutputDirectory 'FastFiles.package.manifest') -Force
    }
    if (Test-Path -LiteralPath (Join-Path $packageRoot 'FastFiles.package.manifest.p7s') -PathType Leaf) {
        Copy-Item -LiteralPath (Join-Path $packageRoot 'FastFiles.package.manifest.p7s') -Destination (Join-Path $OutputDirectory 'FastFiles.package.manifest.p7s') -Force
    }
    if (Test-Path -LiteralPath (Join-Path $root 'kit-manifest.json') -PathType Leaf) {
        Copy-Item -LiteralPath (Join-Path $root 'kit-manifest.json') -Destination (Join-Path $OutputDirectory 'kit-manifest.json') -Force
    }
    $summary = if ($status -eq 'PASS') { 'PASS: host is clean and the kit manifest matches.' } else { "BLOCKED_EXTERNAL: $reason" }
    $summary | Set-Content -LiteralPath (Join-Path $OutputDirectory 'summary.txt') -Encoding utf8NoBOM
    if ($OutputFormat -eq 'json') { $result | ConvertTo-Json -Depth 20 } else { Write-Host $summary; if ($preflight.contamination.Count -gt 0) { Write-Host "Contamination: $($preflight.contamination -join ', ')" } }
    exit $(if ($status -eq 'PASS') { 0 } else { 1 })
} catch {
    if ($OutputFormat -eq 'json') { [pscustomobject]@{ status = 'FAIL'; error = $_.Exception.Message } | ConvertTo-Json -Depth 8 } else { Write-Error $_.Exception.Message }
    exit 1
}
