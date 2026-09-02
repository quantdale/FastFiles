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

function Get-VisualGitCommit {
    param([string] $RepoRoot)
    try {
        $resolved = if ($RepoRoot) { $RepoRoot } else { (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path }
        $commit = (& git -C $resolved rev-parse HEAD 2>$null)
        if ($LASTEXITCODE -eq 0 -and $commit -match '^[0-9a-f]{40}$') { return $commit.Trim().ToLowerInvariant() }
    } catch { }
    return $null
}

function Get-VisualGitTree {
    param([string] $RepoRoot)
    try {
        $resolved = if ($RepoRoot) { $RepoRoot } else { (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path }
        $tree = (& git -C $resolved rev-parse HEAD^{tree} 2>$null)
        if ($LASTEXITCODE -eq 0 -and $tree -match '^[0-9a-f]{40}$') { return $tree.Trim().ToLowerInvariant() }
    } catch { }
    return $null
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

function New-VisualCaptureRun {
    param(
        [Parameter(Mandatory)][string] $Root,
        [Parameter(Mandatory)][string] $ApplicationSha,
        [string] $HostId = 'local-interactive-windows',
        [string] $RepoRoot = '',
        [string] $ExecutableIdentity = ''
    )
    New-Item -ItemType Directory -Path $Root -Force | Out-Null
    $resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
    $repoRoot = if ($RepoRoot) { $RepoRoot } else { (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path }
    $commit = Get-VisualGitCommit -RepoRoot $repoRoot
    $tree = Get-VisualGitTree -RepoRoot $repoRoot
    $exeId = if ($ExecutableIdentity) { $ExecutableIdentity } else { $ApplicationSha }
    return [pscustomobject]@{
        Root = $resolvedRoot
        RepoRoot = $repoRoot
        ApplicationSha = $ApplicationSha
        ExecutableIdentity = $exeId
        Commit = $commit
        Tree = $tree
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
        [int] $Dpi = 0,
        [Parameter(Mandatory)][int] $ViewportWidth,
        [Parameter(Mandatory)][int] $ViewportHeight,
        [string] $PaneMode = 'single',
        [string] $FixtureId = '',
        [string] $CaptureMethod = 'DwmExtendedFrameBounds+CopyFromScreen',
        [object[]] $PrivacyRedactions = @(),
        [Parameter(Mandatory)][string] $ScreenshotHash,
        [string] $BaselineIdentity = $null,
        $DiffResult = $null,
        $CriticResult = $null,
        [string] $ArtifactProvenance = ''
    )
    $commit = if ($Run.Commit) { $Run.Commit } else { ('0' * 40) }
    # Validate commit format for schema compliance
    if ($commit -notmatch '^[0-9a-f]{40}$') { $commit = ('0' * 40) }
    $provenance = if ($ArtifactProvenance) { $ArtifactProvenance } else { "capture:$Surface/$ScreenshotHash" }
    $effectiveDpi = if ($Dpi -gt 0) { $Dpi } else { 96 }
    return [ordered]@{
        schemaVersion = 2
        commit = $commit
        tree = $Run.Tree
        executableIdentity = $Run.ExecutableIdentity
        captureTimestamp = [DateTime]::UtcNow.ToString('o')
        surface = $Surface
        semanticState = $SemanticState
        theme = $Theme
        density = $Density
        dpi = $effectiveDpi
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
    $getProp = { param($obj,$name)
        try {
            if ($obj -is [System.Collections.IDictionary]) { if ($obj.Contains($name)) { return $obj[$name] } else { return $null } }
            $p = $obj.PSObject.Properties[$name]; if ($null -ne $p) { return $p.Value } else { return $null }
        } catch { $null }
    }
    $sv = & $getProp $Record 'schemaVersion'
    if ($null -eq $sv -or $sv -ne 2) { [void]$errors.Add('schemaVersion must be 2') }
    $commit = & $getProp $Record 'commit'
    if (-not $commit -or $commit -notmatch '^[0-9a-f]{40}$') { [void]$errors.Add('commit must be 40-char hex') }
    $ts = & $getProp $Record 'captureTimestamp'
    if (-not $ts) { [void]$errors.Add('captureTimestamp required') }
    else {
        try { $null = [DateTime]::Parse([string]$ts) } catch { [void]$errors.Add('captureTimestamp must be ISO8601 date-time') }
    }
    $surface = & $getProp $Record 'surface'
    if (-not $surface) { [void]$errors.Add('surface required') }
    $validThemes = @('light','dark','system','highContrast')
    $theme = & $getProp $Record 'theme'
    if ($theme -notin $validThemes) { [void]$errors.Add("theme must be one of $($validThemes -join ',')") }
    $vp = & $getProp $Record 'viewport'
    if ($null -eq $vp -or $null -eq (& $getProp $vp 'width') -or $null -eq (& $getProp $vp 'height')) { [void]$errors.Add('viewport.width/height required') }
    $sh = & $getProp $Record 'screenshotHash'
    if (-not $sh) { [void]$errors.Add('screenshotHash required') }
    $prov = & $getProp $Record 'artifactProvenance'
    if (-not $prov) { [void]$errors.Add('artifactProvenance required') }
    return [pscustomobject]@{ Valid = ($errors.Count -eq 0); Errors = @($errors) }
}

function Add-VisualScenarioResult {
    param(
        [Parameter(Mandatory)] $Run,
        [Parameter(Mandatory)][string] $ScenarioId,
        [Parameter(Mandatory)][string] $ExpectedState,
        [Parameter(Mandatory)][string] $FixtureId,
        [Parameter(Mandatory)][string] $Theme,
        [Parameter(Mandatory)][int] $Width,
        [Parameter(Mandatory)][string] $PaneMode,
        [int] $DpiScale,
        [Parameter(Mandatory)][ValidateSet('PASS', 'FAIL', 'SKIPPED', 'BLOCKED_EXTERNAL', 'BLOCKED_ENVIRONMENT')][string] $Status,
        [string] $Reason,
        [bool] $AssertionPass = $false,
        $AssertionEvidence = $null,
        [string] $ScreenshotPath,
        [string] $ImageHash,
        [string] $DuplicateOf,
        [string] $DuplicateReason,
        [object[]] $PrivacyRedactions,
        [string] $Density = 'comfortable',
        [int] $ViewportHeight = 0,
        [string] $BaselineIdentity = $null,
        $DiffResult = $null,
        $CriticResult = $null
    )
    $relativeImage = $null
    if ($ScreenshotPath) { $relativeImage = Get-VisualRelativePath -Root $Run.Root -Path $ScreenshotPath }
    # Map legacy theme names to v2 enum
    $v2Theme = switch ($Theme.ToLowerInvariant()) {
        'light' { 'light' }
        'dark' { 'dark' }
        'highcontrast' { 'highContrast' }
        'high_contrast' { 'highContrast' }
        default { 'light' }
    }
    $v2Density = if ($Density -in @('compact','comfortable')) { $Density } else { 'comfortable' }
    $effectiveDpi = if ($DpiScale -gt 0) { $DpiScale } else { 96 }
    $vpHeight = if ($ViewportHeight -gt 0) { $ViewportHeight } else { 800 }
    $v2Record = $null
    if ($ImageHash) {
        $v2Record = New-VisualEvidenceV2Record -Run $Run -Surface $ScenarioId -SemanticState $ExpectedState `
            -Theme $v2Theme -Density $v2Density -Dpi $effectiveDpi `
            -ViewportWidth $Width -ViewportHeight $vpHeight `
            -PaneMode $PaneMode -FixtureId $FixtureId -PrivacyRedactions $PrivacyRedactions `
            -ScreenshotHash $ImageHash -BaselineIdentity $BaselineIdentity -DiffResult $DiffResult -CriticResult $CriticResult `
            -ArtifactProvenance "visual-capture:$ScenarioId/$ImageHash"
    }
    $record = [ordered]@{
        scenarioId = $ScenarioId
        expectedState = $ExpectedState
        fixtureId = $FixtureId
        theme = $Theme
        width = $Width
        paneMode = $PaneMode
        dpiScale = $DpiScale
        applicationSha = $Run.ApplicationSha
        screenshotPath = $relativeImage
        imageHash = $ImageHash
        captureMethod = if ($ScreenshotPath) { 'DwmExtendedFrameBounds+CopyFromScreen' } else { $null }
        privacyRedactions = $PrivacyRedactions
        assertionResult = [ordered]@{
            pass = $AssertionPass
            evidence = $AssertionEvidence
        }
        timestampUtc = [DateTime]::UtcNow.ToString('o')
        status = $Status
        reason = $Reason
        duplicateOf = $DuplicateOf
        duplicateReason = $DuplicateReason
        # v2 augmentation
        v2 = $v2Record
    }
    [void]$Run.Records.Add([pscustomobject]$record)
    return $record
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
        captures = @($Run.Records)
    }
    $manifestPath = Join-Path $Run.Root 'manifest.json'
    $summaryPath = Join-Path $Run.Root 'summary.md'
    $manifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $manifestPath -Encoding utf8

    # v2 manifest: structured evidence per verify/visual/schemas/visual-evidence-v2.schema.json
    $v2Captures = @()
    foreach ($rec in $Run.Records) {
        if ($rec.v2) { $v2Captures += $rec.v2 }
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
        captures = @($Run.Records)
        v2Records = @($v2Captures)
    }
    $v2ManifestPath = Join-Path $Run.Root 'manifest-v2.json'
    $v2Manifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $v2ManifestPath -Encoding utf8

    $lines = [System.Collections.Generic.List[string]]::new()
    [void]$lines.Add('# FastFiles deterministic visual capture')
    [void]$lines.Add('')
    [void]$lines.Add("- Application SHA-256: $($Run.ApplicationSha)")
    [void]$lines.Add("- Commit: $($Run.Commit)")
    [void]$lines.Add("- Started (UTC): $($Run.StartedUtc.ToString('o'))")
    [void]$lines.Add("- Finished (UTC): $($finishedUtc.ToString('o'))")
    [void]$lines.Add('')
    [void]$lines.Add('| Scenario | Theme | Width | Pane | Status | Assertion | Image | Reason |')
    [void]$lines.Add('| --- | --- | ---: | --- | --- | --- | --- | --- |')
    foreach ($item in $Run.Records) {
        $image = if ($item.screenshotPath) { $item.screenshotPath } else { '' }
        $reason = if ($item.reason) { $item.reason -replace '\|', '\\|' } else { '' }
        $assertion = if ($item.assertionResult.pass) { 'PASS' } else { 'FAIL' }
        [void]$lines.Add("| $($item.scenarioId) | $($item.theme) | $($item.width) | $($item.paneMode) | $($item.status) | $assertion | $image | $reason |")
    }
    $lines | Set-Content -LiteralPath $summaryPath -Encoding utf8
    return [pscustomobject]@{ Manifest = $manifestPath; ManifestV2 = $v2ManifestPath; Summary = $summaryPath; Count = $Run.Records.Count }
}

Export-ModuleMember -Function Get-VisualWindowBounds, Get-VisualGitCommit, Get-VisualGitTree,
    New-VisualCaptureRun, New-VisualEvidenceV2Record, Test-VisualEvidenceV2Record,
    Add-VisualScenarioResult, Register-VisualCaptureFile, Capture-VisualWindow,
    Write-VisualCaptureRun
