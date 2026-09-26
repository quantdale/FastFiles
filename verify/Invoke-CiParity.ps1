# hardening-campaign Phase 8: local CI-parity orchestrator.
#
# Replays every LOCALLY reproducible hosted gate from .github/workflows/ci.yml
# (the single workflow file; 4 job ids / 6 hosted runs) as one bounded,
# evidence-retaining run. This is LOCAL parity evidence only: its output must
# never be described as hosted GitHub Actions success. Hosted CI is currently
# Hosted status is not collected here and must not be inferred from local results.
#
# Contract:
#   * orchestrates EXISTING canonical commands (never re-implements validation),
#   * every step runs in a CHILD pwsh process (bounded by -TimeoutSeconds,
#     native log redirection) -- mirroring hosted step isolation and giving
#     true exit-code semantics,
#   * read-only with respect to git (rev-parse/status/diff-check only) and
#     writes ONLY into governance-allowlisted paths (verify/runs/**, ci-status/**),
#   * exit codes reuse the verifier vocabulary:
#       0 PASS   - every executed parity step passed
#       1 FAIL   - any blocking step failed (or retained-evidence assertion failed)
#       2 SKIPPED- nothing required could run (e.g. no VC toolchain)
#       3 HARNESS ERROR - invalid usage / evidence-write failure
#       10 reserved (as in verify.ps1)
[CmdletBinding()]
param(
    [string] $EvidenceRoot = '',
    [ValidateRange(60, 7200)]
    [int] $TimeoutSeconds = 3600,
    [switch] $FailFast,
    [switch] $SkipAsan,
    [switch] $SkipReleaseCandidate
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'core\CiParity.psm1') -Force -DisableNameChecking

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not (Test-Path -LiteralPath (Join-Path $repoRoot 'CMakeLists.txt') -PathType Leaf)) {
    Write-Host "HARNESS ERROR: repository root not found at '$repoRoot'"
    exit 3
}

# Canonical hosted-job manifest. Guarded by ffverify_parity_contract_ps_tests:
# if .github/workflows/ci.yml gains or loses a job id, that contract test fails
# until this manifest (and the matching step below) is updated -- a workflow
# change can never silently leave local parity stale.
$script:ParityWorkflowJobs = @('build', 'asan', 'hygiene', 'release-qualification')

if (-not $EvidenceRoot) {
    $EvidenceRoot = Join-Path $repoRoot 'verify\runs\ci-parity'
}
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$runDir = Join-Path $EvidenceRoot $stamp
New-Item -ItemType Directory -Path $runDir -Force | Out-Null
$ciStatusDir = Join-Path $runDir 'ci-status'
New-Item -ItemType Directory -Path $ciStatusDir -Force | Out-Null

$steps = [System.Collections.Generic.List[object]]::new()
$script:SkipRequested = @()
if ($SkipAsan) { $script:SkipRequested += 'asan-slice' }
if ($SkipReleaseCandidate) { $script:SkipRequested += 'release-candidate' }

function Get-RepoBinding {
    $sha = (& git -C $repoRoot rev-parse HEAD 2>$null | Select-Object -First 1)
    $branch = (& git -C $repoRoot rev-parse --abbrev-ref HEAD 2>$null | Select-Object -First 1)
    $statusLines = @(& git -C $repoRoot status --porcelain=v1 2>$null)
    return [pscustomobject]@{
        headSha = if ($sha) { ([string]$sha).Trim().ToLowerInvariant() } else { $null }
        branch  = if ($branch) { ([string]$branch).Trim() } else { $null }
        dirty   = ($statusLines.Count -gt 0)
    }
}

# Every child step body gets a dev-environment preamble (build/test steps
# only; hygiene/contract steps run plain pwsh exactly like the hosted hygiene
# job, which does no toolchain entry). {ROOT} tokens are replaced per child.
$toolPrefix = 'Import-Module ''{ROOT}\verify\core\Toolchain.psm1'' -Force; $t = Find-VSToolchain; if ($null -eq $t) { exit 2 }; Enter-DevEnvironment -Toolchain $t -Arch x64; '

