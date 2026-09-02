param(
    [Parameter(Mandatory)][string]$BaselinePath,
    [Parameter(Mandatory)][string]$CurrentPath,
    [double]$Threshold = 0.01,
    [string]$DiffOutputPath,
    [double]$PerPixelTolerance = 0.0
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

<#
    Deterministic visual regression engine.

    Semantics:
      - Validates both paths exist and are readable images.
      - Exact SHA256 fast path: identical bytes => 0 diff, no diff map.
      - Dimension mismatch => explicit DimensionMismatch classification (not a percentage).
      - Pixel comparison: per-pixel normalized difference. Default exact (tolerance 0).
        Optional PerPixelTolerance in [0,1] allows near-identical colors to count as same
        (useful for anti-aliased edges; 0 = exact).
      - Reports: width, height, totalPixels, changedPixels, diffPercent (changed/total),
        meanNormalizedDiff, maxNormalizedDiff, threshold, passed, classification, diff map.
      - Diff map: deterministic magenta overlay image showing changed pixels where applicable.
      - All outputs deterministic for same inputs.

    Threshold rationale: default 1% (0.01) allows small rendering variance but
    catches meaningful layout shifts. Callers should set per-scenario thresholds
    and document them; narrowing to find regressions is encouraged.
#>

if ([double]::IsNaN($Threshold) -or [double]::IsInfinity($Threshold) -or $Threshold -lt 0 -or $Threshold -gt 1) {
    throw "Threshold must be finite and in [0,1], got $Threshold"
}
if ([double]::IsNaN($PerPixelTolerance) -or [double]::IsInfinity($PerPixelTolerance) -or $PerPixelTolerance -lt 0 -or $PerPixelTolerance -gt 1) {
    throw "PerPixelTolerance must be finite and in [0,1], got $PerPixelTolerance"
}
if ($DiffOutputPath) {
    $diffParent = Split-Path -Parent $DiffOutputPath
    if ($diffParent -and -not (Test-Path -LiteralPath $diffParent -PathType Container)) {
        New-Item -ItemType Directory -Path $diffParent -Force | Out-Null
    }
}

if (-not (Test-Path -LiteralPath $BaselinePath -PathType Leaf)) { throw "baseline not found: $BaselinePath" }
if (-not (Test-Path -LiteralPath $CurrentPath -PathType Leaf)) { throw "current not found: $CurrentPath" }

$baselineHash = (Get-FileHash -LiteralPath $BaselinePath -Algorithm SHA256).Hash.ToLowerInvariant()
$currentHash = (Get-FileHash -LiteralPath $CurrentPath -Algorithm SHA256).Hash.ToLowerInvariant()
$isIdentical = $baselineHash -eq $currentHash

$maximumPixels = [int64]64 * 1000 * 1000
# Try to load as images for dimension + pixel comparison
Add-Type -AssemblyName System.Drawing

function Get-ImageInfo {
    param([string]$Path)
    try {
        $img = [System.Drawing.Image]::FromFile($Path)
        try {
            return [pscustomobject]@{
                Width = $img.Width
                Height = $img.Height
                PixelFormat = $img.PixelFormat
                Loaded = $true
                Image = $img
            }
        } catch {
            if ($img) { $img.Dispose() }
            throw
        }
    } catch {
        return [pscustomobject]@{ Loaded = $false; Error = $_.Exception.Message; Width = 0; Height = 0; Image = $null }
    }
}

$baselineInfo = Get-ImageInfo -Path $BaselinePath
$currentInfo = Get-ImageInfo -Path $CurrentPath

# Non-image or unloadable => explicit classification
if (-not $baselineInfo.Loaded -or -not $currentInfo.Loaded) {
    $result = [ordered]@{
        baseline = $BaselinePath
        current = $CurrentPath
        classification = 'UnsupportedFormat'
        identical = $false
        passed = $false
        baselineHash = $baselineHash
        currentHash = $currentHash
        baselineWidth = $baselineInfo.Width
        baselineHeight = $baselineInfo.Height
        currentWidth = $currentInfo.Width
        currentHeight = $currentInfo.Height
        totalPixels = 0
        changedPixels = 0
        diffPercent = 1.0
        meanNormalizedDiff = 1.0
        maxNormalizedDiff = 1.0
        threshold = $Threshold
        error = if (-not $baselineInfo.Loaded) { "baseline not a valid image: $($baselineInfo.Error)" } else { "current not a valid image: $($currentInfo.Error)" }
        diffMapPath = $null
    }
    if ($baselineInfo.Image) { $baselineInfo.Image.Dispose() }
    if ($currentInfo.Image) { $currentInfo.Image.Dispose() }
    return [pscustomobject]$result
}

# Dimension mismatch - explicit classification, no pixel diff
if ($baselineInfo.Width -ne $currentInfo.Width -or $baselineInfo.Height -ne $currentInfo.Height) {
    $result = [ordered]@{
        baseline = $BaselinePath
        current = $CurrentPath
        classification = 'DimensionMismatch'
        identical = $false
        passed = $false
        baselineHash = $baselineHash
        baselineWidth = $baselineInfo.Width
        baselineHeight = $baselineInfo.Height
        currentHash = $currentHash
        currentWidth = $currentInfo.Width
        currentHeight = $currentInfo.Height
        totalPixels = 0
        changedPixels = 0
        diffPercent = 1.0
        meanNormalizedDiff = 1.0
        maxNormalizedDiff = 1.0
        threshold = $Threshold
        diffMapPath = $null
    }
    $baselineInfo.Image.Dispose()
    $currentInfo.Image.Dispose()
    if ($DiffOutputPath) {
        "classification=DimensionMismatch`nbaseline=$baselineHash`ncurrent=$currentHash`nbaselineSize=$($baselineInfo.Width)x$($baselineInfo.Height)`ncurrentSize=$($currentInfo.Width)x$($currentInfo.Height)" | Set-Content -LiteralPath $DiffOutputPath -NoNewline -Encoding utf8
    }
    return [pscustomobject]$result
}

$pixelCount = [int64]$baselineInfo.Width * [int64]$baselineInfo.Height
if ($pixelCount -gt $maximumPixels) {
    $baselineInfo.Image.Dispose()
    $currentInfo.Image.Dispose()
    throw "image dimensions exceed comparator safety limit of $maximumPixels pixels"
}

# Identical hash fast path - dimensions already equal
if ($isIdentical) {
    $result = [ordered]@{
        baseline = $BaselinePath
        current = $CurrentPath
        classification = 'Identical'
        identical = $true
        passed = $true
        baselineHash = $baselineHash
        currentHash = $currentHash
        baselineWidth = $baselineInfo.Width
        baselineHeight = $baselineInfo.Height
        currentWidth = $currentInfo.Width
        currentHeight = $currentInfo.Height
        totalPixels = [int64]$baselineInfo.Width * [int64]$baselineInfo.Height
        changedPixels = 0
        diffPercent = 0.0
        meanNormalizedDiff = 0.0
        maxNormalizedDiff = 0.0
        threshold = $Threshold
        diffMapPath = $null
    }
    $baselineInfo.Image.Dispose()
    $currentInfo.Image.Dispose()
    return [pscustomobject]$result
}

# Pixel comparison
$width = $baselineInfo.Width
$height = $baselineInfo.Height
$totalPixels = [int64]$width * [int64]$height

# Convert to bitmaps for pixel access
function To-Bitmap {
    param($Image)
    if ($Image -is [System.Drawing.Bitmap]) { return $Image }
    $bmp = [System.Drawing.Bitmap]::new($Image)
    $Image.Dispose()
    return $bmp
}

$bmpBase = To-Bitmap -Image $baselineInfo.Image
$bmpCurr = To-Bitmap -Image $currentInfo.Image

# Use LockBits for deterministic fast path; fallback to GetPixel if needed
$changedPixels = 0
$sumNorm = 0.0
$maxNorm = 0.0

function Ensure-32bpp {
    param([System.Drawing.Bitmap]$Bmp)
    $converted = [System.Drawing.Bitmap]::new($Bmp.Width, $Bmp.Height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($converted)
    try {
        $g.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
        $g.DrawImageUnscaled($Bmp, 0, 0)
    } finally {
        $g.Dispose()
        $Bmp.Dispose()
    }
    return $converted
}

$bmpBase = Ensure-32bpp -Bmp $bmpBase
$bmpCurr = Ensure-32bpp -Bmp $bmpCurr

$rect = [System.Drawing.Rectangle]::new(0, 0, $width, $height)
$baseData = $null
$currData = $null
try {
    $baseData = $bmpBase.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $currData = $bmpCurr.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
} catch {
    if ($baseData) { $bmpBase.UnlockBits($baseData) }
    if ($currData) { $bmpCurr.UnlockBits($currData) }
    $bmpBase.Dispose()
    $bmpCurr.Dispose()
    throw
}

# Allocate diff map bitmap if requested
$diffBmp = $null
if ($DiffOutputPath) {
    $diffBmp = [System.Drawing.Bitmap]::new($width, $height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    # Leave diff map initially transparent; magenta pixels will mark changes
    $diffBmp.MakeTransparent()
}

$loopFailed = $false
try {
    $baseStride = [int]$baseData.Stride
    $currStride = [int]$currData.Stride
    $rowBytes = [int]($width * 4)
    for ($y = 0; $y -lt $height; $y++) {
        $baseRowIndex = if ($baseStride -ge 0) { $y } else { $height - 1 - $y }
        $currRowIndex = if ($currStride -ge 0) { $y } else { $height - 1 - $y }
        $baseRow = [IntPtr]::Add($baseData.Scan0, [int]($baseRowIndex * [Math]::Abs($baseStride)))
        $currRow = [IntPtr]::Add($currData.Scan0, [int]($currRowIndex * [Math]::Abs($currStride)))
        $baseBytes = [byte[]]::new($rowBytes)
        $currBytes = [byte[]]::new($rowBytes)
        [System.Runtime.InteropServices.Marshal]::Copy($baseRow, $baseBytes, 0, $rowBytes)
        [System.Runtime.InteropServices.Marshal]::Copy($currRow, $currBytes, 0, $rowBytes)
        for ($x = 0; $x -lt $width; $x++) {
            $o = $x * 4
            $bB = $baseBytes[$o]; $gB = $baseBytes[$o+1]; $rB = $baseBytes[$o+2]; $aB = $baseBytes[$o+3]
            $bC = $currBytes[$o]; $gC = $currBytes[$o+1]; $rC = $currBytes[$o+2]; $aC = $currBytes[$o+3]
            # Compare the visible (alpha-weighted) color. RGB in a fully
            # transparent pixel is not part of the rendered image.
            $baseAlpha = [double]$aB / 255.0
            $currAlpha = [double]$aC / 255.0
            $dr = [Math]::Abs(([double]$rB * $baseAlpha) - ([double]$rC * $currAlpha))
            $dg = [Math]::Abs(([double]$gB * $baseAlpha) - ([double]$gC * $currAlpha))
            $db = [Math]::Abs(([double]$bB * $baseAlpha) - ([double]$bC * $currAlpha))
            $da = [Math]::Abs([double]$aB - [double]$aC)
            $norm = ([Math]::Max([Math]::Max($dr, $dg), [Math]::Max($db, $da * 0.5))) / 255.0
            if ($norm -gt $maxNorm) { $maxNorm = $norm }
            $sumNorm += $norm
            $isChanged = $norm -gt $PerPixelTolerance
            if ($isChanged) {
                $changedPixels++
                if ($diffBmp) { $diffBmp.SetPixel($x, $y, [System.Drawing.Color]::Magenta) }
            }
        }
    }
} catch {
    $loopFailed = $true
    throw
} finally {
    if ($baseData) { $bmpBase.UnlockBits($baseData) }
    if ($currData) { $bmpCurr.UnlockBits($currData) }
    if ($loopFailed) {
        if ($diffBmp) { $diffBmp.Dispose() }
        $bmpBase.Dispose()
        $bmpCurr.Dispose()
    }
}

$diffPercent = if ($totalPixels -gt 0) { [double]$changedPixels / [double]$totalPixels } else { 1.0 }
$meanNorm = if ($totalPixels -gt 0) { $sumNorm / [double]$totalPixels } else { 1.0 }

$actualDiffPath = $null
if ($DiffOutputPath -and $changedPixels -gt 0 -and $diffBmp) {
    $diffBmp.Save($DiffOutputPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $actualDiffPath = $DiffOutputPath
} elseif ($DiffOutputPath -and $changedPixels -eq 0) {
    # No diff image needed when identical at pixel level; remove stale file if any
    if (Test-Path -LiteralPath $DiffOutputPath) { Remove-Item -LiteralPath $DiffOutputPath -Force -ErrorAction SilentlyContinue }
    $actualDiffPath = $null
}

if ($diffBmp) { $diffBmp.Dispose() }
$bmpBase.Dispose()
$bmpCurr.Dispose()

$classification = if ($changedPixels -eq 0) { 'Identical' } else { 'Different' }
$passed = ($diffPercent -le $Threshold)

$result = [ordered]@{
    baseline = $BaselinePath
    current = $CurrentPath
    classification = $classification
    identical = ($changedPixels -eq 0)
    passed = $passed
    baselineHash = $baselineHash
    currentHash = $currentHash
    baselineWidth = $width
    baselineHeight = $height
    currentWidth = $width
    currentHeight = $height
    totalPixels = $totalPixels
    changedPixels = $changedPixels
    diffPercent = $diffPercent
    meanNormalizedDiff = $meanNorm
    maxNormalizedDiff = $maxNorm
    threshold = $Threshold
    perPixelTolerance = $PerPixelTolerance
    diffMapPath = $actualDiffPath
}

if (-not $result.passed) { Write-Warning "visual regression FAIL: diffPercent=$diffPercent threshold=$Threshold changed=$changedPixels/$totalPixels" }
return [pscustomobject]$result
