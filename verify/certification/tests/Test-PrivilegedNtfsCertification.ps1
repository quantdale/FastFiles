[CmdletBinding()]
param(
    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
)

$ErrorActionPreference = 'Stop'

Write-Host "Running Test-PrivilegedNtfsCertification.ps1..." -ForegroundColor Cyan

$runner = Join-Path $RepoRoot 'verify\certification\Invoke-PrivilegedNtfsCertification.ps1'
$schemaPath = Join-Path $RepoRoot 'verify\certification\schemas\certification-result.schema.json'

if (-not (Test-Path -LiteralPath $runner)) { throw "Runner not found at $runner" }
if (-not (Test-Path -LiteralPath $schemaPath)) { throw "Schema not found at $schemaPath" }

function Assert-ResultSchema {
    param([string] $JsonPath)
    $pyCmd = "import json, jsonschema; s = json.load(open(r'$schemaPath')); d = json.load(open(r'$JsonPath')); jsonschema.validate(instance=d, schema=s); print('OK')"
    $val = & (Get-Command python).Source -c $pyCmd 2>&1
    if ($val -notmatch 'OK') { throw "Schema validation failed: $val" }
}

function Get-Check {
    param($Json, [string] $Id)
    return @($Json.checks | Where-Object { $_.checkId -eq $Id }) | Select-Object -First 1
}

# Test 1: Normal execution produces schema-conforming artifact with cleanup + capabilities
Write-Host "  Test 1: Execution produces schema-compliant JSON..."
$tempJson = [IO.Path]::GetTempFileName()
try {
    $res = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $runner `
        -VolumeRoot "C:" `
        -OutputPath $tempJson `
        -RepoRoot $RepoRoot

    if ($LASTEXITCODE -ne 0) { throw "Runner failed with exit code $LASTEXITCODE" }
    if (-not (Test-Path -LiteralPath $tempJson)) { throw "Output JSON was not created" }

    Assert-ResultSchema -JsonPath $tempJson

    $json = Get-Content -LiteralPath $tempJson -Raw | ConvertFrom-Json
    if ($json.lane -ne 'privileged-ntfs') { throw "Expected lane privileged-ntfs, got $($json.lane)" }
    if ($json.details.readOnly -ne $true) { throw "Read-only invariant violated in details" }
    if ($null -eq $json.cleanup) { throw "Result JSON MUST include a cleanup object" }
    if ($json.cleanup.status -ne 'PASS' -and $json.cleanup.status -ne 'FAIL') { throw "cleanup.status must be PASS or FAIL, got $($json.cleanup.status)" }
    if ($null -eq $json.capabilities) { throw "Result JSON MUST include a capabilities array" }

    Write-Host "    PASS (Status: $($json.status), Disposition: $($json.disposition))" -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $tempJson) { Remove-Item -LiteralPath $tempJson -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath "$tempJson.sha256") { Remove-Item -LiteralPath "$tempJson.sha256" -Force -ErrorAction SilentlyContinue }
}

# Test 2: Unprivileged execution must not claim PASS
Write-Host "  Test 2: Elevation invariant check..."
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    $tempJson2 = [IO.Path]::GetTempFileName()
    try {
        $res = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $runner `
            -VolumeRoot "C:" `
            -OutputPath $tempJson2 `
            -RepoRoot $RepoRoot

        $json2 = Get-Content -LiteralPath $tempJson2 -Raw | ConvertFrom-Json
        if ($json2.status -eq 'PASS') {
            throw "Unprivileged process must not claim PASS on privileged NTFS lane"
        }
        if ($json2.status -ne 'BLOCKED_NOT_ELEVATED') {
            throw "Expected status BLOCKED_NOT_ELEVATED, got $($json2.status)"
        }
        if ($null -eq $json2.cleanup) { throw "Result JSON MUST include a cleanup object" }
        Write-Host "    PASS (Correctly classified BLOCKED_NOT_ELEVATED)" -ForegroundColor Green
    } finally {
        if (Test-Path -LiteralPath $tempJson2) { Remove-Item -LiteralPath $tempJson2 -Force -ErrorAction SilentlyContinue }
        if (Test-Path -LiteralPath "$tempJson2.sha256") { Remove-Item -LiteralPath "$tempJson2.sha256" -Force -ErrorAction SilentlyContinue }
    }
} else {
    Write-Host "    PASS (Host is elevated; verified in Test 1)" -ForegroundColor Green
}

