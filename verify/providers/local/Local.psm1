<#
    Local Environment Provider (task 2.2).

    Tier 0 executes in the current PowerShell session. Tier 1 must execute from an
    elevated process; verify.ps1 can relaunch itself with -Elevate, which presents a
    one-time UAC approval prompt before any privileged provider operation begins.
    Capabilities register every system mutation as a teardown action. Cleanup runs
    those actions in reverse registration order and writes its outcome to the run
    tree, even when a capability or provider operation fails.
#>

function New-LocalProviderState {
    [CmdletBinding()]
    param()

    [pscustomobject]@{
        TeardownActions = [System.Collections.Generic.List[object]]::new()
        MutatedSystemState = $false
        CleanupCompleted = $false
        CleanupResult = $null
    }
}

function Register-LocalProviderTeardown {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [pscustomobject] $ProviderContext,
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [scriptblock] $Action
    )

    if (-not $ProviderContext.State) {
        throw 'Local provider state has not been provisioned.'
    }
    $ProviderContext.State.TeardownActions.Add([pscustomobject]@{ Name = $Name; Action = $Action })
    $ProviderContext.State.MutatedSystemState = $true
}

function Invoke-LocalProviderProvision {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [pscustomobject] $ProviderContext)

    $ProviderContext.State = New-LocalProviderState
    [pscustomobject]@{
        Ready = $true
        Mode = if ($ProviderContext.Fingerprint.IsElevated) { 'elevated-current-session' } else { 'current-session' }
        RequiredTier = $ProviderContext.RequiredTier
    }
}

function Invoke-LocalProviderActivate {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [pscustomobject] $ProviderContext)

    [pscustomobject]@{
        TargetIdentity = $ProviderContext.Fingerprint.TargetIdentity
        IsElevated = $ProviderContext.Fingerprint.IsElevated
        SessionId = $ProviderContext.Fingerprint.SessionId
        SessionKind = $ProviderContext.Fingerprint.SessionKind
    }
}

function Invoke-LocalProviderCollectLogs {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [pscustomobject] $ProviderContext)

    # Provider-specific log collection is intentionally empty for now. Tier-1
    # installer/service capabilities add their own declared logs in task 5.1.
    return @()
}

function Invoke-LocalProviderCleanup {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [pscustomobject] $ProviderContext)

    if ($ProviderContext.State.CleanupCompleted) {
        return $ProviderContext.State.CleanupResult
    }

    $entries = @()
    $actions = @($ProviderContext.State.TeardownActions)
    [array]::Reverse($actions)
    foreach ($teardown in $actions) {
        try {
            & $teardown.Action
            $entries += [pscustomobject]@{ name = $teardown.Name; status = 'PASS'; error = $null }
        } catch {
            $entries += [pscustomobject]@{ name = $teardown.Name; status = 'FAIL'; error = $_.Exception.Message }
        }
    }

    $cleanupResult = [pscustomobject]@{
        provider = 'local'
        completedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
        mutatedSystemState = $ProviderContext.State.MutatedSystemState
        actions = $entries
        succeeded = (@($entries | Where-Object { $_.status -eq 'FAIL' }).Count -eq 0)
    }
    $cleanupResult | ConvertTo-Json -Depth 8 | Set-Content -Path (Join-Path $ProviderContext.RunContext.RunPath 'provider-cleanup.json') -Encoding utf8
    $ProviderContext.State.TeardownActions.Clear()
    $ProviderContext.State.CleanupResult = $cleanupResult
    $ProviderContext.State.CleanupCompleted = $true
    return $cleanupResult
}

Export-ModuleMember -Function Invoke-LocalProviderProvision, Invoke-LocalProviderActivate, Invoke-LocalProviderCollectLogs, Invoke-LocalProviderCleanup, Register-LocalProviderTeardown
