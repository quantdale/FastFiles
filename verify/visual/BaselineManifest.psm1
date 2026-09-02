Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

<#
    Baseline manifest lifecycle for deterministic visual regression.

    Each baseline entry is bound to an environment profile (theme, density, dpi, viewport, pane mode)
    plus source identity (commit/tree) and scenario. Baselines are content-addressed by
    SHA256 of the image file; paths are sanitized and relative.

    Update is explicit: Add-BaselineEntry requires -Force or a new identity. A failing
    visual test must never silently overwrite its baseline.
#>

function New-BaselineManifest {
    param(
        [string] $SchemaVersion = '1',
        [string] $Description = 'FastFiles visual baseline manifest'
    )
    return [ordered]@{
        schemaVersion = $SchemaVersion
        schema = 'fastfiles.visual-baseline-manifest.v1'
        createdUtc = [DateTime]::UtcNow.ToString('o')
        updatedUtc = [DateTime]::UtcNow.ToString('o')
        entries = @()
    }
}

function Get-BaselineEnvironmentKey {
    param(
        [Parameter(Mandatory)][string] $ScenarioId,
        [Parameter(Mandatory)][string] $Theme,
        [string] $Density = 'comfortable',
        [int] $Dpi = 96,
        [int] $ViewportWidth = 1280,
        [int] $ViewportHeight = 800,
        [string] $PaneMode = 'single'
    )
    return "$ScenarioId|$Theme|$Density|${Dpi}|${ViewportWidth}x${ViewportHeight}|$PaneMode"
}

function Add-BaselineEntry {
    param(
        [Parameter(Mandatory)] $Manifest,
        [Parameter(Mandatory)][string] $ScenarioId,
        [Parameter(Mandatory)][string] $ImagePath,
        [Parameter(Mandatory)][string] $SourceCommit,
        [string] $SourceTree,
        [Parameter(Mandatory)][string] $Theme,
        [string] $Density = 'comfortable',
        [int] $Dpi = 96,
        [int] $ViewportWidth = 1280,
        [int] $ViewportHeight = 800,
        [string] $PaneMode = 'single',
        [string] $Rationale = '',
        [string] $BaselineRoot = '',
        [switch] $Force
    )
    if (-not (Test-Path -LiteralPath $ImagePath -PathType Leaf)) { throw "baseline image not found: $ImagePath" }
    if ($SourceCommit -notmatch '^[0-9a-f]{40}$') { throw "SourceCommit must be 40-char hex, got '$SourceCommit'" }
    $key = Get-BaselineEnvironmentKey -ScenarioId $ScenarioId -Theme $Theme -Density $Density -Dpi $Dpi -ViewportWidth $ViewportWidth -ViewportHeight $ViewportHeight -PaneMode $PaneMode
    $existing = @($Manifest.entries | Where-Object { $_.environmentKey -eq $key })
    if ($existing.Count -gt 0 -and -not $Force) {
        throw "baseline entry for '$key' already exists; use -Force to update explicitly"
    }
    if ($existing.Count -gt 0 -and $Force) {
        $Manifest.entries = @($Manifest.entries | Where-Object { $_.environmentKey -ne $key })
    }
    $hash = (Get-FileHash -LiteralPath $ImagePath -Algorithm SHA256).Hash.ToLowerInvariant()
    $relativePath = if ($BaselineRoot) {
        try { [System.IO.Path]::GetRelativePath($BaselineRoot, $ImagePath) -replace '\\','/' } catch { $ImagePath }
    } else { $ImagePath -replace '\\','/' }

    # Sanitize: reject absolute user-profile paths being committed
    if ($relativePath -match '^[A-Za-z]:' -or $relativePath -match '^\\\\') {
        throw "baseline path must be relative/sanitized, got '$relativePath' (use BaselineRoot)"
    }

    $entry = [ordered]@{
        baselineId = "$ScenarioId@$Theme/${ViewportWidth}x${ViewportHeight}/$PaneMode"
        environmentKey = $key
        scenarioId = $ScenarioId
        theme = $Theme
        density = $Density
        dpi = $Dpi
        viewport = [ordered]@{ width = $ViewportWidth; height = $ViewportHeight }
        paneMode = $PaneMode
        imagePath = $relativePath
        screenshotHash = $hash
        sourceCommit = $SourceCommit
        sourceTree = $SourceTree
        rationale = $Rationale
        createdUtc = [DateTime]::UtcNow.ToString('o')
        updatedUtc = [DateTime]::UtcNow.ToString('o')
    }
    $Manifest.entries += [pscustomobject]$entry
    $Manifest.updatedUtc = [DateTime]::UtcNow.ToString('o')
    return $entry
}

function Get-BaselineEntry {
    param(
        [Parameter(Mandatory)] $Manifest,
        [Parameter(Mandatory)][string] $ScenarioId,
        [string] $Theme,
        [string] $Density,
        [int] $Dpi = -1,
        [int] $ViewportWidth = -1,
        [int] $ViewportHeight = -1,
        [string] $PaneMode
    )
    $results = @($Manifest.entries)
    if ($ScenarioId) { $results = @($results | Where-Object { $_.scenarioId -eq $ScenarioId }) }
    if ($Theme) { $results = @($results | Where-Object { $_.theme -eq $Theme }) }
    if ($Density) { $results = @($results | Where-Object { $_.density -eq $Density }) }
    if ($Dpi -ge 0) { $results = @($results | Where-Object { $_.dpi -eq $Dpi }) }
    if ($ViewportWidth -ge 0) { $results = @($results | Where-Object { $_.viewport.width -eq $ViewportWidth }) }
    if ($ViewportHeight -ge 0) { $results = @($results | Where-Object { $_.viewport.height -eq $ViewportHeight }) }
    if ($PaneMode) { $results = @($results | Where-Object { $_.paneMode -eq $PaneMode }) }
    return $results
}