# One bounded step: writes the exact command to step-<id>.ps1 (retained
# evidence), launches it as a CHILD pwsh with a hard timeout, captures
# stdout/stderr to log-<id>.txt, and maps exit codes to the vocabulary.
function Invoke-ParityStep {
    param(
        [Parameter(Mandatory)] [string] $Id,
        [Parameter(Mandatory)] [string] $HostedRef,
        [Parameter(Mandatory)] [string] $Command,
        [switch] $NeedsToolchain,
        [switch] $UnavailableExitCode,
        [string] $UnavailableReason = ''
    )
    if ($script:SkipRequested -contains $Id) {
        $steps.Add([pscustomobject]@{ id = $Id; hostedRef = $HostedRef; command = 'skipped by operator switch'; status = 'SKIPPED'; exitCode = $null; log = $null; durationMs = 0; reason = 'operator-skip' })
        return 'SKIPPED'
    }
    $body = $Command -replace '\{ROOT\}', $repoRoot -replace '\{RUNDIR\}', $runDir
    if ($NeedsToolchain) { $body = ($toolPrefix -replace '\{ROOT\}', $repoRoot) + $body }
    $scriptPath = Join-Path $runDir "step-$Id.ps1"
    Set-Content -LiteralPath $scriptPath -Value $body -Encoding utf8
    $outLog = Join-Path $runDir "log-$Id.txt"
    $errLog = Join-Path $runDir "log-$Id.err.txt"
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $exit = $null
    $timedOut = $false
    $proc = $null
    try {
        $proc = Start-Process -FilePath 'pwsh' -PassThru -NoNewWindow `
            -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $scriptPath) `
            -RedirectStandardOutput $outLog -RedirectStandardError $errLog
        if (-not $proc.WaitForExit($TimeoutSeconds * 1000)) {
            $timedOut = $true
            try { $proc.Kill($true) } catch { try { $proc.Kill() } catch { } }
            $proc.WaitForExit(10000) | Out-Null
        }
        $exit = $proc.ExitCode
    } catch {
        $exit = 3
        Add-Content -LiteralPath $outLog -Value "ORCHESTRATOR-EXCEPTION: $($_.Exception.Message)" -Encoding utf8
    }
    $sw.Stop()
    if (-not (Test-Path -LiteralPath $outLog)) { Set-Content -LiteralPath $outLog -Value '' -Encoding utf8 }
    if ((Test-Path -LiteralPath $errLog) -and (Get-Item -LiteralPath $errLog).Length -gt 0) {
        Add-Content -LiteralPath $outLog -Value "--- stderr ---" -Encoding utf8
        Get-Content -LiteralPath $errLog | Add-Content -LiteralPath $outLog -Encoding utf8
    }
    $status = Get-CiParityStepStatus -ExitCode ([int]($exit ?? 3)) -TimedOut $timedOut -AllowUnavailableExit:$UnavailableExitCode
    $reason = if ($timedOut) { "timeout-${TimeoutSeconds}s" }
              elseif ($status -eq 'SKIPPED' -and $UnavailableReason) { $UnavailableReason }
              elseif ($status -eq 'FAIL' -and $exit -ne $null) { "exit-code-$exit" }
              else { $null }
    $steps.Add([pscustomobject]@{
        id         = $Id
        hostedRef  = $HostedRef
        command    = "step-$Id.ps1"
        status     = $status
        exitCode   = $exit
        log        = "log-$Id.txt"
        durationMs = [math]::Round($sw.Elapsed.TotalMilliseconds, 0)
        reason     = $reason
    })
    Write-Host ("[{0}] {1} -> {2} exit={3} ({4}s)" -f $Id, $HostedRef, $status, $exit, [math]::Round($sw.Elapsed.TotalSeconds, 1))
    if ($status -eq 'FAIL' -and $FailFast) { return 'FAIL' }
    return $status
}

$sourceBinding = Get-RepoBinding

# ---- Step 1: preflight (hosted: toolchain discovery in every build job) ----
$toolchainOk = $true
$pfStatus = Invoke-ParityStep -Id 'preflight' -HostedRef 'ci.yml:37-45 toolchain discovery' -NeedsToolchain -UnavailableExitCode -Command @'
$toolchain = Find-VSToolchain
if (-not $toolchain) { Write-Host 'no VC toolchain'; exit 2 }
Write-Host "VS $($toolchain.InstallationVersion); cmake=$($toolchain.CMakeExe)"
& $toolchain.CMakeExe --version
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $toolchain.NinjaExe --version
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
exit 0
'@
if ($pfStatus -eq 'SKIPPED') { $toolchainOk = $false }

