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

$script:markerName = '.fastfiles-release-candidate-v1'
$script:markerText = 'FastFiles-Release-Candidate-v1'

function Get-GitText {
    param([Parameter(Mandatory)] [string[]] $Arguments)

    $value = & git -C $repoRoot @Arguments 2>$null
    if ($LASTEXITCODE -ne 0) { throw "GIT_COMMAND_FAILED: git $($Arguments -join ' ')" }
    return ([string](@($value) -join "`n")).Trim()
}

function Assert-CleanRepository {
    $status = @(git -C $repoRoot status --porcelain=v1 2>$null)
    if ($LASTEXITCODE -ne 0) { throw 'GIT_STATUS_FAILED: could not inspect repository cleanliness.' }
    if ($status.Count -gt 0) {
        throw "RELEASE_CLEAN_TREE_REQUIRED: release candidates require a clean checkout; found $($status.Count) changed path(s)."
    }
}

function Invoke-ReleaseTool {
    param(
        [Parameter(Mandatory)] [string] $FilePath,
        [Parameter(Mandatory)] [string[]] $Arguments,
        [Parameter(Mandatory)] [string] $Name
    )

    & $FilePath @Arguments
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) { throw "${Name}_FAILED: $Name exited with code $exitCode." }
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
    $header = Join-Path $repoRoot 'third_party\sqlite\sqlite3.h'
    if (-not (Test-Path -LiteralPath $header -PathType Leaf)) { return 'unknown' }
    $line = Select-String -LiteralPath $header -Pattern '^#define SQLITE_VERSION "([^"]+)"' | Select-Object -First 1
    if ($line) { return [string]$line.Matches[0].Groups[1].Value }
    return 'unknown'
}

function Write-ReleaseNotes {
    param(
        [Parameter(Mandatory)] [string] $Version,
        [Parameter(Mandatory)] [string] $Classification,
        [Parameter(Mandatory)] [string] $Commit,
        [Parameter(Mandatory)] [string] $Path
    )

    $templatePath = Join-Path $repoRoot 'docs\release\RELEASE_NOTES_TEMPLATE.md'
    if (-not (Test-Path -LiteralPath $templatePath -PathType Leaf)) { throw "RELEASE_NOTES_TEMPLATE_MISSING: $templatePath" }
    $text = [IO.File]::ReadAllText($templatePath)
    $text = $text.Replace('[VERSION]', $Version).Replace('[CLASSIFICATION]', $Classification).Replace('[COMMIT]', $Commit)
    Write-Utf8NoBom -Path $Path -Text $text
}

