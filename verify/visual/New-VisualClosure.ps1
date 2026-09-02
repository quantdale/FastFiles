param(
    [Parameter(Mandatory)][string]$ScenarioId,
    [Parameter(Mandatory)][string]$BeforeImagePath,
    [Parameter(Mandatory)][string]$AfterImagePath,
    [string]$OutputRoot = '',
    [string]$DesignRule = '',
    [string]$FindingJson = '',
    [double]$Threshold = 0.01
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

<#
    Before/after visual closure packet generator.

    Takes a before image, an after image, a design rule / finding, and
    produces a deterministic closure packet containing:
      - comparison result (pixel diff, classification)
      - finding before/after
      - diff map image
      - UIA/accessibility/functional regression placeholders (caller fills)

    The before and after images must be real captures (not fixtures) when
    available; fixture-driven closure is also supported for machinery proof.
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

$packet = [ordered]@{
    schema = 'fastfiles.visual-closure.v1'
    scenarioId = $ScenarioId
    designRule = $DesignRule
    before = [ordered]@{ path = $BeforeImagePath; hash = $beforeHash }
    after = [ordered]@{ path = $AfterImagePath; hash = $afterHash }
    comparison = $comparison
    finding = $finding
    threshold = $Threshold
    diffMapPath = if ($comparison.diffMapPath) { $comparison.diffMapPath } else { $null }
    createdUtc = [DateTime]::UtcNow.ToString('o')
    commit = try { (& git -C $repo rev-parse HEAD 2>$null).Trim() } catch { '' }
}

$packetPath = Join-Path $OutputRoot 'closure.json'
$packet | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $packetPath -Encoding utf8

$summaryPath = Join-Path $OutputRoot 'summary.md'
$lines = @(
    "# Visual Closure: $ScenarioId"
    ""
    "- Design rule: $DesignRule"
    "- Before: $beforeHash"
    "- After: $afterHash"
    "- Comparison: $($comparison.classification) | changed $($comparison.changedPixels)/$($comparison.totalPixels) ($($comparison.diffPercent))"
    "- Threshold: $Threshold | Passed: $($comparison.passed)"
    "- Finding: $(if ($finding) { $finding.findingId } else { 'none' })"
    "- Commit: $($packet.commit)"
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
