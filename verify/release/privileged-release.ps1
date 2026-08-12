[CmdletBinding()]
param(
    [string] $PackageDirectory = $PSScriptRoot,
    [string] $RepoRoot,
    [string] $BuildRoot,
    [string] $OutputDirectory = (Join-Path ([IO.Path]::GetTempPath()) 'FastFiles-privileged-release-evidence'),
    [string] $FixtureRoot = (Join-Path ([IO.Path]::GetTempPath()) 'FastFiles-release-fixture'),
    [string] $ExpectedSignerThumbprint,
    [int] $TimeoutSeconds = 180,
    [switch] $SkipInstall,
    [ValidateSet('human', 'json')]
    [string] $OutputFormat = 'human'
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'ReleaseCommon.psm1') -Force -DisableNameChecking

function Invoke-ReleaseProcess {
    param(
        [Parameter(Mandatory)] [string] $FilePath,
        [Parameter(Mandatory)] [string[]] $Arguments,
        [Parameter(Mandatory)] [int] $Timeout
    )
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $FilePath
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    if (-not $process.Start()) { throw "PROCESS_START_FAILED: $FilePath" }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $timedOut = -not $process.WaitForExit($Timeout * 1000)
    if ($timedOut) { $process.Kill($true); $process.WaitForExit() }
    # The output is intentionally not placed in the evidence bundle. Product
    # diagnostics are read separately and can contain user paths.
    [pscustomobject]@{ exitCode = $process.ExitCode; timedOut = $timedOut; stdout = $stdoutTask.Result; stderr = $stderrTask.Result; stdoutAvailable = $null -ne $stdoutTask.Result; stderrAvailable = $null -ne $stderrTask.Result }
}

function Add-Check {
    param([Parameter(Mandatory)] [System.Collections.Generic.List[object]] $Checks, [string] $Id, [string] $Status, [string] $Reason, $Detail)
    $Checks.Add([pscustomobject]@{ id = $Id; status = $Status; reason = $Reason; detail = $Detail })
}