# Test 3: Raw-open alone MUST NOT qualify for MFT/USN certification (fail-closed BLOCKED).
Write-Host "  Test 3: Raw-open alone cannot PASS without fixture comparison..."
$tempJson3 = [IO.Path]::GetTempFileName()
try {
    & $runner -VolumeRoot 'C:' -OutputPath $tempJson3 -RepoRoot $RepoRoot `
        -PrivilegeProbe { $true } `
        -FilesystemProbe { 'NTFS' } `
        -VolumeOpenProbe { param($p) $true } | Out-Null

    Assert-ResultSchema -JsonPath $tempJson3
    $json3 = Get-Content -LiteralPath $tempJson3 -Raw | ConvertFrom-Json
    if ($json3.status -notmatch 'BLOCKED') {
        throw "Raw-open without fixture comparison must yield BLOCKED_*, got $($json3.status)"
    }
    if ($json3.status -eq 'PASS') { throw "Raw-open alone must never PASS" }
    if ($json3.disposition -ne 'BLOCKED_EXTERNAL') { throw "Expected disposition BLOCKED_EXTERNAL, got $($json3.disposition)" }
    $mft3 = Get-Check -Json $json3 -Id 'mft-ground-truth'
    $usn3 = Get-Check -Json $json3 -Id 'usn-ground-truth'
    if ($mft3.status -notmatch 'BLOCKED') { throw "mft-ground-truth must be BLOCKED_* without comparison, got $($mft3.status)" }
    if ($usn3.status -notmatch 'BLOCKED') { throw "usn-ground-truth must be BLOCKED_* without comparison, got $($usn3.status)" }
    if (-not ($json3.capabilities -contains 'raw-volume-read')) { throw "raw-volume-read capability must be present when the handle opens" }
    if ($json3.capabilities -contains 'mft-ground-truth') { throw "mft-ground-truth must NOT be claimed without comparison" }
    if ($json3.capabilities -contains 'usn-ground-truth') { throw "usn-ground-truth must NOT be claimed without comparison" }
    if ($json3.cleanup.status -ne 'PASS') { throw "Expected clean cleanup, got $($json3.cleanup.status)" }
    Write-Host "    PASS (Raw-open alone yields $($json3.status); no ground-truth capability claimed)" -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $tempJson3) { Remove-Item -LiteralPath $tempJson3 -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath "$tempJson3.sha256") { Remove-Item -LiteralPath "$tempJson3.sha256" -Force -ErrorAction SilentlyContinue }
}

# Test 4: Fixture field mismatch MUST yield FAIL with per-field evidence.
Write-Host "  Test 4: Fixture mismatch yields FAIL..."
$tempJson4 = [IO.Path]::GetTempFileName()
try {
    $truthSeam = {
        param($d)
        @(
            [pscustomobject]@{ Name = 'fixture-01.bin'; Size = [long]256; ParentPath = 'C:\ff-ntfs-test'; FileId = $null },
            [pscustomobject]@{ Name = 'fixture-02.bin'; Size = [long]1024; ParentPath = 'C:\ff-ntfs-test'; FileId = $null }
        )
    }
    $mftBadSeam = {
        param($d)
        @(
            [pscustomobject]@{ Name = 'fixture-01.bin'; Size = [long]999; ParentPath = 'C:\ff-ntfs-test'; FileId = $null },
            [pscustomobject]@{ Name = 'fixture-02.bin'; Size = [long]1024; ParentPath = 'C:\ff-ntfs-test'; FileId = $null }
        )
    }
    $usnGoodSeam = {
        param($d)
        @(
            [pscustomobject]@{ Name = 'fixture-01.bin'; Size = [long]256; ParentPath = 'C:\ff-ntfs-test'; FileId = $null },
            [pscustomobject]@{ Name = 'fixture-02.bin'; Size = [long]1024; ParentPath = 'C:\ff-ntfs-test'; FileId = $null }
        )
    }
    & $runner -VolumeRoot 'C:' -OutputPath $tempJson4 -RepoRoot $RepoRoot `
        -PrivilegeProbe { $true } `
        -FilesystemProbe { 'NTFS' } `
        -VolumeOpenProbe { param($p) $true } `
        -FixtureTruthProvider $truthSeam -MftReader $mftBadSeam -UsnReader $usnGoodSeam | Out-Null

    Assert-ResultSchema -JsonPath $tempJson4
    $json4 = Get-Content -LiteralPath $tempJson4 -Raw | ConvertFrom-Json
    if ($json4.status -ne 'FAIL') { throw "Fixture mismatch must yield FAIL, got $($json4.status)" }
    if ($json4.disposition -ne 'FAIL') { throw "Expected disposition FAIL, got $($json4.disposition)" }
    $mft4 = Get-Check -Json $json4 -Id 'mft-ground-truth'
    if ($mft4.status -ne 'FAIL') { throw "mft-ground-truth must be FAIL on mismatch, got $($mft4.status)" }
    $sizeRows = @($mft4.details.fieldResults | Where-Object { $_.file -eq 'fixture-01.bin' -and $_.field -eq 'Size' })
    if ($sizeRows.Count -eq 0) { throw "Per-field comparison must record the Size row for fixture-01.bin" }
    if ($sizeRows[0].match -ne $false) { throw "Size row for fixture-01.bin must record match=false" }
    $usn4 = Get-Check -Json $json4 -Id 'usn-ground-truth'
    if ($usn4.status -ne 'PASS') { throw "Matching USN leg must stay PASS, got $($usn4.status)" }
    if ($json4.capabilities -contains 'mft-ground-truth') { throw "mft-ground-truth must NOT be claimed on mismatch" }
    if (-not ($json4.capabilities -contains 'usn-ground-truth')) { throw "usn-ground-truth must be claimed for the matching USN leg" }
    if ($json4.cleanup.status -ne 'PASS') { throw "Expected clean cleanup, got $($json4.cleanup.status)" }
    if (Test-Path -LiteralPath $json4.cleanup.fixtureDir) { throw "Fixture dir must be removed: $($json4.cleanup.fixtureDir)" }
    Write-Host "    PASS (Mismatch yields FAIL with per-field evidence)" -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $tempJson4) { Remove-Item -LiteralPath $tempJson4 -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath "$tempJson4.sha256") { Remove-Item -LiteralPath "$tempJson4.sha256" -Force -ErrorAction SilentlyContinue }
}

# Shared production-observer seam: reports genuine OBSERVED semantics.
$observerObservedSeam = {
    param([string[]] $ObserverArgs)
    [pscustomobject]@{
        status = 'OBSERVED'; parsedRecords = 2; parsedName = 'fixture-01.bin'
        journalQueried = $true; journalId = '0x12345'; journalRead = $true
        nextUsn = '0x5000'; mutationOperations = 0; requestedAccess = @('GENERIC_READ')
        volumeOpened = $true; filesystem = 'NTFS'; reason = ''
    }
}

# Test 5: Matching fixtures via seams yield PASS deterministically (repeat twice).
Write-Host "  Test 5: Matching fixtures yield deterministic PASS..."
$truthMatchSeam = {
    param($d)
    @(
        [pscustomobject]@{ Name = 'fixture-01.bin'; Size = [long]256; ParentPath = 'C:\ff-ntfs-test'; FileId = '0xDEADBEEF' },
        [pscustomobject]@{ Name = 'fixture-02.bin'; Size = [long]1024; ParentPath = 'C:\ff-ntfs-test'; FileId = '0xDEADBEEF' }
    )
}
$readerMatchSeam = {
    param($d)
    @(
        [pscustomobject]@{ Name = 'fixture-01.bin'; Size = [long]256; ParentPath = 'C:\ff-ntfs-test'; FileId = '0xdeadbeef' },
        [pscustomobject]@{ Name = 'fixture-02.bin'; Size = [long]1024; ParentPath = 'C:\ff-ntfs-test'; FileId = '0xDEADBEEF' }
    )
}
$seenStatuses = @()
for ($run = 1; $run -le 2; $run++) {
    $tempJson5 = [IO.Path]::GetTempFileName()
    try {
        & $runner -VolumeRoot 'C:' -OutputPath $tempJson5 -RepoRoot $RepoRoot `
            -PrivilegeProbe { $true } `
            -FilesystemProbe { 'NTFS' } `
            -VolumeOpenProbe { param($p) $true } `
            -NtfsObserverCommand $observerObservedSeam `
            -FixtureTruthProvider $truthMatchSeam -MftReader $readerMatchSeam -UsnReader $readerMatchSeam | Out-Null

        Assert-ResultSchema -JsonPath $tempJson5
        $json5 = Get-Content -LiteralPath $tempJson5 -Raw | ConvertFrom-Json
        if ($json5.status -ne 'PASS') { throw "Run ${run}: matching fixtures must yield PASS, got $($json5.status)" }
        if ($json5.disposition -ne 'PASS') { throw "Run ${run}: expected disposition PASS, got $($json5.disposition)" }
        foreach ($cap in @('raw-volume-read', 'mft-ground-truth', 'usn-ground-truth', 'mft-observer-semantics', 'usn-observer-semantics')) {
            if (-not ($json5.capabilities -contains $cap)) { throw "Run ${run}: capability missing: $cap" }
        }
        if ($json5.cleanup.status -ne 'PASS') { throw "Run ${run}: expected clean cleanup" }
        if (Test-Path -LiteralPath $json5.cleanup.fixtureDir) { throw "Run ${run}: fixture dir must be removed" }
        $seenStatuses += $json5.status
    } finally {
        if (Test-Path -LiteralPath $tempJson5) { Remove-Item -LiteralPath $tempJson5 -Force -ErrorAction SilentlyContinue }
        if (Test-Path -LiteralPath "$tempJson5.sha256") { Remove-Item -LiteralPath "$tempJson5.sha256" -Force -ErrorAction SilentlyContinue }
    }
}
if ($seenStatuses[0] -ne $seenStatuses[1]) { throw "Seams are not deterministic: $($seenStatuses -join ',')" }
Write-Host "    PASS (Deterministic PASS with all ground-truth capabilities, run twice)" -ForegroundColor Green

