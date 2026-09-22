<#
    Phase 3 git-mutation guard tests on a hermetic fixture repository:
    Invoke-GuardedGit maps verbs to governance operations, blocks every
    mutating verb under FASTFILES_AGENT_MODE=readonly without touching HEAD,
    and Write-GovernedFile applies the tracked-path allowlist.
#>

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
Import-Module (Join-Path $repoRoot 'verify\core\Governance.psm1') -Force -DisableNameChecking

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

$savedMode = $env:FASTFILES_AGENT_MODE
$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-mutation-guard-' + [guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
    Invoke-FixtureGit $fixtureRoot init | Out-Null
    Invoke-FixtureGit $fixtureRoot config user.name 'FastFiles Fixture' | Out-Null
    Invoke-FixtureGit $fixtureRoot config user.email 'fixture@example.invalid' | Out-Null
    Set-Content -LiteralPath (Join-Path $fixtureRoot 'tracked.txt') -Value 'base' -Encoding utf8NoBOM
    Invoke-FixtureGit $fixtureRoot add . | Out-Null
    Invoke-FixtureGit $fixtureRoot commit -m 'A' | Out-Null

    # --- readonly: every mutating verb is refused before git runs ---
    $env:FASTFILES_AGENT_MODE = 'readonly'
    $headBefore = (Invoke-FixtureGit $fixtureRoot rev-parse HEAD)[0].Trim()

    $result = Invoke-GuardedGit -RepoRoot $fixtureRoot -Arguments @('status', '--porcelain=v1') -PassThru
    Check ($result.ok) 'readonly allows read-only git status through Invoke-GuardedGit'

    $result = Invoke-GuardedGit -RepoRoot $fixtureRoot -Arguments @('commit', '-m', 'must-not-run') -PassThru
    Check ((-not $result.ok) -and $result.exitCode -eq 1 -and $result.message -eq 'readonly-mode-violation:git-commit') 'readonly blocks git commit with the canonical violation message'

    $result = Invoke-GuardedGit -RepoRoot $fixtureRoot -Arguments @('add', '--', 'tracked.txt') -PassThru
    Check ((-not $result.ok) -and $result.message -eq 'readonly-mode-violation:git-add') 'readonly blocks git add'

    $result = Invoke-GuardedGit -RepoRoot $fixtureRoot -Arguments @('push', 'origin', 'HEAD') -PassThru
    Check ((-not $result.ok) -and $result.message -eq 'readonly-mode-violation:git-push') 'readonly blocks git push'

    $result = Invoke-GuardedGit -RepoRoot $fixtureRoot -Arguments @('tag', 'v-readonly-probe') -PassThru
    Check ((-not $result.ok) -and $result.message -eq 'readonly-mode-violation:git-ref-write') 'readonly blocks git tag as git-ref-write'

    $result = Invoke-GuardedGit -RepoRoot $fixtureRoot -Arguments @('branch', 'feature-readonly-probe') -PassThru
    Check ((-not $result.ok) -and $result.message -eq 'readonly-mode-violation:git-ref-write') 'readonly blocks git branch as git-ref-write'

    $result = Invoke-GuardedGit -RepoRoot $fixtureRoot -Arguments @('reset', '--hard') -PassThru
    Check ((-not $result.ok) -and $result.message -eq 'readonly-mode-violation:git-ref-write') 'readonly blocks git reset as git-ref-write'

    $headAfter = (Invoke-FixtureGit $fixtureRoot rev-parse HEAD)[0].Trim()
    Check ($headAfter -eq $headBefore) 'HEAD is unchanged after every blocked mutating verb'
    $tags = @(Invoke-FixtureGit $fixtureRoot tag -l)
    Check (@($tags | Where-Object { $_ -eq 'v-readonly-probe' }).Count -eq 0) 'no probe tag was created by the blocked tag attempt'
    & git -C $fixtureRoot rev-parse --verify --quiet 'refs/heads/feature-readonly-probe' 2>&1 | Out-Null
    Check ($LASTEXITCODE -ne 0) 'no probe branch was created by the blocked branch attempt'

    # Write-GovernedFile under readonly.
    $guard = Write-GovernedFile -RepoRoot $fixtureRoot -Path (Join-Path $fixtureRoot 'notes\blocked.txt') -Content 'x' -PassThru
    Check ((-not $guard.ok) -and $guard.message -match 'readonly-mode-violation:tracked-write' -and $guard.message -match 'notes/blocked.txt') 'readonly blocks Write-GovernedFile of an unlisted tracked path'
    Check (-not (Test-Path -LiteralPath (Join-Path $fixtureRoot 'notes\blocked.txt'))) 'blocked write did not create the tracked file'
    $guard = Write-GovernedFile -RepoRoot $fixtureRoot -Path (Join-Path $fixtureRoot 'build\allowed.txt') -Content 'x' -PassThru
    Check ($guard.ok -and (Test-Path -LiteralPath $guard.path)) 'readonly allows Write-GovernedFile into allowlisted build/'

    # Non-matching verbs pass through unguarded (read-only surface).
    $result = Invoke-GuardedGit -RepoRoot $fixtureRoot -Arguments @('rev-parse', 'HEAD') -PassThru
    Check ($result.ok -and ($result.output -join '') -match '^[0-9a-f]{40}') 'readonly allows non-mutating git verbs (rev-parse)'

    # --- normal: mutating verbs execute ---
    $env:FASTFILES_AGENT_MODE = 'normal'
    Set-Content -LiteralPath (Join-Path $fixtureRoot 'tracked.txt') -Value 'changed' -Encoding utf8NoBOM
    $result = Invoke-GuardedGit -RepoRoot $fixtureRoot -Arguments @('add', '--', 'tracked.txt') -PassThru
    Check ($result.ok) 'normal mode allows git add'
    $result = Invoke-GuardedGit -RepoRoot $fixtureRoot -Arguments @('commit', '-m', 'guard-ok') -PassThru
    Check ($result.ok) 'normal mode allows git commit'
    $headFinal = (Invoke-FixtureGit $fixtureRoot rev-parse HEAD)[0].Trim()
    Check ($headFinal -ne $headBefore) 'HEAD advanced only under normal mode'

    $guard = Write-GovernedFile -RepoRoot $fixtureRoot -Path (Join-Path $fixtureRoot 'docs\note.txt') -Content 'ok' -PassThru
    Check ($guard.ok -and (Test-Path -LiteralPath $guard.path)) 'normal mode Write-GovernedFile writes a tracked path'
}
finally {
    Remove-Item -LiteralPath $fixtureRoot -Recurse -Force -ErrorAction SilentlyContinue
    if ($null -ne $savedMode) { $env:FASTFILES_AGENT_MODE = $savedMode } else { Remove-Item Env:FASTFILES_AGENT_MODE -ErrorAction SilentlyContinue }
}

if ($failures -gt 0) {
    Write-Host "FAILED: $failures check(s)"
    exit 1
}
Write-Host 'All git-mutation guard checks passed.'
exit 0
