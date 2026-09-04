[CmdletBinding()]
param(
    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
)

$ErrorActionPreference = 'Stop'

Write-Host "Running Test-EvidenceConsistency.ps1..." -ForegroundColor Cyan

$generator = Join-Path $RepoRoot 'verify\certification\Generate-CertificationMarkdown.ps1'
$validator = Join-Path $RepoRoot 'verify\certification\Test-EvidenceConsistency.ps1'

if (-not (Test-Path -LiteralPath $generator)) { throw "Generator script not found: $generator" }
if (-not (Test-Path -LiteralPath $validator)) { throw "Validator script not found: $validator" }

$tempDir = Join-Path ([IO.Path]::GetTempPath()) "FastFiles-evidence-consistency-test-$([guid]::NewGuid().ToString('n'))"
New-Item -ItemType Directory -Path $tempDir -Force | Out-Null

try {
    # Create mock long-run result
    $mockLr = [ordered]@{
        schemaVersion = 1
        testId = "mock_test_1"
        category = "ipc-lifecycle"
        command = "build/debug/mock.exe"
        commit = "1111111111111111111111111111111111111111"
        tree = "2222222222222222222222222222222222222222"
        exactTreeBound = $true
        executableHash = "3333333333333333333333333333333333333333333333333333333333333333"
        environment = [ordered]@{ os = "Windows"; architecture = "AMD64" }
        startUtc = [DateTime]::UtcNow.ToString('o')
        endUtc = [DateTime]::UtcNow.ToString('o')
        requestedIterations = 10
        completedIterations = 10
        passed = 10
        failed = 0
        timedOut = 0
        skipped = 0
        minDurationMs = 10.0
        medianDurationMs = 12.0
        p95DurationMs = 15.0
        maxDurationMs = 16.0
        firstFailure = $null
        artifactHashes = [ordered]@{}
        disposition = "PASS"
    }
    $mockLrPath = Join-Path $tempDir "long-run-mock_test_1.json"
    Set-Content -LiteralPath $mockLrPath -Value ($mockLr | ConvertTo-Json -Depth 10) -Encoding utf8NoBOM

    # Create mock soak result
    $mockSoak = [ordered]@{
        schemaVersion = 1
        commit = "1111111111111111111111111111111111111111"
        tree = "2222222222222222222222222222222222222222"
        exactTreeBound = $true
        targetExecutable = "build/debug/src/ui/FastFiles.exe"
        executableHash = "3333333333333333333333333333333333333333333333333333333333333333"
        environment = [ordered]@{ os = "Windows"; architecture = "AMD64" }
        startUtc = [DateTime]::UtcNow.ToString('o')
        endUtc = [DateTime]::UtcNow.ToString('o')
        durationSeconds = 10.0
        sampleIntervalSeconds = 2
        sampleCount = 5
        fastFilesPid = 12345
        enginePid = $null
        engine = [ordered]@{ observed = $false; reason = "in-proc" }
        phases = [ordered]@{
            launchUtc = [DateTime]::UtcNow.ToString('o')
            readyUtc = [DateTime]::UtcNow.ToString('o')
            readyDurationMs = 500.0
            warmupDurationSeconds = 2
            steadyStateBaselineUtc = [DateTime]::UtcNow.ToString('o')
            measuredSoakEndUtc = [DateTime]::UtcNow.ToString('o')
            shutdownUtc = [DateTime]::UtcNow.ToString('o')
            exitUtc = [DateTime]::UtcNow.ToString('o')
            shutdownMethod = "graceful"
            shutdownDurationMs = 120.0
        }
        startupMetrics = [ordered]@{
            workingSetBytes = 10485760.0
            privateBytes = 5242880.0
            handles = 100
            threads = 5
            gdiObjects = 10
            userObjects = 10
        }
        steadyStateMetrics = [ordered]@{
            workingSetBytes = [ordered]@{ baseline = 20971520.0; minimum = 20971520.0; median = 20971520.0; maximum = 20971520.0; final = 20971520.0; slope = 0.0 }
            privateBytes = [ordered]@{ baseline = 10485760.0; minimum = 10485760.0; median = 10485760.0; maximum = 10485760.0; final = 10485760.0; slope = 0.0 }
            handles = [ordered]@{ baseline = 200; minimum = 200; median = 200; maximum = 200; final = 200; slope = 0.0 }
            threads = [ordered]@{ baseline = 10; minimum = 10; median = 10; maximum = 10; final = 10; slope = 0.0 }
            gdiObjects = [ordered]@{ baseline = 50; minimum = 50; median = 50; maximum = 50; final = 50; slope = 0.0 }
            userObjects = [ordered]@{ baseline = 50; minimum = 50; median = 50; maximum = 50; final = 50; slope = 0.0 }
            cpuSeconds = [ordered]@{ baseline = 1.0; minimum = 1.0; median = 1.0; maximum = 1.0; final = 1.0; slope = 0.0 }
        }
        activityTransitions = @()
        residue = [ordered]@{ clean = $true; orphanCount = 0; orphanPids = @() }
        stabilityLegs = [ordered]@{ survival = "PASS"; functionalActivity = "SKIPPED"; resourceStability = "PASS"; shutdown = "PASS"; residue = "PASS" }
        disposition = "PASS"
        artifactHashes = [ordered]@{}
    }
    $mockSoakPath = Join-Path $tempDir "resource-soak-summary.json"
    Set-Content -LiteralPath $mockSoakPath -Value ($mockSoak | ConvertTo-Json -Depth 10) -Encoding utf8NoBOM

    # Test 1: Generate Markdown and Manifest
    Write-Host "  Test 1: Generate markdown reports and evidence-manifest.json..."
    & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $generator -EvidenceDirectory $tempDir -RepoRoot $RepoRoot
    if ($LASTEXITCODE -ne 0) { throw "Markdown generation failed" }

    $manifestPath = Join-Path $tempDir "evidence-manifest.json"
    $lrMdPath = Join-Path $tempDir "local-long-run-certification.md"
    $soakMdPath = Join-Path $tempDir "resource-soak-summary.md"

    if (-not (Test-Path -LiteralPath $manifestPath)) { throw "Missing evidence-manifest.json" }
    if (-not (Test-Path -LiteralPath $lrMdPath)) { throw "Missing local-long-run-certification.md" }
    if (-not (Test-Path -LiteralPath $soakMdPath)) { throw "Missing resource-soak-summary.md" }
    Write-Host "    PASS (Reports and manifest generated)" -ForegroundColor Green

    # Test 2: Consistency check passes on clean generated artifacts
    Write-Host "  Test 2: Verification of matching markdown and manifest..."
    & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $validator -EvidenceDirectory $tempDir -RequireExactTree -RepoRoot $RepoRoot
    if ($LASTEXITCODE -ne 0) { throw "Expected consistency check to pass on clean generated evidence" }
    Write-Host "    PASS (Consistency check passed)" -ForegroundColor Green

    # Test 3: Discrepancy detection fails closed
    Write-Host "  Test 3: Tampered markdown metric fails closed..."
    $tamperedMd = (Get-Content -LiteralPath $soakMdPath -Raw) -replace '200', '999999' # alter handles
    Set-Content -LiteralPath $soakMdPath -Value $tamperedMd -Encoding utf8NoBOM

    $discrepancyOut = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $validator -EvidenceDirectory $tempDir -RepoRoot $RepoRoot 2>&1
    if ($LASTEXITCODE -eq 0) {
        throw "Expected discrepancy check to fail on tampered markdown, but it passed"
    }
    Write-Host "    PASS (Discrepancy detected and rejected fail-closed)" -ForegroundColor Green

} finally {
    if (Test-Path -LiteralPath $tempDir) {
        Remove-Item -LiteralPath $tempDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "All Test-EvidenceConsistency tests PASSED!" -ForegroundColor Green
