<#
    Deterministic visual evidence primitives for the Windows UI validation
    capabilities.

    The image path is deliberately narrow: the live top-level window's DWM
    extended frame bounds are copied from the screen. PrintWindow is not a
    capture substitute. A screenshot is only produced after the caller has
    completed its semantic UIA post-condition.
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
        [string] $HostId = 'local-interactive-windows'
    )
    New-Item -ItemType Directory -Path $Root -Force | Out-Null
    return [pscustomobject]@{
        Root = (Resolve-Path -LiteralPath $Root).Path
        ApplicationSha = $ApplicationSha
        HostId = $HostId
        StartedUtc = [DateTime]::UtcNow
        Records = [System.Collections.ArrayList]::new()
        SeenHashes = @{}
    }
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
        [object[]] $PrivacyRedactions
    )
    $relativeImage = $null
    if ($ScreenshotPath) { $relativeImage = Get-VisualRelativePath -Root $Run.Root -Path $ScreenshotPath }
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

    $lines = [System.Collections.Generic.List[string]]::new()
    [void]$lines.Add('# FastFiles deterministic visual capture')
    [void]$lines.Add('')
    [void]$lines.Add("- Application SHA-256: $($Run.ApplicationSha)")
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
    return [pscustomobject]@{ Manifest = $manifestPath; Summary = $summaryPath; Count = $Run.Records.Count }
}

Export-ModuleMember -Function Get-VisualWindowBounds, New-VisualCaptureRun,
    Add-VisualScenarioResult, Register-VisualCaptureFile, Capture-VisualWindow,
    Write-VisualCaptureRun
