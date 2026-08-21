[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $Root,
    [ValidateSet('Small', 'Medium', 'Large')] [string] $Profile = 'Small',
    [int] $ItemCount = 0,
    [switch] $Reset,
    [switch] $IncludeReparsePoints,
    [ValidateSet('human', 'json')] [string] $OutputFormat = 'human'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$modulePath = Join-Path $PSScriptRoot 'DogfoodCommon.psm1'
Import-Module $modulePath -Force

$defaults = @{ Small = 120; Medium = 10000; Large = 100000 }
if ($ItemCount -le 0) { $ItemCount = [int]$defaults[$Profile] }
if ($ItemCount -lt 1) { throw 'DOGFOOD_ITEM_COUNT_INVALID' }

$rootFull = Assert-DogfoodRootSafe -Root $Root
if (Test-Path -LiteralPath $rootFull) {
    if (-not $Reset) {
        throw "DOGFOOD_ROOT_EXISTS_USE_RESET: '$rootFull'"
    }
    Remove-DogfoodRoot -Root $rootFull
}

[IO.Directory]::CreateDirectory($rootFull) | Out-Null
Write-DogfoodUtf8Text -Path (Join-Path $rootFull (Get-DogfoodMarkerName)) -Text 'FastFiles-Dogfood-Root-v1'

$writer = $null
$warnings = [Collections.Generic.List[string]]::new()
$fileCount = 0
$directoryCount = 0
$fixedTime = [DateTime]::Parse('2024-01-15T12:00:00Z').ToUniversalTime()

function Get-DeterministicBytes {
    param([int] $Length, [int] $Seed)

    $bytes = [byte[]]::new($Length)
    for ($index = 0; $index -lt $Length; $index++) {
        $bytes[$index] = [byte](($Seed + ($index * 31) + (($index * $index) % 251)) % 256)
    }
    return $bytes
}

function Get-ByteHash {
    param([byte[]] $Bytes)

    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($algorithm.ComputeHash($Bytes))).Replace('-', '').ToLowerInvariant()
    } finally {
        $algorithm.Dispose()
    }
}

function Get-FileAttributesForKind {
    param([string] $Kind)

    $attributes = [IO.FileAttributes]::Normal
    if ($Kind -eq 'hidden') { $attributes = $attributes -bor [IO.FileAttributes]::Hidden }
    if ($Kind -eq 'readonly') { $attributes = $attributes -bor [IO.FileAttributes]::ReadOnly }
    if ($Kind -eq 'system-like') { $attributes = $attributes -bor [IO.FileAttributes]::System }
    return [IO.FileAttributes]$attributes
}

function New-DogfoodDirectory {
    param([Parameter(Mandatory)] [string] $RelativePath)

    $path = Join-Path $rootFull $RelativePath
    try {
        [IO.Directory]::CreateDirectory($path) | Out-Null
    } catch [IO.PathTooLongException] {
        [IO.Directory]::CreateDirectory((ConvertTo-DogfoodExtendedPath -Path $path)) | Out-Null
    }
    $script:directoryCount++
    Write-DogfoodManifestItem -State $writer -RelativePath $RelativePath -IsDirectory $true -Attributes 'Directory' -LastWriteTimeUtc $fixedTime.ToString('o') -Kind 'directory'
}

function New-DogfoodFile {
    param(
        [Parameter(Mandatory)] [string] $RelativePath,
        [Parameter(Mandatory)] [byte[]] $Bytes,
        [string] $Kind = 'fixture',
        [string] $AttributeKind = 'normal',
        [DateTime] $LastWriteTimeUtc = $fixedTime
    )

    $path = Join-Path $rootFull $RelativePath
    $parent = [IO.Path]::GetDirectoryName($path)
    try {
        [IO.Directory]::CreateDirectory($parent) | Out-Null
    } catch [IO.PathTooLongException] {
        [IO.Directory]::CreateDirectory((ConvertTo-DogfoodExtendedPath -Path $parent)) | Out-Null
    }

    $ioPath = $path
    try {
        [IO.File]::WriteAllBytes($ioPath, $Bytes)
    } catch [IO.PathTooLongException] {
        $ioPath = ConvertTo-DogfoodExtendedPath -Path $path
        [IO.File]::WriteAllBytes($ioPath, $Bytes)
    }
    [IO.File]::SetAttributes($ioPath, (Get-FileAttributesForKind -Kind $AttributeKind))
    [IO.File]::SetLastWriteTimeUtc($ioPath, $LastWriteTimeUtc)
    $script:fileCount++
    Write-DogfoodManifestItem -State $writer -RelativePath $RelativePath -IsDirectory $false -Size ([UInt64]$Bytes.Length) -Sha256 (Get-ByteHash -Bytes $Bytes) -Attributes ([IO.File]::GetAttributes($ioPath)).ToString() -LastWriteTimeUtc $LastWriteTimeUtc.ToString('o') -Kind $Kind
}

