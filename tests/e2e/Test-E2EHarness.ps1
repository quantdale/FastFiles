$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Import-Module (Join-Path $repoRoot 'verify\e2e\FastFilesE2E.psm1') -Force

$failures = 0
function Check([bool] $condition, [string] $description) {
    if ($condition) {
        Write-Host "PASS: $description"
    } else {
        Write-Error "FAIL: $description"
        $script:failures++
    }
}

$root = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-E2E-Harness-' + [guid]::NewGuid().ToString('N'))
$inside = Join-Path $root 'fixture\file.txt'
$siblingPrefix = $root + '-escape\file.txt'
Check (Test-E2EPathWithinRoot -Root $root -Path $root) 'the E2E root is inside itself'
Check (Test-E2EPathWithinRoot -Root $root -Path $inside) 'a descendant path is accepted'
Check (-not (Test-E2EPathWithinRoot -Root $root -Path $siblingPrefix)) 'a sibling prefix escape is rejected'
try {
    Assert-E2EPathWithinRoot -Root $root -Path $siblingPrefix | Out-Null
    Check $false 'the assert helper rejects paths outside the fixture root'
} catch {
    Check $true 'the assert helper rejects paths outside the fixture root'
}

$bounded = Wait-E2ECondition -TimeoutMs 1000 -PollIntervalMs 10 -Description 'headless eventual condition' -Condition {
    return $true
}
Check $bounded.Pass -and $bounded.ElapsedMs -lt 1000 'bounded wait returns a successful condition'

$never = Wait-E2ECondition -TimeoutMs 60 -PollIntervalMs 10 -Description 'headless timeout condition' -Condition { return $false }
Check (-not $never.Pass -and $never.ElapsedMs -ge 50 -and $never.ElapsedMs -lt 1000) 'bounded wait reports a finite timeout'

$commands = @(Get-Command -Module FastFilesE2E -Name Assert-E2EPathWithinRoot,Test-E2EPathWithinRoot,Wait-E2ECondition,Write-E2EFailureEvidence,Invoke-FastFilesE2E)
Check ($commands.Count -eq 5) 'the E2E module exposes its safety, wait, evidence, and runner entry points'

$artifactRoot = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-E2E-Artifacts-' + [guid]::NewGuid().ToString('N'))
try {
    $mockContext = [pscustomobject]@{
        Driver = [pscustomobject]@{ Provider = 'mock'; Root = $null }
        Main = $null
        Launched = [pscustomobject]@{ Id = $PID }
        SessionRoot = $root
        Environment = [pscustomobject]@{ LOCALAPPDATA = [Environment]::GetFolderPath('LocalApplicationData') }
    }
    $artifactPath = Write-E2EFailureEvidence -Context $mockContext -ArtifactDir $artifactRoot `
        -ScenarioId 'headless-envelope' -Stage 'bounded-wait' -Expected 'terminal Ready state' `
        -Observed '' -ErrorMessage 'synthetic timeout for envelope validation' -ElapsedMs 61
    $failurePath = Join-Path $artifactPath 'failure.json'
    $failure = Get-Content -LiteralPath $failurePath -Raw | ConvertFrom-Json
    Check (Test-Path -LiteralPath $failurePath -PathType Leaf) 'failure evidence writes a JSON envelope'
    Check ($failure.scenario -eq 'headless-envelope' -and $failure.stage -eq 'bounded-wait') 'failure evidence preserves scenario and stage'
    Check ($failure.expected -eq 'terminal Ready state' -and $failure.observed -match 'processId') 'failure evidence preserves expected and observed state'
    Check ([int]$failure.elapsedMs -eq 61 -and $failure.error -match 'synthetic timeout') 'failure evidence preserves timeout duration and error'
    Check ($null -ne $failure.observation -and $null -ne $failure.processes) 'failure evidence includes observation and process snapshots'
} finally {
    if (Test-Path -LiteralPath $artifactRoot) { Remove-Item -LiteralPath $artifactRoot -Recurse -Force }
}

if ($failures -gt 0) {
    Write-Error "$failures E2E harness contract checks failed."
    exit 1
}
& (Join-Path $PSScriptRoot 'Test-E2EProvenance.ps1')
Write-Host 'PASS: E2E harness contract checks completed.'
exit 0
