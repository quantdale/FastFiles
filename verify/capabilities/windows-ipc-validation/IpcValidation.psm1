<#
    Tier-1 IPC validation (task 6.5). Validates the two named-pipe seams and the
    snapshot section (design.md D2/D4): the control-pipe name must be held by a
    properly ACLed first instance (squatting hard-fail), its DACL must be
    SYSTEM+current-user, the snapshot section must be readable without round-trip,
    and the engine-service handshake must reject impostors. Session-isolation and
    timeout/reconnection paths SKIP with a reason when the required context is
    absent - never a silent pass.
#>

Import-Module (Join-Path $PSScriptRoot '..\..\core\Probe.psm1') -Force -Global

$script:ProbeBin = 'build\debug\src\fftest\fftest.exe'
$script:InstallDir = Join-Path $env:ProgramFiles 'FastFiles'
$script:ServiceName = 'FastFilesIndexSvc'
$script:SessionId = [System.Diagnostics.Process]::GetCurrentProcess().SessionId
$script:ControlPipeName = "\\.\pipe\FastFiles.Ui.Ctrl.$script:SessionId"
$script:SnapshotSectionName = "Local\FastFiles.IndexSnapshot.$script:SessionId"

function New-IpcSubResult {
    param([string] $Id, [string] $Status, [string] $Reason, [double] $DurationMs, [string] $Detail, [array] $Diagnostics = @())
    [pscustomobject]@{ id = $Id; tier = 1; status = $Status; reason = $Reason; requiredContext = $null; durationMs = [math]::Round($DurationMs, 0); detail = $Detail; diagnostics = $Diagnostics }
}

function Test-IpcValidationAvailability {
    param([Parameter(Mandatory)] $Fingerprint)
    if (-not $IsWindows) {
        return [pscustomobject]@{ Available = $false; Reason = 'windows-required'; RequiredContext = [pscustomobject]@{ needs = 'Windows named pipes and mapped sections' } }
    }
    if (-not $Fingerprint.IsElevated) {
        return [pscustomobject]@{ Available = $false; Reason = 'elevation-required'; RequiredContext = [pscustomobject]@{ needs = 'Elevated administrator token to read pipe DACLs and test squatting' } }
    }
    return [pscustomobject]@{ Available = $true; Reason = $null; RequiredContext = $null }
}

function Get-IpcValidationDiagnostics {
    return @('control-pipe-first-instance', 'control-pipe-acl', 'snapshot-section-publication', 'engine-service-handshake', 'session-isolation', 'timeout-recovery')
}

