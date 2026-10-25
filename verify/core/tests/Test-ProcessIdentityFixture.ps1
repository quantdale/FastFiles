<# Non-destructive qualification of the reaper fixture's identity capture.
   The child exits naturally. This does not invoke Stop/Kill or certify reaping.
   The negative control reconstructs the historical launch-alias assumption so
   the acceptance assertion is discriminating even on non-Store PowerShell. #>
param([switch] $UseLaunchPathNegativeControl)

$ErrorActionPreference = 'Stop'
$script:Failures = 0
function Check {
    param([bool] $Condition, [string] $Description)
    if ($Condition) { Write-Host "PASS: $Description" } else { Write-Host "FAIL: $Description"; $script:Failures++ }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$runner = Import-Module (Join-Path $repoRoot 'verify\core\CapabilityRunner.psm1') -Force -PassThru
Import-Module (Join-Path $PSScriptRoot 'ProcessIdentityFixture.psm1') -Force
function Test-CurrentIdentity {
    param($Identity)
    return (& $runner { param($Value) Test-VerificationProcessIdentityCurrent -Identity $Value } $Identity)
}

$pwsh = (Get-Command pwsh.exe -ErrorAction Stop).Source
$child = Start-Process -FilePath $pwsh -ArgumentList @('-NoProfile', '-NonInteractive', '-Command', 'Start-Sleep -Seconds 5') -PassThru -WindowStyle Hidden
try {
    $identity = Wait-ProcessFixtureIdentity -Process $child
    $identity.path = & $runner { param($Path) Get-VerificationNormalizedExecutablePath -Path $Path } $identity.path
    Check ($identity.pid -eq $child.Id) 'capture retains the exact launched process ID'
    Check ([DateTime]::Parse($identity.startTimeUtc).ToUniversalTime() -eq $child.StartTime.ToUniversalTime()) 'capture retains the exact launched process start'
    Check (-not [string]::IsNullOrWhiteSpace($identity.path)) 'capture waits for a nonempty actual image path'

    # A synthetic execution-alias spelling has the same basename but is not
    # the child's actual image. It requires no installed alias or OS mutation.
    $aliasIdentity = $identity.PSObject.Copy()
    $aliasIdentity.path = Join-Path $repoRoot 'build\identity-fixture\WindowsApps\pwsh.exe'
    Check (-not (Test-CurrentIdentity $aliasIdentity)) 'production verifier rejects a launch-path alias for the same PID/start'

    $wrongStart = $identity.PSObject.Copy()
    $wrongStart.startTimeUtc = [DateTime]::Parse($identity.startTimeUtc).AddMinutes(-1).ToString('o')
    Check (-not (Test-CurrentIdentity $wrongStart)) 'production verifier rejects the correct image with a different start'

    $captured = if ($UseLaunchPathNegativeControl) { $aliasIdentity } else { $identity }
    Check (Test-CurrentIdentity $captured) 'captured fixture identity verifies against the actual live process'
    Check ($child.WaitForExit(10000)) 'identity fixture child exits naturally within its bound'
    Check (-not (Test-CurrentIdentity $identity)) 'naturally exited child no longer verifies as the captured identity'
} finally {
    # Never terminate this fixture: its command has a bounded natural lifetime.
    $child.Dispose()
}

if ($script:Failures -gt 0) { exit 1 }
Write-Host 'All non-destructive process-identity fixture checks passed; reaping was not executed.'
exit 0
