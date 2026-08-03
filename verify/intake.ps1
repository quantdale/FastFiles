<#
.SYNOPSIS
    FastFiles autonomous intake orchestrator — the single resumable agent entry point
    (zero-touch-autonomous-engineering tasks 7.1-7.6).

.DESCRIPTION
    `autonomous` drives the full lifecycle non-interactively:
        discover -> plan -> provision -> implement -> build -> test -> diagnose ->
        repair -> re-test -> validate -> collect-evidence -> update-tasks -> commit ->
        sync -> archive
    with persistent run state at verify/runs/autonomous/<run-id>/state.json, resume on
    re-invocation, per-step timeouts, bounded retries (<=3), failure classification
    (Class A / Class B / external), and deterministic exit codes.

    `status` returns machine-readable run state (current phase, completed and remaining
    steps, authoritative open-task count).

    `archive-gate` re-resolves the terminal four-state archive gate from a run's state.

.NOTES
    Exit codes (match verify.ps1):
      0  PASS
      1  FAIL
      2  SKIPPED
      3  HARNESS ERROR
      10 NOT-YET-IMPLEMENTED
    See verify/autonomous/contract.json and AUTONOMOUS.md for the intake contract.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [ValidateSet('autonomous', 'status', 'archive-gate')]
    [string] $Verb,

    [string] $RunId,
    [switch] $Fresh,
    [string] $Change = 'zero-touch-autonomous-engineering',
    [string[]] $Configuration = @('debug'),
    [string] $Provider = 'local',
    [int] $MaxIterations = 3,
    [int] $MaxRetries = 3,
    [int] $TimeoutSeconds = 1200,
    [string] $ImplementScript,
    [switch] $AllowPush,
    [switch] $SkipCommit
)

$ErrorActionPreference = 'Stop'
$VerifyRoot = $PSScriptRoot
$RepoRoot = Split-Path $VerifyRoot -Parent

$ExitPass = 0
$ExitFail = 1
$ExitSkipped = 2
$ExitError = 3
$ExitNotImplemented = 10

# Core harness modules (same set verify.ps1 imports).
Import-Module (Join-Path $VerifyRoot 'core\Fingerprint.psm1') -Force
Import-Module (Join-Path $VerifyRoot 'core\Providers.psm1') -Force
Import-Module (Join-Path $VerifyRoot 'core\RunTree.psm1') -Force
Import-Module (Join-Path $VerifyRoot 'core\Registry.psm1') -Force
Import-Module (Join-Path $VerifyRoot 'core\CapabilityRunner.psm1') -Force
Import-Module (Join-Path $VerifyRoot 'core\Reporting.psm1') -Force
Import-Module (Join-Path $VerifyRoot 'core\Repair.psm1') -Force
Import-Module (Join-Path $VerifyRoot 'core\Gate.psm1') -Force
Import-Module (Join-Path $VerifyRoot 'core\Toolchain.psm1') -Force
Import-Module (Join-Path $VerifyRoot 'core\Schema.psm1') -Force
Import-Module (Join-Path $VerifyRoot 'core\FlakyTestPolicy.psm1') -Force
Import-Module (Join-Path $VerifyRoot 'capabilities\diagnostics\Diagnostics.psm1') -Force -Global

$script:RunPhases = @(
    'discover', 'plan', 'provision', 'implement', 'build', 'test',
    'diagnose', 'repair', 're-test', 'validate', 'collect-evidence',
    'update-tasks', 'commit', 'sync', 'archive'
)
$script:AutonomousRoot = Join-Path $VerifyRoot 'runs\autonomous'
$script:StateSchemaPath = Join-Path $VerifyRoot 'autonomous\schemas\run-state.schema.json'
$script:RunMaxTimeout = $TimeoutSeconds

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

function Get-UtcNow {
    (Get-Date).ToUniversalTime().ToString('o')
}

function Set-NoteProperty {
    param([pscustomobject] $Obj, [string] $Name, $Value)
    if ($Obj.PSObject.Properties[$Name]) { $Obj.PSObject.Properties[$Name].Value = $Value }
    else { $Obj | Add-Member -NotePropertyName $Name -NotePropertyValue $Value -Force }
}

function Invoke-BoundedProcess {
    <#
    .SYNOPSIS
        Runs an external command with a hard timeout and streamed log capture.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $FilePath,
        $ArgumentList,
        [string] $WorkingDirectory,
        [int] $TimeoutSeconds,
        [string] $LogPath
    )
    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $FilePath
    if ($ArgumentList) {
        foreach ($a in $ArgumentList) {
            if ($a -is [System.Array]) { foreach ($x in $a) { $psi.ArgumentList.Add([string]$x) } }
            else { $psi.ArgumentList.Add([string]$a) }
        }
    }
    if ($WorkingDirectory) { $psi.WorkingDirectory = $WorkingDirectory }
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
    $psi.Environment['GIT_TERMINAL_PROMPT'] = '0'

    $proc = [System.Diagnostics.Process]::new()
    $proc.StartInfo = $psi
    if (-not $proc.Start()) { return [pscustomobject]@{ exitCode = -2; timedOut = $false; output = 'process failed to start' } }
    $stdout = $proc.StandardOutput.ReadToEndAsync()
    $stderr = $proc.StandardError.ReadToEndAsync()
    if (-not $proc.WaitForExit($TimeoutSeconds * 1000)) {
        try { $proc.Kill($true) } catch { }
        try { $proc.WaitForExit(5000) } catch { }
        $combined = "$($stdout.Result)`n$($stderr.Result)" | Out-String
        if ($LogPath) { $combined | Set-Content -LiteralPath $LogPath -Encoding utf8 }
        return [pscustomobject]@{ exitCode = -1; timedOut = $true; output = $combined }
    }
    $combined = "$($stdout.Result)`n$($stderr.Result)" | Out-String
    if ($LogPath) { $combined | Set-Content -LiteralPath $LogPath -Encoding utf8 }
    [pscustomobject]@{ exitCode = $proc.ExitCode; timedOut = $false; output = $combined }
}

