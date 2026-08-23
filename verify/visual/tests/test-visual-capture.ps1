<#
    Headless contract tests for the deterministic visual-capture primitives.
    These tests do not drive a window: they exercise the semantic assertion
    gate, content-hash duplicate policy, manifest fields, and summary writer.
    Exit 0 = PASS; non-zero = FAIL.
#>

$ErrorActionPreference = 'Stop'
$script:failures = 0

function Check {
    param([bool] $Condition, [string] $Description)
    if ($Condition) {
        Write-Host "  ok: $Description"
    } else {
        $script:failures++
        Write-Host "FAIL: $Description"
    }
}

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$modulePath = Join-Path $repo 'verify\visual\VisualCapture.psm1'
Import-Module $modulePath -Force

$root = Join-Path ([System.IO.Path]::GetTempPath()) ('ffvisual-contract-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root -Force | Out-Null
try {
    $run = New-VisualCaptureRun -Root $root -ApplicationSha ('a' * 64)
    $firstPath = Join-Path $root 'first.png'
    $duplicatePath = Join-Path $root 'duplicate.png'
    $intentionalPath = Join-Path $root 'intentional.png'
    $wrongDeclarationPath = Join-Path $root 'wrong-declaration.png'
    [System.IO.File]::WriteAllBytes($firstPath, [byte[]](0x01, 0x02, 0x03, 0x04))
    Copy-Item -LiteralPath $firstPath -Destination $duplicatePath
    Copy-Item -LiteralPath $firstPath -Destination $intentionalPath
    Copy-Item -LiteralPath $firstPath -Destination $wrongDeclarationPath

    $first = Register-VisualCaptureFile -Run $run -Path $firstPath -ScenarioId 'first'
    Check ($first.Hash.Length -eq 64 -and $first.DuplicateOf -eq $null) 'first image registers with a SHA-256 hash'

    $unexpectedRejected = $false
    try {
        $null = Register-VisualCaptureFile -Run $run -Path $duplicatePath -ScenarioId 'unexpected-duplicate'
    } catch {
        $unexpectedRejected = $_.Exception.Message -like '*unexpected duplicate*'
    }
    Check $unexpectedRejected 'unexpected duplicate is rejected'
    Check (-not (Test-Path -LiteralPath $duplicatePath)) 'rejected duplicate file is removed'

    $intentional = Register-VisualCaptureFile -Run $run -Path $intentionalPath -ScenarioId 'intentional-duplicate' -IntentionalDuplicateReason 'same controlled state' -ExpectedDuplicateOf 'first'
    Check ($intentional.DuplicateOf -eq 'first' -and $intentional.DuplicateReason -eq 'same controlled state') 'declared duplicate records its source and reason'

    $wrongDeclarationRejected = $false
    try {
        $null = Register-VisualCaptureFile -Run $run -Path $wrongDeclarationPath -ScenarioId 'wrong-declaration' -IntentionalDuplicateReason 'incorrect source' -ExpectedDuplicateOf 'other'
    } catch {
        $wrongDeclarationRejected = $_.Exception.Message -like '*expected*but hash belongs*'
    }
    Check $wrongDeclarationRejected 'duplicate declaration must name the actual source scenario'
    Check (-not (Test-Path -LiteralPath $wrongDeclarationPath)) 'wrong duplicate declaration file is removed'

    $assertionGateRejected = $false
    try {
        $null = Capture-VisualWindow -Run $run -Element ([pscustomobject]@{}) -ScenarioId 'assertion-failed' -AssertionPass $false
    } catch {
        $assertionGateRejected = $_.Exception.Message -like '*semantic post-condition did not pass*'
    }
    Check $assertionGateRejected 'capture refuses a failed semantic post-condition before touching the window'

    $record = Add-VisualScenarioResult -Run $run -ScenarioId 'first' -ExpectedState 'controlled state' -FixtureId 'fixture-1' -Theme 'Light' -Width 1280 -PaneMode 'single' -DpiScale 100 -Status 'PASS' -AssertionPass $true -AssertionEvidence ([pscustomobject]@{ state = 'active' }) -ScreenshotPath $firstPath -ImageHash $first.Hash
    Check ($record.captureMethod -eq 'DwmExtendedFrameBounds+CopyFromScreen') 'manifest record declares the canonical capture method'
    Check ($record.screenshotPath -eq 'first.png' -and $record.assertionResult.pass) 'manifest record stores a relative screenshot and passing assertion'

    $written = Write-VisualCaptureRun -Run $run
    $manifest = Get-Content -LiteralPath $written.Manifest -Raw | ConvertFrom-Json
    Check ($manifest.schema -eq 'fastfiles.visual-capture.v1') 'manifest schema is versioned'
    Check (@($manifest.captures).Count -eq 1) 'manifest contains the recorded scenario'
    Check ((Get-Content -LiteralPath $written.Summary -Raw) -like '*| Scenario | Theme | Width | Pane | Status | Assertion | Image | Reason |*') 'readable Markdown summary is written'
} finally {
    if (Test-Path -LiteralPath $root) {
        Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
    }
}

if ($script:failures -gt 0) {
    Write-Host "RESULT: $script:failures check(s) FAILED"
    exit 1
}
Write-Host 'RESULT: all visual-capture contract tests passed'
exit 0
