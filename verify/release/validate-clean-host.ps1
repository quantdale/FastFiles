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
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $rows = @($manifest.artifacts | ForEach-Object {
        $path = Join-Path $Root $_.filename
        $present = Test-Path -LiteralPath $path -PathType Leaf
        $hash = if ($present) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToUpperInvariant() } else { $null }
        [pscustomobject]@{ path = $_.path; present = $present; sha256Matches = $present -and $hash -eq $_.sha256; architecture = if ($present) { (Get-PeArchitecture -Path $path).architecture } else { $null }; signatureValid = if ($present) { (Get-SignatureRecord -Path $path).valid } else { $false } }
    })
    [pscustomobject]@{ path = $manifestPath; valid = @($rows | Where-Object { -not $_.present -or -not $_.sha256Matches -or $_.architecture -ne 'x64' }).Count -eq 0; manifest = $manifest; artifacts = $rows }
}

try {
    $root = [IO.Path]::GetFullPath($KitRoot)
    $preflight = Get-CleanHostPreflight
    $artifactCheck = Test-KitManifest -Root $root
    $signaturesReady = @($artifactCheck.artifacts | Where-Object { -not $_.signatureValid }).Count -eq 0
    $status = if ($preflight.clean -and $preflight.architectureCompatible -and $artifactCheck.valid -and $signaturesReady) { 'PASS' } else { 'BLOCKED_EXTERNAL' }
    $reason = if (-not $preflight.clean) { 'CLEAN_HOST_PRECONDITION_FAILED' } elseif (-not $preflight.architectureCompatible) { 'WRONG_HOST_ARCHITECTURE' } elseif (-not $artifactCheck.valid) { 'KIT_ARTIFACT_INTEGRITY_FAILED' } elseif (-not $signaturesReady) { 'AUTHORIZED_SIGNING_REQUIRED' } else { $null }
    $result = [pscustomobject]@{
        schemaVersion = 1
        kind = 'fastfiles-clean-host-preflight'
        status = $status
        reason = $reason
        host = $preflight
        artifactCheck = $artifactCheck
        installExecuted = $false
        cleanupExecuted = $false
        signatureReady = $signaturesReady
        note = 'This command is read-only. It never removes contamination or installs over an existing FastFiles state.'
    }
    Write-ReleaseJson -Value $preflight -Path (Join-Path $OutputDirectory 'host.json')
    Write-ReleaseJson -Value $artifactCheck -Path (Join-Path $OutputDirectory 'artifact-validation.json')
    Write-ReleaseJson -Value $result -Path (Join-Path $OutputDirectory 'clean-host-result.json')
    Copy-Item -LiteralPath (Join-Path $root 'artifact-manifest.json') -Destination (Join-Path $OutputDirectory 'artifact-manifest.json') -Force
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
