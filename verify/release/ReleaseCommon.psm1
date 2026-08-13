Set-StrictMode -Version Latest

$script:ReleaseArtifactDefinitions = @(
    [pscustomobject]@{ Name = 'FastFiles.exe'; PackagePath = 'FastFiles.exe'; BuildPath = 'src\ui\FastFiles.exe'; Pin = $null },
    [pscustomobject]@{ Name = 'FastFilesEngine.exe'; PackagePath = 'FastFilesEngine.exe'; BuildPath = 'src\engine\FastFilesEngine.exe'; Pin = 'engine' },
    [pscustomobject]@{ Name = 'FastFilesIndexSvc.exe'; PackagePath = 'FastFilesIndexSvc.exe'; BuildPath = 'src\indexsvc\FastFilesIndexSvc.exe'; Pin = 'service' },
    [pscustomobject]@{ Name = 'FastFilesSetup.exe'; PackagePath = 'FastFilesSetup.exe'; BuildPath = 'src\installer\FastFilesSetup.exe'; Pin = $null }
)

function Normalize-Thumbprint {
    param([AllowNull()] [string] $Value)

    if ($null -eq $Value) { return $null }
    $normalized = ($Value -replace '[\s-]', '').ToUpperInvariant()
    if ($normalized.Length -eq 0) { return $null }
    return $normalized
}

function Test-ThumbprintFormat {
    param([AllowNull()] [string] $Value)

    $normalized = Normalize-Thumbprint -Value $Value
    return $null -ne $normalized -and $normalized -match '^[0-9A-F]{40}$'
}

function Test-UsableThumbprint {
    param([AllowNull()] [string] $Value)

    $normalized = Normalize-Thumbprint -Value $Value
    return Test-ThumbprintFormat -Value $normalized -and $normalized -notmatch '^0+$'
}

function Test-ProductionCertificate {
    param([AllowNull()] $Certificate)

    return $null -ne $Certificate -and -not [string]::IsNullOrWhiteSpace($Certificate.Subject) -and
        -not [string]::IsNullOrWhiteSpace($Certificate.Issuer) -and $Certificate.Subject -ne $Certificate.Issuer
}

function Get-PinState {
    param(
        [Parameter(Mandatory)] [string] $Name,
        [AllowNull()] [string] $Value,
        [string] $Source = 'unknown'
    )

    $normalized = Normalize-Thumbprint -Value $Value
    $state = if ($null -eq $normalized) {
        'MISSING'
    } elseif ($normalized -match '^0+$') {
        'PLACEHOLDER'
    } elseif ($normalized -notmatch '^[0-9A-F]{40}$') {
        'MALFORMED'
    } else {
        'POPULATED'
    }
    [pscustomobject]@{
        name = $Name
        state = $state
        source = $Source
        length = if ($null -eq $normalized) { 0 } else { $normalized.Length }
        normalized = $normalized
    }
}

function Get-CMakeCacheValue {
    param([Parameter(Mandatory)] [string] $CachePath, [Parameter(Mandatory)] [string] $Name)

    if (-not (Test-Path -LiteralPath $CachePath)) { return $null }
    $prefix = $Name + ':'
    $line = Get-Content -LiteralPath $CachePath | Where-Object { $_.StartsWith($prefix, [StringComparison]::Ordinal) } | Select-Object -First 1
    if ($null -eq $line) { return $null }
    $separator = $line.IndexOf('=')
    if ($separator -lt 0) { return $null }
    return $line.Substring($separator + 1).Trim()
}

