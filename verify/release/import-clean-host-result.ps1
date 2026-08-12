[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $BundleDirectory,
    [Parameter(Mandatory)] [string] $DestinationDirectory,
    [switch] $Force,
    [ValidateSet('human', 'json')]
    [string] $OutputFormat = 'human'
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'ReleaseCommon.psm1') -Force -DisableNameChecking

try {
    $bundle = [IO.Path]::GetFullPath($BundleDirectory).TrimEnd('\')
    $destination = [IO.Path]::GetFullPath($DestinationDirectory).TrimEnd('\')
    $resultPath = Join-Path $bundle 'clean-host-result.json'
    $manifestPath = Join-Path $bundle 'artifact-manifest.json'
    if (-not (Test-Path -LiteralPath $resultPath -PathType Leaf)) { throw "EVIDENCE_RESULT_REQUIRED: $resultPath" }
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw "EVIDENCE_MANIFEST_REQUIRED: $manifestPath" }
    $result = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($result.kind -ne 'fastfiles-clean-host-preflight' -or $result.status -ne 'PASS' -or -not $result.host.clean) { throw 'EVIDENCE_NOT_PASS: only a validated PASS clean-host result may be imported.' }
    if ($result.artifactCheck.manifest.commit -ne $manifest.commit) { throw 'EVIDENCE_PROVENANCE_MISMATCH: result and manifest commit values differ.' }
    $artifactRows = @($manifest.artifacts | ForEach-Object {
        $path = Join-Path $bundle $_.filename
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToUpperInvariant()
            if ($actual -ne $_.sha256) { throw "EVIDENCE_ARTIFACT_HASH_MISMATCH: $($_.filename)" }
            [pscustomobject]@{ filename = $_.filename; sha256 = $actual; bytesPresent = $true }
        } else {
            $captured = @($result.artifactCheck.artifacts | Where-Object path -eq $_.path | Select-Object -First 1)
            if ($captured.Count -ne 1 -or -not $captured[0].present -or -not $captured[0].sha256Matches) { throw "EVIDENCE_ARTIFACT_HASH_UNCONFIRMED: $($_.filename)" }
            [pscustomobject]@{ filename = $_.filename; sha256 = $_.sha256; bytesPresent = $false }
        }
    })
    if ((Test-Path -LiteralPath $destination) -and -not $Force) { throw "EVIDENCE_DESTINATION_EXISTS: use a new destination or -Force after review." }
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    foreach ($name in @('clean-host-result.json', 'host.json', 'artifact-validation.json', 'artifact-manifest.json', 'summary.txt', 'privileged-release-result.json', 'transaction.json')) {
        $source = Join-Path $bundle $name
        if (Test-Path -LiteralPath $source -PathType Leaf) { Copy-Item -LiteralPath $source -Destination (Join-Path $destination $name) -Force }
    }
    $importRecord = [pscustomobject]@{
        schemaVersion = 1
        kind = 'fastfiles-clean-host-evidence-import'
        importedAtUtc = [DateTime]::UtcNow.ToString('o')
        sourceBundle = $bundle
        destination = $destination
        commit = $manifest.commit
        artifacts = $artifactRows
        taskStateChanged = $false
        note = 'Validated evidence was copied only. OpenSpec tasks are never marked automatically by this command.'
    }
    Write-ReleaseJson -Value $importRecord -Path (Join-Path $destination 'import-record.json')
    if ($OutputFormat -eq 'json') { $importRecord | ConvertTo-Json -Depth 12 } else { Write-Host "PASS: validated clean-host evidence imported to $destination" }
    exit 0
} catch {
    if ($OutputFormat -eq 'json') { [pscustomobject]@{ status = 'FAIL'; error = $_.Exception.Message } | ConvertTo-Json -Depth 8 } else { Write-Error $_.Exception.Message }
    exit 1
}