# Test 6: Cleanup failure forces FAIL (never hidden).
Write-Host "  Test 6: Cleanup failure forces FAIL..."
$tempJson6 = [IO.Path]::GetTempFileName()
$leakBox = @{}
$leftoverDir = $null
try {
    $faithfulReaderSeam = {
        param($d)
        if (-not $leakBox.ContainsKey('Lock')) {
            $target = Join-Path $d 'fixture-01.bin'
            $leakBox['Lock'] = [IO.File]::Open($target, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
        }
        @(Get-ChildItem -LiteralPath $d -Recurse -File | ForEach-Object {
                [pscustomobject]@{ Name = $_.Name; Size = [long]$_.Length; ParentPath = $_.DirectoryName; FileId = $null }
            })
    }
    & $runner -VolumeRoot 'C:' -OutputPath $tempJson6 -RepoRoot $RepoRoot `
        -PrivilegeProbe { $true } `
        -FilesystemProbe { 'NTFS' } `
        -VolumeOpenProbe { param($p) $true } `
        -MftReader $faithfulReaderSeam -UsnReader $faithfulReaderSeam | Out-Null

    $json6 = Get-Content -LiteralPath $tempJson6 -Raw | ConvertFrom-Json
    $leftoverDir = $json6.cleanup.fixtureDir
    if ($json6.cleanup.status -ne 'FAIL') { throw "Locked fixture must report cleanup FAIL, got $($json6.cleanup.status)" }
    if ($json6.status -ne 'FAIL') { throw "Cleanup failure must force lane FAIL, got $($json6.status)" }
    if ($json6.disposition -ne 'FAIL') { throw "Expected disposition FAIL, got $($json6.disposition)" }
    Write-Host "    PASS (Cleanup failure correctly forces FAIL)" -ForegroundColor Green
} finally {
    if ($leakBox.ContainsKey('Lock') -and $null -ne $leakBox['Lock']) {
        $leakBox['Lock'].Close()
        $leakBox['Lock'] = $null
    }
    if ($leftoverDir -and (Test-Path -LiteralPath $leftoverDir)) {
        Remove-Item -LiteralPath $leftoverDir -Force -Recurse -ErrorAction SilentlyContinue
    }
    if (Test-Path -LiteralPath $tempJson6) { Remove-Item -LiteralPath $tempJson6 -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath "$tempJson6.sha256") { Remove-Item -LiteralPath "$tempJson6.sha256" -Force -ErrorAction SilentlyContinue }
}

# Test 7: Observer semantics PASS alone never yields ground-truth PASS.
Write-Host "  Test 7: Semantics without fixture comparison cannot PASS ground truth..."
$tempJson7 = [IO.Path]::GetTempFileName()
try {
    $observerSemOnlySeam = {
        param([string[]] $ObserverArgs)
        if ($ObserverArgs[1] -eq 'semantics') { return (& $observerObservedSeam @ObserverArgs) }
        return [pscustomobject]@{
            status = 'BLOCKED'; parsedRecords = 0; parsedName = ''
            journalQueried = $false; journalId = ''; journalRead = $false
            nextUsn = ''; mutationOperations = 0; requestedAccess = @('GENERIC_READ')
            volumeOpened = $true; filesystem = 'NTFS'
            reason = 'seam provides semantics only; fixture fetch refused'
        }
    }
    & $runner -VolumeRoot 'C:' -OutputPath $tempJson7 -RepoRoot $RepoRoot `
        -PrivilegeProbe { $true } `
        -FilesystemProbe { 'NTFS' } `
        -VolumeOpenProbe { param($p) $true } `
        -NtfsObserverCommand $observerSemOnlySeam | Out-Null

    Assert-ResultSchema -JsonPath $tempJson7
    $json7 = Get-Content -LiteralPath $tempJson7 -Raw | ConvertFrom-Json
    if ($json7.status -notmatch 'BLOCKED') { throw "Semantics alone must yield BLOCKED_*, got $($json7.status)" }
    $mftSem7 = Get-Check -Json $json7 -Id 'mft-observer-semantics'
    $usnSem7 = Get-Check -Json $json7 -Id 'usn-observer-semantics'
    if ($mftSem7.status -ne 'PASS') { throw "mft-observer-semantics must PASS, got $($mftSem7.status)" }
    if ($usnSem7.status -ne 'PASS') { throw "usn-observer-semantics must PASS, got $($usnSem7.status)" }
    $mft7 = Get-Check -Json $json7 -Id 'mft-ground-truth'
    if ($mft7.status -notmatch 'BLOCKED') { throw "mft-ground-truth must stay BLOCKED without readers, got $($mft7.status)" }
    if (-not ($json7.capabilities -contains 'mft-observer-semantics')) { throw 'mft-observer-semantics capability missing' }
    if ($json7.capabilities -contains 'mft-ground-truth') { throw 'mft-ground-truth must NOT be claimed without comparison' }
    Write-Host '    PASS (Semantics PASS; ground truth BLOCKED without comparison)' -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $tempJson7) { Remove-Item -LiteralPath $tempJson7 -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath "$tempJson7.sha256") { Remove-Item -LiteralPath "$tempJson7.sha256" -Force -ErrorAction SilentlyContinue }
}

