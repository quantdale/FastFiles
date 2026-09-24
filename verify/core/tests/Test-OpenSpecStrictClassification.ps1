# residual-defect-closure-and-certification-completion 2.4: exercise the
# real OpenSpec child with controlled command resolution. No network/package
# fetch and no repository mutation is performed.
$ErrorActionPreference = 'Stop'
$failures = 0
function Check {
    param([bool] $Condition, [string] $Description)
    if ($Condition) { Write-Host "PASS: $Description" } else { Write-Host "FAIL: $Description"; $script:failures++ }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$childScript = Join-Path $repoRoot 'verify\Invoke-OpenSpecStrict.ps1'
$pwsh = (Get-Command pwsh -ErrorAction Stop).Source
$temp = Join-Path $env:TEMP ('fastfiles-openspec-fixture-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temp -Force | Out-Null

function Set-FakeNpx {
    param([string] $Body, [int] $ExitCode, [switch] $OmitNpx)
    $npm = Join-Path $temp 'npm.cmd'
    $npx = Join-Path $temp 'npx.cmd'
    $content = "@echo off`r`n$Body`r`n@exit /b $ExitCode`r`n"
    Set-Content -LiteralPath $npm -Value $content -Encoding ascii
    if ($OmitNpx) {
        Remove-Item -LiteralPath $npx -Force -ErrorAction SilentlyContinue
    } else {
        Set-Content -LiteralPath $npx -Value $content -Encoding ascii
    }
}
function Invoke-OpenSpecChild {
    $oldPath = $env:PATH
    try {
        $env:PATH = "$temp;$env:SystemRoot\System32;$env:SystemRoot"
        $output = @(& $pwsh -NoProfile -ExecutionPolicy Bypass -File $childScript -RepoRoot $repoRoot 2>&1)
        return [pscustomobject]@{ exitCode = $LASTEXITCODE; output = ($output -join "`n") }
    } finally { $env:PATH = $oldPath }
}

try {
    Set-FakeNpx -Body '@echo validated' -ExitCode 0
    $success = Invoke-OpenSpecChild
    Check ($success.exitCode -eq 0) 'successful validator child exits 0'

    Set-FakeNpx -Body '@echo validation ENOENT: project file missing' -ExitCode 1
    $validationFailure = Invoke-OpenSpecChild
    Check ($validationFailure.exitCode -eq 1) 'validator failure containing ENOENT remains FAIL'

    Set-FakeNpx -Body '@echo npm ERR! could not determine executable' -ExitCode 1
    $bootstrapFailure = Invoke-OpenSpecChild
    Check ($bootstrapFailure.exitCode -eq 2) 'proven npm/npx bootstrap failure exits 2 as unavailable'

    Set-FakeNpx -Body '@echo validator chose exit 2' -ExitCode 2
    $exitTwo = Invoke-OpenSpecChild
    Check ($exitTwo.exitCode -eq 1) 'unmarked validator exit 2 is converted to FAIL'

    Set-FakeNpx -Body '@echo should not run' -ExitCode 0 -OmitNpx
    $missingNpx = Invoke-OpenSpecChild
    Check ($missingNpx.exitCode -eq 2) 'missing npx fails closed as unavailable before invocation'
} finally {
    Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}

if ($failures -gt 0) { exit 1 }
Write-Host 'All OpenSpec child classification checks passed.'
exit 0
