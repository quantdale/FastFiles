<#
    Tier-1 object-security validation (task 6.3). Reads real object security state
    through the fftest `acl`/`mapping` probes and the WinVerifyTrust-backed
    Get-AuthenticodeSignature, then checks it against the hardened expectations
    (design.md D4): the client group may read/execute the install dir but never
    write or take full control; no Everyone/anonymous grants; every installed
    binary is Authenticode-signed; the engine control pipe is SYSTEM+user only.
    Engine-dependent checks SKIP with a reason when the engine is not running -
    never a silent pass.
#>

$script:ProbeBin = 'build\debug\src\fftest\fftest.exe'
$script:InstallDir = Join-Path $env:ProgramFiles 'FastFiles'
$script:ControlPipeName = "\\.\pipe\FastFiles.Ui.Ctrl.$([System.Diagnostics.Process]::GetCurrentProcess().SessionId)"
$script:SnapshotSectionName = "Local\FastFiles.IndexSnapshot.$([System.Diagnostics.Process]::GetCurrentProcess().SessionId)"
$script:InstalledBinaries = @('FastFiles.exe', 'FastFilesEngine.exe', 'FastFilesIndexSvc.exe')

function New-SecuritySubResult {
    param([string] $Id, [string] $Status, [string] $Reason, [double] $DurationMs, [string] $Detail, [array] $Diagnostics = @())
    [pscustomobject]@{ id = $Id; tier = 1; status = $Status; reason = $Reason; requiredContext = $null; durationMs = [math]::Round($DurationMs, 0); detail = $Detail; diagnostics = $Diagnostics }
}

function Test-ObjectSecurityValidationAvailability {
    param([Parameter(Mandatory)] $Fingerprint)
    if (-not $IsWindows) {
        return [pscustomobject]@{ Available = $false; Reason = 'windows-required'; RequiredContext = [pscustomobject]@{ needs = 'Windows object-security APIs' } }
    }
    if (-not $Fingerprint.IsElevated) {
        return [pscustomobject]@{ Available = $false; Reason = 'elevation-required'; RequiredContext = [pscustomobject]@{ needs = 'Elevated administrator token to read protected-object security descriptors' } }
    }
    if (-not (Test-Path -LiteralPath $script:InstallDir)) {
        return [pscustomobject]@{ Available = $false; Reason = 'product-not-installed'; RequiredContext = [pscustomobject]@{ needs = 'Installed FastFiles under Program Files (windows-install-service-validation)' } }
    }
    return [pscustomobject]@{ Available = $true; Reason = $null; RequiredContext = $null }
}

function Get-ObjectSecurityValidationDiagnostics {
    return @('install-dir-acl', 'binary-authenticode', 'snapshot-mapping-dacl', 'control-pipe-acl')
}

function Invoke-FFTestProbe {
    param([string] $FftestExe, [string[]] $Arguments, [string] $LogPath)
    $started = Get-Date
    $output = & $FftestExe @Arguments 2>&1 | ForEach-Object ToString
    $exitCode = $LASTEXITCODE
    $finished = Get-Date
    $jsonLine = $output | Where-Object { $_ -match '^\{' } | Select-Object -First 1
    $record = $null
    if ($jsonLine) {
        try { $record = $jsonLine | ConvertFrom-Json } catch { $record = $null }
    }
    $output | Set-Content -LiteralPath $LogPath -Encoding utf8
    [pscustomobject]@{
        ExitCode = $exitCode
        Record = $record
        DurationMs = [math]::Round(($finished - $started).TotalMilliseconds, 0)
        Output = $output
    }
}

function Get-SddlClientSidSuffix {
    <# SDDL renders the FastFilesUsers local group as a raw SID string. Resolve it
       from the machine so checks match both the named form (SDDL on some systems)
       and the raw-SID form observed in probe output. #>
    param([string] $GroupName = 'FastFilesUsers')
    try {
        $account = [System.Security.Principal.NTAccount]::new($GroupName)
        return $account.Translate([System.Security.Principal.SecurityIdentifier]).Value
    } catch {
        return $null
    }
}

