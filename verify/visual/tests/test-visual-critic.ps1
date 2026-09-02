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

# Provider state: fake is deterministic and never an external submission.
$stateFake=Test-VisualCriticProviderState -Provider fake
Check ($stateFake.State -eq 'Available' -and -not $stateFake.CanSubmit) 'fake provider is available locally but cannot submit externally'

# Real provider states distinguish absent, partial, configured-without-adapter,
# privacy-rejected, adapter-error, and successful adapter paths.
$oldKey=$env:FASTFILES_VISION_PROVIDER_KEY
$oldEp=$env:FASTFILES_VISION_PROVIDER_ENDPOINT
try {
    $env:FASTFILES_VISION_PROVIDER_KEY=$null; $env:FASTFILES_VISION_PROVIDER_ENDPOINT=$null
    $stateReal=Test-VisualCriticProviderState -Provider real
    Check ($stateReal.State -eq 'BlockedEnvironment') 'real provider blocked without credentials'
    $stateVia=Invoke-VisualCritic -ImagePath 'C:\tmp\test.png' -Surface 'home' -Provider real
    Check ($stateVia.Count -ge 1 -and $stateVia[0].findingId -eq 'BLOCKED-001') 'real without creds => BLOCKED-001'

    $env:FASTFILES_VISION_PROVIDER_KEY='test-key'; $env:FASTFILES_VISION_PROVIDER_ENDPOINT=$null
    Check ((Test-VisualCriticProviderState -Provider real).State -eq 'Misconfigured') 'partial real configuration is misconfigured'
    $env:FASTFILES_VISION_PROVIDER_ENDPOINT='not-a-uri'
    Check ((Test-VisualCriticProviderState -Provider real).State -eq 'Misconfigured') 'malformed real endpoint is misconfigured'
    $env:FASTFILES_VISION_PROVIDER_ENDPOINT='https://vision.example.test/review'
    Check ((Test-VisualCriticProviderState -Provider real).State -eq 'NotImplemented') 'credentials without an adapter are not implemented'
    $notImplemented=Invoke-VisualCritic -ImagePath 'C:\tmp\test.png' -Surface 'home' -Provider real
    Check ($notImplemented[0].findingId -eq 'NOT-IMPLEMENTED-001') 'configured provider without adapter => NOT-IMPLEMENTED-001'

    $inputPath=Join-Path ([IO.Path]::GetTempPath()) ('critic-input-'+[Guid]::NewGuid().ToString('N')+'.png')
    [IO.File]::WriteAllBytes($inputPath,[byte[]](0x01,0x02))
    $adapter = {
        param($imagePath,$surface,$context)
        return [pscustomobject]@{
            findingId='REAL-ADAPTER-001'; severity='suggestion'; confidence=0.5
            surface=$surface; category='layout'; observedProblem='adapter response'
            expectedDesignRule='adapter response is schema-valid'; requiresHumanTasteDecision=$true
            regressionCandidate=$false
        }
    }
    $adapterContext=@{ Invoker=$adapter }
    Check ((Test-VisualCriticProviderState -Provider real -Context $adapterContext).State -eq 'Available') 'configured provider with explicit adapter is available'
    $privacyRejected=Invoke-VisualCritic -ImagePath $inputPath -Surface 'home' -Provider real -Context $adapterContext
    Check ($privacyRejected[0].findingId -eq 'PRIVACY-REJECTED-001') 'real adapter is blocked until privacy gate is explicit'
    $approvedContext=@{ Invoker=$adapter; ExternalReviewAllowed=$true; RedactionPerformed=$true; PrivacyClassification='sanitized' }
    $realResult=Invoke-VisualCritic -ImagePath $inputPath -Surface 'home' -Provider real -Context $approvedContext
    Check ($realResult.Count -eq 1 -and $realResult[0].findingId -eq 'REAL-ADAPTER-001') 'approved adapter response is normalized and returned'
    $errorAdapter = { param($imagePath,$surface,$context) throw 'simulated provider outage' }
    $errorContext=@{ Invoker=$errorAdapter; ExternalReviewAllowed=$true; RedactionPerformed=$true; PrivacyClassification='sanitized' }
    $providerError=Invoke-VisualCritic -ImagePath $inputPath -Surface 'home' -Provider real -Context $errorContext
    Check ($providerError[0].findingId -eq 'PROVIDER-ERROR-001') 'adapter exception => PROVIDER-ERROR-001'
    Remove-Item -LiteralPath $inputPath -Force -ErrorAction SilentlyContinue
} finally {
    $env:FASTFILES_VISION_PROVIDER_KEY=$oldKey
    $env:FASTFILES_VISION_PROVIDER_ENDPOINT=$oldEp
}

# Subjective findings from fake must not be regression candidates.
$advisory=Invoke-FakeCritic -ImagePath 'C:\tmp\normal.png' -Surface 'home'
foreach($f in @($advisory)) {
    if($f.requiresHumanTasteDecision){ Check (-not $f.regressionCandidate) "subjective $($f.findingId) not regression candidate" }
}
if($script:failures -gt 0){ Write-Host "RESULT: $script:failures FAILED"; exit 1 }
Write-Host 'RESULT: all critic tests passed'; exit 0