function Get-ConfiguredPinStates {
    param(
        [Parameter(Mandatory)] [string] $RepoRoot,
        [string] $BuildRoot
    )

    if (-not $BuildRoot) { $BuildRoot = Join-Path $RepoRoot 'build\release' }
    $cachePath = [IO.Path]::GetFullPath((Join-Path $BuildRoot 'CMakeCache.txt'))
    $engine = Get-PinState -Name 'engine' -Value (Get-CMakeCacheValue -CachePath $cachePath -Name 'FASTFILES_ENGINE_SIGNATURE_THUMBPRINT') -Source $cachePath
    $service = Get-PinState -Name 'service' -Value (Get-CMakeCacheValue -CachePath $cachePath -Name 'FASTFILES_INDEX_SVC_SIGNATURE_THUMBPRINT') -Source $cachePath
    [pscustomobject]@{
        engine = $engine
        service = $service
        buildRoot = [IO.Path]::GetFullPath($BuildRoot)
        cachePresent = Test-Path -LiteralPath $cachePath
    }
}

function Get-ReleaseArtifactPaths {
    param([Parameter(Mandatory)] [string] $ArtifactRoot)

    $root = [IO.Path]::GetFullPath($ArtifactRoot)
    $packageLayout = @($script:ReleaseArtifactDefinitions | ForEach-Object { Join-Path $root $_.Name })
    $buildLayout = @($script:ReleaseArtifactDefinitions | ForEach-Object { Join-Path $root $_.BuildPath })
    $layout = if (@($packageLayout | Where-Object { -not (Test-Path -LiteralPath $_) }).Count -eq 0) {
        'package'
    } elseif (@($buildLayout | Where-Object { -not (Test-Path -LiteralPath $_) }).Count -eq 0) {
        'build'
    } else {
        throw "Release artifact root is incomplete. Expected either the four package files directly under '$root' or the Release build layout under '$root'."
    }

    $index = 0
    return @($script:ReleaseArtifactDefinitions | ForEach-Object {
        $path = if ($layout -eq 'package') { Join-Path $root $_.Name } else { Join-Path $root $_.BuildPath }
        $record = [pscustomobject]@{
            order = $index
            name = $_.Name
            packagePath = $_.PackagePath
            sourcePath = [IO.Path]::GetFullPath($path)
            pin = $_.Pin
        }
        $index++
        $record
    })
}

function Get-PeArchitecture {
    param([Parameter(Mandatory)] [string] $Path)

    $fullPath = [IO.Path]::GetFullPath($Path)
    $stream = $null
    $reader = $null
    try {
        $stream = [IO.File]::OpenRead($fullPath)
        if ($stream.Length -lt 0x40) { throw "PE image is too small: $fullPath" }
        $reader = [IO.BinaryReader]::new($stream)
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw "Missing DOS header: $fullPath" }
        $stream.Seek(0x3c, [IO.SeekOrigin]::Begin) | Out-Null
        $peOffset = $reader.ReadInt32()
        if ($peOffset -lt 0 -or $peOffset -gt $stream.Length - 6) { throw "Invalid PE header offset: $fullPath" }
        $stream.Seek($peOffset, [IO.SeekOrigin]::Begin) | Out-Null
        if ($reader.ReadUInt32() -ne 0x00004550) { throw "Missing PE signature: $fullPath" }
        $machine = $reader.ReadUInt16()
        $architecture = switch ($machine) {
            0x8664 { 'x64' }
            0xAA64 { 'arm64' }
            0x014c { 'x86' }
            default { 'unknown' }
        }
        [pscustomobject]@{ architecture = $architecture; machine = ('0x{0:X4}' -f $machine) }
    } finally {
        if ($reader) { $reader.Dispose() }
        elseif ($stream) { $stream.Dispose() }
    }
}

function Get-SignatureRecord {
    param([Parameter(Mandatory)] [string] $Path)

    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    $signer = $signature.SignerCertificate
    $timestamp = $signature.TimeStamperCertificate
    $isSigned = $signature.Status -ne 'NotSigned' -and $null -ne $signer
    [pscustomobject]@{
        signed = [bool]$isSigned
        valid = [bool]($signature.Status -eq 'Valid')
        status = [string]$signature.Status
        signerSubject = if ($signer) { [string]$signer.Subject } else { $null }
        leafThumbprint = if ($signer) { Normalize-Thumbprint -Value $signer.Thumbprint } else { $null }
        timestampSigned = $null -ne $timestamp
        timestampValid = if ($timestamp) { [bool]($signature.Status -eq 'Valid') } else { $null }
        timestampSubject = if ($timestamp) { [string]$timestamp.Subject } else { $null }
        timestampThumbprint = if ($timestamp) { Normalize-Thumbprint -Value $timestamp.Thumbprint } else { $null }
    }
}