# Test 8: Blocked observer is BLOCKED, never PASS and never silent.
Write-Host "  Test 8: Blocked observer cannot PASS..."
$tempJson8 = [IO.Path]::GetTempFileName()
try {
    $observerDeniedSeam = {
        param([string[]] $ObserverArgs)
        [pscustomobject]@{
            status = 'BLOCKED'; parsedRecords = 0; parsedName = ''
            journalQueried = $false; journalId = ''; journalRead = $false
            nextUsn = ''; mutationOperations = 0; requestedAccess = @('GENERIC_READ')
            volumeOpened = $false; filesystem = 'NTFS'
            reason = 'raw volume open denied (elevation required for production observer)'
        }
    }
    & $runner -VolumeRoot 'C:' -OutputPath $tempJson8 -RepoRoot $RepoRoot `
        -PrivilegeProbe { $true } `
        -FilesystemProbe { 'NTFS' } `
        -VolumeOpenProbe { param($p) $true } `
        -NtfsObserverCommand $observerDeniedSeam | Out-Null

    Assert-ResultSchema -JsonPath $tempJson8
    $json8 = Get-Content -LiteralPath $tempJson8 -Raw | ConvertFrom-Json
    if ($json8.status -eq 'PASS') { throw 'Denied observer must never PASS' }
    $mftSem8 = Get-Check -Json $json8 -Id 'mft-observer-semantics'
    if ($mftSem8.status -notmatch 'BLOCKED') { throw "mft-observer-semantics must be BLOCKED_*, got $($mftSem8.status)" }
    if ($json8.capabilities -contains 'mft-observer-semantics') { throw 'blocked semantics must not claim capability' }
    if ($json8.capabilities -contains 'usn-observer-semantics') { throw 'blocked semantics must not claim capability' }
    Write-Host "    PASS (Denied observer yields $($json8.status); no capability claimed)" -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $tempJson8) { Remove-Item -LiteralPath $tempJson8 -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath "$tempJson8.sha256") { Remove-Item -LiteralPath "$tempJson8.sha256" -Force -ErrorAction SilentlyContinue }
}

