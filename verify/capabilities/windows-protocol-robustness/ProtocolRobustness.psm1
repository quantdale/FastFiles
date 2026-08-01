function Test-ProtocolRobustnessAvailability {
    param([Parameter(Mandatory)] $Fingerprint)
    if (-not $IsWindows) {
        return [pscustomobject]@{ Available = $false; Reason = 'windows-required'; RequiredContext = [pscustomobject]@{ needs = 'Windows pipe handles' } }
    }
    return [pscustomobject]@{ Available = $true; Reason = $null; RequiredContext = $null }
}

function Get-ProtocolRobustnessDiagnostics {
    return @('deterministic-protocol-tests', 'pipe-framing-boundary-tests', 'fixed-seed-protocol-fuzz')
}

function Invoke-ProtocolTestProcess {
    param([Parameter(Mandatory)] [string] $Id, [Parameter(Mandatory)] [string] $Path, [Parameter(Mandatory)] [string] $ArtifactsDir)
    $started = Get-Date
    if (-not (Test-Path -LiteralPath $Path)) {
        return [pscustomobject]@{
            Result = [pscustomobject]@{ id = $Id; tier = 0; status = 'FAIL'; reason = 'test-binary-not-built'; requiredContext = $null; durationMs = 0; detail = $Path; diagnostics = @() }
            Artifact = $null
        }
    }
    $output = & $Path 2>&1 | ForEach-Object ToString
    $exitCode = $LASTEXITCODE
    $finished = Get-Date
    $logName = "$Id.log"
    $output | Set-Content -LiteralPath (Join-Path $ArtifactsDir $logName) -Encoding utf8
    [pscustomobject]@{
        Result = [pscustomobject]@{ id = $Id; tier = 0; status = if ($exitCode -eq 0) { 'PASS' } else { 'FAIL' }; reason = if ($exitCode -ne 0) { 'protocol-test-failed' } else { $null }; requiredContext = $null; durationMs = [math]::Round(($finished-$started).TotalMilliseconds,0); detail = "exitCode=$exitCode; executable=$Path"; diagnostics = @($output | Where-Object { $_ -match '^FAIL:' }) }
        Artifact = [pscustomobject]@{ path = "artifacts/windows-protocol-robustness/$logName"; type = 'protocol-test-log' }
    }
}

function Invoke-ProtocolRobustnessCapability {
    param($RunContext, $Fingerprint, [string] $ArtifactsDir, [hashtable] $Options)
    $tests = [ordered]@{
        'protocol-deterministic' = Join-Path $Options.RepoRoot 'build\debug\tests\protocol\ffprotocol_tests.exe'
        'ipc-framing-boundaries' = Join-Path $Options.RepoRoot 'build\debug\tests\ipc\ffipc_framing_tests.exe'
        'protocol-fixed-seed-fuzz' = Join-Path $Options.RepoRoot 'build\debug\tests\protocol\ffprotocol_fuzz_tests.exe'
    }
    $results = @()
    $artifacts = @()
    foreach ($entry in $tests.GetEnumerator()) {
        $outcome = Invoke-ProtocolTestProcess -Id $entry.Key -Path $entry.Value -ArtifactsDir $ArtifactsDir
        $results += $outcome.Result
        if ($outcome.Artifact) { $artifacts += $outcome.Artifact }
    }
    $failures = @($results | Where-Object status -eq FAIL).Count
    [pscustomobject]@{
        Status = if ($failures -gt 0) { 'FAIL' } else { 'PASS' }
        Reason = if ($failures -gt 0) { 'protocol-robustness-validation-failed' } else { $null }
        Summary = "$(@($results | Where-Object status -eq PASS).Count) protocol suites passed; $failures failed"
        Artifacts = $artifacts
        SubResults = $results
    }
}

Export-ModuleMember -Function Test-ProtocolRobustnessAvailability, Invoke-ProtocolRobustnessCapability, Get-ProtocolRobustnessDiagnostics
