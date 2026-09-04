[CmdletBinding()]
param(
    [string] $VolumeRoot = "C:",

    [string] $DiagnosticFixtureDirectory,

    [string] $OutputPath,

    [switch] $RequireExactTree,

    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,

    # Injectable seams for deterministic contract tests (all optional; defaults probe the real host).
    # Volume/privilege/filesystem detection overrides:
    [scriptblock] $PrivilegeProbe = $null,
    [scriptblock] $FilesystemProbe = $null,
    [scriptblock] $VolumeOpenProbe = $null,
    # Raw-observation readers: each receives the fixture directory and returns records with
    # Name / Size / ParentPath / FileId. $null means "no reader available" (fail-closed BLOCKED).
    [scriptblock] $MftReader = $null,
    [scriptblock] $UsnReader = $null,
    # Fixture-truth provider: receives the fixture directory, returns truth records with the
    # same shape. Default enumerates the live filesystem.
    [scriptblock] $FixtureTruthProvider = $null,
    # Production observer backend (FC-003/FC-004): `fftest ntfs-observe`, which
    # reuses the production VolumeScanner/UsnJournalReader ioctl sequences and
    # the production MFT/USN parsers in bounded read-only form.
    # $NtfsObserverCommand (optional seam) receives the probe argv array
    # (e.g. @('C','semantics')) and returns the parsed probe JSON object.
    # Otherwise the runner locates fftest.exe under build/<config>/src/fftest
    # (explicit $NtfsObserverExe wins). No backend -> fail-closed BLOCKED.
    [scriptblock] $NtfsObserverCommand = $null,
    [string] $NtfsObserverExe = ''
)

# READ-ONLY INVARIANT: this lane opens raw volumes read-only and creates campaign-owned
# fixtures through normal filesystem APIs only. It NEVER performs raw writes, MFT writes,
# or destructive FSCTLs.

$ErrorActionPreference = 'Stop'

$startUtc = [DateTime]::UtcNow
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()

# 1. Capture Git Provenance
$headSha = (& git -C $RepoRoot rev-parse HEAD 2>&1).ToString().Trim()
$treeSha = (& git -C $RepoRoot write-tree 2>&1).ToString().Trim()
$porcelain = @(& git -C $RepoRoot status --porcelain 2>&1 | ForEach-Object { $_.ToString().Trim() } | Where-Object { $_ -ne '' })
$isDirty = ($porcelain.Count -gt 0)

if ($RequireExactTree -and $isDirty) {
    throw "Exact-tree qualification required, but repository working tree is dirty ($($porcelain.Count) changes)."
}

$exactTreeBound = (-not $isDirty)

