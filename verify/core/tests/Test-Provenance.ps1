$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
Import-Module (Join-Path $repoRoot 'verify\core\RepositoryState.psm1') -Force
Import-Module (Join-Path $repoRoot 'verify\core\Provenance.psm1') -Force
Import-Module (Join-Path $repoRoot 'verify\core\Schema.psm1') -Force

$failures = 0
function Check {
    param([bool] $Condition, [string] $Description)
    if ($Condition) { Write-Host "ok: $Description" } else { Write-Host "FAIL: $Description"; $script:failures++ }
}
function Invoke-FixtureGit {
    param([Parameter(Mandatory)] [string] $Root, [Parameter(ValueFromRemainingArguments)] [string[]] $GitArgs)
    $output = @(& git -C $Root @GitArgs 2>&1 | ForEach-Object { $_.ToString() })
    if ($LASTEXITCODE -ne 0) { throw "fixture git command failed: git $($GitArgs -join ' '): $($output -join ' ')" }
    return ,@($output)
}
function New-FakeBinaryRecord {
    param(
        [string] $Filename = 'FastFiles.exe',
        [string] $Identity = '',
        [string[]] $ExtraIssues = @(),
        [string] $Architecture = 'x64'
    )
    $issues = @()
    if ($Identity -eq '') { $issues += 'MISSING_IDENTITY' }
    elseif ($Identity -notmatch '^[0-9a-f]{40}$') { $issues += 'MALFORMED_IDENTITY' }
    $issues += $ExtraIssues
    if ($Architecture -ne 'x64') { $issues += 'ARCH_MISMATCH' }
    return [pscustomobject]@{
        filename                 = $Filename
        path                     = "C:\fake\$Filename"
        exists                   = $true
        sha256                   = ('ab' * 32)
        peArchitecture            = $Architecture
        productVersion           = '0.1.0'
        fileVersion              = '0.1.0.0'
        productName              = 'FastFiles'
        companyName               = 'FastFiles'
        originalFilename         = $Filename
        internalName            = $Filename
        buildIdentity           = if ($Identity -match '^[0-9a-f]{40}$') { $Identity } else { $Identity }
        buildIdentityFormatValid = $Identity -match '^[0-9a-f]{40}$'
        worktreeState            = 'clean'
        configuration            = 'Debug'
        architectureLabel        = $Architecture
        issues                  = @($issues | Sort-Object -Unique)
    }
}
function New-SourceStub {
    param(
        [string] $HeadSha,
        [string] $TreeSha = ('cd' * 20),
        [bool] $Clean = $true
    )
    return [pscustomobject]@{
        headSha        = $HeadSha
        treeSha        = $TreeSha
        treeSemantic   = 'HEAD^{tree}'
        sourceClean    = $Clean
        trackedDirty   = -not $Clean
        branch         = 'main'
        detached       = $false
        stateDigest    = ('ef' * 32)
        reasonCodes    = @()
        stagedPaths    = @()
        unstagedPaths  = @()
        untrackedPaths = @()
        mode           = 'exact-tree'
    }
}

$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-provenance-' + [guid]::NewGuid().ToString('N'))
$notARepo = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-provenance-norepo-' + [guid]::NewGuid().ToString('N'))
$shaA = 'a' * 40
$shaB = 'b' * 40

