Set-StrictMode -Version Latest

$script:DogfoodMarkerName = '.fastfiles-dogfood-root'
$script:DogfoodManifestName = 'fixture-manifest.jsonl'
$script:DogfoodSchemaVersion = 1

function Get-DogfoodMarkerName {
    return $script:DogfoodMarkerName
}

function Get-DogfoodManifestName {
    return $script:DogfoodManifestName
}

function ConvertTo-DogfoodFullPath {
    param(
        [Parameter(Mandatory)] [string] $Path
    )

    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw 'DOGFOOD_ROOT_REQUIRED'
    }

    return [IO.Path]::GetFullPath($Path).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
}

function Assert-DogfoodRootSafe {
    param(
        [Parameter(Mandatory)] [string] $Root,
        [switch] $RequireMarker
    )

    $fullRoot = ConvertTo-DogfoodFullPath -Path $Root
    $pathRoot = ([IO.Path]::GetPathRoot($fullRoot)).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    if ([string]::IsNullOrWhiteSpace($pathRoot) -or $fullRoot -eq $pathRoot) {
        throw "DOGFOOD_ROOT_TOO_BROAD: '$fullRoot' is a volume root."
    }

    $repoRoot = ConvertTo-DogfoodFullPath -Path (Join-Path $PSScriptRoot '..\..')
    if ($fullRoot.Equals($repoRoot, [StringComparison]::OrdinalIgnoreCase) -or
        $fullRoot.StartsWith($repoRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "DOGFOOD_ROOT_INSIDE_REPOSITORY: '$fullRoot'"
    }

    $markerPath = Join-Path $fullRoot $script:DogfoodMarkerName
    if ($RequireMarker) {
        if (-not (Test-Path -LiteralPath $markerPath -PathType Leaf)) {
            throw "DOGFOOD_ROOT_MARKER_REQUIRED: '$fullRoot'"
        }

        $marker = [IO.File]::ReadAllText($markerPath).Trim()
        if ($marker -ne 'FastFiles-Dogfood-Root-v1') {
            throw "DOGFOOD_ROOT_MARKER_INVALID: '$fullRoot'"
        }
    }

    return $fullRoot
}

function ConvertTo-DogfoodExtendedPath {
    param(
        [Parameter(Mandatory)] [string] $Path
    )

    $fullPath = ConvertTo-DogfoodFullPath -Path $Path
    if ($fullPath.StartsWith('\\?\', [StringComparison]::Ordinal)) {
        return $fullPath
    }

    if ($fullPath.StartsWith('\\', [StringComparison]::Ordinal)) {
        return '\\?\UNC\' + $fullPath.Substring(2)
    }

    return '\\?\' + $fullPath
}

function Write-DogfoodUtf8Text {
    param(
        [Parameter(Mandatory)] [string] $Path,
        [Parameter(Mandatory)] [string] $Text
    )

    $encoding = [Text.UTF8Encoding]::new($false)
    [IO.File]::WriteAllText($Path, $Text, $encoding)
}

function Get-DogfoodSha256 {
    param(
        [Parameter(Mandatory)] [string] $Path
    )

    $stream = $null
    $algorithm = $null
    try {
        $stream = [IO.File]::OpenRead($Path)
        $algorithm = [Security.Cryptography.SHA256]::Create()
        return ([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
    } finally {
        if ($null -ne $algorithm) { $algorithm.Dispose() }
        if ($null -ne $stream) { $stream.Dispose() }
    }
}

function Get-DogfoodRelativePath {
    param(
        [Parameter(Mandatory)] [string] $Root,
        [Parameter(Mandatory)] [string] $Path
    )

    $rootFull = ConvertTo-DogfoodFullPath -Path $Root
    $pathFull = ConvertTo-DogfoodFullPath -Path $Path
    return ([IO.Path]::GetRelativePath($rootFull, $pathFull)).Replace([IO.Path]::DirectorySeparatorChar, '\').Replace([IO.Path]::AltDirectorySeparatorChar, '\')
}

function New-DogfoodManifestWriter {
    param(
        [Parameter(Mandatory)] [string] $Root,
        [Parameter(Mandatory)] [ValidateSet('Small', 'Medium', 'Large')] [string] $Profile,
        [Parameter(Mandatory)] [int] $ItemBudget
    )

    $rootFull = Assert-DogfoodRootSafe -Root $Root
    $manifestPath = Join-Path $rootFull $script:DogfoodManifestName
    $stream = [IO.StreamWriter]::new($manifestPath, $false, [Text.UTF8Encoding]::new($false))
    $header = [ordered]@{
        schema = $script:DogfoodSchemaVersion
        generator = 'FastFiles dogfood fixture generator 1'
        profile = $Profile.ToLowerInvariant()
        itemBudget = $ItemBudget
        pathEncoding = 'UTF-8 JSON Lines; relative paths use backslash'
    }
    $stream.WriteLine(($header | ConvertTo-Json -Compress))
    return [pscustomobject]@{ Path = $manifestPath; Writer = $stream; Count = 0 }
}

function Write-DogfoodManifestItem {
    param(
        [Parameter(Mandatory)] $State,
        [Parameter(Mandatory)] [string] $RelativePath,
        [Parameter(Mandatory)] [bool] $IsDirectory,
        [UInt64] $Size = 0,
        [string] $Sha256,
        [string] $Attributes,
        [string] $LastWriteTimeUtc,
        [string] $Kind = 'fixture'
    )

    $entry = [ordered]@{
        relativePath = $RelativePath.Replace('/', '\')
        isDirectory = $IsDirectory
        size = $Size
        sha256 = if ($Sha256) { $Sha256.ToLowerInvariant() } else { $null }
        attributes = if ($Attributes) { $Attributes } else { $null }
        lastWriteTimeUtc = if ($LastWriteTimeUtc) { $LastWriteTimeUtc } else { $null }
        kind = $Kind
    }
    $State.Writer.WriteLine(($entry | ConvertTo-Json -Compress))
    $State.Count = [int]$State.Count + 1
}

function Close-DogfoodManifestWriter {
    param(
        [Parameter(Mandatory)] $State
    )

    $State.Writer.Flush()
    $State.Writer.Dispose()
}

function Get-DogfoodManifestEntries {
    param(
        [Parameter(Mandatory)] [string] $ManifestPath
    )

    if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
        throw "DOGFOOD_MANIFEST_MISSING: '$ManifestPath'"
    }

    $lines = Get-Content -LiteralPath $ManifestPath
    if ($lines.Count -lt 2) { throw "DOGFOOD_MANIFEST_EMPTY: '$ManifestPath'" }
    $header = $lines[0] | ConvertFrom-Json
    if ([int]$header.schema -ne $script:DogfoodSchemaVersion) {
        throw "DOGFOOD_MANIFEST_SCHEMA_UNSUPPORTED: $($header.schema)"
    }

    $entries = [Collections.Generic.List[object]]::new()
    foreach ($line in ($lines | Select-Object -Skip 1)) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        $entries.Add(($line | ConvertFrom-Json))
    }
    return $entries.ToArray()
}

function Get-DogfoodSnapshot {
    param(
        [Parameter(Mandatory)] [string] $Root,
        [switch] $HashFiles
    )

    $rootFull = Assert-DogfoodRootSafe -Root $Root -RequireMarker
    $controlNames = @($script:DogfoodMarkerName, $script:DogfoodManifestName, 'fixture-summary.json')
    $entries = [Collections.Generic.List[object]]::new()
    $items = @(Get-ChildItem -LiteralPath $rootFull -Recurse -Force -ErrorAction Stop)
    foreach ($item in $items) {
        if (([IO.Path]::GetDirectoryName($item.FullName) -eq $rootFull) -and $controlNames -contains $item.Name) { continue }
        $relativePath = Get-DogfoodRelativePath -Root $rootFull -Path $item.FullName
        $isDirectory = [bool]$item.PSIsContainer
        $hash = $null
        if (-not $isDirectory -and $HashFiles) {
            $hash = Get-DogfoodSha256 -Path $item.FullName
        }
        $entries.Add([pscustomobject]@{
            relativePath = $relativePath
            isDirectory = $isDirectory
            size = if ($isDirectory) { [UInt64]0 } else { [UInt64]$item.Length }
            sha256 = $hash
            attributes = ([IO.File]::GetAttributes($item.FullName)).ToString()
            lastWriteTimeUtc = $item.LastWriteTimeUtc.ToString('o')
        })
    }
    return $entries.ToArray()
}

function Compare-DogfoodEntries {
    param(
        [Parameter(Mandatory)] [object[]] $Expected,
        [Parameter(Mandatory)] [object[]] $Actual,
        [switch] $CompareMetadata
    )

    $expectedMap = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
    $actualMap = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $Expected) { $expectedMap[$entry.relativePath] = $entry }
    foreach ($entry in $Actual) { $actualMap[$entry.relativePath] = $entry }

    $missing = [Collections.Generic.List[string]]::new()
    $unexpected = [Collections.Generic.List[string]]::new()
    $changed = [Collections.Generic.List[string]]::new()
    foreach ($path in $expectedMap.Keys) {
        if (-not $actualMap.ContainsKey($path)) {
            $missing.Add($path)
            continue
        }
        $expectedEntry = $expectedMap[$path]
        $actualEntry = $actualMap[$path]
        $mismatch = ([bool]$expectedEntry.isDirectory -ne [bool]$actualEntry.isDirectory) -or
            ([UInt64]$expectedEntry.size -ne [UInt64]$actualEntry.size)
        if (-not $expectedEntry.isDirectory -and $expectedEntry.sha256 -and $actualEntry.sha256) {
            $mismatch = $mismatch -or ($expectedEntry.sha256.ToLowerInvariant() -ne $actualEntry.sha256.ToLowerInvariant())
        }
        if ($CompareMetadata -and $expectedEntry.attributes -and
            ($expectedEntry.attributes -ne $actualEntry.attributes)) {
            $mismatch = $true
        }
        if ($mismatch) { $changed.Add($path) }
    }
    foreach ($path in $actualMap.Keys) {
        if (-not $expectedMap.ContainsKey($path)) { $unexpected.Add($path) }
    }

    return [pscustomobject]@{
        passed = ($missing.Count -eq 0 -and $unexpected.Count -eq 0 -and $changed.Count -eq 0)
        expectedCount = $expectedMap.Count
        actualCount = $actualMap.Count
        missing = @($missing.ToArray())
        unexpected = @($unexpected.ToArray())
        changed = @($changed.ToArray())
    }
}

function Remove-DogfoodRoot {
    param(
        [Parameter(Mandatory)] [string] $Root
    )

    $fullRoot = Assert-DogfoodRootSafe -Root $Root -RequireMarker
    Remove-Item -LiteralPath $fullRoot -Recurse -Force -ErrorAction Stop
}

Export-ModuleMember -Function @(
    'Assert-DogfoodRootSafe',
    'Close-DogfoodManifestWriter',
    'Compare-DogfoodEntries',
    'ConvertTo-DogfoodExtendedPath',
    'ConvertTo-DogfoodFullPath',
    'Get-DogfoodManifestEntries',
    'Get-DogfoodManifestName',
    'Get-DogfoodMarkerName',
    'Get-DogfoodRelativePath',
    'Get-DogfoodSha256',
    'Get-DogfoodSnapshot',
    'New-DogfoodManifestWriter',
    'Remove-DogfoodRoot',
    'Write-DogfoodManifestItem',
    'Write-DogfoodUtf8Text'
)
