<#
    Tests for DPI match semantics, retention bounds, and no-fabrication guards.
#>
$ErrorActionPreference='Stop'
$script:failures=0
function Check{param([bool]$c,[string]$d) if($c){Write-Host "  ok: $d"} else{$script:failures++; Write-Host "FAIL: $d"} }
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
Import-Module (Join-Path $repo 'verify\visual\VisualCapture.psm1') -Force -DisableNameChecking

# Requested-vs-observed DPI: 100% means 96 device DPI.
$m=Test-VisualDpiMatch -RequestedPercent 100 -ObservedDpi 96
Check ($m.Match -and $m.Known -and $m.ExpectedDpi -eq 96) '100 percent matches observed 96 DPI'
$m=Test-VisualDpiMatch -RequestedPercent 150 -ObservedDpi 144
Check ($m.Match -and $m.ExpectedDpi -eq 144) '150 percent matches observed 144 DPI'
$m=Test-VisualDpiMatch -RequestedPercent 100 -ObservedDpi 144
Check ((-not $m.Match) -and $m.Known -and $m.ObservedDpi -eq 144) '100 percent request with 144 observed is a recorded mismatch, not a pass'
$m=Test-VisualDpiMatch -RequestedPercent 100 -ObservedDpi 0
Check ((-not $m.Match) -and (-not $m.Known)) 'unobserved DPI is unknown, never silently 96'

# Screenshot records require observed DPI: missing DPI is a hard error.
$runRoot=Join-Path ([IO.Path]::GetTempPath()) ('ff-dpi-run-'+[Guid]::NewGuid().ToString('N'))
$run=New-VisualCaptureRun -Root $runRoot -ApplicationSha ('ab'*32)
$imgPath=Join-Path $runRoot 'shot.png'
Add-Type -AssemblyName System.Drawing
$bmp=[System.Drawing.Bitmap]::new(8,8,[System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
try { $bmp.Save($imgPath,[System.Drawing.Imaging.ImageFormat]::Png) } finally { $bmp.Dispose() }
$imgHash=(Get-FileHash -LiteralPath $imgPath -Algorithm SHA256).Hash.ToLowerInvariant()
$threw=$false
try {
    Add-VisualScenarioResult -Run $run -ScenarioId 'dpi-missing' -ExpectedState 'x' -FixtureId 'f' `
        -Theme 'Light' -Width 1280 -PaneMode 'single' -DpiScalePercent 100 -ObservedDpi 0 `
        -Status 'PASS' -AssertionPass $true -ScreenshotPath $imgPath -ImageHash $imgHash | Out-Null
} catch { $threw=$true }
Check $threw 'PASS with unobserved DPI throws instead of fabricating 96'
$okRecord=Add-VisualScenarioResult -Run $run -ScenarioId 'dpi-ok' -ExpectedState 'x' -FixtureId 'f' `
    -Theme 'Light' -Width 1280 -PaneMode 'single' -DpiScalePercent 100 -ObservedDpi 96 `
    -Status 'PASS' -AssertionPass $true -ScreenshotPath $imgPath -ImageHash $imgHash
Check ($okRecord.observedDpi -eq 96 -and $okRecord.dpiMatch -eq $true -and $null -ne $okRecord.v2) 'observed DPI flows into record and v2 evidence'
Check ($okRecord.v2.dpi -eq 96 -and $okRecord.v2.dpiScalePercent -eq 100) 'v2 separates observed dpi from requested percent'
$misRecord=Add-VisualScenarioResult -Run $run -ScenarioId 'dpi-mismatch' -ExpectedState 'x' -FixtureId 'f' `
    -Theme 'Light' -Width 1280 -PaneMode 'single' -DpiScalePercent 100 -ObservedDpi 144 `
    -Status 'PASS' -AssertionPass $true -ScreenshotPath $imgPath -ImageHash $imgHash
Check ($misRecord.dpiMatch -eq $false -and $misRecord.v2.dpi -eq 144) 'DPI mismatch is recorded, observed value preserved'
Remove-Item -LiteralPath $runRoot -Recurse -Force -ErrorAction SilentlyContinue

# Retention: only stale tool-owned run dirs go away.
$retRoot=Join-Path ([IO.Path]::GetTempPath()) ('ff-retention-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $retRoot -Force | Out-Null
try {
    $oldOwned=Join-Path $retRoot 'run-old'; New-Item -ItemType Directory -Path $oldOwned -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $oldOwned 'manifest.json') -Value '{"schema":"fastfiles.visual-capture.v1"}' -Encoding utf8
    $newOwned=Join-Path $retRoot 'run-new'; New-Item -ItemType Directory -Path $newOwned -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $newOwned 'manifest.json') -Value '{"schema":"fastfiles.visual-capture.v1"}' -Encoding utf8
    $oldForeign=Join-Path $retRoot 'notes-old'; New-Item -ItemType Directory -Path $oldForeign -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $oldForeign 'readme.txt') -Value 'not tool-owned' -Encoding utf8
    (Get-Item $oldOwned).LastWriteTime=(Get-Date).AddDays(-30)
    (Get-Item $oldForeign).LastWriteTime=(Get-Date).AddDays(-30)
    $removed=Clear-VisualRunRetention -Root $retRoot -OlderThanDays 14
    Check ((-not (Test-Path $oldOwned)) -and ($removed -contains $oldOwned)) 'stale tool-owned run dir removed'
    Check ((Test-Path $newOwned) -and (Test-Path $oldForeign)) 'fresh runs and foreign dirs preserved'
    $removedNone=Clear-VisualRunRetention -Root (Join-Path $retRoot 'missing') -OlderThanDays 14
    Check (@($removedNone).Count -eq 0) 'missing root removes nothing'
} finally {
    Remove-Item -LiteralPath $retRoot -Recurse -Force -ErrorAction SilentlyContinue
}
if($script:failures -gt 0){ Write-Host "RESULT: $script:failures FAILED"; exit 1 }
Write-Host 'RESULT: all DPI and retention tests passed'; exit 0
