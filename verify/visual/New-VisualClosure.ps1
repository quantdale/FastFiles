param(
    [Parameter(Mandatory)][string]$ScenarioId,
    [Parameter(Mandatory)][string]$BeforeImagePath,
    [Parameter(Mandatory)][string]$AfterImagePath,
    [string]$OutputRoot = '',
    [string]$DesignRule = '',
    [string]$FindingId = '',
    [string]$FindingJson = '',
    [string]$BaselineIdentity = '',
    [double]$Threshold = 0.01,
    [ValidateSet('PASS','FAIL','BLOCKED')][string]$VisualRegression = 'FAIL',
    [ValidateSet('PASS','FAIL','ADVISORY','BLOCKED')][string]$VisualReview = 'BLOCKED',
    [ValidateSet('PASS','FAIL','BLOCKED')][string]$UiaResult = 'BLOCKED',
    [string]$UiaEvidence = 'no UIA lane executed for this closure',
    [ValidateSet('PASS','FAIL','BLOCKED')][string]$AccessibilityResult = 'BLOCKED',
    [string]$AccessibilityEvidence = 'no accessibility lane executed for this closure',
    [ValidateSet('PASS','FAIL','BLOCKED')][string]$FunctionalResult = 'BLOCKED',
    [string]$FunctionalEvidence = 'no functional lane executed for this closure',
    [string]$PerformanceEvidence = 'no performance sanity check executed for this closure',
    [ValidateSet('FIXED','OPEN','WONT_FIX','NEEDS_REVIEW')][string]$Disposition = 'NEEDS_REVIEW',
    [ValidateSet('live-capture','fixture')][string]$EvidenceKind = 'live-capture'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

<#
    Before/after visual closure packet generator.

    Produces a deterministic closure packet containing:
      - finding ID + design rule + finding blob (optional)
      - before/after captures with SHA-256 hashes
      - deterministic comparator result + diff map
      - baseline identity (when the after image was compared to a baseline)
      - independent lanes: visual regression, visual review, UIA semantic,
        accessibility, functional, performance sanity. Lanes that did not run
        stay BLOCKED with caller-supplied evidence text; they are never
        defaulted to PASS.
      - source commit/tree (real values or null; never a fabricated SHA),
        worktree state, provenance state, final disposition.

    Fixture-driven closure (EvidenceKind=fixture) is supported for machinery
    proof and is always labeled as such; it must never be mistaken for
    live-capture evidence of a shipped fix.
#>

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$compareScript = Join-Path $repo 'verify\visual\Compare-VisualBaseline.ps1'

if (-not $OutputRoot) {
    $OutputRoot = Join-Path $repo "verify\runs\visual-closure\$ScenarioId-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
}
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null

if (-not (Test-Path -LiteralPath $BeforeImagePath -PathType Leaf)) { throw "before image not found: $BeforeImagePath" }
if (-not (Test-Path -LiteralPath $AfterImagePath -PathType Leaf)) { throw "after image not found: $AfterImagePath" }

$diffMapPath = Join-Path $OutputRoot 'diff-map.png'
$comparison = & $compareScript -BaselinePath $BeforeImagePath -CurrentPath $AfterImagePath -Threshold $Threshold -DiffOutputPath $diffMapPath

$finding = $null
if ($FindingJson -and (Test-Path -LiteralPath $FindingJson -PathType Leaf)) {
    $finding = Get-Content -LiteralPath $FindingJson -Raw | ConvertFrom-Json
}

$beforeHash = (Get-FileHash -LiteralPath $BeforeImagePath -Algorithm SHA256).Hash.ToLowerInvariant()
$afterHash = (Get-FileHash -LiteralPath $AfterImagePath -Algorithm SHA256).Hash.ToLowerInvariant()

# Provenance: real commit/tree or explicit null. Never a fabricated SHA.
$commit = try { ((& git -C $repo rev-parse HEAD 2>$null) | Out-String).Trim() } catch { '' }
if ($commit -notmatch '^[0-9a-f]{40}$') { $commit = $null }
$tree = try { ((& git -C $repo rev-parse 'HEAD^{tree}' 2>$null) | Out-String).Trim() } catch { '' }
if ($tree -notmatch '^[0-9a-f]{40}$') { $tree = $null }
$worktreeState = try {
    $porcelain = (& git -C $repo status --porcelain=v1 --untracked-files=no 2>$null)
    if ($LASTEXITCODE -ne 0 -or $null -eq $porcelain) { 'unbound' }
    elseif (@($porcelain).Count -eq 0) { 'clean' } else { 'dirty' }
} catch { 'unbound' }
$provenanceState = if ($worktreeState -eq 'clean' -and $commit -and $tree) {
    'exact-committed-tree'
} elseif ($worktreeState -eq 'dirty') {
    'dirty-development'
} else {
    'unbound-development'
}

$packet = [ordered]@{
    schema = 'fastfiles.visual-closure.v1'
    scenarioId = $ScenarioId
    evidenceKind = $EvidenceKind
    designRule = $DesignRule
    findingId = $FindingId
    before = [ordered]@{ path = $BeforeImagePath; hash = $beforeHash }
    after = [ordered]@{ path = $AfterImagePath; hash = $afterHash }
    comparison = $comparison
    baselineIdentity = $BaselineIdentity
    lanes = [ordered]@{
        visualRegression = [ordered]@{ state = $VisualRegression; evidence = "comparator $($comparison.classification) changed $($comparison.changedPixels)/$($comparison.totalPixels)" }
        visualReview = [ordered]@{ state = $VisualReview; evidence = 'vision-critic review state for this closure' }
        uia = [ordered]@{ state = $UiaResult; evidence = $UiaEvidence }
        accessibility = [ordered]@{ state = $AccessibilityResult; evidence = $AccessibilityEvidence }
        functional = [ordered]@{ state = $FunctionalResult; evidence = $FunctionalEvidence }
        performance = [ordered]@{ state = 'BLOCKED'; evidence = $PerformanceEvidence }
    }
    finding = $finding
    threshold = $Threshold
    diffMapPath = if ($comparison.diffMapPath) { $comparison.diffMapPath } else { $null }
    createdUtc = [DateTime]::UtcNow.ToString('o')
    commit = $commit
    tree = $tree
    worktreeState = $worktreeState
    provenanceState = $provenanceState
    disposition = $Disposition
}

$packetPath = Join-Path $OutputRoot 'closure.json'
$packet | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $packetPath -Encoding utf8

$summaryPath = Join-Path $OutputRoot 'summary.md'
$lines = @(
    "# Visual Closure: $ScenarioId"
    ""
    "- Evidence kind: $EvidenceKind"
    "- Design rule: $DesignRule"
    "- Finding: $(if ($FindingId) { $FindingId } else { 'none' })"
    "- Before: $beforeHash"
    "- After: $afterHash"
    "- Comparison: $($comparison.classification) | changed $($comparison.changedPixels)/$($comparison.totalPixels) ($($comparison.diffPercent))"
    "- Threshold: $Threshold | Passed: $($comparison.passed)"
    "- Baseline: $(if ($BaselineIdentity) { $BaselineIdentity } else { 'none' })"
    "- Lanes: regression=$VisualRegression review=$VisualReview uia=$UiaResult accessibility=$AccessibilityResult functional=$FunctionalResult"
    "- Provenance: $provenanceState ($worktreeState)"
    "- Commit: $(if ($commit) { $commit } else { '<unbound>' })"
    "- Disposition: $Disposition"
)
$lines | Set-Content -LiteralPath $summaryPath -Encoding utf8

Write-Host "closure packet: $packetPath"
Write-Host "diff map: $($comparison.diffMapPath)"
Write-Host "comparison: $($comparison.classification) passed=$($comparison.passed) diffPercent=$($comparison.diffPercent)"

return [pscustomobject]@{
    PacketPath = $packetPath
    SummaryPath = $summaryPath
    Comparison = $comparison
    OutputRoot = $OutputRoot
}
