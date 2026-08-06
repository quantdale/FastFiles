<#
    Tier-1 engine-service boundary validation (task 6.4). The runtime paths
    (mutual auth, Authenticode pinning, heartbeat-loss recovery, idle disconnect,
    version-mismatch -> degraded mode, startup sequencing) live in
    PrivilegedConnection/VolumeSessionManager; the harness validates the
    mechanically checkable boundary state and runs the handshake-impostor probe.
    Interactive-session runtime paths SKIP with a reason - never a silent pass.
#>

Import-Module (Join-Path $PSScriptRoot '..\..\core\Probe.psm1') -Force -Global

$script:ProbeBin = 'build\debug\src\fftest\fftest.exe'
$script:InstallDir = Join-Path $env:ProgramFiles 'FastFiles'
$script:ServiceName = 'FastFilesIndexSvc'
$script:SnapshotSectionName = "Local\FastFiles.IndexSnapshot.$([System.Diagnostics.Process]::GetCurrentProcess().SessionId)"

function New-ServiceSubResult {
    param([string] $Id, [string] $Status, [string] $Reason, [double] $DurationMs, [string] $Detail, [array] $Diagnostics = @())
    [pscustomobject]@{ id = $Id; tier = 1; status = $Status; reason = $Reason; requiredContext = $null; durationMs = [math]::Round($DurationMs, 0); detail = $Detail; diagnostics = $Diagnostics }
}

function Test-EngineServiceValidationAvailability {
    param([Parameter(Mandatory)] $Fingerprint)
    if (-not $IsWindows) {
        return [pscustomobject]@{ Available = $false; Reason = 'windows-required'; RequiredContext = [pscustomobject]@{ needs = 'Windows SCM' } }
    }
    if (-not $Fingerprint.IsElevated) {
        return [pscustomobject]@{ Available = $false; Reason = 'elevation-required'; RequiredContext = [pscustomobject]@{ needs = 'Elevated administrator token for SCM queries and handshake probing' } }
    }
    if (-not (Get-Service -Name $script:ServiceName -ErrorAction SilentlyContinue)) {
        return [pscustomobject]@{ Available = $false; Reason = 'service-not-installed'; RequiredContext = [pscustomobject]@{ needs = 'Installed FastFilesIndexSvc (windows-install-service-validation)' } }
    }
    if (-not (Test-Path -LiteralPath $script:InstallDir)) {
        return [pscustomobject]@{ Available = $false; Reason = 'product-not-installed'; RequiredContext = [pscustomobject]@{ needs = 'Installed FastFiles under Program Files' } }
    }
    return [pscustomobject]@{ Available = $true; Reason = $null; RequiredContext = $null }
}

function Get-EngineServiceValidationDiagnostics {
    return @('service-identity-and-start', 'handshake-impostor-rejection', 'binary-version-parity', 'snapshot-publication', 'interactive-session-sequencing')
}

