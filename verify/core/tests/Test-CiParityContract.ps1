<#
    hardening-campaign Phase 8: CI-parity drift contract. Extracts the
    canonical job manifest from the CURRENT .github/workflows/ci.yml and
    requires the local parity orchestrator's embedded manifest + hosted-ref
    citations to match it exactly, so a workflow change can never silently
    leave the local parity definition stale. Also pins the truth-label
    vocabulary: local parity must never phrase itself as hosted CI success.
    Headless text/parse checks only; runs nothing from the workflow.
#>

$ErrorActionPreference = 'Stop'
$failures = 0
function Check {
    param([bool] $Condition, [string] $Description)
    if ($Condition) { Write-Host "PASS: $Description" } else { Write-Host "FAIL: $Description"; $script:failures++ }
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
$workflowPath = Join-Path $repoRoot '.github\workflows\ci.yml'
$orchestratorPath = Join-Path $repoRoot 'verify\Invoke-CiParity.ps1'
Check (Test-Path -LiteralPath $workflowPath) 'current workflow file exists'
Check (Test-Path -LiteralPath $orchestratorPath) 'local parity orchestrator exists'
if (-not (Test-Path -LiteralPath $workflowPath) -or -not (Test-Path -LiteralPath $orchestratorPath)) {
    if ($failures -gt 0) { exit 1 } else { exit 0 }
}

$workflow = Get-Content -LiteralPath $workflowPath -Raw
$orchestrator = Get-Content -LiteralPath $orchestratorPath -Raw

# Canonical extraction: job ids are the indent-2 keys under the top-level
# `jobs:` mapping (two-space indent, no deeper). Not a raw-text hash --
# structural, so comments/reformat do not false-alarm while job adds/drops do.
$jobsMatch = [regex]::Match($workflow, '(?ms)^jobs:\r?\n((?:  [A-Za-z0-9_-]+:.*\r?\n)+)')
Check $jobsMatch.Success 'workflow exposes a jobs mapping'
$workflowJobs = @()
if ($jobsMatch.Success) {
    foreach ($m in [regex]::Matches($jobsMatch.Groups[1].Value, '(?m)^  ([A-Za-z0-9_-]+):')) {
        $workflowJobs += $m.Groups[1].Value
    }
}
$workflowJobs = @($workflowJobs | Sort-Object -Unique)
Check ($workflowJobs.Count -gt 0) 'job ids extracted from current workflow'

# The orchestrator's embedded manifest.
$manifestMatch = [regex]::Match($orchestrator, '\$script:ParityWorkflowJobs = @\(([^)]*)\)')
Check $manifestMatch.Success 'parity orchestrator embeds the canonical job manifest'
$manifestJobs = @()
if ($manifestMatch.Success) {
    foreach ($m in [regex]::Matches($manifestMatch.Groups[1].Value, "'([^']+)'")) {
        $manifestJobs += $m.Groups[1].Value
    }
}
$manifestJobs = @($manifestJobs | Sort-Object -Unique)
$joinedWorkflow = $workflowJobs -join '|'
$joinedManifest = $manifestJobs -join '|'
Check (($joinedWorkflow -eq $joinedManifest)) ("job manifest matches current workflow (workflow=[" + ($workflowJobs -join ',') + "] orchestrator=[" + ($manifestJobs -join ',') + "]")

# Every job must be cited by hosted line-range reference in the orchestrator
# (the parity mapping itself, not just the names).
$jobCitations = @{
    'build'               = 'ci.yml:9-63'
    'asan'                = 'ci.yml:83-114'
    'hygiene'             = 'ci.yml:116-181'
    'release-qualification' = 'ci.yml:183-224'
}
foreach ($job in $workflowJobs) {
    if ($jobCitations.ContainsKey($job)) {
        Check ($orchestrator -like '*' + $jobCitations[$job] + '*') "orchestrator cites hosted steps for job '$job' ($($jobCitations[$job]))"
    } else {
        Check $false "job '$job' has no registered hosted-ref citation -- extend Invoke-CiParity.ps1 and this test together"
    }
}

# Truth-label vocabulary: local parity can never claim hosted success.
Check ($orchestrator -match 'LOCAL-PARITY-NOT-HOSTED-CI') 'orchestrator labels output as local parity, not hosted CI'
Check ($orchestrator -match 'EXTERNALLY_BLOCKED_BILLING') 'orchestrator records the hosted billing block explicitly'
Check ($orchestrator -notmatch 'GitHub CI PASS') 'orchestrator never contains a GitHub CI PASS claim'
foreach ($code in @("'PASS'", "'FAIL'", "'SKIPPED'")) {
    Check ($orchestrator -like '*' + $code + '*') "result vocabulary includes $code"
}
Check (($orchestrator -match 'exit 0') -and ($orchestrator -match 'exit 2') -and ($orchestrator -match 'exit 1')) 'exit vocabulary maps to 0/1/2 harness codes'
# residual-campaign Phase B: deterministic OpenSpec resolution + honest
# three-way classification (PASS / validation FAIL / tool-unavailable).
Check ($orchestrator -match [regex]::Escape('npx --yes --package=@fission-ai/openspec@1.8.0 -- openspec validate --all --strict')) 'openspec command uses the pinned package with the npm-11 -- executable separator'
Check ($orchestrator -match 'could not determine executable') 'tool-resolution failure still classifies as unavailable (exit 2), never FAIL'
Check ($orchestrator -match 'exit \$code') 'actual validation exit code propagates (validation failure stays FAIL, never SKIPPED)'
Check ($orchestrator -notmatch 'openspec@latest') 'openspec invocation is version-pinned (no latest/uncontrolled resolution)'
Check ($orchestrator -notmatch '& npx') 'openspec invocation avoids the call-operator npx form (Roaming-shim argument mangling on Windows)'

if ($failures -gt 0) { exit 1 }
Write-Host 'All CI-parity contract checks passed.'
exit 0