function New-DogfoodCopiedFile {
    param(
        [Parameter(Mandatory)] [string] $RelativePath,
        [Parameter(Mandatory)] [string] $SourcePath,
        [string] $Kind = 'executable'
    )

    $bytes = [IO.File]::ReadAllBytes($SourcePath)
    New-DogfoodFile -RelativePath $RelativePath -Bytes $bytes -Kind $Kind
}

function New-ZipBytes {
    $stream = [IO.MemoryStream]::new()
    $archive = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Create, $true)
    try {
        $entry = $archive.CreateEntry('inside/archive-note.txt')
        $entry.LastWriteTime = [DateTimeOffset]::Parse('2024-01-15T12:00:00Z')
        $entryStream = $entry.Open()
        try {
            $textBytes = [Text.UTF8Encoding]::new($false).GetBytes('FastFiles deterministic archive fixture.' + "`n")
            $entryStream.Write($textBytes, 0, $textBytes.Length)
        } finally {
            $entryStream.Dispose()
        }
    } finally {
        $archive.Dispose()
    }
    return $stream.ToArray()
}

function New-BmpBytes {
    param([int] $Width = 1024, [int] $Height = 1024)

    $rowBytes = $Width * 3
    $pixelBytes = $rowBytes * $Height
    $pixels = [byte[]]::new($pixelBytes)
    for ($offset = 0; $offset -lt $pixelBytes; $offset += 3) {
        $pixel = [int]($offset / 3)
        $pixels[$offset] = [byte]($pixel % 256)
        $pixels[$offset + 1] = [byte](($pixel * 3) % 256)
        $pixels[$offset + 2] = [byte](($pixel * 5) % 256)
    }
    $stream = [IO.MemoryStream]::new()
    $binary = [IO.BinaryWriter]::new($stream)
    try {
        $binary.Write([byte]0x42)
        $binary.Write([byte]0x4D)
        $binary.Write([int](54 + $pixelBytes))
        $binary.Write([int]0)
        $binary.Write([int]54)
        $binary.Write([int]40)
        $binary.Write([int]$Width)
        $binary.Write([int]$Height)
        $binary.Write([short]1)
        $binary.Write([short]24)
        $binary.Write([int]0)
        $binary.Write([int]$pixelBytes)
        $binary.Write([int]2835)
        $binary.Write([int]2835)
        $binary.Write([int]0)
        $binary.Write([int]0)
        $binary.Write($pixels)
        return $stream.ToArray()
    } finally {
        $binary.Dispose()
        $stream.Dispose()
    }
}

