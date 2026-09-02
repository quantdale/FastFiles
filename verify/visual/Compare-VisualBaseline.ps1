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

if (-not (Test-Path -LiteralPath $BaselinePath -PathType Leaf)) { throw "baseline not found: $BaselinePath" }
if (-not (Test-Path -LiteralPath $CurrentPath -PathType Leaf)) { throw "current not found: $CurrentPath" }

$baselineHash = (Get-FileHash -LiteralPath $BaselinePath -Algorithm SHA256).Hash.ToLowerInvariant()
$currentHash = (Get-FileHash -LiteralPath $CurrentPath -Algorithm SHA256).Hash.ToLowerInvariant()
$isIdentical = $baselineHash -eq $currentHash

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
        totalPixels = $baselineInfo.Width * $baselineInfo.Height
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
$totalPixels = $width * $height

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

# Ensure 32bpp for consistent comparison
function Ensure-32bpp {
    param([System.Drawing.Bitmap]$Bmp)
    if ($Bmp.PixelFormat -eq [System.Drawing.Imaging.PixelFormat]::Format32bppArgb -or
        $Bmp.PixelFormat -eq [System.Drawing.Imaging.PixelFormat]::Format32bppPArgb) {
        return $Bmp
    }
    $converted = [System.Drawing.Bitmap]::new($Bmp.Width, $Bmp.Height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($converted)
    try { $g.DrawImage($Bmp, 0, 0) } finally { $g.Dispose() }
    $Bmp.Dispose()
    return $converted
}

$bmpBase = Ensure-32bpp -Bmp $bmpBase
$bmpCurr = Ensure-32bpp -Bmp $bmpCurr

$rect = [System.Drawing.Rectangle]::new(0, 0, $width, $height)
$baseData = $bmpBase.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$currData = $bmpCurr.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)

# Allocate diff map bitmap if requested
$diffBmp = $null
if ($DiffOutputPath) {
    $diffBmp = [System.Drawing.Bitmap]::new($width, $height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    # Leave diff map initially transparent; magenta pixels will mark changes
    $diffBmp.MakeTransparent()
}

try {
    $stride = $baseData.Stride
    for ($y = 0; $y -lt $height; $y++) {
        $baseRow = [IntPtr]::Add($baseData.Scan0, $y * $stride)
        $currRow = [IntPtr]::Add($currData.Scan0, $y * $stride)
        $rowBytes = $width * 4
        $baseBytes = [byte[]]::new($rowBytes)
        $currBytes = [byte[]]::new($rowBytes)
        [System.Runtime.InteropServices.Marshal]::Copy($baseRow, $baseBytes, 0, $rowBytes)
        [System.Runtime.InteropServices.Marshal]::Copy($currRow, $currBytes, 0, $rowBytes)
        for ($x = 0; $x -lt $width; $x++) {
            $o = $x * 4
            $bB = $baseBytes[$o]; $gB = $baseBytes[$o+1]; $rB = $baseBytes[$o+2]; $aB = $baseBytes[$o+3]
            $bC = $currBytes[$o]; $gC = $currBytes[$o+1]; $rC = $currBytes[$o+2]; $aC = $currBytes[$o+3]
            $dr = [Math]::Abs([int]$rB - [int]$rC)
            $dg = [Math]::Abs([int]$gB - [int]$gC)
            $db = [Math]::Abs([int]$bB - [int]$bC)
            $da = [Math]::Abs([int]$aB - [int]$aC)
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
} finally {
    $bmpBase.UnlockBits($baseData)
    $bmpCurr.UnlockBits($currData)
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
