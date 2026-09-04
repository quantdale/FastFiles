<#
    Deterministic fake provider fixture for environment-provider
    certification tests.

    New-ProviderFixtureAdapter returns a scriptblock adapter suitable for
    Invoke-EnvironmentProviderCertification.ps1 -ProviderAdapter. Each
    -Scenario simulates one lifecycle condition deterministically, with no
    wall-clock waits, no host mutation, and no external dependencies.

    Scenarios:
        success                 Full lifecycle passes; provenance verifies.
        preflight-only          Preflight passes but the provider declines
                                every execution step (BLOCKED). Must NOT pass.
        preflight-unavailable   Preflight blocked (no usable guest image).
        provision-failure       PROVISION fails.
        activation-failure      ACTIVATE fails.
        channel-failure         ESTABLISH_SECURE_GUEST_CHANNEL fails.
        transfer-failure        TRANSFER_PAYLOAD fails.
        execution-failure       Guest runs and reports failure (exit != 0).
        collection-failure      COLLECT_STRUCTURED_RESULTS fails.
        malformed-result        Guest result is missing required fields.
        provenance-mismatch     Guest result carries a wrong payload digest.
        timeout                 WAIT_READY reports a (simulated) timeout.
        rollback-failure        RESTORE_ROLLBACK fails after success.
        cleanup-failure         CLEANUP fails; residue remains.
        cancellation            EXECUTE_PAYLOAD reports cancellation.

    PowerShell 5.1-compatible syntax only.
#>

function Get-ProviderFixtureScenarios {
    return @(
        'success',
        'preflight-only',
        'preflight-unavailable',
        'provision-failure',
        'activation-failure',
        'channel-failure',
        'transfer-failure',
        'execution-failure',
        'collection-failure',
        'malformed-result',
        'provenance-mismatch',
        'timeout',
        'rollback-failure',
        'cleanup-failure',
        'cancellation'
    )
}

function Get-FixtureStringSha256Hex {
    param([string] $Text)
    if ($null -eq $Text) { $Text = '' }
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
    $hash = [System.Security.Cryptography.SHA256]::Create().ComputeHash($bytes)
    return ([System.BitConverter]::ToString($hash)).Replace('-', '').ToLowerInvariant()
}

function New-FixtureAdapterResult {
    param([string] $Status, [string] $Reason = $null, [string] $ErrorText = $null)
    return [pscustomobject]@{
        Status = $Status
        Reason = $Reason
        Error = $ErrorText
        GuestIdentity = $null
        Artifacts = @()
        Actions = @()
        Data = $null
    }
}

function New-FixtureGuestResult {
    param([pscustomobject] $Context, [string] $Scenario)
    $guestId = "fixture-host:hyperv:FastFiles-Fixture-$Scenario"
    $artifactFacts = [ordered]@{
        probe = 'fixture-certification-probe'
        scenario = $Scenario
        provider = 'hyperv'
        commit = $Context.HostCommit
        payloadDigest = $Context.PayloadDigest
        tests = @(@{ name = 'fixture-probe'; result = 'pass' })
    }
    $artifactJson = ($artifactFacts | ConvertTo-Json -Depth 6 -Compress)
    $artifactHash = Get-FixtureStringSha256Hex -Text $artifactJson
    return [pscustomobject]@{
        schemaVersion = 1
        provider = 'hyperv'
        vmIdentity = $guestId
        guestOs = 'Microsoft Windows 11 Pro (fixture guest)'
        guestTimestampUtc = ([DateTime]::UtcNow.ToString('o'))
        command = "fixture-probe.exe --certify --scenario $Scenario"
        executableHash = (Get-FixtureStringSha256Hex -Text "fixture-executable:hyperv:1")
        payloadDigest = $Context.PayloadDigest
        hostCommit = $Context.HostCommit
        hostTree = $Context.HostTree
        exitCode = 0
        testCounts = [pscustomobject]@{ total = 1; passed = 1; failed = 0 }
        resultArtifact = $artifactJson
        resultArtifactHash = $artifactHash
    }
}