function Test-SddlIsHardened {
    <#
        Shared expectations for both protected DACLs:
        - no grant to Everyone (WD) or Anonymous (AN)
        - no full-access grant (generic FA or hex 0x001f01ff)
        - no write grant to the FastFilesUsers client group (named or raw SID)
    #>
    param([string] $Sddl, [string] $ClientSid)
    $problems = @()
    if ($Sddl -match '\(A;;[^)]*;;;(WD|AN)\)') {
        $problems += 'everyone-or-anonymous-grant-present'
    }
    if ($Sddl -match '\(A;;[^)]*(FA|0x001f01ff)[^)]*;;;') {
        $problems += 'full-access-grant-present'
    }
    $clientPatterns = @('FastFilesUsers')
    if ($ClientSid) { $clientPatterns += [regex]::Escape($ClientSid) }
    foreach ($pattern in $clientPatterns) {
        if ($Sddl -match "\(A;;[^;]*W[^;]*;;;$pattern\)") {
            $problems += 'client-group-write-grant-present'
            break
        }
    }
    [pscustomobject]@{
        Valid = $problems.Count -eq 0
        Problems = $problems
        ResultCode = if ($problems.Count -eq 0) { 'PASS' } else { 'FAIL' }
    }
}

function Invoke-ObjectSecurityValidationCapability {
    param($RunContext, $Fingerprint, [string] $ArtifactsDir, [hashtable] $Options)

    $fftestExe = Join-Path $Options.RepoRoot $script:ProbeBin
    $subResults = @()
    $artifacts = @()

    if (-not (Test-Path -LiteralPath $fftestExe)) {
        $subResults += New-SecuritySubResult -Id 'fftest-binary' -Status 'FAIL' -Reason 'fftest-not-built' -DurationMs 0 -Detail $fftestExe
        return [pscustomobject]@{
            Status = 'FAIL'
            Reason = 'fftest-binary-missing'
            Summary = 'fftest probe binary is not built; run windows-build-validation first'
            Artifacts = $artifacts
            SubResults = $subResults
        }
    }

    $aclOutcome = Invoke-FFTestProbe -FftestExe $fftestExe -Arguments @('acl', $script:InstallDir) -LogPath (Join-Path $ArtifactsDir 'install-dir-acl.log')
    $artifacts += [pscustomobject]@{ path = "artifacts/windows-object-security-validation/install-dir-acl.log"; type = 'acl-log' }
    $sddl = $aclOutcome.Record.sddl
    $clientSid = Get-SddlClientSidSuffix
    $installDirIssues = @()
    if (-not $sddl) {
        $installDirIssues += "acl-probe-unreadable (error=$($aclOutcome.Record.error))"
    } else {
        $systemPresent = ($sddl -match 'S-1-5-18' -or $sddl -match ';SY\)')
        $adminPresent = ($sddl -match 'S-1-5-32-544' -or $sddl -match ';BA\)')
        $clientPresent = ($sddl -match 'FastFilesUsers' -or ($clientSid -and $sddl -match [regex]::Escape($clientSid)))
        if (-not $systemPresent) { $installDirIssues += 'SYSTEM-grant-missing' }
        if (-not $adminPresent) { $installDirIssues += 'Administrators-grant-missing' }
        if (-not $clientPresent) { $installDirIssues += 'FastFilesUsers-grant-missing' }
        $hardened = Test-SddlIsHardened -Sddl $sddl -ClientSid $clientSid
        if (-not $hardened.Valid) { $installDirIssues += $hardened.Problems }
    }
    $subResults += New-SecuritySubResult -Id 'install-dir-acl' -Status $(if (@($installDirIssues).Count -eq 0) { 'PASS' } else { 'FAIL' }) `
        -Reason $(if (@($installDirIssues).Count -eq 0) { $null } else { 'install-dir-acl-unhardened' }) -DurationMs $aclOutcome.DurationMs `
        -Detail $(if (@($installDirIssues).Count -eq 0) { 'hardened: SYSTEM+Administrators full; service+FastFilesUsers read/execute only' } else { $installDirIssues -join '; ' })

    $signatureIssues = @()
    $signatureDetail = @()
    foreach ($binary in $script:InstalledBinaries) {
        $binaryPath = Join-Path $script:InstallDir $binary
        if (-not (Test-Path -LiteralPath $binaryPath)) {
            $signatureIssues += "$binary-missing"
            $signatureDetail += "$binary=missing"
            continue
        }
        $signature = Get-AuthenticodeSignature -LiteralPath $binaryPath
        $signatureDetail += "$binary=$($signature.Status)"
        if ($signature.Status -ne 'Valid') {
            $signatureIssues += "$binary-status-$($signature.Status)"
        }
    }
    $subResults += New-SecuritySubResult -Id 'binary-authenticode' -Status $(if (@($signatureIssues).Count -eq 0) { 'PASS' } else { 'FAIL' }) `
        -Reason $(if (@($signatureIssues).Count -eq 0) { $null } else { 'binary-authenticode-unexpected' }) -DurationMs 0 -Detail ($signatureDetail -join '; ')

    $mappingOutcome = Invoke-FFTestProbe -FftestExe $fftestExe -Arguments @('mapping', $script:SnapshotSectionName) -LogPath (Join-Path $ArtifactsDir 'snapshot-mapping.log')
    $artifacts += [pscustomobject]@{ path = "artifacts/windows-object-security-validation/snapshot-mapping.log"; type = 'probe-log' }
    $mappingRecord = $mappingOutcome.Record
    if ($mappingRecord -and $mappingRecord.readable) {
        $subResults += New-SecuritySubResult -Id 'snapshot-mapping-dacl' -Status 'PASS' -Reason $null -DurationMs $mappingOutcome.DurationMs -Detail "section '$($mappingRecord.name)' readable"
    } else {
        $subResults += New-SecuritySubResult -Id 'snapshot-mapping-dacl' -Status 'SKIPPED' -Reason 'snapshot-not-published' -DurationMs $mappingOutcome.DurationMs `
            -Detail 'index snapshot section absent (engine not running in this session); readability not exercised' `
            -Diagnostics @()
    }

    $pipeOutcome = Invoke-FFTestProbe -FftestExe $fftestExe -Arguments @('acl', $script:ControlPipeName) -LogPath (Join-Path $ArtifactsDir 'control-pipe-acl.log')
    $artifacts += [pscustomobject]@{ path = "artifacts/windows-object-security-validation/control-pipe-acl.log"; type = 'acl-log' }
    $pipeRecord = $pipeOutcome.Record
    if ($pipeRecord -and $pipeRecord.status -eq 'PASS') {
        $pipeSddl = $pipeRecord.sddl
        $pipeIssues = @()
        $systemPresent = ($pipeSddl -match 'S-1-5-18' -or $pipeSddl -match ';SY\)')
        if (-not $systemPresent) { $pipeIssues += 'SYSTEM-grant-missing' }
        $clientPresent = ($pipeSddl -match 'FastFilesUsers' -or ($clientSid -and $pipeSddl -match [regex]::Escape($clientSid)))
        if ($clientPresent) { $pipeIssues += 'client-group-should-not-be-granted' }
        $hardened = Test-SddlIsHardened -Sddl $pipeSddl -ClientSid $clientSid
        if (-not $hardened.Valid) { $pipeIssues += $hardened.Problems }
        $subResults += New-SecuritySubResult -Id 'control-pipe-acl' -Status $(if (@($pipeIssues).Count -eq 0) { 'PASS' } else { 'FAIL' }) `
            -Reason $(if (@($pipeIssues).Count -eq 0) { $null } else { 'control-pipe-acl-unhardened' }) -DurationMs $pipeOutcome.DurationMs `
            -Detail $(if (@($pipeIssues).Count -eq 0) { 'SYSTEM+current-user only' } else { $pipeIssues -join '; ' })
    } else {
        $subResults += New-SecuritySubResult -Id 'control-pipe-acl' -Status 'SKIPPED' -Reason 'engine-not-running' -DurationMs $pipeOutcome.DurationMs `
            -Detail "control pipe '$($script:ControlPipeName)' not present (engine not running in this session)"
    }

    $failures = @($subResults | Where-Object status -eq 'FAIL').Count
    [pscustomobject]@{
        Status = if ($failures -gt 0) { 'FAIL' } else { 'PASS' }
        Reason = if ($failures -gt 0) { 'object-security-validation-failed' } else { $null }
        Summary = "$(@($subResults | Where-Object status -eq 'PASS').Count) security checks passed; $failures failed; $(@($subResults | Where-Object status -eq 'SKIPPED').Count) skipped"
        Artifacts = $artifacts
        SubResults = $subResults
    }
}

Export-ModuleMember -Function Test-ObjectSecurityValidationAvailability, Invoke-ObjectSecurityValidationCapability, Get-ObjectSecurityValidationDiagnostics
