[CmdletBinding()]
param(
    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
)

$ErrorActionPreference = 'Stop'

$testDir = Join-Path $RepoRoot 'verify\certification\tests'
$testFiles = @(Get-ChildItem -LiteralPath $testDir -Filter "Test-*.ps1" -File | Sort-Object Name)

Write-Host "=================================================================" -ForegroundColor Cyan
Write-Host "Running FastFiles Certification Test Suite ($($testFiles.Count) test suites)" -ForegroundColor Cyan
Write-Host "=================================================================" -ForegroundColor Cyan

$passed = 0
$failed = 0
$pwshPath = (Get-Command pwsh).Source

foreach ($t in $testFiles) {
    Write-Host "`n>>> Running: $($t.Name) <<<" -ForegroundColor Yellow
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    & $pwshPath -NoProfile -NonInteractive -File $t.FullName -RepoRoot $RepoRoot
    $exit = $LASTEXITCODE
    $sw.Stop()

    if ($exit -eq 0) {
        $passed++
        Write-Host "PASS: $($t.Name) in $([Math]::Round($sw.Elapsed.TotalSeconds, 2))s" -ForegroundColor Green
    } else {
        $failed++
        Write-Host "FAIL: $($t.Name) exited $exit in $([Math]::Round($sw.Elapsed.TotalSeconds, 2))s" -ForegroundColor Red
    }
}

Write-Host "`n=================================================================" -ForegroundColor Cyan
Write-Host "Certification Test Results: $passed Passed, $failed Failed out of $($testFiles.Count) total" -ForegroundColor $(if ($failed -eq 0) { 'Green' } else { 'Red' })
Write-Host "=================================================================" -ForegroundColor Cyan

if ($failed -gt 0) { exit 1 } else { exit 0 }
