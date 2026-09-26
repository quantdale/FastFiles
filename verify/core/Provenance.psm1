<#
    Source-to-binary provenance binding for FastFiles certification.

    Single authority for comparing embedded executable identity (VERSIONINFO
    PrivateBuild / SpecialBuild) against the repository source state before any
    functional certification work runs. Composes with RepositoryState.psm1,
    which owns source/tree identity; this module owns binary-to-source binding.

    A mismatch is terminal in strict mode and is never downgraded to a warning.
#>

Import-Module (Join-Path $PSScriptRoot 'RepositoryState.psm1') -Force -DisableNameChecking

if (-not ('FastFilesProvenanceFile' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

public static class FastFilesProvenanceFile
{
    private const uint GenericRead = 0x80000000;
    private const uint ShareRead = 0x00000001;
    private const uint OpenExisting = 3;
    private const uint FileAttributeNormal = 0x00000080;
    private const uint FileFlagOpenReparsePoint = 0x00200000;
    private const uint FileFlagSequentialScan = 0x08000000;
    private const uint AttributeDirectory = 0x00000010;
    private const uint AttributeReparsePoint = 0x00000400;

    [StructLayout(LayoutKind.Sequential)]
    private struct FileAttributeTagInfo
    {
        public uint FileAttributes;
        public uint ReparseTag;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true, EntryPoint = "CreateFileW")]
    private static extern SafeFileHandle CreateFile(string name, uint access, uint share, IntPtr security,
        uint creation, uint flags, IntPtr template);

    [DllImport("kernel32.dll", SetLastError = true, EntryPoint = "GetFileInformationByHandleEx")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetFileInformationByHandleEx(SafeFileHandle handle, int infoClass,
        out FileAttributeTagInfo info, uint size);

    public static FileStream OpenRegularReadOnly(string path)
    {
        SafeFileHandle handle = CreateFile(path, GenericRead, ShareRead, IntPtr.Zero, OpenExisting,
            FileAttributeNormal | FileFlagOpenReparsePoint | FileFlagSequentialScan, IntPtr.Zero);
        if (handle.IsInvalid)
        {
            int error = Marshal.GetLastWin32Error();
            handle.Dispose();
            throw new Win32Exception(error);
        }

        try
        {
            FileAttributeTagInfo info;
            if (!GetFileInformationByHandleEx(handle, 9, out info, (uint)Marshal.SizeOf(typeof(FileAttributeTagInfo))))
            {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            if ((info.FileAttributes & AttributeReparsePoint) != 0)
            {
                throw new IOException("Binary provenance rejects reparse-point executables.");
            }
            if ((info.FileAttributes & AttributeDirectory) != 0)
            {
                throw new IOException("Binary provenance requires a regular executable file.");
            }
            return new FileStream(handle, FileAccess.Read, 65536, false);
        }
        catch
        {
            handle.Dispose();
            throw;
        }
    }
}
'@ -Language CSharp -ErrorAction Stop
}

function Get-BinaryVersionInfo {
    param([Parameter(Mandatory)] [string] $Path)
    return [Diagnostics.FileVersionInfo]::GetVersionInfo($Path)
}

function Get-BinaryProvenanceRecord {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $Path,
        [string] $RepoRoot = ''
    )

    $issues = [System.Collections.Generic.List[string]]::new()
    $fullPath = [IO.Path]::GetFullPath($Path)
    $record = [ordered]@{
        path                    = $fullPath
        filename                = [IO.Path]::GetFileName($fullPath)
        exists                  = $false
        sha256                  = $null
        peArchitecture          = $null
        productVersion          = $null
        fileVersion             = $null
        productName             = $null
        companyName             = $null
        originalFilename        = $null
        internalName            = $null
        buildIdentity           = $null
        buildIdentityFormatValid = $false
        worktreeState           = $null
        configuration           = $null
        architectureLabel       = $null
        issues                  = @()
    }

    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        $issues.Add('MISSING_BINARY')
        $record.issues = @($issues)
        return [pscustomobject]$record
    }

    $record.exists = $true
    $snapshotPath = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-provenance-' + [guid]::NewGuid().ToString('N') + '.bin')
    $sourceStream = $null
    $snapshotStream = $null
    $snapshotReady = $false
    try {
        # Open the selected file once without write/delete sharing and do not
        # follow a final-component reparse point. All metadata below is read
        # from this immutable byte snapshot, so hash/VERSIONINFO/PE cannot race
        # separate opens of a changing path.
        $sourceStream = [FastFilesProvenanceFile]::OpenRegularReadOnly($fullPath)
        $snapshotStream = [IO.File]::Open($snapshotPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        $sourceStream.CopyTo($snapshotStream)
        $snapshotStream.Flush()
        $snapshotReady = $true
    } catch {
        # A locked, disappearing, or rejected reparse-point input cannot be
        # snapshotted and therefore cannot produce a stable content hash.
        $issues.Add('HASH_READ_ERROR')
        $issues.Add('BINARY_RECORD_READ_ERROR')
    } finally {
        if ($snapshotStream) { $snapshotStream.Dispose() }
        if ($sourceStream) { $sourceStream.Dispose() }
    }
    if (-not $snapshotReady) {
        if (Test-Path -LiteralPath $snapshotPath -PathType Leaf) {
            Remove-Item -LiteralPath $snapshotPath -Force -ErrorAction SilentlyContinue
        }
        $record.issues = @($issues)
        return [pscustomobject]$record
    }

    $snapshotGuard = $null
    try {
        # Lock and validate the captured copy itself while metadata APIs reopen
        # it by path. Its contents and directory entry cannot be replaced under
        # the read-only shared handle.
        $snapshotGuard = [FastFilesProvenanceFile]::OpenRegularReadOnly($snapshotPath)
        try {
            $record.sha256 = (Get-FileHash -LiteralPath $snapshotPath -Algorithm SHA256).Hash.ToLowerInvariant()
        } catch {
            $issues.Add('HASH_READ_ERROR')
        }

        try {
            $info = Get-BinaryVersionInfo -Path $snapshotPath
            $record.productVersion = if ([string]::IsNullOrWhiteSpace($info.ProductVersion)) { $null } else { $info.ProductVersion }
            $record.fileVersion = if ([string]::IsNullOrWhiteSpace($info.FileVersion)) { $null } else { $info.FileVersion }
            $record.productName = if ([string]::IsNullOrWhiteSpace($info.ProductName)) { $null } else { $info.ProductName }
            $record.companyName = if ([string]::IsNullOrWhiteSpace($info.CompanyName)) { $null } else { $info.CompanyName }
            $record.originalFilename = if ([string]::IsNullOrWhiteSpace($info.OriginalFilename)) { $null } else { $info.OriginalFilename }
            $record.internalName = if ([string]::IsNullOrWhiteSpace($info.InternalName)) { $null } else { $info.InternalName }
            $identity = if ([string]::IsNullOrWhiteSpace($info.PrivateBuild)) { $null } else { $info.PrivateBuild.Trim().ToLowerInvariant() }
            $record.buildIdentity = $identity
            $special = if ([string]::IsNullOrWhiteSpace($info.SpecialBuild)) { '' } else { $info.SpecialBuild }
            if ($special -match 'tree=([^;]+)') { $record.worktreeState = $Matches[1] }
            if ($special -match 'config=([^;]+)') { $record.configuration = $Matches[1] }
            if ($special -match 'arch=([^;]+)') { $record.architectureLabel = $Matches[1] }
            if ($null -eq $identity) {
                $issues.Add('MISSING_IDENTITY')
            } elseif ($identity -notmatch '^[0-9a-f]{40}$') {
                $issues.Add('MALFORMED_IDENTITY')
            } else {
                $record.buildIdentityFormatValid = $true
            }
        } catch {
            $issues.Add('VERSION_RESOURCE_READ_ERROR')
        }

        try {
            # Use the canonical release PE parser for every provenance record;
            # it receives the same stable snapshot as the hash and VERSIONINFO reads.
            $releaseCommon = Join-Path (Split-Path -Parent $PSScriptRoot) 'release\ReleaseCommon.psm1'
            if (-not (Test-Path -LiteralPath $releaseCommon -PathType Leaf)) {
                throw "Release PE parser is missing: $releaseCommon"
            }
            Import-Module $releaseCommon -Force -DisableNameChecking
            $architecture = Get-PeArchitecture -Path $snapshotPath
            if ($architecture -and $architecture.architecture) {
                $record.peArchitecture = $architecture.architecture
                if ($architecture.architecture -ne 'x64') { $issues.Add('ARCH_MISMATCH') }
            } else {
                $issues.Add('ARCH_READ_ERROR')
            }
        } catch {
            $issues.Add('ARCH_READ_ERROR')
        }
    } finally {
        if ($snapshotGuard) { $snapshotGuard.Dispose() }
        if (Test-Path -LiteralPath $snapshotPath -PathType Leaf) {
            Remove-Item -LiteralPath $snapshotPath -Force -ErrorAction SilentlyContinue
        }
    }

    $record.issues = @($issues)
    return [pscustomobject]$record
}

function Get-SourceProvenanceRecord {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RepoRoot,
        [ValidateSet('development', 'exact-tree')]
        [string] $Mode = 'development'
    )

    $state = Get-RepositorySourceState -RepoRoot $RepoRoot -QualificationMode $Mode
    return [pscustomobject]@{
        headSha        = $state.headSha
        treeSha        = $state.treeSha
        treeSemantic   = 'HEAD^{tree}'
        sourceClean    = $state.sourceClean
        trackedDirty   = $state.trackedDirty
        branch         = $state.branch
        detached       = $state.detached
        stateDigest    = $state.stateDigest
        reasonCodes    = $state.reasonCodes
        stagedPaths    = $state.stagedPaths
        unstagedPaths  = $state.unstagedPaths
        untrackedPaths = $state.untrackedPaths
        mode           = $Mode
    }
}

