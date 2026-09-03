[CmdletBinding()]
param(
    [string] $VolumeRoot = "C:",

    [string] $OutputPath,

    [switch] $RequireExactTree,

    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)

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

# 3. Check Process Elevation
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if ($isAdmin) {
    Add-CheckResult -Id "elevation-check" -Status "PASS" -Message "Process is executing with administrative elevation"
} else {
    Add-CheckResult -Id "elevation-check" -Status "BLOCKED_NOT_ELEVATED" -Message "Process is unprivileged; raw volume MFT/USN queries require elevation"
}

# 4. Check Filesystem Type
$driveLetter = $VolumeRoot.TrimEnd('\').TrimEnd(':')
$driveInfo = Get-PSDrive -Name $driveLetter -ErrorAction SilentlyContinue
$fsType = $null
if ($driveInfo) {
    # Query Win32 Volume
    $cimVol = Get-CimInstance -ClassName Win32_Volume -Filter "DriveLetter = '${driveLetter}:'" -ErrorAction SilentlyContinue
    if ($cimVol) { $fsType = $cimVol.FileSystem }
}

if ($fsType -eq 'NTFS') {
    Add-CheckResult -Id "filesystem-type" -Status "PASS" -Message "Volume '${driveLetter}:' is formatted as NTFS"
} elseif ($null -eq $fsType) {
    Add-CheckResult -Id "filesystem-type" -Status "BLOCKED_ENVIRONMENT" -Message "Unable to query volume properties for '${driveLetter}:'"
} else {
    Add-CheckResult -Id "filesystem-type" -Status "BLOCKED_UNSUPPORTED_FILESYSTEM" -Message "Volume '${driveLetter}:' filesystem is '$fsType'; FastFiles requires NTFS"
}

# 5. Check Raw-Volume Read-Only Access
if ($isAdmin -and $fsType -eq 'NTFS') {
    $rawPath = "\\.\$driveLetter`:"
    try {
        # Test open raw volume with read-only sharing
        $fileStream = [IO.File]::Open($rawPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
        $fileStream.Close()
        Add-CheckResult -Id "raw-volume-access" -Status "PASS" -Message "Read-only handle opened successfully on $rawPath"
    } catch {
        Add-CheckResult -Id "raw-volume-access" -Status "BLOCKED_RAW_VOLUME_ACCESS" -Message "Failed to open read-only handle on $rawPath - $($_.Exception.Message)"
    }
} else {
    Add-CheckResult -Id "raw-volume-access" -Status "SKIPPED" -Message "Raw volume access skipped due to missing elevation or unsupported filesystem"
}

# 6. Parser Verification via Isolated Fixtures
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

$stopwatch.Stop()
$endUtc = [DateTime]::UtcNow

# Derive lane status
$hasFail = @($checks | Where-Object { $_.status -eq 'FAIL' }).Count -gt 0
$hasBlockedElev = @($checks | Where-Object { $_.status -eq 'BLOCKED_NOT_ELEVATED' }).Count -gt 0
$hasBlockedFs = @($checks | Where-Object { $_.status -eq 'BLOCKED_UNSUPPORTED_FILESYSTEM' }).Count -gt 0
$hasBlockedRaw = @($checks | Where-Object { $_.status -eq 'BLOCKED_RAW_VOLUME_ACCESS' }).Count -gt 0
$hasBlockedEnv = @($checks | Where-Object { $_.status -eq 'BLOCKED_ENVIRONMENT' }).Count -gt 0

$laneStatus = $(if ($hasFail) { 'FAIL' }
                elseif ($hasBlockedElev) { 'BLOCKED_NOT_ELEVATED' }
                elseif ($hasBlockedFs) { 'BLOCKED_UNSUPPORTED_FILESYSTEM' }
                elseif ($hasBlockedRaw) { 'BLOCKED_RAW_VOLUME_ACCESS' }
                elseif ($hasBlockedEnv) { 'BLOCKED_ENVIRONMENT' }
                else { 'PASS' })

$disposition = $(if ($laneStatus -eq 'PASS') { 'PASS' }
                 elseif ($laneStatus -match 'BLOCKED') { 'BLOCKED_EXTERNAL' }
                 else { 'FAIL' })

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
    failureReason = $(if ($hasFail) { "Privileged NTFS verification checks failed" } else { $null })
    details = [ordered]@{
        volume = "${driveLetter}:"
        filesystem = $fsType
        readOnly = $true
    }
    disposition = $disposition
    artifactHashes = [ordered]@{}
}

$jsonText = $resultObject | ConvertTo-Json -Depth 10
Set-Content -LiteralPath $OutputPath -Value $jsonText -Encoding utf8NoBOM

$fileBytes = [IO.File]::ReadAllBytes($OutputPath)
$artifactHash = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($fileBytes)).Replace('-', '').ToLowerInvariant()
$resultObject.artifactHashes[([IO.Path]::GetFileName($OutputPath))] = $artifactHash

$jsonText = $resultObject | ConvertTo-Json -Depth 10
Set-Content -LiteralPath $OutputPath -Value $jsonText -Encoding utf8NoBOM

Write-Host "Privileged NTFS certification finished with status: $laneStatus (disposition: $disposition)"
Write-Host "Artifact written: $OutputPath"

return [pscustomobject]$resultObject
