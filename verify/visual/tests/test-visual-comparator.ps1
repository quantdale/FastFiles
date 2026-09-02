<#
    Fixture tests for the real visual comparator (Compare-VisualBaseline.ps1).
    No display required: operates on programmatically generated PNG fixtures.
#>
$ErrorActionPreference = 'Stop'
$script:failures = 0
function Check { param([bool]$Condition,[string]$Description) if($Condition){Write-Host "  ok: $Description"} else{$script:failures++; Write-Host "FAIL: $Description"} }

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$compareScript = Join-Path $repo 'verify\visual\Compare-VisualBaseline.ps1'
Add-Type -AssemblyName System.Drawing

$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ('ff-visual-compare-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tmp -Force | Out-Null
try {
    function New-SolidPng { param([string]$Path,[int]$W,[int]$H,[System.Drawing.Color]$Color)
        $bmp=[System.Drawing.Bitmap]::new($W,$H,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $g=[System.Drawing.Graphics]::FromImage($bmp); try{ $g.Clear($Color); $bmp.Save($Path,[System.Drawing.Imaging.ImageFormat]::Png) } finally{ $g.Dispose(); $bmp.Dispose() }
    }
    function Copy-PngWithOnePixelChange { param([string]$Src,[string]$Dst,[int]$X,[int]$Y,[System.Drawing.Color]$Color)
        $bmp=[System.Drawing.Bitmap]::new($Src); try{ $bmp.SetPixel($X,$Y,$Color); $bmp.Save($Dst,[System.Drawing.Imaging.ImageFormat]::Png) } finally{ $bmp.Dispose() }
    }

    $a = Join-Path $tmp 'a.png'; $b = Join-Path $tmp 'b-identical.png'
    $c = Join-Path $tmp 'c-one-pixel.png'; $d = Join-Path $tmp 'd-large-region.png'
    $e = Join-Path $tmp 'e-different-size.png'; $f = Join-Path $tmp 'f-corrupt.png'
    $diffMap = Join-Path $tmp 'diff.png'

    New-SolidPng -Path $a -W 64 -H 64 -Color ([System.Drawing.Color]::FromArgb(255,240,240,240))
    Copy-Item -LiteralPath $a -Destination $b
    Copy-PngWithOnePixelChange -Src $a -Dst $c -X 10 -Y 10 -Color ([System.Drawing.Color]::Red)
    # Large region: 32x32 red block
    $bmp=[System.Drawing.Bitmap]::new($a); try{ $g=[System.Drawing.Graphics]::FromImage($bmp); try{ $g.FillRectangle([System.Drawing.SolidBrush]::new([System.Drawing.Color]::Red),[System.Drawing.Rectangle]::new(16,16,32,32)); $bmp.Save($d,[System.Drawing.Imaging.ImageFormat]::Png) } finally{ $g.Dispose() } } finally{ $bmp.Dispose() }
    New-SolidPng -Path $e -W 32 -H 32 -Color ([System.Drawing.Color]::FromArgb(255,240,240,240))
    [System.IO.File]::WriteAllBytes($f, [byte[]](0x00,0x01,0x02,0x03,0xFF))

    # 1. Identical => zero diff, identical classification
    $r1 = & $compareScript -BaselinePath $a -CurrentPath $b -Threshold 0.01
    Check ($r1.identical -and $r1.changedPixels -eq 0 -and $r1.diffPercent -eq 0.0 -and $r1.passed -and $r1.classification -eq 'Identical') 'identical images => zero diff, Identical, passed'

    # 2. One pixel => small bounded diff
    $r2 = & $compareScript -BaselinePath $a -CurrentPath $c -Threshold 0.01 -DiffOutputPath $diffMap
    Check ($r2.changedPixels -eq 1) 'one pixel change => changedPixels == 1'
    Check ($r2.diffPercent -gt 0 -and $r2.diffPercent -lt 0.01) 'one pixel => small diffPercent'
    Check ($r2.classification -eq 'Different') 'one pixel => Different classification'
    Check (Test-Path -LiteralPath $diffMap) 'diff map produced for non-identical'
    Check ($r2.changedPixels -lt 100) 'diff bounded for small change'

    # 3. Large region => substantially larger diff than one pixel
    $r3 = & $compareScript -BaselinePath $a -CurrentPath $d -Threshold 0.01
    Check ($r3.changedPixels -gt $r2.changedPixels) 'large region has more changed pixels than one pixel'
    Check ($r3.diffPercent -gt $r2.diffPercent) 'large region diffPercent > small diffPercent'
    Check ($r3.changedPixels -eq 1024) 'large 32x32 region => 1024 changed pixels'

    # 4. Dimension mismatch => explicit classification
    $r4 = & $compareScript -BaselinePath $a -CurrentPath $e -Threshold 0.01
    Check ($r4.classification -eq 'DimensionMismatch') 'dimension mismatch => DimensionMismatch'

    # 5. Threshold boundary: tight threshold should fail large diff, loose should pass
    $r5tight = & $compareScript -BaselinePath $a -CurrentPath $d -Threshold 0.001
    Check (-not $r5tight.passed) 'tight threshold rejects large diff'
    $r5loose = & $compareScript -BaselinePath $a -CurrentPath $d -Threshold 0.5
    Check ($r5loose.passed) 'loose threshold passes large diff'

    # 6. Determinism: repeated runs identical
    $r6a = & $compareScript -BaselinePath $a -CurrentPath $c -Threshold 0.01
    $r6b = & $compareScript -BaselinePath $a -CurrentPath $c -Threshold 0.01
    Check ($r6a.changedPixels -eq $r6b.changedPixels -and $r6a.diffPercent -eq $r6b.diffPercent) 'deterministic repeated runs'

    # 7. Malformed input fails safely
    $malformedFailed = $false
    try { $null = & $compareScript -BaselinePath $f -CurrentPath $a -Threshold 0.01; $malformedFailed = $false } catch { $malformedFailed = $true }
    # Compare returns UnsupportedFormat classification rather than throwing for corrupt images
    $r7 = & $compareScript -BaselinePath $f -CurrentPath $a -Threshold 0.01
    Check ($r7.classification -eq 'UnsupportedFormat' -and -not $r7.passed) 'malformed image => UnsupportedFormat, not passed'

    # 8. Missing file throws
    $missingThrows = $false
    try { $null = & $compareScript -BaselinePath (Join-Path $tmp 'nope.png') -CurrentPath $a } catch { $missingThrows = $true }
    Check $missingThrows 'missing baseline throws'

} finally { if(Test-Path -LiteralPath $tmp){ Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue } }

if($script:failures -gt 0){ Write-Host "RESULT: $script:failures check(s) FAILED"; exit 1 }
Write-Host 'RESULT: all comparator tests passed'; exit 0
