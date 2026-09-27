<#
    runtime-certification-and-verifier-convergence (owned-process cleanup race):
    deterministic fixtures for the repository-build process reaper. The 2026-09-05
    core UIA run proved the failure class: a capability-owned FastFiles process
    exited during the after-snapshot, its transient CIM CreationDate read failure
    poisoned `verified`, and cleanup was reported CLEANUP_INCOMPLETE although no
    residue existed. Cleanup truth must come from residue + verified absence of the
    exact owned identities, never from a read race on a process that already left.
    These fixtures never launch FastFiles and never consume verify/runs.
#>

$ErrorActionPreference = 'Stop'
$script:Failures = 0
function Check {
    param([bool] $Condition, [string] $Description)
    if ($Condition) { Write-Host "PASS: $Description" } else { Write-Host "FAIL: $Description"; $script:Failures++ }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
Import-Module (Join-Path $repoRoot 'verify\core\RunTree.psm1') -Force
$runner = Import-Module (Join-Path $repoRoot 'verify\core\CapabilityRunner.psm1') -Force -PassThru
$RunContext = [pscustomobject]@{ VerifyRoot = (Join-Path $repoRoot 'verify') }

function Invoke-RunnerFunction {
    param([Parameter(Mandatory)][string] $Body, $Parameters)
    $sb = [ScriptBlock]::Create($Body)
    return (& $runner $sb $Parameters)
}

# 1. A healthy host snapshot is verified and carries only repository-build identities.
$snapshot = Invoke-RunnerFunction -Body 'param($P) Get-VerificationBuildProcessSnapshot -RunContext $P.RunContext' -Parameters @{ RunContext = $RunContext }
Check ($snapshot.verified -and @($snapshot.errors).Count -eq 0) 'clean build-process snapshot is verified with no errors'
Check (@($snapshot.identities | Where-Object { $_.path -notlike ((Join-Path $repoRoot 'build') + '*') }).Count -eq 0) 'snapshot identities are confined to the repository build tree'
Check (@($snapshot.identities | Where-Object { $null -eq $_.startTimeUtc }).Count -eq 0) 'every snapshot identity carries an exact start time'

# 2. An owned identity whose process already exited is terminated, never residue,
#    and cleanup still passes (the exact false-FAIL class from the live run).
$absentPid = 999999
while (Get-Process -Id $absentPid -ErrorAction SilentlyContinue) { $absentPid++ }
$absentIdentity = [pscustomobject]@{
    pid = $absentPid
    path = (Join-Path $repoRoot 'build\debug-diagnostic\src\ui\FastFiles.exe')
    startTimeUtc = [DateTime]::UtcNow.ToString('o')
    identityKey = "$absentPid|absent"
}
$emptyBefore = [pscustomobject]@{ identities = @(); errors = @(); verified = $true }
$absentAfter = [pscustomobject]@{ identities = @($absentIdentity); errors = @(); verified = $true }
$absentAttempt = Invoke-RunnerFunction -Body 'param($P) Stop-VerificationOwnedBuildProcesses -RunContext $P.RunContext -Before $P.Before -After $P.After' -Parameters @{ RunContext = $RunContext; Before = $emptyBefore; After = $absentAfter }
Check ($absentAttempt.pass -and @($absentAttempt.residue).Count -eq 0) 'already-exited owned identity passes cleanup with zero residue'
Check (@($absentAttempt.terminated | Where-Object { $_.pid -eq $absentPid -and $_.observation -eq 'identity-no-longer-verifiable' }).Count -eq 1) 'already-exited owned identity is recorded as terminated, not adopted'

# 3. A live owned identity is terminated exactly by identity and cleanup passes.
$pwsh = (Get-Command pwsh.exe -ErrorAction SilentlyContinue)
if ($null -eq $pwsh) {
    Write-Host 'SKIP: pwsh.exe unavailable for the live owned-process fixture'
} else {
    $live = Start-Process -FilePath $pwsh.Source -ArgumentList @('-NoProfile', '-NonInteractive', '-Command', 'Start-Sleep -Seconds 300') -PassThru -WindowStyle Hidden
    try {
        $live.Refresh()
        # System.Diagnostics.Path is filled lazily and can still be empty right
        # after Start-Process under load; GetFullPath('') then aborted this
        # fixture with 'The path is empty'. The production snapshot reads CIM
        # ExecutablePath and skips empty paths, so record the exact path we
        # launched instead of depending on the lazy property.
        $liveIdentity = [pscustomobject]@{
            pid = [int]$live.Id
            path = [IO.Path]::GetFullPath([string]$pwsh.Source)
            startTimeUtc = $live.StartTime.ToUniversalTime().ToString('o')
            identityKey = "$($live.Id)|live"
        }
        $liveAfter = [pscustomobject]@{ identities = @($liveIdentity); errors = @(); verified = $true }
        $liveAttempt = Invoke-RunnerFunction -Body 'param($P) Stop-VerificationOwnedBuildProcesses -RunContext $P.RunContext -Before $P.Before -After $P.After' -Parameters @{ RunContext = $RunContext; Before = $emptyBefore; After = $liveAfter }
        Check ($liveAttempt.pass -and @($liveAttempt.residue).Count -eq 0) 'live owned identity is terminated with zero residue'
        Check (@($liveAttempt.terminated | Where-Object { $_.pid -eq $live.Id }).Count -eq 1) 'live owned identity is recorded as terminated'
        # Terminating a process is asynchronous: the identity-checked kill can
        # return before the kernel reaps the process object, so disappearance is
        # verified with a bounded wait. An immediate poll made a whole-suite run
        # under load fail intermittently; a kill that never takes effect still
        # fails here after the bound.
        $goneDeadline = [DateTime]::UtcNow.AddSeconds(5)
        while ([DateTime]::UtcNow -lt $goneDeadline -and (Get-Process -Id $live.Id -ErrorAction SilentlyContinue)) {
            Start-Sleep -Milliseconds 50
        }
        Check (-not (Get-Process -Id $live.Id -ErrorAction SilentlyContinue)) 'terminated owned process is actually gone'
    } finally {
        try { if (-not $live.HasExited) { $live.Kill(); $null = $live.WaitForExit(5000) } } catch { }
    }
}

# 4. Extended-length (`\\?\`) CIM paths must normalize to the same identity; the
#    live interrupted-run probe observed the engine reporting
#    `\\?\D:\...\build\...\FastFilesEngine.exe`, which an unnormalized prefix
#    filter silently drops from ownership tracking.
$buildExe = (Join-Path $repoRoot 'build\debug-diagnostic\src\engine\FastFilesEngine.exe')
$normalized = Invoke-RunnerFunction -Body 'param($P) Get-VerificationNormalizedExecutablePath -Path $P.Path' -Parameters @{ Path = "\\?\$buildExe" }
Check ($normalized -eq $buildExe) "extended-length path normalizes to the build-tree path"
Check ($normalized.StartsWith((Join-Path $repoRoot 'build'), [StringComparison]::OrdinalIgnoreCase)) 'normalized extended-length path matches the build-root prefix filter'
$volumePath = "\\?\Volume{00000000-0000-0000-0000-000000000000}\file.exe"
$volumeNormalized = Invoke-RunnerFunction -Body 'param($P) Get-VerificationNormalizedExecutablePath -Path $P.Path' -Parameters @{ Path = $volumePath }
Check ($volumeNormalized -eq $volumePath) 'volume-GUID device paths are preserved verbatim'
if ($null -ne $pwsh) {
    $live2 = Start-Process -FilePath $pwsh.Source -ArgumentList @('-NoProfile', '-NonInteractive', '-Command', 'Start-Sleep -Seconds 300') -PassThru -WindowStyle Hidden
    $null = $live2.Refresh()
    try {
        $cimPath = [string](Get-CimInstance Win32_Process -Filter "ProcessId=$($live2.Id)").ExecutablePath
        # Record as the snapshot would: from a CIM path carrying the extended prefix.
        $prefixedIdentity = [pscustomobject]@{
            pid = [int]$live2.Id
            path = (Invoke-RunnerFunction -Body 'param($P) Get-VerificationNormalizedExecutablePath -Path $P.Path' -Parameters @{ Path = "\\?\$cimPath" })
            startTimeUtc = $live2.StartTime.ToUniversalTime().ToString('o')
            identityKey = "$($live2.Id)|prefixed"
        }
        Check (Invoke-RunnerFunction -Body 'param($P) Test-VerificationProcessIdentityCurrent -Identity $P.Identity' -Parameters @{ Identity = $prefixedIdentity }) 'identity recorded from a normalized extended-length path verifies against the live process'
    } finally {
        try { if (-not $live2.HasExited) { $live2.Kill(); $null = $live2.WaitForExit(5000) } } catch { }
    }
}

# 5. An unverifiable snapshot still fails cleanup closed (no residue laundering).
$unverifiedAfter = [pscustomobject]@{ identities = @(); errors = @('process snapshot failed: simulated'); verified = $false }
$unverifiedAttempt = Invoke-RunnerFunction -Body 'param($P) Stop-VerificationOwnedBuildProcesses -RunContext $P.RunContext -Before $P.Before -After $P.After' -Parameters @{ RunContext = $RunContext; Before = $emptyBefore; After = $unverifiedAfter }
Check (-not $unverifiedAttempt.pass -and -not $unverifiedAttempt.verified) 'unverified snapshot fails cleanup closed'

if ($script:Failures -gt 0) { exit 1 }
Write-Host 'All owned-process reaper checks passed.'
exit 0
