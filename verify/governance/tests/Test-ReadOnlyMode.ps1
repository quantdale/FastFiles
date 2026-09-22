<#
    Phase 3 read-only mode contract tests (hermetic): Get-AgentMode /
    Test-AgentModeReadOnly / Test-WritablePathAllowlist / Assert-AgentGovernance.
    Runs from a temp non-repo directory so the real repository sentinel and
    FASTFILES_AGENT_MODE from the caller cannot leak into assertions.
#>

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
Import-Module (Join-Path $repoRoot 'verify\core\Governance.psm1') -Force -DisableNameChecking

$failures = 0
function Check {
    param([bool] $Condition, [string] $Description)
    if ($Condition) { Write-Host "ok: $Description" } else { Write-Host "FAIL: $Description"; $script:failures++ }
}

$savedMode = $env:FASTFILES_AGENT_MODE
$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-readonly-mode-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null
$sentinelPath = Join-Path $tempRoot 'verify\runs\agent-mode.readonly'
Push-Location $tempRoot
try {
    # Env=readonly resolves regardless of sentinel.
    $env:FASTFILES_AGENT_MODE = 'readonly'
    Check ((Get-AgentMode) -eq 'readonly') 'FASTFILES_AGENT_MODE=readonly resolves to readonly'
    Check (Test-AgentModeReadOnly) 'Test-AgentModeReadOnly is true under readonly env'

    # Unknown values fail closed as unrecognized + readonly enforcement.
    $env:FASTFILES_AGENT_MODE = 'banana'
    $mode = Get-AgentMode
    Check ($mode -eq 'unrecognized:banana') 'unknown mode is reported as unrecognized:banana'
    Check (Test-AgentModeReadOnly) 'unrecognized mode fails closed to readonly enforcement'
    $guard = Assert-AgentGovernance -Operation 'git-commit' -PassThru
    Check ((-not $guard.ok) -and $guard.exitCode -eq 3 -and $guard.message.StartsWith('agent-mode-unrecognized:')) 'unrecognized mode blocks git-commit with exitCode 3'

    # Env=normal without sentinel is a normal session.
    $env:FASTFILES_AGENT_MODE = 'normal'
    Check ((Get-AgentMode) -eq 'normal') 'FASTFILES_AGENT_MODE=normal without sentinel is normal'
    $guard = Assert-AgentGovernance -Operation 'git-commit' -PassThru
    Check ($guard.ok) 'normal mode allows git-commit'

    # Unset env without sentinel is normal.
    Remove-Item Env:FASTFILES_AGENT_MODE -ErrorAction SilentlyContinue
    Check ((Get-AgentMode) -eq 'normal') 'unset mode without sentinel is normal'

    # Sticky sentinel forces readonly even when the env claims normal.
    New-Item -ItemType Directory -Path (Split-Path -Parent $sentinelPath) -Force | Out-Null
    Set-Content -LiteralPath $sentinelPath -Value 'fixture-run' -Encoding utf8NoBOM
    Check ((Get-AgentMode) -eq 'readonly') 'sticky sentinel forces readonly with env unset'
    $env:FASTFILES_AGENT_MODE = 'normal'
    Check ((Get-AgentMode) -eq 'readonly') 'sentinel overrides FASTFILES_AGENT_MODE=normal'
    Remove-Item -LiteralPath $sentinelPath -Force

    # Writable allowlist shape and decisions.
    $allow = @(Get-AgentWritableAllowlist)
    foreach ($prefix in @('build/', 'out/', 'dist/', 'verify/runs/', 'verify/baselines/', 'verify/status/generated/', 'ci-status/', '.tmp-ff/')) {
        Check ($allow -contains $prefix) "allowlist contains $prefix"
    }
    $decisions = @(Test-WritablePathAllowlist -RepoRoot $tempRoot -Paths @(
            (Join-Path $tempRoot 'build\out.txt'),
            (Join-Path $tempRoot 'src\Main.cpp'),
            (Join-Path $tempRoot 'verify\runs\x.json'),
            (Join-Path ([IO.Path]::GetTempPath()) 'ff-outside-probe.txt')
        ))
    Check ($decisions.Count -eq 4) 'four path decisions are returned'
    $buildDecision = @($decisions | Where-Object { $_.relative -eq 'build/out.txt' })[0]
    Check ($buildDecision.writable -and $buildDecision.reason -eq 'allowlisted:build/') 'build/ path is allowlisted writable'
    $srcDecision = @($decisions | Where-Object { $_.relative -eq 'src/Main.cpp' })[0]
    Check ((-not $srcDecision.writable) -and $srcDecision.reason -eq 'tracked-path-not-allowlisted') 'src path is not allowlisted'
    $runsDecision = @($decisions | Where-Object { $_.relative -eq 'verify/runs/x.json' })[0]
    Check ($runsDecision.writable -and $runsDecision.reason -eq 'allowlisted:verify/runs/') 'verify/runs/ path is allowlisted writable'
    $outsideDecision = @($decisions | Where-Object { $_.reason -eq 'outside-repository' })[0]
    Check ($outsideDecision -and $outsideDecision.writable) 'outside-repository temp evidence is writable'

    # Readonly tracked-write: blocked for source, allowed for allowlisted run dirs.
    $env:FASTFILES_AGENT_MODE = 'readonly'
    $guard = Assert-AgentGovernance -Operation 'tracked-write' -RepoRoot $tempRoot -Paths @((Join-Path $tempRoot 'src\Main.cpp')) -PassThru
    Check ((-not $guard.ok) -and $guard.exitCode -eq 1 -and $guard.message -match 'readonly-mode-violation:tracked-write' -and $guard.message -match 'src/Main.cpp') 'readonly blocks tracked-write of src path with path detail'
    $guard = Assert-AgentGovernance -Operation 'tracked-write' -RepoRoot $tempRoot -Paths @((Join-Path $tempRoot 'build\probe.txt')) -PassThru
    Check ($guard.ok) 'readonly allows tracked-write into allowlisted build/'

    # Git verbs stay blocked even when every listed path is allowlisted.
    $guard = Assert-AgentGovernance -Operation 'git-commit' -RepoRoot $tempRoot -Paths @((Join-Path $tempRoot 'build\probe.txt')) -PassThru
    Check ((-not $guard.ok) -and $guard.message -eq 'readonly-mode-violation:git-commit') 'git-commit is blocked under readonly even with allowlisted paths only'
    $guard = Assert-AgentGovernance -Operation 'git-push' -RepoRoot $tempRoot -PassThru
    Check ((-not $guard.ok) -and $guard.message -eq 'readonly-mode-violation:git-push') 'git-push is blocked under readonly'
    $guard = Assert-AgentGovernance -Operation 'git-ref-write' -RepoRoot $tempRoot -PassThru
    Check ((-not $guard.ok) -and $guard.message -eq 'readonly-mode-violation:git-ref-write') 'git-ref-write is blocked under readonly'

    # Write-GovernedFile honors the same gate.
    $guard = Write-GovernedFile -RepoRoot $tempRoot -Path (Join-Path $tempRoot 'src\should-not-exist.cpp') -Content 'x' -PassThru
    Check ((-not $guard.ok) -and $guard.message -match 'readonly-mode-violation:tracked-write') 'Write-GovernedFile refuses a src path under readonly'
    Check (-not (Test-Path -LiteralPath (Join-Path $tempRoot 'src\should-not-exist.cpp'))) 'blocked Write-GovernedFile does not create the file'
    $guard = Write-GovernedFile -RepoRoot $tempRoot -Path (Join-Path $tempRoot 'verify\runs\probe.txt') -Content 'x' -PassThru
    Check ($guard.ok -and (Test-Path -LiteralPath $guard.path)) 'Write-GovernedFile writes an allowlisted verify/runs path under readonly'

    # Normal mode allows a tracked write.
    $env:FASTFILES_AGENT_MODE = 'normal'
    $guard = Write-GovernedFile -RepoRoot $tempRoot -Path (Join-Path $tempRoot 'docs\note.txt') -Content 'ok' -PassThru
    Check ($guard.ok -and (Test-Path -LiteralPath $guard.path)) 'normal mode Write-GovernedFile writes a tracked path'
}
finally {
    Pop-Location
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
    if ($null -ne $savedMode) { $env:FASTFILES_AGENT_MODE = $savedMode } else { Remove-Item Env:FASTFILES_AGENT_MODE -ErrorAction SilentlyContinue }
}

if ($failures -gt 0) {
    Write-Host "FAILED: $failures check(s)"
    exit 1
}
Write-Host 'All read-only mode checks passed.'
exit 0