try {
    Assert-CleanRepository
    $commit = Get-GitText -Arguments @('rev-parse', 'HEAD')
    $commitShort = Get-ShortSha -Commit $commit
    $commitTimestamp = Get-GitText -Arguments @('show', '-s', '--format=%cI', 'HEAD')
    $version = Get-RepositoryProductVersion -RepoRoot $repoRoot
    $classification = if ($Mode -eq 'production') { 'production-signed' } else { 'unsigned-validation' }
    $preset = if ($Mode -eq 'production') { 'release' } else { 'release-test' }
    if (-not $BuildRoot) { $BuildRoot = Join-Path $repoRoot ("build\$preset") }
    $BuildRoot = [IO.Path]::GetFullPath($BuildRoot)

    if (-not $OutputDirectory) {
        $OutputDirectory = Join-Path $repoRoot ("out\release-candidate\FastFiles-{0}-{1}-x64-{2}" -f $version, $classification, $commitShort)
    }
    $output = Prepare-CandidateOutput -Path $OutputDirectory
    $markerPath = Join-Path $output $script:markerName
    $verification = [ordered]@{
        status = if ($SkipVerification) { 'SKIPPED_BY_OPERATOR' } else { 'PENDING' }
        command = 'pwsh ./verify/verify.ps1 build'
        preset = $preset
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
    if ($Mode -eq 'production') { $kitArgs += @('-ManifestSignaturePath', [IO.Path]::GetFullPath($ManifestSignaturePath)) }
    Invoke-ReleaseTool -FilePath (Get-Command pwsh -ErrorAction Stop).Source -Arguments $kitArgs -Name 'RELEASE_KIT'

    $artifactManifestPath = Join-Path $output 'artifact-manifest.json'
    $artifactManifest = Get-Content -LiteralPath $artifactManifestPath -Raw | ConvertFrom-Json
    $versionCheck = Test-ReleaseVersionMetadata -ArtifactRoot $BuildRoot -RepoRoot $repoRoot -AdditionalPaths @((Join-Path $output 'validation\fftest.exe'))
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

    $packageRoot = Join-Path $output 'package'
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
    New-DeterministicArchive -Root $output -ArchivePath $archivePath -ExcludedPath $archivePath -ExcludedMarkerPath $markerPath
    $archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToUpperInvariant()
    $notesPath = Join-Path $output 'RELEASE_NOTES.md'
    Write-ReleaseNotes -Version $version -Classification $classification -Commit $commit -Path $notesPath
    $dependencyPath = Join-Path $output 'dependency-inventory.json'
    Write-ReleaseJson -Value ([ordered]@{
            schemaVersion = 1
            kind = 'fastfiles-dependency-inventory'
            format = 'internal-structured-inventory'
            commit = $commit
            components = @([ordered]@{ name = 'SQLite'; version = Get-SqliteVersion; scope = 'statically linked build/runtime dependency'; source = 'third_party/sqlite' })
            note = 'This is a lightweight dependency inventory, not a claim of a complete SPDX or CycloneDX SBOM.'
        }) -Path $dependencyPath

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
        signed = $Mode -eq 'production'
        productionReady = $Mode -eq 'production'
        sourceCommitTimestampUtc = $commitTimestamp
        artifactManifest = [ordered]@{ path = 'artifact-manifest.json'; sha256 = (Get-FileHash -LiteralPath $artifactManifestPath -Algorithm SHA256).Hash.ToUpperInvariant() }
        package = [ordered]@{ root = 'package'; manifest = 'package/FastFiles.package.manifest'; manifestSha256 = (Get-FileHash -LiteralPath $packageManifestPath -Algorithm SHA256).Hash.ToUpperInvariant(); detachedSignaturePresent = Test-Path -LiteralPath (Join-Path $packageRoot 'FastFiles.package.manifest.p7s') }
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
    $provenancePath = Join-Path $output 'provenance.json'
    Write-ReleaseJson -Value ([ordered]@{
            schemaVersion = 1
            kind = 'fastfiles-release-provenance'
            product = 'FastFiles'
            version = $version
            commit = $commit
            architecture = 'x64'
            buildConfiguration = $artifactManifest.configuration
            classification = $classification
            sourceCommitTimestampUtc = $commitTimestamp
            candidateManifestSha256 = (Get-FileHash -LiteralPath $candidateManifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
            archiveSha256 = $archiveHash
            signing = [ordered]@{ requiredForProduction = $true; binaryStatus = if ($binarySigningPass) { 'PASS' } else { 'BLOCKED_EXTERNAL' }; packageManifestStatus = if ($packageCheck.productionReady) { 'PASS' } else { 'BLOCKED_EXTERNAL' }; pins = [ordered]@{ engine = Get-SafePinState -PinState $pinStates.engine; service = Get-SafePinState -PinState $pinStates.service } }
            dependencyInventory = 'dependency-inventory.json'
            releaseNotes = 'RELEASE_NOTES.md'
        }) -Path $provenancePath
    $checksumsPath = Join-Path $output 'SHA256SUMS.txt'
    Write-Checksums -Root $output -OutputPath $checksumsPath -MarkerPath $markerPath

    $result = [ordered]@{
        status = 'PASS'
        output = $output
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
        Write-Host "Release candidate ready: $output"
        Write-Host "Classification: $classification"
        Write-Host "Archive: $archivePath"
        Write-Host "SHA-256 list: $checksumsPath"
        Write-Host "Production-ready: $($result.productionReady)"
    }
    exit 0
} catch {
    if ($OutputFormat -eq 'json') { [pscustomobject]@{ status = 'FAIL'; error = $_.Exception.Message } | ConvertTo-Json -Depth 8 } else { Write-Error $_.Exception.Message }
    exit 1
}
