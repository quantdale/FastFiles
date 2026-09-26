[CmdletBinding()]
param(
    [string] $RepoRoot = (Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent),
    [string] $BuildRoot,
    [string] $RetainedEvidenceDirectory
)

$ErrorActionPreference = 'Stop'
if (-not $BuildRoot) { $BuildRoot = Join-Path $RepoRoot 'build\release-test' }
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
$candidateScript = Join-Path $RepoRoot 'verify\release\Build-ReleaseCandidate.ps1'
$releaseKitScript = Join-Path $RepoRoot 'verify\release\New-ReleaseKit.ps1'
$provenanceModule = Join-Path $RepoRoot 'verify\core\Provenance.psm1'
Import-Module $provenanceModule -Force -DisableNameChecking
Import-Module (Join-Path $RepoRoot 'verify\core\Schema.psm1') -Force
$candidateProvenanceSchema = Get-Content -LiteralPath (Join-Path $RepoRoot 'verify\release\schemas\release-build-root-provenance.schema.json') -Raw | ConvertFrom-Json
$failures = 0

function Check {
    param([bool] $Condition, [string] $Description)
    if ($Condition) { Write-Host "PASS: $Description" } else { Write-Host "FAIL: $Description"; $script:failures++ }
}

function Invoke-Checked {
    param([string] $FilePath, [string[]] $Arguments)
    & $FilePath @Arguments
    $exitCode = $LASTEXITCODE
    Check ($exitCode -eq 0) "command succeeds: $([IO.Path]::GetFileName($FilePath)) $($Arguments -join ' ')"
    return $exitCode
}

