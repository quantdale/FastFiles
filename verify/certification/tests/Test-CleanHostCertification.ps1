[CmdletBinding()]
param(
    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
)

$ErrorActionPreference = 'Stop'

Write-Host "Running Test-CleanHostCertification.ps1..." -ForegroundColor Cyan

$script:runner = Join-Path $RepoRoot 'verify\certification\Invoke-CleanHostCertification.ps1'
$schemaPath = Join-Path $RepoRoot 'verify\certification\schemas\certification-result.schema.json'
$pythonExe = (Get-Command python).Source
$winPsExe = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'

if (-not (Test-Path -LiteralPath $script:runner)) { throw "Runner not found at $script:runner" }
if (-not (Test-Path -LiteralPath $schemaPath)) { throw "Schema not found at $schemaPath" }
if (-not (Test-Path -LiteralPath $winPsExe -PathType Leaf)) { throw "Fixture host not found at $winPsExe" }

$isElevated = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
$hostContaminated = ((@(Get-Process -Name 'FastFiles*' -ErrorAction SilentlyContinue)).Count -gt 0) -or ($null -ne (Get-Service -Name 'FastFilesIndexSvc' -ErrorAction SilentlyContinue))
Write-Host "  Environment: elevated=$isElevated, hostContaminated=$hostContaminated"

$script:failedTests = @()
$script:testDirs = [System.Collections.Generic.List[string]]::new()
$script:resultFiles = [System.Collections.Generic.List[string]]::new()

$journeyLegIds = @(
    'install-artifact-exists', 'install-artifact-hash', 'install-artifact-provenance',
    'install-precondition', 'install-privilege-elevation', 'install-invoke',
    'install-exit-status', 'install-files-created', 'install-launch',
    'install-identity', 'install-readiness', 'install-smoke',
    'install-shutdown', 'install-orphans', 'install-uninstall-removal',
    'install-residue', 'install-classification', 'install-rollback-visibility'
)

function Invoke-NamedTest {
    param([string] $Name, [scriptblock] $Body)
    Write-Host "  Test: $Name..."
    try {
        & $Body
        Write-Host "    PASS" -ForegroundColor Green
    } catch {
        Write-Host "    FAIL: $($_.Exception.Message)" -ForegroundColor Red
        $script:failedTests += $Name
    }
}

function Assert-True {
    param($Condition, [string] $Message)
    if (-not $Condition) { throw $Message }
}

function Test-ResultSchema {
    param([string] $JsonPath)
    $pyCmd = "import json, jsonschema; s = json.load(open(r'$schemaPath')); d = json.load(open(r'$JsonPath')); jsonschema.validate(instance=d, schema=s); print('SCHEMA-OK')"
    $val = & $pythonExe -c $pyCmd 2>&1
    if ($val -notmatch 'SCHEMA-OK') { throw "Schema validation failed: $val" }
}

function Get-ResultCheck {
    param($Json, [string] $Id)
    $c = @($Json.checks | Where-Object { $_.checkId -eq $Id })
    if ($c.Count -eq 0) { throw "Expected check '$Id' not present in result" }
    return $c[0]
}

function Assert-CommonContract {
    param($Json, [string] $RawText, [string] $TestName)
    Assert-True ($Json.lane -eq 'clean-host') "$TestName`: expected lane clean-host, got $($Json.lane)"
    Assert-True ($null -ne $Json.status) "$TestName`: missing status"
    Assert-True ($null -ne $Json.disposition) "$TestName`: missing disposition"
    Assert-True ($null -ne $Json.cleanup) "$TestName`: missing cleanup object"
    Assert-True (@('PASS', 'FAIL') -contains $Json.cleanup.status) "$TestName`: cleanup.status must be PASS/FAIL, got $($Json.cleanup.status)"
    Assert-True ($RawText -match '"capabilities"\s*:\s*\[') "$TestName`: missing capabilities array in result JSON"
    Assert-True ($Json.checks.Count -ge 3) "$TestName`: expected at least 3 checks, got $($Json.checks.Count)"
}

