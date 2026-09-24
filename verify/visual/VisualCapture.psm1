<#
    Deterministic visual evidence primitives for the Windows UI validation
    capabilities.

    The image path is deliberately narrow: the live top-level window's DWM
    extended frame bounds are copied from the screen. PrintWindow is not a
    capture substitute. A screenshot is only produced after the caller has
    completed its semantic UIA post-condition.

    v2: Emits schema visual-evidence-v2 (schemaVersion 2) with full provenance.
    v1 manifest field `schema = 'fastfiles.visual-capture.v1'` is retained for
    backward compat; the v2 envelope is written alongside as `manifest-v2.json`
    and each capture record now carries v2-augmented fields.
#>

Set-StrictMode -Version 3.0

if (-not ('FfVisualCaptureNative' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class FfVisualCaptureNative
{
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT
    {
        public int left;
        public int top;
        public int right;
        public int bottom;
    }

    [DllImport("dwmapi.dll")]
    public static extern int DwmGetWindowAttribute(
        IntPtr hwnd, uint attribute, out RECT value, uint valueSize);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool GetWindowRect(IntPtr hwnd, out RECT value);

    [DllImport("user32.dll")]
    public static extern uint GetDpiForWindow(IntPtr hwnd);

    [DllImport("user32.dll", EntryPoint = "SendMessageW")]
    public static extern IntPtr SendMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);

    public const uint DWMWA_EXTENDED_FRAME_BOUNDS = 9;
}
'@
}

function Get-VisualRelativePath {
    param([Parameter(Mandatory)][string] $Root, [Parameter(Mandatory)][string] $Path)
    return ([System.IO.Path]::GetRelativePath($Root, $Path) -replace '\\', '/')
}

function Invoke-VisualGitCommand {
    param(
        [Parameter(Mandatory)][string] $RepoRoot,
        [Parameter(Mandatory)][string] $GitExecutable,
        [Parameter(Mandatory)][string[]] $Arguments
    )
    try {
        $output = @(& $GitExecutable -C $RepoRoot @Arguments 2>$null | ForEach-Object { $_.ToString() })
        return [pscustomobject]@{
            Succeeded = ($LASTEXITCODE -eq 0)
            Output = $output
            ExitCode = $LASTEXITCODE
        }
    } catch {
        return [pscustomobject]@{
            Succeeded = $false
            Output = @()
            ExitCode = -1
        }
    }
}

function Get-VisualGitState {
    param(
        [string] $RepoRoot,
        [string] $GitExecutable = 'git'
    )
    $unbound = {
        param([string] $Reason)
        return [pscustomobject]@{
            Commit = $null
            Tree = $null
            Branch = '<unbound>'
            WorktreeState = 'unbound'
            ProvenanceState = 'unbound-development'
            ExactTreeBound = $false
            SourceKind = 'unbound'
            Reason = $Reason
        }
    }
    try {
        $resolved = if ($RepoRoot) {
            (Resolve-Path -LiteralPath $RepoRoot -ErrorAction Stop).Path
        } else {
            (Resolve-Path (Join-Path $PSScriptRoot '..\..') -ErrorAction Stop).Path
        }
        if (-not (Test-Path -LiteralPath $resolved -PathType Container)) {
            return & $unbound 'repository-root-not-found'
        }
    } catch {
        return & $unbound 'repository-root-not-found'
    }

    $headResult = Invoke-VisualGitCommand -RepoRoot $resolved -GitExecutable $GitExecutable -Arguments @('rev-parse', 'HEAD')
    $treeResult = Invoke-VisualGitCommand -RepoRoot $resolved -GitExecutable $GitExecutable -Arguments @('rev-parse', 'HEAD^{tree}')
    $head = (@($headResult.Output) -join '').Trim().ToLowerInvariant()
    $tree = (@($treeResult.Output) -join '').Trim().ToLowerInvariant()
    if (-not $headResult.Succeeded -or $head -notmatch '^[0-9a-f]{40}$') {
        return & $unbound 'git-head-unavailable-or-malformed'
    }
    if (-not $treeResult.Succeeded -or $tree -notmatch '^[0-9a-f]{40}$') {
        return & $unbound 'git-tree-unavailable-or-malformed'
    }

    $statusResult = Invoke-VisualGitCommand -RepoRoot $resolved -GitExecutable $GitExecutable `
        -Arguments @('status', '--porcelain=v1', '--untracked-files=all')
    if (-not $statusResult.Succeeded) {
        return & $unbound 'git-status-unavailable'
    }
    $branchResult = Invoke-VisualGitCommand -RepoRoot $resolved -GitExecutable $GitExecutable `
        -Arguments @('symbolic-ref', '--quiet', '--short', 'HEAD')
    $branch = if ($branchResult.Succeeded -and @($branchResult.Output).Count -gt 0) {
        (@($branchResult.Output) -join '').Trim()
    } else {
        '<detached>'
    }
    $dirty = @($statusResult.Output).Count -gt 0
    $detached = $branch -eq '<detached>'
    return [pscustomobject]@{
        Commit = $head
        Tree = $tree
        Branch = $branch
        WorktreeState = if ($dirty) { 'dirty' } else { 'clean' }
        ProvenanceState = if ($dirty) { 'dirty-development' } else { 'exact-committed-tree' }
        ExactTreeBound = -not $dirty
        SourceKind = if ($detached) { 'detached-git-commit' } else { 'git-commit' }
        Reason = $null
    }
}

