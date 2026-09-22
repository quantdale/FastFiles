[CmdletBinding()]
param(
    [switch] $InstallCandidate,

    [switch] $AuditOnly,

    [string] $CandidatePath,

    [string] $OutputPath,

    [switch] $RequireExactTree,

    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,

    [switch] $AcknowledgeHostContamination,

    [string] $InstallerArgs,
    [string] $LaunchArgs,

    [string] $InstalledExePath,

    [string] $UninstallCommand,

    [int] $TimeoutSec = 60,

    [int] $ShutdownGraceSec = 5
)

$ErrorActionPreference = 'Stop'

if ($TimeoutSec -lt 5) { $TimeoutSec = 5 }
if ($ShutdownGraceSec -lt 1) { $ShutdownGraceSec = 1 }

# AuditOnly takes precedence when both switches are present: an audit must
# never be silently upgraded into an install certification.
$installMode = $InstallCandidate -and (-not $AuditOnly)

$startUtc = [DateTime]::UtcNow
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()

# 1. Capture Git Provenance via the shared source-state authority
# (HEAD^{tree}, not git write-tree — index tree and commit tree are different
# objects and only HEAD^{tree} describes the source revision under test).
Import-Module (Join-Path $RepoRoot 'verify\core\RepositoryState.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $RepoRoot 'verify\core\Provenance.psm1') -Force -DisableNameChecking
$sourceState = Get-RepositorySourceState -RepoRoot $RepoRoot -QualificationMode $(if ($RequireExactTree) { 'exact-tree' } else { 'development' })
$headSha = $sourceState.headSha
$treeSha = $sourceState.treeSha
$isDirty = -not $sourceState.sourceClean

if ($RequireExactTree -and $isDirty) {
    throw "Exact-tree qualification required, but repository working tree is dirty ($(@($sourceState.stagedPaths + $sourceState.unstagedPaths + $sourceState.untrackedPaths).Count) changes)."
}

$exactTreeBound = (-not $isDirty)

# 2. Setup output path
if (-not $OutputPath) {
    $runDir = Join-Path $RepoRoot "verify\runs\certification\clean-host"
    if (-not (Test-Path -LiteralPath $runDir)) { New-Item -ItemType Directory -Path $runDir -Force | Out-Null }
    $ts = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
    $OutputPath = Join-Path $runDir "clean-host-certification-$ts.json"
} else {
    $parent = Split-Path -Parent $OutputPath
    if ($parent -and -not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
}

$checks = [System.Collections.Generic.List[object]]::new()
$details = [ordered]@{}

# Helper check recorder
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

# Destructive-operation guardrail: only campaign-owned disposable directories
# (FastFilesCleanHost-* directly under the system temp root) may be removed.
function Assert-CampaignOwnedPathForRemoval {
    param([string] $Path)
    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw "Refusing to remove a null or empty path."
    }
    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
    if ($full.Length -le $tempRoot.Length) {
        throw "Refusing to remove '$Path': outside the campaign temp root."
    }
    if (-not $full.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove '$Path': outside the campaign temp root."
    }
    $leaf = Split-Path -Leaf $full
    if ($leaf -notlike 'FastFilesCleanHost-*') {
        throw "Refusing to remove '$Path': not a campaign-owned disposable directory."
    }
    return $full
}

function Remove-CampaignOwnedDir {
    param([string] $Path)
    $full = Assert-CampaignOwnedPathForRemoval -Path $Path
    if (Test-Path -LiteralPath $full) {
        Remove-Item -LiteralPath $full -Recurse -Force
    }
}

# Orphan scan: live processes whose executable image sits inside the staged dir.
function Get-StagedOrphanProcesses {
    param([string] $StagedDir)
    $found = @()
    if ([string]::IsNullOrWhiteSpace($StagedDir)) { return $found }
    $prefix = ([IO.Path]::GetFullPath($StagedDir).TrimEnd('\', '/') + '\')
    foreach ($proc in (Get-Process -ErrorAction SilentlyContinue)) {
        try {
            $img = $proc.MainModule.FileName
        } catch {
            continue
        }
        if ($img -and $img.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
            $found += $proc
        }
    }
    return $found
}

function Test-ProcessAlive {
    param([int] $ProcessId)
    $p = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
    return (($null -ne $p) -and (-not $p.HasExited))
}

Write-Host "Starting Clean-Host Certification Package:"
Write-Host "  Mode: $(if ($installMode) { 'InstallCandidate' } else { 'AuditOnly' })"
Write-Host "  Git: head=$headSha, tree=$treeSha, exactTreeBound=$exactTreeBound"

# 3. Check OS and Environment
$osVer = [Environment]::OSVersion.Version
$isWin10Or11 = ($osVer.Major -ge 10)
if ($isWin10Or11) {
    Add-CheckResult -Id "os-compatibility" -Status "PASS" -Message "Supported Windows platform detected: $($osVer.ToString())"
} else {
    Add-CheckResult -Id "os-compatibility" -Status "FAIL" -Message "Unsupported OS version: $($osVer.ToString())"
}

# 4. Check Elevation
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if ($isAdmin) {
    Add-CheckResult -Id "privilege-elevation" -Status "PASS" -Message "Running with administrative elevation"
} else {
    Add-CheckResult -Id "privilege-elevation" -Status "BLOCKED_NOT_ELEVATED" -Message "Process is not elevated; clean host installation/service registration requires admin"
}

# 5. Conflict Audit (Ensure host has no conflicting FastFiles instances)
$existingProcs = @(Get-Process -Name "FastFiles*" -ErrorAction SilentlyContinue)
$existingService = Get-Service -Name "FastFilesIndexSvc" -ErrorAction SilentlyContinue
$hasConflict = $false
$conflictDetails = [System.Collections.Generic.List[string]]::new()

if ($existingProcs.Count -gt 0) {
    $hasConflict = $true
    $procNames = @($existingProcs | ForEach-Object { "$($_.Name):$($_.Id)" } | Select-Object -Unique)
    $conflictDetails.Add("Conflicting processes: $($procNames -join ', ')")
}

if ($null -ne $existingService) {
    $hasConflict = $true
    $conflictDetails.Add("Conflicting service: FastFilesIndexSvc ($($existingService.Status))")
}

if ($hasConflict) {
    $conflictText = ($conflictDetails -join "; ")
    $conflictMsg = "Host contains existing FastFiles state; clean host execution requires uncontaminated environment"
    if ($AcknowledgeHostContamination -and $installMode) {
        $conflictMsg += " (operator acknowledged contamination via -AcknowledgeHostContamination; recorded in evidence)"
    }
    Add-CheckResult -Id "host-conflict-audit" -Status "BLOCKED_ENVIRONMENT" -Message $conflictMsg -DetailObj $conflictText
} else {
    Add-CheckResult -Id "host-conflict-audit" -Status "PASS" -Message "No conflicting processes or services found on host"
}

# 6. Candidate Verification
$candidateHash = $null
$candidateFile = $null
$candidateMissing = $false
if ($CandidatePath) {
    if (Test-Path -LiteralPath $CandidatePath -PathType Leaf) {
        $candidateFile = (Resolve-Path -LiteralPath $CandidatePath).Path
        $sha256 = [Security.Cryptography.SHA256]::Create()
        $stream = [IO.File]::OpenRead($candidateFile)
        try {
            $candidateHash = [BitConverter]::ToString($sha256.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
        } finally {
            $stream.Close()
        }
        $candidateBytes = (Get-Item -LiteralPath $candidateFile).Length
        Add-CheckResult -Id "candidate-provenance" -Status "PASS" -Message "Candidate artifact verified: $candidateFile (SHA256: $candidateHash, $candidateBytes bytes)"
    } else {
        $candidateMissing = $true
        Add-CheckResult -Id "candidate-provenance" -Status "REQUIRED-BUT-UNAVAILABLE" -Message "Referenced candidate is missing or unresolvable: $CandidatePath"
    }
} else {
    Add-CheckResult -Id "candidate-provenance" -Status "SKIPPED" -Message "No candidate package path supplied; audit mode evaluated host readiness only"
}

$capabilities = @()
$cleanup = [ordered]@{
    status = 'PASS'
    stagedDir = $null
    stagedDirRemoved = $true
    stagedDirRemovalError = $null
    orphansAtSweep = 0
    message = 'Audit-only run; nothing was staged.'
}
$laneStatus = $null
$failureReason = $null
$executionProof = $false

$journeyLegIds = @(
    'install-artifact-exists', 'install-artifact-hash', 'install-artifact-provenance',
    'install-precondition', 'install-privilege-elevation', 'install-invoke',
    'install-exit-status', 'install-files-created', 'install-launch',
    'install-identity', 'install-readiness', 'install-smoke',
    'install-shutdown', 'install-orphans', 'install-uninstall-removal',
    'install-residue', 'install-classification', 'install-rollback-visibility'
)

if (-not $installMode) {
    # ------------------------------------------------------------------
    # 7a. Audit-only mode. Readiness is assessed but no product install
    # certification is implied: a clean audit NEVER yields product PASS.
    # Schema-compatible encoding of READY: top-level status
    # REQUIRED-BUT-UNAVAILABLE (install certification was not requested,
    # so it is unavailable), disposition BLOCKED_EXTERNAL, plus
    # details.auditOutcome = AUDIT_READY. REQUIRED-BUT-UNAVAILABLE is a
    # member of both the top-level and check status enums, so orchestrators
    # that embed lane statuses as check statuses stay schema-valid.
    # ------------------------------------------------------------------
    Add-CheckResult -Id "candidate-installation" -Status "SKIPPED" -Message "Installation not requested (audit-only mode default)"

    $hasFail = @($checks | Where-Object { $_.status -eq 'FAIL' }).Count -gt 0
    $hasBlockedEnv = @($checks | Where-Object { $_.status -eq 'BLOCKED_ENVIRONMENT' }).Count -gt 0
    $hasBlockedElev = @($checks | Where-Object { $_.status -eq 'BLOCKED_NOT_ELEVATED' }).Count -gt 0
    $hasRequiredUnavailable = @($checks | Where-Object { $_.status -eq 'REQUIRED-BUT-UNAVAILABLE' }).Count -gt 0

    $auditOutcome = 'AUDIT_READY'
    if ($hasFail) {
        $laneStatus = 'FAIL'
        $auditOutcome = 'AUDIT_FAILED'
        $failureReason = 'Clean host audit checks failed'
    } elseif ($hasBlockedEnv) {
        $laneStatus = 'BLOCKED_ENVIRONMENT'
        $auditOutcome = 'AUDIT_BLOCKED_ENVIRONMENT'
    } elseif ($hasBlockedElev) {
        $laneStatus = 'BLOCKED_NOT_ELEVATED'
        $auditOutcome = 'AUDIT_BLOCKED_NOT_ELEVATED'
    } elseif ($hasRequiredUnavailable) {
        $laneStatus = 'REQUIRED-BUT-UNAVAILABLE'
        $auditOutcome = 'AUDIT_REQUIRED_BUT_UNAVAILABLE'
    } else {
        $laneStatus = 'REQUIRED-BUT-UNAVAILABLE'
    }
    $details['auditOnly'] = $true
    $details['mode'] = 'AuditOnly'
    $details['auditOutcome'] = $auditOutcome
    $details['executionProof'] = $false
    $details['existingFastFilesProcesses'] = $existingProcs.Count
    $details['existingServiceFound'] = ($null -ne $existingService)
} else {
    # ------------------------------------------------------------------
    # 7b. Install-certification mode. The supplied candidate MUST be
    # really executed; without execution proof no PASS is possible.
    # Contamination refuses install work by default (BLOCKED_ENVIRONMENT).
    # -AcknowledgeHostContamination is an explicit operator override for
    # campaigns that knowingly certify on non-pristine hosts (e.g. upgrade
    # certification): the conflict stays recorded as BLOCKED_ENVIRONMENT
    # evidence, but the journey may proceed and overall status is decided
    # by the journey legs plus the cleanup sweep. The override never waives
    # the elevation gate for third-party installers.
    # ------------------------------------------------------------------
    $capabilities = @('clean-install', 'installed-runtime', 'uninstall-cleanup')
    $details['auditOnly'] = $false
    $details['mode'] = 'InstallCandidate'
    $details['existingFastFilesProcesses'] = $existingProcs.Count
    $details['existingServiceFound'] = ($null -ne $existingService)
    $details['contaminationAcknowledged'] = [bool]($AcknowledgeHostContamination -and $hasConflict)
    $details['acknowledgedConflicts'] = $(if ($AcknowledgeHostContamination -and $hasConflict) { ($conflictDetails -join "; ") } else { $null })

    $stagedDir = $null
    $journeyState = @{ Failed = $false; FailedLeg = $null }
    $missingInstallableMarker = 'BLOCKED_MISSING_INSTALLABLE_CANDIDATE'

    function Invoke-JourneyLeg {
        param([string] $Id, [scriptblock] $Body)
        if ($journeyState.Failed) {
            Add-CheckResult -Id $Id -Status 'SKIPPED' -Message "Skipped because earlier leg '$($journeyState.FailedLeg)' failed"
            return
        }
        & $Body
    }

    function Set-JourneyFailed {
        param([string] $Id)
        $journeyState.Failed = $true
        $journeyState.FailedLeg = $Id
    }

    function Skip-InstallJourney {
        param([string] $Reason)
        foreach ($legId in $journeyLegIds) {
            Add-CheckResult -Id $legId -Status 'SKIPPED' -Message $Reason
        }
    }

    if ($hasConflict -and (-not $AcknowledgeHostContamination)) {
        # Contaminated host: refuse any destructive/install work.
        Skip-InstallJourney -Reason 'Skipped: refusing install work on contaminated host (BLOCKED_ENVIRONMENT)'
        $laneStatus = 'BLOCKED_ENVIRONMENT'
        $details['executionProof'] = $false
    } elseif ($candidateMissing -or (-not $candidateFile)) {
        # Fail closed: no resolvable installable, no execution, no PASS.
        Add-CheckResult -Id 'install-artifact-exists' -Status 'FAIL' -Message "$missingInstallableMarker`: no installable candidate was supplied or resolved"
        foreach ($legId in @($journeyLegIds | Where-Object { $_ -ne 'install-artifact-exists' })) {
            Add-CheckResult -Id $legId -Status 'SKIPPED' -Message 'Skipped: no installable candidate to execute'
        }
        $laneStatus = 'FAIL'
        $failureReason = "${missingInstallableMarker}: -InstallCandidate requires a resolvable -CandidatePath"
        $details['executionProof'] = $false
    } else {
        $candidateExt = [IO.Path]::GetExtension($candidateFile).ToLowerInvariant()
        $candidateBase = [IO.Path]::GetFileNameWithoutExtension($candidateFile)
        $candidateKind = 'unsupported'
        if ($candidateExt -eq '.zip') {
            $candidateKind = 'zip'
        } elseif ($candidateExt -eq '.exe' -and $candidateBase -ieq 'FastFiles') {
            $candidateKind = 'portable-exe'
        } elseif ($candidateExt -eq '.exe') {
            $candidateKind = 'installer-exe'
        }
        $details['candidateKind'] = $candidateKind
        $details['installedExe'] = $InstalledExePath
        $details['uninstallCommandPresent'] = (-not [string]::IsNullOrWhiteSpace($UninstallCommand))

        if ($candidateKind -eq 'unsupported') {
            Add-CheckResult -Id 'install-artifact-exists' -Status 'PASS' -Message "Candidate file exists: $candidateFile"
            Add-CheckResult -Id 'install-artifact-hash' -Status 'PASS' -Message "Candidate hash recorded: $candidateHash"
            Add-CheckResult -Id 'install-artifact-provenance' -Status 'PASS' -Message "Source state recorded: commit $headSha / tree $treeSha (exactTreeBound=$exactTreeBound); candidate identity not applicable for unsupported kind"
            Add-CheckResult -Id 'install-precondition' -Status 'PASS' -Message 'Host precondition checks passed'
            Add-CheckResult -Id 'install-privilege-elevation' -Status $(if ($isAdmin) { 'PASS' } else { 'BLOCKED_NOT_ELEVATED' }) -Message $(if ($isAdmin) { 'Running with administrative elevation' } else { 'Process is not elevated' })
            Add-CheckResult -Id 'install-invoke' -Status 'FAIL' -Message "Unsupported candidate type '$candidateExt'; only .exe and .zip candidates can be executed (fail-closed)"
            foreach ($legId in @($journeyLegIds | Where-Object { @('install-artifact-exists', 'install-artifact-hash', 'install-artifact-provenance', 'install-precondition', 'install-privilege-elevation', 'install-invoke') -notcontains $_ })) {
                Add-CheckResult -Id $legId -Status 'SKIPPED' -Message 'Skipped: unsupported candidate was not executed'
            }
            $laneStatus = 'FAIL'
            $failureReason = "Unsupported candidate type '$candidateExt'; cannot execute install journey"
            $details['executionProof'] = $false
        } elseif (($candidateKind -eq 'installer-exe') -and (-not $isAdmin)) {
            # Arbitrary installers may write outside campaign dirs: elevation required.
            Add-CheckResult -Id 'install-artifact-exists' -Status 'PASS' -Message "Candidate file exists: $candidateFile"
            Add-CheckResult -Id 'install-artifact-hash' -Status 'PASS' -Message "Candidate hash recorded: $candidateHash"
            Add-CheckResult -Id 'install-artifact-provenance' -Status 'PASS' -Message "Source state recorded: commit $headSha / tree $treeSha (exactTreeBound=$exactTreeBound); candidate identity not checked for blocked installer path"
            Add-CheckResult -Id 'install-precondition' -Status 'PASS' -Message 'Host precondition checks passed'
            Add-CheckResult -Id 'install-privilege-elevation' -Status 'BLOCKED_NOT_ELEVATED' -Message 'Cannot execute third-party installer without elevation'
            foreach ($legId in @($journeyLegIds | Where-Object { @('install-artifact-exists', 'install-artifact-hash', 'install-artifact-provenance', 'install-precondition', 'install-privilege-elevation') -notcontains $_ })) {
                Add-CheckResult -Id $legId -Status 'SKIPPED' -Message 'Skipped: installer execution blocked without elevation'
            }
            $laneStatus = 'BLOCKED_NOT_ELEVATED'
            $details['executionProof'] = $false
        } else {
            # ---- Real journey: stage into a campaign-owned disposable dir ----
            $stagedDir = Join-Path ([IO.Path]::GetTempPath()) ("FastFilesCleanHost-" + [Guid]::NewGuid().ToString("N"))
            New-Item -ItemType Directory -Path $stagedDir -Force | Out-Null
            $cleanup['stagedDir'] = $stagedDir
            $cleanup['stagedDirRemoved'] = $false
            $cleanup['message'] = 'Install journey staged a disposable dir; see sweep outcome.'
            $details['stagedDir'] = $stagedDir

            $script:stagedRuntimeExe = $null
            $script:stagedInstallerExe = $null
            $script:appPid = $null
            $script:launchAttempted = $false
            $script:uninstallOk = $false
            $script:invokeOk = $false

            try {
                Invoke-JourneyLeg -Id 'install-artifact-exists' -Body {
                    $size = (Get-Item -LiteralPath $candidateFile).Length
                    Add-CheckResult -Id 'install-artifact-exists' -Status 'PASS' -Message "Installable candidate exists: $candidateFile ($size bytes)"
                }

                Invoke-JourneyLeg -Id 'install-artifact-hash' -Body {
                    if ([string]::IsNullOrWhiteSpace($candidateHash)) {
                        Set-JourneyFailed -Id 'install-artifact-hash'
                        Add-CheckResult -Id 'install-artifact-hash' -Status 'FAIL' -Message 'Candidate hash could not be computed'
                    } else {
                        Add-CheckResult -Id 'install-artifact-hash' -Status 'PASS' -Message "Candidate SHA256: $candidateHash"
                    }
                }

                Invoke-JourneyLeg -Id 'install-artifact-provenance' -Body {
                    # Structural binding, not a restatement of host git state:
                    # when the candidate is a FastFiles product executable, its
                    # embedded PrivateBuild must equal the current source HEAD.
                    # Archives and foreign installers cannot expose PrivateBuild
                    # here, so they record an honest limited-binding result.
                    $gateMode = if ($RequireExactTree) { 'exact-tree' } else { 'development' }
                    if ($candidateKind -eq 'installer-exe' -or $candidateKind -eq 'portable-exe') {
                        $binRecord = Get-BinaryProvenanceRecord -Path $candidateFile
                        $sourceRecord = Get-SourceProvenanceRecord -RepoRoot $RepoRoot -Mode $(if ($RequireExactTree) { 'exact-tree' } else { 'development' })
                        $binding = Test-BinarySourceBinding -BinaryRecords @($binRecord) -SourceRecord $sourceRecord -Mode $(if ($RequireExactTree) { 'strict' } else { 'development' })
                        $details['binaryProvenance'] = New-BinaryProvenanceEvidence -SourceRecord $sourceRecord -Binding $binding -BinaryRecords @($binRecord) -Stage 'install-artifact-provenance' -Mode $gateMode
                        if (-not $binding.valid) {
                            Set-JourneyFailed -Id 'install-artifact-provenance'
                            $message = Format-ProvenanceMismatch -ReasonCode ($binding.issueCodes | Select-Object -First 1) -Stage 'install-artifact-provenance' `
                                -Expected $sourceRecord.headSha -Actual $binding.setIdentity -Binaries @([IO.Path]::GetFileName($candidateFile)) `
                                -Check 'head-binding' -Detail ($binding.issueCodes -join ',') -SourceRecord $sourceRecord
                            Add-CheckResult -Id 'install-artifact-provenance' -Status 'FAIL' -Message $message
                        } else {
                            Add-CheckResult -Id 'install-artifact-provenance' -Status 'PASS' -Message "Candidate PrivateBuild $($binRecord.buildIdentity) bound to commit $headSha / tree $treeSha (exactTreeBound=$exactTreeBound, sha256=$candidateHash)"
                        }
                    } else {
                        Add-CheckResult -Id 'install-artifact-provenance' -Status 'PASS' -Message "Archive candidate source state recorded: commit $headSha / tree $treeSha (exactTreeBound=$exactTreeBound, sha256=$candidateHash); embedded PrivateBuild unavailable until package payloads are read"
                        $details['candidateBinaryIdentityUnavailable'] = $true
                    }
                }

                Invoke-JourneyLeg -Id 'install-precondition' -Body {
                    $probe = Join-Path $stagedDir 'precondition.probe'
                    Set-Content -LiteralPath $probe -Value 'precondition' -Encoding ascii -NoNewline
                    Remove-Item -LiteralPath $probe -Force
                    Add-CheckResult -Id 'install-precondition' -Status 'PASS' -Message "Staging dir writable ($stagedDir); no host conflicts; candidate readable"
                }

                Invoke-JourneyLeg -Id 'install-privilege-elevation' -Body {
                    if ($candidateKind -eq 'installer-exe') {
                        Add-CheckResult -Id 'install-privilege-elevation' -Status 'PASS' -Message 'Running with administrative elevation for installer execution'
                    } else {
                        Add-CheckResult -Id 'install-privilege-elevation' -Status 'PASS' -Message 'Staged execution is confined to the campaign temp dir; elevation not required'
                    }
                }

                Invoke-JourneyLeg -Id 'install-invoke' -Body {
                    try {
                        if ($candidateKind -eq 'zip') {
                            Expand-Archive -LiteralPath $candidateFile -DestinationPath $stagedDir -Force
                            Add-CheckResult -Id 'install-invoke' -Status 'PASS' -Message "Expanded candidate zip into staged dir: $stagedDir"
                        } elseif ($candidateKind -eq 'portable-exe') {
                            Copy-Item -LiteralPath $candidateFile -Destination (Join-Path $stagedDir 'FastFiles.exe') -Force
                            $script:stagedRuntimeExe = Join-Path $stagedDir 'FastFiles.exe'
                            Add-CheckResult -Id 'install-invoke' -Status 'PASS' -Message "Staged portable executable into disposable dir: $script:stagedRuntimeExe"
                        } else {
                            $dest = Join-Path $stagedDir ([IO.Path]::GetFileName($candidateFile))
                            Copy-Item -LiteralPath $candidateFile -Destination $dest -Force
                            $script:stagedInstallerExe = $dest
                            $startArgs = @{ FilePath = $dest; PassThru = $true }
                            if (-not [string]::IsNullOrWhiteSpace($InstallerArgs)) { $startArgs['ArgumentList'] = $InstallerArgs }
                            $installerProc = Start-Process @startArgs
                            $script:installerProc = $installerProc
                            Add-CheckResult -Id 'install-invoke' -Status 'PASS' -Message "Installer process started from staged copy (pid $($installerProc.Id))$($(if ($InstallerArgs) { " with args: $InstallerArgs" } else { '' }))"
                        }
                        $script:invokeOk = $true
                    } catch {
                        Set-JourneyFailed -Id 'install-invoke'
                        Add-CheckResult -Id 'install-invoke' -Status 'FAIL' -Message "Deployment/invocation failed: $($_.Exception.Message)"
                    }
                }

                Invoke-JourneyLeg -Id 'install-exit-status' -Body {
                    if ($candidateKind -eq 'installer-exe') {
                        try {
                            $exited = $script:installerProc.WaitForExit($TimeoutSec * 1000)
                            if (-not $exited) {
                                try { Stop-Process -Id $script:installerProc.Id -Force -ErrorAction SilentlyContinue } catch {}
                                Set-JourneyFailed -Id 'install-exit-status'
                                Add-CheckResult -Id 'install-exit-status' -Status 'FAIL' -Message "Installer did not exit within ${TimeoutSec}s and was terminated"
                            } elseif ($script:installerProc.ExitCode -ne 0) {
                                Set-JourneyFailed -Id 'install-exit-status'
                                Add-CheckResult -Id 'install-exit-status' -Status 'FAIL' -Message "Installer exited with code $($script:installerProc.ExitCode) (expected 0)"
                            } else {
                                Add-CheckResult -Id 'install-exit-status' -Status 'PASS' -Message 'Installer exited with code 0'
                            }
                        } catch {
                            Set-JourneyFailed -Id 'install-exit-status'
                            Add-CheckResult -Id 'install-exit-status' -Status 'FAIL' -Message "Installer exit-status collection failed: $($_.Exception.Message)"
                        }
                    } else {
                        Add-CheckResult -Id 'install-exit-status' -Status 'PASS' -Message 'Staging completed (no installer exit code; deployment verified by files-created leg)'
                    }
                }

                Invoke-JourneyLeg -Id 'install-files-created' -Body {
                    $files = @(Get-ChildItem -LiteralPath $stagedDir -Recurse -File -ErrorAction SilentlyContinue)
                    if ($files.Count -ge 1) {
                        $shown = @($files | Select-Object -First 10 | ForEach-Object { $_.FullName.Substring($stagedDir.Length).TrimStart('\', '/') })
                        Add-CheckResult -Id 'install-files-created' -Status 'PASS' -Message "Deployment produced $($files.Count) file(s): $($shown -join ', ')$($(if ($files.Count -gt 10) { ', ...' } else { '' }))"
                    } else {
                        Set-JourneyFailed -Id 'install-files-created'
                        Add-CheckResult -Id 'install-files-created' -Status 'FAIL' -Message 'Deployment produced no files in the staged dir'
                    }
                }

                Invoke-JourneyLeg -Id 'install-launch' -Body {
                    $script:launchAttempted = $true
                    try {
                        if ($candidateKind -eq 'installer-exe') {
                            if ($InstalledExePath -and (Test-Path -LiteralPath $InstalledExePath -PathType Leaf)) {
                                $script:stagedRuntimeExe = (Resolve-Path -LiteralPath $InstalledExePath).Path
                            } else {
                                Set-JourneyFailed -Id 'install-launch'
                                Add-CheckResult -Id 'install-launch' -Status 'FAIL' -Message 'runtime-not-found: installer finished but no installed runtime was found (supply -InstalledExePath)'
                                return
                            }
                        } elseif ($candidateKind -eq 'zip') {
                            $found = Get-ChildItem -LiteralPath $stagedDir -Filter 'FastFiles.exe' -File -ErrorAction SilentlyContinue | Select-Object -First 1
                            if (-not $found) {
                                $found = Get-ChildItem -LiteralPath $stagedDir -Filter 'FastFiles.exe' -Recurse -File -ErrorAction SilentlyContinue | Select-Object -First 1
                            }
                            if (-not $found) {
                                Set-JourneyFailed -Id 'install-launch'
                                Add-CheckResult -Id 'install-launch' -Status 'FAIL' -Message 'runtime-not-found: no FastFiles.exe in the expanded candidate'
                                return
                            }
                            $script:stagedRuntimeExe = $found.FullName
                        }
                        $launchParams = @{ FilePath = $script:stagedRuntimeExe; PassThru = $true }
                        if (-not [string]::IsNullOrWhiteSpace($LaunchArgs)) { $launchParams['ArgumentList'] = $LaunchArgs }
                        $app = Start-Process @launchParams
                        $script:appPid = $app.Id
                        Add-CheckResult -Id 'install-launch' -Status 'PASS' -Message "Launched runtime from staged location: $script:stagedRuntimeExe (pid $($app.Id))"
                    } catch {
                        Set-JourneyFailed -Id 'install-launch'
                        Add-CheckResult -Id 'install-launch' -Status 'FAIL' -Message "Launch failed: $($_.Exception.Message)"
                    }
                }

                Invoke-JourneyLeg -Id 'install-identity' -Body {
                    try {
                        # The image path may be momentarily unavailable while the
                        # new process starts; poll briefly before concluding.
                        $img = $null
                        $p = $null
                        for ($i = 0; $i -lt 20; $i++) {
                            $p = Get-Process -Id $script:appPid -ErrorAction SilentlyContinue
                            if (($null -eq $p) -or $p.HasExited) { break }
                            try {
                                if ($p.MainModule) { $img = $p.MainModule.FileName }
                            } catch {
                                $img = $null
                            }
                            if ($img) { break }
                            Start-Sleep -Milliseconds 500
                        }
                        if (($null -eq $p) -or $p.HasExited) {
                            Set-JourneyFailed -Id 'install-identity'
                            Add-CheckResult -Id 'install-identity' -Status 'FAIL' -Message "Launched process (pid $($script:appPid)) already exited before identity could be verified"
                            return
                        }
                        if (-not $img) {
                            Set-JourneyFailed -Id 'install-identity'
                            Add-CheckResult -Id 'install-identity' -Status 'FAIL' -Message "Identity verification failed: process image path unreadable for pid $($script:appPid)"
                            return
                        }
                        $expectedBase = [IO.Path]::GetFileNameWithoutExtension($script:stagedRuntimeExe)
                        $locationOk = $false
                        if ($candidateKind -eq 'installer-exe') {
                            $locationOk = $img.Equals($script:stagedRuntimeExe, [StringComparison]::OrdinalIgnoreCase)
                        } else {
                            $stagedPrefix = ([IO.Path]::GetFullPath($stagedDir).TrimEnd('\', '/') + '\')
                            $locationOk = $img.StartsWith($stagedPrefix, [StringComparison]::OrdinalIgnoreCase)
                        }
                        if ($p.Name -ieq $expectedBase -and $locationOk) {
                            Add-CheckResult -Id 'install-identity' -Status 'PASS' -Message "Process identity verified: name=$($p.Name), pid=$($p.Id), image=$img"
                        } else {
                            Set-JourneyFailed -Id 'install-identity'
                            Add-CheckResult -Id 'install-identity' -Status 'FAIL' -Message "Identity mismatch: name=$($p.Name) (expected $expectedBase), image=$img"
                        }
                    } catch {
                        Set-JourneyFailed -Id 'install-identity'
                        Add-CheckResult -Id 'install-identity' -Status 'FAIL' -Message "Identity verification failed: $($_.Exception.Message)"
                    }
                }

                Invoke-JourneyLeg -Id 'install-readiness' -Body {
                    $aliveSlices = 0
                    for ($i = 0; $i -lt 10; $i++) {
                        if (-not (Test-ProcessAlive -ProcessId $script:appPid)) { break }
                        $aliveSlices++
                        Start-Sleep -Milliseconds 500
                    }
                    if ($aliveSlices -lt 10) {
                        Set-JourneyFailed -Id 'install-readiness'
                        Add-CheckResult -Id 'install-readiness' -Status 'FAIL' -Message 'Runtime did not stay alive through the readiness window'
                    } else {
                        $p = Get-Process -Id $script:appPid -ErrorAction SilentlyContinue
                        $evidence = 'process alive for 5s readiness window'
                        if ($p) { $evidence += " (responding=$($p.Responding), workingSet=$($p.WorkingSet64))" }
                        Add-CheckResult -Id 'install-readiness' -Status 'PASS' -Message "Runtime ready: $evidence"
                    }
                }

                Invoke-JourneyLeg -Id 'install-smoke' -Body {
                    Start-Sleep -Seconds 1
                    $p = Get-Process -Id $script:appPid -ErrorAction SilentlyContinue
                    if (($null -ne $p) -and (-not $p.HasExited)) {
                        Add-CheckResult -Id 'install-smoke' -Status 'PASS' -Message "Smoke check passed: runtime alive (pid $($p.Id), cpu=$($p.CPU), workingSet=$($p.WorkingSet64))"
                    } else {
                        Set-JourneyFailed -Id 'install-smoke'
                        Add-CheckResult -Id 'install-smoke' -Status 'FAIL' -Message 'Smoke check failed: runtime exited unexpectedly'
                    }
                }

                Invoke-JourneyLeg -Id 'install-shutdown' -Body {
                    try {
                        $p = Get-Process -Id $script:appPid -ErrorAction SilentlyContinue
                        if (($null -eq $p) -or $p.HasExited) {
                            Add-CheckResult -Id 'install-shutdown' -Status 'PASS' -Message 'Runtime had already exited; no shutdown required'
                            return
                        }
                        $method = 'graceful-close'
                        $hasWindow = $false
                        try { $hasWindow = ($p.MainWindowHandle -ne 0) } catch {}
                        if ($hasWindow) {
                            try { [void]$p.CloseMainWindow() } catch {}
                            $deadline = [DateTime]::UtcNow.AddSeconds($ShutdownGraceSec)
                            while ((Test-ProcessAlive -ProcessId $script:appPid) -and ([DateTime]::UtcNow -lt $deadline)) {
                                Start-Sleep -Milliseconds 250
                            }
                        }
                        if (Test-ProcessAlive -ProcessId $script:appPid) {
                            $method = 'forced-terminate'
                            Stop-Process -Id $script:appPid -Force -ErrorAction Stop
                            $deadline = [DateTime]::UtcNow.AddSeconds(5)
                            while ((Test-ProcessAlive -ProcessId $script:appPid) -and ([DateTime]::UtcNow -lt $deadline)) {
                                Start-Sleep -Milliseconds 250
                            }
                        }
                        if (Test-ProcessAlive -ProcessId $script:appPid) {
                            Set-JourneyFailed -Id 'install-shutdown'
                            Add-CheckResult -Id 'install-shutdown' -Status 'FAIL' -Message 'Shutdown failed: runtime survived forced termination'
                        } else {
                            Add-CheckResult -Id 'install-shutdown' -Status 'PASS' -Message "Runtime shut down via $method"
                        }
                    } catch {
                        Set-JourneyFailed -Id 'install-shutdown'
                        Add-CheckResult -Id 'install-shutdown' -Status 'FAIL' -Message "Shutdown failed: $($_.Exception.Message)"
                    }
                }

                Invoke-JourneyLeg -Id 'install-orphans' -Body {
                    $orphans = @(Get-StagedOrphanProcesses -StagedDir $stagedDir)
                    if ($orphans.Count -eq 0) {
                        Add-CheckResult -Id 'install-orphans' -Status 'PASS' -Message 'No orphan processes remain from the staged deployment'
                    } else {
                        Set-JourneyFailed -Id 'install-orphans'
                        $desc = @($orphans | ForEach-Object { "$($_.Name):$($_.Id)" } | Select-Object -Unique)
                        Add-CheckResult -Id 'install-orphans' -Status 'FAIL' -Message "Orphan processes still reference the staged dir: $($desc -join ', ')"
                    }
                }

                Invoke-JourneyLeg -Id 'install-uninstall-removal' -Body {
                    $uninstallError = $null
                    if ($candidateKind -eq 'installer-exe') {
                        if ([string]::IsNullOrWhiteSpace($UninstallCommand)) {
                            Set-JourneyFailed -Id 'install-uninstall-removal'
                            Add-CheckResult -Id 'install-uninstall-removal' -Status 'FAIL' -Message 'No -UninstallCommand supplied; removal of installer-deployed state cannot be verified (fail-closed)'
                            return
                        }
                        try {
                            $up = Start-Process -FilePath $env:ComSpec -ArgumentList "/c $UninstallCommand" -NoNewWindow -PassThru
                            if (-not $up.WaitForExit($TimeoutSec * 1000)) {
                                try { Stop-Process -Id $up.Id -Force -ErrorAction SilentlyContinue } catch {}
                                $uninstallError = "Uninstaller did not exit within ${TimeoutSec}s and was terminated"
                            } elseif ($up.ExitCode -ne 0) {
                                $uninstallError = "Uninstaller exited with code $($up.ExitCode) (expected 0)"
                            }
                        } catch {
                            $uninstallError = "Uninstaller execution failed: $($_.Exception.Message)"
                        }
                    }
                    try {
                        Remove-CampaignOwnedDir -Path $stagedDir
                    } catch {
                        $stageError = "Staged-dir removal failed: $($_.Exception.Message)"
                        if ($uninstallError) { $uninstallError += "; $stageError" } else { $uninstallError = $stageError }
                    }
                    if ($uninstallError) {
                        Set-JourneyFailed -Id 'install-uninstall-removal'
                        Add-CheckResult -Id 'install-uninstall-removal' -Status 'FAIL' -Message "Uninstall/removal failed: $uninstallError"
                    } else {
                        $script:uninstallOk = $true
                        $msg = 'Staged deployment removed from disposable dir'
                        if ($candidateKind -eq 'installer-exe') { $msg = "Uninstaller exited 0; $msg" }
                        Add-CheckResult -Id 'install-uninstall-removal' -Status 'PASS' -Message $msg
                    }
                }

                Invoke-JourneyLeg -Id 'install-residue' -Body {
                    $leftovers = [System.Collections.Generic.List[string]]::new()
                    if (Test-Path -LiteralPath $stagedDir) { $leftovers.Add("staged dir still present: $stagedDir") }
                    if (($candidateKind -eq 'installer-exe') -and $InstalledExePath -and (Test-Path -LiteralPath $InstalledExePath)) {
                        $leftovers.Add("installed runtime still present: $InstalledExePath")
                    }
                    if ($leftovers.Count -eq 0) {
                        Add-CheckResult -Id 'install-residue' -Status 'PASS' -Message 'No residue: staged dir removed and no installed runtime remains'
                    } else {
                        Set-JourneyFailed -Id 'install-residue'
                        Add-CheckResult -Id 'install-residue' -Status 'FAIL' -Message "Residue detected: $($leftovers -join '; ')"
                    }
                }

                # Classification always runs: it records the journey verdict.
                if ($journeyState.Failed) {
                    Add-CheckResult -Id 'install-classification' -Status 'FAIL' -Message "Install journey classified FAILED at leg '$($journeyState.FailedLeg)'"
                } else {
                    Add-CheckResult -Id 'install-classification' -Status 'PASS' -Message 'Install journey classified: install certified (all legs passed with execution proof)'
                }

                # Rollback visibility always runs: it documents the rollback path and outcome.
                $rollbackAvailable = $true
                $rollbackMethod = 'staged-dir-removal'
                if ($candidateKind -eq 'installer-exe') {
                    $rollbackAvailable = (-not [string]::IsNullOrWhiteSpace($UninstallCommand))
                    $rollbackMethod = 'uninstall-command'
                }
                $rollbackPerformed = $script:uninstallOk -and (-not (Test-Path -LiteralPath $stagedDir))
                Add-CheckResult -Id 'install-rollback-visibility' -Status 'PASS' -Message "Rollback visible: available=$rollbackAvailable, performed=$rollbackPerformed, method=$rollbackMethod" -DetailObj ([ordered]@{
                    rollbackAvailable = $rollbackAvailable
                    rollbackPerformed = $rollbackPerformed
                    rollbackMethod = $rollbackMethod
                })
            } catch {
                if (-not $journeyState.Failed) {
                    Set-JourneyFailed -Id 'install-journey'
                    Add-CheckResult -Id 'install-journey-aborted' -Status 'FAIL' -Message "Install journey aborted with exception: $($_.Exception.Message)"
                }
            }

            $executionProof = ($script:invokeOk -and $script:launchAttempted)

            # ---- Cleanup sweep: runner-owned resources only (guardrailed) ----
            try {
                $sweepOrphans = @(Get-StagedOrphanProcesses -StagedDir $stagedDir)
                $cleanup['orphansAtSweep'] = $sweepOrphans.Count
                if (Test-Path -LiteralPath $stagedDir) {
                    try {
                        Remove-CampaignOwnedDir -Path $stagedDir
                        $cleanup['stagedDirRemoved'] = $true
                    } catch {
                        $cleanup['status'] = 'FAIL'
                        $cleanup['stagedDirRemoved'] = $false
                        $cleanup['stagedDirRemovalError'] = $_.Exception.Message
                    }
                    if (Test-Path -LiteralPath $stagedDir) {
                        $cleanup['status'] = 'FAIL'
                        $cleanup['stagedDirRemoved'] = $false
                    }
                } else {
                    $cleanup['stagedDirRemoved'] = $true
                }
                if ($cleanup['status'] -eq 'PASS') {
                    $cleanup['message'] = 'Cleanup sweep complete: staged dir removed, no runner-owned residue.'
                } else {
                    $cleanup['message'] = 'Cleanup sweep FAILED: runner-owned residue remains; see stagedDirRemovalError.'
                }
            } catch {
                $cleanup['status'] = 'FAIL'
                $cleanup['message'] = "Cleanup sweep failed: $($_.Exception.Message)"
            }

            if ($cleanup['status'] -eq 'PASS') {
                Add-CheckResult -Id 'cleanup-sweep' -Status 'PASS' -Message 'Cleanup sweep verified: no runner-owned residue remains'
            } else {
                Add-CheckResult -Id 'cleanup-sweep' -Status 'FAIL' -Message "Cleanup sweep failed: $($cleanup['stagedDirRemovalError'])"
            }

            $details['executionProof'] = $executionProof
            if ($journeyState.Failed -or ($cleanup['status'] -ne 'PASS')) {
                $laneStatus = 'FAIL'
                if ($cleanup['status'] -ne 'PASS') {
                    $failureReason = "Cleanup failed: $($cleanup['stagedDirRemovalError'])"
                } elseif (-not $executionProof) {
                    $failureReason = 'Install journey failed before execution proof was established; PASS is not possible without real execution'
                } else {
                    $failureReason = "Install journey failed at leg '$($journeyState.FailedLeg)'"
                }
            } elseif (-not $executionProof) {
                $laneStatus = 'FAIL'
                $failureReason = 'Install journey completed without execution proof; PASS is not possible without real execution'
            } else {
                $laneStatus = 'PASS'
            }
        }
    }
}

$stopwatch.Stop()
$endUtc = [DateTime]::UtcNow

$disposition = $(if ($laneStatus -eq 'PASS') { 'PASS' }
                 elseif ($laneStatus -match 'BLOCKED') { 'BLOCKED_EXTERNAL' }
                 elseif ($laneStatus -eq 'REQUIRED-BUT-UNAVAILABLE') { 'BLOCKED_EXTERNAL' }
                 else { 'FAIL' })

$resultObject = [ordered]@{
    schemaVersion = 1
    lane = "clean-host"
    status = $laneStatus
    commit = $headSha
    tree = $treeSha
    exactTreeBound = $exactTreeBound
    candidatePath = $CandidatePath
    candidateHash = $candidateHash
    capabilities = $capabilities
    cleanup = $cleanup
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
    failureReason = $failureReason
    details = $details
    disposition = $disposition
    artifactHashes = [ordered]@{}
}

$jsonText = $resultObject | ConvertTo-Json -Depth 10
Set-Content -LiteralPath $OutputPath -Value $jsonText -Encoding utf8NoBOM

$fileBytes = [IO.File]::ReadAllBytes($OutputPath)
$finalSha256 = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($fileBytes)).Replace('-', '').ToLowerInvariant()
Set-Content -LiteralPath "$OutputPath.sha256" -Value "$finalSha256  $([IO.Path]::GetFileName($OutputPath))" -Encoding utf8NoBOM
Write-Host "Clean-host certification finished with status: $laneStatus (disposition: $disposition)"
Write-Host "Artifact written: $OutputPath"

return [pscustomobject]$resultObject
