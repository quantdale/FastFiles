<#
    Phase 3 terminal assertion round-trip: Enter-ReadOnlySession captures a
    baseline + sticky sentinel on a hermetic fixture repository, and
    Assert-ReadOnlySession PASSes an unchanged session (clearing the sentinel),
    SKIPs a missing baseline, and FAILs (keeping the sentinel) when HEAD moves.
#>

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path

$failures = 0
function Check {
    param([bool] $Condition, [string] $Description)
    if ($Condition) { Write-Host "ok: $Description" } else { Write-Host "FAIL: $Description"; $script:failures++ }
}
function Invoke-FixtureGit {
    param([Parameter(Mandatory)] [string] $Root, [Parameter(ValueFromRemainingArguments)] [string[]] $GitArgs)
    $output = @(& git -C $Root @GitArgs 2>&1 | ForEach-Object { $_.ToString() })
    if ($LASTEXITCODE -ne 0) { throw "fixture git command failed: git $($GitArgs -join ' '): $($output -join ' ')" }
    return ,@($output)
}
function Invoke-GovernanceScript {
    param([Parameter(Mandatory)] [string] $Script, [string[]] $ScriptArgs = @())
    $output = @(& pwsh -NoProfile -ExecutionPolicy Bypass -File $Script @ScriptArgs 2>&1 | ForEach-Object { $_.ToString() })
    $exitCode = $LASTEXITCODE
    $jsonText = ($output -join "`n").Trim()
    $parsed = $null
    if ($jsonText) {
        try { $parsed = $jsonText | ConvertFrom-Json } catch { $parsed = $null }
    }
    [pscustomobject]@{ exitCode = $exitCode; output = $output; json = $parsed; jsonText = $jsonText }
}

$enterScript = Join-Path $repoRoot 'verify\governance\Enter-ReadOnlySession.ps1'
$assertScript = Join-Path $repoRoot 'verify\governance\Assert-ReadOnlySession.ps1'
$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-readonly-terminal-' + [guid]::NewGuid().ToString('N'))
$sentinelPath = Join-Path $fixtureRoot 'verify\runs\agent-mode.readonly'