function Test-BinarySourceBinding {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [AllowEmptyCollection()] [object[]] $BinaryRecords,
        [Parameter(Mandatory)] $SourceRecord,
        [ValidateSet('strict', 'development')]
        [string] $Mode = 'strict'
    )

    $issueCodes = [System.Collections.Generic.List[string]]::new()
    $records = @($BinaryRecords)
    if ($Mode -eq 'strict' -and $records.Count -eq 0) {
        [void]$issueCodes.Add('EMPTY_BINARY_SET')
    }

    foreach ($record in $records) {
        if ($null -eq $record) {
            if ($Mode -eq 'strict') { [void]$issueCodes.Add('BINARY_RECORD_INVALID') }
            continue
        }

        $issuesProperty = $record.PSObject.Properties['issues']
        $recordIssues = if ($issuesProperty) { @($issuesProperty.Value) } else { @() }
        if ($Mode -eq 'strict') {
            if (-not $issuesProperty) { [void]$issueCodes.Add('BINARY_RECORD_INVALID') }
            foreach ($issue in $recordIssues) {
                if (-not [string]::IsNullOrWhiteSpace([string]$issue)) { [void]$issueCodes.Add([string]$issue) }
            }

            $existsProperty = $record.PSObject.Properties['exists']
            if (-not $existsProperty -or $existsProperty.Value -isnot [bool]) {
                [void]$issueCodes.Add('BINARY_RECORD_INVALID')
            } elseif (-not [bool]$existsProperty.Value) {
                [void]$issueCodes.Add('MISSING_BINARY')
            }

            foreach ($requiredStringName in @('filename', 'path')) {
                $requiredString = $record.PSObject.Properties[$requiredStringName]
                if (-not $requiredString -or $requiredString.Value -isnot [string] -or [string]::IsNullOrWhiteSpace([string]$requiredString.Value)) {
                    [void]$issueCodes.Add('BINARY_RECORD_INVALID')
                }
            }
        }
        foreach ($issue in $recordIssues) {
            if ($issue -in @('MISSING_IDENTITY', 'MALFORMED_IDENTITY')) {
                [void]$issueCodes.Add($issue)
            }
        }

        if ($Mode -eq 'strict') {
            $hashProperty = $record.PSObject.Properties['sha256']
            if (-not $hashProperty -or [string]$hashProperty.Value -notmatch '^[0-9a-fA-F]{64}$') {
                [void]$issueCodes.Add('BINARY_HASH_UNAVAILABLE')
            }

            $identityProperty = $record.PSObject.Properties['buildIdentity']
            $identity = if ($identityProperty) { [string]$identityProperty.Value } else { '' }
            if ($identity -notmatch '^[0-9a-fA-F]{40}$') {
                if ($recordIssues -notcontains 'MALFORMED_IDENTITY') {
                    [void]$issueCodes.Add('MISSING_IDENTITY')
                }
            } else {
                $identityFormatProperty = $record.PSObject.Properties['buildIdentityFormatValid']
                if (-not $identityFormatProperty -or $identityFormatProperty.Value -isnot [bool] -or -not $identityFormatProperty.Value) {
                    [void]$issueCodes.Add('BINARY_RECORD_INVALID')
                }
            }

            $treeProperty = $record.PSObject.Properties['worktreeState']
            $treeState = if ($treeProperty) { ([string]$treeProperty.Value).Trim().ToLowerInvariant() } else { '' }
            if ($treeState -eq 'dirty') {
                [void]$issueCodes.Add('DIRTY_BINARY_BUILD')
            } elseif ($treeState -eq 'unbound') {
                [void]$issueCodes.Add('UNBOUND_BINARY_BUILD')
            } elseif ($treeState -ne 'clean') {
                [void]$issueCodes.Add('BINARY_TREE_STATE_UNAVAILABLE')
            }

            $architectureProperty = $record.PSObject.Properties['peArchitecture']
            $architecture = if ($architectureProperty) { [string]$architectureProperty.Value } else { '' }
            if ($architecture -eq 'x64') {
                # The canonical PE probe has independently verified the required
                # executable architecture.
            } elseif ([string]::IsNullOrWhiteSpace($architecture) -or $recordIssues -contains 'ARCH_READ_ERROR') {
                [void]$issueCodes.Add('ARCH_READ_ERROR')
            } else {
                [void]$issueCodes.Add('ARCH_MISMATCH')
            }
        }
    }

    $identities = @($records | Where-Object { $_.buildIdentityFormatValid } | ForEach-Object { $_.buildIdentity } | Sort-Object -Unique)
    if ($records.Count -gt 0 -and @($records | Where-Object { $_.buildIdentityFormatValid }).Count -gt 0) {
        if ($identities.Count -gt 1) {
            [void]$issueCodes.Add('MIXED_BINARY_SET')
        } elseif ($identities.Count -eq 1) {
            $observed = $identities[0]
            $override = if ($env:FASTFILES_BUILD_ID -and $env:FASTFILES_BUILD_ID.Trim() -match '^[0-9a-fA-F]{40}$') {
                $env:FASTFILES_BUILD_ID.Trim().ToLowerInvariant()
            } else { $null }
            $expected = $SourceRecord.headSha
            if ($Mode -eq 'strict' -and $override -and $expected -and $override -ne $expected) {
                [void]$issueCodes.Add('BUILD_ID_OVERRIDE_MISMATCH')
            }
            if (-not $expected) {
                if ($Mode -eq 'strict') { [void]$issueCodes.Add('HEAD_UNAVAILABLE') }
            } elseif ($observed -ne $expected) {
                [void]$issueCodes.Add('STALE_BINARY')
            }
        }
    }

    if ($Mode -eq 'strict') {
        if (-not $SourceRecord.headSha -or $SourceRecord.headSha -notmatch '^[0-9a-f]{40}$') {
            [void]$issueCodes.Add('HEAD_UNAVAILABLE')
        }
        $sourceCleanProperty = $SourceRecord.PSObject.Properties['sourceClean']
        if (-not $sourceCleanProperty -or $sourceCleanProperty.Value -isnot [bool] -or -not $sourceCleanProperty.Value) {
            [void]$issueCodes.Add('DIRTY_TREE_MISMATCH')
        }
        # Tree binding is not yet embedded as its own identity channel; record
        # that only commit-level binding was provable when the tree is clean.
        if ($sourceCleanProperty -and $sourceCleanProperty.Value -is [bool] -and $sourceCleanProperty.Value -and
            ([string]$SourceRecord.treeSha -notmatch '^[0-9a-f]{40}$')) {
            [void]$issueCodes.Add('TREE_BINDING_UNAVAILABLE')
        }
    }

    $uniqueIssues = @($issueCodes | Sort-Object -Unique)
    return [pscustomobject]@{
        mode            = $Mode
        issueCodes      = $uniqueIssues
        setIdentity     = if ($identities.Count -eq 1) { $identities[0] } else { $null }
        setConsistent   = $identities.Count -le 1
        boundToHead     = ($uniqueIssues -notcontains 'STALE_BINARY' -and $uniqueIssues -notcontains 'MIXED_BINARY_SET' -and $identities.Count -eq 1 -and $SourceRecord.headSha -and $identities[0] -eq $SourceRecord.headSha)
        # The build embeds commit identity and a build-time cleanliness claim,
        # but no tree SHA. Keep this false even when the current source tree is
        # known; a source tree hash alone does not bind these binary bytes to it.
        boundToTree     = $false
        treeBindingStatus = 'NOT_EMBEDDED'
        treeSemantic    = $SourceRecord.treeSemantic
        valid           = $uniqueIssues.Count -eq 0
    }
}