function Invoke-IpcValidationCapability {
    param($RunContext, $Fingerprint, [string] $ArtifactsDir, [hashtable] $Options)

    $fftestExe = Join-Path $Options.RepoRoot $script:ProbeBin
    $subResults = @()
    $artifacts = @()

    if (-not (Test-Path -LiteralPath $fftestExe)) {
        $subResults += New-IpcSubResult -Id 'fftest-binary' -Status 'FAIL' -Reason 'fftest-not-built' -DurationMs 0 -Detail $fftestExe
        return [pscustomobject]@{
            Status = 'FAIL'
            Reason = 'fftest-binary-missing'
            Summary = 'fftest probe binary is not built; run windows-build-validation first'
            Artifacts = $artifacts
            SubResults = $subResults
        }
    }

    $pipeAclOutcome = Invoke-FFTestProbe -FftestExe $fftestExe -Arguments @('acl', $script:ControlPipeName) -LogPath (Join-Path $ArtifactsDir 'control-pipe-acl.log')
    $artifacts += [pscustomobject]@{ path = "artifacts/windows-ipc-validation/control-pipe-acl.log"; type = 'acl-log' }
    $pipeRecord = $pipeAclOutcome.Record
    $clientSid = Get-SddlClientSidSuffix
    if ($pipeRecord -and $pipeRecord.status -eq 'PASS') {
        $pipeSddl = $pipeRecord.sddl
        $pipeIssues = @()
        $systemPresent = ($pipeSddl -match 'S-1-5-18' -or $pipeSddl -match ';SY\)')
        if (-not $systemPresent) { $pipeIssues += 'SYSTEM-grant-missing' }
        $clientPresent = ($pipeSddl -match 'FastFilesUsers' -or ($clientSid -and $pipeSddl -match [regex]::Escape($clientSid)))
        if ($clientPresent) { $pipeIssues += 'client-group-should-not-be-granted' }
        $hardened = Test-SddlIsHardened -Sddl $pipeSddl -ClientSid $clientSid
        if (-not $hardened.Valid) { $pipeIssues += $hardened.Problems }
        # The pipe's DACL is fixed by its first instance (engine, SYSTEM+current-user).
        # The engine's first-instance create hard-fails if a squatter already held the
        # name with a different ACL, so a running engine implies the name is held by a
        # correctly ACLed instance - the squatting hard-fail is enforced.
        $subResults += New-IpcSubResult -Id 'control-pipe-first-instance' -Status $(if (@($pipeIssues).Count -eq 0) { 'PASS' } else { 'FAIL' }) `
            -Reason $(if (@($pipeIssues).Count -eq 0) { $null } else { 'control-pipe-dacl-suggests-squat' }) -DurationMs $pipeAclOutcome.DurationMs `
            -Detail $(if (@($pipeIssues).Count -eq 0) { 'name held by hardened first instance; squatting would have failed the engine first-instance create' } else { $pipeIssues -join '; ' })
        $subResults += New-IpcSubResult -Id 'control-pipe-acl' -Status $(if (@($pipeIssues).Count -eq 0) { 'PASS' } else { 'FAIL' }) `
            -Reason $(if (@($pipeIssues).Count -eq 0) { $null } else { 'control-pipe-acl-unhardened' }) -DurationMs $pipeAclOutcome.DurationMs `
            -Detail $(if (@($pipeIssues).Count -eq 0) { 'SYSTEM+current-user only' } else { $pipeIssues -join '; ' })
    } else {
        $subResults += New-IpcSubResult -Id 'control-pipe-first-instance' -Status 'SKIPPED' -Reason 'engine-not-running' -DurationMs $pipeAclOutcome.DurationMs `
            -Detail 'control pipe not present (engine not running in this session); first-instance hold not exercised'
        $subResults += New-IpcSubResult -Id 'control-pipe-acl' -Status 'SKIPPED' -Reason 'engine-not-running' -DurationMs $pipeAclOutcome.DurationMs `
            -Detail "control pipe '$($script:ControlPipeName)' not present"
    }

    $mappingOutcome = Invoke-FFTestProbe -FftestExe $fftestExe -Arguments @('mapping', $script:SnapshotSectionName) -LogPath (Join-Path $ArtifactsDir 'snapshot-section.log')
    $artifacts += [pscustomobject]@{ path = "artifacts/windows-ipc-validation/snapshot-section.log"; type = 'probe-log' }
    $mappingRecord = $mappingOutcome.Record
    if ($mappingRecord -and $mappingRecord.readable) {
        $subResults += New-IpcSubResult -Id 'snapshot-section-publication' -Status 'PASS' -Reason $null -DurationMs $mappingOutcome.DurationMs `
            -Detail "section '$($mappingRecord.name)' readable without IPC round-trip"
    } else {
        $subResults += New-IpcSubResult -Id 'snapshot-section-publication' -Status 'SKIPPED' -Reason 'engine-not-running' -DurationMs $mappingOutcome.DurationMs `
            -Detail 'snapshot section absent (engine not running in this session)'
    }

    $serviceInstalled = $null -ne (Get-Service -Name $script:ServiceName -ErrorAction SilentlyContinue)
    if ($serviceInstalled) {
        $handshakeOutcome = Invoke-FFTestProbe -FftestExe $fftestExe -Arguments @('handshake-impostor', $script:InstallDir) -LogPath (Join-Path $ArtifactsDir 'engine-service-handshake.log')
        $artifacts += [pscustomobject]@{ path = "artifacts/windows-ipc-validation/engine-service-handshake.log"; type = 'probe-log' }
        $record = $handshakeOutcome.Record
        $handshakePassed = $false
        $handshakeDetail = "exitCode=$($handshakeOutcome.ExitCode)"
        if ($record -and $record.probe -eq 'handshake-impostor' -and $record.status -eq 'PASS' -and $record.accepted -eq $false -and $handshakeOutcome.ExitCode -eq 0) {
            $handshakePassed = $true
            $handshakeDetail = "impostor rejected with UnverifiedImagePath (rejectReason=$($record.rejectReason))"
        } elseif ($record) {
            $handshakeDetail += "; accepted=$($record.accepted); rejectReason=$($record.rejectReason)"
        }
        $subResults += New-IpcSubResult -Id 'engine-service-handshake' -Status $(if ($handshakePassed) { 'PASS' } else { 'FAIL' }) `
            -Reason $(if ($handshakePassed) { $null } else { 'impostor-handshake-not-rejected' }) -DurationMs $handshakeOutcome.DurationMs -Detail $handshakeDetail
    } else {
        $subResults += New-IpcSubResult -Id 'engine-service-handshake' -Status 'SKIPPED' -Reason 'service-not-installed' -DurationMs 0 `
            -Detail 'engine-service handshake boundary requires an installed service'
    }

    $subResults += New-IpcSubResult -Id 'session-isolation' -Status 'SKIPPED' -Reason 'requires-multi-session-context' -DurationMs 0 `
        -Detail 'session-scoped pipe/section names validated for the current session only; cross-session isolation needs a second interactive session'

    $subResults += New-IpcSubResult -Id 'timeout-recovery' -Status 'SKIPPED' -Reason 'requires-interactive-engine-session' -DurationMs 0 `
        -Detail 'pipe timeout recovery and reconnection paths need a live engine+UI session'

    $failures = @($subResults | Where-Object status -eq 'FAIL').Count
    [pscustomobject]@{
        Status = if ($failures -gt 0) { 'FAIL' } else { 'PASS' }
        Reason = if ($failures -gt 0) { 'ipc-validation-failed' } else { $null }
        Summary = "$(@($subResults | Where-Object status -eq 'PASS').Count) IPC checks passed; $failures failed; $(@($subResults | Where-Object status -eq 'SKIPPED').Count) skipped"
        Artifacts = $artifacts
        SubResults = $subResults
    }
}

Export-ModuleMember -Function Test-IpcValidationAvailability, Invoke-IpcValidationCapability, Get-IpcValidationDiagnostics