function Get-FileVersionRecord {
    param([Parameter(Mandatory)] [string] $Path)

    $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($Path)
    [pscustomobject]@{
        productVersion = if ([string]::IsNullOrWhiteSpace($info.ProductVersion)) { $null } else { $info.ProductVersion }
        fileVersion = if ([string]::IsNullOrWhiteSpace($info.FileVersion)) { $null } else { $info.FileVersion }
    }
}

function Get-RepositoryCommit {
    param([Parameter(Mandatory)] [string] $RepoRoot)

    try {
        $commit = git -C $RepoRoot rev-parse HEAD 2>$null
        if ($LASTEXITCODE -eq 0 -and $commit) { return ([string](@($commit) | Select-Object -First 1)).Trim() }
    } catch { }
    return $null
}

function Get-ReleaseArtifactManifest {
    param(
        [Parameter(Mandatory)] [string] $ArtifactRoot,
        [string] $RepoRoot,
        [string] $Configuration = 'Release'
    )

    $paths = Get-ReleaseArtifactPaths -ArtifactRoot $ArtifactRoot
    $records = @($paths | Sort-Object order | ForEach-Object {
        $architecture = Get-PeArchitecture -Path $_.sourcePath
        $signature = Get-SignatureRecord -Path $_.sourcePath
        $version = Get-FileVersionRecord -Path $_.sourcePath
        [pscustomobject]@{
            path = $_.packagePath.Replace('\', '/')
            filename = $_.name
            size = (Get-Item -LiteralPath $_.sourcePath).Length
            sha256 = (Get-FileHash -LiteralPath $_.sourcePath -Algorithm SHA256).Hash.ToUpperInvariant()
            architecture = $architecture.architecture
            peMachine = $architecture.machine
            productVersion = $version.productVersion
            fileVersion = $version.fileVersion
            signature = $signature
        }
    })
    [pscustomobject]@{
        schemaVersion = 1
        kind = 'fastfiles-release-artifact-manifest'
        commit = if ($RepoRoot) { Get-RepositoryCommit -RepoRoot $RepoRoot } else { $null }
        configuration = $Configuration
        expectedArchitecture = 'x64'
        artifacts = $records
    }
}

function Write-PackageManifest {
    param([Parameter(Mandatory)] $Manifest, [Parameter(Mandatory)] [string] $Path)

    $version = @($Manifest.artifacts | Where-Object { -not [string]::IsNullOrWhiteSpace($_.productVersion) } |
        Select-Object -ExpandProperty productVersion -First 1)
    if (-not $version) { $version = '0.0.0-unsigned-build' }
    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add('FASTFILES_PACKAGE_MANIFEST_V1')
    $lines.Add('version=1')
    $lines.Add(('productVersion={0}' -f ([string]$version)))
    foreach ($artifact in @($Manifest.artifacts | Sort-Object filename)) {
        if ([string]::IsNullOrWhiteSpace($artifact.filename) -or [string]::IsNullOrWhiteSpace($artifact.sha256)) {
            throw "PACKAGE_MANIFEST_ARTIFACT_INVALID: $($artifact.filename)"
        }
        $lines.Add(('file={0}|{1}|{2}' -f $artifact.filename, [uint64]$artifact.size, ([string]$artifact.sha256).ToUpperInvariant()))
    }
    $parent = Split-Path -Parent ([IO.Path]::GetFullPath($Path))
    if ($parent) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    $encoding = [Text.UTF8Encoding]::new($false)
    [IO.File]::WriteAllText([IO.Path]::GetFullPath($Path), (($lines -join "`n") + "`n"), $encoding)
}

function Test-PackageManifest {
    param(
        [Parameter(Mandatory)] [string] $Path,
        [Parameter(Mandatory)] $ArtifactManifest,
        [string] $PackageRoot
    )

    $fullPath = [IO.Path]::GetFullPath($Path)
    try {
        if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
            return [pscustomobject]@{ valid = $false; reason = 'PACKAGE_MANIFEST_MISSING'; entries = @() }
        }
        $lines = [IO.File]::ReadAllLines($fullPath, [Text.UTF8Encoding]::new($false))
        if ($lines.Count -lt 4 -or $lines[0] -cne 'FASTFILES_PACKAGE_MANIFEST_V1' -or $lines[1] -cne 'version=1') {
            return [pscustomobject]@{ valid = $false; reason = 'PACKAGE_MANIFEST_HEADER_INVALID'; entries = @() }
        }
        if ($lines[2] -notmatch '^productVersion=(.+)$' -or [string]::IsNullOrWhiteSpace($Matches[1]) -or $Matches[1].Contains('|')) {
            return [pscustomobject]@{ valid = $false; reason = 'PACKAGE_MANIFEST_VERSION_INVALID'; entries = @() }
        }
        $productVersion = $Matches[1]

        $entries = [System.Collections.Generic.List[object]]::new()
        $seen = @{}
        for ($index = 3; $index -lt $lines.Count; $index++) {
            if ($lines[$index] -notmatch '^file=([^|]+)\|([0-9]+)\|([0-9A-Fa-f]{64})$') {
                return [pscustomobject]@{ valid = $false; reason = 'PACKAGE_MANIFEST_FILE_ROW_INVALID'; entries = @($entries) }
            }
            $name = $Matches[1]
            if ($seen.ContainsKey($name)) {
                return [pscustomobject]@{ valid = $false; reason = 'PACKAGE_MANIFEST_DUPLICATE_FILE'; entries = @($entries) }
            }
            $seen[$name] = $true
            $entries.Add([pscustomobject]@{
                    filename = $name
                    size = [uint64]$Matches[2]
                    sha256 = $Matches[3].ToUpperInvariant()
                })
        }

        $expected = @($ArtifactManifest.artifacts | Sort-Object filename)
        if ($entries.Count -ne $expected.Count -or $entries.Count -eq 0) {
            return [pscustomobject]@{ valid = $false; reason = 'PACKAGE_MANIFEST_INVENTORY_MISMATCH'; entries = @($entries) }
        }
        for ($index = 0; $index -lt $expected.Count; $index++) {
            if ($entries[$index].filename -cne [string]$expected[$index].filename -or
                $entries[$index].size -ne [uint64]$expected[$index].size -or
                $entries[$index].sha256 -cne ([string]$expected[$index].sha256).ToUpperInvariant()) {
                return [pscustomobject]@{ valid = $false; reason = 'PACKAGE_MANIFEST_HASH_MISMATCH'; entries = @($entries) }
            }
            if ($PackageRoot) {
                $artifactPath = Join-Path ([IO.Path]::GetFullPath($PackageRoot)) $entries[$index].filename
                if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf)) {
                    return [pscustomobject]@{ valid = $false; reason = 'PACKAGE_ARTIFACT_MISSING'; entries = @($entries) }
                }
                $artifact = Get-Item -LiteralPath $artifactPath
                $actualHash = (Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash.ToUpperInvariant()
                if ([uint64]$artifact.Length -ne $entries[$index].size -or $actualHash -cne $entries[$index].sha256) {
                    return [pscustomobject]@{ valid = $false; reason = 'PACKAGE_ARTIFACT_HASH_MISMATCH'; entries = @($entries) }
                }
            }
        }
        [pscustomobject]@{ valid = $true; reason = $null; productVersion = $productVersion; entries = @($entries) }
    } catch {
        [pscustomobject]@{ valid = $false; reason = 'PACKAGE_MANIFEST_PARSE_ERROR'; entries = @(); error = $_.Exception.Message }
    }
}