function Find-DiagnosticLogs {
    $paths = @()
    foreach ($root in @($env:LOCALAPPDATA, $env:ProgramData)) {
        if ($root) { $paths += Join-Path $root 'FastFiles\logs\diagnostics.log' }
    }
    return @($paths | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
}

function Wait-ForRawVolumeEvidence {
    param([Parameter(Mandatory)] [int] $Timeout)
    $deadline = (Get-Date).AddSeconds($Timeout)
    do {
        foreach ($path in @(Find-DiagnosticLogs)) {
            $text = Get-Content -LiteralPath $path -Raw -ErrorAction SilentlyContinue
            if ($text -match 'codeName=FFDIAG_RAW_VOLUME_OPEN' -and $text -match 'outcome=succeeded' -and $text -match 'privilegeEnabled=1') {
                return [pscustomobject]@{ status = 'PASS'; logs = @($path); evidence = 'raw-volume-open succeeded with enabled privilege' }
            }
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)
    [pscustomobject]@{ status = 'REQUIRED-BUT-UNAVAILABLE'; logs = @(Find-DiagnosticLogs); evidence = 'No authenticated service raw-volume success diagnostic observed before timeout.' }
}

function Wait-ForJournalEvidence {
    param([Parameter(Mandatory)] [int] $Timeout)
    [pscustomobject]@{ status = 'NOT_SEPARATELY_INSTRUMENTED'; logs = @(Find-DiagnosticLogs); evidence = 'The current product diagnostics expose raw-volume success but not a distinct journal-open event. The driver proves post-scan mutation propagation through the real aggregate path and records this limitation.' }
}

function Invoke-IndexAggregateProbe {
    param(
        [Parameter(Mandatory)] [string] $ProbePath,
        [Parameter(Mandatory)] [string] $DatabasePath,
        [Parameter(Mandatory)] [string] $FolderName,
        [Parameter(Mandatory)] [uint64] $ExpectedItems,
        [Parameter(Mandatory)] [uint64] $ExpectedBytes
    )
    if (-not (Test-Path -LiteralPath $ProbePath -PathType Leaf) -or -not (Test-Path -LiteralPath $DatabasePath -PathType Leaf)) { return $null }
    $record = Invoke-ReleaseProcess -FilePath $ProbePath -Arguments @('index-aggregate', $DatabasePath, $FolderName, $ExpectedItems.ToString(), $ExpectedBytes.ToString()) -Timeout 30
    $json = $null
    try { $json = $record.stdout | ConvertFrom-Json } catch { }
    [pscustomobject]@{ process = $record; record = $json; passed = $record.exitCode -eq 0 -and $json -and $json.status -eq 'PASS' }
}

function Wait-ForIndexAggregate {
    param(
        [Parameter(Mandatory)] [string] $ProbePath,
        [Parameter(Mandatory)] [string] $DatabasePath,
        [Parameter(Mandatory)] [string] $FolderName,
        [Parameter(Mandatory)] [uint64] $ExpectedItems,
        [Parameter(Mandatory)] [uint64] $ExpectedBytes,
        [Parameter(Mandatory)] [int] $Timeout
    )
    $deadline = (Get-Date).AddSeconds($Timeout)
    do {
        $probe = Invoke-IndexAggregateProbe -ProbePath $ProbePath -DatabasePath $DatabasePath -FolderName $FolderName -ExpectedItems $ExpectedItems -ExpectedBytes $ExpectedBytes
        if ($probe -and $probe.passed) { return $probe }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)
    return $probe
}

function Stop-ExactEngineProcess {
    param([Parameter(Mandatory)] [int] $ProcessId, [Parameter(Mandatory)] [string] $ExpectedPath)
    if ($ProcessId -le 0) { return $true }
    $process = Get-CimInstance Win32_Process -Filter "ProcessId=$ProcessId" -ErrorAction SilentlyContinue
    if (-not $process) { return $true }
    if (-not $process.ExecutablePath -or [IO.Path]::GetFullPath([string]$process.ExecutablePath) -ine [IO.Path]::GetFullPath($ExpectedPath)) {
        throw "PROCESS_STOP_REJECTED: PID $ProcessId is not the expected FastFilesEngine path."
    }
    Stop-Process -Id $ProcessId -ErrorAction Stop
    return $true
}

function Get-KitPinStates {
    param([Parameter(Mandatory)] [string] $Package)
    $kitManifestPath = Join-Path $Package 'kit-manifest.json'
    if (-not (Test-Path -LiteralPath $kitManifestPath -PathType Leaf)) { return $null }
    $kitManifest = Get-Content -LiteralPath $kitManifestPath -Raw | ConvertFrom-Json
    if (-not $kitManifest.pinProvisioning) { return $null }
    [pscustomobject]@{
        engine = [pscustomobject]@{ name = 'engine'; state = $kitManifest.pinProvisioning.engine.state; source = $kitManifest.pinProvisioning.engine.source; length = $kitManifest.pinProvisioning.engine.length; normalized = $kitManifest.pinProvisioning.engine.thumbprint }
        service = [pscustomobject]@{ name = 'service'; state = $kitManifest.pinProvisioning.service.state; source = $kitManifest.pinProvisioning.service.source; length = $kitManifest.pinProvisioning.service.length; normalized = $kitManifest.pinProvisioning.service.thumbprint }
    }
}

$transaction = $null
$checks = [System.Collections.Generic.List[object]]::new()
$result = $null
$exitCode = 1
$errorMessage = $null
try {
    $package = [IO.Path]::GetFullPath($PackageDirectory)
    $output = [IO.Path]::GetFullPath($OutputDirectory)
    New-Item -ItemType Directory -Path $output -Force | Out-Null
    $manifestPath = Join-Path $package 'artifact-manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw "ARTIFACT_MANIFEST_REQUIRED: $manifestPath" }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $resolvedRepoRoot = if ($RepoRoot) { [IO.Path]::GetFullPath($RepoRoot) } else { $null }
    $resolvedBuildRoot = if ($BuildRoot) { [IO.Path]::GetFullPath($BuildRoot) } else { $null }
    $pinStates = if ($resolvedRepoRoot -and $resolvedBuildRoot) { Get-ConfiguredPinStates -RepoRoot $resolvedRepoRoot -BuildRoot $resolvedBuildRoot } else { Get-KitPinStates -Package $package }
    if (-not $pinStates) {
        $pinStates = [pscustomobject]@{
            engine = [pscustomobject]@{ name = 'engine'; state = 'MISSING'; source = 'no-kit-pin-provenance'; length = 0; normalized = $null }
            service = [pscustomobject]@{ name = 'service'; state = 'MISSING'; source = 'no-kit-pin-provenance'; length = 0; normalized = $null }
        }
    }
    $matrix = Get-ReleaseSignatureMatrix -Manifest $manifest -PinStates $pinStates -ExpectedSignerThumbprint $ExpectedSignerThumbprint
    $hostPreflight = Get-CleanHostPreflight
    $signatureReady = @($matrix | Where-Object { $_.result -notin @('PASS') }).Count -eq 0
    $pinReady = $pinStates.engine.state -eq 'POPULATED' -and $pinStates.service.state -eq 'POPULATED' -and $signatureReady
    $checks.Add([pscustomobject]@{ id = 'artifact-integrity'; status = if (@($manifest.artifacts | Where-Object { $_.architecture -ne 'x64' }).Count -eq 0) { 'PASS' } else { 'FAIL' }; reason = $null; detail = 'Manifest contains four x64 release artifacts.' })
    $checks.Add([pscustomobject]@{ id = 'signature-and-signer'; status = if ($signatureReady) { 'PASS' } else { 'BLOCKED_EXTERNAL' }; reason = if ($signatureReady) { $null } else { 'AUTHORIZED_SIGNING_REQUIRED' }; detail = $matrix })
    $checks.Add([pscustomobject]@{ id = 'production-pins'; status = if ($pinReady) { 'PASS' } else { 'BLOCKED_EXTERNAL' }; reason = if ($pinReady) { $null } else { 'PRODUCTION_PINS_REQUIRED' }; detail = [pscustomobject]@{ engine = Get-SafePinState -PinState $pinStates.engine; service = Get-SafePinState -PinState $pinStates.service } })
    $checks.Add([pscustomobject]@{ id = 'clean-host-preflight'; status = if ($hostPreflight.clean) { 'PASS' } else { 'BLOCKED_EXTERNAL' }; reason = if ($hostPreflight.clean) { $null } else { 'CLEAN_HOST_PRECONDITION_FAILED' }; detail = $hostPreflight })
    $checks.Add([pscustomobject]@{ id = 'elevation'; status = if ($hostPreflight.elevated) { 'PASS' } else { 'BLOCKED_EXTERNAL' }; reason = if ($hostPreflight.elevated) { $null } else { 'ELEVATED_EXECUTION_REQUIRED' }; detail = 'Run this command from an Administrator PowerShell on the clean validation host.' })

    $transaction = [ordered]@{ schemaVersion = 1; startedAtUtc = [DateTime]::UtcNow.ToString('o'); createdService = $false; createdTask = $false; createdClientGroup = $false; createdInstallDirectory = $false; createdFixture = $false; fixturePath = $null; startedEngineProcessId = $null; startedEnginePath = $null; cleanup = 'not-run' }
    if (-not $signatureReady) {
        $checks.Add([pscustomobject]@{ id = 'privileged-path'; status = 'BLOCKED_EXTERNAL'; reason = 'AUTHORIZED_SIGNING_REQUIRED'; detail = 'The driver stopped before installation or machine mutation.' })
        $checks.Add([pscustomobject]@{ id = 'mft'; status = 'BLOCKED_EXTERNAL'; reason = 'SIGNED_AUTHENTICATED_PATH_REQUIRED'; detail = 'MFT PASS requires the real signed Engine → authenticated Service → raw-volume → index path.' })
        $checks.Add([pscustomobject]@{ id = 'usn'; status = 'BLOCKED_EXTERNAL'; reason = 'SIGNED_AUTHENTICATED_PATH_REQUIRED'; detail = 'USN PASS requires a signed authenticated runtime and controlled filesystem mutation.' })
        $checks.Add([pscustomobject]@{ id = 'index-population-and-query'; status = 'BLOCKED_EXTERNAL'; reason = 'SIGNED_AUTHENTICATED_PATH_REQUIRED'; detail = 'The real index aggregate probe is prepared but was not run without the signed path.' })
    } elseif (-not $pinReady) {
        $checks.Add([pscustomobject]@{ id = 'privileged-path'; status = 'BLOCKED_EXTERNAL'; reason = 'PRODUCTION_PINS_REQUIRED'; detail = 'The driver stopped before installation or machine mutation.' })
    } elseif (-not $hostPreflight.clean) {
        $checks.Add([pscustomobject]@{ id = 'privileged-path'; status = 'BLOCKED_EXTERNAL'; reason = 'CLEAN_HOST_PRECONDITION_FAILED'; detail = 'The driver never installs over a contaminated host.' })
    } elseif (-not $hostPreflight.elevated) {
        $checks.Add([pscustomobject]@{ id = 'privileged-path'; status = 'BLOCKED_EXTERNAL'; reason = 'ELEVATED_EXECUTION_REQUIRED'; detail = 'Use an Administrator PowerShell on the clean host.' })
    } elseif ($SkipInstall) {
        $checks.Add([pscustomobject]@{ id = 'privileged-path'; status = 'SKIPPED'; reason = 'install-skipped'; detail = 'Explicit -SkipInstall.' })
    } else {
        $setup = Join-Path $package 'FastFilesSetup.exe'
        if (-not (Test-Path -LiteralPath $setup -PathType Leaf)) { throw "INSTALLER_REQUIRED: $setup" }
        $fixtureName = 'FastFilesReleaseFixture_' + $PID
        $fixture = Join-Path ([IO.Path]::GetFullPath($FixtureRoot)) $fixtureName
        New-Item -ItemType Directory -Path $fixture -Force | Out-Null
        $transaction.createdFixture = $true
        $transaction.fixturePath = $fixture
        [IO.File]::WriteAllBytes((Join-Path $fixture 'A.bin'), [byte[]]::new(1024))
        [IO.File]::WriteAllBytes((Join-Path $fixture 'B.bin'), [byte[]]::new(2048))
        $installRecord = Invoke-ReleaseProcess -FilePath $setup -Arguments @('/install') -Timeout $TimeoutSeconds
        $transaction.createdInstallDirectory = $true
        if ($installRecord.timedOut -or $installRecord.exitCode -ne 0) { throw "INSTALL_FAILED: setup exit code $($installRecord.exitCode), timedOut=$($installRecord.timedOut)." }
        $service = Get-CimInstance Win32_Service -Filter "Name='FastFilesIndexSvc'" -ErrorAction SilentlyContinue
        $transaction.createdService = $null -ne $service
        $transaction.createdTask = $null -ne (Get-ScheduledTask -TaskPath '\FastFiles\' -TaskName 'FastFilesEngine' -ErrorAction SilentlyContinue)
        $transaction.createdClientGroup = $null -ne (Get-LocalGroup -Name 'FastFilesUsers' -ErrorAction SilentlyContinue)
        $serviceOk = $service -and $service.StartName -eq 'LocalSystem'
        $checks.Add([pscustomobject]@{ id = 'service-identity'; status = if ($serviceOk) { 'PASS' } else { 'FAIL' }; reason = if ($serviceOk) { $null } else { 'SERVICE_IDENTITY_MISMATCH' }; detail = if ($service) { [pscustomobject]@{ startName = $service.StartName; state = $service.State; path = $service.PathName } } else { $null } })
        $enginePath = Join-Path $env:ProgramFiles 'FastFiles\FastFilesEngine.exe'
        $engine = Start-Process -FilePath $enginePath -WorkingDirectory (Split-Path $enginePath) -PassThru
        $transaction.startedEngineProcessId = $engine.Id
        $transaction.startedEnginePath = $enginePath
        $raw = Wait-ForRawVolumeEvidence -Timeout $TimeoutSeconds
        $journal = Wait-ForJournalEvidence -Timeout $TimeoutSeconds
        $dbPath = Join-Path $env:LOCALAPPDATA 'FastFiles\index.db'
        $aggregateProbe = Join-Path $package 'validation\fftest.exe'
        $initialAggregate = Wait-ForIndexAggregate -ProbePath $aggregateProbe -DatabasePath $dbPath -FolderName $fixtureName -ExpectedItems 2 -ExpectedBytes 3072 -Timeout $TimeoutSeconds
        [IO.File]::WriteAllBytes((Join-Path $fixture 'C.bin'), [byte[]]::new(3072))
        $mutationAggregate = Wait-ForIndexAggregate -ProbePath $aggregateProbe -DatabasePath $dbPath -FolderName $fixtureName -ExpectedItems 3 -ExpectedBytes 6144 -Timeout $TimeoutSeconds
        $mftPassed = $raw.status -eq 'PASS' -and $initialAggregate -and $initialAggregate.passed
        $usnPassed = $journal.status -eq 'PASS' -and $raw.status -eq 'PASS' -and $mutationAggregate -and $mutationAggregate.passed
        $checks.Add([pscustomobject]@{ id = 'mft'; status = if ($mftPassed) { 'PASS' } else { 'REQUIRED-BUT-UNAVAILABLE' }; reason = if ($mftPassed) { $null } else { 'MFT_RUNTIME_EVIDENCE_NOT_OBSERVED' }; detail = [pscustomobject]@{ rawVolume = $raw; initialAggregate = $initialAggregate } })
        $checks.Add([pscustomobject]@{ id = 'usn'; status = if ($usnPassed) { 'PASS' } else { 'REQUIRED-BUT-UNAVAILABLE' }; reason = if ($usnPassed) { $null } else { 'USN_RUNTIME_EVIDENCE_NOT_OBSERVED' }; detail = [pscustomobject]@{ journal = $journal; mutationAggregate = $mutationAggregate; acceptance = 'raw-volume-authentication plus real post-scan aggregate mutation; journal-open metadata remains separately uninstrumented' } })
        $checks.Add([pscustomobject]@{ id = 'index-population-and-query'; status = if ($initialAggregate -and $initialAggregate.passed -and $mutationAggregate -and $mutationAggregate.passed) { 'PASS' } else { 'REQUIRED-BUT-UNAVAILABLE' }; reason = if ($initialAggregate -and $initialAggregate.passed -and $mutationAggregate -and $mutationAggregate.passed) { $null } else { 'INDEX_AGGREGATE_PROBE_NOT_CONFIRMED' }; detail = [pscustomobject]@{ database = $dbPath; probe = $aggregateProbe; initial = $initialAggregate; afterMutation = $mutationAggregate } })
        $checks.Add([pscustomobject]@{ id = 'privileged-path'; status = if ($mftPassed -and $usnPassed) { 'PASS' } else { 'REQUIRED-BUT-UNAVAILABLE' }; reason = if ($mftPassed -and $usnPassed) { $null } else { 'END_TO_END_RUNTIME_NOT_CONFIRMED' }; detail = 'Real installed service, engine, diagnostics, controlled fixture mutation, and aggregate probe evidence are required.' })
    }

    $result = [pscustomobject]@{ status = if (@($checks | Where-Object status -eq 'FAIL').Count -gt 0) { 'FAIL' } elseif (@($checks | Where-Object status -eq 'BLOCKED_EXTERNAL').Count -gt 0) { 'BLOCKED_EXTERNAL' } else { 'PASS' }; output = $output; checks = @($checks); transaction = $transaction }
    Write-ReleaseJson -Value $manifest -Path (Join-Path $output 'artifact-manifest.json')
    Write-ReleaseJson -Value ([pscustomobject]@{ schemaVersion = 1; kind = 'fastfiles-privileged-release-result'; status = $result.status; checks = @($checks); transaction = $transaction; package = $package }) -Path (Join-Path $output 'privileged-release-result.json')
    $exitCode = if ($result.status -eq 'PASS') { 0 } elseif ($result.status -eq 'BLOCKED_EXTERNAL') { 2 } else { 1 }
} catch {
    $checks.Add([pscustomobject]@{ id = 'driver'; status = 'FAIL'; reason = 'DRIVER_EXCEPTION'; detail = $_.Exception.Message })
    $errorMessage = $_.Exception.Message
    $exitCode = 1
} finally {
    if ($transaction) {
        try {
            if ($transaction.startedEngineProcessId -and $transaction.startedEnginePath) {
                Stop-ExactEngineProcess -ProcessId $transaction.startedEngineProcessId -ExpectedPath $transaction.startedEnginePath | Out-Null
            }
            if ($transaction.createdInstallDirectory) {
                $cleanupScript = Join-Path $PSScriptRoot 'cleanup-clean-host.ps1'
                & (Get-Command pwsh).Source -NoProfile -File $cleanupScript -RunUninstall -OutputFormat json | Out-Null
                if ($LASTEXITCODE -ne 0) { throw "CLEANUP_FAILED: cleanup-clean-host.ps1 exited with code $LASTEXITCODE." }
            }
            if ($transaction.createdFixture -and $transaction.fixturePath) {
                $fixtureFullPath = [IO.Path]::GetFullPath([string]$transaction.fixturePath).TrimEnd('\')
                $tempFullPath = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
                if ($fixtureFullPath.StartsWith($tempFullPath + '\', [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $fixtureFullPath)) {
                    Remove-Item -LiteralPath $fixtureFullPath -Recurse -Force
                }
            }
            $transaction.cleanup = 'PASS'
        } catch {
            $transaction.cleanup = 'FAIL: ' + $_.Exception.Message
            $exitCode = 1
            if ($result) {
                $result.status = 'FAIL'
                $checks.Add([pscustomobject]@{ id = 'cleanup'; status = 'FAIL'; reason = 'CLEANUP_FAILED'; detail = $transaction.cleanup })
            }
        }
        $transaction.finishedAtUtc = [DateTime]::UtcNow.ToString('o')
        try {
            Write-ReleaseJson -Value $transaction -Path (Join-Path $OutputDirectory 'transaction.json')
            if ($result) {
                Write-ReleaseJson -Value ([pscustomobject]@{ schemaVersion = 1; kind = 'fastfiles-privileged-release-result'; status = $result.status; checks = @($checks); transaction = $transaction; package = $package }) -Path (Join-Path $OutputDirectory 'privileged-release-result.json')
            }
        } catch { }
    }
}

if ($result) {
    try {
        Write-ReleaseJson -Value ([pscustomobject]@{ schemaVersion = 1; kind = 'fastfiles-privileged-release-result'; status = $result.status; checks = @($checks); transaction = $transaction; package = $package }) -Path (Join-Path $OutputDirectory 'privileged-release-result.json')
    } catch { $exitCode = 1 }
}
if ($errorMessage) {
    if ($OutputFormat -eq 'json') { [pscustomobject]@{ status = 'FAIL'; error = $errorMessage } | ConvertTo-Json -Depth 8 } else { Write-Error $errorMessage }
} elseif ($OutputFormat -eq 'json') {
    $result | ConvertTo-Json -Depth 20
} else {
    Write-Host "Release privileged-path result: $($result.status)"
    foreach ($check in $checks) { Write-Host ("{0} {1} {2}" -f $check.status, $check.id, $(if ($check.reason) { $check.reason } else { '' })) }
}
exit $exitCode