# 2. Output Setup
if (-not $OutputPath) {
    $runDir = Join-Path $RepoRoot "verify\runs\certification\privileged-ntfs"
    if (-not (Test-Path -LiteralPath $runDir)) { New-Item -ItemType Directory -Path $runDir -Force | Out-Null }
    $ts = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
    $OutputPath = Join-Path $runDir "privileged-ntfs-certification-$ts.json"
} else {
    $parent = Split-Path -Parent $OutputPath
    if ($parent -and -not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
}

$checks = [System.Collections.Generic.List[object]]::new()

function Add-CheckResult {
    param([string] $Id, [string] $Status, [string] $Message, $DetailObj = $null)
    $script:checks.Add([pscustomobject]@{
        checkId = $Id
        status = $Status
        message = $Message
        details = $DetailObj
    })
    $color = $(if ($Status -eq 'PASS') { 'Green' } elseif ($Status -match 'BLOCKED') { 'Yellow' } else { 'Red' })
    Write-Host "  [$Status] ${Id}: $Message" -ForegroundColor $color
}

Write-Host "Starting Privileged NTFS Certification Package:"
Write-Host "  Target Volume: $VolumeRoot"
Write-Host "  Git: head=$headSha, tree=$treeSha, exactTreeBound=$exactTreeBound"

# 3. Check Process Elevation (seam: $PrivilegeProbe overrides host detection)
if ($PrivilegeProbe) {
    $isAdmin = [bool](& $PrivilegeProbe)
} else {
    $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}
if ($isAdmin) {
    Add-CheckResult -Id "elevation-check" -Status "PASS" -Message "Process is executing with administrative elevation"
} else {
    Add-CheckResult -Id "elevation-check" -Status "BLOCKED_NOT_ELEVATED" -Message "Process is unprivileged; raw volume MFT/USN queries require elevation"
}

# 4. Check Filesystem Type (seam: $FilesystemProbe overrides CIM detection)
$driveLetter = $VolumeRoot.TrimEnd('\').TrimEnd(':')
if ($FilesystemProbe) {
    $fsType = [string](& $FilesystemProbe $driveLetter)
    if ([string]::IsNullOrWhiteSpace($fsType)) { $fsType = $null }
} else {
    $driveInfo = Get-PSDrive -Name $driveLetter -ErrorAction SilentlyContinue
    $fsType = $null
    if ($driveInfo) {
        # Query Win32 Volume
        $cimVol = Get-CimInstance -ClassName Win32_Volume -Filter "DriveLetter = '${driveLetter}:'" -ErrorAction SilentlyContinue
        if ($cimVol) { $fsType = $cimVol.FileSystem }
    }
}

if ($fsType -eq 'NTFS') {
    Add-CheckResult -Id "filesystem-type" -Status "PASS" -Message "Volume '${driveLetter}:' is formatted as NTFS"
} elseif ($null -eq $fsType) {
    Add-CheckResult -Id "filesystem-type" -Status "BLOCKED_ENVIRONMENT" -Message "Unable to query volume properties for '${driveLetter}:'"
} else {
    Add-CheckResult -Id "filesystem-type" -Status "BLOCKED_UNSUPPORTED_FILESYSTEM" -Message "Volume '${driveLetter}:' filesystem is '$fsType'; FastFiles requires NTFS"
}

# 5. Check Raw-Volume Read-Only Access (seam: $VolumeOpenProbe overrides the real open)
$rawHandleOpened = $false
$rawHandleClosed = $true
if ($isAdmin -and $fsType -eq 'NTFS') {
    $rawPath = "\\.\$driveLetter`:"
    try {
        if ($VolumeOpenProbe) {
            # Injected probe owns its handle lifecycle; completion without throwing counts as open.
            & $VolumeOpenProbe $rawPath | Out-Null
            $rawHandleOpened = $true
            $rawHandleClosed = $true
        } else {
            # Test open raw volume with read-only sharing; the handle is closed immediately.
            $rawHandleClosed = $false
            $fileStream = [IO.File]::Open($rawPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
            try {
                $rawHandleOpened = $true
            } finally {
                $fileStream.Close()
                $rawHandleClosed = $true
            }
        }
        Add-CheckResult -Id "raw-volume-access" -Status "PASS" -Message "Read-only handle opened successfully on $rawPath"
    } catch {
        $rawHandleOpened = $false
        Add-CheckResult -Id "raw-volume-access" -Status "BLOCKED_RAW_VOLUME_ACCESS" -Message "Failed to open read-only handle on $rawPath - $($_.Exception.Message)"
    }
} else {
    Add-CheckResult -Id "raw-volume-access" -Status "SKIPPED" -Message "Raw volume access skipped due to missing elevation or unsupported filesystem"
}

# 5b. Production observer semantics (FC-003/FC-004).
# The default backend is the real FastFiles probe; seams override it for
# deterministic contract tests. Semantics PASS requires genuine observation
# (>=1 parsed MFT record, queried+read journal, zero mutations) -- a blocked
# or absent backend can never PASS.
function Resolve-NtfsObserverExe {
    param([string] $Explicit)
    if ($Explicit -and (Test-Path -LiteralPath $Explicit)) { return $Explicit }
    foreach ($candidate in @(
        (Join-Path $RepoRoot 'build\debug\src\fftest\fftest.exe'),
        (Join-Path $RepoRoot 'build\release-test\src\fftest\fftest.exe'),
        (Join-Path $RepoRoot 'build\release\src\fftest\fftest.exe'))) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    return $null
}

function Invoke-NtfsObserver {
    param([string[]] $ObserverArgs)
    if ($NtfsObserverCommand) {
        try {
            # Single-argument binding: the seam receives the whole argv
            # array as one [string[]] (splatting re-binds positionally and
            # breaks $ObserverArgs[1] mode dispatch).
            return (& $NtfsObserverCommand $ObserverArgs)
        } catch {
            return $null
        }
    }
    $exe = Resolve-NtfsObserverExe -Explicit $NtfsObserverExe
    if (-not $exe) { return $null }
    $script:observerExeResolved = $exe
    $jsonText = (& $exe ntfs-observe @ObserverArgs 2>$null | Out-String)
    if ([string]::IsNullOrWhiteSpace($jsonText)) { return $null }
    try {
        return ($jsonText | ConvertFrom-Json)
    } catch {
        return $null
    }
}

function Get-ObserverBlockedStatus {
    param($Semantics)
    if ($null -eq $Semantics) {
        if ($script:observerExeResolved) {
            return 'FAIL'
        }
        return 'BLOCKED_ENVIRONMENT'
    }
    if ($Semantics.status -eq 'BLOCKED') {
        $reason = "$($Semantics.reason)"
        if ($reason -match 'elevation') { return 'BLOCKED_NOT_ELEVATED' }
        if ("$($Semantics.filesystem)" -ne '' -and "$($Semantics.filesystem)" -ne 'NTFS') {
            return 'BLOCKED_UNSUPPORTED_FILESYSTEM'
        }
        if ($Semantics.volumeOpened -eq $false) { return 'BLOCKED_RAW_VOLUME_ACCESS' }
        return 'BLOCKED_ENVIRONMENT'
    }
    return 'FAIL'
}

$observerExeResolved = $null
$observerSemantics = Invoke-NtfsObserver @($driveLetter, 'semantics')
$observerBackendName = $(if ($NtfsObserverCommand) { 'injected-seam' } elseif ($observerExeResolved) { $observerExeResolved } else { 'none' })
$observerPreFixtureNextUsn = $null
if ($observerSemantics -and $observerSemantics.status -eq 'OBSERVED') {
    $observerPreFixtureNextUsn = "$($observerSemantics.nextUsn)"
    $mftSemOk = ([int]$observerSemantics.parsedRecords -ge 1) -and
        (-not [string]::IsNullOrWhiteSpace("$($observerSemantics.parsedName)")) -and
        ([int]$observerSemantics.mutationOperations -eq 0)
    if ($mftSemOk) {
        Add-CheckResult -Id 'mft-observer-semantics' -Status 'PASS' `
            -Message "Production MFT observer: $($observerSemantics.parsedRecords) parsed record(s), first '$($observerSemantics.parsedName)', read-only, 0 mutations" `
            -DetailObj ([ordered]@{ backend = $observerBackendName; parsedRecords = $observerSemantics.parsedRecords; firstRecord = $observerSemantics.parsedName; journalId = "$($observerSemantics.journalId)" })
    } else {
        Add-CheckResult -Id 'mft-observer-semantics' -Status 'FAIL' `
            -Message 'Production MFT observer ran but reported no usable parsed record'
    }
    $usnSemOk = ([bool]$observerSemantics.journalQueried) -and
        ("$($observerSemantics.journalId)" -ne '' -and "$($observerSemantics.journalId)" -ne '0x0') -and
        ([bool]$observerSemantics.journalRead) -and ([int]$observerSemantics.mutationOperations -eq 0)
    if ($usnSemOk) {
        Add-CheckResult -Id 'usn-observer-semantics' -Status 'PASS' `
            -Message "Production USN observer: journal $($observerSemantics.journalId) queried+read from $($observerSemantics.nextUsn), read-only, 0 mutations" `
            -DetailObj ([ordered]@{ backend = $observerBackendName; journalId = "$($observerSemantics.journalId)"; nextUsn = "$($observerSemantics.nextUsn)" })
    } else {
        Add-CheckResult -Id 'usn-observer-semantics' -Status 'FAIL' `
            -Message 'Production USN observer ran but journal query/read is incomplete'
    }
} else {
    $blockedStatus = Get-ObserverBlockedStatus -Semantics $observerSemantics
    $blockedMessage = $(if ($null -eq $observerSemantics -and -not $observerExeResolved) {
            'No production NTFS observer backend available (fftest ntfs-observe not built); MFT/USN semantics unverified'
        } elseif ($null -eq $observerSemantics) {
            'Production NTFS observer backend failed to produce output'
        } else {
            "Production NTFS observer blocked: $($observerSemantics.reason)"
        })
    Add-CheckResult -Id 'mft-observer-semantics' -Status $blockedStatus -Message $blockedMessage
    Add-CheckResult -Id 'usn-observer-semantics' -Status $blockedStatus -Message $blockedMessage
}

# 6. Fixture Ground-Truth Leg (MFT + USN)
# Campaign-owned fixtures are created through normal filesystem APIs under a disposable
# directory; normal-filesystem truth is captured, then the privileged/raw observation path
# is queried for the same records and compared field-by-field. Raw-open alone NEVER
# qualifies: without an executed fixture comparison the MFT/USN checks yield BLOCKED_*.

function New-NtfsCertFixture {
    param([string] $FixtureDir)
    New-Item -ItemType Directory -Path $FixtureDir -Force | Out-Null
    $subDir = Join-Path $FixtureDir "ff-ntfs-fixture-subdir"
    New-Item -ItemType Directory -Path $subDir -Force | Out-Null
    $created = @()
    $binSpecs = @(
        @{ Name = "fixture-01.bin"; Dir = $FixtureDir; Bytes = 256; Fill = 65 },
        @{ Name = "fixture-02.bin"; Dir = $FixtureDir; Bytes = 1024; Fill = 66 }
    )
    foreach ($spec in $binSpecs) {
        $full = Join-Path $spec.Dir $spec.Name
        $bytes = New-Object byte[] $spec.Bytes
        for ($i = 0; $i -lt $spec.Bytes; $i++) { $bytes[$i] = [byte]$spec.Fill }
        [IO.File]::WriteAllBytes($full, $bytes)
        $created += $full
    }
    $textFull = Join-Path $subDir "fixture-03.txt"
    Set-Content -LiteralPath $textFull -Value "FastFiles NTFS ground-truth fixture 03" -Encoding ascii -NoNewline
    $created += $textFull
    $created += $subDir
    return $created
}

function Get-NtfsFixtureTruth {
    param([string] $FixtureDir)
    $rows = @()
    $files = @(Get-ChildItem -LiteralPath $FixtureDir -Recurse -File -ErrorAction Stop)
    foreach ($f in $files) {
        $fid = $null
        try {
            $fsutilOut = @(& fsutil.exe file queryFileID "$($f.FullName)" 2>$null)
            foreach ($line in $fsutilOut) {
                if ($line -match '0x[0-9A-Fa-f]+') { $fid = $Matches[0].ToUpperInvariant(); break }
            }
        } catch {
            $fid = $null
        }
        $rows += [pscustomobject]@{
            Name = $f.Name
            Size = [long]$f.Length
            ParentPath = $f.DirectoryName
            FileId = $fid
        }
    }
    return $rows
}

function Compare-NtfsFixtureRecords {
    param([array] $Truth, [array] $Observed)
    $rows = @()
    $match = $true
    $truthList = @($Truth)
    $observedList = @($Observed)
    foreach ($t in $truthList) {
        $o = $observedList | Where-Object { $_.Name -eq $t.Name } | Select-Object -First 1
        if ($null -eq $o) {
            $match = $false
            foreach ($field in @('Name', 'Size', 'ParentPath')) {
                $rows += [pscustomobject]@{ file = $t.Name; field = $field; expected = "$($t.$field)"; actual = '<missing-record>'; match = $false; compared = $true; note = 'Observed record missing' }
            }
            $rows += [pscustomobject]@{ file = $t.Name; field = 'FileId'; expected = "$($t.FileId)"; actual = '<missing-record>'; match = $false; compared = $false; note = 'Observed record missing' }
            continue
        }
        $nameOk = ("$($o.Name)" -eq "$($t.Name)")
        $rows += [pscustomobject]@{ file = $t.Name; field = 'Name'; expected = "$($t.Name)"; actual = "$($o.Name)"; match = $nameOk; compared = $true; note = '' }
        if (-not $nameOk) { $match = $false }
        $sizeOk = ([long]$o.Size -eq [long]$t.Size)
        $rows += [pscustomobject]@{ file = $t.Name; field = 'Size'; expected = "$($t.Size)"; actual = "$($o.Size)"; match = $sizeOk; compared = $true; note = '' }
        if (-not $sizeOk) { $match = $false }
        $parentOk = ("$($o.ParentPath)" -eq "$($t.ParentPath)")
        $rows += [pscustomobject]@{ file = $t.Name; field = 'ParentPath'; expected = "$($t.ParentPath)"; actual = "$($o.ParentPath)"; match = $parentOk; compared = $true; note = '' }
        if (-not $parentOk) { $match = $false }
        # FileId is platform-unreliable (fsutil availability, reader support): it is enforced
        # only when both sides supply it; every exclusion is recorded explicitly, never silent.
        if (-not [string]::IsNullOrEmpty("$($t.FileId)")) {
            if (-not [string]::IsNullOrEmpty("$($o.FileId)")) {
                $fidOk = ("$($o.FileId)".ToUpperInvariant() -eq "$($t.FileId)".ToUpperInvariant())
                $rows += [pscustomobject]@{ file = $t.Name; field = 'FileId'; expected = "$($t.FileId)"; actual = "$($o.FileId)"; match = $fidOk; compared = $true; note = '' }
                if (-not $fidOk) { $match = $false }
            } else {
                $rows += [pscustomobject]@{ file = $t.Name; field = 'FileId'; expected = "$($t.FileId)"; actual = ''; match = $true; compared = $false; note = 'Reader did not supply FileId; excluded from enforcement (documented, remaining fields still enforced)' }
            }
        } else {
            $rows += [pscustomobject]@{ file = $t.Name; field = 'FileId'; expected = ''; actual = "$($o.FileId)"; match = $true; compared = $false; note = 'Normal-filesystem truth has no FileId on this path; excluded from enforcement (documented, remaining fields still enforced)' }
        }
    }
    foreach ($o in $observedList) {
        $known = $truthList | Where-Object { $_.Name -eq $o.Name } | Select-Object -First 1
        if ($null -eq $known) {
            $match = $false
            $rows += [pscustomobject]@{ file = "$($o.Name)"; field = '<record>'; expected = '<absent>'; actual = 'present'; match = $false; compared = $true; note = 'Phantom observed record with no fixture counterpart' }
        }
    }
    return @{ Match = $match; Rows = $rows }
}

function Get-NtfsLegBlockedStatus {
    if (-not $isAdmin) { return 'BLOCKED_NOT_ELEVATED' }
    if ($fsType -ne 'NTFS') {
        if ($null -eq $fsType) { return 'BLOCKED_ENVIRONMENT' }
        return 'BLOCKED_UNSUPPORTED_FILESYSTEM'
    }
    return 'BLOCKED_RAW_VOLUME_ACCESS'
}

$mftCompared = $false
$usnCompared = $false
$mftMatched = $false
$usnMatched = $false
$fixtureDir = $null
$fixtureRootCreatedByRunner = $false
$createdFixturePaths = @()
$fixtureTruthCount = 0

# Observer-backed defaults: when no reader seams are injected but the real
# production observer verified live semantics above, fixture confirmation
# runs through targeted production record fetches (MFT) and a bounded
# journal read (USN) instead of failing closed on null readers.
$observerBacked = ((-not $MftReader) -and (-not $UsnReader) -and ($null -ne $observerSemantics) -and
    ($observerSemantics.status -eq 'OBSERVED'))
if ($rawHandleOpened -and ($MftReader -or $UsnReader -or $observerBacked)) {
    try {
        if ($DiagnosticFixtureDirectory) {
            $fixtureDir = $DiagnosticFixtureDirectory
        } else {
            $fixtureDir = Join-Path ([IO.Path]::GetTempPath()) ("ff-ntfs-cert-" + [Guid]::NewGuid().ToString("N"))
        }
        if (-not (Test-Path -LiteralPath $fixtureDir)) {
            New-Item -ItemType Directory -Path $fixtureDir -Force | Out-Null
            $fixtureRootCreatedByRunner = $true
        }
        $createdFixturePaths = @(New-NtfsCertFixture -FixtureDir $fixtureDir)
        if ($FixtureTruthProvider) {
            $truthRecords = @(& $FixtureTruthProvider $fixtureDir)
        } else {
            $truthRecords = @(Get-NtfsFixtureTruth -FixtureDir $fixtureDir)
        }
        $fixtureTruthCount = @($truthRecords).Count

        # Resolve targeted-fetch FRNs from normal-filesystem FileId truth.
        # A truth row without a FileId cannot be addressed to the production
        # record fetcher; that leg stays BLOCKED (documented), never guessed.
        $wantedFrns = @()
        $truthIdsUsable = $true
        foreach ($t in @($truthRecords)) {
            $fid = "$($t.FileId)"
            if ([string]::IsNullOrWhiteSpace($fid)) { $truthIdsUsable = $false; break }
            try {
                $num = if ($fid -match '^0[xX]') { [Convert]::ToUInt64($fid.Substring(2), 16) } else { [Convert]::ToUInt64($fid, 10) }
                $wantedFrns += ('0x{0:X}' -f $num)
            } catch {
                $truthIdsUsable = $false; break
            }
        }

        function Compare-AndRecordLeg {
            param([string] $Leg, [array] $Observed)
            $cmp = Compare-NtfsFixtureRecords -Truth $truthRecords -Observed $Observed
            $detail = [ordered]@{ fixtureDir = $fixtureDir; comparedFiles = $fixtureTruthCount; fieldResults = @($cmp.Rows) }
            if ([bool]$cmp.Match) {
                Add-CheckResult -Id "$Leg-ground-truth" -Status 'PASS' `
                    -Message "$Leg raw observation matches normal-filesystem truth ($fixtureTruthCount files, field-by-field)" -DetailObj $detail
                return $true
            }
            Add-CheckResult -Id "$Leg-ground-truth" -Status 'FAIL' `
                -Message "$Leg raw observation diverges from normal-filesystem truth; see fieldResults" -DetailObj $detail
            return $false
        }

        function ConvertTo-ObservedRecords {
            param($ObservedArray)
            $rows = @()
            foreach ($o in @($ObservedArray)) {
                if ($o.found -ne $true) {
                    $rows += [pscustomobject]@{ Name = ''; Size = [long]0; ParentPath = ''; FileId = $null; MissingFrn = $true }
                } else {
                    $rows += [pscustomobject]@{ Name = "$($o.name)"; Size = [long]$o.size; ParentPath = "$($o.parentPath)"; FileId = $null }
                }
            }
            return $rows
        }

        if ($MftReader) {
            $mftCompared = $true
            $mftObserved = @(& $MftReader $fixtureDir)
            $mftMatched = Compare-AndRecordLeg -Leg 'mft' -Observed $mftObserved
        } elseif ($observerBacked -and $truthIdsUsable) {
            $mftCompared = $true
            $findResult = Invoke-NtfsObserver (@($driveLetter, 'find') + $wantedFrns)
            if ($findResult -and $findResult.status -eq 'OBSERVED') {
                $mftObserved = @(ConvertTo-ObservedRecords -ObservedArray $findResult.observed)
                $mftMatched = Compare-AndRecordLeg -Leg 'mft' -Observed $mftObserved
            } elseif ($findResult -and $findResult.status -eq 'BLOCKED') {
                $mftCompared = $false
                Add-CheckResult -Id 'mft-ground-truth' -Status (Get-ObserverBlockedStatus -Semantics $findResult) `
                    -Message "Production MFT fetch blocked: $($findResult.reason)"
            } else {
                $mftCompared = $false
                Add-CheckResult -Id 'mft-ground-truth' -Status 'FAIL' `
                    -Message 'Production MFT fetch failed unexpectedly'
            }
        } elseif ($observerBacked) {
            Add-CheckResult -Id "mft-ground-truth" -Status "BLOCKED_ENVIRONMENT" -Message "Normal-filesystem FileId truth unavailable for targeted production fetch; fixture comparison requires addressable records"
        } else {
            Add-CheckResult -Id "mft-ground-truth" -Status "BLOCKED_ENVIRONMENT" -Message "No MFT reader available; fixture ground-truth comparison is required before MFT certification (raw-open alone does not qualify)"
        }
        if ($mftCompared -and ($null -eq $mftObserved)) { $mftObserved = @() }

        if ($UsnReader) {
            $usnCompared = $true
            $usnObserved = @(& $UsnReader $fixtureDir)
            $usnMatched = Compare-AndRecordLeg -Leg 'usn' -Observed $usnObserved
        } elseif ($observerBacked -and $truthIdsUsable -and $observerPreFixtureNextUsn) {
            $usnCompared = $true
            $usnResult = Invoke-NtfsObserver (@($driveLetter, 'usn', $observerPreFixtureNextUsn) + $wantedFrns)
            if ($usnResult -and $usnResult.status -eq 'OBSERVED') {
                $usnObserved = @(ConvertTo-ObservedRecords -ObservedArray $usnResult.observed)
                $usnMatched = Compare-AndRecordLeg -Leg 'usn' -Observed $usnObserved
            } elseif ($usnResult -and $usnResult.status -eq 'BLOCKED') {
                $usnCompared = $false
                Add-CheckResult -Id 'usn-ground-truth' -Status (Get-ObserverBlockedStatus -Semantics $usnResult) `
                    -Message "Production USN fetch blocked: $($usnResult.reason)"
            } else {
                $usnCompared = $false
                Add-CheckResult -Id 'usn-ground-truth' -Status 'FAIL' `
                    -Message 'Production USN fetch failed unexpectedly'
            }
        } elseif ($observerBacked) {
            Add-CheckResult -Id "usn-ground-truth" -Status "BLOCKED_ENVIRONMENT" -Message "Normal-filesystem FileId truth or pre-fixture journal USN unavailable for bounded production journal read"
        } else {
            Add-CheckResult -Id "usn-ground-truth" -Status "BLOCKED_ENVIRONMENT" -Message "No USN reader available; fixture ground-truth comparison is required before USN certification (raw-open alone does not qualify)"
        }
        if ($usnCompared -and ($null -eq $usnObserved)) { $usnObserved = @() }
    } catch {
        $legMessage = "Fixture ground-truth leg failed before comparison: $($_.Exception.Message)"
        if (-not $mftCompared) {
            Add-CheckResult -Id "mft-ground-truth" -Status "BLOCKED_ENVIRONMENT" -Message $legMessage
        }
        if (-not $usnCompared) {
            Add-CheckResult -Id "usn-ground-truth" -Status "BLOCKED_ENVIRONMENT" -Message $legMessage
        }
    }
} else {
    if (-not $rawHandleOpened) {
        $gateStatus = Get-NtfsLegBlockedStatus
        $gateMessage = $(if ($gateStatus -eq 'BLOCKED_NOT_ELEVATED') { "MFT/USN fixture comparison requires elevation; raw observation path unavailable" }
            elseif ($gateStatus -eq 'BLOCKED_UNSUPPORTED_FILESYSTEM') { "MFT/USN fixture comparison requires NTFS; raw observation path unavailable" }
            elseif ($gateStatus -eq 'BLOCKED_ENVIRONMENT') { "MFT/USN fixture comparison unavailable; volume properties could not be determined" }
            else { "MFT/USN fixture comparison requires a raw volume handle; raw-open failed" })
        Add-CheckResult -Id "mft-ground-truth" -Status $gateStatus -Message $gateMessage
        Add-CheckResult -Id "usn-ground-truth" -Status $gateStatus -Message $gateMessage
    } else {
        Add-CheckResult -Id "mft-ground-truth" -Status "BLOCKED_ENVIRONMENT" -Message "No MFT reader available; fixture ground-truth comparison is required before MFT certification (raw-open alone does not qualify)"
        Add-CheckResult -Id "usn-ground-truth" -Status "BLOCKED_ENVIRONMENT" -Message "No USN reader available; fixture ground-truth comparison is required before USN certification (raw-open alone does not qualify)"
    }
}

# 7. Parser Verification via Isolated Fixtures
$mftTestExe = Join-Path $RepoRoot "build\debug\tests\indexsvc\ffmftparser_tests.exe"
$usnTestExe = Join-Path $RepoRoot "build\debug\tests\indexsvc\ffusnrecord_tests.exe"

$parserChecksPass = $true
if ((Test-Path -LiteralPath $mftTestExe) -and (Test-Path -LiteralPath $usnTestExe)) {
    & $mftTestExe | Out-Null
    $mftExit = $LASTEXITCODE
    & $usnTestExe | Out-Null
    $usnExit = $LASTEXITCODE

    if ($mftExit -eq 0 -and $usnExit -eq 0) {
        Add-CheckResult -Id "parser-ground-truth-contracts" -Status "PASS" -Message "MFT record parser and USN journal record contracts verified"
    } else {
        $parserChecksPass = $false
        Add-CheckResult -Id "parser-ground-truth-contracts" -Status "FAIL" -Message "Parser contract tests exited with non-zero code (MFT: $mftExit, USN: $usnExit)"
    }
} else {
    Add-CheckResult -Id "parser-ground-truth-contracts" -Status "SKIPPED" -Message "Parser test executables not found in build/debug"
}

# 8. Fixture Cleanup Verification (failure forces lane FAIL, never hidden)
$cleanupMessages = @()
$fixtureRemoved = $true
if ($fixtureDir) {
    try {
        foreach ($p in $createdFixturePaths) {
            if (Test-Path -LiteralPath $p) {
                Remove-Item -LiteralPath $p -Force -Recurse -ErrorAction Stop
            }
        }
        if ($fixtureRootCreatedByRunner -and (Test-Path -LiteralPath $fixtureDir)) {
            Remove-Item -LiteralPath $fixtureDir -Force -Recurse -ErrorAction Stop
        }
        $remaining = @($createdFixturePaths | Where-Object { Test-Path -LiteralPath $_ })
        if ($remaining.Count -gt 0) {
            throw "Fixture remnants remain: $($remaining -join '; ')"
        }
        if ($fixtureRootCreatedByRunner -and (Test-Path -LiteralPath $fixtureDir)) {
            throw "Fixture root was not removed: $fixtureDir"
        }
        $cleanupMessages += "Fixture cleanup verified; no remnants under $fixtureDir"
    } catch {
        $fixtureRemoved = $false
        $cleanupMessages += "Fixture cleanup FAILED: $($_.Exception.Message)"
    }
} else {
    $cleanupMessages += "No fixture directory was created; nothing to remove"
}
if (-not $rawHandleClosed) {
    $fixtureRemoved = $false
    $cleanupMessages += "Raw volume handle was not closed"
}
$cleanupStatus = $(if ($fixtureRemoved) { 'PASS' } else { 'FAIL' })
$cleanup = [ordered]@{
    status = $cleanupStatus
    fixtureDir = $fixtureDir
    fixtureRemoved = $fixtureRemoved
    handlesClosed = ([bool]$rawHandleClosed)
    message = ($cleanupMessages -join '; ')
}

$stopwatch.Stop()
$endUtc = [DateTime]::UtcNow

# Derive lane status (raw-open alone can never yield PASS: mft/usn BLOCKED_* without comparison)
$hasFail = @($checks | Where-Object { $_.status -eq 'FAIL' }).Count -gt 0
$hasBlockedElev = @($checks | Where-Object { $_.status -eq 'BLOCKED_NOT_ELEVATED' }).Count -gt 0
$hasBlockedFs = @($checks | Where-Object { $_.status -eq 'BLOCKED_UNSUPPORTED_FILESYSTEM' }).Count -gt 0
$hasBlockedRaw = @($checks | Where-Object { $_.status -eq 'BLOCKED_RAW_VOLUME_ACCESS' }).Count -gt 0
$hasBlockedEnv = @($checks | Where-Object { $_.status -eq 'BLOCKED_ENVIRONMENT' }).Count -gt 0
$cleanupFailed = ($cleanupStatus -ne 'PASS')

$laneStatus = $(if ($hasFail -or $cleanupFailed) { 'FAIL' }
                elseif ($hasBlockedElev) { 'BLOCKED_NOT_ELEVATED' }
                elseif ($hasBlockedFs) { 'BLOCKED_UNSUPPORTED_FILESYSTEM' }
                elseif ($hasBlockedRaw) { 'BLOCKED_RAW_VOLUME_ACCESS' }
                elseif ($hasBlockedEnv) { 'BLOCKED_ENVIRONMENT' }
                else { 'PASS' })

$disposition = $(if ($laneStatus -eq 'PASS') { 'PASS' }
                 elseif ($laneStatus -match 'BLOCKED') { 'BLOCKED_EXTERNAL' }
                 else { 'FAIL' })

# Capabilities are claimed only for executed, matching proof.
function Test-CheckPass {
    param([string] $Id)
    $c = @($script:checks | Where-Object { $_.checkId -eq $Id }) | Select-Object -First 1
    return ($null -ne $c -and $c.status -eq 'PASS')
}
$capabilities = @()
if ($rawHandleOpened) { $capabilities += 'raw-volume-read' }
if ($mftCompared -and $mftMatched) { $capabilities += 'mft-ground-truth' }
if ($usnCompared -and $usnMatched) { $capabilities += 'usn-ground-truth' }
if (Test-CheckPass -Id 'mft-observer-semantics') { $capabilities += 'mft-observer-semantics' }
if (Test-CheckPass -Id 'usn-observer-semantics') { $capabilities += 'usn-observer-semantics' }

$resultObject = [ordered]@{
    schemaVersion = 1
    lane = "privileged-ntfs"
    status = $laneStatus
    commit = $headSha
    tree = $treeSha
    exactTreeBound = $exactTreeBound
    candidatePath = $null
    candidateHash = $null
    environment = [ordered]@{
        os = [Environment]::OSVersion.ToString()
        architecture = [Environment]::GetEnvironmentVariable('PROCESSOR_ARCHITECTURE')
        computer = "<local-host>"
        powershell = $PSVersionTable.PSVersion.ToString()
        elevated = $isAdmin
    }
    startUtc = $startUtc.ToString('o')
    endUtc = $endUtc.ToString('o')
    durationMs = [Math]::Round($stopwatch.Elapsed.TotalMilliseconds, 2)
    checks = @($checks)
    failureReason = $(if ($cleanupFailed) { "Privileged NTFS fixture cleanup verification failed: $($cleanup.message)" } elseif ($hasFail) { "Privileged NTFS verification checks failed" } else { $null })
    details = [ordered]@{
        volume = "${driveLetter}:"
        filesystem = $fsType
        readOnly = $true
        fixtureDirectory = $fixtureDir
        fixtureTruthFiles = $fixtureTruthCount
        mftCompared = $mftCompared
        usnCompared = $usnCompared
        observer = [ordered]@{
            backend = $observerBackendName
            productionPrimitive = $true
            accessMode = 'read-only'
            observerBackedFixture = [bool]$observerBacked
            preFixtureNextUsn = $observerPreFixtureNextUsn
        }
    }
    capabilities = @($capabilities)
    cleanup = $cleanup
    disposition = $disposition
    artifactHashes = [ordered]@{}
}

$jsonText = $resultObject | ConvertTo-Json -Depth 10
Set-Content -LiteralPath $OutputPath -Value $jsonText -Encoding utf8NoBOM

$fileBytes = [IO.File]::ReadAllBytes($OutputPath)
$finalSha256 = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($fileBytes)).Replace('-', '').ToLowerInvariant()
Set-Content -LiteralPath "$OutputPath.sha256" -Value "$finalSha256  $([IO.Path]::GetFileName($OutputPath))" -Encoding utf8NoBOM
Write-Host "Privileged NTFS certification finished with status: $laneStatus (disposition: $disposition)"
Write-Host "Artifact written: $OutputPath"

return [pscustomobject]$resultObject
