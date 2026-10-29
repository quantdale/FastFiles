<#
    Phase 3 git-hook guard tests: the tracked .githooks shims delegate to
    hook-guard.ps1, and hook-guard refuses commit/push under read-only mode
    (env or sticky sentinel) while allowing a normal session. Runs in child
    processes from a temp working directory so the real repository sentinel
    cannot leak into assertions.
#>

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path

$failures = 0
function Check {
    param([bool] $Condition, [string] $Description)
    if ($Condition) { Write-Host "ok: $Description" } else { Write-Host "FAIL: $Description"; $script:failures++ }
}

$savedMode = $env:FASTFILES_AGENT_MODE
$hookGuard = Join-Path $repoRoot 'verify\governance\hook-guard.ps1'
# large-index-lifecycle-and-search-scalability: embed quotes around the
# script path -- Start-Process -ArgumentList joins elements with spaces and
# does not quote them, so a checkout path containing a space (e.g.
# C:\Users\Michael Roy\...) splits -File and the child exits 64 with
# "not recognized as the name of a script file" instead of running.
$hookGuardQuoted = '"' + $hookGuard + '"'
$preCommit = Join-Path $repoRoot '.githooks\pre-commit'
$prePush = Join-Path $repoRoot '.githooks\pre-push'
$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-hook-guard-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null
$sentinelPath = Join-Path $tempRoot 'verify\runs\agent-mode.readonly'

function Invoke-HookGuard {
    param([Parameter(Mandatory)] [string] $Operation)
    $stdoutPath = Join-Path $tempRoot 'hook-guard-stdout.txt'
    $stderrPath = Join-Path $tempRoot 'hook-guard-stderr.txt'
    $pwshExe = (Get-Command pwsh -ErrorAction Stop).Source
    $process = Start-Process -FilePath $pwshExe `
        -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $hookGuardQuoted, '-Operation', $Operation) `
        -Wait -PassThru -NoNewWindow `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    $stdout = if (Test-Path -LiteralPath $stdoutPath) { Get-Content -LiteralPath $stdoutPath -Raw } else { '' }
    $stderr = if (Test-Path -LiteralPath $stderrPath) { Get-Content -LiteralPath $stderrPath -Raw } else { '' }
    Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue
    [pscustomobject]@{ exitCode = $process.ExitCode; stdout = $stdout; stderr = $stderr }
}

Push-Location $tempRoot
try {
    # Tracked shims exist and delegate to hook-guard with the right operation.
    Check (Test-Path -LiteralPath $preCommit -PathType Leaf) 'tracked .githooks/pre-commit exists'
    Check (Test-Path -LiteralPath $prePush -PathType Leaf) 'tracked .githooks/pre-push exists'
    $preCommitText = Get-Content -LiteralPath $preCommit -Raw
    Check ($preCommitText -match 'hook-guard\.ps1' -and $preCommitText -match '-Operation pre-commit') 'pre-commit shim delegates to hook-guard -Operation pre-commit'
    $prePushText = Get-Content -LiteralPath $prePush -Raw
    Check ($prePushText -match 'hook-guard\.ps1' -and $prePushText -match '-Operation pre-push') 'pre-push shim delegates to hook-guard -Operation pre-push'

    # Normal session: both operations are allowed.
    $env:FASTFILES_AGENT_MODE = 'normal'
    $run = Invoke-HookGuard -Operation 'pre-commit'
    Check ($run.exitCode -eq 0) 'hook-guard allows pre-commit in a normal session'
    $run = Invoke-HookGuard -Operation 'pre-push'
    Check ($run.exitCode -eq 0) 'hook-guard allows pre-push in a normal session'

    # Readonly env: both operations refuse with the canonical violation on stderr.
    $env:FASTFILES_AGENT_MODE = 'readonly'
    $run = Invoke-HookGuard -Operation 'pre-commit'
    Check ($run.exitCode -eq 1) 'hook-guard refuses pre-commit under FASTFILES_AGENT_MODE=readonly'
    Check ($run.stderr -match 'readonly-mode-violation:git-commit') 'pre-commit refusal names git-commit on stderr'
    $run = Invoke-HookGuard -Operation 'pre-push'
    Check ($run.exitCode -eq 1) 'hook-guard refuses pre-push under FASTFILES_AGENT_MODE=readonly'
    Check ($run.stderr -match 'readonly-mode-violation:git-push') 'pre-push refusal names git-push on stderr'

    # Sticky sentinel with env claiming normal still refuses.
    $env:FASTFILES_AGENT_MODE = 'normal'
    New-Item -ItemType Directory -Path (Split-Path -Parent $sentinelPath) -Force | Out-Null
    Set-Content -LiteralPath $sentinelPath -Value 'hook-fixture' -Encoding utf8NoBOM
    $run = Invoke-HookGuard -Operation 'pre-commit'
    Check ($run.exitCode -eq 1) 'sticky sentinel refuses pre-commit even when env says normal'
    Check ($run.stderr -match 'readonly-mode-violation:git-commit') 'sentinel refusal names git-commit on stderr'
    Remove-Item -LiteralPath $sentinelPath -Force

    # Unset env without sentinel: allowed again.
    Remove-Item Env:FASTFILES_AGENT_MODE -ErrorAction SilentlyContinue
    $run = Invoke-HookGuard -Operation 'pre-commit'
    Check ($run.exitCode -eq 0) 'hook-guard allows pre-commit with env unset and no sentinel'

    # Invalid operation is rejected by the ValidateSet (nonzero exit).
    $pwshExe = (Get-Command pwsh -ErrorAction Stop).Source
    $bad = Start-Process -FilePath $pwshExe `
        -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $hookGuardQuoted, '-Operation', 'not-an-operation') `
        -Wait -PassThru -NoNewWindow `
        -RedirectStandardOutput (Join-Path $tempRoot 'bad-out.txt') -RedirectStandardError (Join-Path $tempRoot 'bad-err.txt')
    Check ($bad.ExitCode -ne 0) 'hook-guard rejects an unknown -Operation value'
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
Write-Host 'All hook-guard checks passed.'
exit 0