if (-not $toolchainOk) {
    Write-Host 'SKIPPED: no VC toolchain -- no build/test parity step could run.'
} else {
    # ---- Step 2: policy gates (hosted pre-steps of build/asan/hygiene) ----
    Invoke-ParityStep -Id 'policy' -HostedRef 'ci.yml:26-32,75-81,126-132 markers + diagnostic policy + drift + diff-check' -Command @'
& '{ROOT}\verify\check_source_markers.ps1'
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& '{ROOT}\verify\check_diagnostic_policy.ps1'
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
# ffdoc_drift_tests has no dedicated hosted step (rides CTest + exact-tree
# qualification) -- assert it explicitly per the parity contract.
& '{ROOT}\tests\drift\check_doc_drift.ps1'
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& git -C '{ROOT}' diff --check
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
exit 0
'@ | Out-Null

    # ---- Step 3: OpenSpec strict (hosted hygiene job) ----
    Invoke-ParityStep -Id 'openspec' -HostedRef 'ci.yml:134-136 openspec validate --all --strict' -UnavailableExitCode -UnavailableReason 'REQUIRED-BUT-UNAVAILABLE: openspec CLI/network fetch unavailable on this host' -Command @'
& '{ROOT}\verify\Invoke-OpenSpecStrict.ps1' -RepoRoot '{ROOT}'
exit $LASTEXITCODE
'@ | Out-Null

    # ---- Step 4: build matrix (hosted build job, fail-fast:false legs) ----
    foreach ($preset in @('debug', 'release-test', 'analyze')) {
        $cmd = @'
Set-Location '{ROOT}'
& $t.CMakeExe --preset __PRESET__
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $t.CMakeExe --build --preset __PRESET__
exit $LASTEXITCODE
'@ -replace '__PRESET__', $preset
        Invoke-ParityStep -Id "build-$preset" -HostedRef "ci.yml:9-63 build[$preset]" -NeedsToolchain -Command $cmd | Out-Null
    }

    # ---- Step 5: CTest matrix (debug + release-test; analyze has no test preset) ----
    foreach ($preset in @('debug', 'release-test')) {
        $cmd = @'
Set-Location '{ROOT}'
& $t.CTestExe --preset __PRESET__ --output-on-failure
exit $LASTEXITCODE
'@ -replace '__PRESET__', $preset
        Invoke-ParityStep -Id "ctest-$preset" -HostedRef "ci.yml:51-63 ctest --preset $preset" -NeedsToolchain -Command $cmd | Out-Null
    }

    # ---- Step 6: ASAN slice (exact hosted target/test list) ----
    Invoke-ParityStep -Id 'asan-slice' -HostedRef 'ci.yml:83-114 asan 4 targets + 4 tests' -NeedsToolchain -Command @'
Set-Location '{ROOT}'
& $t.CMakeExe --preset asan
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $t.CMakeExe --build --preset asan --target ffprotocol_fuzz_tests ffipc_framing_tests ffipc_listener_lifecycle_tests ffvolumescanner_terminal_tests
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $t.CTestExe --test-dir build/asan -R '^(ffprotocol_fuzz_tests|ffipc_framing_tests|ffipc_listener_lifecycle_tests|ffvolumescanner_terminal_tests)$' --output-on-failure
exit $LASTEXITCODE
'@ | Out-Null

    # ---- Step 7: hygiene suite (status/contracts/visual/exact-tree) ----
    $hygieneCommand = @'
Set-Location '{ROOT}'
$statusDir = Join-Path '{RUNDIR}' 'ci-status'
New-Item -ItemType Directory -Path $statusDir -Force | Out-Null
& '{ROOT}\verify\status\Get-ProjectStatus.ps1' -SkipCtest -OutputDirectory $statusDir -Format human
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& '{ROOT}\verify\status\New-ReleaseReadinessReport.ps1' -SkipCtest -OutputDirectory $statusDir -Format human
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& '{ROOT}\verify\status\tests\Test-ProjectStatus.ps1'
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& '{ROOT}\verify\providers\tests\Test-ProviderContract.ps1'
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Get-ChildItem -Path (Join-Path '{ROOT}' 'verify\visual\tests\*.ps1') | ForEach-Object {
    & $_.FullName
    if ($LASTEXITCODE -ne 0) { throw "visual contract $($_.Name) failed" }
}
$qualification = Join-Path $statusDir 'exact-tree-qualification.json'
& '{ROOT}\verify\qualification\Invoke-ExactTreeQualification.ps1' -OutputPath $qualification -Format human
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& '{ROOT}\verify\status\Get-ProjectStatus.ps1' -SkipCtest -OutputDirectory $statusDir -VerificationRunResultPath $qualification -Format human
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& '{ROOT}\verify\status\New-ReleaseReadinessReport.ps1' -SkipCtest -OutputDirectory $statusDir -VerificationRunResultPath $qualification -Format human
exit $LASTEXITCODE
'@
    Invoke-ParityStep -Id 'hygiene-suite' -HostedRef 'ci.yml:116-181 status + contracts + visual + exact-tree' -Command $hygieneCommand | Out-Null

    # ---- Step 8: unsigned release qualification (retained evidence assertions) ----
    Invoke-ParityStep -Id 'release-candidate' -HostedRef 'ci.yml:183-224 unsigned release qualification' -NeedsToolchain -Command @'
Set-Location '{ROOT}'
& $t.CMakeExe --preset release-test
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $t.CMakeExe --build --preset release-test
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$retained = Join-Path '{ROOT}' 'out\release-candidate'
New-Item -ItemType Directory -Path $retained -Force | Out-Null
& '{ROOT}\verify\release\tests\Test-ReleaseCandidate.ps1' -RepoRoot '{ROOT}' -BuildRoot (Join-Path '{ROOT}' 'build\release-test') -RetainedEvidenceDirectory $retained
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
# Hosted upload semantics: if-no-files-found: error -> assert both.
foreach ($f in @('candidate-manifest.json', 'provenance.json')) {
    $p = Join-Path $retained $f
    if (-not (Test-Path -LiteralPath $p) -or (Get-Item -LiteralPath $p).Length -le 0) {
        Write-Host "EVIDENCE-MISSING: $f"
        exit 1
    }
}
exit 0
'@ | Out-Null
}