try {
    New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
    Invoke-FixtureGit $fixtureRoot init | Out-Null
    Invoke-FixtureGit $fixtureRoot config user.name 'FastFiles Fixture' | Out-Null
    Invoke-FixtureGit $fixtureRoot config user.email 'fixture@example.invalid' | Out-Null
    # Mirror the real repository: verify/runs is ignored evidence, so the
    # Enter/Assert baseline + sentinel files must not shift sourceClean/stateDigest.
    Set-Content -LiteralPath (Join-Path $fixtureRoot '.gitignore') -Value @('verify/runs/', 'build/') -Encoding utf8NoBOM
    Set-Content -LiteralPath (Join-Path $fixtureRoot 'tracked.txt') -Value 'base' -Encoding utf8NoBOM
    Invoke-FixtureGit $fixtureRoot add . | Out-Null
    Invoke-FixtureGit $fixtureRoot commit -m 'A' | Out-Null

    # 1) Enter captures a baseline and writes the sticky sentinel.
    $run = Invoke-GovernanceScript -Script $enterScript -ScriptArgs @('-RepoRoot', $fixtureRoot, '-RunId', 'gov-terminal-1')
    Check ($run.exitCode -eq 0) 'Enter-ReadOnlySession exits 0'
    Check ($null -ne $run.json -and $run.json.status -eq 'PASS') 'Enter output parses as JSON with status PASS'
    Check ($run.json.runId -eq 'gov-terminal-1') 'Enter echoes the requested run id'
    Check ([string]$run.json.headSha -match '^[0-9a-f]{40}$' -and [string]$run.json.treeSha -match '^[0-9a-f]{40}$') 'baseline records full HEAD and tree identities'
    Check ([string]$run.json.stateDigest -match '^[0-9a-f]{64}$') 'baseline records a SHA-256 state digest'
    Check ($run.json.mode -eq 'readonly') 'Enter reports mode readonly'
    Check (Test-Path -LiteralPath $sentinelPath) 'Enter wrote the sticky sentinel under the fixture repo'
    Check (Test-Path -LiteralPath $run.json.baselinePath -PathType Leaf) 'Enter wrote baseline.json'

    # 2) Missing baseline is an explicit SKIP (exit 2), not a silent pass.
    $run = Invoke-GovernanceScript -Script $assertScript -ScriptArgs @('-RepoRoot', $fixtureRoot, '-BaselinePath', (Join-Path $fixtureRoot 'missing-baseline.json'))
    Check ($run.exitCode -eq 2) 'Assert-ReadOnlySession exits 2 when the baseline is missing'
    Check ($run.jsonText -match 'skip: no read-only baseline') 'missing-baseline path reports the skip reason'

    # 3) Unchanged session PASSes and clears the sentinel.
    $run = Invoke-GovernanceScript -Script $assertScript -ScriptArgs @('-RepoRoot', $fixtureRoot, '-RunId', 'gov-terminal-1')
    Check ($run.exitCode -eq 0) 'Assert-ReadOnlySession exits 0 for an unchanged session'
    Check ($null -ne $run.json -and $run.json.status -eq 'PASS') 'unchanged assertion status is PASS'
    Check (@($run.json.reasonCodes).Count -eq 0) 'unchanged assertion has no reason codes'
    Check (-not (Test-Path -LiteralPath $sentinelPath)) 'successful assertion removes the sticky sentinel'

    # 4) -KeepSentinel preserves the sentinel after a PASS.
    $run = Invoke-GovernanceScript -Script $enterScript -ScriptArgs @('-RepoRoot', $fixtureRoot, '-RunId', 'gov-terminal-2')
    Check ($run.exitCode -eq 0) 'second Enter session starts cleanly'
    $run = Invoke-GovernanceScript -Script $assertScript -ScriptArgs @('-RepoRoot', $fixtureRoot, '-RunId', 'gov-terminal-2', '-KeepSentinel')
    Check ($run.exitCode -eq 0 -and $run.json.status -eq 'PASS') 'KeepSentinel assertion still PASSes'
    Check (Test-Path -LiteralPath $sentinelPath) 'KeepSentinel preserves the sentinel after PASS'

    # 5) A HEAD mutation during the session FAILs and keeps the sentinel.
    $run = Invoke-GovernanceScript -Script $enterScript -ScriptArgs @('-RepoRoot', $fixtureRoot, '-RunId', 'gov-terminal-3')
    Check ($run.exitCode -eq 0) 'third Enter session starts cleanly'
    Set-Content -LiteralPath (Join-Path $fixtureRoot 'tracked.txt') -Value 'mutated' -Encoding utf8NoBOM
    Invoke-FixtureGit $fixtureRoot add . | Out-Null
    Invoke-FixtureGit $fixtureRoot commit -m 'B' | Out-Null
    $run = Invoke-GovernanceScript -Script $assertScript -ScriptArgs @('-RepoRoot', $fixtureRoot, '-RunId', 'gov-terminal-3')
    Check ($run.exitCode -eq 1) 'Assert-ReadOnlySession exits 1 when HEAD moved during the session'
    Check ($null -ne $run.json -and $run.json.status -eq 'FAIL') 'mutated assertion status is FAIL'
    Check (@($run.json.reasonCodes) -contains 'head-changed-during-validation') 'failure reason includes head-changed-during-validation'
    Check (Test-Path -LiteralPath $sentinelPath) 'failed assertion keeps the sticky sentinel for continued enforcement'

    # 6) -CheckRemote with no origin configured still resolves (null == null).
    $run = Invoke-GovernanceScript -Script $enterScript -ScriptArgs @('-RepoRoot', $fixtureRoot, '-RunId', 'gov-terminal-4', '-CheckRemote')
    Check ($run.exitCode -eq 0) 'Enter -CheckRemote succeeds without an origin remote'
    $run = Invoke-GovernanceScript -Script $assertScript -ScriptArgs @('-RepoRoot', $fixtureRoot, '-RunId', 'gov-terminal-4', '-CheckRemote')
    Check ($run.exitCode -eq 0 -and $run.json.status -eq 'PASS') 'Assert -CheckRemote PASSes when origin/main did not change (both null)'
}
finally {
    Remove-Item -LiteralPath $fixtureRoot -Recurse -Force -ErrorAction SilentlyContinue
}

if ($failures -gt 0) {
    Write-Host "FAILED: $failures check(s)"
    exit 1
}
Write-Host 'All read-only terminal assertion checks passed.'
exit 0