# Test 9: Observer with zero parsed records is FAIL, not PASS.
Write-Host "  Test 9: Zero-record observer observation is FAIL..."
$tempJson9 = [IO.Path]::GetTempFileName()
try {
    $observerEmptySeam = {
        param([string[]] $ObserverArgs)
        [pscustomobject]@{
            status = 'OBSERVED'; parsedRecords = 0; parsedName = ''
            journalQueried = $true; journalId = '0x12345'; journalRead = $true
            nextUsn = '0x5000'; mutationOperations = 0; requestedAccess = @('GENERIC_READ')
            volumeOpened = $true; filesystem = 'NTFS'; reason = ''
        }
    }
    & $runner -VolumeRoot 'C:' -OutputPath $tempJson9 -RepoRoot $RepoRoot `
        -PrivilegeProbe { $true } `
        -FilesystemProbe { 'NTFS' } `
        -VolumeOpenProbe { param($p) $true } `
        -NtfsObserverCommand $observerEmptySeam | Out-Null

    Assert-ResultSchema -JsonPath $tempJson9
    $json9 = Get-Content -LiteralPath $tempJson9 -Raw | ConvertFrom-Json
    if ($json9.status -ne 'FAIL') { throw "Zero-record observation must yield FAIL, got $($json9.status)" }
    $mftSem9 = Get-Check -Json $json9 -Id 'mft-observer-semantics'
    if ($mftSem9.status -ne 'FAIL') { throw "mft-observer-semantics must be FAIL, got $($mftSem9.status)" }
    Write-Host '    PASS (Zero-record observation yields FAIL)' -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $tempJson9) { Remove-Item -LiteralPath $tempJson9 -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath "$tempJson9.sha256") { Remove-Item -LiteralPath "$tempJson9.sha256" -Force -ErrorAction SilentlyContinue }
}

Write-Host "All Test-PrivilegedNtfsCertification tests PASSED!" -ForegroundColor Green
