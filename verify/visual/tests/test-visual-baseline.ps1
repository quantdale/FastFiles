<#
    Tests for BaselineManifest.psm1 lifecycle.
#>
$ErrorActionPreference='Stop'
$script:failures=0
function Check{param([bool]$c,[string]$d) if($c){Write-Host "  ok: $d"} else{$script:failures++; Write-Host "FAIL: $d"} }
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
Import-Module (Join-Path $repo 'verify\visual\BaselineManifest.psm1') -Force
Add-Type -AssemblyName System.Drawing

$tmp=Join-Path ([System.IO.Path]::GetTempPath()) ('ff-baseline-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tmp -Force | Out-Null
try {
    # Create a tiny fixture image
    $imgPath=Join-Path $tmp 'baseline.png'
    $bmp=[System.Drawing.Bitmap]::new(16,16); $g=[System.Drawing.Graphics]::FromImage($bmp); try{ $g.Clear([System.Drawing.Color]::LightBlue); $bmp.Save($imgPath,[System.Drawing.Imaging.ImageFormat]::Png) } finally{ $g.Dispose(); $bmp.Dispose() }
    $imgPath2=Join-Path $tmp 'updated.png'
    $bmp2=[System.Drawing.Bitmap]::new(16,16); $g2=[System.Drawing.Graphics]::FromImage($bmp2); try{ $g2.Clear([System.Drawing.Color]::LightCoral); $bmp2.Save($imgPath2,[System.Drawing.Imaging.ImageFormat]::Png) } finally{ $g2.Dispose(); $bmp2.Dispose() }

    $commit='a' * 40; $commit2='b'*40

    $manifest=New-BaselineManifest
    $v0=Test-BaselineManifest -Manifest $manifest
    Check $v0.Valid 'empty manifest valid'

    $e1=Add-BaselineEntry -Manifest $manifest -ScenarioId 'home-default' -ImagePath $imgPath -SourceCommit $commit -Theme light -Density comfortable -Dpi 96 -ViewportWidth 1280 -ViewportHeight 800 -PaneMode single -BaselineRoot $tmp -Rationale 'initial'
    Check ($e1.scenarioId -eq 'home-default' -and $e1.screenshotHash.Length -eq 64) 'add entry with hash'

    $v1=Test-BaselineManifest -Manifest $manifest
    Check $v1.Valid 'manifest with one entry valid'

    # Duplicate without Force should fail
    $dupFails=$false; try{ $null=Add-BaselineEntry -Manifest $manifest -ScenarioId 'home-default' -ImagePath $imgPath -SourceCommit $commit -Theme light -ViewportWidth 1280 -ViewportHeight 800 -BaselineRoot $tmp } catch{$dupFails=$true}
    Check $dupFails 'duplicate without -Force rejected'

    # Duplicate with Force should succeed
    $e1upd=Add-BaselineEntry -Manifest $manifest -ScenarioId 'home-default' -ImagePath $imgPath2 -SourceCommit $commit2 -Theme light -ViewportWidth 1280 -ViewportHeight 800 -BaselineRoot $tmp -Force -Rationale 'update'
    Check ($e1upd.sourceCommit -eq $commit2 -and $manifest.entries.Count -eq 1) 'duplicate with -Force updates'

    # Environment mismatch: different theme should be distinct
    $e2=Add-BaselineEntry -Manifest $manifest -ScenarioId 'home-default' -ImagePath $imgPath -SourceCommit $commit -Theme dark -ViewportWidth 1280 -ViewportHeight 800 -BaselineRoot $tmp
    Check ($manifest.entries.Count -eq 2) 'different theme is distinct entry'

    # Get-BaselineEntry filtering
    $lightEntries=Get-BaselineEntry -Manifest $manifest -ScenarioId 'home-default' -Theme light
    Check ($lightEntries.Count -eq 1 -and $lightEntries[0].theme -eq 'light') 'filter by theme'

    # Sanitized paths: absolute without BaselineRoot should fail
    $absFails=$false; try{ $null=Add-BaselineEntry -Manifest $manifest -ScenarioId 'new-scenario' -ImagePath $imgPath -SourceCommit $commit -Theme light } catch{ $absFails=$true }
    Check $absFails 'absolute path without BaselineRoot rejected'

    # Save/load round-trip
    $manifestPath=Join-Path $tmp 'manifest.json'
    Save-BaselineManifest -Manifest $manifest -Path $manifestPath
    $loaded=Load-BaselineManifest -Path $manifestPath
    Check ($loaded.entries.Count -eq 2) 'save/load round-trip'

    # Compare against baseline: identical image should pass
    $compareResult=Compare-AgainstBaseline -Manifest $loaded -ScenarioId 'home-default' -CurrentImagePath $imgPath2 -Theme light -BaselineRoot $tmp -Threshold 0.01
    Check ($compareResult.Status -eq 'Pass') 'compare identical (after update) passes'

    # Missing baseline
    $missing=Compare-AgainstBaseline -Manifest $loaded -ScenarioId 'nonexistent' -CurrentImagePath $imgPath -Threshold 0.01
    Check ($missing.Status -eq 'MissingBaseline') 'missing baseline detected'

    # Ambiguous: add same scenario different viewport then query without viewport filter catches 2
    # For this we need same scenario+theme but different viewport that would be ambiguous if not filtered
    # Add entry with same scenario+theme but different dpi, then query without dpi should be ambiguous only if we have two that match all filters
    # Simpler: verify that duplicate environmentKey is caught by validation
    $manifest.entries += $manifest.entries[0]
    $vdup=Test-BaselineManifest -Manifest $manifest
    Check (-not $vdup.Valid) 'duplicate environmentKey fails validation'

} finally { if(Test-Path -LiteralPath $tmp){ Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue } }

if($script:failures -gt 0){ Write-Host "RESULT: $script:failures FAILED"; exit 1 }
Write-Host 'RESULT: all baseline tests passed'; exit 0
