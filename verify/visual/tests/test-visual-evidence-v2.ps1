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
    Add-Type -AssemblyName System.Drawing
    $commit='a'*40
    $run=New-VisualCaptureRun -Root $tmp -ApplicationSha ('b'*64) -HostId 'test-host'
    # The test image is a fixture; provenance remains explicitly development-bound,
    # never the old all-zero SHA sentinel.
    $run.Commit=$commit; $run.Tree='c'*40; $run.ExecutableIdentity='d'*64
    $run.Branch='test'; $run.SourceKind='git-commit'; $run.WorktreeState='dirty'
    $run.ProvenanceState='dirty-development'; $run.ExactTreeBound=$false

    # Valid v2 record
    $rec=New-VisualEvidenceV2Record -Run $run -Surface 'home-default' -SemanticState 'Home active' -Theme light -Density comfortable -Dpi 96 -DpiScalePercent 100 -ViewportWidth 1280 -ViewportHeight 800 -PaneMode single -FixtureId 'fixture-1' -ScreenshotHash ('e'*64) -ArtifactProvenance 'test-provenance'
    $v=Test-VisualEvidenceV2Record -Record $rec
    Check $v.Valid 'valid v2 record passes'
    Check ($rec.commit -ne ('0'*40) -and $rec.provenanceState -eq 'dirty-development') 'development provenance is explicit and never zero-SHA'
    Check ($rec.dpi -eq 96 -and $rec.dpiScalePercent -eq 100) 'actual DPI and requested percentage are separate'

    # Missing required field
    $bad=[pscustomobject]@{ schemaVersion=2; captureTimestamp=[DateTime]::UtcNow.ToString('o'); surface='home'; theme='light'; viewport=[pscustomobject]@{width=1280;height=800}; artifactProvenance='x' }
    $v2=Test-VisualEvidenceV2Record -Record $bad
    Check (-not $v2.Valid) 'missing required provenance and screenshot fields fail'

    $clone = { param($obj) [pscustomobject]($obj | ConvertTo-Json -Depth 20 | ConvertFrom-Json) }
    # Invalid SHA
    $rec2=& $clone $rec; $rec2.commit='not-a-sha'
    $v3=Test-VisualEvidenceV2Record -Record $rec2
    Check (-not $v3.Valid) 'invalid commit SHA fails'

    # Invalid enum
    $rec3=& $clone $rec; $rec3.theme='neon'
    $v4=Test-VisualEvidenceV2Record -Record $rec3
    Check (-not $v4.Valid) 'invalid theme enum fails'

    # Malformed viewport
    $rec4=& $clone $rec; $rec4.viewport=[pscustomobject]@{width=1280}
    $v5=Test-VisualEvidenceV2Record -Record $rec4
    Check (-not $v5.Valid) 'malformed viewport fails'

    # Unsupported schema version
    $rec5=& $clone $rec; $rec5.schemaVersion=99
    $v6=Test-VisualEvidenceV2Record -Record $rec5
    Check (-not $v6.Valid) 'unsupported schema version fails'

    # All-zero commit is rejected even though it has the right length.
    $recZero=& $clone $rec; $recZero.commit='0'*40
    $vZero=Test-VisualEvidenceV2Record -Record $recZero
    Check (-not $vZero.Valid) 'all-zero commit fails'

    # Test via Add-VisualScenarioResult with a real PNG and v2.
    $fakeImg=Join-Path $tmp 'fixture.png'
    $bmp=[System.Drawing.Bitmap]::new(16,16)
    $graphics=[System.Drawing.Graphics]::FromImage($bmp)
    try { $graphics.Clear([System.Drawing.Color]::LightBlue); $bmp.Save($fakeImg,[System.Drawing.Imaging.ImageFormat]::Png) }
    finally { $graphics.Dispose(); $bmp.Dispose() }
    $hash=(Get-FileHash -LiteralPath $fakeImg -Algorithm SHA256).Hash.ToLowerInvariant()
    $null=Add-VisualScenarioResult -Run $run -ScenarioId 'home-default' -ExpectedState 'Home active' -FixtureId 'fixture-1' -Theme Light -Width 1280 -PaneMode single -DpiScalePercent 100 -ObservedDpi 96 -Status PASS -AssertionPass $true -AssertionEvidence ([pscustomobject]@{ state = 'active' }) -ScreenshotPath $fakeImg -ImageHash $hash
    $passWithoutImage=$false
    try { $null=Add-VisualScenarioResult -Run $run -ScenarioId 'missing-image' -ExpectedState 'Home active' -FixtureId 'fixture-1' -Theme Light -Width 1280 -PaneMode single -DpiScalePercent 100 -Status PASS -AssertionPass $true } catch { $passWithoutImage=$true }
    Check $passWithoutImage 'PASS without a captured image is rejected'
    $written=Write-VisualCaptureRun -Run $run
    Check (Test-Path -LiteralPath $written.Manifest) 'manifest written'
    Check (Test-Path -LiteralPath $written.ManifestV2) 'manifest-v2 written'
    $m2=Get-Content -LiteralPath $written.ManifestV2 -Raw | ConvertFrom-Json
    Check ($m2.schemaVersion -eq 2 -and $m2.v2Records.Count -eq 1) 'manifest-v2 contains v2 records'
    $v7=Test-VisualEvidenceV2Record -Record $m2.v2Records[0]
    Check $v7.Valid 'emitted v2 record validates'
    Check ($m2.v2Records[0].dpi -eq 96 -and $m2.v2Records[0].dpiScalePercent -eq 100) 'emitted record preserves observed DPI semantics'

    # Sanitized relative path in manifest
    $firstCapture=$m2.captures[0]
    Check ($firstCapture.screenshotPath -notmatch '^[A-Za-z]:' -and $firstCapture.screenshotPath -eq 'fixture.png') 'screenshot path is sanitized relative'

} finally { if(Test-Path -LiteralPath $tmp){ Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue } }

if($script:failures -gt 0){ Write-Host "RESULT: $script:failures FAILED"; exit 1 }
Write-Host 'RESULT: all visual-evidence-v2 tests passed'; exit 0
