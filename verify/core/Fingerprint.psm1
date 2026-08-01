<#
    Execution-context fingerprint (task 1.4 / D5): captured once, before any capability
    runs, and persisted as manifest.json. Tier gating (Tier-Gate.psm1) compares each
    capability's declared tier requirement against this fingerprint.
#>

Import-Module (Join-Path $PSScriptRoot 'Toolchain.psm1') -Force -Global

function Test-IsElevated {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltinRole]::Administrator)
}

function Get-SessionInfo {
    $sessionId = [System.Diagnostics.Process]::GetCurrentProcess().SessionId
    $kind = 'unknown'
    try {
        $queryOutput = & query session $sessionId 2>$null
        if ($LASTEXITCODE -eq 0 -and $queryOutput) {
            $line = ($queryOutput | Select-Object -Skip 1 | Select-Object -First 1)
            if ($line -match '(?i)rdp-tcp') { $kind = 'rdp' }
            elseif ($line -match '(?i)console') { $kind = 'console' }
            elseif ($line -match '(?i)services') { $kind = 'services' }
        }
    } catch {
        $kind = 'unknown'
    }
    if ($sessionId -eq 0) { $kind = 'session-0' }
    [pscustomobject]@{ SessionId = $sessionId; SessionKind = $kind }
}

function Get-EnvironmentFingerprint {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $Change,
        [string] $ProviderId = 'local',
        [pscustomobject] $Provider
    )

    $os = Get-CimInstance -ClassName Win32_OperatingSystem
    $sessionInfo = Get-SessionInfo
    $toolchain = Find-VSToolchain

    [pscustomobject]@{
        RunId            = [guid]::NewGuid().ToString('n')
        Change           = $Change
        StartedAtUtc     = (Get-Date).ToUniversalTime().ToString('o')
        OsBuild          = "$($os.Caption) $($os.Version) (build $($os.BuildNumber))"
        IsElevated       = Test-IsElevated
        SessionId        = $sessionInfo.SessionId
        SessionKind      = $sessionInfo.SessionKind
        ProviderId       = $ProviderId
        Provider         = $Provider
        TargetIdentity   = if ($Provider -and $Provider.TargetIdentity) { $Provider.TargetIdentity } else { "$($env:COMPUTERNAME):$ProviderId" }
        Toolchain        = $toolchain
        CapabilityLoadDiagnostics = @()
    }
}

Export-ModuleMember -Function Get-EnvironmentFingerprint, Test-IsElevated