function Get-VisualGitCommit {
    param([string] $RepoRoot, [string] $GitExecutable = 'git')
    return (Get-VisualGitState -RepoRoot $RepoRoot -GitExecutable $GitExecutable).Commit
}

function Get-VisualGitTree {
    param([string] $RepoRoot, [string] $GitExecutable = 'git')
    return (Get-VisualGitState -RepoRoot $RepoRoot -GitExecutable $GitExecutable).Tree
}

function Get-VisualWindowBounds {
    param([Parameter(Mandatory)] $Element)
    $hwnd = [IntPtr]$Element.Current.NativeWindowHandle
    if ($hwnd -eq [IntPtr]::Zero) { throw 'visual window has no native HWND' }

    $dwmRect = [FfVisualCaptureNative+RECT]::new()
    $dwmHr = [FfVisualCaptureNative]::DwmGetWindowAttribute(
        $hwnd, [FfVisualCaptureNative]::DWMWA_EXTENDED_FRAME_BOUNDS,
        [ref]$dwmRect, [uint32][Runtime.InteropServices.Marshal]::SizeOf($dwmRect))
    if ($dwmHr -ne 0) {
        throw "DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS) failed: HRESULT 0x$('{0:X8}' -f ([uint32]$dwmHr))"
    }

    $width = $dwmRect.right - $dwmRect.left
    $height = $dwmRect.bottom - $dwmRect.top
    if ($width -le 0 -or $height -le 0) { throw 'visual window has an empty DWM frame bounds rectangle' }
    $dpi = [FfVisualCaptureNative]::GetDpiForWindow($hwnd)
    return [pscustomobject]@{
        Hwnd = $hwnd
        X = $dwmRect.left
        Y = $dwmRect.top
        Width = $width
        Height = $height
        Dpi = $(if ($dpi -gt 0) { [int]$dpi } else { $null })
        Source = 'DwmExtendedFrameBounds'
    }
}

# Compares the requested scenario scale (percent, 100 = 96 DPI) against the
# observed GetDpiForWindow device DPI. A mismatch never fails the capture; it
# records that the nominal environment was not achieved so baselines cannot
# silently claim untested DPI coverage.
function Test-VisualDpiMatch {
    param(
        [int] $RequestedPercent = 100,
        [int] $ObservedDpi = 0
    )
    $expectedDpi = if ($RequestedPercent -gt 0) { [int][Math]::Round($RequestedPercent * 96.0 / 100.0) } else { 96 }
    if ($ObservedDpi -le 0) {
        return [pscustomobject]@{ Match = $false; ExpectedDpi = $expectedDpi; ObservedDpi = $ObservedDpi; Known = $false }
    }
    return [pscustomobject]@{
        Match = ($ObservedDpi -eq $expectedDpi)
        ExpectedDpi = $expectedDpi
        ObservedDpi = $ObservedDpi
        Known = $true
    }
}

