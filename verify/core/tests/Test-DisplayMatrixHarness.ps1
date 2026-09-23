<#
    hardening-campaign Phase 6: display-matrix harness contract. Fixed
    four-status vocabulary (PASS | FAIL | HOST_UNSUPPORTED | NOT_RUN),
    injectable detection fixtures, requested-cell completeness, and evidence
    shape. Headless: imports the detector and resolves cells from injectable
    probes; runs no app, writes no verify/runs evidence, mutates nothing.
    Exit 0 = PASS; exit 1 = FAIL.
#>

$ErrorActionPreference = 'Stop'
$failures = 0
function Check {
    param([bool] $Condition, [string] $Description)
    if ($Condition) { Write-Host "PASS: $Description" } else { Write-Host "FAIL: $Description"; $script:failures++ }
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
$modulePath = Join-Path $repoRoot 'verify\certification\HostDisplayCapabilities.psm1'
$orchestratorPath = Join-Path $repoRoot 'verify\certification\Invoke-DisplayMatrix.ps1'
Import-Module $modulePath -Force -DisableNameChecking

function New-DisplayMatrixFixture {
    param(
        [object[]] $Monitors,
        [int] $SystemDpi = 96,
        [object[]] $PerMonitorDpi = @(),
        $AppsUseLightTheme = 1,
        $SystemUsesLightTheme = 1,
        [int] $HighContrastFlags = 0,
        $MixedDpi = $null
    )
    $probe = @{
        Monitors            = $Monitors
        SystemDpi           = $SystemDpi
        PerMonitorDpi       = $PerMonitorDpi
        AppsUseLightTheme   = $AppsUseLightTheme
        SystemUsesLightTheme = $SystemUsesLightTheme
        HighContrastFlags   = $HighContrastFlags
    }
    if ($null -ne $MixedDpi) { $probe['MixedDpi'] = $MixedDpi }
    return $probe
}

function New-DisplayMatrixMonitor {
    param([string] $Id, [int] $Width = 1920, [int] $Height = 1080, [int] $X = 0, [int] $Y = 0, [bool] $Primary = $false)
    return [pscustomobject]@{
        Id         = $Id
        DeviceName = $Id
        Bounds     = [pscustomobject]@{ X = $X; Y = $Y; Width = $Width; Height = $Height }
        Primary    = $Primary
    }
}

# --- 1. Fixed four-status vocabulary ---
$vocabulary = @(Get-DisplayCellVocabulary)
Check ((@($vocabulary | Sort-Object) -join '|') -eq 'FAIL|HOST_UNSUPPORTED|NOT_RUN|PASS') 'cell vocabulary is exactly the fixed four-status set'
Check ($vocabulary.Count -eq 4) 'cell vocabulary has exactly four members'

# The orchestrator text must name all four statuses and must never contain a
# PASS claim reachable from the detection path.
$orchestratorText = Get-Content -LiteralPath $orchestratorPath -Raw
foreach ($status in @('PASS', 'FAIL', 'HOST_UNSUPPORTED', 'NOT_RUN')) {
    Check ($orchestratorText -match "'$status'") "orchestrator vocabulary includes '$status'"
}
Check ($orchestratorText -notmatch 'GitHub CI PASS') 'orchestrator never contains a hosted PASS claim'

# --- 2. Requested-cell completeness: every listed cell exactly once ---
$cells = @(Get-DisplayMatrixCells)
Check ($cells.Count -ge 14) "matrix lists at least 14 cells (observed $($cells.Count))"
$ids = @($cells | ForEach-Object { [string]$_.Id })
Check ((@($ids | Sort-Object -Unique)).Count -eq $ids.Count) 'every requested cell appears exactly once'
foreach ($required in @('dpi-100', 'dpi-125', 'dpi-150', 'dpi-200', 'high-contrast', 'light-theme', 'dark-theme',
        'single-monitor', 'multi-monitor', 'mixed-dpi', 'monitor-transition',
        'window-maximize', 'window-restore', 'window-minimize', 'window-resize')) {
    Check ($ids -contains $required) "required cell '$required' is listed"
}
foreach ($cell in $cells) {
    Check (-not [string]::IsNullOrWhiteSpace([string]$cell.VisualScenario)) "cell '$($cell.Id)' maps to a visual-catalog scenario"
    Check (-not [string]::IsNullOrWhiteSpace([string]$cell.OwnerAction)) "cell '$($cell.Id)' documents its user-session owner action"
    Check (@($cell.Requires).Count -gt 0) "cell '$($cell.Id)' declares at least one prerequisite"
}
$categories = @($cells | ForEach-Object { [string]$_.Category } | Sort-Object -Unique)
foreach ($category in @('dpi', 'accessibility', 'theme', 'topology', 'window-state')) {
    Check ($categories -contains $category) "cell category '$category' is covered"
}

# The mapped scenarios must exist in the canonical catalog, including the
# Block rows whose text documents why the campaign never mutates those host
# settings itself.
$visualModulePath = Join-Path $repoRoot 'verify\capabilities\windows-ui-visual-validation\VisualValidation.psm1'
$visualText = Get-Content -LiteralPath $visualModulePath -Raw
$mapped = @($cells | ForEach-Object { [string]$_.VisualScenario } | Sort-Object -Unique)
Check ($mapped.Count -gt 0) 'mapped visual scenarios are non-empty'
foreach ($scenario in $mapped) {
    Check ($visualText -match [regex]::Escape($scenario)) "mapped scenario '$scenario' exists in the canonical visual catalog"
}

# --- 3. Injectable detection fixtures ---
$singleProbe = New-DisplayMatrixFixture `
    -Monitors @((New-DisplayMatrixMonitor -Id 'primary' -Primary $true)) `
    -SystemDpi 96 -PerMonitorDpi @(96) `
    -AppsUseLightTheme 1 -SystemUsesLightTheme 1 `
    -HighContrastFlags 0 -MixedDpi $false
$single = Get-HostDisplayFacts -Probe $singleProbe
Check ($single.TopologyObservable -and $single.MonitorCount -eq 1) 'single-monitor fixture detects one observable monitor'
Check ($single.SystemDpiObservable -and $single.SystemDpi -eq 96 -and $single.SystemDpiPercent -eq 100) 'fixture maps 96 device DPI to 100 percent'
Check ($single.ThemeObservable -and $single.ThemeName -eq 'Light') 'light-theme fixture resolves to Light'
Check ($single.HighContrastObservable -and -not $single.HighContrast) 'flags=0 fixture resolves to High Contrast off'
Check ($single.PrimaryResolution -eq '1920x1080') 'primary resolution is observed from monitor bounds'

$dualProbe = New-DisplayMatrixFixture `
    -Monitors @((New-DisplayMatrixMonitor -Id 'primary' -Primary $true), (New-DisplayMatrixMonitor -Id 'secondary' -X 1920)) `
    -SystemDpi 120 -PerMonitorDpi @(96, 144) `
    -AppsUseLightTheme 0 -SystemUsesLightTheme 0 `
    -HighContrastFlags 1 -MixedDpi $true
$dual = Get-HostDisplayFacts -Probe $dualProbe
Check ($dual.MonitorCount -eq 2) 'dual-monitor fixture detects two monitors'
Check ($dual.SystemDpiPercent -eq 125) 'fixture maps 120 device DPI to 125 percent'
Check ($dual.ThemeName -eq 'Dark') 'apps-use-light=0 fixture resolves to Dark'
Check ($dual.HighContrast -and $dual.HighContrastObservable) 'flags=1 fixture resolves to High Contrast on'
Check ($dual.MixedDpiSupported -and $dual.MixedDpiObservable) 'mixed-DPI fixture reports supported and observable'

# Forced-unobservable probe: unsupported is observed, never hardcoded.
$blindProbe = @{
    Monitors = $null; SystemDpi = $null; PerMonitorDpi = $null
    AppsUseLightTheme = $null; SystemUsesLightTheme = $null
    HighContrastFlags = $null; MixedDpi = $null
}
$blind = Get-HostDisplayFacts -Probe $blindProbe
Check ((-not $blind.TopologyObservable) -and (-not $blind.SystemDpiObservable) -and (-not $blind.ThemeObservable) -and (-not $blind.HighContrastObservable)) 'null probe forces every fact unobservable'

# --- 4. Status resolution: unsupported never PASS ---
function Get-DisplayMatrixResolution {
    param($Probe, [string] $CellId)
    $facts = Get-HostDisplayFacts -Probe $Probe
    $cell = @((Get-DisplayMatrixCells) | Where-Object { [string]$_.Id -eq $CellId }) | Select-Object -First 1
    return (Resolve-DisplayCellStatus -Cell $cell -Facts $facts)
}

$blindStatuses = @{}
foreach ($cell in $cells) {
    $resolution = Get-DisplayMatrixResolution -Probe $blindProbe -CellId ([string]$cell.Id)
    $blindStatuses[[string]$cell.Id] = [string]$resolution.Status
    Check ([string]$resolution.Status -eq 'HOST_UNSUPPORTED') "blind host resolves '$($cell.Id)' to HOST_UNSUPPORTED (observed=$([string]$resolution.Reason))"
    Check ([string]$resolution.Status -ne 'PASS') "blind host never PASSes '$($cell.Id)'"
    Check (-not [string]::IsNullOrWhiteSpace([string]$resolution.Reason)) "blind host '$($cell.Id)' carries a reason"
}
Check ((@($blindStatuses.Values) | Where-Object { $_ -eq 'PASS' }).Count -eq 0) 'unsupported is never folded into PASS on a blind host'

$singleMulti = Get-DisplayMatrixResolution -Probe $singleProbe -CellId 'multi-monitor'
Check ([string]$singleMulti.Status -eq 'HOST_UNSUPPORTED') 'single-monitor host cannot PASS the multi-monitor cell'
Check ([string]$singleMulti.Status -ne 'PASS') 'single-monitor host never PASSes the multi-monitor cell'

$singleMixed = Get-DisplayMatrixResolution -Probe $singleProbe -CellId 'mixed-dpi'
Check ([string]$singleMixed.Status -eq 'HOST_UNSUPPORTED') 'single-monitor host cannot PASS the mixed-DPI cell'

$singleDpi150 = Get-DisplayMatrixResolution -Probe $singleProbe -CellId 'dpi-150'
Check ([string]$singleDpi150.Status -eq 'NOT_RUN') 'inactive DPI scale defers as NOT_RUN, not HOST_UNSUPPORTED'
Check ([string]$singleDpi150.Reason -match 'user-session') 'deferred DPI reason names the user-session owner action'

$dualDpi125 = Get-DisplayMatrixResolution -Probe $dualProbe -CellId 'dpi-125'
Check ([string]$dualDpi125.Status -eq 'NOT_RUN') 'active DPI scale with satisfied prerequisites still defers as NOT_RUN (owner-sequenced live execution)'
Check ([string]$dualDpi125.Reason -match 'owner-sequenced') 'deferred reason names owner sequencing'

$dualMulti = Get-DisplayMatrixResolution -Probe $dualProbe -CellId 'multi-monitor'
Check ([string]$dualMulti.Status -eq 'NOT_RUN') 'satisfied multi-monitor prerequisites defer as NOT_RUN without -Execute'

$hcOff = Get-DisplayMatrixResolution -Probe $singleProbe -CellId 'high-contrast'
Check ([string]$hcOff.Status -eq 'NOT_RUN') 'High Contrast off defers as NOT_RUN (enabling it is a user-session action)'
Check ([string]$hcOff.Reason -match 'user-session') 'High Contrast deferral reason names the user-session action'

$hcOnProbe = New-DisplayMatrixFixture `
    -Monitors @((New-DisplayMatrixMonitor -Id 'primary' -Primary $true)) `
    -SystemDpi 96 -PerMonitorDpi @(96) `
    -HighContrastFlags 1 -MixedDpi $false
$hcOn = Get-DisplayMatrixResolution -Probe $hcOnProbe -CellId 'high-contrast'
Check ([string]$hcOn.Status -eq 'NOT_RUN') 'High Contrast on still defers as NOT_RUN without -Execute (default mode never PASSes)'

# Default mode NEVER reports PASS or FAIL: every fixture outcome is
# NOT_RUN or HOST_UNSUPPORTED.
$noPassNoFail = $true
foreach ($fixture in @($singleProbe, $dualProbe, $blindProbe, $hcOnProbe)) {
    $facts = Get-HostDisplayFacts -Probe $fixture
    foreach ($cell in $cells) {
        $status = [string](Resolve-DisplayCellStatus -Cell $cell -Facts $facts).Status
        if ($status -notin @('NOT_RUN', 'HOST_UNSUPPORTED')) { $noPassNoFail = $false }
    }
}
Check $noPassNoFail 'default detection resolves every cell to NOT_RUN or HOST_UNSUPPORTED only (never PASS/FAIL)'

# Every status the resolver can emit is inside the fixed vocabulary.
$vocabularySet = @('PASS', 'FAIL', 'HOST_UNSUPPORTED', 'NOT_RUN')
$emitted = @{}
foreach ($fixture in @($singleProbe, $dualProbe, $blindProbe)) {
    $facts = Get-HostDisplayFacts -Probe $fixture
    foreach ($cell in $cells) {
        $emitted[[string](Resolve-DisplayCellStatus -Cell $cell -Facts $facts).Status] = $true
    }
}
foreach ($status in @($emitted.Keys | Sort-Object)) {
    Check ($vocabularySet -contains $status) "emitted status '$status' is inside the fixed vocabulary"
}

# --- 5. Reuse points: the orchestrator consumes, never duplicates ---
foreach ($reuse in @(
        @{ Pattern = 'HostDisplayCapabilities\.psm1'; Label = 'detector module consumed' },
        @{ Pattern = 'Invoke-UiVisualValidationCapability'; Label = 'canonical visual capability delegation for -Execute' },
        @{ Pattern = 'Test-UiVisualValidationAvailability'; Label = 'interactive-context preflight for -Execute' },
        @{ Pattern = 'Test-VisualDpiMatch'; Label = 'requested-vs-observed DPI gate for dpi-* cells' },
        @{ Pattern = 'Get-VisualGitState|Get-DisplayMatrixGitHead'; Label = 'headSha provenance gate' },
        @{ Pattern = 'Capture-VisualWindow'; Label = 'DWM capture path for transition cells' },
        @{ Pattern = 'Set-UiaWindowPosition|ShowWindow'; Label = 'Win32 window-state actuation (no duplicated geometry code)' }
    )) {
    Check ($orchestratorText -match $reuse.Pattern) ("reuse point: " + $reuse.Label)
}

# --- 6. Evidence shape: detection-only -Execute-less matrix parses with headSha ---
$tempDir = Join-Path ([IO.Path]::GetTempPath()) ('ffdisplay-harness-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempDir -Force | Out-Null
try {
    $evidenceRoot = Join-Path $tempDir 'display-matrix-test'
    & $orchestratorPath -RepoRoot ([string]$repoRoot) -OutputRoot $evidenceRoot -Probe $singleProbe | Out-Null
    $matrixPath = Join-Path $evidenceRoot 'matrix.json'
    Check (Test-Path -LiteralPath $matrixPath -PathType Leaf) 'detection-only run writes matrix.json evidence'
    $matrix = Get-Content -LiteralPath $matrixPath -Raw | ConvertFrom-Json
    Check ([string]$matrix.schema -eq 'fastfiles.display-matrix.v1') 'evidence schema is fastfiles.display-matrix.v1'
    Check ([string]$matrix.headSha -match '^[0-9a-f]{40}$|^unbound$') 'evidence parses with a headSha (40-hex or unbound)'
    Check ([string]$matrix.mode -eq 'detection-only') 'evidence mode is detection-only without -Execute'
    Check ([int]$matrix.requestedCount -eq $cells.Count) 'evidence covers every requested cell'
    Check ([int]$matrix.executedCount -eq 0) 'detection-only evidence executes zero cells'
    $evidenceStatuses = @($matrix.cells | ForEach-Object { [string]$_.status })
    Check ((@($evidenceStatuses | Where-Object { $_ -notin @('NOT_RUN', 'HOST_UNSUPPORTED') })).Count -eq 0) 'evidence cells are NOT_RUN/HOST_UNSUPPORTED only'
    Check ((@($matrix.cells | ForEach-Object { [string]$_.id } | ForEach-Object { $_ } | Sort-Object -Unique)).Count -eq $cells.Count) 'evidence lists every requested cell exactly once'
    foreach ($row in @($matrix.cells)) {
        if ([string]::IsNullOrWhiteSpace([string]$row.reason) -or [string]::IsNullOrWhiteSpace([string]$row.evidenceRef)) {
            Check $false "evidence cell '$($row.id)' carries {reason, evidenceRef}"
        }
    }
    Check $true 'every evidence cell carries {reason, evidenceRef}'
    $subsetRoot = Join-Path $tempDir 'display-matrix-subset'
    & $orchestratorPath -RepoRoot ([string]$repoRoot) -OutputRoot $subsetRoot -Probe $singleProbe -CellIds @('dpi-100', 'multi-monitor') | Out-Null
    $subset = Get-Content -LiteralPath (Join-Path $subsetRoot 'matrix.json') -Raw | ConvertFrom-Json
    Check ([int]$subset.requestedCount -eq 2) 'CellIds subset requests exactly two cells'
    $unknownRejected = $false
    try {
        & $orchestratorPath -RepoRoot ([string]$repoRoot) -OutputRoot (Join-Path $tempDir 'display-matrix-bad') -Probe $singleProbe -CellIds @('no-such-cell') | Out-Null
    } catch {
        $unknownRejected = $_.Exception.Message -like '*unknown display cell id*'
    }
    Check $unknownRejected 'unknown cell ids throw instead of silently narrowing the matrix'
    $summaryPath = Join-Path $evidenceRoot 'summary.md'
    $factsPath = Join-Path $evidenceRoot 'host-facts.json'
    Check (Test-Path -LiteralPath $summaryPath -PathType Leaf) 'detection-only run writes summary.md'
    Check (Test-Path -LiteralPath $factsPath -PathType Leaf) 'detection-only run writes host-facts.json'
} finally {
    if (Test-Path -LiteralPath $tempDir) {
        Remove-Item -LiteralPath $tempDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

if ($failures -gt 0) {
    Write-Host "RESULT: $failures check(s) FAILED"
    exit 1
}
Write-Host 'RESULT: all display-matrix harness checks passed'
exit 0