# ---- Step 9: report ----
$pass = @($steps | Where-Object { $_.status -eq 'PASS' }).Count
$fail = @($steps | Where-Object { $_.status -eq 'FAIL' }).Count
$skipped = @($steps | Where-Object { $_.status -eq 'SKIPPED' }).Count
$overall = Get-CiParityOverallStatus -Steps @($steps)

$report = [ordered]@{
    schemaVersion  = 1
    kind           = 'fastfiles-local-ci-parity'
    label          = 'LOCAL-PARITY-NOT-HOSTED-CI'
    generatedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
    source         = $sourceBinding
    workflowJobs   = $script:ParityWorkflowJobs
    steps          = @($steps)
    summary        = [ordered]@{ pass = $pass; fail = $fail; skipped = $skipped; total = @($steps).Count }
    overall        = $overall
}
$reportPath = Join-Path $runDir 'parity-report.json'
$jsonTemp = "$reportPath.tmp-$PID"
$md = @()
$md += '# LOCAL CI-PARITY (this Windows host) -- NOT hosted GitHub Actions evidence'
$md += ''
$md += '- Label: LOCAL-PARITY-NOT-HOSTED-CI'
$md += "- Source: $($sourceBinding.headSha) branch=$($sourceBinding.branch) dirty=$($sourceBinding.dirty)"
$md += "- Overall: $overall (pass=$pass fail=$fail skipped=$skipped)"
$md += ''
$md += '| step | hosted ref | status | exit | duration ms | reason |'
$md += '|---|---|---|---|---|---|'
foreach ($s in $steps) {
    $md += "| $($s.id) | $($s.hostedRef) | $($s.status) | $($s.exitCode) | $($s.durationMs) | $($s.reason) |"
}
$md += ''
$md += '## Hosted-only omissions (never simulated)'
$md += '- actions/checkout fetch, actions/upload-artifact retention, runner image/timeout enforcement, ::error annotations, workflow-command output, job-isolation freshness, npx registry fetch provenance.'
$mdPath = Join-Path $runDir 'parity-report.md'
$mdTemp = "$mdPath.tmp-$PID"
try {
    # Publish Markdown first and the authoritative JSON last. If either write
    # fails, exit 3 without leaving a newly discoverable PASS JSON artifact.
    $md | Set-Content -LiteralPath $mdTemp -Encoding utf8
    $report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $jsonTemp -Encoding utf8
    Move-Item -LiteralPath $mdTemp -Destination $mdPath -Force
    Move-Item -LiteralPath $jsonTemp -Destination $reportPath -Force
} catch {
    Remove-Item -LiteralPath $jsonTemp, $mdTemp -Force -ErrorAction SilentlyContinue
    Write-Host "HARNESS ERROR: evidence publication failed: $($_.Exception.Message)"
    exit 3
}

Write-Host ''
Write-Host "parity report: $reportPath"
Write-Host "overall=$overall pass=$pass fail=$fail skipped=$skipped"
Write-Host 'LOCAL-PARITY-NOT-HOSTED-CI: this report contains no hosted run status.'

switch ($overall) {
    'PASS' { exit 0 }
    'SKIPPED' { exit 2 }
    default { exit 1 }
}