function New-VisualCaptureRun {
    param(
        [Parameter(Mandatory)][string] $Root,
        [Parameter(Mandatory)][string] $ApplicationSha,
        [string] $HostId = 'local-interactive-windows',
        [string] $RepoRoot = '',
        [string] $ExecutableIdentity = '',
        [string] $GitExecutable = 'git'
    )
    New-Item -ItemType Directory -Path $Root -Force | Out-Null
    $resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
    $repoRoot = if ($RepoRoot) { $RepoRoot } else { (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path }
    $gitState = Get-VisualGitState -RepoRoot $repoRoot -GitExecutable $GitExecutable
    $exeId = if ($ExecutableIdentity) { $ExecutableIdentity } else { $ApplicationSha }
    return [pscustomobject]@{
        Root = $resolvedRoot
        RepoRoot = $repoRoot
        ApplicationSha = $ApplicationSha
        ExecutableIdentity = $exeId
        Commit = $gitState.Commit
        Tree = $gitState.Tree
        Branch = $gitState.Branch
        WorktreeState = $gitState.WorktreeState
        ProvenanceState = $gitState.ProvenanceState
        ExactTreeBound = $gitState.ExactTreeBound
        SourceKind = $gitState.SourceKind
        ProvenanceReason = $gitState.Reason
        HostId = $HostId
        StartedUtc = [DateTime]::UtcNow
        Records = [System.Collections.ArrayList]::new()
        SeenHashes = @{}
    }
}

function New-VisualEvidenceV2Record {
    param(
        [Parameter(Mandatory)] $Run,
        [Parameter(Mandatory)][string] $Surface,
        [string] $SemanticState = '',
        [Parameter(Mandatory)][ValidateSet('light','dark','system','highContrast')][string] $Theme,
        [ValidateSet('compact','comfortable')][string] $Density = 'comfortable',
        [Parameter(Mandatory)][ValidateRange(1, 1000)][int] $Dpi,
        [Parameter(Mandatory)][ValidateRange(1, 100000)][int] $ViewportWidth,
        [Parameter(Mandatory)][ValidateRange(1, 100000)][int] $ViewportHeight,
        [ValidateSet('single','dual','dialog')][string] $PaneMode = 'single',
        [string] $FixtureId = '',
        [string] $CaptureMethod = 'DwmExtendedFrameBounds+CopyFromScreen',
        [object[]] $PrivacyRedactions = @(),
        [Parameter(Mandatory)][string] $ScreenshotHash,
        [double] $DpiScalePercent = 0,
        [string] $BaselineIdentity = $null,
        $DiffResult = $null,
        $CriticResult = $null,
        [object] $ArtifactProvenance = $null
    )
    if ($ScreenshotHash -notmatch '^[0-9a-f]{64}$') {
        throw 'screenshotHash must be a lowercase 64-character SHA-256'
    }
    if ([string]::IsNullOrWhiteSpace($Surface)) { throw 'surface is required' }
    if ([string]::IsNullOrWhiteSpace($CaptureMethod)) { throw 'captureMethod is required' }

    $commit = if ($Run.PSObject.Properties['Commit'] -and $Run.Commit -match '^[0-9a-f]{40}$') {
        $Run.Commit.ToLowerInvariant()
    } else { $null }
    $tree = if ($Run.PSObject.Properties['Tree'] -and $Run.Tree -match '^[0-9a-f]{40}$') {
        $Run.Tree.ToLowerInvariant()
    } else { $null }
    $provenanceState = if ($Run.PSObject.Properties['ProvenanceState'] -and $Run.ProvenanceState) {
        [string]$Run.ProvenanceState
    } else {
        if ($commit -and $tree) { 'dirty-development' } else { 'unbound-development' }
    }
    $worktreeState = if ($Run.PSObject.Properties['WorktreeState'] -and $Run.WorktreeState) {
        [string]$Run.WorktreeState
    } else {
        if ($provenanceState -eq 'exact-committed-tree') { 'clean' } elseif ($provenanceState -eq 'dirty-development') { 'dirty' } else { 'unbound' }
    }
    $exactTreeBound = if ($Run.PSObject.Properties['ExactTreeBound']) {
        [bool]$Run.ExactTreeBound
    } else { $provenanceState -eq 'exact-committed-tree' }
    if ($exactTreeBound -and (-not $commit -or -not $tree -or $worktreeState -ne 'clean')) {
        throw 'exactTreeBound evidence requires valid commit/tree and a clean worktree'
    }
    if ($provenanceState -notin @('exact-committed-tree','dirty-development','unbound-development')) {
        throw "unsupported provenanceState '$provenanceState'"
    }
    if ($worktreeState -notin @('clean','dirty','unbound')) {
        throw "unsupported worktreeState '$worktreeState'"
    }

    $provenance = $ArtifactProvenance
    if ($null -eq $provenance -or [string]::IsNullOrWhiteSpace([string]$provenance)) {
        $provenance = [ordered]@{
            source = 'local-capture'
            scenarioId = $Surface
            captureMethod = $CaptureMethod
            screenshotHash = $ScreenshotHash
        }
    } elseif ($provenance -is [string]) {
        $provenance = [ordered]@{
            source = [string]$provenance
            scenarioId = $Surface
            captureMethod = $CaptureMethod
            screenshotHash = $ScreenshotHash
        }
    }
    return [ordered]@{
        schemaVersion = 2
        commit = $commit
        tree = $tree
        executableIdentity = if ($Run.PSObject.Properties['ExecutableIdentity']) { [string]$Run.ExecutableIdentity } else { '' }
        branch = if ($Run.PSObject.Properties['Branch']) { [string]$Run.Branch } else { '<unknown>' }
        sourceKind = if ($Run.PSObject.Properties['SourceKind']) { [string]$Run.SourceKind } else { 'unbound' }
        worktreeState = $worktreeState
        provenanceState = $provenanceState
        exactTreeBound = $exactTreeBound
        captureTimestamp = [DateTime]::UtcNow.ToString('o')
        surface = $Surface
        semanticState = $SemanticState
        theme = $Theme
        density = $Density
        dpi = $Dpi
        dpiScalePercent = if ($DpiScalePercent -gt 0) { $DpiScalePercent } else { [Math]::Round($Dpi * 100.0 / 96.0, 2) }
        viewport = [ordered]@{ width = $ViewportWidth; height = $ViewportHeight }
        paneMode = $PaneMode
        fixtureId = $FixtureId
        captureMethod = $CaptureMethod
        privacyRedactions = @($PrivacyRedactions)
        screenshotHash = $ScreenshotHash
        baselineIdentity = $BaselineIdentity
        diffResult = $DiffResult
        criticResult = $CriticResult
        artifactProvenance = $provenance
    }
}

function Test-VisualEvidenceV2Record {
    param([Parameter(Mandatory)] $Record)
    $errors = [System.Collections.Generic.List[string]]::new()
    $schemaPath = Join-Path $PSScriptRoot 'schemas\visual-evidence-v2.schema.json'
    $schema = try { Get-Content -LiteralPath $schemaPath -Raw | ConvertFrom-Json } catch { $null }
    if ($null -eq $schema) {
        [void]$errors.Add('visual-evidence-v2 schema could not be loaded')
        return [pscustomobject]@{ Valid = $false; Errors = @($errors) }
    }
    $getProperty = {
        param($obj, [string]$name)
        if ($null -eq $obj) { return $null }
        if ($obj -is [System.Collections.IDictionary]) {
            if ($obj.Contains($name)) { return $obj[$name] }
            return $null
        }
        $property = $obj.PSObject.Properties[$name]
        if ($null -ne $property) { return $property.Value }
        return $null
    }
    $hasProperty = {
        param($obj, [string]$name)
        if ($null -eq $obj) { return $false }
        if ($obj -is [System.Collections.IDictionary]) { return $obj.Contains($name) }
        return $null -ne $obj.PSObject.Properties[$name]
    }
    $properties = @($schema.properties.PSObject.Properties.Name)
    foreach ($required in @($schema.required)) {
        if (-not (& $hasProperty $Record ([string]$required))) {
            [void]$errors.Add("$required is required")
        }
    }
    $recordProperties = if ($Record -is [System.Collections.IDictionary]) { @($Record.Keys) } else { @($Record.PSObject.Properties.Name) }
    foreach ($property in $recordProperties) {
        if ($property -notin $properties) { [void]$errors.Add("unknown property '$property'") }
    }
    $sv = & $getProperty $Record 'schemaVersion'
    if ($null -eq $sv -or [int]$sv -ne 2) { [void]$errors.Add('schemaVersion must be 2') }

    foreach ($hashProperty in @('commit','tree')) {
        $hash = & $getProperty $Record $hashProperty
        if ($null -ne $hash -and ([string]$hash -notmatch '^[0-9a-f]{40}$' -or ([string]$hash -match '^0+$'))) {
            [void]$errors.Add("$hashProperty must be null or a non-zero 40-character lowercase hex SHA")
        }
    }
    $timestamp = & $getProperty $Record 'captureTimestamp'
    if ($timestamp) {
        try { $null = [DateTime]::Parse([string]$timestamp, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind) }
        catch { [void]$errors.Add('captureTimestamp must be ISO8601 date-time') }
    }
    foreach ($stringProperty in @('surface','semanticState','executableIdentity','branch','fixtureId','captureMethod')) {
        $value = & $getProperty $Record $stringProperty
        if ($null -eq $value -or $value -isnot [string]) { [void]$errors.Add("$stringProperty must be a string") }
    }
    foreach ($enumProperty in @('theme','density','paneMode','sourceKind','worktreeState','provenanceState')) {
        $value = [string](& $getProperty $Record $enumProperty)
        $schemaProperty = $schema.properties.PSObject.Properties[$enumProperty].Value
        $allowed = @($schemaProperty.enum | ForEach-Object { [string]$_ })
        if ($value -notin $allowed) { [void]$errors.Add("$enumProperty must be one of $($allowed -join ',')") }
    }
    foreach ($numericProperty in @('dpi','dpiScalePercent')) {
        $value = & $getProperty $Record $numericProperty
        # Bounds mirror the JSON schema (maximum: 1000); dpi must be whole.
        if ($null -eq $value -or [double]$value -le 0 -or [double]$value -gt 1000) {
            [void]$errors.Add("$numericProperty must be in (0, 1000]")
        } elseif ($numericProperty -eq 'dpi' -and [double]$value -ne [Math]::Truncate([double]$value)) {
            [void]$errors.Add('dpi must be an integer device DPI')
        }
    }
    $viewport = & $getProperty $Record 'viewport'
    foreach ($dimension in @('width','height')) {
        $value = & $getProperty $viewport $dimension
        if ($null -eq $value -or [int64]$value -le 0 -or [int64]$value -gt 100000) {
            [void]$errors.Add("viewport.$dimension must be in (0, 100000]")
        }
    }
    $screenshotHash = [string](& $getProperty $Record 'screenshotHash')
    if ($screenshotHash -notmatch '^[0-9a-f]{64}$' -or $screenshotHash -match '^0+$') {
        [void]$errors.Add('screenshotHash must be a non-zero lowercase 64-character SHA')
    }
    $redactions = & $getProperty $Record 'privacyRedactions'
    if ($null -eq $redactions) {
        # An empty JSON array is deserialized as no pipeline output in Windows
        # PowerShell; null is therefore the canonical empty-array representation.
    } elseif ($redactions -isnot [System.Collections.IEnumerable] -or $redactions -is [string]) {
        [void]$errors.Add('privacyRedactions must be an array')
    } else {
        foreach ($redaction in @($redactions)) {
            foreach ($dimension in @('x','y','width','height')) {
                $value = & $getProperty $redaction $dimension
                if ($null -eq $value -or [int64]$value -lt 0 -or ($dimension -in @('width','height') -and [int64]$value -le 0)) {
                    [void]$errors.Add("privacyRedactions.$dimension is invalid")
                }
            }
            $reason = & $getProperty $redaction 'reason'
            if ($null -eq $reason -or [string]::IsNullOrWhiteSpace([string]$reason)) { [void]$errors.Add('privacyRedactions.reason is required') }
        }
    }
    $provenance = & $getProperty $Record 'artifactProvenance'
    if ($null -eq $provenance -or $provenance -is [string]) {
        [void]$errors.Add('artifactProvenance must be an object')
    } else {
        foreach ($property in @('source','scenarioId','captureMethod','screenshotHash')) {
            $value = & $getProperty $provenance $property
            if ($null -eq $value -or [string]::IsNullOrWhiteSpace([string]$value)) { [void]$errors.Add("artifactProvenance.$property is required") }
        }
        $provenanceHash = [string](& $getProperty $provenance 'screenshotHash')
        if ($provenanceHash -ne $screenshotHash) { [void]$errors.Add('artifactProvenance.screenshotHash must match screenshotHash') }
    }
    $exact = [bool](& $getProperty $Record 'exactTreeBound')
    if ($exact) {
        if ($null -eq (& $getProperty $Record 'commit') -or $null -eq (& $getProperty $Record 'tree')) {
            [void]$errors.Add('exactTreeBound requires commit and tree')
        }
        if ([string](& $getProperty $Record 'worktreeState') -ne 'clean') {
            [void]$errors.Add('exactTreeBound requires worktreeState=clean')
        }
    }
    return [pscustomobject]@{ Valid = ($errors.Count -eq 0); Errors = @($errors) }
}
function Add-VisualScenarioResult {
    param(
        [Parameter(Mandatory)] $Run,
        [Parameter(Mandatory)][string] $ScenarioId,
        [Parameter(Mandatory)][string] $ExpectedState,
        [Parameter(Mandatory)][string] $FixtureId,
        [Parameter(Mandatory)][string] $Theme,
        [Parameter(Mandatory)][ValidateRange(1, 100000)][int] $Width,
        [Parameter(Mandatory)][ValidateSet('single','dual','dialog')][string] $PaneMode,
        [int] $DpiScale = 0,
        [int] $DpiScalePercent = 0,
        [int] $ObservedDpi = 0,
        [Parameter(Mandatory)][ValidateSet('PASS', 'FAIL', 'SKIPPED', 'BLOCKED_EXTERNAL', 'BLOCKED_ENVIRONMENT')][string] $Status,
        [string] $Reason,
        [bool] $AssertionPass = $false,
        $AssertionEvidence = $null,
        [string] $ScreenshotPath,
        [string] $ImageHash,
        [string] $DuplicateOf,
        [string] $DuplicateReason,
        [object[]] $PrivacyRedactions = @(),
        [ValidateSet('compact','comfortable')][string] $Density = 'comfortable',
        [ValidateRange(1, 100000)][int] $ViewportHeight = 800,
        [string] $BaselineIdentity = $null,
        $DiffResult = $null,
        $CriticResult = $null
    )
    $requestedDpiScale = if ($DpiScalePercent -gt 0) { $DpiScalePercent } else { $DpiScale }
    $hasScreenshotPath = -not [string]::IsNullOrWhiteSpace($ScreenshotPath)
    $hasImageHash = -not [string]::IsNullOrWhiteSpace($ImageHash)
    if ($hasScreenshotPath -xor $hasImageHash) {
        throw "visual scenario '$ScenarioId' must declare both screenshotPath and imageHash, or neither"
    }
    if ($Status -eq 'PASS' -and (-not $hasScreenshotPath -or -not $AssertionPass)) {
        throw "visual scenario '$ScenarioId' cannot be PASS without a captured image and semantic assertion"
    }
    $relativeImage = $null
    if ($hasScreenshotPath) {
        if (-not (Test-Path -LiteralPath $ScreenshotPath -PathType Leaf)) {
            throw "visual scenario '$ScenarioId' screenshot does not exist: $ScreenshotPath"
        }
        $actualHash = (Get-FileHash -LiteralPath $ScreenshotPath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($ImageHash -notmatch '^[0-9a-f]{64}$' -or $ImageHash.ToLowerInvariant() -ne $actualHash) {
            throw "visual scenario '$ScenarioId' imageHash does not match the captured file"
        }
        if ($ObservedDpi -le 0) {
            throw "visual scenario '$ScenarioId' captured image is missing observed window DPI"
        }
        $relativeImage = Get-VisualRelativePath -Root $Run.Root -Path $ScreenshotPath
    }
    if ($null -eq $PrivacyRedactions) { $PrivacyRedactions = @() }
    $dpiCheck = Test-VisualDpiMatch -RequestedPercent $requestedDpiScale -ObservedDpi $ObservedDpi
    $dpiMatch = if ($hasScreenshotPath) { $dpiCheck.Match } else { $null }
    # Map legacy theme names to v2 enum.
    $v2Theme = switch ($Theme.ToLowerInvariant()) {
        'light' { 'light' }
        'dark' { 'dark' }
        'highcontrast' { 'highContrast' }
        'high_contrast' { 'highContrast' }
        default { 'system' }
    }
    $v2Record = $null
    if ($hasScreenshotPath) {
        $v2Record = New-VisualEvidenceV2Record -Run $Run -Surface $ScenarioId -SemanticState $ExpectedState `
            -Theme $v2Theme -Density $Density -Dpi $ObservedDpi -DpiScalePercent $requestedDpiScale `
            -ViewportWidth $Width -ViewportHeight $ViewportHeight `
            -PaneMode $PaneMode -FixtureId $FixtureId -PrivacyRedactions $PrivacyRedactions `
            -ScreenshotHash $ImageHash -BaselineIdentity $BaselineIdentity -DiffResult $DiffResult -CriticResult $CriticResult `
            -ArtifactProvenance ([ordered]@{
                source = 'local-dwm-capture'
                scenarioId = $ScenarioId
                captureMethod = 'DwmExtendedFrameBounds+CopyFromScreen'
                screenshotHash = $ImageHash
            })
    }
    $record = [ordered]@{
        scenarioId = $ScenarioId
        expectedState = $ExpectedState
        fixtureId = $FixtureId
        theme = $Theme
        density = $Density
        width = $Width
        viewportHeight = $ViewportHeight
        paneMode = $PaneMode
        dpiScale = $requestedDpiScale
        observedDpi = if ($hasScreenshotPath) { $ObservedDpi } else { $null }
        dpiExpected = if ($hasScreenshotPath) { $dpiCheck.ExpectedDpi } else { $null }
        dpiMatch = $dpiMatch
        applicationSha = $Run.ApplicationSha
        screenshotPath = $relativeImage
        imageHash = $ImageHash
        captureMethod = if ($hasScreenshotPath) { 'DwmExtendedFrameBounds+CopyFromScreen' } else { $null }
        privacyRedactions = $PrivacyRedactions
        provenanceState = $Run.ProvenanceState
        worktreeState = $Run.WorktreeState
        exactTreeBound = $Run.ExactTreeBound
        assertionResult = [ordered]@{
            pass = $AssertionPass
            evidence = $AssertionEvidence
        }
        timestampUtc = [DateTime]::UtcNow.ToString('o')
        status = $Status
        reason = $Reason
        duplicateOf = $DuplicateOf
        duplicateReason = $DuplicateReason
        v2 = $v2Record
    }
    [void]$Run.Records.Add([pscustomobject]$record)
    return $record
}

# Bounded retention for tool-owned visual run directories. Removes ONLY
# immediate child directories of $Root that (a) are older than $OlderThanDays
# and (b) contain a FastFiles visual manifest (manifest.json,
# manifest-v2.json, or closure.json carrying a fastfiles schema marker).
# Anything else — foreign files, unknown trees, fresh runs — is left alone.
# Raw captures default under gitignored verify/runs/; this keeps them bounded.
function Clear-VisualRunRetention {
    param(
        [Parameter(Mandatory)][string] $Root,
        [int] $OlderThanDays = 14
    )
    $removed = [System.Collections.Generic.List[string]]::new()
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) { return @($removed) }
    $cutoff = (Get-Date).AddDays(-$OlderThanDays)
    foreach ($child in @(Get-ChildItem -LiteralPath $Root -Directory -ErrorAction SilentlyContinue)) {
        if ($child.LastWriteTime -ge $cutoff) { continue }
        $owned = $false
        foreach ($manifestName in @('manifest.json','manifest-v2.json','closure.json')) {
            $manifestPath = Join-Path $child.FullName $manifestName
            if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { continue }
            try {
                $text = Get-Content -LiteralPath $manifestPath -Raw -ErrorAction Stop
                if ($text -match 'fastfiles\.visual|fastfiles\.closure') { $owned = $true; break }
            } catch { }
        }
        if (-not $owned) { continue }
        Remove-Item -LiteralPath $child.FullName -Recurse -Force -ErrorAction SilentlyContinue
        if (-not (Test-Path -LiteralPath $child.FullName)) { [void]$removed.Add($child.FullName) }
    }
    return @($removed)
}

function Register-VisualCaptureFile {
    param(
        [Parameter(Mandatory)] $Run,
        [Parameter(Mandatory)][string] $Path,
        [Parameter(Mandatory)][string] $ScenarioId,
        [string] $IntentionalDuplicateReason,
        [string] $ExpectedDuplicateOf
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "visual capture file does not exist: $Path"
    }
    $hash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($Run.SeenHashes.ContainsKey($hash)) {
        if ([string]::IsNullOrWhiteSpace($IntentionalDuplicateReason) -or
            [string]::IsNullOrWhiteSpace($ExpectedDuplicateOf)) {
            Remove-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
            throw "unexpected duplicate visual capture for '$ScenarioId'; hash already belongs to '$($Run.SeenHashes[$hash])'"
        }
        if ($Run.SeenHashes[$hash] -ne $ExpectedDuplicateOf) {
            Remove-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
            throw "intentional duplicate declaration for '$ScenarioId' expected '$ExpectedDuplicateOf' but hash belongs to '$($Run.SeenHashes[$hash])'"
        }
        $duplicateOf = $Run.SeenHashes[$hash]
    } else {
        $Run.SeenHashes[$hash] = $ScenarioId
        $duplicateOf = $null
    }
    return [pscustomobject]@{
        Path = $Path
        Hash = $hash
        DuplicateOf = $duplicateOf
        DuplicateReason = $IntentionalDuplicateReason
    }
}

function Capture-VisualWindow {
    param(
        [Parameter(Mandatory)] $Run,
        [Parameter(Mandatory)] $Element,
        [Parameter(Mandatory)][string] $ScenarioId,
        [Parameter(Mandatory)][bool] $AssertionPass,
        [string] $IntentionalDuplicateReason,
        [string] $ExpectedDuplicateOf,
        [ValidateSet('Light', 'Dark', 'Follow System')][string] $PrivacyTheme = 'Light',
        [object[]] $PrivacyRectangles = @()
    )
    if (-not $AssertionPass) {
        throw "visual capture '$ScenarioId' refused: semantic post-condition did not pass"
    }
    $bounds = Get-VisualWindowBounds -Element $Element
    Add-Type -AssemblyName System.Drawing
    $fileName = ($ScenarioId -replace '[^A-Za-z0-9._-]', '_') + '.png'
    $path = Join-Path $Run.Root $fileName
    $bitmap = [System.Drawing.Bitmap]::new($bounds.Width, $bounds.Height,
        [System.Drawing.Imaging.PixelFormat]::Format32bppPArgb)
    try {
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.CopyFromScreen($bounds.X, $bounds.Y, 0, 0,
                [System.Drawing.Size]::new($bounds.Width, $bounds.Height),
                [System.Drawing.CopyPixelOperation]::SourceCopy)
            $redactions = [System.Collections.Generic.List[object]]::new()
            $redactionColor = if ($PrivacyTheme -eq 'Light') {
                [System.Drawing.Color]::FromArgb(255, 255, 255, 255)
            } else {
                [System.Drawing.Color]::FromArgb(255, 31, 34, 36)
            }
            $redactionBrush = [System.Drawing.SolidBrush]::new($redactionColor)
            try {
                foreach ($sourceRect in @($PrivacyRectangles)) {
                    if ($null -eq $sourceRect) { continue }
                    $left = [Math]::Max(0, [int]$sourceRect.x - $bounds.X)
                    $top = [Math]::Max(0, [int]$sourceRect.y - $bounds.Y)
                    $right = [Math]::Min($bounds.Width, [int]($sourceRect.x + $sourceRect.width) - $bounds.X)
                    $bottom = [Math]::Min($bounds.Height, [int]($sourceRect.y + $sourceRect.height) - $bounds.Y)
                    if ($right -le $left -or $bottom -le $top) { continue }
                    $rect = [System.Drawing.Rectangle]::new($left, $top, $right - $left, $bottom - $top)
                    $graphics.FillRectangle($redactionBrush, $rect)
                    [void]$redactions.Add([ordered]@{
                        x = $left; y = $top; width = $rect.Width; height = $rect.Height
                        reason = 'host-user path redacted from committed visual evidence'
                    })
                }
            } finally {
                $redactionBrush.Dispose()
            }
        } finally {
            $graphics.Dispose()
        }
        $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $bitmap.Dispose()
    }

    $registered = Register-VisualCaptureFile -Run $Run -Path $path -ScenarioId $ScenarioId -IntentionalDuplicateReason $IntentionalDuplicateReason -ExpectedDuplicateOf $ExpectedDuplicateOf
    return [pscustomobject]@{
        Path = $registered.Path
        Hash = $registered.Hash
        Bounds = $bounds
        DuplicateOf = $registered.DuplicateOf
        DuplicateReason = $registered.DuplicateReason
        PrivacyRedactions = @($redactions)
    }
}

function Write-VisualCaptureRun {
    param([Parameter(Mandatory)] $Run)
    $finishedUtc = [DateTime]::UtcNow
    $manifest = [ordered]@{
        schema = 'fastfiles.visual-capture.v1'
        hostId = $Run.HostId
        startedUtc = $Run.StartedUtc.ToString('o')
        finishedUtc = $finishedUtc.ToString('o')
        applicationSha = $Run.ApplicationSha
        commit = $Run.Commit
        tree = $Run.Tree
        branch = $Run.Branch
        worktreeState = $Run.WorktreeState
        provenanceState = $Run.ProvenanceState
        exactTreeBound = $Run.ExactTreeBound
        sourceKind = $Run.SourceKind
        captures = @($Run.Records)
    }
    $manifestPath = Join-Path $Run.Root 'manifest.json'
    $summaryPath = Join-Path $Run.Root 'summary.md'
    $manifest | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $manifestPath -Encoding utf8

    # Validate every v2 record against the checked-in schema before publishing it.
    $v2Captures = [System.Collections.Generic.List[object]]::new()
    foreach ($rec in $Run.Records) {
        if ($null -eq $rec.v2) { continue }
        $validation = Test-VisualEvidenceV2Record -Record $rec.v2
        if (-not $validation.Valid) {
            throw "visual evidence v2 record '$($rec.scenarioId)' is invalid: $($validation.Errors -join '; ')"
        }
        [void]$v2Captures.Add($rec.v2)
    }
    $v2Manifest = [ordered]@{
        schemaVersion = 2
        schema = 'fastfiles.visual-evidence.v2'
        hostId = $Run.HostId
        startedUtc = $Run.StartedUtc.ToString('o')
        finishedUtc = $finishedUtc.ToString('o')
        applicationSha = $Run.ApplicationSha
        executableIdentity = $Run.ExecutableIdentity
        commit = $Run.Commit
        tree = $Run.Tree
        branch = $Run.Branch
        worktreeState = $Run.WorktreeState
        provenanceState = $Run.ProvenanceState
        exactTreeBound = $Run.ExactTreeBound
        sourceKind = $Run.SourceKind
        captures = @($Run.Records)
        v2Records = @($v2Captures)
    }
    $v2ManifestPath = Join-Path $Run.Root 'manifest-v2.json'
    $v2Manifest | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $v2ManifestPath -Encoding utf8

    $lines = [System.Collections.Generic.List[string]]::new()
    [void]$lines.Add('# FastFiles deterministic visual capture')
    [void]$lines.Add('')
    [void]$lines.Add("- Application SHA-256: $($Run.ApplicationSha)")
    [void]$lines.Add("- Commit: $(if ($Run.Commit) { $Run.Commit } else { 'unbound' })")
    [void]$lines.Add("- Tree: $(if ($Run.Tree) { $Run.Tree } else { 'unbound' })")
    [void]$lines.Add("- Provenance: $($Run.ProvenanceState) | worktree=$($Run.WorktreeState) | exactTreeBound=$($Run.ExactTreeBound)")
    [void]$lines.Add("- Started (UTC): $($Run.StartedUtc.ToString('o'))")
    [void]$lines.Add("- Finished (UTC): $($finishedUtc.ToString('o'))")
    [void]$lines.Add('')
    [void]$lines.Add('| Scenario | Theme | Width | Pane | DPI | Status | Assertion | Image | Reason |')
    [void]$lines.Add('| --- | --- | ---: | --- | ---: | --- | --- | --- | --- |')
    foreach ($item in $Run.Records) {
        $image = if ($item.screenshotPath) { $item.screenshotPath } else { '' }
        $reason = if ($item.reason) { $item.reason -replace '\|', '\\|' } else { '' }
        $assertion = if ($item.assertionResult.pass) { 'PASS' } else { 'FAIL' }
        $observedDpi = if ($null -ne $item.observedDpi) { $item.observedDpi } else { '' }
        [void]$lines.Add("| $($item.scenarioId) | $($item.theme) | $($item.width) | $($item.paneMode) | $observedDpi | $($item.status) | $assertion | $image | $reason |")
    }
    $lines | Set-Content -LiteralPath $summaryPath -Encoding utf8
    return [pscustomobject]@{ Manifest = $manifestPath; ManifestV2 = $v2ManifestPath; Summary = $summaryPath; Count = $Run.Records.Count; ProvenanceState = $Run.ProvenanceState }
}

function Compare-VisualScenarioCoverage {
    [OutputType([pscustomobject])]
    param(
        [Parameter(Mandatory)] [string[]] $ExpectedScenarioIds,
        [object[]] $Records,
        [string[]] $AllowedDiagnosticIds = @('catalog-lifecycle', 'gallery-error')
    )

    $expected = @($ExpectedScenarioIds | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Sort-Object -Unique)
    $records = @($Records)
    $ids = @($records | ForEach-Object { [string]$_.scenarioId } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    $counts = @{}
    foreach ($id in $ids) {
        if ($counts.ContainsKey($id)) { $counts[$id] = [int]$counts[$id] + 1 } else { $counts[$id] = 1 }
    }
    $missing = @($expected | Where-Object { -not $counts.ContainsKey($_) })
    $duplicates = @($counts.Keys | Where-Object { [int]$counts[$_] -gt 1 } | Sort-Object)
    $allowed = @($AllowedDiagnosticIds)
    $unexpected = @($ids | Where-Object { ($expected -notcontains $_) -and ($allowed -notcontains $_) } | Sort-Object -Unique)
    $expectedRecords = @($records | Where-Object { $expected -contains [string]$_.scenarioId })
    $nonPass = @($expectedRecords | Where-Object { [string]$_.status -ne 'PASS' } | ForEach-Object { [string]$_.scenarioId } | Sort-Object -Unique)
    $invalidPass = @($expectedRecords | Where-Object {
        [string]$_.status -eq 'PASS' -and (
            $null -eq $_.assertionResult -or -not [bool]$_.assertionResult.pass -or
            [string]::IsNullOrWhiteSpace([string]$_.screenshotPath) -or
            [string]::IsNullOrWhiteSpace([string]$_.imageHash))
    } | ForEach-Object { [string]$_.scenarioId } | Sort-Object -Unique)
    return [pscustomobject]@{
        Ok = ($missing.Count -eq 0 -and $duplicates.Count -eq 0 -and $unexpected.Count -eq 0 -and $nonPass.Count -eq 0 -and $invalidPass.Count -eq 0)
        Expected = $expected
        Missing = $missing
        Duplicates = $duplicates
        Unexpected = $unexpected
        NonPass = $nonPass
        InvalidPass = $invalidPass
        Recorded = @($ids | Sort-Object -Unique)
    }
}

Export-ModuleMember -Function Get-VisualWindowBounds, Get-VisualGitCommit, Get-VisualGitTree,
    New-VisualCaptureRun, New-VisualEvidenceV2Record, Test-VisualEvidenceV2Record,
    Add-VisualScenarioResult, Register-VisualCaptureFile, Capture-VisualWindow,
    Write-VisualCaptureRun, Test-VisualDpiMatch, Clear-VisualRunRetention,
    Compare-VisualScenarioCoverage