function Remove-TempDirectory {
    param([string] $Path)
    if (-not $Path -or -not (Test-Path -LiteralPath $Path)) { return }
    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    $temp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
    if (-not $full.StartsWith($temp + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "TEST_TEMP_ROOT_ESCAPE: $full" }
    Remove-Item -LiteralPath $full -Recurse -Force
}

function Get-ZipEntrySha256 {
    param([string] $ArchivePath, [string] $EntryPath)

    Add-Type -AssemblyName System.IO.Compression -ErrorAction SilentlyContinue
    Add-Type -AssemblyName System.IO.Compression.FileSystem -ErrorAction SilentlyContinue
    $archive = [IO.Compression.ZipFile]::OpenRead($ArchivePath)
    try {
        $entry = $archive.GetEntry($EntryPath)
        if ($null -eq $entry) { return $null }
        $stream = $entry.Open()
        $sha = [Security.Cryptography.SHA256]::Create()
        try { return [Convert]::ToHexString($sha.ComputeHash($stream)) }
        finally { $sha.Dispose(); $stream.Dispose() }
    } finally { $archive.Dispose() }
}

function Copy-CertificationBuildRoot {
    param([string] $SourceRoot, [string] $DestinationRoot)

    New-Item -ItemType Directory -Path $DestinationRoot -Force | Out-Null
    foreach ($sourceBinary in @(Get-DefaultCertificationBinaries -RepoRoot $RepoRoot -BuildDirectory $SourceRoot)) {
        $relativeBinary = $sourceBinary.Substring($SourceRoot.TrimEnd('\').Length).TrimStart('\')
        $destinationBinary = Join-Path $DestinationRoot $relativeBinary
        New-Item -ItemType Directory -Path (Split-Path -Parent $destinationBinary) -Force | Out-Null
        Copy-Item -LiteralPath $sourceBinary -Destination $destinationBinary -Force
    }
}

function Replace-EmbeddedBuildConfigurationField {
    param(
        [Parameter(Mandatory)] [string] $Path,
        [Parameter(Mandatory)] [string] $Configuration,
        [string] $FieldName = 'config',
        [string] $ReplacementConfiguration = ''
    )

    if (-not $PSBoundParameters.ContainsKey('ReplacementConfiguration')) { $ReplacementConfiguration = $Configuration }
    $oldText = "config=$Configuration"
    $newText = "$FieldName=$ReplacementConfiguration"
    if ($oldText.Length -ne $newText.Length) { throw 'TEST_VERSIONINFO_REPLACEMENT_LENGTH_MISMATCH' }
    $bytes = [IO.File]::ReadAllBytes($Path)
    $oldBytes = [Text.Encoding]::Unicode.GetBytes($oldText)
    $newBytes = [Text.Encoding]::Unicode.GetBytes($newText)
    $offsets = [System.Collections.Generic.List[int]]::new()
    $start = 0
    while ($start -le $bytes.Length - $oldBytes.Length) {
        $offset = [Array]::IndexOf($bytes, $oldBytes[0], $start)
        if ($offset -lt 0 -or $offset -gt $bytes.Length - $oldBytes.Length) { break }
        $matches = $true
        for ($index = 0; $index -lt $oldBytes.Length; $index++) {
            if ($bytes[$offset + $index] -ne $oldBytes[$index]) { $matches = $false; break }
        }
        if ($matches) { $offsets.Add($offset) }
        $start = $offset + 1
    }
    if ($offsets.Count -ne 1) { throw "TEST_VERSIONINFO_TOKEN_COUNT: expected one '$oldText' in $Path, found $($offsets.Count)." }
    [Array]::Copy($newBytes, 0, $bytes, $offsets[0], $newBytes.Length)
    [IO.File]::WriteAllBytes($Path, $bytes)
}

function Prepare-RetainedEvidenceDirectory {
    param([string] $Path)

    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    $root = [IO.Path]::GetPathRoot($full).TrimEnd('\')
    if ([string]::IsNullOrWhiteSpace($full) -or $full -eq $root) { throw "RETAINED_EVIDENCE_INVALID: refusing a filesystem root: $full" }
    if (Test-Path -LiteralPath $full) {
        $item = Get-Item -LiteralPath $full -Force
        if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "RETAINED_EVIDENCE_INVALID: output must be a non-reparse directory: $full"
        }
    } else {
        New-Item -ItemType Directory -Path $full -Force | Out-Null
    }
    foreach ($name in @('candidate-manifest.json', 'provenance.json')) {
        $pathToClear = Join-Path $full $name
        if (Test-Path -LiteralPath $pathToClear) {
            $item = Get-Item -LiteralPath $pathToClear -Force
            if (-not $item.PSIsContainer -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) {
                Remove-Item -LiteralPath $pathToClear -Force
            } else {
                throw "RETAINED_EVIDENCE_INVALID: expected a regular file at $pathToClear"
            }
        }
    }
    return $full
}

function Publish-RetainedEvidence {
    param(
        [string] $SourceDirectory,
        [string] $DestinationDirectory
    )

    $destination = Prepare-RetainedEvidenceDirectory -Path $DestinationDirectory
    foreach ($name in @('candidate-manifest.json', 'provenance.json')) {
        $source = Join-Path $SourceDirectory $name
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "RETAINED_EVIDENCE_SOURCE_MISSING: $source" }
        Copy-Item -LiteralPath $source -Destination (Join-Path $destination $name) -Force | Out-Null
    }
    return $destination
}

$outputA = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-release-candidate-test-a-' + [guid]::NewGuid().ToString('N'))
$outputB = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-release-candidate-test-b-' + [guid]::NewGuid().ToString('N'))
$retainedEvidence = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-release-candidate-retained-' + [guid]::NewGuid().ToString('N'))
$missingBuildRoot = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-release-candidate-missing-build-' + [guid]::NewGuid().ToString('N'))
$missingBuildOutput = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-release-candidate-missing-output-' + [guid]::NewGuid().ToString('N'))
$unboundBuildRoot = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-release-candidate-unbound-build-' + [guid]::NewGuid().ToString('N'))
$unboundBuildOutput = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-release-candidate-unbound-output-' + [guid]::NewGuid().ToString('N'))
$debugBuildRoot = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-release-candidate-debug-build-' + [guid]::NewGuid().ToString('N'))
$debugBuildOutput = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-release-candidate-debug-output-' + [guid]::NewGuid().ToString('N'))
$missingConfigurationBuildRoot = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-release-candidate-null-config-build-' + [guid]::NewGuid().ToString('N'))
$missingConfigurationOutput = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-release-candidate-null-config-output-' + [guid]::NewGuid().ToString('N'))
$mixedConfigurationBuildRoot = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-release-candidate-mixed-config-build-' + [guid]::NewGuid().ToString('N'))
$mixedConfigurationOutput = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-release-candidate-mixed-config-output-' + [guid]::NewGuid().ToString('N'))
$productionSkipBuildRoot = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-release-candidate-production-skip-build-' + [guid]::NewGuid().ToString('N'))
$productionSkipOutput = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-release-candidate-production-skip-output-' + [guid]::NewGuid().ToString('N'))
$productionRelWithOutput = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-release-candidate-production-relwith-output-' + [guid]::NewGuid().ToString('N'))
$directKitOutput = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-release-candidate-direct-kit-' + [guid]::NewGuid().ToString('N'))
$postKitFailureOutput = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-release-candidate-post-kit-failure-' + [guid]::NewGuid().ToString('N'))
if (-not $RetainedEvidenceDirectory) { $RetainedEvidenceDirectory = $retainedEvidence }
try {
    [void](Prepare-RetainedEvidenceDirectory -Path $RetainedEvidenceDirectory)
    Check (Test-Path -LiteralPath $candidateScript -PathType Leaf) 'release-candidate entry point exists'
    $verifyText = Get-Content -LiteralPath (Join-Path $RepoRoot 'verify\verify.ps1') -Raw
    Check ($verifyText -match "release-candidate") 'verify.ps1 exposes release-candidate'
    Check ((Get-Content -LiteralPath $candidateScript -Raw) -match 'RELEASE_CLEAN_TREE_REQUIRED') 'candidate rejects dirty trees'
    Check ((Get-Content -LiteralPath $candidateScript -Raw) -match 'PRODUCTION_PIN_REQUIRED') 'candidate keeps production pins fail-closed'
    $candidateSource = Get-Content -LiteralPath $candidateScript -Raw
    Check ($candidateSource -match 'Assert-CandidateSourceUnchanged -Before \$candidateSourceStart' -and
        $candidateSource -match 'stateDigest.*Before\.stateDigest') 'candidate rechecks exact source state after packaging inputs are read'
    Check ($candidateSource -match 'New-CandidateArchiveSnapshot' -and
        $candidateSource -match 'Get-ReleaseArchiveInventory' -and
        $candidateSource -match 'Test-ReleaseArchiveContents') 'candidate archives a guarded snapshot and validates every final ZIP entry'
    Check ($candidateSource -match 'Test-ReleaseCandidateSnapshotContents' -and
        $candidateSource -match 'ExpectedSqliteVersion' -and
        $candidateSource -match 'CANDIDATE_ARCHIVE_SNAPSHOT_INVALID') 'candidate enforces an exact archive member allowlist and validates dependency inventory identity'
    Check ($candidateSource -match 'Test-ReleaseKitManifestProjection' -and
        $candidateSource -match 'pinProvisioning\s*=\s*\[ordered\]@' -and
        $candidateSource -match 'productionSigning\s*=\s*\[ordered\]@') 'candidate validates the complete kit manifest including privileged pin provisioning'
    Check ($candidateSource -match 'Assert-CandidateOutputMatchesHeadBlob' -and
        $candidateSource -match "'ReleaseCommon\.psm1' = 'verify/release/ReleaseCommon\.psm1'") 'candidate verifies copied release helper payloads against the committed blobs'
    Check ($candidateSource -match '\$toolOutput = @\(& \$FilePath @Arguments \*>&1\)' -and
        $candidateSource -match 'FASTFILES_TEST_RELEASE_CANDIDATE_FAIL_AFTER_KIT') 'candidate captures child output and has a fail-only post-kit JSON regression seam'
    $checksumGateIndex = $candidateSource.IndexOf('$checksumsCheck = Test-ReleaseCandidateChecksums', [StringComparison]::Ordinal)
    $finalOutputGateIndex = $candidateSource.LastIndexOf('Test-ReleaseCandidateOutputContents', [StringComparison]::Ordinal)
    $resultObjectIndex = $candidateSource.IndexOf('$result = [ordered]@{', [StringComparison]::Ordinal)
    Check ($checksumGateIndex -ge 0 -and $finalOutputGateIndex -gt $checksumGateIndex -and
        $resultObjectIndex -gt $finalOutputGateIndex) 'candidate repeats exact output inventory validation after checksum enumeration and before PASS'

    $cmake = (Get-Command cmake -ErrorAction Stop).Source
    Invoke-Checked -FilePath $cmake -Arguments @('--preset', 'release-test') | Out-Null
    Invoke-Checked -FilePath $cmake -Arguments @('--build', '--preset', 'release-test') | Out-Null
    Invoke-Checked -FilePath $cmake -Arguments @('--preset', 'debug') | Out-Null
    Invoke-Checked -FilePath $cmake -Arguments @('--build', '--preset', 'debug', '--target', 'FastFiles', 'FastFilesEngine', 'FastFilesIndexSvc', 'FastFilesSetup', 'fftest') | Out-Null
    $pwsh = (Get-Command pwsh -ErrorAction Stop).Source
    $debugPresetBuildRoot = Join-Path $RepoRoot 'build\debug'
    $directKitMismatch = & $pwsh -NoProfile -NonInteractive -File $releaseKitScript -BuildRoot $debugPresetBuildRoot `
        -OutputDirectory $directKitOutput -Configuration Release -Force -OutputFormat json 2>&1
    $directKitMismatchExit = $LASTEXITCODE
    $directKitMismatchText = $directKitMismatch | Out-String
    Check ($directKitMismatchExit -eq 1 -and $directKitMismatchText -match 'RELEASE_ARTIFACT_CONFIGURATION_MISMATCH' -and
        $directKitMismatchText -match 'expected ''Release''') 'direct release-kit invocation rejects Debug executables mislabeled as Release'
    $candidateJsonArgs = @('-NoProfile', '-NonInteractive', '-File', $candidateScript, '-Mode', 'unsigned-validation', '-BuildRoot', $BuildRoot, '-OutputDirectory', $outputA, '-SkipVerification', '-OutputFormat', 'json')
    $candidateJsonOutput = & $pwsh @candidateJsonArgs 2>&1
    $candidateJsonExit = $LASTEXITCODE
    $candidateJsonText = (@($candidateJsonOutput | ForEach-Object { [string]$_ }) -join [Environment]::NewLine).Trim()
    $candidateJson = $null
    try { $candidateJson = $candidateJsonText | ConvertFrom-Json -ErrorAction Stop } catch { }
    Check ($candidateJsonExit -eq 0 -and $candidateJson -and $candidateJson.status -ceq 'PASS' -and
        [string]$candidateJson.output -ceq $outputA -and $candidateJson.authority -ceq 'archive-only' -and
        $candidateJson.outputDirectoryRole -ceq 'mutable-companion-evidence' -and
        [string]$candidateJson.authoritativeArtifact -ceq [string]$candidateJson.archive -and
        [string]$candidateJson.authoritativeArtifactSha256 -match '^[0-9A-F]{64}$') `
        'JSON mode emits one terminal PASS document and names the ZIP as its sole authoritative payload'
    $hasAuthoritativeArchive = $candidateJson -and $candidateJson.authoritativeArtifact -and
        (Test-Path -LiteralPath ([string]$candidateJson.authoritativeArtifact) -PathType Leaf)
    if ($hasAuthoritativeArchive) {
        $candidateArchiveHashBeforeCompanionMutation = (Get-FileHash -LiteralPath $candidateJson.authoritativeArtifact -Algorithm SHA256).Hash
        Check ($candidateArchiveHashBeforeCompanionMutation -ceq [string]$candidateJson.authoritativeArtifactSha256) `
            'terminal result carries the observed hash of its authoritative archive'
        $lateCompanionPath = Join-Path $outputA 'late-untrusted-sibling.dll'
        [IO.File]::WriteAllText($lateCompanionPath, 'not an authoritative payload', [Text.UTF8Encoding]::new($false))
        $candidateArchiveHashAfterCompanionMutation = (Get-FileHash -LiteralPath $candidateJson.authoritativeArtifact -Algorithm SHA256).Hash
        $lateCompanionArchiveEntry = Get-ZipEntrySha256 -ArchivePath $candidateJson.authoritativeArtifact -EntryPath 'late-untrusted-sibling.dll'
        Check ($candidateArchiveHashBeforeCompanionMutation -ceq $candidateArchiveHashAfterCompanionMutation -and
            $null -eq $lateCompanionArchiveEntry) 'a post-return sibling addition does not alter or enter the authoritative ZIP'
        Remove-Item -LiteralPath $lateCompanionPath -Force
        $archiveReplacementBackup = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-candidate-archive-backup-' + [guid]::NewGuid().ToString('N') + '.zip')
        Copy-Item -LiteralPath $candidateJson.authoritativeArtifact -Destination $archiveReplacementBackup -Force
        try {
            [IO.File]::WriteAllText($candidateJson.authoritativeArtifact, 'replaced archive bytes', [Text.UTF8Encoding]::new($false))
            $replacementArchiveHash = (Get-FileHash -LiteralPath $candidateJson.authoritativeArtifact -Algorithm SHA256).Hash
            Check ($replacementArchiveHash -cne [string]$candidateJson.authoritativeArtifactSha256) `
                'a post-return authoritative archive replacement is detected by the terminal result hash'
        } finally {
            Copy-Item -LiteralPath $archiveReplacementBackup -Destination $candidateJson.authoritativeArtifact -Force
            Remove-Item -LiteralPath $archiveReplacementBackup -Force
        }
    } else {
        Check $false 'terminal result carries the observed hash of its authoritative archive'
        Check $false 'a post-return sibling addition does not alter or enter the authoritative ZIP'
        Check $false 'a post-return authoritative archive replacement is detected by the terminal result hash'
    }

    $oldPostKitFailpoint = [Environment]::GetEnvironmentVariable('FASTFILES_TEST_RELEASE_CANDIDATE_FAIL_AFTER_KIT', 'Process')
    try {
        [Environment]::SetEnvironmentVariable('FASTFILES_TEST_RELEASE_CANDIDATE_FAIL_AFTER_KIT', '1', 'Process')
        $postKitFailureArgs = @('-NoProfile', '-NonInteractive', '-File', $candidateScript, '-Mode', 'unsigned-validation', '-BuildRoot', $BuildRoot, '-OutputDirectory', $postKitFailureOutput, '-SkipVerification', '-OutputFormat', 'json')
        $postKitFailureOutputText = & $pwsh @postKitFailureArgs 2>&1
        $postKitFailureExit = $LASTEXITCODE
    } finally {
        [Environment]::SetEnvironmentVariable('FASTFILES_TEST_RELEASE_CANDIDATE_FAIL_AFTER_KIT', $oldPostKitFailpoint, 'Process')
    }
    $postKitFailureText = (@($postKitFailureOutputText | ForEach-Object { [string]$_ }) -join [Environment]::NewLine).Trim()
    $postKitFailureJson = $null
    try { $postKitFailureJson = $postKitFailureText | ConvertFrom-Json -ErrorAction Stop } catch { }
    Check ($postKitFailureExit -eq 1 -and $postKitFailureJson -and $postKitFailureJson.status -ceq 'FAIL' -and
        $postKitFailureJson.error -ceq 'TEST_ONLY_POST_KIT_FAILURE' -and
        $postKitFailureText -notmatch '(?s)\{\s*"status"\s*:\s*"PASS"') `
        'post-kit failure emits one parseable terminal FAIL document and suppresses the successful child JSON'

    $candidateArgs = @('-NoProfile', '-NonInteractive', '-File', $candidateScript, '-Mode', 'unsigned-validation', '-BuildRoot', $BuildRoot, '-OutputDirectory', $outputB, '-SkipVerification', '-OutputFormat', 'human')
    Invoke-Checked -FilePath $pwsh -Arguments $candidateArgs | Out-Null

    foreach ($output in @($outputA, $outputB)) {
        foreach ($name in @('candidate-manifest.json', 'provenance.json', 'SHA256SUMS.txt', 'RELEASE_NOTES.md', 'dependency-inventory.json')) {
            Check (Test-Path -LiteralPath (Join-Path $output $name) -PathType Leaf) "candidate emits $name"
        }
        $manifest = Get-Content -LiteralPath (Join-Path $output 'candidate-manifest.json') -Raw | ConvertFrom-Json
        Check ([string]$manifest.classification -ceq 'unsigned-validation') 'candidate manifest classifies unsigned output explicitly'
        Check ([string]$manifest.payloadAuthority -ceq 'archive-only') 'candidate manifest declares only the ZIP as authoritative payload'
        $provenanceDocument = Get-Content -LiteralPath (Join-Path $output 'provenance.json') -Raw | ConvertFrom-Json
        Check ([string]$provenanceDocument.payloadAuthority -ceq 'archive-only') 'provenance marks loose output files as companion evidence'
        Check (-not [bool]$manifest.productionReady -and -not [bool]$manifest.signed) 'unsigned candidate is not production-ready'
        Check ([string]$manifest.binaryProvenance.stage -ceq 'release-candidate-build-root' -and [string]$manifest.binaryProvenance.buildRoot) 'candidate provenance binds the selected BuildRoot through the strict gate'
        Check ([string]$manifest.binaryProvenance.expectedConfiguration -ceq 'RelWithDebInfo' -and
            [string]$manifest.binaryProvenance.artifactManifestConfiguration -ceq 'release-test' -and
            @($manifest.binaryProvenance.binaries | Where-Object { $_.configuration -cne 'RelWithDebInfo' }).Count -eq 0) 'unsigned candidate binds all selected binaries and artifact manifest to the optimized test configuration'
        $candidateProvenanceResult = Test-JsonSchema -Data $manifest.binaryProvenance -Schema $candidateProvenanceSchema
        Check ($candidateProvenanceResult.Valid) 'release candidate projection validates against its distinct deterministic schema'
        $semanticProjectionResult = Test-ReleaseBuildRootProvenance -Provenance $manifest.binaryProvenance
        Check ($semanticProjectionResult.valid) 'release candidate projection passes exact inventory and cross-field semantic validation'
        $invalidProjectionPair = $manifest.binaryProvenance | ConvertTo-Json -Depth 12 | ConvertFrom-Json
        $invalidProjectionPair.artifactManifestConfiguration = 'Release'
        Check (-not (Test-ReleaseBuildRootProvenance -Provenance $invalidProjectionPair).valid) 'semantic validator rejects a mismatched expected and artifact-manifest configuration pair'
        $invalidProjectionInventory = $manifest.binaryProvenance | ConvertTo-Json -Depth 12 | ConvertFrom-Json
        $invalidProjectionInventory.binaries = @($invalidProjectionInventory.binaries | Where-Object { $_.filename -cne 'fftest.exe' })
        Check (-not (Test-ReleaseBuildRootProvenance -Provenance $invalidProjectionInventory).valid) 'semantic validator rejects a missing required executable from an otherwise PASS inventory'
        $invalidProjectionIssues = $manifest.binaryProvenance | ConvertTo-Json -Depth 12 | ConvertFrom-Json
        $invalidProjectionIssues.binaries[0].issues = @('HASH_READ_ERROR')
        Check (-not (Test-ReleaseBuildRootProvenance -Provenance $invalidProjectionIssues).valid) 'semantic validator rejects a PASS record carrying collector issues'
        $invalidCandidateProjection = $manifest.binaryProvenance | ConvertTo-Json -Depth 12 | ConvertFrom-Json
        $invalidCandidateProjection.PSObject.Properties.Remove('boundToHead')
        $invalidCandidateResult = Test-JsonSchema -Data $invalidCandidateProjection -Schema $candidateProvenanceSchema
        Check (-not $invalidCandidateResult.Valid) 'release candidate projection schema rejects a missing required binding field'
        Check ([bool]$manifest.binaryProvenance.boundToHead -and -not [bool]$manifest.binaryProvenance.boundToTree -and [string]$manifest.binaryProvenance.treeBindingStatus -ceq 'NOT_EMBEDDED') 'candidate reports commit binding without overstating tree binding'
        Check (@($manifest.binaryProvenance.binaries).Count -eq 5 -and @($manifest.binaryProvenance.binaries | Where-Object { $_.sha256 -notmatch '^[0-9a-f]{64}$' -or $_.peArchitecture -ne 'x64' -or $_.worktreeState -ne 'clean' }).Count -eq 0) 'candidate records hashes, x64 PE checks, and clean build claims for all five required executables'
        Check (@($manifest.binaryProvenance.binaries | Where-Object { $_.path -match '^[A-Za-z]:[\\/]|^\\\\' }).Count -eq 0) 'candidate provenance uses portable selected-BuildRoot paths'
        Check ([string]$manifest.validation.status -ceq 'SKIPPED_BY_OPERATOR' -and [string]$manifest.validation.selectedBuildRootStatus -ceq 'STRICT_PROVENANCE_PASS') 'candidate separates skipped canonical build verification from selected-root provenance'
        $artifactManifest = Get-Content -LiteralPath (Join-Path $output 'artifact-manifest.json') -Raw | ConvertFrom-Json
        Check ([string]$artifactManifest.configuration -ceq 'release-test' -and [string]$manifest.buildConfiguration -ceq [string]$artifactManifest.configuration) 'artifact manifest configuration agrees with the unsigned candidate mode'
        Check (@($artifactManifest.artifacts | Where-Object {
                    $artifact = $_
                    $bound = @($manifest.binaryProvenance.binaries | Where-Object filename -eq $artifact.filename) | Select-Object -First 1
                    $null -eq $bound -or [string]$bound.sha256 -ine [string]$artifact.sha256
                }).Count -eq 0) 'packaged product artifact hashes still match the selected-root provenance hashes'
        $candidateArchive = Get-ChildItem -LiteralPath $output -Filter '*.zip' -File | Select-Object -First 1
        $archiveManifestMatches = $false
        $archivePayloadMismatches = @()
        $archiveFftestMatches = $false
        $archiveHelperMismatches = @()
        $archiveNotesMatch = $false
        if ($candidateArchive) {
            $archiveManifestHash = Get-ZipEntrySha256 -ArchivePath $candidateArchive.FullName -EntryPath 'artifact-manifest.json'
            $diskManifestHash = (Get-FileHash -LiteralPath (Join-Path $output 'artifact-manifest.json') -Algorithm SHA256).Hash
            $archiveManifestMatches = $archiveManifestHash -and $archiveManifestHash -ieq $diskManifestHash
            $archivePayloadMismatches = @($artifactManifest.artifacts | Where-Object {
                    $entryHash = Get-ZipEntrySha256 -ArchivePath $candidateArchive.FullName -EntryPath ("package/{0}" -f $_.filename)
                    $null -eq $entryHash -or $entryHash -ine [string]$_.sha256
                })
            $archiveFftestHash = Get-ZipEntrySha256 -ArchivePath $candidateArchive.FullName -EntryPath 'validation/fftest.exe'
            $archiveFftestMatches = $archiveFftestHash -and $archiveFftestHash -ieq [string](@($manifest.binaryProvenance.binaries | Where-Object filename -eq 'fftest.exe' | Select-Object -First 1).sha256)
            $archiveHelperNames = @('ReleaseCommon.psm1', 'derive-pin.ps1', 'sign-release.ps1', 'validate-clean-host.ps1',
                'cleanup-clean-host.ps1', 'privileged-release.ps1', 'import-clean-host-result.ps1', 'README.md')
            $archiveHelperMismatches = @($archiveHelperNames | Where-Object {
                    $entryHash = Get-ZipEntrySha256 -ArchivePath $candidateArchive.FullName -EntryPath $_
                    $fileHash = (Get-FileHash -LiteralPath (Join-Path $output $_) -Algorithm SHA256).Hash
                    $null -eq $entryHash -or $entryHash -ine $fileHash
                })
            $archiveNotesHash = Get-ZipEntrySha256 -ArchivePath $candidateArchive.FullName -EntryPath 'RELEASE_NOTES.md'
            $diskNotesHash = (Get-FileHash -LiteralPath (Join-Path $output 'RELEASE_NOTES.md') -Algorithm SHA256).Hash
            $archiveNotesMatch = $archiveNotesHash -and $archiveNotesHash -ieq $diskNotesHash
        }
        Check ($archiveManifestMatches -and $archivePayloadMismatches.Count -eq 0 -and $archiveFftestMatches -and
            $archiveHelperMismatches.Count -eq 0 -and $archiveNotesMatch) `
            'final archive preserves the checked artifact manifest, binaries, validation probe, helpers, and generated release notes'
        $fftestHash = (Get-FileHash -LiteralPath (Join-Path $output 'validation\fftest.exe') -Algorithm SHA256).Hash
        $boundFftest = @($manifest.binaryProvenance.binaries | Where-Object filename -eq 'fftest.exe') | Select-Object -First 1
        Check ($boundFftest -and [string]$boundFftest.sha256 -ieq $fftestHash) 'packaged validation probe hash still matches the selected-root provenance hash'
        Check ([string]$manifest.version -match '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') 'candidate manifest carries strict product version'
        $versionPaths = @($manifest.versionConsistency.artifacts | ForEach-Object { [string]$_.path })
        Check (@($versionPaths | Where-Object { $_ -match '^[A-Za-z]:[\\/]|^\\\\' }).Count -eq 0) 'candidate metadata contains no absolute artifact paths'
        Check (@($versionPaths | Where-Object { $_ -notmatch '^(package|validation)/[^/]+$' }).Count -eq 0) 'candidate metadata uses portable artifact paths'
        $pinSources = @(
            [string]$manifest.pins.engine.source,
            [string]$manifest.pins.service.source,
            @($manifest.signatures.matrix | ForEach-Object { if ($_.pinState) { [string]$_.pinState.source } })
        )
        Check (@($pinSources | Where-Object { $_ -match '^[A-Za-z]:[\\/]|^\\\\' }).Count -eq 0) 'candidate metadata redacts pin source paths'
        $checksumLines = @(Get-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') | Where-Object { $_ -and $_ -notmatch '^\s*#' })
        Check ($checksumLines.Count -gt 0) 'candidate checksum list is non-empty'
        foreach ($line in $checksumLines) {
            if ($line -notmatch '^([0-9A-F]{64})  (.+)$') { Check $false 'checksum row has standard SHA-256 format'; continue }
            $hash = (Get-FileHash -LiteralPath (Join-Path $output $Matches[2]) -Algorithm SHA256).Hash.ToUpperInvariant()
            Check ($hash -ceq $Matches[1]) "checksum matches $($Matches[2])"
        }
    }
    $archiveA = (Get-FileHash -LiteralPath (Get-ChildItem -LiteralPath $outputA -Filter '*.zip' -File | Select-Object -First 1).FullName -Algorithm SHA256).Hash
    $archiveB = (Get-FileHash -LiteralPath (Get-ChildItem -LiteralPath $outputB -Filter '*.zip' -File | Select-Object -First 1).FullName -Algorithm SHA256).Hash
    Check ($archiveA -ceq $archiveB) 'repeated candidate archives have identical SHA-256'

    $published = Publish-RetainedEvidence -SourceDirectory $outputA -DestinationDirectory $RetainedEvidenceDirectory
    foreach ($name in @('candidate-manifest.json', 'provenance.json')) {
        $retainedPath = Join-Path $published $name
        Check (Test-Path -LiteralPath $retainedPath -PathType Leaf) "retained evidence emits $name"
        if (Test-Path -LiteralPath $retainedPath -PathType Leaf) {
            Check ((Get-Item -LiteralPath $retainedPath).Length -gt 0) "retained evidence $name is non-empty"
        }
    }
    $sourceManifest = Get-Content -LiteralPath (Join-Path $outputA 'candidate-manifest.json') -Raw | ConvertFrom-Json
    $retainedManifest = Get-Content -LiteralPath (Join-Path $published 'candidate-manifest.json') -Raw | ConvertFrom-Json
    $retainedProvenance = Get-Content -LiteralPath (Join-Path $published 'provenance.json') -Raw | ConvertFrom-Json
    Check ([string]$retainedManifest.commit -ceq [string]$sourceManifest.commit) 'retained candidate manifest is bound to the qualified commit'
    Check ([string]$retainedProvenance.commit -ceq [string]$sourceManifest.commit) 'retained provenance is bound to the qualified commit'
    $provenancePinSources = @(
        [string]$retainedProvenance.signing.pins.engine.source,
        [string]$retainedProvenance.signing.pins.service.source
    )
    Check (@($provenancePinSources | Where-Object { $_ -match '^[A-Za-z]:[\\/]|^\\\\' }).Count -eq 0) 'retained provenance contains no absolute pin source paths'
    $retainedManifestHash = (Get-FileHash -LiteralPath (Join-Path $published 'candidate-manifest.json') -Algorithm SHA256).Hash.ToUpperInvariant()
    Check ([string]$retainedProvenance.candidateManifestSha256 -ceq $retainedManifestHash) 'retained provenance binds the retained candidate manifest'
    Check ([string]$retainedProvenance.binaryProvenance.stage -ceq 'release-candidate-build-root' -and [bool]$retainedProvenance.binaryProvenance.boundToHead) 'retained provenance preserves selected-BuildRoot source binding'

    # A caller-selected empty directory must fail against that directory rather
    # than silently verifying or packaging the repository's default preset root.
    New-Item -ItemType Directory -Path $missingBuildRoot -Force | Out-Null
    $customRootWithoutSkip = & $pwsh -NoProfile -NonInteractive -File $candidateScript -Mode unsigned-validation `
        -BuildRoot $missingBuildRoot -OutputDirectory $missingBuildOutput -OutputFormat json 2>&1
    $customRootWithoutSkipExit = $LASTEXITCODE
    $customRootWithoutSkipText = $customRootWithoutSkip | Out-String
    Check ($customRootWithoutSkipExit -eq 1 -and $customRootWithoutSkipText -match 'RELEASE_BUILD_ROOT_VERIFICATION_MISMATCH') 'candidate refuses to label a custom root as covered by the canonical build gate'

    # A same-HEAD Debug artifact set still cannot be packaged as an unsigned
    # release-validation candidate, even when its source binding is otherwise valid.
    $debugPresetBuildRoot = Join-Path $RepoRoot 'build\debug'
    Copy-CertificationBuildRoot -SourceRoot $debugPresetBuildRoot -DestinationRoot $debugBuildRoot
    $debugRootOutput = & $pwsh -NoProfile -NonInteractive -File $candidateScript -Mode unsigned-validation `
        -BuildRoot $debugBuildRoot -OutputDirectory $debugBuildOutput -SkipVerification -OutputFormat json 2>&1
    $debugRootExit = $LASTEXITCODE
    $debugRootText = $debugRootOutput | Out-String
    Check ($debugRootExit -eq 1 -and $debugRootText -match 'RELEASE_BUILD_CONFIGURATION_MISMATCH' -and
        $debugRootText -match 'expected config=RelWithDebInfo' -and $debugRootText -match 'Debug') 'candidate rejects same-HEAD Debug binaries copied into a custom Release BuildRoot'

    Copy-CertificationBuildRoot -SourceRoot $BuildRoot -DestinationRoot $missingConfigurationBuildRoot
    $missingConfigurationBinary = Join-Path $missingConfigurationBuildRoot 'src\engine\FastFilesEngine.exe'
    Replace-EmbeddedBuildConfigurationField -Path $missingConfigurationBinary -Configuration 'RelWithDebInfo' -FieldName 'detail'
    $missingConfigurationRecord = Get-BinaryProvenanceRecord -Path $missingConfigurationBinary -RepoRoot $RepoRoot
    Check ($null -eq $missingConfigurationRecord.configuration -and $missingConfigurationRecord.buildIdentityFormatValid -and
        $missingConfigurationRecord.worktreeState -eq 'clean') 'missing-configuration fixture preserves the strict source identity while omitting its config field'
    $missingConfigurationResult = & $pwsh -NoProfile -NonInteractive -File $candidateScript -Mode unsigned-validation `
        -BuildRoot $missingConfigurationBuildRoot -OutputDirectory $missingConfigurationOutput -SkipVerification -OutputFormat json 2>&1
    $missingConfigurationExit = $LASTEXITCODE
    $missingConfigurationText = $missingConfigurationResult | Out-String
    Check ($missingConfigurationExit -eq 1 -and $missingConfigurationText -match 'RELEASE_BUILD_CONFIGURATION_MISMATCH' -and
        $missingConfigurationText -match 'FastFilesEngine.exe=unavailable') 'candidate rejects a required executable with a missing embedded configuration'

    Copy-CertificationBuildRoot -SourceRoot $BuildRoot -DestinationRoot $mixedConfigurationBuildRoot
    $mixedDebugBinary = Join-Path $mixedConfigurationBuildRoot 'src\installer\FastFilesSetup.exe'
    $debugInstallerBinary = Join-Path $debugPresetBuildRoot 'src\installer\FastFilesSetup.exe'
    Copy-Item -LiteralPath $debugInstallerBinary -Destination $mixedDebugBinary -Force
    $mixedConfigurationResult = & $pwsh -NoProfile -NonInteractive -File $candidateScript -Mode unsigned-validation `
        -BuildRoot $mixedConfigurationBuildRoot -OutputDirectory $mixedConfigurationOutput -SkipVerification -OutputFormat json 2>&1
    $mixedConfigurationExit = $LASTEXITCODE
    $mixedConfigurationText = $mixedConfigurationResult | Out-String
    Check ($mixedConfigurationExit -eq 1 -and $mixedConfigurationText -match 'RELEASE_BUILD_CONFIGURATION_MISMATCH' -and
        $mixedConfigurationText -match 'FastFilesSetup.exe=Debug') 'candidate rejects a mixed selected BuildRoot containing Debug and RelWithDebInfo executables'

    $productionRelWithResult = & $pwsh -NoProfile -NonInteractive -File $candidateScript -Mode production `
        -BuildRoot $BuildRoot -OutputDirectory $productionRelWithOutput -SkipVerification -OutputFormat json 2>&1
    $productionRelWithExit = $LASTEXITCODE
    $productionRelWithText = $productionRelWithResult | Out-String
    Check ($productionRelWithExit -eq 1 -and $productionRelWithText -match 'RELEASE_BUILD_CONFIGURATION_MISMATCH' -and
        $productionRelWithText -match 'requires config=Release' -and $productionRelWithText -match 'RelWithDebInfo') 'production candidate rejects unsigned RelWithDebInfo binaries'

    $missingRootOutput = & $pwsh -NoProfile -NonInteractive -File $candidateScript -Mode unsigned-validation `
        -BuildRoot $missingBuildRoot -OutputDirectory $missingBuildOutput -SkipVerification -OutputFormat json 2>&1
    $missingRootExit = $LASTEXITCODE
    $missingRootText = $missingRootOutput | Out-String
    Check ($missingRootExit -eq 1 -and $missingRootText -match 'PROVENANCE_MISMATCH:' -and
        $missingRootText -match 'MISSING_BINARY' -and $missingRootText -match 'release-candidate-build-root') 'candidate rejects a selected BuildRoot with missing required executables'

    New-Item -ItemType Directory -Path $unboundBuildRoot -Force | Out-Null
    foreach ($sourceBinary in @(Get-DefaultCertificationBinaries -RepoRoot $RepoRoot -BuildDirectory $BuildRoot)) {
        $relativeBinary = $sourceBinary.Substring($BuildRoot.TrimEnd('\').Length).TrimStart('\')
        $destinationBinary = Join-Path $unboundBuildRoot $relativeBinary
        New-Item -ItemType Directory -Path (Split-Path -Parent $destinationBinary) -Force | Out-Null
        Copy-Item -LiteralPath $sourceBinary -Destination $destinationBinary -Force
    }
    $foreignExecutable = Get-Process -Id $PID | Select-Object -ExpandProperty Path
    Copy-Item -LiteralPath $foreignExecutable -Destination (Join-Path $unboundBuildRoot 'src\fftest\fftest.exe') -Force
    $unboundRootOutput = & $pwsh -NoProfile -NonInteractive -File $candidateScript -Mode unsigned-validation `
        -BuildRoot $unboundBuildRoot -OutputDirectory $unboundBuildOutput -SkipVerification -OutputFormat json 2>&1
    $unboundRootExit = $LASTEXITCODE
    $unboundRootText = $unboundRootOutput | Out-String
    Check ($unboundRootExit -eq 1 -and $unboundRootText -match 'PROVENANCE_MISMATCH:' -and
        $unboundRootText -match 'MISSING_IDENTITY' -and $unboundRootText -match 'BINARY_TREE_STATE_UNAVAILABLE') 'candidate rejects a custom BuildRoot containing a same-host PE with unbound source metadata'

    Copy-CertificationBuildRoot -SourceRoot $BuildRoot -DestinationRoot $productionSkipBuildRoot
    foreach ($productionBinary in @(Get-DefaultCertificationBinaries -RepoRoot $RepoRoot -BuildDirectory $productionSkipBuildRoot)) {
        Replace-EmbeddedBuildConfigurationField -Path $productionBinary -Configuration 'RelWithDebInfo' `
            -FieldName 'config' -ReplacementConfiguration 'Release;;;;;;;'
    }
    $productionSkipConfigurations = @(Get-DefaultCertificationBinaries -RepoRoot $RepoRoot -BuildDirectory $productionSkipBuildRoot |
        ForEach-Object { (Get-BinaryProvenanceRecord -Path $_ -RepoRoot $RepoRoot).configuration })
    Check ($productionSkipConfigurations.Count -eq 5 -and
        @($productionSkipConfigurations | Where-Object { $_ -cne 'Release' }).Count -eq 0) 'production skip fixture reports Release in all five embedded version resources'
    $productionSkipResult = & $pwsh -NoProfile -NonInteractive -File $candidateScript -Mode production `
        -BuildRoot $productionSkipBuildRoot -OutputDirectory $productionSkipOutput -SkipVerification -OutputFormat json 2>&1
    $productionSkipExit = $LASTEXITCODE
    $productionSkipText = $productionSkipResult | Out-String
    $productionSkipJson = $productionSkipText | ConvertFrom-Json
    Check ($productionSkipExit -eq 1 -and $productionSkipJson.status -ceq 'FAIL' -and
        -not [bool]$productionSkipJson.productionReady -and $productionSkipJson.error -match 'PRODUCTION_VERIFICATION_CANNOT_BE_SKIPPED') 'production skip cannot emit a passing or production-ready candidate'
} catch {
    Check $false "release-candidate test completed without exception ($($_.Exception.Message))"
} finally {
    Remove-TempDirectory -Path $outputA
    Remove-TempDirectory -Path $outputB
    Remove-TempDirectory -Path $retainedEvidence
    Remove-TempDirectory -Path $missingBuildRoot
    Remove-TempDirectory -Path $missingBuildOutput
    Remove-TempDirectory -Path $unboundBuildRoot
    Remove-TempDirectory -Path $unboundBuildOutput
    Remove-TempDirectory -Path $debugBuildRoot
    Remove-TempDirectory -Path $debugBuildOutput
    Remove-TempDirectory -Path $missingConfigurationBuildRoot
    Remove-TempDirectory -Path $missingConfigurationOutput
    Remove-TempDirectory -Path $mixedConfigurationBuildRoot
    Remove-TempDirectory -Path $mixedConfigurationOutput
    Remove-TempDirectory -Path $productionSkipBuildRoot
    Remove-TempDirectory -Path $productionSkipOutput
    Remove-TempDirectory -Path $productionRelWithOutput
    Remove-TempDirectory -Path $directKitOutput
    Remove-TempDirectory -Path $postKitFailureOutput
}

Write-Host "Failures: $failures"
exit $(if ($failures -eq 0) { 0 } else { 1 })
