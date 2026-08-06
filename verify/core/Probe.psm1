<#
    Shared fftest probe + SDDL-check helpers (deduplicated from the Tier-1
    capability modules: windows-privilege-validation, windows-object-security-
    validation, windows-ipc-validation, windows-engine-service-validation).

    Invoke-FFTestProbe was byte-identical in all four modules. The SDDL helpers
    existed in windows-object-security-validation and windows-ipc-validation with
    identical logic; the object-security copy additionally returns ResultCode
    (PASS/FAIL) alongside Valid/Problems. The shared versions below preserve that
    superset so both callers keep their behavior.
#>

function Invoke-FFTestProbe {
    <# Runs the fftest probe binary with the given arguments and parses its first
       JSON line (the probe record) from stdout. Returns the exit code, parsed
       record, elapsed duration, and raw output, and persists the log. #>
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

Export-ModuleMember -Function Invoke-FFTestProbe, Get-SddlClientSidSuffix, Test-SddlIsHardened