try {
    $writer = New-DogfoodManifestWriter -Root $rootFull -Profile $Profile -ItemBudget $ItemCount

    foreach ($directory in @(
        '01-small',
        '02-duplicates',
        '02-duplicates\left',
        '02-duplicates\right',
        '03-deep',
        '04-special',
        '05-content',
        '06-aged',
        '07-bulk',
        '08-reparse'
    )) {
        New-DogfoodDirectory -RelativePath $directory
    }

    New-DogfoodDirectory -RelativePath '04-special\empty-directory'
    New-DogfoodDirectory -RelativePath '06-aged\old'
    New-DogfoodDirectory -RelativePath '06-aged\recent'

    $utf8 = [Text.UTF8Encoding]::new($false).GetBytes("FastFiles UTF-8 fixture: café, 日本語, 🙂`n")
    $utf16Encoding = [Text.UnicodeEncoding]::new($false, $true)
    $utf16 = $utf16Encoding.GetPreamble() + $utf16Encoding.GetBytes("FastFiles UTF-16 fixture: café, 日本語, 🙂`n")
    New-DogfoodFile -RelativePath '05-content\utf8.txt' -Bytes $utf8 -Kind 'utf8-text'
    New-DogfoodFile -RelativePath '05-content\utf16.txt' -Bytes $utf16 -Kind 'utf16-text'
    New-DogfoodFile -RelativePath '05-content\no-extension' -Bytes (Get-DeterministicBytes -Length 211 -Seed 11) -Kind 'binary-no-extension'
    New-DogfoodFile -RelativePath '05-content\multiple.dots.name.tar' -Bytes $utf8 -Kind 'multiple-dot'
    New-DogfoodFile -RelativePath '05-content\document.rtf' -Bytes ([Text.Encoding]::ASCII.GetBytes('{\rtf1\ansi FastFiles fixture}')) -Kind 'document'
    New-DogfoodFile -RelativePath '05-content\sample.csv' -Bytes ([Text.UTF8Encoding]::new($false).GetBytes("name,value`nalpha,1`nbeta,2`n")) -Kind 'document'
    New-DogfoodFile -RelativePath '05-content\sample.zip' -Bytes (New-ZipBytes) -Kind 'archive'
    New-DogfoodFile -RelativePath '05-content\large-image.bmp' -Bytes (New-BmpBytes) -Kind 'image'
    New-DogfoodFile -RelativePath '05-content\binary.dat' -Bytes (Get-DeterministicBytes -Length 4096 -Seed 97) -Kind 'binary'
    New-DogfoodFile -RelativePath '04-special\hidden.txt' -Bytes $utf8 -Kind 'hidden' -AttributeKind 'hidden'
    New-DogfoodFile -RelativePath '04-special\readonly.txt' -Bytes $utf8 -Kind 'readonly' -AttributeKind 'readonly'
    New-DogfoodFile -RelativePath '04-special\system-like.txt' -Bytes $utf8 -Kind 'system-like' -AttributeKind 'system-like'
    New-DogfoodFile -RelativePath '04-special\emoji-🙂.txt' -Bytes $utf8 -Kind 'unicode'
    New-DogfoodFile -RelativePath '04-special\é.txt' -Bytes ([Text.UTF8Encoding]::new($false).GetBytes('lowercase-case-pair')) -Kind 'unicode-case-pair'
    New-DogfoodFile -RelativePath '04-special\É.txt' -Bytes ([Text.UTF8Encoding]::new($false).GetBytes('uppercase-case-pair')) -Kind 'unicode-case-pair'
    New-DogfoodFile -RelativePath ('04-special\long-' + ('x' * 100) + '.txt') -Bytes $utf8 -Kind 'long-name'

    $deepParts = [Collections.Generic.List[string]]::new()
    for ($level = 1; $level -le 12; $level++) { $deepParts.Add(('level-{0:D2}-{1}' -f $level, ('y' * 28))) }
    $deepDirectory = ($deepParts -join '\')
    $deepPrefix = '03-deep'
    foreach ($deepPart in $deepParts) {
        $deepPrefix += '\' + $deepPart
        New-DogfoodDirectory -RelativePath $deepPrefix
    }
    try {
        New-DogfoodFile -RelativePath ('03-deep\' + $deepDirectory + '\extended-length-' + ('z' * 110) + '.txt') -Bytes $utf8 -Kind 'extended-length'
    } catch {
        $warnings.Add('extended-length-path-unavailable: ' + $_.Exception.Message)
    }

    New-DogfoodFile -RelativePath '02-duplicates\left\duplicate.txt' -Bytes ([Text.UTF8Encoding]::new($false).GetBytes('left-copy')) -Kind 'duplicate-basename'
    New-DogfoodFile -RelativePath '02-duplicates\right\duplicate.txt' -Bytes ([Text.UTF8Encoding]::new($false).GetBytes('right-copy')) -Kind 'duplicate-basename'
    New-DogfoodFile -RelativePath '06-aged\old\old.txt' -Bytes $utf8 -Kind 'old' -LastWriteTimeUtc ([DateTime]::Parse('2020-02-03T04:05:06Z').ToUniversalTime())
    New-DogfoodFile -RelativePath '06-aged\recent\recent.txt' -Bytes $utf8 -Kind 'recent' -LastWriteTimeUtc ([DateTime]::Parse('2024-12-03T04:05:06Z').ToUniversalTime())
    New-DogfoodFile -RelativePath '01-small\small-large-file.bin' -Bytes (Get-DeterministicBytes -Length 5242880 -Seed 41) -Kind 'large-file'

    $notepad = Join-Path $env:SystemRoot 'System32\notepad.exe'
    if (Test-Path -LiteralPath $notepad -PathType Leaf) {
        New-DogfoodCopiedFile -RelativePath '05-content\sample.exe' -SourcePath $notepad
    } else {
        $warnings.Add('system-executable-unavailable')
        New-DogfoodFile -RelativePath '05-content\sample.exe' -Bytes ([byte[]](0x4D, 0x5A, 0x46, 0x46, 0x2D, 0x44, 0x4F, 0x47)) -Kind 'executable-shaped'
    }

    for ($index = 0; $index -lt $ItemCount; $index++) {
        $bucket = [int]([Math]::Floor($index / 1000))
        if (($index % 1000) -eq 0) {
            New-DogfoodDirectory -RelativePath ('07-bulk\bucket-{0:D4}' -f $bucket)
        }
        $kind = if (($index % 5) -eq 0) { 'bulk-text' } elseif (($index % 5) -eq 1) { 'bulk-log' } elseif (($index % 5) -eq 2) { 'bulk-data' } elseif (($index % 5) -eq 3) { 'bulk-multi-dot' } else { 'bulk-no-extension' }
        $extension = switch ($kind) {
            'bulk-text' { '.txt' }
            'bulk-log' { '.log' }
            'bulk-data' { '.dat' }
            'bulk-multi-dot' { '.part.one.bin' }
            default { '' }
        }
        $content = [Text.UTF8Encoding]::new($false).GetBytes(('FastFiles bulk item {0:D8}; profile={1}; kind={2}' -f $index, $Profile.ToLowerInvariant(), $kind))
        New-DogfoodFile -RelativePath ('07-bulk\bucket-{0:D4}\item-{1:D8}{2}' -f $bucket, $index, $extension) -Bytes $content -Kind $kind -LastWriteTimeUtc $fixedTime.AddMinutes($index % 1440)
    }

    if ($IncludeReparsePoints) {
        $junctionPath = Join-Path $rootFull '08-reparse\junction-to-small'
        $junctionTarget = Join-Path $rootFull '01-small'
        try {
            New-Item -ItemType Junction -Path $junctionPath -Target $junctionTarget | Out-Null
            Write-DogfoodManifestItem -State $writer -RelativePath '08-reparse\junction-to-small' -IsDirectory $true -Attributes 'ReparsePoint' -LastWriteTimeUtc $fixedTime.ToString('o') -Kind 'junction'
            $script:directoryCount++
        } catch {
            $warnings.Add('junction-unavailable: ' + $_.Exception.Message)
        }
    }
} finally {
    if ($null -ne $writer) { Close-DogfoodManifestWriter -State $writer }
}

$summary = [ordered]@{
    schema = 1
    root = $rootFull
    profile = $Profile.ToLowerInvariant()
    itemBudget = $ItemCount
    files = $fileCount
    directories = $directoryCount
    manifest = Join-Path $rootFull (Get-DogfoodManifestName)
    warnings = @($warnings.ToArray())
}
Write-DogfoodUtf8Text -Path (Join-Path $rootFull 'fixture-summary.json') -Text (($summary | ConvertTo-Json -Depth 4))

if ($OutputFormat -eq 'json') {
    $summary | ConvertTo-Json -Depth 4
} else {
    Write-Output ('Fixture created: ' + $rootFull)
    Write-Output ('Profile: ' + $Profile + '; bulk items: ' + $ItemCount + '; files: ' + $fileCount + '; directories: ' + $directoryCount)
    Write-Output ('Manifest: ' + $summary.manifest)
    if ($warnings.Count -gt 0) { Write-Output ('Warnings: ' + ($warnings -join '; ')) }
}
