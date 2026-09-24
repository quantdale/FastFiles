$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
Import-Module (Join-Path $repoRoot 'verify\core\RepositoryState.psm1') -Force
Import-Module (Join-Path $repoRoot 'verify\core\Provenance.psm1') -Force

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
    $gateOk = Invoke-ProvenanceGate -RepoRoot $fixtureRoot -Binaries @() -Mode exact-tree -Stage 'unit-match' -PassThru
    # empty binary list with no identities is valid under binding rules only if no format issues
    Check ($gateOk.ok) 'gate with no binaries and clean source passes (nothing to mismatch)'

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
