<#{
    Deterministic provider-contract tests. The fake provider exercises the
    orchestration seam only; it is not evidence for a real Windows guest.
}#>

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$script:Failures = 0
$script:FakeMode = 'success'

function Check {
    param([bool] $Condition, [string] $Description)
    if (-not $Condition) {
        Write-Host "FAIL: $Description" -ForegroundColor Red
        $script:Failures++
    } else {
        Write-Host "PASS: $Description"
    }
}

function New-FakeContext {
    [pscustomobject]@{
        RunContext = [pscustomobject]@{ Timestamp = 'provider-contract'; RunPath = (Join-Path $env:TEMP 'FastFilesProviderContractTests') }
        Fingerprint = [pscustomobject]@{ TargetIdentity = 'fake:contract'; IsElevated = $false; SessionId = 1; SessionKind = 'test' }
        RequiredTier = 0
        SourcePath = $null
        VmName = $null
        Cancelled = $false
        State = $null
    }
}

function global:Invoke-FakePreflight {
    param([pscustomobject] $ProviderContext)
    if ($script:FakeMode -eq 'preflight-fail') {
        return [pscustomobject]@{ Ready = $false; ReasonCodes = @('NO_USABLE_SOURCE_IMAGE'); Detail = 'fake preflight failure' }
    }
    [pscustomobject]@{ Ready = $true; ReasonCodes = @(); Detail = 'fake ready' }
}

function global:Invoke-FakeProvision {
    param([pscustomobject] $ProviderContext)
    $ProviderContext.State = [pscustomobject]@{ CleanupCalls = 0; Provisioned = $true }
    if ($script:FakeMode -eq 'provision-fail') {
        return [pscustomobject]@{ Ready = $false; ReasonCodes = @('GUEST_NOT_BOOTABLE'); Detail = 'fake provision failure' }
    }
    [pscustomobject]@{ Ready = $true }
}

function global:Invoke-FakeActivate {
    param([pscustomobject] $ProviderContext)
    if ($script:FakeMode -eq 'activate-fail') { return [pscustomobject]@{ Ready = $false; Detail = 'fake activation failure' } }
    [pscustomobject]@{ Ready = $true }
}

function global:Invoke-FakeCollectLogs {
    param([pscustomobject] $ProviderContext)
    if ($script:FakeMode -eq 'collect-fail') { throw 'fake log collection failure' }
    @([pscustomobject]@{ path = 'safe/fake-log.json'; type = 'fake' })
}

function global:Invoke-FakeSnapshotRestore {
    param([pscustomobject] $ProviderContext)
    if ($script:FakeMode -eq 'snapshot-fail') { return [pscustomobject]@{ Restored = $false; Detail = 'fake snapshot failure' } }
    [pscustomobject]@{ Restored = $true; Snapshot = 'fake-clean' }
}

function global:Invoke-FakeCleanup {
    param([pscustomobject] $ProviderContext)
    $ProviderContext.State.CleanupCalls++
    if ($script:FakeMode -eq 'cleanup-fail') { return [pscustomobject]@{ succeeded = $false; reason = 'CLEANUP_INCOMPLETE' } }
    [pscustomobject]@{ succeeded = $true; alreadyCompleted = ($ProviderContext.State.CleanupCalls -gt 1) }
}

Import-Module (Join-Path $PSScriptRoot '..\..\core\Providers.psm1') -Force

$fake = New-EnvironmentProviderDescriptor -Id 'fake-contract' -InterfaceVersion '1.0.0' `
    -SupportsSnapshotRestore $true -SourceKinds @('fixture') `
    -EntryPoints @{
        preflight = 'Invoke-FakePreflight'; provision = 'Invoke-FakeProvision'; activate = 'Invoke-FakeActivate'
        collectLogs = 'Invoke-FakeCollectLogs'; cleanup = 'Invoke-FakeCleanup'
        snapshotRestore = 'Invoke-FakeSnapshotRestore'
    }
$valid = Test-EnvironmentProviderDescriptor -Provider $fake
Check $valid.Valid 'fake descriptor validates against the provider contract'

