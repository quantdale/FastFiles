[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $EvidenceDirectory,

    [switch] $RequireExactTree,

    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $EvidenceDirectory)) {
    throw "Evidence directory not found: $EvidenceDirectory"
}

Write-Host "Verifying Evidence Consistency and Artifact Integrity:"
Write-Host "  Directory: $EvidenceDirectory"

$discrepancies = [System.Collections.Generic.List[string]]::new()

# 1. Verify Manifest Hashes
$manifestPath = Join-Path $EvidenceDirectory "evidence-manifest.json"
if (Test-Path -LiteralPath $manifestPath) {
    Write-Host "  Verifying evidence-manifest.json..."
    $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding utf8 | ConvertFrom-Json
    foreach ($entry in $manifest.artifacts.PSObject.Properties) {
        $fileName = $entry.Name
        $expectedSha = $entry.Value.sha256.ToLowerInvariant()
        $targetFile = Join-Path $EvidenceDirectory $fileName
        if (-not (Test-Path -LiteralPath $targetFile)) {
            $discrepancies.Add("Manifest references missing file: $fileName")
            continue
        }
        $actualBytes = [IO.File]::ReadAllBytes($targetFile)
        $actualSha = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($actualBytes)).Replace('-', '').ToLowerInvariant()
        if ($actualSha -ne $expectedSha) {
            $discrepancies.Add("Manifest hash mismatch for ${fileName}: manifest=$expectedSha, actual=$actualSha")
        }
    }
    Write-Host "  Manifest verification: $(if ($discrepancies.Count -eq 0) { 'PASS' } else { 'FAIL' })"
}

# 2. Verify Sidecar Hashes
$sidecars = @(Get-ChildItem -LiteralPath $EvidenceDirectory -Filter "*.json.sha256" -File)
foreach ($sc in $sidecars) {
    $targetJson = $sc.FullName.Substring(0, $sc.FullName.Length - 7) # remove .sha256
    if (Test-Path -LiteralPath $targetJson) {
        $expectedHash = ((Get-Content -LiteralPath $sc.FullName -Raw).Trim() -split '\s+')[0].ToLowerInvariant()
        $actualBytes = [IO.File]::ReadAllBytes($targetJson)
        $actualSha = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($actualBytes)).Replace('-', '').ToLowerInvariant()
        if ($actualSha -ne $expectedHash) {
            $discrepancies.Add("Sidecar hash mismatch for $([IO.Path]::GetFileName($targetJson)): sidecar=$expectedHash, actual=$actualSha")
        }
    }
}

# 3. Verify Exact-Tree Bound Invariant
if ($RequireExactTree) {
    $jsonFiles = @(Get-ChildItem -LiteralPath $EvidenceDirectory -Filter "*.json" -File | Where-Object { $_.Name -ne 'evidence-manifest.json' })
    foreach ($jf in $jsonFiles) {
        $doc = Get-Content -LiteralPath $jf.FullName -Raw -Encoding utf8 | ConvertFrom-Json
        if ($null -ne $doc.exactTreeBound -and -not [bool]$doc.exactTreeBound) {
            $discrepancies.Add("Artifact $($jf.Name) has exactTreeBound=false while -RequireExactTree is asserted")
        }
    }
}

# 4. Verify Resource Soak Markdown vs JSON
$soakJsonPath = Join-Path $EvidenceDirectory "resource-soak-summary.json"
$soakMdPath = Join-Path $EvidenceDirectory "resource-soak-summary.md"
if ((Test-Path -LiteralPath $soakJsonPath) -and (Test-Path -LiteralPath $soakMdPath)) {
    Write-Host "  Checking consistency between resource-soak-summary.md and resource-soak-summary.json..."
    $soakJson = Get-Content -LiteralPath $soakJsonPath -Raw -Encoding utf8 | ConvertFrom-Json
    $soakMdText = Get-Content -LiteralPath $soakMdPath -Raw -Encoding utf8

    # Check commit
    if ($soakMdText -notmatch [regex]::Escape($soakJson.commit)) {
        $discrepancies.Add("resource-soak-summary.md does not contain commit $($soakJson.commit)")
    }
    # Check tree
    if ($soakMdText -notmatch [regex]::Escape($soakJson.tree)) {
        $discrepancies.Add("resource-soak-summary.md does not contain tree $($soakJson.tree)")
    }
    # Check disposition
    if ($soakMdText -notmatch [regex]::Escape($soakJson.disposition)) {
        $discrepancies.Add("resource-soak-summary.md does not contain disposition $($soakJson.disposition)")
    }
    # Check steady-state handles
    $handlesBase = [string]$soakJson.steadyStateMetrics.handles.baseline
    $handlesFinal = [string]$soakJson.steadyStateMetrics.handles.final
    if ($soakMdText -notmatch $handlesBase -or $soakMdText -notmatch $handlesFinal) {
        $discrepancies.Add("resource-soak-summary.md does not match steady-state handles ($handlesBase -> $handlesFinal)")
    }
    # Check steady-state threads
    $threadsBase = [string]$soakJson.steadyStateMetrics.threads.baseline
    $threadsFinal = [string]$soakJson.steadyStateMetrics.threads.final
    if ($soakMdText -notmatch $threadsBase -or $soakMdText -notmatch $threadsFinal) {
        $discrepancies.Add("resource-soak-summary.md does not match steady-state threads ($threadsBase -> $threadsFinal)")
    }
}

# 5. Verify Long-Run Markdown vs JSON Artifacts
$longRunMdPath = Join-Path $EvidenceDirectory "local-long-run-certification.md"
if (Test-Path -LiteralPath $longRunMdPath) {
    Write-Host "  Checking consistency between local-long-run-certification.md and long-run-*.json artifacts..."
    $lrMdText = Get-Content -LiteralPath $longRunMdPath -Raw -Encoding utf8
    $longRunFiles = @(Get-ChildItem -LiteralPath $EvidenceDirectory -Filter "long-run-*.json" -File | Where-Object { $_.Name -ne 'long-run-summary.json' })
    foreach ($lrf in $longRunFiles) {
        $data = Get-Content -LiteralPath $lrf.FullName -Raw -Encoding utf8 | ConvertFrom-Json
        # Check testId exists in markdown
        if ($lrMdText -notmatch [regex]::Escape($data.testId)) {
            $discrepancies.Add("local-long-run-certification.md missing test ID $($data.testId)")
        }
        # Check disposition exists
        if ($lrMdText -notmatch [regex]::Escape($data.disposition)) {
            $discrepancies.Add("local-long-run-certification.md missing disposition $($data.disposition)")
        }
    }
}

if ($discrepancies.Count -gt 0) {
    Write-Host "`nConsistency check FAILED with $($discrepancies.Count) discrepancy(ies):" -ForegroundColor Red
    foreach ($d in $discrepancies) {
        Write-Host "  - $d" -ForegroundColor Red
    }
    throw "Evidence consistency check failed: $($discrepancies.Count) error(s) found"
}

Write-Host "Evidence consistency and hash integrity check PASSED (0 discrepancies)." -ForegroundColor Green
return $true
