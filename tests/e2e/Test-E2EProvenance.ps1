<# Executes the actual runner only as far as a mandatory fake preflight failure.
   Dummy files are non-executable text; no application or UI is launched. #>
param([string]$ReceiptPath = '')
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$runner = Join-Path $repo 'verify/e2e/Run-FastFilesE2E.ps1'
Import-Module (Join-Path $repo 'verify/core/Provenance.psm1') -Force -DisableNameChecking
$defaultHelper = (Get-Command Get-DefaultCertificationBinaries).Definition
$tempParent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$ownedRoot = Join-Path $tempParent ('FastFiles-E2E-Provenance-' + [guid]::NewGuid().ToString('N'))
$result = [ordered]@{ Status = 'FAIL'; RunnerSha256 = (Get-FileHash -LiteralPath $runner -Algorithm SHA256).Hash; Cases = @() }
$checks = 0
function Assert([bool]$Condition, [string]$Description) {
    if (-not $Condition) { throw "FAIL: $Description" }
    $script:checks++
}
try {
    [void][IO.Directory]::CreateDirectory((Join-Path $ownedRoot 'verify/core'))
    $fakeModule = 'function Get-RealDefaultCertificationBinaries {' + [Environment]::NewLine + $defaultHelper + [Environment]::NewLine + '}' + [Environment]::NewLine + @'
function Get-DefaultCertificationBinaries {
    param([string]$RepoRoot, [string]$BuildDirectory)
    [IO.File]::WriteAllText((Join-Path $RepoRoot 'build-directory.txt'), $BuildDirectory)
    Get-RealDefaultCertificationBinaries -RepoRoot $RepoRoot -BuildDirectory $BuildDirectory
}
function Invoke-ProvenanceGate {
    param([string]$RepoRoot, [string[]]$Binaries, [string]$Mode, [string]$Stage, [switch]$PassThru)
    $evidence = [ordered]@{
        BuildDirectory = [IO.File]::ReadAllText((Join-Path $RepoRoot 'build-directory.txt'))
        Binaries = @($Binaries)
        Missing = @($Binaries | Where-Object { -not [IO.File]::Exists($_) })
        Mode = $Mode
        Stage = $Stage
    }
    $evidence | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $RepoRoot 'gate-call.json') -Encoding utf8
    # ALWAYS stop the actual runner before it can import/execute FastFiles E2E.
    [pscustomobject]@{ ok = $false; message = 'OWNED_HEADLESS_PREFLIGHT_STOP'; evidence = $evidence }
}
Export-ModuleMember -Function Get-DefaultCertificationBinaries, Invoke-ProvenanceGate
'@
    [IO.File]::WriteAllText((Join-Path $ownedRoot 'verify/core/Provenance.psm1'), $fakeModule)
    $buildRoot = Join-Path $ownedRoot 'build/test-preset'
    $expected = @(Get-DefaultCertificationBinaries -RepoRoot $ownedRoot -BuildDirectory $buildRoot)
    foreach ($path in $expected) {
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
        [IO.File]::WriteAllText($path, 'Owned headless preflight fixture; deliberately not a PE executable.')
    }
    $exe = $expected[0]
    $engine = $expected[1]
    $shell = (Get-Process -Id $PID).Path
    foreach ($case in @('complete-development', 'missing-engine-exact')) {
        if ($case -eq 'missing-engine-exact') { Remove-Item -LiteralPath $engine }
        $artifacts = Join-Path $ownedRoot ('artifacts-' + $case)
        $arguments = @('-NoProfile', '-File', $runner, '-RepoRoot', $ownedRoot, '-Exe', $exe, '-ArtifactsDir', $artifacts)
        if ($case -eq 'missing-engine-exact') { $arguments += '-RequireExactTree' }
        $childOutput = & $shell @arguments 2>&1 | Out-String
        $childExit = $LASTEXITCODE
        $call = Get-Content -LiteralPath (Join-Path $ownedRoot 'gate-call.json') -Raw | ConvertFrom-Json
        $failurePath = Join-Path $artifacts 'provenance-gate-failure.json'
        $result.Cases += [pscustomobject]@{
            Name = $case; ExitCode = $childExit; Call = $call
            PreflightReceiptExists = [IO.File]::Exists($failurePath)
            PreflightStopReported = $childOutput.Contains('OWNED_HEADLESS_PREFLIGHT_STOP')
        }
    }
    # Assert only after both safe preflight executions so a red old-runner
    # receipt records both the complete and missing-engine failure modes.
    foreach ($case in $result.Cases) {
        Assert ($case.ExitCode -eq 1 -and $case.PreflightStopReported -and $case.PreflightReceiptExists) "$($case.Name): actual runner stops at failed provenance gate"
        Assert ([IO.Path]::GetFullPath($case.Call.BuildDirectory) -eq [IO.Path]::GetFullPath($buildRoot)) "$($case.Name): helper receives preset build root above src/ui"
        Assert ($case.Call.Binaries.Count -eq 5 -and @($case.Call.Binaries | Select-Object -Unique).Count -eq 5) "$($case.Name): all five distinct certification paths reach gate"
        foreach ($path in $expected) { Assert ($case.Call.Binaries -contains $path) "$($case.Name): required path retained: $([IO.Path]::GetFileName($path))" }
        Assert ($case.Call.Stage -ceq 'e2e-preflight') "$($case.Name): correct preflight stage"
    }
    Assert ($result.Cases[0].Call.Mode -ceq 'development' -and $result.Cases[0].Call.Missing.Count -eq 0) 'complete set preserves development gate mode'
    Assert ($result.Cases[1].Call.Mode -ceq 'exact-tree' -and $result.Cases[1].Call.Missing -contains $engine) 'missing engine still reaches exact-tree gate as missing'
    $result.Status = 'PASS'
    $result.Checks = $checks
} catch {
    $result.Error = $_.Exception.Message
    throw
} finally {
    # Delete only this invocation's unique, positively resolved temp directory.
    try {
        $resolvedOwned = [IO.Path]::GetFullPath($ownedRoot)
        $boundedParent = $tempParent.TrimEnd([char[]]'\/') + [IO.Path]::DirectorySeparatorChar
        if (-not $resolvedOwned.StartsWith($boundedParent, [StringComparison]::OrdinalIgnoreCase) -or
            [IO.Path]::GetFileName($resolvedOwned) -notmatch '^FastFiles-E2E-Provenance-[0-9a-f]{32}$') { throw 'Refusing cleanup outside owned temporary fixture.' }
        if ([IO.Directory]::Exists($resolvedOwned)) { Remove-Item -LiteralPath $resolvedOwned -Recurse -Force }
        $result.CleanupSucceeded = -not [IO.Directory]::Exists($resolvedOwned)
    } catch {
        $result.Status = 'FAIL'
        $result.CleanupError = $_.Exception.Message
        throw
    } finally {
        if ($ReceiptPath) {
            [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($ReceiptPath)))
            $result | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $ReceiptPath -Encoding utf8
        }
    }
}
Write-Host "PASS: actual E2E runner provenance contract ($checks checks, two preflight stops, no app execution)"
