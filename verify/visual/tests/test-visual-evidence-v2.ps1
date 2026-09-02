<#
    Tests for VisualEvidence v2 schema validation and emission.
#>
$ErrorActionPreference='Stop'
$script:failures=0
function Check{param([bool]$c,[string]$d) if($c){Write-Host "  ok: $d"} else{$script:failures++; Write-Host "FAIL: $d"} }
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
Import-Module (Join-Path $repo 'verify\visual\VisualCapture.psm1') -Force

$tmp=Join-Path ([System.IO.Path]::GetTempPath()) ('ff-v2-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tmp -Force | Out-Null
try {
    $commit='a'*40; $run=New-VisualCaptureRun -Root $tmp -ApplicationSha ('b'*64) -HostId 'test-host'
    # Override commit for deterministic test
    $run.Commit=$commit; $run.Tree='c'*40; $run.ExecutableIdentity='d'*64

    # Valid v2 record
    $rec=New-VisualEvidenceV2Record -Run $run -Surface 'home-default' -SemanticState 'Home active' -Theme light -Density comfortable -Dpi 96 -ViewportWidth 1280 -ViewportHeight 800 -PaneMode single -FixtureId 'fixture-1' -ScreenshotHash ('e'*64) -ArtifactProvenance 'test-provenance'
    $v=Test-VisualEvidenceV2Record -Record $rec
    Check $v.Valid 'valid v2 record passes'

    # Missing required field
    $bad=[pscustomobject]@{ schemaVersion=2; captureTimestamp=[DateTime]::UtcNow.ToString('o'); surface='home'; theme='light'; viewport=[pscustomobject]@{width=1280;height=800}; artifactProvenance='x' }
    # missing commit/screenshotHash
    $v2=Test-VisualEvidenceV2Record -Record $bad
    Check (-not $v2.Valid) 'missing commit/screenshotHash fails'

    # Invalid SHA
    $rec2=New-VisualEvidenceV2Record -Run $run -Surface 'home' -Theme light -ViewportWidth 1280 -ViewportHeight 800 -ScreenshotHash ('e'*64)
    $rec2.commit='not-a-sha'
    $v3=Test-VisualEvidenceV2Record -Record $rec2
    Check (-not $v3.Valid) 'invalid commit SHA fails'

    # Invalid enum
    $rec3=[pscustomobject]@{ schemaVersion=2; commit=$commit; captureTimestamp=[DateTime]::UtcNow.ToString('o'); surface='home'; theme='neon'; viewport=[pscustomobject]@{width=1280;height=800}; screenshotHash=('e'*64); artifactProvenance='x' }
    $v4=Test-VisualEvidenceV2Record -Record $rec3
    Check (-not $v4.Valid) 'invalid theme enum fails'

    # Malformed viewport
    $rec4=[pscustomobject]@{ schemaVersion=2; commit=$commit; captureTimestamp=[DateTime]::UtcNow.ToString('o'); surface='home'; theme='light'; viewport=[pscustomobject]@{width=1280}; screenshotHash=('e'*64); artifactProvenance='x' }
    $v5=Test-VisualEvidenceV2Record -Record $rec4
    Check (-not $v5.Valid) 'malformed viewport fails'

    # Unsupported schema version
    $rec5=[pscustomobject]@{ schemaVersion=99; commit=$commit; captureTimestamp=[DateTime]::UtcNow.ToString('o'); surface='home'; theme='light'; viewport=[pscustomobject]@{width=1280;height=800}; screenshotHash=('e'*64); artifactProvenance='x' }
    $v6=Test-VisualEvidenceV2Record -Record $rec5
    Check (-not $v6.Valid) 'unsupported schema version fails'

    # Test via Add-VisualScenarioResult with v2
    $fakeImg=Join-Path $tmp 'fake.png'; [System.IO.File]::WriteAllBytes($fakeImg,[byte[]](0x01,0x02))
    $hash=(Get-FileHash -LiteralPath $fakeImg -Algorithm SHA256).Hash.ToLowerInvariant()
    $null=Add-VisualScenarioResult -Run $run -ScenarioId 'home-default' -ExpectedState 'Home active' -FixtureId 'fixture-1' -Theme Light -Width 1280 -PaneMode single -DpiScale 96 -Status PASS -AssertionPass $true -ScreenshotPath $fakeImg -ImageHash $hash
    $written=Write-VisualCaptureRun -Run $run
    Check (Test-Path -LiteralPath $written.Manifest) 'manifest written'
    Check (Test-Path -LiteralPath $written.ManifestV2) 'manifest-v2 written'
    $m2=Get-Content -LiteralPath $written.ManifestV2 -Raw | ConvertFrom-Json
    Check ($m2.schemaVersion -eq 2 -and $m2.v2Records.Count -eq 1) 'manifest-v2 contains v2 records'
    $v7=Test-VisualEvidenceV2Record -Record $m2.v2Records[0]
    Check $v7.Valid 'emitted v2 record validates'

    # Sanitized relative path in manifest
    $firstCapture=$m2.captures[0]
    Check ($firstCapture.screenshotPath -notmatch '^[A-Za-z]:' -and $firstCapture.screenshotPath -eq 'fake.png') 'screenshot path is sanitized relative'

} finally { if(Test-Path -LiteralPath $tmp){ Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue } }

if($script:failures -gt 0){ Write-Host "RESULT: $script:failures FAILED"; exit 1 }
Write-Host 'RESULT: all visual-evidence-v2 tests passed'; exit 0