function Test-BaselineManifest {
    param([Parameter(Mandatory)] $Manifest)
    $errors = [System.Collections.Generic.List[string]]::new()
    if (-not $Manifest.schemaVersion) { [void]$errors.Add('schemaVersion required') }
    if ($null -eq $Manifest.entries) { [void]$errors.Add('entries array required') }
    $seenKeys = @{}
    foreach ($entry in @($Manifest.entries)) {
        if (-not $entry.baselineId) { [void]$errors.Add('entry missing baselineId') }
        if (-not $entry.environmentKey) { [void]$errors.Add("entry $($entry.baselineId) missing environmentKey") }
        if ($entry.screenshotHash -and $entry.screenshotHash -notmatch '^[0-9a-f]{64}$') { [void]$errors.Add("entry $($entry.baselineId) has invalid screenshotHash") }
        if ($entry.sourceCommit -and $entry.sourceCommit -notmatch '^[0-9a-f]{40}$') { [void]$errors.Add("entry $($entry.baselineId) has invalid sourceCommit") }
        if ($entry.environmentKey) {
            if ($seenKeys.ContainsKey($entry.environmentKey)) { [void]$errors.Add("duplicate environmentKey '$($entry.environmentKey)'") }
            else { $seenKeys[$entry.environmentKey] = $true }
        }
        if ($entry.imagePath -match '^[A-Za-z]:' -or $entry.imagePath -match '^\\\\') { [void]$errors.Add("entry $($entry.baselineId) has non-relative imagePath") }
    }
    return [pscustomobject]@{ Valid = ($errors.Count -eq 0); Errors = @($errors) }
}

function Save-BaselineManifest {
    param(
        [Parameter(Mandatory)] $Manifest,
        [Parameter(Mandatory)][string] $Path
    )
    $dir = Split-Path -Parent $Path
    if ($dir -and -not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    $Manifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $Path -Encoding utf8
}

function Load-BaselineManifest {
    param([Parameter(Mandatory)][string] $Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "baseline manifest not found: $Path" }
    $content = Get-Content -LiteralPath $Path -Raw
    $manifest = $content | ConvertFrom-Json
    $validation = Test-BaselineManifest -Manifest $manifest
    if (-not $validation.Valid) {
        throw "baseline manifest invalid: $($validation.Errors -join '; ')"
    }
    return $manifest
}

function Compare-AgainstBaseline {
    param(
        [Parameter(Mandatory)] $Manifest,
        [Parameter(Mandatory)][string] $ScenarioId,
        [Parameter(Mandatory)][string] $CurrentImagePath,
        [string] $Theme = '',
        [string] $Density = '',
        [int] $Dpi = -1,
        [int] $ViewportWidth = -1,
        [int] $ViewportHeight = -1,
        [string] $PaneMode = '',
        [string] $BaselineRoot = '',
        [double] $Threshold = 0.01,
        [string] $DiffOutputPath = ''
    )
    $candidates = @(Get-BaselineEntry -Manifest $Manifest -ScenarioId $ScenarioId -Theme $Theme -Density $Density -Dpi $Dpi -ViewportWidth $ViewportWidth -ViewportHeight $ViewportHeight -PaneMode $PaneMode)
    if ($candidates.Count -eq 0) {
        return [pscustomobject]@{ Status = 'MissingBaseline'; Message = "no baseline for scenario '$ScenarioId' with given environment profile"; BaselineEntry = $null }
    }
    if ($candidates.Count -gt 1) {
        return [pscustomobject]@{ Status = 'AmbiguousEnvironment'; Message = "multiple baselines match scenario '$ScenarioId'; refine environment profile"; BaselineEntry = $null; Candidates = $candidates }
    }
    $entry = $candidates[0]
    $baselinePath = if ($BaselineRoot) { Join-Path $BaselineRoot $entry.imagePath } else { $entry.imagePath }
    if (-not (Test-Path -LiteralPath $baselinePath -PathType Leaf)) {
        return [pscustomobject]@{ Status = 'MissingBaselineFile'; Message = "baseline file not found: $baselinePath"; BaselineEntry = $entry }
    }
    $compareScript = Join-Path $PSScriptRoot 'Compare-VisualBaseline.ps1'
    $params = @{ BaselinePath = $baselinePath; CurrentPath = $CurrentImagePath; Threshold = $Threshold }
    if ($DiffOutputPath) { $params.DiffOutputPath = $DiffOutputPath }
    $result = & $compareScript @params
    return [pscustomobject]@{
        Status = if ($result.passed) { 'Pass' } else { 'Fail' }
        BaselineEntry = $entry
        Comparison = $result
    }
}

Export-ModuleMember -Function New-BaselineManifest, Get-BaselineEnvironmentKey, Add-BaselineEntry,
    Get-BaselineEntry, Test-BaselineManifest, Save-BaselineManifest, Load-BaselineManifest,
    Compare-AgainstBaseline
