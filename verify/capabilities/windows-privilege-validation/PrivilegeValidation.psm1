<#
    Tier-1 privilege/token/integrity validation (task 6.2). Runs the fftest
    `privilege` and `token` probes, parses their JSON records, and validates the
    expectations that encode the product's SeBackup-only posture. Gated to an
    elevated context: the backup-privilege probe opens a raw volume, which is not
    reachable from an unprivileged token.
#>

Import-Module (Join-Path $PSScriptRoot '..\..\core\Probe.psm1') -Force -Global

$script:ProbeBin = 'build\debug\src\fftest\fftest.exe'

function New-PrivilegeSubResult {
    param([string] $Id, [string] $Status, [string] $Reason, [double] $DurationMs, [string] $Detail, [array] $Diagnostics = @())
    [pscustomobject]@{ id = $Id; tier = 1; status = $Status; reason = $Reason; requiredContext = $null; durationMs = [math]::Round($DurationMs, 0); detail = $Detail; diagnostics = $Diagnostics }
}

function Test-PrivilegeValidationAvailability {
    param([Parameter(Mandatory)] $Fingerprint)
    if (-not $IsWindows) {
        return [pscustomobject]@{ Available = $false; Reason = 'windows-required'; RequiredContext = [pscustomobject]@{ needs = 'Windows volume and token APIs' } }
    }
    if (-not $Fingerprint.IsElevated) {
        return [pscustomobject]@{ Available = $false; Reason = 'elevation-required'; RequiredContext = [pscustomobject]@{ needs = 'Elevated administrator token to open a raw volume with backup semantics' } }
    }
    return [pscustomobject]@{ Available = $true; Reason = $null; RequiredContext = $null }
}

function Get-PrivilegeValidationDiagnostics {
    return @('backup-privilege-sufficiency', 'elevated-token-integrity', 'token-privilege-set')
}

function Invoke-PrivilegeValidationCapability {
    param($RunContext, $Fingerprint, [string] $ArtifactsDir, [hashtable] $Options)

    $fftestExe = Join-Path $Options.RepoRoot $script:ProbeBin
    $subResults = @()
    $artifacts = @()

    if (-not (Test-Path -LiteralPath $fftestExe)) {
        $subResults += New-PrivilegeSubResult -Id 'fftest-binary' -Status 'FAIL' -Reason 'fftest-not-built' -DurationMs 0 -Detail $fftestExe
        return [pscustomobject]@{
            Status = 'FAIL'
            Reason = 'fftest-binary-missing'
            Summary = 'fftest probe binary is not built; run windows-build-validation first'
            Artifacts = $artifacts
            SubResults = $subResults
        }
    }

    $privilegeOutcome = Invoke-FFTestProbe -FftestExe $fftestExe -Arguments @('privilege') -LogPath (Join-Path $ArtifactsDir 'privilege-probe.log')
    $artifacts += [pscustomobject]@{ path = "artifacts/windows-privilege-validation/privilege-probe.log"; type = 'probe-log' }
    $record = $privilegeOutcome.Record
    $privilegePassed = $false
    $privilegeDetail = 'exitCode=' + $privilegeOutcome.ExitCode
    if ($record -and $record.probe -eq 'backup-privilege' -and $record.status -eq 'PASS' -and $privilegeOutcome.ExitCode -eq 0) {
        $expectations = @{ privilegeEnabled = $record.privilegeEnabled; volumeFound = $record.volumeFound; volumeOpened = $record.volumeOpened }
        $met = $expectations.Values | Where-Object { $_ -ne $true }
        $privilegePassed = @($met).Count -eq 0
        $privilegeDetail = "driveLetter=$($record.driveLetter); journalQueried=$($record.journalQueried); volumeOpenError=$($record.volumeOpenError); journalQueryError=$($record.journalQueryError)"
        if (-not $privilegePassed) {
            $privilegeDetail += "; unexpected false expectations: $((@($expectations.GetEnumerator() | Where-Object { -not $_.Value }) | ForEach-Object { $_.Key }) -join ',')"
        }
    }
    $subResults += New-PrivilegeSubResult -Id 'backup-privilege-sufficiency' -Status $(if ($privilegePassed) { 'PASS' } else { 'FAIL' }) `
        -Reason $(if ($privilegePassed) { $null } else { 'backup-privilege-probe-failed' }) -DurationMs $privilegeOutcome.DurationMs -Detail $privilegeDetail `
        -Diagnostics @($privilegeOutcome.Output | Where-Object { $_ -match '"status":"FAIL"' })

    $tokenOutcome = Invoke-FFTestProbe -FftestExe $fftestExe -Arguments @('token') -LogPath (Join-Path $ArtifactsDir 'token-probe.log')
    $artifacts += [pscustomobject]@{ path = "artifacts/windows-privilege-validation/token-probe.log"; type = 'probe-log' }
    $record = $tokenOutcome.Record
    if (-not $record -or $record.probe -ne 'token' -or $tokenOutcome.ExitCode -ne 0) {
        $subResults += New-PrivilegeSubResult -Id 'token-probe-record' -Status 'FAIL' -Reason 'token-probe-record-invalid' `
            -DurationMs $tokenOutcome.DurationMs -Detail "exitCode=$($tokenOutcome.ExitCode); probe=$($record.probe)"
    } else {
        $privilegeNames = @($record.privileges | ForEach-Object { $_.name })
        $privilegeSet = ($privilegeNames | Sort-Object) -join ', '
        $tokenDetail = "elevated=$($record.elevated); integrityRid=$($record.integrityRid); privileges=[$privilegeSet]"
        $integrityOk = $record.elevated -and $record.integrityRid -ge 12288
        $subResults += New-PrivilegeSubResult -Id 'elevated-token-integrity' -Status $(if ($integrityOk) { 'PASS' } else { 'FAIL' }) `
            -Reason $(if ($integrityOk) { $null } else { 'token-integrity-expected-high' }) -DurationMs $tokenOutcome.DurationMs -Detail $tokenDetail
        $missingPrivileges = @('SeBackupPrivilege', 'SeRestorePrivilege') | Where-Object { $_ -notin $privilegeNames }
        $subResults += New-PrivilegeSubResult -Id 'token-privilege-set' -Status $(if (@($missingPrivileges).Count -eq 0) { 'PASS' } else { 'FAIL' }) `
            -Reason $(if (@($missingPrivileges).Count -eq 0) { $null } else { 'token-privilege-set-unexpected' }) -DurationMs $tokenOutcome.DurationMs `
            -Detail $(if (@($missingPrivileges).Count -eq 0) { $tokenDetail } else { "missing: $($missingPrivileges -join ', '); $tokenDetail" })
    }

    $failures = @($subResults | Where-Object status -eq 'FAIL').Count
    [pscustomobject]@{
        Status = if ($failures -gt 0) { 'FAIL' } else { 'PASS' }
        Reason = if ($failures -gt 0) { 'privilege-validation-failed' } else { $null }
        Summary = "$(@($subResults | Where-Object status -eq 'PASS').Count) privilege checks passed; $failures failed"
        Artifacts = $artifacts
        SubResults = $subResults
    }
}

Export-ModuleMember -Function Test-PrivilegeValidationAvailability, Invoke-PrivilegeValidationCapability, Get-PrivilegeValidationDiagnostics
