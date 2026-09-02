<#
    Tests for CriticAdapter FakeCritic determinism and contract.
#>
$ErrorActionPreference='Stop'
$script:failures=0
function Check{param([bool]$c,[string]$d) if($c){Write-Host "  ok: $d"} else{$script:failures++; Write-Host "FAIL: $d"} }
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
Import-Module (Join-Path $repo 'verify\visual\CriticAdapter.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $repo 'verify\visual\VisualFindings.psm1') -Force

# FakeCritic deterministic: same input => same output
$r1a=Invoke-FakeCritic -ImagePath 'C:\tmp\test-home.png' -Surface 'home'
$r1b=Invoke-FakeCritic -ImagePath 'C:\tmp\test-home.png' -Surface 'home'
Check ($r1a.Count -eq $r1b.Count -and $r1a[0].findingId -eq $r1b[0].findingId) 'FakeCritic deterministic for same input'

# Clipping fixture => blocker
$clip=Invoke-FakeCritic -ImagePath 'C:\tmp\clipping-fixture.png' -Surface 'home'
Check ($clip.Count -ge 1 -and $clip[0].severity -eq 'blocker' -and $clip[0].category -eq 'clipping') 'clipping fixture => blocker finding'

# Clean fixture => no findings
$clean=Invoke-FakeCritic -ImagePath 'C:\tmp\clean-state.png' -Surface 'home'
Check ($clean.Count -eq 0) 'clean fixture => no findings'

# All findings validate
foreach($f in @($clip)) { $v=Test-VisualFinding -Finding $f; Check $v.Valid "clip finding $($f.findingId) validates" }
foreach($f in @($r1a)) { $v=Test-VisualFinding -Finding $f; Check $v.Valid "default finding $($f.findingId) validates" }

# Provider state: fake always available
$stateFake=Test-VisualCriticProviderState -Provider fake
Check ($stateFake.State -eq 'Available') 'fake provider always available'

# Provider state: real blocked without env
$oldKey=$env:FASTFILES_VISION_PROVIDER_KEY; $oldEp=$env:FASTFILES_VISION_PROVIDER_ENDPOINT
$env:FASTFILES_VISION_PROVIDER_KEY=$null; $env:FASTFILES_VISION_PROVIDER_ENDPOINT=$null
$stateReal=Test-VisualCriticProviderState -Provider real
Check ($stateReal.State -eq 'BlockedEnvironment') 'real provider blocked without credentials'
$stateVia=Invoke-VisualCritic -ImagePath 'C:\tmp\test.png' -Surface 'home' -Provider real
Check ($stateVia.Count -ge 1 -and $stateVia[0].findingId -eq 'BLOCKED-001') 'real without creds => BLOCKED-001'
# Restore
$env:FASTFILES_VISION_PROVIDER_KEY=$oldKey; $env:FASTFILES_VISION_PROVIDER_ENDPOINT=$oldEp

# Subjective findings from fake must not be regression candidates
$advisory=Invoke-FakeCritic -ImagePath 'C:\tmp\normal.png' -Surface 'home'
foreach($f in @($advisory)) {
    if($f.requiresHumanTasteDecision){ Check (-not $f.regressionCandidate) "subjective $($f.findingId) not regression candidate" }
}

if($script:failures -gt 0){ Write-Host "RESULT: $script:failures FAILED"; exit 1 }
Write-Host 'RESULT: all critic tests passed'; exit 0