function New-CampaignTestDir {
    $d = Join-Path ([IO.Path]::GetTempPath()) ("FFCHTest-" + [Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path $d -Force | Out-Null
    $script:testDirs.Add($d)
    return $d
}

function New-FakeExe {
    param([string] $Dir, [string] $Name)
    $dest = Join-Path $Dir $Name
    Copy-Item -LiteralPath $winPsExe -Destination $dest -Force
    return $dest
}

function Invoke-CertRunner {
    param(
        [switch] $InstallCandidate,
        [switch] $AcknowledgeHostContamination,
        [string] $CandidatePath,
        [string] $InstallerArgs,
        [string] $LaunchArgs,
        [string] $InstalledExePath,
        [string] $UninstallCommand
    )
    $out = Join-Path ([IO.Path]::GetTempPath()) ("FFCHResult-" + [Guid]::NewGuid().ToString("N") + ".json")
    $script:resultFiles.Add($out)
    # In-process invocation: hashtable splatting keeps dash-leading argument
    # values (e.g. '-NoProfile ...') intact with no native argv re-quoting.
    $psArgs = @{
        OutputPath       = $out
        RepoRoot         = $RepoRoot
        TimeoutSec       = 25
        ShutdownGraceSec = 2
    }
    if ($InstallCandidate) { $psArgs['InstallCandidate'] = $true }
    if ($AcknowledgeHostContamination) { $psArgs['AcknowledgeHostContamination'] = $true }
    if ($CandidatePath) { $psArgs['CandidatePath'] = $CandidatePath }
    if ($InstallerArgs) { $psArgs['InstallerArgs'] = $InstallerArgs }
    if ($LaunchArgs) { $psArgs['LaunchArgs'] = $LaunchArgs }
    if ($InstalledExePath) { $psArgs['InstalledExePath'] = $InstalledExePath }
    if ($UninstallCommand) { $psArgs['UninstallCommand'] = $UninstallCommand }
    $all = @( & $script:runner @psArgs )
    if (-not (Test-Path -LiteralPath $out)) { throw "Runner produced no JSON artifact" }
    Test-ResultSchema -JsonPath $out
    $raw = Get-Content -LiteralPath $out -Raw
    return [pscustomobject]@{ Json = ($raw | ConvertFrom-Json); Raw = $raw; Path = $out }
}

function Clear-StaleCampaignProcesses {
    # Best-effort hygiene: reap leftovers of earlier aborted runs. Only images
    # inside campaign-owned temp dirs are ever touched.
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
    foreach ($proc in @(Get-Process -Name 'FastFiles*' -ErrorAction SilentlyContinue)) {
        try { $img = $proc.MainModule.FileName } catch { continue }
        if (-not $img) { continue }
        $full = [IO.Path]::GetFullPath($img)
        if ($full.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and ((Split-Path -Parent $full) -match 'FastFilesCleanHost-')) {
            try { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } catch {}
        }
    }
}

function Clear-StagedLeftovers {
    param([string] $StagedDir)
    if ([string]::IsNullOrWhiteSpace($StagedDir)) { return }
    $prefix = ([IO.Path]::GetFullPath($StagedDir).TrimEnd('\', '/') + '\')
    foreach ($proc in @(Get-Process -ErrorAction SilentlyContinue)) {
        try { $img = $proc.MainModule.FileName } catch { continue }
        if ($img -and $img.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
            try { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } catch {}
        }
    }
    Start-Sleep -Seconds 2
    if (Test-Path -LiteralPath $StagedDir) {
        Remove-Item -LiteralPath $StagedDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

try {
    Clear-StaleCampaignProcesses

    # 1. Audit-only can never imply an install PASS.
    Invoke-NamedTest -Name 'AuditOnly-CannotImplyInstallPass' -Body {
        $r = Invoke-CertRunner
        Assert-CommonContract -Json $r.Json -RawText $r.Raw -TestName 'audit'
        Assert-True ($r.Json.status -ne 'PASS') "audit-only returned product PASS status"
        Assert-True ($r.Json.disposition -ne 'PASS') "audit-only returned product PASS disposition"
        Assert-True ($r.Json.details.executionProof -eq $false) "audit must not claim execution proof"
        $caps = @($r.Json.capabilities)
        Assert-True (-not ($caps -contains 'clean-install')) "audit must not advertise clean-install"
        Assert-True (-not ($caps -contains 'installed-runtime')) "audit must not advertise installed-runtime"
        Assert-True (-not ($caps -contains 'uninstall-cleanup')) "audit must not advertise uninstall-cleanup"
        Assert-True ($r.Json.cleanup.status -eq 'PASS') "audit cleanup must be PASS, got $($r.Json.cleanup.status)"
        $skip = Get-ResultCheck -Json $r.Json -Id 'candidate-installation'
        Assert-True ($skip.status -eq 'SKIPPED') "candidate-installation must be SKIPPED in audit mode"
        if ($hostContaminated) {
            Assert-True ($r.Json.status -eq 'BLOCKED_ENVIRONMENT') "contaminated audit expected BLOCKED_ENVIRONMENT, got $($r.Json.status)"
            Assert-True ($r.Json.details.auditOutcome -eq 'AUDIT_BLOCKED_ENVIRONMENT') "expected AUDIT_BLOCKED_ENVIRONMENT, got $($r.Json.details.auditOutcome)"
        } elseif (-not $isElevated) {
            Assert-True ($r.Json.status -eq 'BLOCKED_NOT_ELEVATED') "unelevated audit expected BLOCKED_NOT_ELEVATED, got $($r.Json.status)"
        } else {
            Assert-True ($r.Json.status -eq 'REQUIRED-BUT-UNAVAILABLE') "audit-ready expected REQUIRED-BUT-UNAVAILABLE, got $($r.Json.status)"
            Assert-True ($r.Json.details.auditOutcome -eq 'AUDIT_READY') "expected auditOutcome AUDIT_READY, got $($r.Json.details.auditOutcome)"
        }
    }

    # 2. Install work on a contaminated host is refused without acknowledgment.
    Invoke-NamedTest -Name 'ContaminationGate-RefusesInstall' -Body {
        $d = New-CampaignTestDir
        $probeExe = New-FakeExe -Dir $d -Name 'FastFilesGateProbe.exe'
        $probe = Start-Process -FilePath $probeExe -ArgumentList '-NoProfile', '-NonInteractive', '-Command', 'Start-Sleep -Seconds 60' -PassThru
        try {
            Start-Sleep -Seconds 1
            $r = Invoke-CertRunner -InstallCandidate -CandidatePath $probeExe
            Assert-CommonContract -Json $r.Json -RawText $r.Raw -TestName 'contamination-gate'
            Assert-True ($r.Json.status -eq 'BLOCKED_ENVIRONMENT') "unacknowledged contaminated install must be BLOCKED_ENVIRONMENT, got $($r.Json.status)"
            Assert-True ($r.Json.disposition -ne 'PASS') "refused install disposition must never be PASS"
            Assert-True ($r.Json.details.executionProof -eq $false) "refused install must not claim execution proof"
            $gate = Get-ResultCheck -Json $r.Json -Id 'host-conflict-audit'
            Assert-True ($gate.status -eq 'BLOCKED_ENVIRONMENT') "conflict audit must stay BLOCKED_ENVIRONMENT"
        } finally {
            try { Stop-Process -Id $probe.Id -Force -ErrorAction SilentlyContinue } catch {}
        }
    }

    # 3. Install without a resolvable candidate fails closed, never PASS.
    Invoke-NamedTest -Name 'InstallWithoutCandidate-FailsClosed' -Body {
        $r = Invoke-CertRunner -InstallCandidate -AcknowledgeHostContamination
        Assert-CommonContract -Json $r.Json -RawText $r.Raw -TestName 'missing-candidate'
        Assert-True ($r.Json.status -eq 'FAIL') "missing candidate must be FAIL, got $($r.Json.status)"
        Assert-True ($r.Json.disposition -eq 'FAIL') "missing candidate disposition must be FAIL"
        Assert-True ($r.Json.failureReason -match 'BLOCKED_MISSING_INSTALLABLE_CANDIDATE') "failureReason must carry BLOCKED_MISSING_INSTALLABLE_CANDIDATE, got $($r.Json.failureReason)"
        Assert-True ($r.Json.details.executionProof -eq $false) "no execution proof may exist without a candidate"
        $exists = Get-ResultCheck -Json $r.Json -Id 'install-artifact-exists'
        Assert-True ($exists.status -eq 'FAIL') "install-artifact-exists must be FAIL"

        $bogus = Join-Path ([IO.Path]::GetTempPath()) ("FFCHTest-does-not-exist-" + [Guid]::NewGuid().ToString("N") + ".exe")
        $r2 = Invoke-CertRunner -InstallCandidate -AcknowledgeHostContamination -CandidatePath $bogus
        Assert-True ($r2.Json.status -eq 'FAIL') "bogus candidate path must be FAIL"
        Assert-True ($r2.Json.failureReason -match 'BLOCKED_MISSING_INSTALLABLE_CANDIDATE') "bogus path must carry BLOCKED_MISSING_INSTALLABLE_CANDIDATE"
        Assert-True ($r2.Json.details.executionProof -eq $false) "no execution proof may exist for bogus candidate"
    }

    # 4. A real staged journey with execution proof passes and advertises capabilities.
    Invoke-NamedTest -Name 'GreenPortableJourney-PassesWithExecutionProof' -Body {
        $d = New-CampaignTestDir
        $cand = New-FakeExe -Dir $d -Name 'FastFiles.exe'
        $r = Invoke-CertRunner -InstallCandidate -AcknowledgeHostContamination -CandidatePath $cand -LaunchArgs '-NoProfile -NonInteractive -Command "Start-Sleep -Seconds 15"'
        Assert-CommonContract -Json $r.Json -RawText $r.Raw -TestName 'green'
        Assert-True ($r.Json.status -eq 'PASS') "green journey must be PASS, got $($r.Json.status) ($($r.Json.failureReason))"
        Assert-True ($r.Json.disposition -eq 'PASS') "green disposition must be PASS"
        Assert-True ($r.Json.details.executionProof -eq $true) "green journey must record execution proof"
        foreach ($legId in $journeyLegIds) {
            $c = Get-ResultCheck -Json $r.Json -Id $legId
            Assert-True ($c.status -eq 'PASS') "leg $legId must be PASS, got $($c.status): $($c.message)"
        }
        $caps = @($r.Json.capabilities)
        Assert-True (($caps -contains 'clean-install') -and ($caps -contains 'installed-runtime') -and ($caps -contains 'uninstall-cleanup')) "install mode must advertise all three capabilities, got $($caps -join ',')"
        Assert-True ($r.Json.cleanup.status -eq 'PASS') "green cleanup must be PASS"
        Assert-True ($r.Json.cleanup.stagedDirRemoved -eq $true) "staged dir must be removed"
        Assert-True (-not (Test-Path -LiteralPath $r.Json.details.stagedDir)) "staged dir must be gone from disk"
        Assert-True (Test-Path -LiteralPath $cand -PathType Leaf) "runner must not delete the operator-supplied candidate"
    }

    # 5. Installer failure is FAIL (third-party installers still need elevation).
    Invoke-NamedTest -Name 'InstallerFailure-IsFail' -Body {
        $d = New-CampaignTestDir
        $setup = New-FakeExe -Dir $d -Name 'FakeSetup.exe'
        $r = Invoke-CertRunner -InstallCandidate -AcknowledgeHostContamination -CandidatePath $setup -InstallerArgs '-NoProfile -NonInteractive -Command "exit 3"'
        Assert-CommonContract -Json $r.Json -RawText $r.Raw -TestName 'installer-fail'
        if (-not $isElevated) {
            Assert-True ($r.Json.status -eq 'BLOCKED_NOT_ELEVATED') "unelevated installer run must be BLOCKED_NOT_ELEVATED, got $($r.Json.status)"
            Assert-True ($r.Json.details.executionProof -eq $false) "blocked run must not claim execution proof"
        } else {
            Assert-True ($r.Json.status -eq 'FAIL') "installer failure must be FAIL, got $($r.Json.status)"
            Assert-True ($r.Json.disposition -eq 'FAIL') "installer failure disposition must be FAIL"
            $exit = Get-ResultCheck -Json $r.Json -Id 'install-exit-status'
            Assert-True ($exit.status -eq 'FAIL') "install-exit-status must be FAIL, got $($exit.status)"
            Assert-True ($r.Json.details.executionProof -eq $true) "installer was really executed; proof must be recorded"
        }
    }

    # 6. Installer success without a discoverable runtime is FAIL (runtime-not-found).
    Invoke-NamedTest -Name 'RuntimeNotFound-IsFail' -Body {
        $d = New-CampaignTestDir
        $setup = New-FakeExe -Dir $d -Name 'FakeSetup.exe'
        $r = Invoke-CertRunner -InstallCandidate -AcknowledgeHostContamination -CandidatePath $setup -InstallerArgs '-NoProfile -NonInteractive -Command "exit 0"'
        Assert-CommonContract -Json $r.Json -RawText $r.Raw -TestName 'runtime-not-found'
        if (-not $isElevated) {
            Assert-True ($r.Json.status -eq 'BLOCKED_NOT_ELEVATED') "unelevated installer run must be BLOCKED_NOT_ELEVATED, got $($r.Json.status)"
        } else {
            Assert-True ($r.Json.status -eq 'FAIL') "runtime-not-found must be FAIL, got $($r.Json.status)"
            $launch = Get-ResultCheck -Json $r.Json -Id 'install-launch'
            Assert-True ($launch.status -eq 'FAIL') "install-launch must be FAIL"
            Assert-True ($launch.message -match 'runtime-not-found') "launch failure must be classified runtime-not-found, got: $($launch.message)"
        }
    }

    # 7. Launch failure (non-executable candidate) is FAIL.
    Invoke-NamedTest -Name 'LaunchFailure-IsFail' -Body {
        $d = New-CampaignTestDir
        $cand = Join-Path $d 'FastFiles.exe'
        New-Item -ItemType File -Path $cand -Force | Out-Null
        $r = Invoke-CertRunner -InstallCandidate -AcknowledgeHostContamination -CandidatePath $cand
        Assert-CommonContract -Json $r.Json -RawText $r.Raw -TestName 'launch-fail'
        Assert-True ($r.Json.status -eq 'FAIL') "launch failure must be FAIL, got $($r.Json.status)"
        Assert-True ($r.Json.disposition -eq 'FAIL') "launch failure disposition must be FAIL"
        $launch = Get-ResultCheck -Json $r.Json -Id 'install-launch'
        Assert-True ($launch.status -eq 'FAIL') "install-launch must be FAIL, got $($launch.status)"
    }

    # 8. Uninstaller failure is FAIL.
    Invoke-NamedTest -Name 'UninstallFailure-IsFail' -Body {
        $d = New-CampaignTestDir
        $setup = New-FakeExe -Dir $d -Name 'FakeSetup.exe'
        $app = New-FakeExe -Dir $d -Name 'FakeApp.exe'
        $r = Invoke-CertRunner -InstallCandidate -AcknowledgeHostContamination -CandidatePath $setup `
            -InstallerArgs '-NoProfile -NonInteractive -Command "exit 0"' `
            -InstalledExePath $app `
            -LaunchArgs '-NoProfile -NonInteractive -Command "Start-Sleep -Seconds 15"' `
            -UninstallCommand 'exit 5'
        Assert-CommonContract -Json $r.Json -RawText $r.Raw -TestName 'uninstall-fail'
        if (-not $isElevated) {
            Assert-True ($r.Json.status -eq 'BLOCKED_NOT_ELEVATED') "unelevated installer run must be BLOCKED_NOT_ELEVATED, got $($r.Json.status)"
        } else {
            Assert-True ($r.Json.status -eq 'FAIL') "uninstall failure must be FAIL, got $($r.Json.status)"
            $un = Get-ResultCheck -Json $r.Json -Id 'install-uninstall-removal'
            Assert-True ($un.status -eq 'FAIL') "install-uninstall-removal must be FAIL, got $($un.status)"
        }
    }

    # 9. Residue after a no-op uninstall is FAIL, and files outside the staged
    # dir are never deleted (destructive-operation guardrail evidence).
    Invoke-NamedTest -Name 'Residue-IsFail' -Body {
        $d = New-CampaignTestDir
        $setup = New-FakeExe -Dir $d -Name 'FakeSetup.exe'
        $app = New-FakeExe -Dir $d -Name 'FakeApp.exe'
        $r = Invoke-CertRunner -InstallCandidate -AcknowledgeHostContamination -CandidatePath $setup `
            -InstallerArgs '-NoProfile -NonInteractive -Command "exit 0"' `
            -InstalledExePath $app `
            -LaunchArgs '-NoProfile -NonInteractive -Command "Start-Sleep -Seconds 15"' `
            -UninstallCommand 'exit 0'
        Assert-CommonContract -Json $r.Json -RawText $r.Raw -TestName 'residue'
        if (-not $isElevated) {
            Assert-True ($r.Json.status -eq 'BLOCKED_NOT_ELEVATED') "unelevated installer run must be BLOCKED_NOT_ELEVATED, got $($r.Json.status)"
        } else {
            Assert-True ($r.Json.status -eq 'FAIL') "residue must be FAIL, got $($r.Json.status)"
            $un = Get-ResultCheck -Json $r.Json -Id 'install-uninstall-removal'
            Assert-True ($un.status -eq 'PASS') "no-op uninstaller exiting 0 must PASS the removal leg, got $($un.status)"
            $res = Get-ResultCheck -Json $r.Json -Id 'install-residue'
            Assert-True ($res.status -eq 'FAIL') "install-residue must be FAIL, got $($res.status)"
            Assert-True (Test-Path -LiteralPath $app -PathType Leaf) "installed runtime outside the staged dir must be reported, never deleted"
            Assert-True (Test-Path -LiteralPath $setup -PathType Leaf) "operator-supplied candidate must survive the run"
        }
    }

    # 10. Cleanup failure surfaces in the result and forces FAIL, never hidden.
    Invoke-NamedTest -Name 'CleanupFailure-Surfaces' -Body {
        $d = New-CampaignTestDir
        $cand = New-FakeExe -Dir $d -Name 'FastFiles.exe'
        $orphanArgs = '-NoProfile -NonInteractive -Command "$me=[Diagnostics.Process]::GetCurrentProcess().MainModule.FileName; Start-Process -FilePath $me -ArgumentList ''-NoProfile'',''-NonInteractive'',''-Command'',''Start-Sleep -Seconds 60''; Start-Sleep -Seconds 20"'
        $staged = $null
        try {
            $r = Invoke-CertRunner -InstallCandidate -AcknowledgeHostContamination -CandidatePath $cand -LaunchArgs $orphanArgs
            $staged = $r.Json.details.stagedDir
            Assert-CommonContract -Json $r.Json -RawText $r.Raw -TestName 'cleanup-fail'
            Assert-True ($r.Json.status -eq 'FAIL') "orphaned deployment must be FAIL, got $($r.Json.status)"
            $orph = Get-ResultCheck -Json $r.Json -Id 'install-orphans'
            Assert-True ($orph.status -eq 'FAIL') "install-orphans must be FAIL, got $($orph.status)"
            Assert-True ($r.Json.cleanup.status -eq 'FAIL') "cleanup object must report FAIL when the staged dir cannot be removed, got $($r.Json.cleanup.status)"
            Assert-True ($r.Json.cleanup.stagedDirRemoved -eq $false) "cleanup must record that the staged dir was not removed"
        } finally {
            Clear-StagedLeftovers -StagedDir $staged
        }
    }

    # 11. Audit with an unresolvable candidate never yields PASS.
    Invoke-NamedTest -Name 'AuditMissingCandidate-RequiredButUnavailable' -Body {
        $bogus = Join-Path ([IO.Path]::GetTempPath()) ("FFCHTest-does-not-exist-" + [Guid]::NewGuid().ToString("N") + ".exe")
        $r = Invoke-CertRunner -CandidatePath $bogus
        Assert-CommonContract -Json $r.Json -RawText $r.Raw -TestName 'audit-missing'
        Assert-True ($r.Json.status -ne 'PASS') "audit with missing candidate must never PASS"
        Assert-True ($r.Json.disposition -ne 'PASS') "audit with missing candidate disposition must never PASS"
        if ($hostContaminated) {
            Assert-True ($r.Json.status -eq 'BLOCKED_ENVIRONMENT') "contaminated audit expected BLOCKED_ENVIRONMENT, got $($r.Json.status)"
        } else {
            Assert-True ($r.Json.status -eq 'REQUIRED-BUT-UNAVAILABLE') "expected REQUIRED-BUT-UNAVAILABLE, got $($r.Json.status)"
        }
    }
} finally {
    foreach ($f in $script:resultFiles) {
        if (Test-Path -LiteralPath $f) { Remove-Item -LiteralPath $f -Force -ErrorAction SilentlyContinue }
        $sidecar = "$f.sha256"
        if (Test-Path -LiteralPath $sidecar) { Remove-Item -LiteralPath $sidecar -Force -ErrorAction SilentlyContinue }
    }
    foreach ($d in $script:testDirs) {
        if (Test-Path -LiteralPath $d) { Remove-Item -LiteralPath $d -Recurse -Force -ErrorAction SilentlyContinue }
    }
}

if ($script:failedTests.Count -gt 0) {
    Write-Host "Test-CleanHostCertification FAILED: $($script:failedTests.Count) test(s) failed ($($script:failedTests -join ', '))" -ForegroundColor Red
    exit 1
}

Write-Host "All Test-CleanHostCertification tests PASSED!" -ForegroundColor Green