function Get-SafePinState {
    param([Parameter(Mandatory)] $PinState)

    [pscustomobject]@{
        name = $PinState.name
        state = $PinState.state
        source = $PinState.source
        length = $PinState.length
    }
}

function Get-ReleaseSignatureMatrix {
    param(
        [Parameter(Mandatory)] $Manifest,
        [Parameter(Mandatory)] $PinStates,
        [AllowNull()] [string] $ExpectedSignerThumbprint
    )

    $expected = Normalize-Thumbprint -Value $ExpectedSignerThumbprint
    $expectedUsable = Test-UsableThumbprint -Value $expected
    $rows = @($Manifest.artifacts | ForEach-Object {
        $pinState = if ($_.filename -eq 'FastFilesEngine.exe') { $PinStates.engine } elseif ($_.filename -eq 'FastFilesIndexSvc.exe') { $PinStates.service } else { $null }
        $expectedPinned = if ($expected) { [bool]($_.signature.leafThumbprint -and $_.signature.leafThumbprint -eq $expected) } else { $null }
        $reason = $null
        $result = 'PASS'
        if (-not $_.signature.signed) {
            $result = 'UNSIGNED — PRODUCTION SIGNING REQUIRED'
        } elseif (-not $_.signature.valid) {
            $result = 'SIGNATURE_INVALID'
        } elseif ($ExpectedSignerThumbprint -and -not $expectedUsable) {
            $result = 'MALFORMED_EXPECTED_SIGNER'
        } elseif (-not $expected) {
            $result = 'BLOCKED_EXTERNAL — EXPECTED_SIGNER_NOT_CONFIGURED'
        } elseif (-not $expectedPinned) {
            $result = 'WRONG_SIGNER'
        } elseif ($pinState -and $pinState.state -eq 'PLACEHOLDER') {
            $result = 'PLACEHOLDER_PIN'
        } elseif ($pinState -and $pinState.state -eq 'MALFORMED') {
            $result = 'MALFORMED_PIN'
        } elseif ($pinState -and $pinState.state -eq 'MISSING') {
            $result = 'MISSING_PIN'
        } elseif ($pinState -and $pinState.normalized -ne $_.signature.leafThumbprint) {
            $result = 'PIN_MISMATCH'
        }
        [pscustomobject]@{
            artifact = $_.filename
            signed = $_.signature.signed
            signatureValid = $_.signature.valid
            signerSubject = $_.signature.signerSubject
            leafThumbprint = $_.signature.leafThumbprint
            expectedPinned = $expectedPinned
            timestampSigned = $_.signature.timestampSigned
            timestampValid = $_.signature.timestampValid
            result = $result
            pinState = if ($pinState) { Get-SafePinState -PinState $pinState } else { $null }
        }
    })
    return $rows
}