function Get-OpenTaskSummary {
    <#
    .SYNOPSIS
        Authoritative open-task inventory across all openspec/changes/*/tasks.md.
        This is the single source of truth for the status verb's open-task count.
    #>
    [CmdletBinding()]
    param()
    $changesRoot = Join-Path $RepoRoot 'openspec\changes'
    $byChange = [ordered]@{}
    $totalOpen = 0
    $totalTotal = 0
    foreach ($dir in Get-ChildItem -Path $changesRoot -Directory | Where-Object { $_.Name -ne 'archive' } | Sort-Object Name) {
        $tasks = Join-Path $dir.FullName 'tasks.md'
        if (-not (Test-Path -LiteralPath $tasks)) { continue }
        $open = 0; $closed = 0
        foreach ($line in Get-Content -LiteralPath $tasks) {
            if ($line -match '^\s*-\s*\[ \]\s+') { $open++ }
            elseif ($line -match '^\s*-\s*\[x\]\s+') { $closed++ }
        }
        $byChange[$dir.Name] = $open
        $totalOpen += $open
        $totalTotal += ($open + $closed)
    }
    [pscustomobject]@{
        total = $totalOpen
        totalTasks = $totalTotal
        byChange = $byChange
    }
}

function Get-RunIdFromTimestamp {
    param([datetime] $Now)
    $Now.ToString('yyyyMMdd-HHmmss')
}

function Get-GitHead {
    (Invoke-BoundedProcess -FilePath 'git' -ArgumentList @('rev-parse', 'HEAD') -WorkingDirectory $RepoRoot -TimeoutSeconds 30).output.Trim()
}

function Get-LatestIncompleteRun {
    $latest = Get-ChildItem -Path $script:AutonomousRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { Test-Path (Join-Path $_.FullName 'state.json') } |
        Sort-Object Name -Descending | Select-Object -First 1
    if (-not $latest) { return $null }
    $state = Get-Content -LiteralPath (Join-Path $latest.FullName 'state.json') -Raw | ConvertFrom-Json
    if ($state.lastOutcome.status -in @('COMPLETE', 'FAIL', 'ESCALATED', 'TIMEOUT')) { return $null }
    return $state
}

function Save-IntakeState {
    param([pscustomobject] $State, [string] $RunPath)
    $State.updatedAt = Get-UtcNow
    $json = $State | ConvertTo-Json -Depth 20
    New-Item -ItemType Directory -Path $RunPath -Force | Out-Null
    $json | Set-Content -LiteralPath (Join-Path $RunPath 'state.json') -Encoding utf8
    Invoke-ValidateIntakeState -State $State
}

function Invoke-ValidateIntakeState {
    param([pscustomobject] $State)
    if (-not (Test-Path -LiteralPath $script:StateSchemaPath)) { return }
    $schema = Get-Content -LiteralPath $script:StateSchemaPath -Raw | ConvertFrom-Json
    $result = Test-JsonSchema -Data $State -Schema $schema
    if (-not $result.Valid) {
        throw "Intake state failed schema validation: $($result.Errors -join '; ')"
    }
}

function New-EmptyIntakeState {
    param([string] $RunId, [string] $ProviderId, [string] $SourceCommit, [int] $MaxIterations)
    $created = Get-UtcNow
    $openTask = Get-OpenTaskSummary
    [pscustomobject]@{
        schemaVersion   = 1
        runId           = $RunId
        createdAt       = $created
        updatedAt       = $created
        sourceCommit    = $SourceCommit
        provider        = $ProviderId
        phases          = @($script:RunPhases)
        currentPhase    = $script:RunPhases[0]
        completedSteps  = @()
        incompleteSteps = @($script:RunPhases)
        stepTimeoutsSeconds = @{ default = $TimeoutSeconds; build = 2400; test = 2400 }
        history         = @()
        iteration       = 1
        maxIterations   = $MaxIterations
        failures        = @()
        escalated       = $false
        externalBlockers = @()
        evidence        = @()
        openTaskCount   = $openTask
        lastOutcome     = @{ status = 'RUNNING'; exitCode = $ExitPass; updatedAt = $created }
    }
}

function Set-PhaseRunning {
    param([pscustomobject] $State, [string] $Phase, [string] $RunPath)
    $State.currentPhase = $Phase
    $State.history += [pscustomobject]@{
        step = $Phase; startedAt = Get-UtcNow; finishedAt = $null; outcome = 'RUNNING'; retries = 0; class = $null; notes = $null
    }
    Save-IntakeState -State $State -RunPath $RunPath
}

function Set-PhaseOutcome {
    param([pscustomobject] $State, [string] $Phase, [string] $Outcome, [string] $RunPath, $Class = $null, $Notes = $null)
    $entry = $State.history | Where-Object { $_.step -eq $Phase } | Select-Object -Last 1
    if ($entry) {
        $entry.finishedAt = Get-UtcNow
        $entry.outcome = $Outcome
        $entry.class = $Class
        $entry.notes = $Notes
    }
    if ($Outcome -eq 'PASS') {
        $State.completedSteps = @($State.completedSteps | Where-Object { $_ -ne $Phase })
        $State.completedSteps += $Phase
        $State.incompleteSteps = @($State.incompleteSteps | Where-Object { $_ -ne $Phase })
    }
    Save-IntakeState -State $State -RunPath $RunPath
}

function Add-HistoryEntry {
    param([pscustomobject] $State, [string] $Step, [string] $Outcome, $Class = $null, $Notes = $null, [int] $Retries = 0)
    $State.history += [pscustomobject]@{
        step = $Step; startedAt = Get-UtcNow; finishedAt = Get-UtcNow; outcome = $Outcome; retries = $Retries; class = $Class; notes = $Notes
    }
}

function Get-PhaseTimeout {
    param([string] $Phase)
    if ($Phase -in @('build', 'test')) { return 2400 }
    return $script:RunMaxTimeout
}

function Exit-Run {
    param([pscustomobject] $State, [string] $RunPath, [string] $Status, [int] $ExitCode, [string] $Message)
    $State.lastOutcome = @{ status = $Status; exitCode = $ExitCode; updatedAt = Get-UtcNow }
    Save-IntakeState -State $State -RunPath $RunPath
    Write-Host "$Message" -ForegroundColor $(if ($ExitCode -eq 0) { 'Green' } else { 'Red' })
    exit $ExitCode
}

# ---------------------------------------------------------------------------
# Phase implementations
# ---------------------------------------------------------------------------

function Invoke-PhaseDiscover {
    [CmdletBinding()]
    param([pscustomobject] $State, [string] $RunPath)
    $fingerprint = Get-EnvironmentFingerprint -Change $State.provider -ProviderId $State.provider
    $toolchain = $fingerprint.Toolchain
    $toolchainState = if ($toolchain) { 'present' } else { 'missing' }

    $mediaCandidates = @()
    foreach ($root in @('C:\VMs', 'C:\ISO', (Join-Path $env:USERPROFILE 'Downloads'), (Join-Path $env:USERPROFILE 'Desktop'), 'D:\', 'E:\', 'F:\')) {
        if (Test-Path -LiteralPath $root) {
            $mediaCandidates += Get-ChildItem -LiteralPath $root -Recurse -Depth 2 -Include *.iso, *.wim, *.esd -File -ErrorAction SilentlyContinue |
                Select-Object -First 20 FullName, Length
        }
    }
    $media = @($mediaCandidates | Where-Object { $_.Length -gt 10MB })
    $hasMedia = $media.Count -gt 0

    Set-NoteProperty $State 'discover' ([pscustomobject]@{
        fingerprint = $fingerprint
        toolchain = $toolchainState
        installMediaPresent = $hasMedia
        mediaCount = $media.Count
        media = @($media | Select-Object -First 5 FullName, Length)
        openTaskCount = $State.openTaskCount
    })
    Save-IntakeState -State $State -RunPath $RunPath
    return [pscustomobject]@{ ok = $true; summary = "discovered: toolchain=$toolchainState installMedia=$hasMedia openTasks=$($State.openTaskCount.total)" }
}

function Invoke-PhasePlan {
    [CmdletBinding()]
    param([pscustomobject] $State, [string] $RunPath)
    $worklist = @()
    $byChange = $State.openTaskCount.byChange
    $changeNames = @(if ($byChange -is [System.Collections.IDictionary]) { $byChange.Keys } else { $byChange.PSObject.Properties.Name })
    foreach ($changeName in $changeNames) {
        $open = if ($byChange -is [System.Collections.IDictionary]) { $byChange[$changeName] } else { $byChange.$changeName }
        $worklist += [pscustomobject]@{ change = $changeName; openTasks = $open; handled = ($open -gt 0) }
    }
    $plan = [pscustomobject]@{
        provider = $State.provider
        configurations = @($Configuration)
        worklist = @($worklist)
        capabilityWorklist = @(
            'windows-build-validation',
            'windows-protocol-robustness',
            'windows-resource-leak-validation',
            'windows-stress-validation',
            'windows-performance-baselines',
            'windows-engine-service-validation',
            'windows-ui-automation-validation',
            'test-code-signing',
            'diagnostics',
            'crash-analysis'
        )
        implementScript = $ImplementScript
    }
    $plan | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $RunPath 'plan.json') -Encoding utf8
    Set-NoteProperty $State 'plan' $plan
    Save-IntakeState -State $State -RunPath $RunPath
    return [pscustomobject]@{ ok = $true; summary = "plan: $($plan.capabilityWorklist.Count) capabilities, provider=$($plan.provider)" }
}

function Invoke-PhaseProvision {
    [CmdletBinding()]
    param([pscustomobject] $State, [string] $RunPath)
    $provider = Get-EnvironmentProvider -ProviderId $State.provider
    $fingerprint = Get-EnvironmentFingerprint -Change $State.provider -ProviderId $State.provider -Provider $provider
    $runContext = [pscustomobject]@{
        VerifyRoot = $VerifyRoot; Change = 'autonomous'; RunPath = $RunPath
        ManifestPath = Join-Path $RunPath 'manifest.json'; ArtifactsRoot = Join-Path $RunPath 'artifacts'
        SchemasRoot = Join-Path $VerifyRoot 'schemas'
    }
    $providerContext = [pscustomobject]@{
        RunContext = $runContext
        Fingerprint = $fingerprint
        RequiredTier = 0
        State = $null
    }

    $provision = Invoke-EnvironmentProviderLifecycle -Provider $provider -Phase provision -ProviderContext $providerContext
    $activate = $null
    try {
        $activate = Invoke-EnvironmentProviderLifecycle -Provider $provider -Phase activate -ProviderContext $providerContext
    } catch {
        $activate = [pscustomobject]@{ Error = $_.Exception.Message }
    }

    $providerStatus = [pscustomobject]@{
        provider = $State.provider
        ready = [bool]$provision.Ready
        reason = $provision.Reason
        mode = $provision.Mode
        activate = $activate
    }
    $providerStatus | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $RunPath 'provider-status.json') -Encoding utf8
    Set-NoteProperty $State 'providerStatus' $providerStatus

    if (-not $provision.Ready) {
        $blocker = [pscustomobject]@{ id = "provider-$($State.provider)-not-ready"; evidence = (Join-Path $RunPath 'provider-status.json'); detail = $provision.Reason; taskIds = @() }
        $State.externalBlockers += $blocker
        Save-IntakeState -State $State -RunPath $RunPath
        return [pscustomobject]@{ ok = $true; external = $true; summary = "provider $($State.provider) not ready: $($provision.Reason)" }
    }
    Save-IntakeState -State $State -RunPath $RunPath
    return [pscustomobject]@{ ok = $true; summary = "provider $($State.provider) ready (mode $($provision.Mode))" }
}

function Invoke-PhaseImplement {
    [CmdletBinding()]
    param([pscustomobject] $State, [string] $RunPath)
    $diffCheck = Invoke-BoundedProcess -FilePath 'git' -ArgumentList @('diff', '--check') -WorkingDirectory $RepoRoot -TimeoutSeconds 60 -LogPath (Join-Path $RunPath 'log-implement-diffcheck.txt')
    $notes = "git diff --check exit=$($diffCheck.exitCode); implementScript=$($ImplementScript) " + $(if ($ImplementScript) { '' } else { '(none)' })

    if ($ImplementScript) {
        $scriptPath = Join-Path $RepoRoot $ImplementScript
        if (-not (Test-Path -LiteralPath $scriptPath)) {
            return [pscustomobject]@{ ok = $false; class = 'ClassA'; summary = "implementScript not found: $scriptPath" }
        }
        $impl = Invoke-BoundedProcess -FilePath 'pwsh' -ArgumentList @('-NoProfile', '-File', $scriptPath) -WorkingDirectory $RepoRoot -TimeoutSeconds 600 -LogPath (Join-Path $RunPath 'log-implement.txt')
        $notes += "; implement exit=$($impl.exitCode)"
        if ($impl.exitCode -ne 0) {
            return [pscustomobject]@{ ok = $false; class = 'ClassB'; summary = "implement script failed: exit=$($impl.exitCode)" }
        }
    }
    if ($diffCheck.exitCode -ne 0) {
        return [pscustomobject]@{ ok = $false; class = 'ClassA'; summary = "git diff --check failed (whitespace errors): $($diffCheck.output)" }
    }
    Set-NoteProperty $State 'implement' ([pscustomobject]@{ notes = $notes; diffCheckExit = $diffCheck.exitCode })
    Save-IntakeState -State $State -RunPath $RunPath
    return [pscustomobject]@{ ok = $true; summary = "implement: $notes" }
}

function Invoke-PhaseBuild {
    [CmdletBinding()]
    param([pscustomobject] $State, [string] $RunPath)
    $fingerprint = Get-EnvironmentFingerprint -Change $State.provider -ProviderId $State.provider
    $toolchain = $fingerprint.Toolchain
    if (-not $toolchain) {
        return [pscustomobject]@{ ok = $false; class = 'ClassA'; summary = 'VS toolchain not found (build cannot run)' }
    }
    $cmake = $toolchain.CMakeExe
    $ninja = $toolchain.NinjaExe
    if (-not $cmake -or -not $ninja) {
        return [pscustomobject]@{ ok = $false; class = 'ClassA'; summary = 'bundled CMake/Ninja not found in toolchain' }
    }
    $env:FASTFILES_NINJA_EXE = $ninja

    # Activate the VS developer environment (vcvarsall) so child cmake/link/ctest
    # invocations inherit INCLUDE/LIB/PATH; without it the linker cannot resolve SDK
    # import libs (LNK1104: cannot open file 'kernel32.lib').
    try { Enter-DevEnvironment -Toolchain $toolchain } catch {
        return [pscustomobject]@{ ok = $false; class = 'ClassA'; summary = "developer-environment activation failed: $($_.Exception.Message)" }
    }

    $results = @()
    foreach ($cfg in $Configuration) {
        $cfgSecure = $cfg -replace '[^\w\-]', '_'
        $configure = Invoke-BoundedProcess -FilePath $cmake -ArgumentList @('--preset', $cfg) -WorkingDirectory $RepoRoot -TimeoutSeconds 900 -LogPath (Join-Path $RunPath "log-build-$cfgSecure-configure.txt")
        if ($configure.exitCode -ne 0) {
            $results += [pscustomobject]@{ config = $cfg; ok = $false; step = 'configure' }
            continue
        }
        $build = Invoke-BoundedProcess -FilePath $cmake -ArgumentList @('--build', '--preset', $cfg) -WorkingDirectory $RepoRoot -TimeoutSeconds 2400 -LogPath (Join-Path $RunPath "log-build-$cfgSecure.txt")
        $results += [pscustomobject]@{ config = $cfg; ok = ($build.exitCode -eq 0); step = 'build'; exitCode = $build.exitCode }
    }
    Set-NoteProperty $State 'build' ([pscustomobject]@{ results = $results })
    Save-IntakeState -State $State -RunPath $RunPath

    $failed = @($results | Where-Object { -not $_.ok })
    if ($failed.Count -gt 0) {
        return [pscustomobject]@{ ok = $false; class = 'ClassB'; summary = "build failed: $((@($failed | ForEach-Object { "$($_.config)/$($_.step)" }) -join ', '))" }
    }
    return [pscustomobject]@{ ok = $true; summary = "build ok: $($Configuration -join ', ')" }
}

function Invoke-PhaseTest {
    [CmdletBinding()]
    param([pscustomobject] $State, [string] $RunPath)
    $fingerprint = Get-EnvironmentFingerprint -Change $State.provider -ProviderId $State.provider
    $toolchain = $fingerprint.Toolchain
    $cmakeBin = Split-Path $toolchain.CMakeExe -Parent
    $ctest = Join-Path $cmakeBin 'ctest.exe'

    try { Enter-DevEnvironment -Toolchain $toolchain } catch {
        return [pscustomobject]@{ ok = $false; class = 'ClassA'; summary = "developer-environment activation failed: $($_.Exception.Message)" }
    }

    $results = @()
    foreach ($cfg in $Configuration) {
        $cfgSecure = $cfg -replace '[^\w\-]', '_'
        $logPath = Join-Path $RunPath "log-test-$cfgSecure.txt"
        $test = Invoke-BoundedProcess -FilePath $ctest -ArgumentList @('--test-dir', (Join-Path $RepoRoot "build\$cfg"), '--output-on-failure') -WorkingDirectory $RepoRoot -TimeoutSeconds 2400 -LogPath $logPath
        $percent = 0
        if ($test.output -match '(\d+)% tests passed') { $percent = [int]$Matches[1] }
        $results += [pscustomobject]@{ config = $cfg; ok = ($test.exitCode -eq 0); exitCode = $test.exitCode; percent = $percent; log = $logPath }
    }
    Set-NoteProperty $State 'test' ([pscustomobject]@{ results = $results })
    Save-IntakeState -State $State -RunPath $RunPath

    $failed = @($results | Where-Object { -not $_.ok })
    if ($failed.Count -gt 0) {
        return [pscustomobject]@{ ok = $false; class = 'ClassB'; summary = "test failed: $((@($failed | ForEach-Object { "$($_.config) ($($_.percent)%)" }) -join ', '))"; log = $failed[0].log }
    }
    return [pscustomobject]@{ ok = $true; summary = "test ok: $((@($results | ForEach-Object { "$($_.config) $($_.percent)%" }) -join ', '))" }
}

function Invoke-PhaseDiagnose {
    [CmdletBinding()]
    param([pscustomobject] $State, [string] $RunPath)
    $lastFailure = $State.failures | Select-Object -Last 1
    $class = 'ClassB'; $signature = 'unknown'; $message = ''
    if ($lastFailure) {
        $signature = $lastFailure.signature
        $class = $lastFailure.class
        $message = $lastFailure.message
    }
    $diagDir = Join-Path $RunPath 'diagnostics'
    New-Item -ItemType Directory -Path $diagDir -Force | Out-Null
    $diag = [pscustomobject]@{
        step = $State.currentPhase
        class = $class
        signature = $signature
        message = $message
        capturedAt = Get-UtcNow
    }
    $diag | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $diagDir 'diagnosis.json') -Encoding utf8
    Set-NoteProperty $State 'diagnose' $diag
    Save-IntakeState -State $State -RunPath $RunPath
    return [pscustomobject]@{ ok = $true; summary = "diagnosed: class=$class signature=$signature" }
}

function Invoke-PhaseRepair {
    [CmdletBinding()]
    param([pscustomobject] $State, [string] $RunPath)
    $lastFailure = $State.failures | Select-Object -Last 1
    if (-not $lastFailure) { return [pscustomobject]@{ ok = $true; summary = 'no failures to repair' } }

    if ($lastFailure.class -eq 'ClassB') {
        $surface = [pscustomobject]@{
            class = 'ClassB'; step = $lastFailure.step; signature = $lastFailure.signature
            message = $lastFailure.message; diagnosticsRef = $lastFailure.diagnosticsRef
            surfacedAt = Get-UtcNow; actionRequired = 'human review'
        }
        $surface | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $RunPath 'repair-surfaced-classb.json') -Encoding utf8
        return [pscustomobject]@{ ok = $false; class = 'ClassB'; summary = "Class B failure surfaced for review: $($lastFailure.signature)" }
    }
    if ($lastFailure.class -eq 'external') {
        return [pscustomobject]@{ ok = $true; summary = 'external blocker recorded; nothing to repair (see externalBlockers)' }
    }
    # Class A repair: clean the build output for the affected phase and retry.
    $fingerprint = Get-EnvironmentFingerprint -Change $State.provider -ProviderId $State.provider
    $toolchain = $fingerprint.Toolchain
    $cmake = $toolchain.CMakeExe
    $repairs = @()
    if ($lastFailure.step -in @('build', 'test')) {
        foreach ($cfg in $Configuration) {
            $clean = Invoke-BoundedProcess -FilePath $cmake -ArgumentList @('--build', '--preset', $cfg, '--target', 'clean') -WorkingDirectory $RepoRoot -TimeoutSeconds 600 -LogPath (Join-Path $RunPath 'log-repair-clean.txt')
            $repairs += "clean-$cfg exit=$($clean.exitCode)"
        }
    }
    $repairDoc = [pscustomobject]@{ class = 'ClassA'; step = $lastFailure.step; repairs = $repairs; appliedAt = Get-UtcNow }
    $repairDoc | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $RunPath 'repair-classa.json') -Encoding utf8
    Set-NoteProperty $State 'repair' $repairDoc
    Save-IntakeState -State $State -RunPath $RunPath
    return [pscustomobject]@{ ok = $true; class = 'ClassA'; summary = "applied Class A repair: $($repairs -join '; ')" }
}

function Invoke-PhaseByName {
    <#
    .SYNOPSIS
        Single dispatch for the 15 run phases. Used by the main loop and by the
        retry cycle's re-test so a failed phase is genuinely re-run (no no-op).
    #>
    [CmdletBinding()]
    param([string] $Phase, [pscustomobject] $State, [string] $RunPath)
    switch ($Phase) {
        'discover'          { return Invoke-PhaseDiscover -State $State -RunPath $RunPath }
        'plan'              { return Invoke-PhasePlan -State $State -RunPath $RunPath }
        'provision'         { return Invoke-PhaseProvision -State $State -RunPath $RunPath }
        'implement'         { return Invoke-PhaseImplement -State $State -RunPath $RunPath }
        'build'             { return Invoke-PhaseBuild -State $State -RunPath $RunPath }
        'test'              { return Invoke-PhaseTest -State $State -RunPath $RunPath }
        'diagnose'          { return Invoke-PhaseDiagnose -State $State -RunPath $RunPath }
        'repair'            { return Invoke-PhaseRepair -State $State -RunPath $RunPath }
        're-test'           { return Invoke-PhaseReTest -State $State -RunPath $RunPath }
        'validate'          { return Invoke-PhaseValidate -State $State -RunPath $RunPath }
        'collect-evidence'  { return Invoke-PhaseCollectEvidence -State $State -RunPath $RunPath }
        'update-tasks'      { return Invoke-PhaseUpdateTasks -State $State -RunPath $RunPath }
        'commit'            { return Invoke-PhaseCommit -State $State -RunPath $RunPath }
        'sync'              { return Invoke-PhaseSync -State $State -RunPath $RunPath }
        'archive'           { return Invoke-PhaseArchive -State $State -RunPath $RunPath }
        default             { throw "Unknown phase '$Phase'" }
    }
}

function Invoke-PhaseReTest {
    [CmdletBinding()]
    param([pscustomobject] $State, [string] $RunPath)
    $lastFailure = $State.failures | Select-Object -Last 1
    if (-not $lastFailure) { return [pscustomobject]@{ ok = $true; summary = 'nothing to re-test' } }
    $step = $lastFailure.step
    if ($step -in @('diagnose', 're-test')) { return [pscustomobject]@{ ok = $true; summary = "no re-test for meta step '$step'" } }
    # Re-invoke the failed phase itself so a commit/sync/other failure is really retried.
    return Invoke-PhaseByName -Phase $step -State $State -RunPath $RunPath

}

function Invoke-PhaseValidate {
    [CmdletBinding()]
    param([pscustomobject] $State, [string] $RunPath)
    # Reuse the harness's own run + gate verbs (design D1) rather than re-implement.
    $capList = @($State.plan.capabilityWorklist) -join ','
    $runCmd = Join-Path $VerifyRoot 'verify.ps1'
    $runLog = Join-Path $RunPath 'log-validate-run.txt'
    $runRes = Invoke-BoundedProcess -FilePath 'pwsh' -ArgumentList @('-NoProfile', '-File', $runCmd, 'run', '-Change', $Change, '-Provider', $State.provider, '-Capability', $capList) -WorkingDirectory $RepoRoot -TimeoutSeconds 2400 -LogPath $runLog
    # NOTE: verify.ps1 run returns exit 1 when ANY capability FAILs — including
    # capabilities that are NOT required by the gate policy. The gate verdict is the
    # arbiter, so a non-zero run exit is expected whenever a non-required capability
    # fails (e.g. the UIA capability while the D2D surface has no item providers).
    # We only fail the phase on a real gate FAIL verdict below.
    $runTree = $null
    if ($runRes.output -match 'Run tree:\s*(\S.*?)\s*[.:]?\s*$') { $runTree = $Matches[1] }
    elseif ($runRes.output -match 'Run tree:\s*(\S+)') { $runTree = $Matches[1] }
    if (-not $runTree) { $runTree = "verify/runs/$Change" }
    $runTree = $runTree.Trim()

    $latestRun = Get-ChildItem -Path (Join-Path $VerifyRoot "runs\$Change") -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1
    $gateRes = $null
    if ($latestRun) {
        $gateLog = Join-Path $RunPath 'log-validate-gate.txt'
        $gateRes = Invoke-BoundedProcess -FilePath 'pwsh' -ArgumentList @('-NoProfile', '-File', $runCmd, 'gate', '-Change', $Change, '-RunTimestamp', $latestRun.Name) -WorkingDirectory $RepoRoot -TimeoutSeconds 120 -LogPath $gateLog
    }
    $verdict = $null
    $verdictPath = Join-Path $VerifyRoot "runs\$Change\$($latestRun.Name)\gate-verdict.json"
    if (Test-Path -LiteralPath $verdictPath) { $verdict = Get-Content -LiteralPath $verdictPath -Raw | ConvertFrom-Json }
    elseif ($gateRes -and $gateRes.exitCode -eq 0) { $verdict = [pscustomobject]@{ Passed = $true; Verdicts = @(); UnrepresentedEdits = @() } }

    # Classify the gate outcome. Hard FAIL verdicts are real failures; verdicts that
    # resolved to REQUIRED-BUT-UNAVAILABLE are recorded as external blockers with machine
    # evidence (the four-state archive gate) and do not fail the run.
    $failVerdicts = @(if ($verdict.Verdicts) { $verdict.Verdicts | Where-Object { $_.verdict -eq 'FAIL' } } else { @() })
    $rbuVerdicts = @(if ($verdict.Verdicts) { $verdict.Verdicts | Where-Object { $_.verdict -eq 'REQUIRED-BUT-UNAVAILABLE' } } else { @() })
    $passed = [bool]$verdict.Passed
    foreach ($v in $rbuVerdicts) {
        $existing = @($State.externalBlockers | Where-Object { $_.id -eq "gate:$($v.capabilityId)" })
        if ($existing.Count -eq 0) {
            $State.externalBlockers += [pscustomobject]@{
                id = "gate:$($v.capabilityId)"
                evidence = $verdictPath
                detail = $v.reason
                taskIds = @()
            }
        }
    }
    Set-NoteProperty $State 'validate' ([pscustomobject]@{ runTree = $runTree; gateExit = $gateRes.exitCode; gatePassed = $passed; verdictPath = $verdictPath; failVerdicts = @($failVerdicts | ForEach-Object { $_.capabilityId }); rbuVerdicts = @($rbuVerdicts | ForEach-Object { $_.capabilityId }) })
    Save-IntakeState -State $State -RunPath $RunPath
    if (-not $passed -and $failVerdicts.Count -gt 0) {
        return [pscustomobject]@{ ok = $false; class = 'ClassB'; summary = "archive gate FAIL: $((@($failVerdicts | ForEach-Object { $_.capabilityId }) -join ', ')) ($verdictPath)" }
    }
    if ($rbuVerdicts.Count -gt 0) {
        return [pscustomobject]@{ ok = $true; external = $true; summary = "validate: gate resolved with $($rbuVerdicts.Count) REQUIRED-BUT-UNAVAILABLE verdict(s) recorded as external evidence ($verdictPath)" }
    }
    return [pscustomobject]@{ ok = $true; summary = "validate: gate PASS (run $runTree)" }
}

function Invoke-PhaseCollectEvidence {
    [CmdletBinding()]
    param([pscustomobject] $State, [string] $RunPath)
    $artifactsDir = Join-Path $RunPath 'artifacts'
    New-Item -ItemType Directory -Path $artifactsDir -Force | Out-Null
    foreach ($f in Get-ChildItem -Path $RunPath -File -Filter 'log-*.txt' -ErrorAction SilentlyContinue) {
        Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $artifactsDir $f.Name) -Force -ErrorAction SilentlyContinue
    }
    foreach ($f in @('gate-verdict.json', 'plan.json', 'provider-status.json', 'repair-classa.json', 'repair-surfaced-classb.json', 'diagnostics\diagnosis.json')) {
        $p = Join-Path $RunPath $f
        if (Test-Path -LiteralPath $p) { Copy-Item -LiteralPath $p -Destination (Join-Path $artifactsDir (Split-Path $f -Leaf)) -Force -ErrorAction SilentlyContinue }
    }
    $evidence = @()
    foreach ($hist in $State.history) {
        if ($hist.outcome -eq 'PASS') {
            $evidence += [pscustomobject]@{ taskId = "orchestrator:$($hist.step)"; status = 'PASS'; ref = (Join-Path $RunPath 'state.json'); reason = $null }
        }
    }
    foreach ($b in $State.externalBlockers) {
        $evidence += [pscustomobject]@{ taskId = $b.id; status = 'REQUIRED-BUT-UNAVAILABLE'; ref = $b.evidence; reason = $b.detail }
    }
    $evidence | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $RunPath 'evidence.json') -Encoding utf8
    $State.evidence = @($evidence)
    Save-IntakeState -State $State -RunPath $RunPath
    return [pscustomobject]@{ ok = $true; summary = "collect-evidence: $($evidence.Count) evidence records" }
}

function Invoke-PhaseUpdateTasks {
    [CmdletBinding()]
    param([pscustomobject] $State, [string] $RunPath)
    # Only PASS evidence produced by this run may close tasks (7.6: never on narrative
    # alone). Orchestrator phases are tracked in state.json; per-change tasks.md closings
    # are applied by the task-closing sweep with evidence references.
    $passEvidence = @($State.evidence | Where-Object { $_.status -eq 'PASS' })
    Set-NoteProperty $State 'updateTasks' ([pscustomobject]@{
        updated = $passEvidence.Count
        note = 'orchestrator phases tracked in state.json; tasks.md closings applied by the task-closing sweep with evidence refs'
    })
    Save-IntakeState -State $State -RunPath $RunPath
    return [pscustomobject]@{ ok = $true; summary = "update-tasks: $($passEvidence.Count) orchestrator-phase evidence records (no narrative-only closings)" }
}

function Invoke-PhaseCommit {
    [CmdletBinding()]
    param([pscustomobject] $State, [string] $RunPath)
    if ($SkipCommit) { return [pscustomobject]@{ ok = $true; summary = 'commit skipped (-SkipCommit)' } }
    $paths = @(
        'verify/intake.ps1',
        'verify/core/FlakyTestPolicy.psm1',
        'verify/autonomous/contract.json',
        'verify/autonomous/schemas/run-state.schema.json',
        'AUTONOMOUS.md',
        'AGENTS.md'
    )
    $existing = @($paths | Where-Object { Test-Path -LiteralPath (Join-Path $RepoRoot $_) })
    if ($existing.Count -eq 0) { return [pscustomobject]@{ ok = $true; summary = 'nothing new to commit' } }
    $add = Invoke-BoundedProcess -FilePath 'git' -ArgumentList @('add', '--', $existing) -WorkingDirectory $RepoRoot -TimeoutSeconds 60 -LogPath (Join-Path $RunPath 'log-commit-add.txt')
    if ($add.exitCode -ne 0) { return [pscustomobject]@{ ok = $false; class = 'ClassA'; summary = "git add failed: $($add.output)" } }
    $msg = "autonomous: add intake orchestrator + flaky policy + contract (run $($State.runId))"
    $commit = Invoke-BoundedProcess -FilePath 'git' -ArgumentList @('commit', '-m', $msg) -WorkingDirectory $RepoRoot -TimeoutSeconds 60 -LogPath (Join-Path $RunPath 'log-commit.txt')
    if ($commit.exitCode -ne 0) {
        if ($commit.output -match 'nothing to commit|no changes added') { return [pscustomobject]@{ ok = $true; summary = 'nothing to commit (already clean)' } }
        return [pscustomobject]@{ ok = $false; class = 'ClassA'; summary = "git commit failed: $($commit.output)" }
    }
    return [pscustomobject]@{ ok = $true; summary = "committed orchestrator delta (run $($State.runId))" }
}

function Invoke-PhaseSync {
    [CmdletBinding()]
    param([pscustomobject] $State, [string] $RunPath)
    $remote = Invoke-BoundedProcess -FilePath 'git' -ArgumentList @('remote', 'get-url', 'origin') -WorkingDirectory $RepoRoot -TimeoutSeconds 30 -LogPath (Join-Path $RunPath 'log-sync-remote.txt')
    $hasRemote = ($remote.exitCode -eq 0 -and $remote.output.Trim() -ne '')
    if (-not $hasRemote) {
        Set-NoteProperty $State 'sync' ([pscustomobject]@{ pushed = $false; reason = 'no-authenticated-remote' })
        Save-IntakeState -State $State -RunPath $RunPath
        return [pscustomobject]@{ ok = $true; summary = 'sync: no remote configured (SKIPPED, recorded)' }
    }
    if (-not $AllowPush) {
        Set-NoteProperty $State 'sync' ([pscustomobject]@{ pushed = $false; reason = 'push-not-opted-in' })
        Save-IntakeState -State $State -RunPath $RunPath
        return [pscustomobject]@{ ok = $true; summary = 'sync: remote available but push not opted-in (-AllowPush) (SKIPPED, recorded)' }
    }
    $push = Invoke-BoundedProcess -FilePath 'git' -ArgumentList @('push', 'origin', 'HEAD') -WorkingDirectory $RepoRoot -TimeoutSeconds 120 -LogPath (Join-Path $RunPath 'log-sync-push.txt')
    Set-NoteProperty $State 'sync' ([pscustomobject]@{ pushed = ($push.exitCode -eq 0); reason = if ($push.exitCode -eq 0) { 'pushed' } else { $push.output } })
    Save-IntakeState -State $State -RunPath $RunPath
    if ($push.exitCode -ne 0) { return [pscustomobject]@{ ok = $false; class = 'ClassA'; summary = "sync: push failed: $($push.output)" } }
    return [pscustomobject]@{ ok = $true; summary = 'sync: pushed' }
}

function Invoke-PhaseArchive {
    [CmdletBinding()]
    param([pscustomobject] $State, [string] $RunPath)
    $hasFail = @($State.history | Where-Object { $_.outcome -eq 'FAIL' }).Count -gt 0
    $hasRunBlocks = $State.externalBlockers.Count -gt 0
    $hasPass = @($State.history | Where-Object { $_.outcome -eq 'PASS' }).Count -gt 0

    if ($hasFail -or $State.escalated) { $status = 'FAIL'; $exit = $ExitFail }
    elseif ($hasPass -and -not $hasRunBlocks) { $status = 'PASS'; $exit = $ExitPass }
    elseif ($hasPass -and $hasRunBlocks) { $status = 'REQUIRED-BUT-UNAVAILABLE'; $exit = $ExitPass }
    else { $status = 'SKIPPED'; $exit = $ExitSkipped }

    $State.lastOutcome = @{ status = $status; exitCode = $exit; updatedAt = Get-UtcNow }
    $archive = [pscustomobject]@{
        status = $status; exitCode = $exit; externalBlockers = $State.externalBlockers.Count
        completedPhases = $State.completedSteps.Count; totalPhases = $script:RunPhases.Count; archivedAt = Get-UtcNow
    }
    $archive | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $RunPath 'ARCHIVED.json') -Encoding utf8
    Save-IntakeState -State $State -RunPath $RunPath
    return [pscustomobject]@{ ok = $true; summary = "archive: $status (exit $exit)" }
}

# ---------------------------------------------------------------------------
# State loading / resume
# ---------------------------------------------------------------------------

function Resolve-IntakeRun {
    param([string] $RequestedRunId, [switch] $Fresh, [switch] $AllowNew)
    if ($RequestedRunId) {
        $runPath = Join-Path $script:AutonomousRoot $RequestedRunId
        if (-not (Test-Path -LiteralPath (Join-Path $runPath 'state.json'))) {
            throw "Run not found: $RequestedRunId (no state.json under $script:AutonomousRoot)"
        }
        $state = Get-Content -LiteralPath (Join-Path $runPath 'state.json') -Raw | ConvertFrom-Json
        return [pscustomobject]@{ State = $state; RunPath = $runPath; IsNew = $false }
    }
    if (-not $Fresh) {
        $existing = Get-LatestIncompleteRun
        if ($existing) {
            $runPath = Join-Path $script:AutonomousRoot $existing.runId
            return [pscustomobject]@{ State = $existing; RunPath = $runPath; IsNew = $false }
        }
    }
    if (-not $AllowNew) {
        throw 'No autonomous run exists yet. Use `intake.ps1 autonomous` to start one, or pass -RunId.'
    }
    $runId = Get-RunIdFromTimestamp -Now (Get-Date)
    $runPath = Join-Path $script:AutonomousRoot $runId
    $state = New-EmptyIntakeState -RunId $runId -ProviderId $Provider -SourceCommit (Get-GitHead) -MaxIterations $MaxIterations
    New-Item -ItemType Directory -Path $runPath -Force | Out-Null
    Save-IntakeState -State $state -RunPath $runPath
    return [pscustomobject]@{ State = $state; RunPath = $runPath; IsNew = $true }
}

# ---------------------------------------------------------------------------
# Entry points
# ---------------------------------------------------------------------------

function Invoke-AutonomousRun {
    $resolved = Resolve-IntakeRun -RequestedRunId $RunId -Fresh:$Fresh -AllowNew:$true
    $state = $resolved.State
    $runPath = $resolved.RunPath
    Write-Host "Autonomous run: $($state.runId) (provider=$($state.provider), fresh=$($resolved.IsNew))" -ForegroundColor Cyan
    $countNames = @(if ($state.openTaskCount.byChange -is [System.Collections.IDictionary]) { $state.openTaskCount.byChange.Keys } else { $state.openTaskCount.byChange.PSObject.Properties.Name })
    Write-Host "Open tasks: $($state.openTaskCount.total) from $(@($countNames).Count) changes" -ForegroundColor Cyan

    if ($state.escalated) { Exit-Run -State $state -RunPath $runPath -Status 'ESCALATED' -ExitCode $ExitFail -Message 'Run is ESCALATED; refusing to continue (see state.json).' }
    if ($state.iteration -gt $state.maxIterations) { Exit-Run -State $state -RunPath $runPath -Status 'FAIL' -ExitCode $ExitFail -Message "Iteration cap reached ($($state.maxIterations)); refusing to continue." }

    foreach ($phase in $script:RunPhases) {
        if ($phase -in $state.completedSteps) { continue }
        $state.currentPhase = $phase
        Set-PhaseRunning -State $state -Phase $phase -RunPath $runPath

        $result = Invoke-PhaseByName -Phase $phase -State $state -RunPath $runPath

        if ($result.ok) {
            Set-PhaseOutcome -State $state -Phase $phase -Outcome 'PASS' -RunPath $runPath -Notes $result.summary
            Write-Host "  [PASS] $phase :: $($result.summary)" -ForegroundColor Green
            continue
        }

        # --- Failure path: bounded retry cycle (diagnose -> repair -> re-test) ---
        $attemptClass = if ($result.class) { $result.class } else { 'ClassB' }
        $attempts = 0
        $recovered = $false
        while ($attempts -lt $MaxRetries -and -not $state.escalated) {
            $attempts++
            $histEntry = $state.history | Where-Object { $_.step -eq $phase } | Select-Object -Last 1
            if ($histEntry) { $histEntry.retries = $attempts }
            $state.failures += [pscustomobject]@{
                step = $phase; iteration = $state.iteration; class = $attemptClass
                signature = $result.summary; message = $result.summary; diagnosticsRef = $result.log
            }
            Set-PhaseOutcome -State $state -Phase $phase -Outcome 'FAIL' -RunPath $runPath -Class $attemptClass -Notes $result.summary

            if ($attemptClass -eq 'ClassB') {
                Invoke-PhaseDiagnose -State $state -RunPath $runPath | Out-Null
                Invoke-PhaseRepair -State $state -RunPath $runPath | Out-Null   # writes repair-surfaced-classb.json
                $state.escalated = $true
                break
            }

            Invoke-PhaseDiagnose -State $state -RunPath $runPath | Out-Null
            $repair = Invoke-PhaseRepair -State $state -RunPath $runPath
            if (-not $repair.ok) { $state.escalated = $true; break }

            $retest = Invoke-PhaseReTest -State $state -RunPath $runPath
            Add-HistoryEntry -State $state -Step 're-test' -Outcome $(if ($retest.ok) { 'PASS' } else { 'FAIL' }) -Class $attemptClass -Notes $retest.summary -Retries $attempts
            if ($retest.ok) {
                # Mark the original phase PASS (recovered via retry).
                $orig = $state.history | Where-Object { $_.step -eq $phase } | Select-Object -Last 1
                if ($orig) { $orig.outcome = 'PASS'; $orig.finishedAt = Get-UtcNow; $orig.notes = "recovered after $attempts retry(s): $($retest.summary)" }
                $state.completedSteps = @($state.completedSteps | Where-Object { $_ -ne $phase }) + $phase
                $state.incompleteSteps = @($state.incompleteSteps | Where-Object { $_ -ne $phase })
                $recovered = $true
                break
            }
            # Same normalized signature recurring -> escalate (spec: stop on recurring failure).
            if ($retest.summary -eq $result.summary) { $state.escalated = $true; break }
            $result = $retest
            $attemptClass = if ($retest.class) { $retest.class } else { 'ClassB' }
            $state.iteration++
            Save-IntakeState -State $state -RunPath $runPath
        }

        if ($recovered) {
            Save-IntakeState -State $state -RunPath $runPath
            Write-Host "  [PASS] $phase :: recovered after retry" -ForegroundColor Green
            continue
        }
        Exit-Run -State $state -RunPath $runPath -Status 'FAIL' -ExitCode $ExitFail -Message "Run failed at phase '$phase' (class=$attemptClass). See state.json for diagnostics."
    }

    $final = $state.history | Where-Object { $_.step -eq 'archive' } | Select-Object -Last 1
    $archiveStatus = if ($final) { $final.outcome } else { $state.lastOutcome.status }
    Write-Host "Autonomous run complete: $($state.lastOutcome.status) (exit $($state.lastOutcome.exitCode))" -ForegroundColor Green
    exit $state.lastOutcome.exitCode
}

function Invoke-StatusQuery {
    param([string] $RequestedRunId)
    $resolved = Resolve-IntakeRun -RequestedRunId $RequestedRunId -Fresh:$false -AllowNew:$false
    $state = $resolved.State
    $openToday = Get-OpenTaskSummary
    $status = [pscustomobject]@{
        runId = $state.runId
        currentPhase = $state.currentPhase
        completedSteps = @($state.completedSteps)
        remainingSteps = @($state.incompleteSteps)
        iteration = $state.iteration
        maxIterations = $state.maxIterations
        escalated = $state.escalated
        openTaskCount = $openToday
        lastOutcome = $state.lastOutcome
        runPath = $resolved.RunPath
    }
    $status | ConvertTo-Json -Depth 10
    exit $ExitPass
}

function Invoke-ArchiveGate {
    param([string] $RequestedRunId)
    $resolved = Resolve-IntakeRun -RequestedRunId $RequestedRunId -Fresh:$false -AllowNew:$false
    $state = $resolved.State
    $archivePath = Join-Path $resolved.RunPath 'ARCHIVED.json'
    if (Test-Path -LiteralPath $archivePath) {
        $archive = Get-Content -LiteralPath $archivePath -Raw | ConvertFrom-Json
        $archive | ConvertTo-Json -Depth 5
        Write-Host "Archive gate: $($archive.status) (exit $($archive.exitCode))"
        exit $archive.exitCode
    }
    Write-Host "Run not archived yet: $($state.runId) (current phase $($state.currentPhase))"
    exit $ExitFail
}

switch ($Verb) {
    'autonomous' { Invoke-AutonomousRun }
    'status'     { Invoke-StatusQuery -RequestedRunId $RunId }
    'archive-gate' { Invoke-ArchiveGate -RequestedRunId $RunId }
}