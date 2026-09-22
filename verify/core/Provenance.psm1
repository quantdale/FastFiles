<#
    Source-to-binary provenance binding for FastFiles certification.

    Single authority for comparing embedded executable identity (VERSIONINFO
    PrivateBuild / SpecialBuild) against the repository source state before any
    functional certification work runs. Composes with RepositoryState.psm1,
    which owns source/tree identity; this module owns binary-to-source binding.

    A mismatch is terminal in strict mode and is never downgraded to a warning.
#>

Import-Module (Join-Path $PSScriptRoot 'RepositoryState.psm1') -Force -DisableNameChecking

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
    try {
        $record.sha256 = (Get-FileHash -LiteralPath $fullPath -Algorithm SHA256).Hash.ToLowerInvariant()
    } catch {
        $issues.Add('HASH_READ_ERROR')
    }

    try {
        $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($fullPath)
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

    if ($RepoRoot) {
        try {
            $architecture = & {
                # Prefer the release-tooling PE probe when available so the
                # architecture field matches package manifests.
                $releaseCommon = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'release\ReleaseCommon.psm1'
                if (Test-Path -LiteralPath $releaseCommon) {
                    Import-Module $releaseCommon -Force -DisableNameChecking
                    Get-PeArchitecture -Path $fullPath
                } else {
                    $bytes = [IO.File]::ReadAllBytes($fullPath)
                    if ($bytes.Length -gt 0x3c) {
                        $peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
                        if (($peOffset + 6) -lt $bytes.Length -and $bytes[$peOffset] -eq 0x4d -and $bytes[$peOffset + 1] -eq 0x5a) {
                            $machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
                            $label = switch ($machine) {
                                0x8664 { 'x64' }
                                0x014c { 'x86' }
                                0xaa64 { 'arm64' }
                                default { "machine-0x$($machine.ToString('x4'))" }
                            }
                            [pscustomobject]@{ architecture = $label; machine = $machine }
                        } else { $null }
                    } else { $null }
                }
            }
            if ($architecture -and $architecture.architecture) {
                $record.peArchitecture = $architecture.architecture
                if ($architecture.architecture -ne 'x64') { $issues.Add('ARCH_MISMATCH') }
            } else {
                $issues.Add('ARCH_READ_ERROR')
            }
        } catch {
            $issues.Add('ARCH_READ_ERROR')
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

    foreach ($record in $records) {
        foreach ($issue in @($record.issues)) {
            if ($issue -in @('MISSING_IDENTITY', 'MALFORMED_IDENTITY')) {
                [void]$issueCodes.Add($issue)
            } elseif ($issue -eq 'ARCH_MISMATCH' -and $Mode -eq 'strict') {
                [void]$issueCodes.Add($issue)
            }
        }
    }

    $identities = @($records | Where-Object { $_.buildIdentityFormatValid } | ForEach-Object { $_.buildIdentity } | Sort-Object -Unique)
    if ($records.Count -gt 0 -and @($records | Where-Object { $_.buildIdentityFormatValid }).Count -gt 0) {
        if ($identities.Count -gt 1) {
            [void]$issueCodes.Add('MIXED_BINARY_SET')
        } elseif ($identities.Count -eq 1) {
            $observed = $identities[0]
            $expected = if ($env:FASTFILES_BUILD_ID -and $env:FASTFILES_BUILD_ID.Trim() -match '^[0-9a-fA-F]{40}$') {
                $env:FASTFILES_BUILD_ID.Trim().ToLowerInvariant()
            } else {
                $SourceRecord.headSha
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
        if (-not $SourceRecord.sourceClean) {
            [void]$issueCodes.Add('DIRTY_TREE_MISMATCH')
        }
        # Tree binding is not yet embedded as its own identity channel; record
        # that only commit-level binding was provable when the tree is clean.
        if ($SourceRecord.sourceClean -and -not $SourceRecord.treeSha) {
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
        boundToTree     = [bool]$SourceRecord.treeSha
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
        [Parameter(Mandatory)] [AllowEmptyCollection()] [string[]] $Binaries,
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
        if ([string]::IsNullOrWhiteSpace($path)) { continue }
        $binaryRecords += Get-BinaryProvenanceRecord -Path $path
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
    'Get-DefaultCertificationBinaries'
)