try {
    New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
    New-Item -ItemType Directory -Path $notARepo -Force | Out-Null
    Invoke-FixtureGit $fixtureRoot init | Out-Null
    Invoke-FixtureGit $fixtureRoot config user.name 'FastFiles Fixture' | Out-Null
    Invoke-FixtureGit $fixtureRoot config user.email 'fixture@example.invalid' | Out-Null
    Set-Content -LiteralPath (Join-Path $fixtureRoot '.gitignore') -Value @('build/', 'gen/') -Encoding utf8NoBOM
    Set-Content -LiteralPath (Join-Path $fixtureRoot 'tracked.txt') -Value 'a' -Encoding utf8NoBOM
    Invoke-FixtureGit $fixtureRoot add . | Out-Null
    Invoke-FixtureGit $fixtureRoot commit -m 'A' | Out-Null
    $realHeadA = (Invoke-FixtureGit $fixtureRoot rev-parse HEAD)[0].Trim().ToLowerInvariant()

    # Source provenance
    $sourceClean = Get-SourceProvenanceRecord -RepoRoot $fixtureRoot -Mode exact-tree
    Check ($sourceClean.headSha -match '^[0-9a-f]{40}$' -and $sourceClean.treeSha -match '^[0-9a-f]{40}$') 'source record resolves HEAD and HEAD^{tree}'
    Check ($sourceClean.treeSemantic -eq 'HEAD^{tree}') 'source tree semantic is HEAD^{tree}'
    Check ($sourceClean.sourceClean) 'fixture repository is source-clean after commit'

    # PASS: matching identity, clean tree, strict mode
    $matchA = @(
        (New-FakeBinaryRecord -Filename 'FastFiles.exe' -Identity $realHeadA)
        (New-FakeBinaryRecord -Filename 'FastFilesEngine.exe' -Identity $realHeadA)
    )
    $bindingOk = Test-BinarySourceBinding -BinaryRecords $matchA -SourceRecord $sourceClean -Mode strict
    Check ($bindingOk.valid) 'matching identity + clean tree passes strict binding'
    Check ($bindingOk.boundToHead -and $bindingOk.setIdentity -eq $realHeadA) 'binding reports head-bound set identity'
    Check (-not $bindingOk.boundToTree -and $bindingOk.treeBindingStatus -eq 'NOT_EMBEDDED') 'binding does not claim the binary is bound to the source tree when no tree SHA is embedded'
    $emptyStrict = Test-BinarySourceBinding -BinaryRecords @() -SourceRecord $sourceClean -Mode strict
    Check ((-not $emptyStrict.valid) -and ($emptyStrict.issueCodes -contains 'EMPTY_BINARY_SET')) 'strict binding rejects an empty required executable set'
    $emptyDevelopment = Test-BinarySourceBinding -BinaryRecords @() -SourceRecord $sourceClean -Mode development
    Check ($emptyDevelopment.valid) 'development/source-only binding preserves an explicitly empty binary set'
    $gateEmptyStrict = Invoke-ProvenanceGate -RepoRoot $fixtureRoot -Binaries @() -Mode exact-tree -Stage 'unit-empty-strict' -PassThru
    Check ((-not $gateEmptyStrict.ok) -and ($gateEmptyStrict.binding.issueCodes -contains 'EMPTY_BINARY_SET')) 'strict provenance gate rejects an empty executable list'
    $gateEmptyPath = Invoke-ProvenanceGate -RepoRoot $fixtureRoot -Binaries @('') -Mode exact-tree -Stage 'unit-empty-path' -PassThru
    Check ((-not $gateEmptyPath.ok) -and ($gateEmptyPath.binding.issueCodes -contains 'EMPTY_BINARY_PATH')) 'strict provenance gate accepts an empty string argument and rejects its empty path'
    $gateWhitespacePath = Invoke-ProvenanceGate -RepoRoot $fixtureRoot -Binaries @('   ') -Mode exact-tree -Stage 'unit-whitespace-path' -PassThru
    Check ((-not $gateWhitespacePath.ok) -and ($gateWhitespacePath.binding.issueCodes -contains 'EMPTY_BINARY_PATH')) 'strict provenance gate rejects a whitespace-only required path'
    $gateEmptyDevelopment = Invoke-ProvenanceGate -RepoRoot $fixtureRoot -Binaries @() -Mode development -Stage 'unit-empty-development' -PassThru
    Check ($gateEmptyDevelopment.ok) 'development/source-only provenance gate accepts no binaries'

    # A real PE from the running PowerShell host makes the test exercise the
    # production PE parser independently of generated FastFiles build output.
    $pwshHost = Get-Process -Id $PID | Select-Object -ExpandProperty Path
    $hostPe = Get-BinaryProvenanceRecord -Path $pwshHost -RepoRoot $fixtureRoot
    $expectedHostArchitecture = switch ([Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString()) {
        'X64' { 'x64' }
        'X86' { 'x86' }
        'Arm64' { 'arm64' }
        default { 'unknown' }
    }
    Check ($hostPe.peArchitecture -eq $expectedHostArchitecture -and $hostPe.issues -notcontains 'ARCH_READ_ERROR') 'canonical PE probe reads a real executable when RepoRoot is supplied'
    $gateHostPe = Invoke-ProvenanceGate -RepoRoot $fixtureRoot -Binaries @($pwshHost) -Mode exact-tree -Stage 'unit-real-pe' -PassThru
    Check ($gateHostPe.evidence.binaries[0].peArchitecture -eq $expectedHostArchitecture -and
        ($gateHostPe.binding.issueCodes -notcontains 'ARCH_READ_ERROR')) 'strict gate requests the canonical PE probe and records its architecture'

    # Replace the selected pathname while VERSIONINFO is being read. The
    # collector must already have snapshotted and hash-bound the exact bytes,
    # so the later PE read remains on the original image.
    New-Item -ItemType Directory -Path (Join-Path $fixtureRoot 'build') -Force | Out-Null
    $racePath = Join-Path $fixtureRoot 'build\provenance-swap.exe'
    Copy-Item -LiteralPath $pwshHost -Destination $racePath -Force
    $raceOriginalHash = (Get-FileHash -LiteralPath $racePath -Algorithm SHA256).Hash.ToLowerInvariant()
    $provenanceModule = Get-Module -Name Provenance
    $raceRecord = & $provenanceModule {
        param($OriginalPath)
        function Get-BinaryVersionInfo {
            param([Parameter(Mandatory)] [string] $Path)
            [IO.File]::WriteAllBytes($OriginalPath, [byte[]]::new(256))
            return [Diagnostics.FileVersionInfo]::GetVersionInfo($Path)
        }
        Get-BinaryProvenanceRecord -Path $OriginalPath
    } $racePath
    Check ($raceRecord.sha256 -ceq $raceOriginalHash -and $raceRecord.peArchitecture -ceq $expectedHostArchitecture -and
        $raceRecord.issues -notcontains 'ARCH_READ_ERROR') 'hash, VERSIONINFO, and PE architecture stay bound to one snapshot during path replacement'
    Check ((Get-FileHash -LiteralPath $racePath -Algorithm SHA256).Hash.ToLowerInvariant() -cne $raceOriginalHash) 'swap fixture replaced the selected pathname between metadata collection steps'

    # Missing required paths, failed hashes, and unreadable version/PE metadata
    # are independently terminal in strict mode.
    $gateMissing = Invoke-ProvenanceGate -RepoRoot $fixtureRoot -Binaries @((Join-Path $fixtureRoot 'build\missing.exe')) -Mode exact-tree -Stage 'unit-missing-binary' -PassThru
    Check ((-not $gateMissing.ok) -and ($gateMissing.binding.issueCodes -contains 'MISSING_BINARY')) 'strict gate rejects a required executable that is absent'
    $gateMixedMissing = Invoke-ProvenanceGate -RepoRoot $fixtureRoot -Binaries @($pwshHost, (Join-Path $fixtureRoot 'build\missing-mixed.exe')) -Mode exact-tree -Stage 'unit-mixed-missing-binary' -PassThru
    Check ((-not $gateMixedMissing.ok) -and ($gateMixedMissing.binding.issueCodes -contains 'MISSING_BINARY')) 'strict gate rejects a missing binary even when another supplied binary is present'

    # An exclusive handle makes the real collector's Get-FileHash call fail
    # deterministically, proving the production gate consumes collector errors.
    $lockedPath = Join-Path $fixtureRoot 'build\locked.exe'
    New-Item -ItemType Directory -Path (Split-Path -Parent $lockedPath) -Force | Out-Null
    Copy-Item -LiteralPath $pwshHost -Destination $lockedPath -Force
    $lockedHandle = [IO.File]::Open($lockedPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
    try {
        $gateLocked = Invoke-ProvenanceGate -RepoRoot $fixtureRoot -Binaries @($lockedPath) -Mode exact-tree -Stage 'unit-locked-binary' -PassThru
    } finally {
        $lockedHandle.Dispose()
    }
    Check ((-not $gateLocked.ok) -and ($gateLocked.evidence.binaries[0].issues -contains 'HASH_READ_ERROR') -and
        ($gateLocked.binding.issueCodes -contains 'HASH_READ_ERROR')) 'strict gate rejects a real collector hash read error from an exclusively locked executable'

    # Shadow the module-private version-resource helper only for this gate call.
    # This keeps the production collector unchanged while forcing its real
    # VERSION_RESOURCE_READ_ERROR handling path.
    $gateVersionError = & $provenanceModule {
        param([string] $Root, [string] $Path)
        function Get-BinaryVersionInfo {
            param([string] $Path)
            throw [IO.IOException]::new('deterministic test-only version-resource failure')
        }
        Invoke-ProvenanceGate -RepoRoot $Root -Binaries @($Path) -Mode exact-tree -Stage 'unit-version-resource-error' -PassThru
    } $fixtureRoot $pwshHost
    Check ((-not $gateVersionError.ok) -and ($gateVersionError.evidence.binaries[0].issues -contains 'VERSION_RESOURCE_READ_ERROR') -and
        ($gateVersionError.binding.issueCodes -contains 'VERSION_RESOURCE_READ_ERROR')) 'strict gate preserves and rejects the collector version-resource read error'

    $missingFlag = @(New-FakeBinaryRecord -Filename 'FastFiles.exe' -Identity $realHeadA)
    $missingFlag[0].exists = $false
    $bindingMissingFlag = Test-BinarySourceBinding -BinaryRecords $missingFlag -SourceRecord $sourceClean -Mode strict
    Check ((-not $bindingMissingFlag.valid) -and ($bindingMissingFlag.issueCodes -contains 'MISSING_BINARY')) 'strict binding rejects a record that claims its executable does not exist'
    $unhashable = @(New-FakeBinaryRecord -Filename 'FastFiles.exe' -Identity $realHeadA)
    $unhashable[0].sha256 = 'bad-hash'
    $bindingUnhashable = Test-BinarySourceBinding -BinaryRecords $unhashable -SourceRecord $sourceClean -Mode strict
    Check ((-not $bindingUnhashable.valid) -and ($bindingUnhashable.issueCodes -contains 'BINARY_HASH_UNAVAILABLE')) 'strict binding rejects an absent or malformed SHA-256 record'
    $unreadable = @(New-FakeBinaryRecord -Filename 'FastFiles.exe' -Identity $realHeadA -ExtraIssues @('VERSION_RESOURCE_READ_ERROR'))
    $bindingUnreadable = Test-BinarySourceBinding -BinaryRecords $unreadable -SourceRecord $sourceClean -Mode strict
    Check ((-not $bindingUnreadable.valid) -and ($bindingUnreadable.issueCodes -contains 'VERSION_RESOURCE_READ_ERROR')) 'strict binding rejects a version-resource read failure'
    $malformedPePath = Join-Path $fixtureRoot 'build\malformed.exe'
    New-Item -ItemType Directory -Path (Split-Path -Parent $malformedPePath) -Force | Out-Null
    [IO.File]::WriteAllBytes($malformedPePath, [Text.Encoding]::ASCII.GetBytes('not a PE image'))
    $malformedPe = Get-BinaryProvenanceRecord -Path $malformedPePath -RepoRoot $fixtureRoot
    Check ($malformedPe.issues -contains 'ARCH_READ_ERROR') 'malformed PE input produces an architecture read failure'
    $bindingMalformedPe = Test-BinarySourceBinding -BinaryRecords @($malformedPe) -SourceRecord $sourceClean -Mode strict
    Check ((-not $bindingMalformedPe.valid) -and ($bindingMalformedPe.issueCodes -contains 'ARCH_READ_ERROR')) 'strict binding rejects malformed PE records'

    $dirtyBuild = @(New-FakeBinaryRecord -Filename 'FastFiles.exe' -Identity $realHeadA)
    $dirtyBuild[0].worktreeState = 'dirty'
    $bindingDirtyBuild = Test-BinarySourceBinding -BinaryRecords $dirtyBuild -SourceRecord $sourceClean -Mode strict
    Check ((-not $bindingDirtyBuild.valid) -and ($bindingDirtyBuild.issueCodes -contains 'DIRTY_BINARY_BUILD')) 'strict binding rejects an embedded tree=dirty build claim'
    $unboundBuild = @(New-FakeBinaryRecord -Filename 'FastFiles.exe' -Identity $realHeadA)
    $unboundBuild[0].worktreeState = 'unbound'
    $bindingUnboundBuild = Test-BinarySourceBinding -BinaryRecords $unboundBuild -SourceRecord $sourceClean -Mode strict
    Check ((-not $bindingUnboundBuild.valid) -and ($bindingUnboundBuild.issueCodes -contains 'UNBOUND_BINARY_BUILD')) 'strict binding rejects an embedded tree=unbound build claim'

    # NEGATIVE: stale binary (built at A, HEAD is B)
    Set-Content -LiteralPath (Join-Path $fixtureRoot 'tracked.txt') -Value 'b' -Encoding utf8NoBOM
    Invoke-FixtureGit $fixtureRoot add . | Out-Null
    Invoke-FixtureGit $fixtureRoot commit -m 'B' | Out-Null
    $realHeadB = (Invoke-FixtureGit $fixtureRoot rev-parse HEAD)[0].Trim().ToLowerInvariant()
    $sourceAfterB = Get-SourceProvenanceRecord -RepoRoot $fixtureRoot -Mode exact-tree
    $stale = @(New-FakeBinaryRecord -Filename 'FastFiles.exe' -Identity $realHeadA)
    $bindingStale = Test-BinarySourceBinding -BinaryRecords $stale -SourceRecord $sourceAfterB -Mode strict
    Check ((-not $bindingStale.valid) -and ($bindingStale.issueCodes -contains 'STALE_BINARY')) 'stale binary from commit A is rejected when HEAD is B'
    $staleMsg = Format-ProvenanceMismatch -ReasonCode 'STALE_BINARY' -Stage 'unit-stale' -Expected $realHeadB -Actual $realHeadA -Binaries @('FastFiles.exe') -SourceRecord $sourceAfterB
    Check ($staleMsg.StartsWith('PROVENANCE_MISMATCH:STALE_BINARY:')) 'stale message uses PROVENANCE_MISMATCH prefix'
    Check ($staleMsg -match "expected=$realHeadB" -and $staleMsg -match "actual=$realHeadA") 'stale message carries expected and observed SHA'

    # NEGATIVE: mixed binary set (one from A, one from B)
    $mixed = @(
        (New-FakeBinaryRecord -Filename 'FastFiles.exe' -Identity $realHeadA),
        (New-FakeBinaryRecord -Filename 'FastFilesEngine.exe' -Identity $realHeadB)
    )
    $bindingMixed = Test-BinarySourceBinding -BinaryRecords $mixed -SourceRecord $sourceAfterB -Mode strict
    Check ((-not $bindingMixed.valid) -and ($bindingMixed.issueCodes -contains 'MIXED_BINARY_SET')) 'mixed binary set from two SHAs is rejected'

    # NEGATIVE: missing identity
    $missing = @(New-FakeBinaryRecord -Filename 'FastFiles.exe' -Identity '')
    $bindingMissing = Test-BinarySourceBinding -BinaryRecords $missing -SourceRecord $sourceAfterB -Mode strict
    Check ((-not $bindingMissing.valid) -and ($bindingMissing.issueCodes -contains 'MISSING_IDENTITY')) 'missing PrivateBuild identity is rejected'

    # NEGATIVE: malformed identity
    $malformed = @(New-FakeBinaryRecord -Filename 'FastFiles.exe' -Identity ('a' * 39))
    $bindingMalformed = Test-BinarySourceBinding -BinaryRecords $malformed -SourceRecord $sourceAfterB -Mode strict
    Check ((-not $bindingMalformed.valid) -and ($bindingMalformed.issueCodes -contains 'MALFORMED_IDENTITY')) 'malformed identity is rejected'

    # NEGATIVE: dirty tree in strict mode
    Set-Content -LiteralPath (Join-Path $fixtureRoot 'tracked.txt') -Value 'b-dirty' -Encoding utf8NoBOM
    $sourceDirty = Get-SourceProvenanceRecord -RepoRoot $fixtureRoot -Mode exact-tree
    $dirtyBin = @(New-FakeBinaryRecord -Filename 'FastFiles.exe' -Identity $realHeadB)
    $bindingDirty = Test-BinarySourceBinding -BinaryRecords $dirtyBin -SourceRecord $sourceDirty -Mode strict
    Check ((-not $bindingDirty.valid) -and ($bindingDirty.issueCodes -contains 'DIRTY_TREE_MISMATCH')) 'dirty tree fails strict exact-tree mode'
    $bindingDirtyDev = Test-BinarySourceBinding -BinaryRecords $dirtyBin -SourceRecord $sourceDirty -Mode development
    Check ($bindingDirtyDev.valid) 'dirty tree is allowed in development mode when identity matches'
    Invoke-FixtureGit $fixtureRoot checkout -- tracked.txt | Out-Null

    # NEGATIVE: non-repository source resolution fails closed
    $gateNoRepo = Invoke-ProvenanceGate -RepoRoot $notARepo -Binaries @() -Mode exact-tree -Stage 'unit-norepo' -PassThru
    Check ((-not $gateNoRepo.ok) -and ($gateNoRepo.message -match 'PROVENANCE_MISMATCH:HEAD_UNAVAILABLE:')) 'non-repository repo root fails the gate closed'

    # ARCH mismatch
    $wrongArch = @(New-FakeBinaryRecord -Filename 'FastFiles.exe' -Identity $realHeadB -Architecture 'x86')
    $bindingArch = Test-BinarySourceBinding -BinaryRecords $wrongArch -SourceRecord $sourceAfterB -Mode strict
    Check ((-not $bindingArch.valid) -and ($bindingArch.issueCodes -contains 'ARCH_MISMATCH')) 'non-x64 architecture fails strict binding'

    # Evidence shape
    $evidence = New-BinaryProvenanceEvidence -SourceRecord $sourceAfterB -Binding $bindingStale -BinaryRecords $stale -Stage 'unit-evidence' -Mode exact-tree
    Check ($evidence.schemaVersion -eq 1 -and $evidence.kind -eq 'fastfiles-binary-provenance') 'evidence envelope has schema and kind'
    Check ($evidence.source.treeSemantic -eq 'HEAD^{tree}') 'evidence records tree semantic'
    Check ($evidence.binaries.Count -eq 1 -and $evidence.binaries[0].buildIdentity -eq $realHeadA) 'evidence records observed binary identity'
    Check ($evidence.checkResult.status -eq 'FAIL' -and ($evidence.checkResult.issueCodes -contains 'STALE_BINARY')) 'evidence checkResult records failure codes'
    Check ($evidence.source.stateDigest -match '^[0-9a-f]{64}$') 'evidence includes source state digest'
    $binarySchema = Get-Content -LiteralPath (Join-Path $repoRoot 'verify\schemas\binary-provenance.schema.json') -Raw | ConvertFrom-Json
    $currentSchemaResult = Test-JsonSchema -Data $evidence -Schema $binarySchema
    Check ($currentSchemaResult.Valid) 'current binary provenance evidence validates against schema v1'
    $historicalEvidence = $evidence | ConvertTo-Json -Depth 12 | ConvertFrom-Json
    $historicalEvidence.PSObject.Properties.Remove('boundToTree')
    $historicalEvidence.PSObject.Properties.Remove('treeBindingStatus')
    $historicalEvidence.binaries[0].PSObject.Properties.Remove('exists')
    $historicalSchemaResult = Test-JsonSchema -Data $historicalEvidence -Schema $binarySchema
    Check ($historicalSchemaResult.Valid) 'historical schema v1 evidence without later optional fields remains valid'

    # Default binary list covers the five product executables
    $defaults = Get-DefaultCertificationBinaries -RepoRoot $fixtureRoot -BuildDirectory (Join-Path $fixtureRoot 'build\debug')
    Check ($defaults.Count -eq 5) 'default certification binary set has five product executables'
    Check (@($defaults | Where-Object { $_ -match 'FastFiles(\.exe|Engine\.exe|IndexSvc\.exe|Setup\.exe)$|fftest\.exe$' }).Count -eq 5) 'default set names ui, engine, service, installer, probe'

    # Env override cannot replace the repository HEAD in strict mode.
    $savedOverride = $env:FASTFILES_BUILD_ID
    try {
        $env:FASTFILES_BUILD_ID = $realHeadA
        $bindingOverride = Test-BinarySourceBinding -BinaryRecords $matchA -SourceRecord $sourceAfterB -Mode strict
        Check ((-not $bindingOverride.valid) -and ($bindingOverride.issueCodes -contains 'BUILD_ID_OVERRIDE_MISMATCH')) 'strict mode rejects a build-id override that differs from repository HEAD'
        $env:FASTFILES_BUILD_ID = $realHeadB
        $bindingOverride2 = Test-BinarySourceBinding -BinaryRecords $stale -SourceRecord $sourceAfterB -Mode strict
        Check ((-not $bindingOverride2.valid) -and ($bindingOverride2.issueCodes -contains 'STALE_BINARY')) 'override mismatch still reports STALE_BINARY'
    } finally {
        if ($null -ne $savedOverride) { $env:FASTFILES_BUILD_ID = $savedOverride } else { Remove-Item Env:FASTFILES_BUILD_ID -ErrorAction SilentlyContinue }
    }
}
finally {
    Remove-Item -LiteralPath $fixtureRoot -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $notARepo -Recurse -Force -ErrorAction SilentlyContinue
    if ($env:FASTFILES_BUILD_ID) { Remove-Item Env:FASTFILES_BUILD_ID -ErrorAction SilentlyContinue }
}

if ($failures -gt 0) {
    Write-Host "FAILED: $failures check(s)"
    exit 1
}
Write-Host 'All provenance binding checks passed.'
exit 0
