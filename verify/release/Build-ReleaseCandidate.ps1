<#
.SYNOPSIS
    Builds a deterministic FastFiles release-candidate kit and provenance bundle.

.DESCRIPTION
    The unsigned-validation mode is the portable developer path. It builds the
    explicitly non-production release-test configuration, runs the repository
    build gate, validates VERSIONINFO/package identity, and emits a labelled kit,
    deterministic archive, checksums, dependency inventory, and provenance.

    The production mode consumes a release build that was signed by the existing
    authorized signing workflow. It never treats unsigned binaries, missing pins,
    or a missing detached package-manifest signature as a production candidate.
    Signing credentials are intentionally not accepted through this wrapper;
    verify/release/sign-release.ps1 remains the narrow external signing seam.
#>

[CmdletBinding()]
param(
    [ValidateSet('unsigned-validation', 'production')]
    [string] $Mode = 'unsigned-validation',
    [string] $BuildRoot,
    [string] $OutputDirectory,
    [switch] $Force,
    [switch] $SkipVerification,
    [string] $ManifestSignaturePath,
    [string] $ExpectedSignerThumbprint,
    [ValidateSet('human', 'json')]
    [string] $OutputFormat = 'human'
)

$ErrorActionPreference = 'Stop'
$releaseRoot = $PSScriptRoot
$verifyRoot = Split-Path $releaseRoot -Parent
$repoRoot = Split-Path $verifyRoot -Parent
Import-Module (Join-Path $releaseRoot 'ReleaseCommon.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $verifyRoot 'core\Provenance.psm1') -Force -DisableNameChecking

$script:markerName = '.fastfiles-release-candidate-v1'
$script:markerText = 'FastFiles-Release-Candidate-v1'

function Get-GitText {
    param([Parameter(Mandatory)] [string[]] $Arguments)

    $value = & git -C $repoRoot @Arguments 2>$null
    if ($LASTEXITCODE -ne 0) { throw "GIT_COMMAND_FAILED: git $($Arguments -join ' ')" }
    return ([string](@($value) -join "`n")).Trim()
}

function Get-GitHeadBlobText {
    param([Parameter(Mandatory)] [string] $RepositoryPath)

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = (Get-Command git -ErrorAction Stop).Source
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($argument in @('-C', $repoRoot, 'cat-file', 'blob', "HEAD:$RepositoryPath")) {
        [void]$startInfo.ArgumentList.Add($argument)
    }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    $bytes = $null
    try {
        if (-not $process.Start()) { throw "GIT_BLOB_READ_FAILED: $RepositoryPath" }
        $memory = [IO.MemoryStream]::new()
        try { $process.StandardOutput.BaseStream.CopyTo($memory); $bytes = $memory.ToArray() }
        finally { $memory.Dispose() }
        $errorText = $process.StandardError.ReadToEnd()
        $process.WaitForExit()
        if ($process.ExitCode -ne 0) { throw "GIT_BLOB_READ_FAILED: $RepositoryPath $errorText" }
        $text = [Text.UTF8Encoding]::new($false, $true).GetString($bytes)
        if ($text.Length -gt 0 -and $text[0] -eq [char]0xFEFF) { $text = $text.Substring(1) }
        return $text
    } finally {
        $process.Dispose()
    }
}

function New-CandidateArchiveSnapshot {
    param(
        [Parameter(Mandatory)] [string] $Root,
        [Parameter(Mandatory)] [string] $ArchivePath,
        [Parameter(Mandatory)] [string] $MarkerPath
    )

    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    $snapshotRoot = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-candidate-archive-' + [guid]::NewGuid().ToString('N'))
    $guards = [System.Collections.Generic.List[IO.FileStream]]::new()
    try {
        New-Item -ItemType Directory -Path $snapshotRoot -Force | Out-Null
        $excluded = @([IO.Path]::GetFullPath($ArchivePath), [IO.Path]::GetFullPath($MarkerPath))
        $items = @(Get-ChildItem -LiteralPath $rootFull -Recurse -Force -ErrorAction Stop)
        foreach ($item in $items) {
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "CANDIDATE_ARCHIVE_REPARSE_INPUT: $($item.FullName)"
            }
            if ($item.PSIsContainer) { continue }
            $sourcePath = [IO.Path]::GetFullPath($item.FullName)
            if ($sourcePath -in $excluded) { continue }
            $relativePath = $sourcePath.Substring($rootFull.Length).TrimStart('\')
            if ([string]::IsNullOrWhiteSpace($relativePath) -or $relativePath -match '(^|[\\/])\.\.?([\\/]|$)') {
                throw "CANDIDATE_ARCHIVE_PATH_INVALID: $relativePath"
            }
            $snapshotPath = Join-Path $snapshotRoot $relativePath
            New-Item -ItemType Directory -Path (Split-Path -Parent $snapshotPath) -Force | Out-Null
            $sourceStream = $null
            $snapshotStream = $null
            try {
                $sourceStream = [FastFilesReleaseArtifactReadFile]::OpenRegularReadOnly($sourcePath)
                $snapshotStream = [IO.File]::Open($snapshotPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
                $sourceStream.CopyTo($snapshotStream)
                $snapshotStream.Flush($true)
            } finally {
                if ($snapshotStream) { $snapshotStream.Dispose() }
                if ($sourceStream) { $sourceStream.Dispose() }
            }
        }
        foreach ($file in @(Get-ChildItem -LiteralPath $snapshotRoot -Recurse -File -Force -ErrorAction Stop)) {
            $guards.Add([FastFilesReleaseArtifactReadFile]::OpenRegularReadOnly($file.FullName))
        }
        return [pscustomobject]@{ root = $snapshotRoot; guards = $guards }
    } catch {
        foreach ($guard in $guards) { $guard.Dispose() }
        $snapshotFull = [IO.Path]::GetFullPath($snapshotRoot).TrimEnd('\')
        $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
        if ($snapshotFull.StartsWith($tempRoot + '\', [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $snapshotFull)) {
            Remove-Item -LiteralPath $snapshotFull -Recurse -Force -ErrorAction SilentlyContinue
        }
        throw
    }
}

function Remove-CandidateArchiveSnapshot {
    param([Parameter(Mandatory)] $Snapshot)

    foreach ($guard in @($Snapshot.guards)) { if ($guard) { $guard.Dispose() } }
    $snapshotFull = [IO.Path]::GetFullPath([string]$Snapshot.root).TrimEnd('\')
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
    if (-not $snapshotFull.StartsWith($tempRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "CANDIDATE_ARCHIVE_SNAPSHOT_ESCAPE: $snapshotFull"
    }
    if (Test-Path -LiteralPath $snapshotFull) { Remove-Item -LiteralPath $snapshotFull -Recurse -Force }
}

function Get-ReleaseSignatureEvidenceProjection {
    param([AllowNull()] $Signature)

    if ($null -eq $Signature) { return $null }
    $policy = $Signature.certificatePolicy
    [ordered]@{
        signed = [bool]$Signature.signed
        valid = [bool]$Signature.valid
        status = [string]$Signature.status
        signerSubject = [string]$Signature.signerSubject
        leafThumbprint = [string]$Signature.leafThumbprint
        verificationPolicy = [string]$Signature.verificationPolicy
        timestampSigned = [bool]$Signature.timestampSigned
        timestampValid = $Signature.timestampValid
        timestampStatus = [string]$Signature.timestampStatus
        timestampSubject = [string]$Signature.timestampSubject
        timestampThumbprint = [string]$Signature.timestampThumbprint
        certificatePolicy = if ($null -eq $policy) { $null } else {
            [ordered]@{
                valid = [bool]$policy.valid
                reasons = @($policy.reasons)
                authenticodeStatus = [string]$policy.authenticodeStatus
                authenticodeValid = [bool]$policy.authenticodeValid
                signerKnown = [bool]$policy.signerKnown
                signerThumbprint = [string]$policy.signerThumbprint
                selfSigned = [bool]$policy.selfSigned
                currentValidityEnforced = [bool]$policy.currentValidityEnforced
                notBefore = $policy.notBefore
                notAfter = $policy.notAfter
                codeSigningEku = [bool]$policy.codeSigningEku
                chainValiditySource = [string]$policy.chainValiditySource
                timestampSigned = [bool]$policy.timestampSigned
                timestampValid = [bool]$policy.timestampValid
                timestampPolicy = [string]$policy.timestampPolicy
                revocationPolicy = [string]$policy.revocationPolicy
                revocationChecked = $policy.revocationChecked
            }
        }
    }
}

function Assert-ArtifactManifestMatchesPackage {
    param(
        [Parameter(Mandatory)] $Manifest,
        [Parameter(Mandatory)] [string] $PackageRoot,
        [Parameter(Mandatory)] [string] $ExpectedConfiguration,
        [Parameter(Mandatory)] [string] $ExpectedCommit
    )

    $computed = Get-ReleaseArtifactManifest -ArtifactRoot $PackageRoot -RepoRoot $repoRoot -Configuration $ExpectedConfiguration
    if ([string]$Manifest.kind -cne [string]$computed.kind -or
        [int]$Manifest.schemaVersion -ne [int]$computed.schemaVersion -or
        [string]$Manifest.commit -ine $ExpectedCommit -or [string]$Manifest.commit -ine [string]$computed.commit -or
        [string]$Manifest.configuration -cne [string]$computed.configuration -or
        [string]$Manifest.productName -cne [string]$computed.productName -or
        [string]$Manifest.expectedArchitecture -cne [string]$computed.expectedArchitecture) {
        throw 'CANDIDATE_ARTIFACT_MANIFEST_IDENTITY_MISMATCH'
    }
    $manifestInventory = @($Manifest.expectedComponentInventory | ForEach-Object { [string]$_ })
    $computedInventory = @($computed.expectedComponentInventory | ForEach-Object { [string]$_ })
    if (($manifestInventory -join '|') -cne ($computedInventory -join '|')) {
        throw 'CANDIDATE_ARTIFACT_MANIFEST_INVENTORY_MISMATCH'
    }
    if (@($Manifest.artifacts).Count -ne @($computed.artifacts).Count) {
        throw 'CANDIDATE_ARTIFACT_MANIFEST_RECORD_COUNT_MISMATCH'
    }
    $metadataFields = @('path', 'filename', 'size', 'sha256', 'architecture', 'peMachine', 'productVersion', 'fileVersion',
        'productName', 'companyName', 'fileDescription', 'internalName', 'originalFilename', 'buildIdentity', 'configuration', 'componentRole')
    foreach ($computedArtifact in @($computed.artifacts)) {
        $manifestArtifact = @($Manifest.artifacts | Where-Object { $_.filename -ceq $computedArtifact.filename }) | Select-Object -First 1
        if ($null -eq $manifestArtifact) { throw "CANDIDATE_ARTIFACT_MANIFEST_RECORD_MISSING: $($computedArtifact.filename)" }
        foreach ($field in $metadataFields) {
            if ([string]$manifestArtifact.$field -cne [string]$computedArtifact.$field) {
                throw "CANDIDATE_ARTIFACT_MANIFEST_FIELD_MISMATCH: $($computedArtifact.filename):$field"
            }
        }
        $manifestSignature = ConvertTo-Json -InputObject (Get-ReleaseSignatureEvidenceProjection -Signature $manifestArtifact.signature) -Depth 12 -Compress
        $computedSignature = ConvertTo-Json -InputObject (Get-ReleaseSignatureEvidenceProjection -Signature $computedArtifact.signature) -Depth 12 -Compress
        if ($manifestSignature -cne $computedSignature) {
            throw "CANDIDATE_ARTIFACT_MANIFEST_SIGNATURE_MISMATCH: $($computedArtifact.filename)"
        }
    }
    return $computed
}

function Assert-CleanRepository {
    $status = @(git -C $repoRoot status --porcelain=v1 2>$null)
    if ($LASTEXITCODE -ne 0) { throw 'GIT_STATUS_FAILED: could not inspect repository cleanliness.' }
    if ($status.Count -gt 0) {
        throw "RELEASE_CLEAN_TREE_REQUIRED: release candidates require a clean checkout; found $($status.Count) changed path(s)."
    }
}

function Assert-CandidateSourceUnchanged {
    param([Parameter(Mandatory)] $Before)

    $after = Get-SourceProvenanceRecord -RepoRoot $repoRoot -Mode exact-tree
    if (-not [bool]$after.sourceClean -or
        [string]$after.headSha -cne [string]$Before.headSha -or
        [string]$after.treeSha -cne [string]$Before.treeSha -or
        [string]$after.stateDigest -cne [string]$Before.stateDigest) {
        throw 'RELEASE_SOURCE_CHANGED_DURING_CANDIDATE: source identity or clean state changed while release inputs were being read.'
    }
}

function Assert-CandidateOutputMatchesHeadBlob {
    param(
        [Parameter(Mandatory)] [string] $OutputPath,
        [Parameter(Mandatory)] [string] $RepositoryPath
    )

    $expectedBlob = Get-GitText -Arguments @('rev-parse', "HEAD:$RepositoryPath")
    $actualBlob = Get-GitText -Arguments @('hash-object', '--', [IO.Path]::GetFullPath($OutputPath))
    if ($expectedBlob -notmatch '^[0-9a-fA-F]{40,64}$' -or $actualBlob -cne $expectedBlob) {
        throw "RELEASE_SOURCE_PAYLOAD_MISMATCH: generated kit file '$RepositoryPath' does not match the committed source blob."
    }
}

function Invoke-ReleaseTool {
    param(
        [Parameter(Mandatory)] [string] $FilePath,
        [Parameter(Mandatory)] [string[]] $Arguments,
        [Parameter(Mandatory)] [string] $Name
    )

    $toolOutput = @(& $FilePath @Arguments *>&1)
    $exitCode = $LASTEXITCODE
    if ($OutputFormat -eq 'human') {
        foreach ($line in $toolOutput) { Write-Host ([string]$line) }
    }
    if ($exitCode -ne 0) {
        $details = (@($toolOutput | ForEach-Object { [string]$_ }) -join [Environment]::NewLine).Trim()
        if ($details.Length -gt 12000) { $details = $details.Substring($details.Length - 12000) }
        $message = "${Name}_FAILED: $Name exited with code $exitCode."
        if ($details) { $message += "`n$details" }
        throw $message
    }
}

function Assert-SafeCandidateOutput {
    param([Parameter(Mandatory)] [string] $Path)

    $full = [IO.Path]::GetFullPath($Path)
    $root = [IO.Path]::GetPathRoot($full).TrimEnd('\')
    $repo = [IO.Path]::GetFullPath($repoRoot).TrimEnd('\')
    if ([string]::IsNullOrWhiteSpace($full) -or $full.TrimEnd('\') -eq $root -or $full.TrimEnd('\') -eq $repo) {
        throw 'CANDIDATE_OUTPUT_INVALID: refusing a filesystem or repository root.'
    }
    return $full.TrimEnd('\')
}

function Prepare-CandidateOutput {
    param([Parameter(Mandatory)] [string] $Path)

    $full = Assert-SafeCandidateOutput -Path $Path
    if (Test-Path -LiteralPath $full) {
        $item = Get-Item -LiteralPath $full -Force
        if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'CANDIDATE_OUTPUT_INVALID: output must be a non-reparse directory.'
        }
        $children = @(Get-ChildItem -LiteralPath $full -Force)
        if ($children.Count -gt 0) {
            if (-not $Force) { throw "CANDIDATE_OUTPUT_NOT_EMPTY: use a new output directory or -Force for a previously marked candidate directory: $full" }
            $markerPath = Join-Path $full $script:markerName
            if (-not (Test-Path -LiteralPath $markerPath -PathType Leaf) -or
                ([IO.File]::ReadAllText($markerPath)).Trim() -cne $script:markerText) {
                throw 'CANDIDATE_OUTPUT_MARKER_REQUIRED: -Force is permitted only for a directory created by this candidate tool.'
            }
            foreach ($child in $children) {
                Remove-Item -LiteralPath $child.FullName -Recurse -Force
            }
        }
    } else {
        New-Item -ItemType Directory -Path $full -Force | Out-Null
    }
    [IO.File]::WriteAllText((Join-Path $full $script:markerName), $script:markerText, [Text.UTF8Encoding]::new($false))
    return $full
}

function Get-ShortSha {
    param([Parameter(Mandatory)] [string] $Commit)
    if ($Commit -notmatch '^[0-9a-fA-F]{40}$') { throw 'GIT_COMMIT_INVALID: expected a 40-hex commit.' }
    return $Commit.Substring(0, 8).ToLowerInvariant()
}

function New-DeterministicArchive {
    param(
        [Parameter(Mandatory)] [string] $Root,
        [Parameter(Mandatory)] [string] $ArchivePath,
        [Parameter(Mandatory)] [string] $ExcludedPath,
        [Parameter(Mandatory)] [string] $ExcludedMarkerPath
    )

    Add-Type -AssemblyName System.IO.Compression -ErrorAction SilentlyContinue
    Add-Type -AssemblyName System.IO.Compression.FileSystem -ErrorAction SilentlyContinue
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    $archiveFull = [IO.Path]::GetFullPath($ArchivePath)
    $files = @(Get-ChildItem -LiteralPath $rootFull -Recurse -File -Force | Where-Object {
            [IO.Path]::GetFullPath($_.FullName) -ine $archiveFull -and
            [IO.Path]::GetFullPath($_.FullName) -ine [IO.Path]::GetFullPath($ExcludedPath) -and
            [IO.Path]::GetFullPath($_.FullName) -ine [IO.Path]::GetFullPath($ExcludedMarkerPath)
        } | Sort-Object { $_.FullName.Substring($rootFull.Length).TrimStart('\').Replace('\', '/') })
    $stream = [IO.File]::Open($archiveFull, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::None)
    $zip = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Create, $false)
    try {
        $fixedTime = [DateTimeOffset]::new(1980, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
        foreach ($file in $files) {
            $relative = $file.FullName.Substring($rootFull.Length).TrimStart('\').Replace('\', '/')
            $entry = $zip.CreateEntry($relative, [IO.Compression.CompressionLevel]::Optimal)
            $entry.LastWriteTime = $fixedTime
            $input = [IO.File]::OpenRead($file.FullName)
            $output = $entry.Open()
            try { $input.CopyTo($output) }
            finally {
                $output.Dispose()
                $input.Dispose()
            }
        }
    } finally {
        $zip.Dispose()
        $stream.Dispose()
    }
}

function Write-Utf8NoBom {
    param([Parameter(Mandatory)] [string] $Path, [Parameter(Mandatory)] [string] $Text)
    [IO.File]::WriteAllText([IO.Path]::GetFullPath($Path), $Text, [Text.UTF8Encoding]::new($false))
}

function Get-RelativeFileName {
    param([Parameter(Mandatory)] [string] $Root, [Parameter(Mandatory)] [string] $Path)
    return ([IO.Path]::GetFullPath($Path).Substring([IO.Path]::GetFullPath($Root).TrimEnd('\').Length).TrimStart('\')).Replace('\', '/')
}

function Write-Checksums {
    param([Parameter(Mandatory)] [string] $Root, [Parameter(Mandatory)] [string] $OutputPath, [Parameter(Mandatory)] [string] $MarkerPath)

    $outputFull = [IO.Path]::GetFullPath($OutputPath)
    $markerFull = [IO.Path]::GetFullPath($MarkerPath)
    $lines = @(Get-ChildItem -LiteralPath $Root -Recurse -File -Force | Where-Object {
            [IO.Path]::GetFullPath($_.FullName) -ine $outputFull -and
            [IO.Path]::GetFullPath($_.FullName) -ine $markerFull
        } | ForEach-Object {
            $relative = Get-RelativeFileName -Root $Root -Path $_.FullName
            $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToUpperInvariant()
            "$hash  $relative"
        } | Sort-Object)
    Write-Utf8NoBom -Path $OutputPath -Text (($lines -join "`n") + "`n")
}

function Get-SqliteVersion {
    $headerText = Get-GitHeadBlobText -RepositoryPath 'third_party/sqlite/sqlite3.h'
    $match = [regex]::Match($headerText, '(?m)^#define SQLITE_VERSION "([^"]+)"')
    if ($match.Success) { return [string]$match.Groups[1].Value }
    return 'unknown'
}

function Write-ReleaseNotes {
    param(
        [Parameter(Mandatory)] [string] $Version,
        [Parameter(Mandatory)] [string] $Classification,
        [Parameter(Mandatory)] [string] $Commit,
        [Parameter(Mandatory)] [string] $Path
    )

    $text = Get-GitHeadBlobText -RepositoryPath 'docs/release/RELEASE_NOTES_TEMPLATE.md'
    $text = $text.Replace('[VERSION]', $Version).Replace('[CLASSIFICATION]', $Classification).Replace('[COMMIT]', $Commit)
    Write-Utf8NoBom -Path $Path -Text $text
}

function Assert-CandidateArchiveSnapshot {
    param(
        [Parameter(Mandatory)] [string] $SnapshotRoot,
        [Parameter(Mandatory)] [string] $Mode,
        [Parameter(Mandatory)] [string] $ExpectedConfiguration,
        [Parameter(Mandatory)] [string] $ExpectedArtifactConfiguration,
        [Parameter(Mandatory)] [string] $ExpectedCommit,
        [Parameter(Mandatory)] $BinaryProvenance,
        [Parameter(Mandatory)] $PinStates,
        [AllowNull()] [string] $ExpectedSignerThumbprint,
        [Parameter(Mandatory)] [System.Collections.IDictionary] $KitPayloads,
        [Parameter(Mandatory)] [string] $Version,
        [Parameter(Mandatory)] [string] $Classification,
        [Parameter(Mandatory)] [string] $ExpectedSqliteVersion
    )

    $artifactManifestPath = Join-Path $SnapshotRoot 'artifact-manifest.json'
    $packageRoot = Join-Path $SnapshotRoot 'package'
    $artifactManifest = Get-Content -LiteralPath $artifactManifestPath -Raw -ErrorAction Stop | ConvertFrom-Json
    $artifactManifest = Assert-ArtifactManifestMatchesPackage -Manifest $artifactManifest -PackageRoot $packageRoot `
        -ExpectedConfiguration $ExpectedArtifactConfiguration -ExpectedCommit $ExpectedCommit
    if ([string]$artifactManifest.configuration -cne $ExpectedArtifactConfiguration -or
        [string]$artifactManifest.commit -ine $ExpectedCommit -or
        [string]$artifactManifest.expectedArchitecture -cne 'x64') {
        throw 'CANDIDATE_ARCHIVE_ARTIFACT_MANIFEST_MISMATCH'
    }
    $requiredArtifacts = @('FastFiles.exe', 'FastFilesEngine.exe', 'FastFilesIndexSvc.exe', 'FastFilesSetup.exe')
    $observedArtifacts = @($artifactManifest.artifacts | ForEach-Object { [string]$_.filename } | Sort-Object)
    if ($observedArtifacts.Count -ne $requiredArtifacts.Count -or
        @($observedArtifacts | Where-Object { $_ -notin $requiredArtifacts }).Count -gt 0 -or
        @($requiredArtifacts | Where-Object { $_ -notin $observedArtifacts }).Count -gt 0) {
        throw 'CANDIDATE_ARCHIVE_ARTIFACT_INVENTORY_MISMATCH'
    }
    $expectedSnapshotPaths = @(
        'artifact-manifest.json',
        'dependency-inventory.json',
        'kit-manifest.json',
        'RELEASE_NOTES.md',
        'validation/fftest.exe'
    )
    $expectedSnapshotPaths += @($requiredArtifacts | ForEach-Object { "package/$_" })
    $expectedSnapshotPaths += @('package/FastFiles.package.manifest')
    $expectedSnapshotPaths += @($KitPayloads.Keys)
    if (Test-Path -LiteralPath (Join-Path $packageRoot 'FastFiles.package.manifest.p7s') -PathType Leaf) {
        $expectedSnapshotPaths += 'package/FastFiles.package.manifest.p7s'
    }
    $snapshotContents = Test-ReleaseCandidateSnapshotContents -Root $SnapshotRoot -ExpectedPaths $expectedSnapshotPaths `
        -ExpectedCommit $ExpectedCommit -ExpectedSqliteVersion $ExpectedSqliteVersion
    if (-not $snapshotContents.valid) {
        throw "CANDIDATE_ARCHIVE_SNAPSHOT_INVALID: $($snapshotContents.issues -join ',')"
    }
    foreach ($artifact in @($artifactManifest.artifacts)) {
        $boundBinary = @($BinaryProvenance.binaries | Where-Object { $_.filename -eq $artifact.filename }) | Select-Object -First 1
        $packageBinary = Join-Path $packageRoot ([string]$artifact.filename)
        if ($null -eq $boundBinary -or
            [string]$boundBinary.sha256 -ine [string]$artifact.sha256 -or
            [string]$artifact.configuration -cne $ExpectedConfiguration -or
            -not (Test-Path -LiteralPath $packageBinary -PathType Leaf) -or
            [long](Get-Item -LiteralPath $packageBinary).Length -ne [long]$artifact.size -or
            [string](Get-FileHash -LiteralPath $packageBinary -Algorithm SHA256).Hash -ine [string]$artifact.sha256) {
            throw "CANDIDATE_ARCHIVE_ARTIFACT_MISMATCH: $($artifact.filename)"
        }
    }

    $packageManifestPath = Join-Path $packageRoot 'FastFiles.package.manifest'
    $packageCheck = if ($Mode -eq 'production') {
        Test-PackageManifest -Path $packageManifestPath -ArtifactManifest $artifactManifest -PackageRoot $packageRoot `
            -AllowedSignerThumbprints @((Normalize-Thumbprint -Value $ExpectedSignerThumbprint))
    } else {
        Test-PackageManifest -Path $packageManifestPath -ArtifactManifest $artifactManifest -PackageRoot $packageRoot -AllowUnsignedPreparation
    }
    if (-not $packageCheck.valid) { throw "CANDIDATE_ARCHIVE_PACKAGE_INVALID: $($packageCheck.reason)" }

    $versionCheck = Test-ReleaseVersionMetadata -ArtifactRoot $packageRoot -RepoRoot $repoRoot `
        -AdditionalPaths @((Join-Path $SnapshotRoot 'validation\fftest.exe'))
    if (-not $versionCheck.valid) { throw 'CANDIDATE_ARCHIVE_VERSION_METADATA_INVALID' }
    foreach ($versionRecord in @($versionCheck.artifacts)) {
        $versionRecord.path = if ($versionRecord.filename -eq 'fftest.exe') {
            'validation/fftest.exe'
        } else {
            "package/$($versionRecord.filename)"
        }
    }

    $boundFftest = @($BinaryProvenance.binaries | Where-Object { $_.filename -eq 'fftest.exe' }) | Select-Object -First 1
    $packagedFftest = Join-Path $SnapshotRoot 'validation\fftest.exe'
    if ($null -eq $boundFftest -or -not (Test-Path -LiteralPath $packagedFftest -PathType Leaf) -or
        [string]$boundFftest.sha256 -ine (Get-FileHash -LiteralPath $packagedFftest -Algorithm SHA256).Hash) {
        throw 'CANDIDATE_ARCHIVE_FFTEST_MISMATCH'
    }
    foreach ($payloadName in $KitPayloads.Keys) {
        Assert-CandidateOutputMatchesHeadBlob -OutputPath (Join-Path $SnapshotRoot $payloadName) -RepositoryPath $KitPayloads[$payloadName]
    }

    $expectedNotes = Get-GitHeadBlobText -RepositoryPath 'docs/release/RELEASE_NOTES_TEMPLATE.md'
    $expectedNotes = $expectedNotes.Replace('[VERSION]', $Version).Replace('[CLASSIFICATION]', $Classification).Replace('[COMMIT]', $ExpectedCommit)
    $actualNotes = [IO.File]::ReadAllText((Join-Path $SnapshotRoot 'RELEASE_NOTES.md'))
    if ($actualNotes -cne $expectedNotes) { throw 'CANDIDATE_ARCHIVE_RELEASE_NOTES_MISMATCH' }

    $kitManifestPath = Join-Path $SnapshotRoot 'kit-manifest.json'
    $signaturePath = Join-Path $packageRoot 'FastFiles.package.manifest.p7s'
    $signaturePresent = Test-Path -LiteralPath $signaturePath -PathType Leaf
    $signatureSha = if ($signaturePresent) { (Get-FileHash -LiteralPath $signaturePath -Algorithm SHA256).Hash.ToUpperInvariant() } else { $null }
    $engineArtifact = @($artifactManifest.artifacts | Where-Object filename -eq 'FastFilesEngine.exe') | Select-Object -First 1
    $serviceArtifact = @($artifactManifest.artifacts | Where-Object filename -eq 'FastFilesIndexSvc.exe') | Select-Object -First 1
    $enginePinTrusted = $PinStates.engine.state -eq 'POPULATED' -and $engineArtifact.signature.valid -and
        $engineArtifact.signature.timestampSigned -and $engineArtifact.signature.timestampValid -and
        $engineArtifact.signature.leafThumbprint -eq $PinStates.engine.normalized
    $servicePinTrusted = $PinStates.service.state -eq 'POPULATED' -and $serviceArtifact.signature.valid -and
        $serviceArtifact.signature.timestampSigned -and $serviceArtifact.signature.timestampValid -and
        $serviceArtifact.signature.leafThumbprint -eq $PinStates.service.normalized
    $engineKitPin = [ordered]@{
        name = 'engine'
        state = if ($enginePinTrusted) { 'POPULATED' } elseif ($PinStates.engine.state -in @('PLACEHOLDER', 'MALFORMED', 'MISSING')) { $PinStates.engine.state } else { 'UNVERIFIED' }
        length = if ($enginePinTrusted) { 40 } else { 0 }
        thumbprint = if ($enginePinTrusted) { $PinStates.engine.normalized } else { $null }
        source = if ($enginePinTrusted) { 'matched-to-signed-FastFilesEngine.exe' } else { 'not-exported-until-matched-to-signed-artifact' }
    }
    $serviceKitPin = [ordered]@{
        name = 'service'
        state = if ($servicePinTrusted) { 'POPULATED' } elseif ($PinStates.service.state -in @('PLACEHOLDER', 'MALFORMED', 'MISSING')) { $PinStates.service.state } else { 'UNVERIFIED' }
        length = if ($servicePinTrusted) { 40 } else { 0 }
        thumbprint = if ($servicePinTrusted) { $PinStates.service.normalized } else { $null }
        source = if ($servicePinTrusted) { 'matched-to-signed-FastFilesIndexSvc.exe' } else { 'not-exported-until-matched-to-signed-artifact' }
    }
    $expectedPackagePaths = @($artifactManifest.artifacts | ForEach-Object { "package/$($_.filename)" }) + @('package/FastFiles.package.manifest')
    if ($signaturePresent) { $expectedPackagePaths += 'package/FastFiles.package.manifest.p7s' }
    $signingBlocked = -not $signaturePresent -or -not [bool]$packageCheck.productionReady -or
        @($artifactManifest.artifacts | Where-Object { -not $_.signature.valid -or -not $_.signature.timestampSigned -or -not $_.signature.timestampValid }).Count -gt 0
    $expectedKitManifest = [ordered]@{
        schemaVersion = 1
        kind = 'fastfiles-clean-host-kit'
        commit = $ExpectedCommit
        architecture = 'x64'
        packageRoot = 'package'
        package = $expectedPackagePaths
        packageManifest = 'package/FastFiles.package.manifest'
        packageManifestSignature = [ordered]@{
            path = if ($signaturePresent) { 'package/FastFiles.package.manifest.p7s' } else { $null }
            required = $true
            present = $signaturePresent
            sha256 = $signatureSha
        }
        packageManifestSha256 = (Get-FileHash -LiteralPath $packageManifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
        versionMetadata = $versionCheck
        validationTool = [ordered]@{ name = 'fftest.exe'; included = $true; path = 'validation/fftest.exe' }
        artifactManifest = 'artifact-manifest.json'
        pinProvisioning = [ordered]@{ engine = $engineKitPin; service = $serviceKitPin; provenanceRequired = $true }
        productionSigning = [ordered]@{ required = $true; currentState = if ($signingBlocked) { 'BLOCKED_EXTERNAL' } else { 'REQUIRES_PIN_PROVENANCE' } }
        sourceCloneRequired = $false
        buildToolsRequired = $false
    }
    $kitManifestCheck = Test-ReleaseKitManifestProjection -Path $kitManifestPath -ExpectedManifest $expectedKitManifest
    if (-not $kitManifestCheck.valid) {
        throw 'CANDIDATE_ARCHIVE_KIT_MANIFEST_MISMATCH'
    }
    return [pscustomobject]@{
        packageCheck = $packageCheck
        artifactManifest = $artifactManifest
        versionCheck = $versionCheck
        expectedSnapshotPaths = $snapshotContents.expectedPaths
        artifactManifestSha256 = (Get-FileHash -LiteralPath $artifactManifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
        packageManifestSha256 = (Get-FileHash -LiteralPath $packageManifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
        detachedSignaturePresent = $signaturePresent
    }
}

$archiveGuard = $null
$candidateOutputGuards = $null
try {
    Assert-CleanRepository
    $candidateSourceStart = Get-SourceProvenanceRecord -RepoRoot $repoRoot -Mode exact-tree
    if (-not [bool]$candidateSourceStart.sourceClean) {
        throw 'RELEASE_CLEAN_TREE_REQUIRED: exact-tree source qualification did not report a clean source state.'
    }
    $commit = Get-GitText -Arguments @('rev-parse', 'HEAD')
    if ([string]$candidateSourceStart.headSha -cne $commit) {
        throw 'RELEASE_SOURCE_CHANGED_DURING_CANDIDATE: HEAD changed while candidate identity was captured.'
    }
    $commitShort = Get-ShortSha -Commit $commit
    $commitTimestamp = Get-GitText -Arguments @('show', '-s', '--format=%cI', 'HEAD')
    $version = Get-RepositoryProductVersion -RepoRoot $repoRoot
    $committedVersion = (Get-GitText -Arguments @('show', 'HEAD:VERSION')).Trim()
    if ($version -cne $committedVersion) {
        throw 'RELEASE_SOURCE_VERSION_MISMATCH: VERSION read from the working tree does not match the candidate commit.'
    }
    $candidateSqliteVersion = Get-SqliteVersion
    $classification = if ($Mode -eq 'production') { 'production-signed' } else { 'unsigned-validation' }
    $preset = if ($Mode -eq 'production') { 'release' } else { 'release-test' }
    $expectedBuildConfiguration = if ($Mode -eq 'production') { 'Release' } else { 'RelWithDebInfo' }
    $artifactManifestConfiguration = if ($Mode -eq 'production') { 'Release' } else { 'release-test' }
    if (-not $BuildRoot) { $BuildRoot = Join-Path $repoRoot ("build\$preset") }
    $BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
    $canonicalBuildRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot "build\$preset"))
    $selectedRootIsCanonical = $BuildRoot.Equals($canonicalBuildRoot, [StringComparison]::OrdinalIgnoreCase)
    if (-not $SkipVerification -and -not $selectedRootIsCanonical) {
        throw "RELEASE_BUILD_ROOT_VERIFICATION_MISMATCH: verify.ps1 build qualifies '$canonicalBuildRoot', not selected BuildRoot '$BuildRoot'. Use the canonical preset root or explicitly skip the repository build gate for an imported artifact root."
    }
    $buildRootLabel = 'external'
    $repoPrefix = [IO.Path]::GetFullPath($repoRoot).TrimEnd('\') + '\'
    if ($BuildRoot.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        $buildRootLabel = $BuildRoot.Substring($repoPrefix.Length).Replace('\', '/')
    }

    if (-not $OutputDirectory) {
        $OutputDirectory = Join-Path $repoRoot ("out\release-candidate\FastFiles-{0}-{1}-x64-{2}" -f $version, $classification, $commitShort)
    }
    $output = Prepare-CandidateOutput -Path $OutputDirectory
    $markerPath = Join-Path $output $script:markerName
    $verification = [ordered]@{
        status = if ($SkipVerification) { 'SKIPPED_BY_OPERATOR' } else { 'PENDING' }
        command = 'pwsh ./verify/verify.ps1 build'
        preset = $preset
        selectedBuildRoot = $buildRootLabel
        selectedBuildRootStatus = 'PENDING'
        note = if ($SkipVerification) { 'Repository build verification was explicitly skipped; artifact evidence remains separate from a full gate.' } else { $null }
    }

    if (-not $SkipVerification) {
        $pwsh = (Get-Command pwsh -ErrorAction Stop).Source
        Invoke-ReleaseTool -FilePath $pwsh -Arguments @('-NoProfile', '-NonInteractive', '-File', (Join-Path $verifyRoot 'verify.ps1'), 'build', '-Format', 'json', '-Change', 'real-user-dogfooding-release-engineering') -Name 'VERIFY_BUILD'
        if ($Mode -eq 'production') {
            $cmake = (Get-Command cmake -ErrorAction Stop).Source
            Invoke-ReleaseTool -FilePath $cmake -Arguments @('--preset', 'release') -Name 'CMAKE_CONFIGURE_RELEASE'
            Invoke-ReleaseTool -FilePath $cmake -Arguments @('--build', '--preset', 'release') -Name 'CMAKE_BUILD_RELEASE'
        }
        $verification.status = 'PASS'
    }

    if (-not (Test-Path -LiteralPath $BuildRoot -PathType Container)) {
        throw "RELEASE_BUILD_ROOT_MISSING: $BuildRoot"
    }

    # The canonical build capability verifies the repository's preset roots.
    # A caller may select a different release BuildRoot, so bind that exact
    # artifact set here before the release kit copies or manifests any files.
    $releaseBinaryPaths = Get-DefaultCertificationBinaries -RepoRoot $repoRoot -BuildDirectory $BuildRoot
    $buildRootGate = Invoke-ProvenanceGate -RepoRoot $repoRoot -Binaries $releaseBinaryPaths `
        -Mode exact-tree -Stage 'release-candidate-build-root' -PassThru
    if (-not $buildRootGate.ok) { throw $buildRootGate.message }
    $configurationMismatches = @($buildRootGate.evidence.binaries | Where-Object {
            [string]$_.configuration -cne $expectedBuildConfiguration
        })
    if ($configurationMismatches.Count -gt 0) {
        $observedConfigurations = @($configurationMismatches | ForEach-Object {
                '{0}={1}' -f $_.filename, $(if ([string]::IsNullOrWhiteSpace([string]$_.configuration)) { 'unavailable' } else { [string]$_.configuration })
            }) -join ', '
        throw "RELEASE_BUILD_CONFIGURATION_MISMATCH: Mode '$Mode' requires config=$expectedBuildConfiguration; selected BuildRoot reported $observedConfigurations."
    }
    if ($Mode -eq 'production' -and $SkipVerification) {
        throw 'PRODUCTION_VERIFICATION_CANNOT_BE_SKIPPED: production candidates require the canonical Release build verification; imported roots need separately qualified evidence.'
    }
    $verification.selectedBuildRootStatus = 'STRICT_PROVENANCE_PASS'
    # This is a deterministic release-candidate projection of the full gate
    # evidence. Its dedicated schema intentionally omits the collector's
    # checkResult/check timestamp envelope while preserving the selected-root
    # binding and artifact facts needed by the package manifest.
    $binaryProvenance = [ordered]@{
        schemaVersion = 1
        kind = 'fastfiles-release-build-root-provenance'
        status = 'PASS'
        stage = 'release-candidate-build-root'
        mode = 'exact-tree'
        expectedConfiguration = $expectedBuildConfiguration
        artifactManifestConfiguration = $artifactManifestConfiguration
        buildRoot = $buildRootLabel
        source = [ordered]@{
            headSha = $buildRootGate.source.headSha
            treeSha = $buildRootGate.source.treeSha
            treeSemantic = $buildRootGate.source.treeSemantic
            sourceClean = $buildRootGate.source.sourceClean
            stateDigest = $buildRootGate.source.stateDigest
        }
        setIdentity = $buildRootGate.binding.setIdentity
        setConsistent = $buildRootGate.binding.setConsistent
        boundToHead = $buildRootGate.binding.boundToHead
        boundToTree = $buildRootGate.binding.boundToTree
        treeBindingStatus = $buildRootGate.binding.treeBindingStatus
        binaries = @($buildRootGate.evidence.binaries | ForEach-Object {
                [ordered]@{
                    filename = $_.filename
                    path = "BuildRoot/$($_.filename)"
                    exists = $_.exists
                    sha256 = $_.sha256
                    buildIdentity = $_.buildIdentity
                    buildIdentityFormatValid = $_.buildIdentityFormatValid
                    peArchitecture = $_.peArchitecture
                    worktreeState = $_.worktreeState
                    configuration = $_.configuration
                    issues = $_.issues
                }
        })
    }
    $binaryProvenanceGate = Test-ReleaseBuildRootProvenance -Provenance ([pscustomobject]$binaryProvenance)
    if (-not $binaryProvenanceGate.valid) {
        throw "RELEASE_BUILD_ROOT_PROVENANCE_INVALID: $($binaryProvenanceGate.issueCodes -join ',')"
    }

    if ($Mode -eq 'production') {
        if ([string]::IsNullOrWhiteSpace($ManifestSignaturePath) -or -not (Test-Path -LiteralPath $ManifestSignaturePath -PathType Leaf)) {
            throw 'PRODUCTION_MANIFEST_SIGNATURE_REQUIRED: supply the detached signature produced after the final package manifest bytes are generated.'
        }
        if (-not (Test-UsableThumbprint -Value (Normalize-Thumbprint -Value $ExpectedSignerThumbprint))) {
            throw 'PRODUCTION_EXPECTED_SIGNER_REQUIRED: supply a non-placeholder 40-hex expected signer thumbprint.'
        }
        foreach ($pinName in @('FASTFILES_ENGINE_SIGNATURE_THUMBPRINT', 'FASTFILES_INDEX_SVC_SIGNATURE_THUMBPRINT')) {
            $pinState = Get-PinState -Name $pinName -Value ([Environment]::GetEnvironmentVariable($pinName)) -Source 'environment'
            if ($pinState.state -ne 'POPULATED') { throw "PRODUCTION_PIN_REQUIRED: $pinName is $($pinState.state); release production pins must be supplied by the authorized signing configuration." }
        }
    }

    $kitScript = Join-Path $releaseRoot 'New-ReleaseKit.ps1'
    $kitArgs = @('-NoProfile', '-NonInteractive', '-File', $kitScript, '-BuildRoot', $BuildRoot, '-OutputDirectory', $output, '-Force', '-OutputFormat', 'json')
    $kitArgs += @('-Configuration', $artifactManifestConfiguration)
    if ($Mode -eq 'production') { $kitArgs += @('-ManifestSignaturePath', [IO.Path]::GetFullPath($ManifestSignaturePath)) }
    Invoke-ReleaseTool -FilePath (Get-Command pwsh -ErrorAction Stop).Source -Arguments $kitArgs -Name 'RELEASE_KIT'
    # This fail-closed seam verifies that a successful child JSON document
    # cannot escape before a later candidate failure. It can never produce PASS.
    if ($env:FASTFILES_TEST_RELEASE_CANDIDATE_FAIL_AFTER_KIT -ceq '1') {
        throw 'TEST_ONLY_POST_KIT_FAILURE'
    }

    $kitPayloads = [ordered]@{
        'ReleaseCommon.psm1' = 'verify/release/ReleaseCommon.psm1'
        'derive-pin.ps1' = 'verify/release/derive-pin.ps1'
        'sign-release.ps1' = 'verify/release/sign-release.ps1'
        'validate-clean-host.ps1' = 'verify/release/validate-clean-host.ps1'
        'cleanup-clean-host.ps1' = 'verify/release/cleanup-clean-host.ps1'
        'privileged-release.ps1' = 'verify/release/privileged-release.ps1'
        'import-clean-host-result.ps1' = 'verify/release/import-clean-host-result.ps1'
        'README.md' = 'verify/release/CleanHostKit.README.md'
    }
    foreach ($payloadName in $kitPayloads.Keys) {
        Assert-CandidateOutputMatchesHeadBlob -OutputPath (Join-Path $output $payloadName) -RepositoryPath $kitPayloads[$payloadName]
    }

    $artifactManifestPath = Join-Path $output 'artifact-manifest.json'
    $packageRoot = Join-Path $output 'package'
    $artifactManifest = Get-Content -LiteralPath $artifactManifestPath -Raw | ConvertFrom-Json
    $artifactManifest = Assert-ArtifactManifestMatchesPackage -Manifest $artifactManifest -PackageRoot $packageRoot `
        -ExpectedConfiguration $artifactManifestConfiguration -ExpectedCommit $commit
    $manifestBuildConfiguration = switch ([string]$artifactManifest.configuration) {
        'release-test' { 'RelWithDebInfo' }
        'Release' { 'Release' }
        default { $null }
    }
    if ([string]$artifactManifest.configuration -cne $artifactManifestConfiguration -or
        $manifestBuildConfiguration -cne $expectedBuildConfiguration) {
        throw "RELEASE_ARTIFACT_CONFIGURATION_MISMATCH: artifact manifest '$($artifactManifest.configuration)' does not map to selected executable config '$expectedBuildConfiguration'."
    }
    foreach ($artifact in @($artifactManifest.artifacts)) {
        $boundBinary = @($binaryProvenance.binaries | Where-Object { $_.filename -eq $artifact.filename }) | Select-Object -First 1
        if ($null -eq $boundBinary -or [string]$boundBinary.sha256 -ine [string]$artifact.sha256) {
            throw "RELEASE_BUILD_ROOT_CONTENT_CHANGED: packaged artifact '$($artifact.filename)' does not match the SHA-256 measured at the selected BuildRoot provenance gate."
        }
    }
    $boundFftest = @($binaryProvenance.binaries | Where-Object { $_.filename -eq 'fftest.exe' }) | Select-Object -First 1
    $packagedFftest = Join-Path $output 'validation\fftest.exe'
    if ($null -eq $boundFftest -or -not (Test-Path -LiteralPath $packagedFftest -PathType Leaf) -or
        [string]$boundFftest.sha256 -ine (Get-FileHash -LiteralPath $packagedFftest -Algorithm SHA256).Hash.ToLowerInvariant()) {
        throw 'RELEASE_BUILD_ROOT_CONTENT_CHANGED: validation fftest.exe does not match the SHA-256 measured at the selected BuildRoot provenance gate.'
    }
    $versionCheck = Test-ReleaseVersionMetadata -ArtifactRoot (Join-Path $output 'package') -RepoRoot $repoRoot -AdditionalPaths @((Join-Path $output 'validation\fftest.exe'))
    if (-not $versionCheck.valid) { throw 'VERSION_CONSISTENCY_FAILED: release executable metadata does not match VERSION and the current commit.' }
    # Keep the candidate's exported version evidence portable; the verifier needs
    # the metadata and validity fields, not the operator's build-machine paths.
    foreach ($versionRecord in @($versionCheck.artifacts)) {
        $versionRecord.path = if ($versionRecord.filename -eq 'fftest.exe') {
            'validation/fftest.exe'
        } else {
            "package/$($versionRecord.filename)"
        }
    }
    $artifactVersions = @($artifactManifest.artifacts | Select-Object -ExpandProperty productVersion -Unique)
    if ($artifactVersions.Count -ne 1 -or [string]$artifactVersions[0] -cne $version) { throw "VERSION_CONSISTENCY_FAILED: VERSION=$version but artifact metadata is '$($artifactVersions -join ',')'." }
    if ([string]$artifactManifest.commit -ine $commit) { throw 'BUILD_IDENTITY_FAILED: artifact manifest does not identify the clean checkout commit.' }

    $packageManifestPath = Join-Path $packageRoot 'FastFiles.package.manifest'
    $packageCheck = if ($Mode -eq 'production') {
        Test-PackageManifest -Path $packageManifestPath -ArtifactManifest $artifactManifest -PackageRoot $packageRoot -AllowedSignerThumbprints @((Normalize-Thumbprint -Value $ExpectedSignerThumbprint))
    } else {
        Test-PackageManifest -Path $packageManifestPath -ArtifactManifest $artifactManifest -PackageRoot $packageRoot -AllowUnsignedPreparation
    }
    if (-not $packageCheck.valid) { throw "PACKAGE_VALIDATION_FAILED: $($packageCheck.reason)" }

    $pinStates = Get-ConfiguredPinStates -RepoRoot $repoRoot -BuildRoot $BuildRoot
    $signatureMatrix = @(Get-ReleaseSignatureMatrix -Manifest $artifactManifest -PinStates $pinStates -ExpectedSignerThumbprint $(if ($Mode -eq 'production') { $ExpectedSignerThumbprint } else { $null }))
    $binarySigningPass = @($signatureMatrix | Where-Object { $_.result -ne 'PASS' }).Count -eq 0
    $pinsPass = $pinStates.engine.state -eq 'POPULATED' -and $pinStates.service.state -eq 'POPULATED' -and
        @($signatureMatrix | Where-Object { $_.artifact -in @('FastFilesEngine.exe', 'FastFilesIndexSvc.exe') -and $_.result -ne 'PASS' }).Count -eq 0
    if ($Mode -eq 'production' -and (-not $binarySigningPass -or -not $pinsPass -or -not $packageCheck.productionReady)) {
        throw 'PRODUCTION_CANDIDATE_BLOCKED: signed PE artifacts, matching production pins, and a valid detached package-manifest signature are all required.'
    }

    $archiveName = "FastFiles-$version-$classification-x64.zip"
    $archivePath = Join-Path $output $archiveName
    $notesPath = Join-Path $output 'RELEASE_NOTES.md'
    Write-ReleaseNotes -Version $version -Classification $classification -Commit $commit -Path $notesPath
    $dependencyPath = Join-Path $output 'dependency-inventory.json'
    $sqliteVersionAfterPackaging = Get-SqliteVersion
    if ($sqliteVersionAfterPackaging -cne $candidateSqliteVersion) {
        throw 'RELEASE_SOURCE_SQLITE_INPUT_CHANGED: dependency inventory input changed while candidate artifacts were being generated.'
    }
    Write-ReleaseJson -Value ([ordered]@{
            schemaVersion = 1
            kind = 'fastfiles-dependency-inventory'
            format = 'internal-structured-inventory'
            commit = $commit
            components = @([ordered]@{ name = 'SQLite'; version = $candidateSqliteVersion; scope = 'statically linked build/runtime dependency'; source = 'third_party/sqlite' })
            note = 'This is a lightweight dependency inventory, not a claim of a complete SPDX or CycloneDX SBOM.'
        }) -Path $dependencyPath

    Assert-CandidateSourceUnchanged -Before $candidateSourceStart

    $archiveSnapshot = New-CandidateArchiveSnapshot -Root $output -ArchivePath $archivePath -MarkerPath $markerPath
    try {
        $archiveSnapshotResult = Assert-CandidateArchiveSnapshot -SnapshotRoot $archiveSnapshot.root -Mode $Mode `
            -ExpectedConfiguration $expectedBuildConfiguration -ExpectedArtifactConfiguration $artifactManifestConfiguration `
            -ExpectedCommit $commit -BinaryProvenance $binaryProvenance -PinStates $pinStates -ExpectedSignerThumbprint $ExpectedSignerThumbprint `
            -KitPayloads $kitPayloads -Version $version -Classification $classification -ExpectedSqliteVersion $candidateSqliteVersion
        $packageCheck = $archiveSnapshotResult.packageCheck
        $artifactManifest = $archiveSnapshotResult.artifactManifest
        $versionCheck = $archiveSnapshotResult.versionCheck
        $archiveInventory = @(Get-ReleaseArchiveInventory -Root $archiveSnapshot.root)
        if ($archiveInventory.Count -eq 0) { throw 'CANDIDATE_ARCHIVE_INPUTS_EMPTY' }
        $expectedArchivePaths = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach ($path in @($archiveSnapshotResult.expectedSnapshotPaths)) { [void]$expectedArchivePaths.Add([string]$path) }
        $observedArchivePaths = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach ($entry in $archiveInventory) {
            if (-not $observedArchivePaths.Add([string]$entry.path) -or -not $expectedArchivePaths.Contains([string]$entry.path)) {
                throw "CANDIDATE_ARCHIVE_MEMBER_INVENTORY_MISMATCH: $($entry.path)"
            }
        }
        if ($observedArchivePaths.Count -ne $expectedArchivePaths.Count) {
            throw 'CANDIDATE_ARCHIVE_MEMBER_INVENTORY_INCOMPLETE'
        }
        New-DeterministicArchive -Root $archiveSnapshot.root -ArchivePath $archivePath `
            -ExcludedPath $archivePath -ExcludedMarkerPath $markerPath
        $archiveGuard = [FastFilesReleaseArtifactReadFile]::OpenRegularReadOnly($archivePath)
        $archiveValidation = Test-ReleaseArchiveContents -ArchivePath $archivePath -ExpectedEntries $archiveInventory
        if (-not $archiveValidation.valid) {
            throw "CANDIDATE_ARCHIVE_CONTENTS_INVALID: $($archiveValidation.issues -join ',')"
        }
        $archiveGuard.Position = 0
        $archiveSha = [Security.Cryptography.SHA256]::Create()
        try { $archiveHash = [Convert]::ToHexString($archiveSha.ComputeHash($archiveGuard)) }
        finally { $archiveSha.Dispose() }
        Assert-CandidateSourceUnchanged -Before $candidateSourceStart
    } finally {
        Remove-CandidateArchiveSnapshot -Snapshot $archiveSnapshot
    }

    $candidateManifestPath = Join-Path $output 'candidate-manifest.json'
    $candidate = [ordered]@{
        schemaVersion = 1
        kind = 'fastfiles-release-candidate'
        product = 'FastFiles'
        version = $version
        commit = $commit
        architecture = 'x64'
        buildConfiguration = $artifactManifest.configuration
        classification = $classification
        payloadAuthority = 'archive-only'
        signed = $Mode -eq 'production'
        productionReady = $Mode -eq 'production'
        sourceCommitTimestampUtc = $commitTimestamp
        artifactManifest = [ordered]@{ path = 'artifact-manifest.json'; sha256 = $archiveSnapshotResult.artifactManifestSha256 }
        binaryProvenance = $binaryProvenance
        package = [ordered]@{ root = 'package'; manifest = 'package/FastFiles.package.manifest'; manifestSha256 = $archiveSnapshotResult.packageManifestSha256; detachedSignaturePresent = $archiveSnapshotResult.detachedSignaturePresent }
        archive = [ordered]@{ path = $archiveName; sha256 = $archiveHash }
        validation = $verification
        versionConsistency = $versionCheck
        signatures = [ordered]@{ status = if ($binarySigningPass) { 'PASS' } else { 'BLOCKED_EXTERNAL' }; matrix = $signatureMatrix }
        pins = [ordered]@{ engine = Get-SafePinState -PinState $pinStates.engine; service = Get-SafePinState -PinState $pinStates.service }
        dependencyInventory = 'dependency-inventory.json'
        releaseNotes = 'RELEASE_NOTES.md'
        checksums = 'SHA256SUMS.txt'
        reproducibility = [ordered]@{ archiveEntryTimesUtc = '1980-01-01T00:00:00Z'; generatedMetadataTimeSource = 'source commit timestamp'; payloadOrdering = 'ordinal relative path' }
    }
    Write-ReleaseJson -Value $candidate -Path $candidateManifestPath
    $candidateManifestCheck = Test-ReleaseJsonProjection -Path $candidateManifestPath -ExpectedValue $candidate
    if (-not $candidateManifestCheck.valid) { throw 'CANDIDATE_MANIFEST_OUTPUT_MISMATCH' }
    $provenancePath = Join-Path $output 'provenance.json'
    $provenance = [ordered]@{
            schemaVersion = 1
            kind = 'fastfiles-release-provenance'
            product = 'FastFiles'
            version = $version
            commit = $commit
            architecture = 'x64'
            buildConfiguration = $artifactManifest.configuration
            classification = $classification
            payloadAuthority = 'archive-only'
            sourceCommitTimestampUtc = $commitTimestamp
            candidateManifestSha256 = (Get-FileHash -LiteralPath $candidateManifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
            archiveSha256 = $archiveHash
            binaryProvenance = $binaryProvenance
            signing = [ordered]@{ requiredForProduction = $true; binaryStatus = if ($binarySigningPass) { 'PASS' } else { 'BLOCKED_EXTERNAL' }; packageManifestStatus = if ($packageCheck.productionReady) { 'PASS' } else { 'BLOCKED_EXTERNAL' }; pins = [ordered]@{ engine = Get-SafePinState -PinState $pinStates.engine; service = Get-SafePinState -PinState $pinStates.service } }
            dependencyInventory = 'dependency-inventory.json'
            releaseNotes = 'RELEASE_NOTES.md'
        }
    Write-ReleaseJson -Value $provenance -Path $provenancePath
    $provenanceCheck = Test-ReleaseJsonProjection -Path $provenancePath -ExpectedValue $provenance
    if (-not $provenanceCheck.valid) { throw 'CANDIDATE_PROVENANCE_OUTPUT_MISMATCH' }
    $checksumsPath = Join-Path $output 'SHA256SUMS.txt'
    Write-Checksums -Root $output -OutputPath $checksumsPath -MarkerPath $markerPath
    $finalOutputEntries = @($archiveInventory) + @([pscustomobject]@{
            path = $archiveName
            length = $archiveGuard.Length
            sha256 = $archiveHash
        })
    $outputContentsCheck = Test-ReleaseCandidateOutputContents -Root $output -ExpectedEntries $finalOutputEntries `
        -AdditionalPaths @('candidate-manifest.json', 'provenance.json', 'SHA256SUMS.txt', $script:markerName)
    if (-not $outputContentsCheck.valid) {
        foreach ($guard in @($outputContentsCheck.guards)) { if ($guard) { $guard.Dispose() } }
        throw "CANDIDATE_OUTPUT_CONTENTS_INVALID: $($outputContentsCheck.issues -join ',')"
    }
    $candidateOutputGuards = $outputContentsCheck.guards
    $candidateManifestCheck = Test-ReleaseJsonProjection -Path $candidateManifestPath -ExpectedValue $candidate
    $provenanceCheck = Test-ReleaseJsonProjection -Path $provenancePath -ExpectedValue $provenance
    $checksumsCheck = Test-ReleaseCandidateChecksums -Root $output -ChecksumsPath $checksumsPath -MarkerPath $markerPath
    if (-not $candidateManifestCheck.valid -or -not $provenanceCheck.valid -or -not $checksumsCheck.valid) {
        throw 'CANDIDATE_OUTPUT_METADATA_CHANGED_AFTER_PACKAGING'
    }
    $finalOutputContentsCheck = Test-ReleaseCandidateOutputContents -Root $output -ExpectedEntries $finalOutputEntries `
        -AdditionalPaths @('candidate-manifest.json', 'provenance.json', 'SHA256SUMS.txt', $script:markerName)
    if (-not $finalOutputContentsCheck.valid) {
        foreach ($guard in @($finalOutputContentsCheck.guards)) { if ($guard) { $guard.Dispose() } }
        throw "CANDIDATE_OUTPUT_CHANGED_AFTER_CHECKSUMS: $($finalOutputContentsCheck.issues -join ',')"
    }
    foreach ($guard in @($candidateOutputGuards)) { if ($guard) { $guard.Dispose() } }
    $candidateOutputGuards = $finalOutputContentsCheck.guards

    $result = [ordered]@{
        status = 'PASS'
        output = $output
        outputDirectoryRole = 'mutable-companion-evidence'
        authority = 'archive-only'
        authoritativeArtifact = $archivePath
        authoritativeArtifactSha256 = $archiveHash
        candidateManifest = $candidateManifestPath
        archive = $archivePath
        checksums = $checksumsPath
        provenance = $provenancePath
        version = $version
        commit = $commit
        classification = $classification
        productionReady = $Mode -eq 'production'
        verification = $verification
        note = if ($Mode -eq 'production') { 'Production candidate accepted only because externally supplied signed artifacts, pins, and package-manifest signature passed validation.' } else { 'Unsigned validation artifact only; production signing, pins, and clean-host privileged validation remain required.' }
    }
    if ($OutputFormat -eq 'json') { $result | ConvertTo-Json -Depth 20 } else {
        Write-Host "Authoritative release payload: $archivePath"
        Write-Host "Authoritative payload SHA-256: $archiveHash"
        Write-Host "Mutable companion evidence directory: $output"
        Write-Host "Classification: $classification"
        Write-Host "SHA-256 list: $checksumsPath"
        Write-Host "Production-ready: $($result.productionReady)"
    }
    foreach ($guard in @($candidateOutputGuards)) { if ($guard) { $guard.Dispose() } }
    $candidateOutputGuards = $null
    if ($archiveGuard) { $archiveGuard.Dispose(); $archiveGuard = $null }
    exit 0
} catch {
    foreach ($guard in @($candidateOutputGuards)) { if ($guard) { $guard.Dispose() } }
    $candidateOutputGuards = $null
    if ($archiveGuard) { $archiveGuard.Dispose(); $archiveGuard = $null }
    if ($OutputFormat -eq 'json') { [pscustomobject]@{ status = 'FAIL'; error = $_.Exception.Message } | ConvertTo-Json -Depth 8 } else { Write-Error $_.Exception.Message }
    exit 1
}