function Invoke-ProviderFixtureOperation {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string] $Scenario,
        [Parameter(Mandatory = $true)][string] $Operation,
        [Parameter(Mandatory = $true)][pscustomobject] $Context
    )
    $guestId = "fixture-host:hyperv:FastFiles-Fixture-$Scenario"
    $result = New-FixtureAdapterResult -Status 'PASS' -Reason "fixture $Operation ok"
    $result.GuestIdentity = $guestId

    if ($Operation -eq 'DISCOVER') {
        $result.Reason = 'fixture hyper-v provider discovered'
        $result.Artifacts = @('fixture:hyper-v-module')
        return $result
    }

    if ($Operation -eq 'PREFLIGHT') {
        if ($Scenario -eq 'preflight-unavailable') {
            $result.Status = 'BLOCKED'
            $result.Reason = 'NO_USABLE_SOURCE_IMAGE'
            $result.Error = 'Simulated preflight: no usable source image (guest image absent).'
            $result.Data = [pscustomobject]@{ ReasonCodes = @('NO_USABLE_SOURCE_IMAGE'); ElevationBlock = $false }
            return $result
        }
        $result.Reason = 'fixture preflight ready'
        $result.Data = [pscustomobject]@{ ReasonCodes = @(); ElevationBlock = $false; SourceVerified = $true }
        return $result
    }

    if ($Scenario -eq 'preflight-only') {
        if ($Operation -eq 'CLEANUP') {
            $result.Reason = 'nothing provisioned; no cleanup required'
            $result.Data = [pscustomobject]@{ MutatedSystemState = $false }
            return $result
        }
        if ($Operation -eq 'RESIDUE_CHECK') {
            $result.Reason = 'no provider residue observed'
            $result.Data = [pscustomobject]@{ Residue = @() }
            return $result
        }
        $result.Status = 'BLOCKED'
        $result.Reason = 'EXECUTION_NOT_ATTEMPTED'
        $result.Error = 'Simulated preflight-only mode: provider declines execution; no payload was transferred or executed.'
        return $result
    }

    if ($Operation -eq 'PROVISION') {
        if ($Scenario -eq 'provision-failure') {
            $result.Status = 'FAIL'
            $result.Reason = 'VM_CREATE_FAILED'
            $result.Error = 'Simulated provisioning fault: unable to create fixture VM.'
            return $result
        }
        $result.Reason = "fixture VM provisioned for scenario '$Scenario'"
        $result.Data = [pscustomobject]@{ VmName = "FastFiles-Fixture-$Scenario"; Reused = $false }
        return $result
    }

    if ($Operation -eq 'ACTIVATE') {
        if ($Scenario -eq 'activation-failure') {
            $result.Status = 'FAIL'
            $result.Reason = 'ACTIVATION_FAILED'
            $result.Error = 'Simulated activation fault: fixture VM did not start.'
            return $result
        }
        $result.Reason = 'fixture VM activated'
        return $result
    }

    if ($Operation -eq 'WAIT_READY') {
        if ($Scenario -eq 'timeout') {
            $result.Status = 'FAIL'
            $result.Reason = 'TIMEOUT'
            $result.Error = "Simulated timeout: fixture guest did not reach ready state within $($Context.TimeoutSec)s (simulated; no wall-clock wait)."
            return $result
        }
        $result.Reason = 'fixture guest ready'
        return $result
    }

    if ($Operation -eq 'ESTABLISH_SECURE_GUEST_CHANNEL') {
        if ($Scenario -eq 'channel-failure') {
            $result.Status = 'FAIL'
            $result.Reason = 'GUEST_CHANNEL_UNAVAILABLE'
            $result.Error = 'Simulated channel fault: secure guest channel could not be established.'
            return $result
        }
        $result.Reason = 'fixture secure guest channel established'
        return $result
    }

    if ($Operation -eq 'TRANSFER_PAYLOAD') {
        if ($Scenario -eq 'transfer-failure') {
            $result.Status = 'FAIL'
            $result.Reason = 'PAYLOAD_TRANSFER_FAILED'
            $result.Error = 'Simulated transfer fault: payload did not reach the fixture guest.'
            return $result
        }
        $result.Reason = "payload transferred; sha256=$($Context.PayloadDigest)"
        $result.Data = [pscustomobject]@{ PayloadDigest = $Context.PayloadDigest }
        return $result
    }

    if ($Operation -eq 'EXECUTE_PAYLOAD') {
        if ($Scenario -eq 'execution-failure') {
            $guestResult = New-FixtureGuestResult -Context $Context -Scenario $Scenario
            $guestResult.exitCode = 1
            $guestResult.testCounts = [pscustomobject]@{ total = 2; passed = 1; failed = 1 }
            $result.Status = 'FAIL'
            $result.Reason = 'GUEST_EXECUTION_FAILED'
            $result.Error = 'Simulated execution fault: fixture probe exited 1 (1 of 2 tests failed).'
            $result.Data = $guestResult
            return $result
        }
        if ($Scenario -eq 'malformed-result') {
            $result.Reason = 'fixture probe executed; proof malformed by scenario design'
            $result.Data = [pscustomobject]@{
                schemaVersion = 1
                provider = 'hyperv'
                exitCode = 0
            }
            return $result
        }
        if ($Scenario -eq 'provenance-mismatch') {
            $guestResult = New-FixtureGuestResult -Context $Context -Scenario $Scenario
            $guestResult.payloadDigest = 'deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef'
            $result.Reason = 'fixture probe executed; proof digest corrupted by scenario design'
            $result.Data = $guestResult
            return $result
        }
        if ($Scenario -eq 'cancellation') {
            $result.Status = 'FAIL'
            $result.Reason = 'CANCELLED'
            $result.Error = 'Simulated cancellation: run was cancelled during payload execution (fail-closed).'
            $result.Data = [pscustomobject]@{ Cancelled = $true }
            return $result
        }
        $result.Reason = 'fixture probe executed with exit code 0'
        $result.Data = (New-FixtureGuestResult -Context $Context -Scenario $Scenario)
        return $result
    }

    if ($Operation -eq 'COLLECT_LOGS') {
        $result.Reason = 'fixture guest logs collected'
        $result.Artifacts = @('fixture:guest-logs/cert-probe.log')
        return $result
    }

    if ($Operation -eq 'COLLECT_STRUCTURED_RESULTS') {
        if ($Scenario -eq 'collection-failure') {
            $result.Status = 'FAIL'
            $result.Reason = 'RESULT_COLLECTION_FAILED'
            $result.Error = 'Simulated collection fault: structured result artifact unavailable.'
            return $result
        }
        $guestResult = New-FixtureGuestResult -Context $Context -Scenario $Scenario
        if ($Scenario -eq 'execution-failure') {
            $guestResult.exitCode = 1
            $guestResult.testCounts = [pscustomobject]@{ total = 2; passed = 1; failed = 1 }
        }
        if ($Scenario -eq 'provenance-mismatch') {
            $guestResult.payloadDigest = 'deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef'
        }
        $result.Reason = 'fixture structured result collected'
        $result.Data = [pscustomobject]@{
            Artifact = $guestResult.resultArtifact
            ArtifactHash = $guestResult.resultArtifactHash
        }
        return $result
    }

    if ($Operation -eq 'RESTORE_ROLLBACK') {
        if ($Scenario -eq 'rollback-failure') {
            $result.Status = 'FAIL'
            $result.Reason = 'ROLLBACK_FAILED'
            $result.Error = 'Simulated rollback fault: fixture snapshot restore did not complete.'
            $result.Actions = @(@{ name = 'restore-snapshot'; status = 'FAIL'; error = 'simulated restore fault' })
            return $result
        }
        $result.Reason = 'fixture guest restored to clean snapshot'
        $result.Actions = @(@{ name = 'restore-snapshot'; status = 'PASS'; error = $null })
        $result.Data = [pscustomobject]@{ Restored = $true; Snapshot = 'FastFiles-Clean-Fixture' }
        return $result
    }

    if ($Operation -eq 'CLEANUP') {
        if ($Scenario -eq 'cleanup-failure') {
            $result.Status = 'FAIL'
            $result.Reason = 'CLEANUP_FAILED'
            $result.Error = 'Simulated cleanup fault: fixture VM removal did not complete.'
            $result.Actions = @(@{ name = 'remove-vm'; status = 'FAIL'; error = 'simulated remove-vm fault' })
            $result.Data = [pscustomobject]@{ MutatedSystemState = $true }
            return $result
        }
        $result.Reason = 'fixture cleanup completed'
        $result.Actions = @(@{ name = 'remove-vm'; status = 'PASS'; error = $null })
        $result.Data = [pscustomobject]@{ MutatedSystemState = $true }
        return $result
    }

    if ($Operation -eq 'RESIDUE_CHECK') {
        if ($Scenario -eq 'cleanup-failure') {
            $result.Status = 'FAIL'
            $result.Reason = 'RESIDUE_FOUND'
            $result.Error = "Simulated residue: fixture guest still present after failed cleanup: $guestId."
            $result.Data = [pscustomobject]@{ Residue = @($guestId) }
            return $result
        }
        $result.Reason = 'no provider residue observed'
        $result.Data = [pscustomobject]@{ Residue = @() }
        return $result
    }

    $result.Status = 'FAIL'
    $result.Reason = 'UNKNOWN_OPERATION'
    $result.Error = "Fixture has no operation '$Operation'."
    return $result
}

function New-ProviderFixtureAdapter {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet('success', 'preflight-only', 'preflight-unavailable', 'provision-failure', 'activation-failure', 'channel-failure', 'transfer-failure', 'execution-failure', 'collection-failure', 'malformed-result', 'provenance-mismatch', 'timeout', 'rollback-failure', 'cleanup-failure', 'cancellation')]
        [string] $Scenario
    )
    $scenarioCopy = $Scenario
    $adapter = {
        param([string] $Operation, [pscustomobject] $Context)
        return (Invoke-ProviderFixtureOperation -Scenario $scenarioCopy -Operation $Operation -Context $Context)
    }.GetNewClosure()
    return $adapter
}

Export-ModuleMember -Function Get-ProviderFixtureScenarios, New-ProviderFixtureAdapter, Invoke-ProviderFixtureOperation
