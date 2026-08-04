<#
    zero-touch-autonomous-engineering (intake gate): headless regression tests for the
    validate-phase gate decision in verify/intake.ps1. In particular the CRITICAL case:
    a gate verdict with Passed=$false caused only by unrepresented product-source edits
    (no per-capability FAIL verdicts) must fail the validate phase rather than silently
    PASS. Exit 0 = PASS, non-zero = FAIL.
#>
$ErrorActionPreference = 'Stop'
$script:failures = 0

function Check {
    param([bool] $Condition, [string] $Description)
    if ($Condition) {
        Write-Host "  ok: $Description"
    } else {
        $script:failures++
        Write-Host "FAIL: $Description"
    }
}

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$intakePath = Join-Path $repo 'verify\intake.ps1'

# Dot-source intake.ps1 so its phase functions (and the gate decision helper) are
# available for direct testing. The dispatch switch at the bottom of intake.ps1 is
# skipped when dot-sourced, so no run is started.
. $intakePath -Verb 'status'

Write-Host "== validate gate: on gate FAIL (CRITICAL regression) =="
# A gate verdict with Passed=false arising ONLY from unrepresented product-source edits
# (no FAIL verdicts). Previously intake.ps1:647 required $failVerdicts.Count -gt 0, so
# this fell through and the run was archived PASS with exit 0.
$verdictUnrepresented = [pscustomobject]@{
    Passed = $false
    Verdicts = @(
        [pscustomobject]@{ capabilityId = 'windows-build-validation'; verdict = 'PASS'; reason = $null },
        [pscustomobject]@{ capabilityId = 'windows-protocol-robustness'; verdict = 'SKIPPED'; reason = 'acceptable-reason' }
    )
    UnrepresentedEdits = @(
        [pscustomobject]@{ path = 'src/engine/src/Main.cpp'; lastWriteUtc = '2026-08-04T00:00:00Z' }
    )
}
$outcome1 = Resolve-ValidateGateOutcome -Verdict $verdictUnrepresented -VerdictPath 'gate-verdict.json'
Check (-not $outcome1.ok) 'validate fails when gate Passed=false from unrepresented edits'
Check ($outcome1.class -eq 'ClassB') 'unrepresented-edit failure classified ClassB'
Check ($outcome1.summary -match 'unrepresented product-source edit') 'summary names the unrepresented edits'

Write-Host "== validate gate: on blocking FAIL verdict =="
$verdictFail = [pscustomobject]@{
    Passed = $false
    Verdicts = @(
        [pscustomobject]@{ capabilityId = 'windows-build-validation'; verdict = 'FAIL'; reason = 'linker error' }
    )
    UnrepresentedEdits = @()
}
$outcome2 = Resolve-ValidateGateOutcome -Verdict $verdictFail -VerdictPath 'gate-verdict.json'
Check (-not $outcome2.ok) 'validate fails on a blocking FAIL verdict'
Check ($outcome2.summary -match 'windows-build-validation') 'FAIL verdict named in summary'

Write-Host "== validate gate: REQUIRED-BUT-UNAVAILABLE stays non-blocking =="
$verdictRbu = [pscustomobject]@{
    Passed = $true
    Verdicts = @(
        [pscustomobject]@{ capabilityId = 'crash-analysis'; verdict = 'REQUIRED-BUT-UNAVAILABLE'; reason = 'no-crash-observed' }
    )
    UnrepresentedEdits = @()
}
$outcome3 = Resolve-ValidateGateOutcome -Verdict $verdictRbu -VerdictPath 'gate-verdict.json'
Check ($outcome3.ok) 'REQUIRED-BUT-UNAVAILABLE verdict does not fail validate'
Check ($outcome3.external) 'REQUIRED-BUT-UNAVAILABLE recorded as external evidence'

Write-Host "== validate gate: clean PASS =="
$verdictPass = [pscustomobject]@{
    Passed = $true
    Verdicts = @([pscustomobject]@{ capabilityId = 'windows-build-validation'; verdict = 'PASS'; reason = $null })
    UnrepresentedEdits = @()
}
$outcome4 = Resolve-ValidateGateOutcome -Verdict $verdictPass -VerdictPath 'gate-verdict.json' -RunTree 'verify/runs/zero-touch-autonomous-engineering/20260804-000000'
Check ($outcome4.ok) 'clean gate PASS stays ok'
Check (-not $outcome4.external) 'clean gate PASS is not marked external'

Write-Host ""
if ($script:failures -gt 0) {
    Write-Host "RESULT: $script:failures check(s) FAILED"
    exit 1
}
Write-Host "RESULT: all intake gate regression tests passed"
exit 0