function Get-FastFilesProcessRecords {
    $names = @('FastFiles.exe', 'FastFilesEngine.exe', 'FastFilesIndexSvc.exe', 'FastFilesSetup.exe')
    if (-not $IsWindows) { return @() }
    $records = @()
    foreach ($process in @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object { $names -contains $_.Name })) {
        $records += [pscustomobject]@{
            name = $process.Name
            processId = [int]$process.ProcessId
            parentProcessId = [int]$process.ParentProcessId
            path = $process.ExecutablePath
            pathKnown = $null -ne $process.ExecutablePath
        }
    }
    return $records
}

function Get-CleanHostPreflight {
    $service = if ($IsWindows) { Get-CimInstance Win32_Service -Filter "Name='FastFilesIndexSvc'" -ErrorAction SilentlyContinue } else { $null }
    $task = if ($IsWindows) { Get-ScheduledTask -TaskPath '\FastFiles\' -TaskName 'FastFilesEngine' -ErrorAction SilentlyContinue } else { $null }
    $group = if ($IsWindows) { Get-LocalGroup -Name 'FastFilesUsers' -ErrorAction SilentlyContinue } else { $null }
    $programFiles = if ($env:ProgramFiles) { Join-Path $env:ProgramFiles 'FastFiles' } else { $null }
    $programData = if ($env:ProgramData) { Join-Path $env:ProgramData 'FastFiles' } else { $null }
    $localData = if ($env:LOCALAPPDATA) { Join-Path $env:LOCALAPPDATA 'FastFiles' } else { $null }
    $registry = if ($IsWindows) { Test-Path -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\FastFiles' } else { $false }
    $processes = @(Get-FastFilesProcessRecords)
    $resources = [ordered]@{
        service = [pscustomobject]@{ present = $null -ne $service; state = if ($service) { $service.State } else { $null }; startName = if ($service) { $service.StartName } else { $null }; path = if ($service) { $service.PathName } else { $null } }
        processes = $processes
        scheduledTask = [pscustomobject]@{ present = $null -ne $task; path = '\FastFiles\FastFilesEngine'; action = if ($task) { @($task.Actions.Execute) } else { @() } }
        clientGroup = [pscustomobject]@{ present = $null -ne $group; name = 'FastFilesUsers' }
        installDirectory = [pscustomobject]@{ present = if ($programFiles) { Test-Path -LiteralPath $programFiles } else { $false }; path = $programFiles }
        programData = [pscustomobject]@{ present = if ($programData) { Test-Path -LiteralPath $programData } else { $false }; path = $programData }
        userData = [pscustomobject]@{ present = if ($localData) { Test-Path -LiteralPath $localData } else { $false }; path = $localData }
        uninstallRegistry = [pscustomobject]@{ present = $registry; path = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\FastFiles' }
    }
    $contamination = @()
    foreach ($entry in $resources.GetEnumerator()) {
        if ($entry.Key -eq 'processes') {
            if (@($entry.Value).Count -gt 0) { $contamination += 'processes' }
        } elseif ($entry.Value.present) {
            $contamination += $entry.Key
        }
    }
    $os = if ($IsWindows) { Get-CimInstance Win32_OperatingSystem -ErrorAction SilentlyContinue } else { $null }
    [pscustomobject]@{
        schemaVersion = 1
        checkedAtUtc = [DateTime]::UtcNow.ToString('o')
        os = [pscustomobject]@{ caption = if ($os) { $os.Caption } else { $null }; version = if ($os) { $os.Version } else { $null }; build = if ($os) { $os.BuildNumber } else { $null }; architecture = $env:PROCESSOR_ARCHITECTURE }
        elevated = if ($IsWindows) { ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) } else { $false }
        resources = $resources
        contamination = @($contamination | Sort-Object -Unique)
        clean = @($contamination).Count -eq 0
        expectedArchitecture = 'x64'
        architectureCompatible = $env:PROCESSOR_ARCHITECTURE -in @('AMD64', 'ARM64')
        trustStore = [pscustomobject]@{ checked = $false; policy = 'The kit does not install or modify trust-store entries; production certificate provenance is validated from artifact signatures.' }
    }
}

function Write-ReleaseJson {
    param([Parameter(Mandatory)] $Value, [Parameter(Mandatory)] [string] $Path)

    $parent = Split-Path -Parent ([IO.Path]::GetFullPath($Path))
    if ($parent) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    $Value | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $Path -Encoding utf8NoBOM
}

Export-ModuleMember -Function @(
    'Normalize-Thumbprint',
    'Test-ThumbprintFormat',
    'Test-UsableThumbprint',
    'Test-ProductionCertificate',
    'Get-PinState',
    'Get-ConfiguredPinStates',
    'Get-ReleaseArtifactPaths',
    'Get-PeArchitecture',
    'Get-SignatureRecord',
    'Get-ReleaseArtifactManifest',
    'Write-PackageManifest',
    'Test-PackageManifest',
    'Get-SafePinState',
    'Get-ReleaseSignatureMatrix',
    'Get-CleanHostPreflight',
    'Write-ReleaseJson'
)