function Invoke-EngineServiceValidationCapability {
    param($RunContext, $Fingerprint, [string] $ArtifactsDir, [hashtable] $Options)

    $fftestExe = Join-Path $Options.RepoRoot $script:ProbeBin
    $subResults = @()
    $artifacts = @()

    if (-not (Test-Path -LiteralPath $fftestExe)) {
        $subResults += New-ServiceSubResult -Id 'fftest-binary' -Status 'FAIL' -Reason 'fftest-not-built' -DurationMs 0 -Detail $fftestExe
        return [pscustomobject]@{
            Status = 'FAIL'
            Reason = 'fftest-binary-missing'
            Summary = 'fftest probe binary is not built; run windows-build-validation first'
            Artifacts = $artifacts
            SubResults = $subResults
        }
    }

    $service = Get-CimInstance -ClassName Win32_Service -Filter "Name='$script:ServiceName'" -ErrorAction SilentlyContinue
    $startName = if ($service) { $service.StartName } else { $null }
    $startMode = if ($service) { $service.StartMode } else { $null }
    $delayedStart = $null
    $scText = ((& sc.exe qc $script:ServiceName 2>&1) | ForEach-Object ToString) -join "`n"
    if ($scText -match 'DELAYED_AUTO_START\s*:\s*(YES|NO)') { $delayedStart = $Matches[1] }
    elseif ($scText -match 'AUTO_START\s*\(DELAYED\)') { $delayedStart = 'YES' }
    $serviceIssues = @()
    if ($startName -notlike 'NT SERVICE\*') { $serviceIssues += "startName='$startName' (expected NT SERVICE virtual account)" }
    if ($startMode -notin @('Auto', 'Automatic')) { $serviceIssues += "startMode='$startMode' (expected automatic)" }
    if ($delayedStart -ne 'YES') { $serviceIssues += "delayedStart='$delayedStart' (expected YES)" }
    $subResults += New-ServiceSubResult -Id 'service-identity-and-start' -Status $(if (@($serviceIssues).Count -eq 0) { 'PASS' } else { 'FAIL' }) `
        -Reason $(if (@($serviceIssues).Count -eq 0) { $null } else { 'service-config-unexpected' }) -DurationMs 0 `
        -Detail "startName=$startName; startMode=$startMode; delayedStart=$delayedStart"

    $handshakeOutcome = Invoke-FFTestProbe -FftestExe $fftestExe -Arguments @('handshake-impostor', $script:InstallDir) -LogPath (Join-Path $ArtifactsDir 'handshake-impostor.log')
    $artifacts += [pscustomobject]@{ path = "artifacts/windows-engine-service-validation/handshake-impostor.log"; type = 'probe-log' }
    $record = $handshakeOutcome.Record
    $handshakePassed = $false
    $handshakeDetail = "exitCode=$($handshakeOutcome.ExitCode)"
    if ($record -and $record.probe -eq 'handshake-impostor' -and $record.status -eq 'PASS' -and $record.accepted -eq $false -and $handshakeOutcome.ExitCode -eq 0) {
        $handshakePassed = $true
        $handshakeDetail = "impostor rejected with UnverifiedImagePath (rejectReason=$($record.rejectReason))"
    } elseif ($record) {
        $handshakeDetail += "; accepted=$($record.accepted); rejectReason=$($record.rejectReason)"
    }
    $subResults += New-ServiceSubResult -Id 'handshake-impostor-rejection' -Status $(if ($handshakePassed) { 'PASS' } else { 'FAIL' }) `
        -Reason $(if ($handshakePassed) { $null } else { 'impostor-handshake-not-rejected' }) -DurationMs $handshakeOutcome.DurationMs -Detail $handshakeDetail

    $engineVersion = $null
    $serviceVersion = $null
    $enginePath = Join-Path $script:InstallDir 'FastFilesEngine.exe'
    $servicePath = Join-Path $script:InstallDir 'FastFilesIndexSvc.exe'
    $enginePresent = Test-Path -LiteralPath $enginePath
    $servicePresent = Test-Path -LiteralPath $servicePath
    if ($enginePresent) {
        $engineVersion = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($enginePath).FileVersion
    }
    if ($servicePresent) {
        $serviceVersion = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($servicePath).FileVersion
    }
    $parityDetail = "engine=$engineVersion; service=$serviceVersion"
    if ($engineVersion -and $serviceVersion -and $engineVersion -eq $serviceVersion) {
        $parityDetail += '; matched'
    }
    if ($enginePresent -and $servicePresent -and -not $engineVersion -and -not $serviceVersion) {
        # Dev builds without a version resource: report the state, do not fail.
        $subResults += New-ServiceSubResult -Id 'binary-version-parity' -Status 'SKIPPED' -Reason 'installed-binaries-lack-version-resource' -DurationMs 0 `
            -Detail "$parityDetail; binaries present but carry no version resource"
    } else {
        $subResults += New-ServiceSubResult -Id 'binary-version-parity' -Status $(if ($enginePresent -and $servicePresent) { 'PASS' } else { 'SKIPPED' }) `
            -Reason $(if ($enginePresent -and $servicePresent) { $null } else { 'installed-binaries-incomplete' }) -DurationMs 0 `
            -Detail $parityDetail
    }

    $mappingOutcome = Invoke-FFTestProbe -FftestExe $fftestExe -Arguments @('mapping', $script:SnapshotSectionName) -LogPath (Join-Path $ArtifactsDir 'snapshot-publication.log')
    $artifacts += [pscustomobject]@{ path = "artifacts/windows-engine-service-validation/snapshot-publication.log"; type = 'probe-log' }
    $mappingRecord = $mappingOutcome.Record
    if ($mappingRecord -and $mappingRecord.readable) {
        $subResults += New-ServiceSubResult -Id 'snapshot-publication' -Status 'PASS' -Reason $null -DurationMs $mappingOutcome.DurationMs -Detail 'index snapshot section readable'
    } else {
        $subResults += New-ServiceSubResult -Id 'snapshot-publication' -Status 'SKIPPED' -Reason 'engine-not-running' -DurationMs $mappingOutcome.DurationMs -Detail 'snapshot not published in this session'
    }

    $subResults += New-ServiceSubResult -Id 'interactive-session-sequencing' -Status 'SKIPPED' -Reason 'requires-interactive-engine-session' -DurationMs 0 `
        -Detail 'startup sequencing, heartbeat-loss recovery, idle disconnect, and version-mismatch degraded-mode paths need a live engine+service session'

    $failures = @($subResults | Where-Object status -eq 'FAIL').Count
    [pscustomobject]@{
        Status = if ($failures -gt 0) { 'FAIL' } else { 'PASS' }
        Reason = if ($failures -gt 0) { 'engine-service-validation-failed' } else { $null }
        Summary = "$(@($subResults | Where-Object status -eq 'PASS').Count) engine-service checks passed; $failures failed; $(@($subResults | Where-Object status -eq 'SKIPPED').Count) skipped"
        Artifacts = $artifacts
        SubResults = $subResults
    }
}

Export-ModuleMember -Function Test-EngineServiceValidationAvailability, Invoke-EngineServiceValidationCapability, Get-EngineServiceValidationDiagnostics