function Format-ProvenanceMismatch {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $ReasonCode,
        [Parameter(Mandatory)] [string] $Stage,
        [string] $Expected = '',
        [string] $Actual = '',
        [AllowEmptyCollection()] [string[]] $Binaries = @(),
        [string] $Check = 'head-binding',
        [string] $Detail = '',
        $SourceRecord = $null
    )

    $sourceCommit = if ($SourceRecord -and $SourceRecord.headSha) { $SourceRecord.headSha } else { 'unavailable' }
    $sourceTree = if ($SourceRecord -and $SourceRecord.treeSha) { $SourceRecord.treeSha } else { 'unavailable' }
    $sourceState = if ($SourceRecord) { if ($SourceRecord.sourceClean) { 'clean' } else { 'dirty' } } else { 'unbound' }
    $binaryList = (@($Binaries) | Where-Object { $_ }) -join ','
    return ('PROVENANCE_MISMATCH:{0}: expected={1}; actual={2}; binary={3}; check={4}; sourceCommit={5}; sourceTree={6}; sourceState={7}; stage={8}; detail={9}' -f `
            $ReasonCode, $Expected, $Actual, $binaryList, $Check, $sourceCommit, $sourceTree, $sourceState, $Stage, $Detail)
}

function New-BinaryProvenanceEvidence {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] $SourceRecord,
        [Parameter(Mandatory)] $Binding,
        [AllowEmptyCollection()] [object[]] $BinaryRecords = @(),
        [Parameter(Mandatory)] [string] $Stage,
        [Parameter(Mandatory)] [string] $Mode
    )

    return [pscustomobject]@{
        schemaVersion = 1
        kind          = 'fastfiles-binary-provenance'
        mode          = $Mode
        stage         = $Stage
        source        = [pscustomobject]@{
            headSha      = $SourceRecord.headSha
            treeSha      = $SourceRecord.treeSha
            treeSemantic = $SourceRecord.treeSemantic
            sourceClean  = $SourceRecord.sourceClean
            stateDigest  = $SourceRecord.stateDigest
            reasonCodes  = $SourceRecord.reasonCodes
        }
        binaries      = @($BinaryRecords | ForEach-Object {
            [pscustomobject]@{
                    filename                 = $_.filename
                    path                     = $_.path
                    exists                   = $_.exists
                    sha256                   = $_.sha256
                    buildIdentity            = $_.buildIdentity
                    buildIdentityFormatValid = $_.buildIdentityFormatValid
                    peArchitecture            = $_.peArchitecture
                    productVersion           = $_.productVersion
                    fileVersion              = $_.fileVersion
                    worktreeState            = $_.worktreeState
                    configuration            = $_.configuration
                    issues                   = $_.issues
                }
            })
        setIdentity    = $Binding.setIdentity
        setConsistent  = $Binding.setConsistent
        boundToHead    = $Binding.boundToHead
        boundToTree    = $Binding.boundToTree
        treeBindingStatus = $Binding.treeBindingStatus
        checkResult    = [pscustomobject]@{
            status     = if ($Binding.valid) { 'PASS' } else { 'FAIL' }
            issueCodes = $Binding.issueCodes
            reason     = if ($Binding.valid) { $null } else { ($Binding.issueCodes -join ',') }
        }
        checkedAtUtc   = [DateTime]::UtcNow.ToString('o')
    }
}

function Invoke-ProvenanceGate {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RepoRoot,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [AllowEmptyString()] [string[]] $Binaries,
        [ValidateSet('exact-tree', 'development')]
        [string] $Mode = 'exact-tree',
        [Parameter(Mandatory)] [string] $Stage,
        [switch] $PassThru
    )

    $resolveMode = if ($Mode -eq 'exact-tree') { 'exact-tree' } else { 'development' }
    $bindingMode = if ($Mode -eq 'exact-tree') { 'strict' } else { 'development' }

    $sourceRecord = $null
    try {
        $sourceRecord = Get-SourceProvenanceRecord -RepoRoot $RepoRoot -Mode $resolveMode
    } catch {
        $message = Format-ProvenanceMismatch -ReasonCode 'HEAD_UNAVAILABLE' -Stage $Stage `
            -Expected 'resolvable repository HEAD' -Actual $_.Exception.Message -Check 'head-binding' -Detail 'source state could not be resolved'
        if ($PassThru) {
            return [pscustomobject]@{ ok = $false; message = $message; evidence = $null; exitCode = 1 }
        }
        throw $message
    }

    $binaryRecords = @()
    foreach ($path in @($Binaries)) {
        if ([string]::IsNullOrWhiteSpace($path)) {
            $binaryRecords += [pscustomobject]@{
                path = [string]$path; filename = ''; exists = $false; sha256 = $null; peArchitecture = $null
                buildIdentity = $null; buildIdentityFormatValid = $false; worktreeState = $null
                issues = @('MISSING_BINARY', 'EMPTY_BINARY_PATH')
            }
            continue
        }
        try {
            $binaryRecords += Get-BinaryProvenanceRecord -Path $path -RepoRoot $RepoRoot
        } catch {
            $binaryRecords += [pscustomobject]@{
                path = [string]$path; filename = [IO.Path]::GetFileName([string]$path); exists = $false; sha256 = $null; peArchitecture = $null
                buildIdentity = $null; buildIdentityFormatValid = $false; worktreeState = $null
                issues = @('BINARY_RECORD_READ_ERROR')
            }
        }
    }

    $binding = Test-BinarySourceBinding -BinaryRecords $binaryRecords -SourceRecord $sourceRecord -Mode $bindingMode
    $evidence = New-BinaryProvenanceEvidence -SourceRecord $sourceRecord -Binding $binding -BinaryRecords $binaryRecords -Stage $Stage -Mode $Mode

    if (-not $binding.valid) {
        $reason = $binding.issueCodes | Select-Object -First 1
        $stale = @($binaryRecords | Where-Object { $_.buildIdentityFormatValid -and $sourceRecord.headSha -and $_.buildIdentity -ne $sourceRecord.headSha })
        $message = Format-ProvenanceMismatch -ReasonCode $reason -Stage $Stage `
            -Expected $sourceRecord.headSha `
            -Actual $(if ($binding.setIdentity) { $binding.setIdentity } else { 'unavailable' }) `
            -Binaries $(if ($stale) { $stale | ForEach-Object { $_.filename } } else { $binaryRecords | ForEach-Object { $_.filename } }) `
            -Check $(switch ($reason) {
                    'MIXED_BINARY_SET' { 'sha256-set' }
                    'MALFORMED_IDENTITY' { 'identity-format' }
                    'MISSING_IDENTITY' { 'identity-format' }
                    'ARCH_MISMATCH' { 'pe-arch' }
                    'DIRTY_TREE_MISMATCH' { 'cleanliness' }
                    default { 'head-binding' }
                }) `
            -Detail ($binding.issueCodes -join ',') `
            -SourceRecord $sourceRecord
        if ($PassThru) {
            return [pscustomobject]@{ ok = $false; message = $message; evidence = $evidence; exitCode = 1; binding = $binding; source = $sourceRecord }
        }
        throw $message
    }

    if ($PassThru) {
        return [pscustomobject]@{ ok = $true; message = $null; evidence = $evidence; exitCode = 0; binding = $binding; source = $sourceRecord }
    }
    return $evidence
}