$ctx = New-FakeContext
$script:FakeMode = 'success'
$success = Invoke-EnvironmentProviderContract -Provider $fake -ProviderContext $ctx -RestoreSnapshot -Execute { param($ignored) [pscustomobject]@{ executed = $true } }
$phaseNames = @($success.phases | ForEach-Object { $_.phase })
Check ($success.status -eq 'PASS') 'successful lifecycle returns PASS'
Check (($phaseNames -join ',') -eq 'preflight,provision,activate,execute,collectLogs,snapshotRestore,cleanup') 'successful lifecycle order is deterministic'
Check ($success.cleanup.succeeded) 'successful lifecycle cleans up'

$cleanupAgain = Invoke-EnvironmentProviderLifecycle -Provider $fake -Phase cleanup -ProviderContext $ctx
Check ($cleanupAgain.succeeded -and $cleanupAgain.alreadyCompleted) 'cleanup is idempotent on repetition'

foreach ($case in @(
    @{ Mode = 'preflight-fail'; Expected = 'REQUIRED-BUT-UNAVAILABLE'; Label = 'preflight failure is unavailable, not a product pass' },
    @{ Mode = 'provision-fail'; Expected = 'REQUIRED-BUT-UNAVAILABLE'; Label = 'provisioning failure preserves unavailable classification' },
    @{ Mode = 'activate-fail'; Expected = 'FAIL'; Label = 'activation failure is a hard failure' },
    @{ Mode = 'collect-fail'; Expected = 'FAIL'; Label = 'log collection failure is visible' },
    @{ Mode = 'snapshot-fail'; Expected = 'REQUIRED-BUT-UNAVAILABLE'; Label = 'snapshot restore failure is unavailable' },
    @{ Mode = 'cleanup-fail'; Expected = 'FAIL'; Label = 'cleanup failure is never hidden' }
)) {
    $script:FakeMode = $case.Mode
    $result = Invoke-EnvironmentProviderContract -Provider $fake -ProviderContext (New-FakeContext) -RestoreSnapshot -Execute { param($ignored) [pscustomobject]@{ executed = $true } }
    Check ($result.status -eq $case.Expected) $case.Label
}

$script:FakeMode = 'cancel'
$cancelled = Invoke-EnvironmentProviderContract -Provider $fake -ProviderContext (New-FakeContext) -Execute {
    param($context)
    $context.Cancelled = $true
    throw 'simulated cancellation'
}
Check ($cancelled.status -eq 'SKIPPED') 'cancellation is classified as SKIPPED with cleanup still attempted'

$script:FakeMode = 'success'
$resumed = Invoke-EnvironmentProviderContract -Provider $fake -ProviderContext (New-FakeContext) -Execute { param($ignored) 'resumed' }
Check ($resumed.status -eq 'PASS') 'a new context can resume after an interrupted run'

$bad = [pscustomobject]@{ Id = 'bad'; InterfaceVersion = '1.0.0'; EntryPoints = [pscustomobject]@{}; SupportsSnapshotRestore = $false }
Check (-not (Test-EnvironmentProviderDescriptor -Provider $bad).Valid) 'corrupt descriptor is rejected'
$unsupported = New-EnvironmentProviderDescriptor -Id 'fake-contract' -InterfaceVersion '2.0.0' -EntryPoints @{
    preflight = 'Invoke-FakePreflight'; provision = 'Invoke-FakeProvision'; activate = 'Invoke-FakeActivate'; collectLogs = 'Invoke-FakeCollectLogs'; cleanup = 'Invoke-FakeCleanup'
}
Check (-not (Test-EnvironmentProviderDescriptor -Provider $unsupported).Valid) 'unsupported interface version is rejected'
$missingCommand = New-EnvironmentProviderDescriptor -Id 'fake-contract' -InterfaceVersion '1.0.0' -EntryPoints @{
    preflight = 'Invoke-FakeMissing'; provision = 'Invoke-FakeProvision'; activate = 'Invoke-FakeActivate'; collectLogs = 'Invoke-FakeCollectLogs'; cleanup = 'Invoke-FakeCleanup'
}
$dependency = Invoke-EnvironmentProviderContract -Provider $missingCommand -ProviderContext (New-FakeContext)
Check ($dependency.status -eq 'FAIL') 'missing provider dependency is surfaced as FAIL'

if ($script:Failures -gt 0) {
    Write-Host "$($script:Failures) provider contract checks failed." -ForegroundColor Red
    exit 1
}
Write-Host 'All provider contract checks passed.' -ForegroundColor Green
exit 0
