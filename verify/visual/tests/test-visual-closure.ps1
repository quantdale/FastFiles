<#
    Tests for New-VisualClosure.ps1 packet generation and lane honesty.
#>
$ErrorActionPreference='Stop'
$script:failures=0
function Check{param([bool]$c,[string]$d) if($c){Write-Host "  ok: $d"} else{$script:failures++; Write-Host "FAIL: $d"} }
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$closureScript=Join-Path $repo 'verify\visual\New-VisualClosure.ps1'

Add-Type -AssemblyName System.Drawing
$workRoot=Join-Path ([IO.Path]::GetTempPath()) ('ff-closure-test-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $workRoot -Force | Out-Null
try {
    $beforePng=Join-Path $workRoot 'before.png'
    $afterPng=Join-Path $workRoot 'after.png'
    $bmp=[System.Drawing.Bitmap]::new(64,32,[System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    try {
        $g=[System.Drawing.Graphics]::FromImage($bmp)
        try { $g.Clear([System.Drawing.Color]::White) } finally { $g.Dispose() }
        $bmp.Save($beforePng,[System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.SetPixel(10,10,[System.Drawing.Color]::Black)
        $bmp.Save($afterPng,[System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $bmp.Dispose() }

    $outRoot=Join-Path $workRoot 'packet'
    $result=& $closureScript -ScenarioId 'closure-test' -BeforeImagePath $beforePng -AfterImagePath $afterPng `
        -OutputRoot $outRoot -DesignRule 'test rule' -FindingId 'FF-TEST-001' -EvidenceKind 'fixture' `
        -VisualRegression FAIL -VisualReview BLOCKED -UiaResult BLOCKED -AccessibilityResult BLOCKED `
        -FunctionalResult PASS -FunctionalEvidence 'focused CTest slice green' -Disposition OPEN
    Check (Test-Path -LiteralPath $result.PacketPath -PathType Leaf) 'closure packet file created'
    Check (Test-Path -LiteralPath $result.SummaryPath -PathType Leaf) 'closure summary file created'
    $packet=Get-Content -LiteralPath $result.PacketPath -Raw | ConvertFrom-Json
    Check ($packet.schema -eq 'fastfiles.visual-closure.v1') 'packet schema marker'
    Check ($packet.scenarioId -eq 'closure-test') 'packet scenario id'
    Check ($packet.evidenceKind -eq 'fixture') 'fixture evidence labeled honestly'
    Check ($packet.findingId -eq 'FF-TEST-001') 'packet carries first-class finding id'
    Check ($packet.before.hash.Length -eq 64 -and $packet.after.hash.Length -eq 64 -and $packet.before.hash -ne $packet.after.hash) 'before/after hashes recorded and differ'
    Check ($null -ne $packet.comparison -and $null -ne $packet.diffMapPath) 'comparator result and diff map recorded'
    Check ($packet.lanes.visualRegression.state -eq 'FAIL') 'regression lane recorded'
    Check ($packet.lanes.visualReview.state -eq 'BLOCKED') 'review lane defaults to blocked, never fake pass'
    Check ($packet.lanes.uia.state -eq 'BLOCKED') 'uia lane blocked when not executed'
    Check ($packet.lanes.accessibility.state -eq 'BLOCKED') 'accessibility lane blocked when not executed'
    Check ($packet.lanes.functional.state -eq 'PASS') 'functional lane records caller evidence'
    Check ($packet.disposition -eq 'OPEN') 'disposition recorded'
    Check ($packet.worktreeState -in @('clean','dirty','unbound')) 'worktree state is a known value'
    Check ($packet.provenanceState -in @('exact-committed-tree','dirty-development','unbound-development')) 'provenance state is a known value'
    Check ($null -eq $packet.commit -or $packet.commit -match '^(?!0+$)[0-9a-f]{40}$') 'commit is real or null, never fabricated zeros'
    Check ($null -eq $packet.tree -or $packet.tree -match '^(?!0+$)[0-9a-f]{40}$') 'tree is real or null, never fabricated zeros'
    $summary=Get-Content -LiteralPath $result.SummaryPath -Raw
    Check ($summary -match 'FF-TEST-001' -and $summary -match 'fixture') 'summary names finding and evidence kind'

    # Default lanes must be BLOCKED, never PASS.
    $outDefault=Join-Path $workRoot 'packet-default'
    $defResult=& $closureScript -ScenarioId 'closure-default' -BeforeImagePath $beforePng -AfterImagePath $afterPng `
        -OutputRoot $outDefault -EvidenceKind 'fixture'
    $defPacket=Get-Content -LiteralPath $defResult.PacketPath -Raw | ConvertFrom-Json
    Check ($defPacket.lanes.visualReview.state -eq 'BLOCKED') 'default review lane is blocked'
    Check ($defPacket.lanes.uia.state -eq 'BLOCKED') 'default uia lane is blocked'
    Check ($defPacket.lanes.accessibility.state -eq 'BLOCKED') 'default accessibility lane is blocked'
    Check ($defPacket.lanes.functional.state -eq 'BLOCKED') 'default functional lane is blocked'
    Check ($defPacket.disposition -eq 'NEEDS_REVIEW') 'default disposition needs review'

    # Missing before image is a hard error, never an empty packet.
    $threw=$false
    try { & $closureScript -ScenarioId 'x' -BeforeImagePath (Join-Path $workRoot 'nope.png') -AfterImagePath $afterPng -OutputRoot (Join-Path $workRoot 'nope') -EvidenceKind 'fixture' | Out-Null } catch { $threw=$true }
    Check $threw 'missing before image throws'
} finally {
    Remove-Item -LiteralPath $workRoot -Recurse -Force -ErrorAction SilentlyContinue
}
if($script:failures -gt 0){ Write-Host "RESULT: $script:failures FAILED"; exit 1 }
Write-Host 'RESULT: all closure tests passed'; exit 0