function Test-ReleaseBuildRootProvenance {
    [CmdletBinding()]
    param([Parameter(Mandatory)] $Provenance)

    $issueCodes = [System.Collections.Generic.List[string]]::new()
    $expectedArtifactConfiguration = switch ([string]$Provenance.expectedConfiguration) {
        'RelWithDebInfo' { 'release-test' }
        'Release' { 'Release' }
        default { $null }
    }
    if (-not $expectedArtifactConfiguration -or
        [string]$Provenance.artifactManifestConfiguration -cne $expectedArtifactConfiguration) {
        [void]$issueCodes.Add('CANDIDATE_CONFIGURATION_PAIR_INVALID')
    }
    if ([string]$Provenance.status -cne 'PASS' -or
        [string]::IsNullOrWhiteSpace([string]$Provenance.buildRoot) -or
        -not [bool]$Provenance.setConsistent -or -not [bool]$Provenance.boundToHead -or
        [bool]$Provenance.boundToTree -or [string]$Provenance.treeBindingStatus -cne 'NOT_EMBEDDED') {
        [void]$issueCodes.Add('CANDIDATE_BINDING_FLAGS_INVALID')
    }

    $source = $Provenance.source
    if (-not $source -or -not [bool]$source.sourceClean -or
        [string]$source.headSha -notmatch '^[0-9a-f]{40}$' -or
        [string]$source.treeSha -notmatch '^[0-9a-f]{40}$' -or
        [string]$source.stateDigest -notmatch '^[0-9a-f]{64}$' -or
        [string]$Provenance.setIdentity -cne [string]$source.headSha) {
        [void]$issueCodes.Add('CANDIDATE_SOURCE_BINDING_INVALID')
    }

    $requiredNames = @('FastFiles.exe', 'FastFilesEngine.exe', 'FastFilesIndexSvc.exe', 'FastFilesSetup.exe', 'fftest.exe')
    $binaries = @($Provenance.binaries)
    if ($binaries.Count -ne $requiredNames.Count) {
        [void]$issueCodes.Add('CANDIDATE_BINARY_INVENTORY_INVALID')
    }
    foreach ($name in $requiredNames) {
        if (@($binaries | Where-Object { [string]$_.filename -ceq $name }).Count -ne 1) {
            [void]$issueCodes.Add('CANDIDATE_BINARY_INVENTORY_INVALID')
        }
    }

    foreach ($binary in $binaries) {
        $filename = [string]$binary.filename
        $binaryIssues = @($binary.issues | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) })
        if ($filename -notin $requiredNames -or
            [string]$binary.path -cne "BuildRoot/$filename" -or
            -not [bool]$binary.exists -or
            [string]$binary.sha256 -notmatch '^[0-9a-f]{64}$' -or
            [string]$binary.buildIdentity -cne [string]$Provenance.setIdentity -or
            -not [bool]$binary.buildIdentityFormatValid -or
            [string]$binary.peArchitecture -cne 'x64' -or
            [string]$binary.worktreeState -cne 'clean') {
            [void]$issueCodes.Add('CANDIDATE_BINARY_BINDING_INVALID')
        }
        if ($expectedArtifactConfiguration -and
            [string]$binary.configuration -cne [string]$Provenance.expectedConfiguration) {
            [void]$issueCodes.Add('CANDIDATE_BINARY_CONFIGURATION_INVALID')
        }
        if ($binaryIssues.Count -gt 0) {
            [void]$issueCodes.Add('CANDIDATE_BINARY_ISSUES_PRESENT')
        }
    }

    $uniqueIssues = @($issueCodes | Sort-Object -Unique)
    [pscustomobject]@{ valid = ($uniqueIssues.Count -eq 0); issueCodes = $uniqueIssues }
}

function Get-DefaultCertificationBinaries {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RepoRoot,
        [string] $BuildDirectory = ''
    )

    if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
        $BuildDirectory = Join-Path $RepoRoot 'build\debug'
    }
    return @(
        (Join-Path $BuildDirectory 'src\ui\FastFiles.exe'),
        (Join-Path $BuildDirectory 'src\engine\FastFilesEngine.exe'),
        (Join-Path $BuildDirectory 'src\indexsvc\FastFilesIndexSvc.exe'),
        (Join-Path $BuildDirectory 'src\installer\FastFilesSetup.exe'),
        (Join-Path $BuildDirectory 'src\fftest\fftest.exe')
    )
}

Export-ModuleMember -Function @(
    'Get-BinaryProvenanceRecord',
    'Get-SourceProvenanceRecord',
    'Test-BinarySourceBinding',
    'Format-ProvenanceMismatch',
    'New-BinaryProvenanceEvidence',
    'Invoke-ProvenanceGate',
    'Test-ReleaseBuildRootProvenance',
    'Get-DefaultCertificationBinaries'
)
