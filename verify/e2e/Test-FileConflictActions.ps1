# original-vision-journey-recovery 2.10: public native conflict decisions on owned fixtures.
[CmdletBinding()]
param(
    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path,
    [Parameter(Mandatory)][string] $ArtifactsDir
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = (Resolve-Path -LiteralPath $RepoRoot).Path
$artifacts = [IO.Path]::GetFullPath($ArtifactsDir)
if (Test-Path -LiteralPath $artifacts) { throw 'A fresh artifact directory is required; existing evidence is preserved.' }
New-Item -ItemType Directory -Path $artifacts | Out-Null
Import-Module (Join-Path $repo 'verify/core/Provenance.psm1') -Force -DisableNameChecking
$exe = Join-Path $repo 'build/debug-diagnostic/src/ui/FastFiles.exe'
$binaries = @(Get-DefaultCertificationBinaries -RepoRoot $repo -BuildDirectory (Split-Path (Split-Path (Split-Path $exe))))
$gate = Invoke-ProvenanceGate -RepoRoot $repo -Binaries $binaries -Mode exact-tree -Stage conflict-actions -PassThru
$gate.evidence | ConvertTo-Json -Depth 20 | Set-Content (Join-Path $artifacts 'provenance.json') -Encoding utf8NoBOM
if (-not $gate.ok) { throw $gate.message }
$e2e = Import-Module (Join-Path $repo 'verify/e2e/FastFilesE2E.psm1') -Force -DisableNameChecking -PassThru
$validation = Get-Module UiAutomationValidation
Import-Module (Join-Path $repo 'verify/visual/VisualCapture.psm1') -Force -DisableNameChecking
$run = New-VisualCaptureRun -Root (Join-Path $artifacts 'images') -ApplicationSha (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant() -RepoRoot $repo
$results = [Collections.Generic.List[object]]::new()
$cleanups = [Collections.Generic.List[object]]::new()
$means = @{}
$failure = $null
try {
    foreach ($theme in @('Light', 'Dark')) {
        $ctx = $null
        $dialog = $null
        try {
            # Seed ordinary persisted settings only inside a fresh session sandbox.
            # No system preference, production profile or hidden application hook changes.
            $sandbox = & $validation { New-UiaSessionSandbox -ScenarioId conflict-actions -InvocationId ([guid]::NewGuid().ToString('N')) }
            $settingsDir = Join-Path $sandbox.SessionRoot 'LocalAppData/FastFiles'
            New-Item -ItemType Directory -Path $settingsDir -Force | Out-Null
            @{schemaVersion=1;appearance=@{theme=$theme;density='Compact'}} | ConvertTo-Json -Depth 3 |
                Set-Content (Join-Path $settingsDir 'settings.json') -Encoding utf8NoBOM
            $ctx = New-UiaScenarioContext -Exe $exe -RepoRoot $repo -ScenarioId ('conflict-'+$theme) -SessionRoot $sandbox.SessionRoot -LogFn {param($m) Write-Host $m}
            if (-not $ctx -or -not $ctx.Main) { throw 'The actual application window is unavailable.' }
            $fixture = New-E2EFixture -SessionFixtureRoot $ctx.FixtureRoot
            [void]$ctx.Fixtures.Add($fixture.Root)
            $sentinel = Join-Path $fixture.Destination 'unrelated-sentinel.txt'
            Set-Content -LiteralPath $sentinel -Value 'untouched owned sentinel' -Encoding utf8NoBOM
            $sentinelHash = (Get-FileHash -LiteralPath $sentinel -Algorithm SHA256).Hash
            foreach ($choice in @('Cancel', 'Skip', 'Keep both', 'Replace')) {
                $id = $theme.ToLowerInvariant()+'-'+$choice.Replace(' ', '-').ToLowerInvariant()
                $name = 'collision-'+$id+'.txt'
                $source = Assert-E2EPathWithinRoot -Root $fixture.Root -Path (Join-Path $fixture.Operations $name)
                $destination = Assert-E2EPathWithinRoot -Root $fixture.Root -Path (Join-Path $fixture.Destination $name)
                Set-Content -LiteralPath $source -Value ('incoming '+$id) -Encoding utf8NoBOM
                Set-Content -LiteralPath $destination -Value ('existing '+$id) -Encoding utf8NoBOM
                $sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
                $destinationHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
                & $e2e {param($context,$fixture,$name)
                    $nav = Invoke-UiaAddressBarNavigate -Driver $context.Driver -Main $context.Main -ProcessId $context.Launched.Id -Path $fixture.Operations -ExpectedLeaf Operations
                    if (-not $nav.Pass) { throw $nav.Detail }
                    $selected = Select-E2EItem -Context $context -Name $name -TimeoutMs 10000
                    if (-not $selected.Pass) { throw $selected.Detail }
                    $copy = Invoke-E2ENavigationCommand -Context $context -Query copy -Name Copy -CommandId file.copy
                    if (-not $copy.Pass) { throw $copy.Detail }
                    $nav = Invoke-UiaAddressBarNavigate -Driver $context.Driver -Main $context.Main -ProcessId $context.Launched.Id -Path $fixture.Destination -ExpectedLeaf Destination
                    if (-not $nav.Pass) { throw $nav.Detail }
                    $paste = Invoke-E2ENavigationCommand -Context $context -Query paste -Name Paste -CommandId file.paste
                    if (-not $paste.Pass) { throw $paste.Detail }
                } $ctx $fixture $name
                $dialog = Find-UiaElement -Driver $ctx.Driver -ClassName FastFilesConflictDialog -ProcessId $ctx.Launched.Id -TimeoutMs 8000
                $bounds = Get-UiaElementProperty -Driver $ctx.Driver -Element $dialog -Name BoundingRectangle
                $controls = @{}
                $missing = @()
                foreach ($label in @('Replace', 'Skip', 'Keep both', 'Cancel', 'Do this for all remaining conflicts')) {
                    try { $controls[$label] = Find-UiaElement -Driver $ctx.Driver -Name $label -FromElement $dialog -TimeoutMs 1000 }
                    catch { if ($_.Exception.Message -notlike '*UiaTargetNotFound*') { throw }; $missing += $label }
                }
                # Capturing the real visible dialog is independent of action readiness.
                # Missing names still fail below; this frame never certifies their presence.
                if ($choice -eq 'Cancel') {
                    $shot = Capture-VisualWindow -Run $run -Element $dialog -ScenarioId ('conflict-'+$id) -AssertionPass ([bool]$dialog)
                    Add-Type -AssemblyName System.Drawing
                    $bitmap = [Drawing.Bitmap]::new($shot.Path)
                    try {
                        $sum = 0.0; $samples = 0
                        for ($y=45; $y -lt $bitmap.Height-10; $y+=11) {
                            for ($x=10; $x -lt $bitmap.Width-10; $x+=11) {
                                $pixel=$bitmap.GetPixel($x,$y);$sum+=($pixel.R+$pixel.G+$pixel.B)/3.0;$samples++
                            }
                        }
                        $means[$theme] = $sum / $samples
                    } finally { $bitmap.Dispose() }
                }
                if ($missing.Count -gt 0) { throw ('Named conflict actions missing: '+($missing -join ', ')) }
                foreach ($control in $controls.Values) {
                    $rect = Get-UiaElementProperty -Driver $ctx.Driver -Element $control -Name BoundingRectangle
                    if ($rect.width -le 0 -or $rect.height -le 0 -or $rect.x -lt $bounds.x -or $rect.y -lt $bounds.y -or
                        $rect.x+$rect.width -gt $bounds.x+$bounds.width -or $rect.y+$rect.height -gt $bounds.y+$bounds.height) {
                        throw 'A conflict decision has clipped or empty physical bounds.'
                    }
                }
                $initial = Wait-E2ECondition -TimeoutMs 1500 -Description 'safe default Skip focus' -Condition {
                    [bool](Get-UiaElementProperty -Driver $ctx.Driver -Element $controls['Skip'] -Name HasKeyboardFocus)
                }
                if (-not $initial.Pass) { throw 'Skip did not retain initial keyboard focus.' }
                $tabOrder = @('Keep both','Cancel','Do this for all remaining conflicts','Replace','Skip')
                foreach ($label in $tabOrder) {
                    $null = Send-UiaInput -Driver $ctx.Driver -Keys '{TAB}'
                    $focused = Wait-E2ECondition -TimeoutMs 1500 -Description ('Tab focus '+$label) -Condition {
                        [bool](Get-UiaElementProperty -Driver $ctx.Driver -Element $controls[$label] -Name HasKeyboardFocus)
                    }
                    if (-not $focused.Pass) { throw ('Keyboard Tab did not reach '+$label) }
                }
                $clicked = & $e2e {param($context,$element,$owner) Invoke-E2EButton -Context $context -Element $element -Owner $owner} $ctx $controls[$choice] $dialog
                if (-not $clicked) { throw ('Native input could not activate '+$choice) }
                if (-not (Wait-UiaElementGone -Driver $ctx.Driver -ClassName FastFilesConflictDialog -ProcessId $ctx.Launched.Id -TimeoutMs 5000 -PollIntervalMs 100)) { throw 'Conflict dialog did not close.' }
                $dialog = $null
                $copyPath = Join-Path $fixture.Destination ([IO.Path]::GetFileNameWithoutExtension($name)+' (2).txt')
                $expectedHash = if ($choice -eq 'Replace') { $sourceHash } else { $destinationHash }
                $outcome = Wait-E2ECondition -TimeoutMs 8000 -Description ('filesystem decision '+$id) -Condition {
                    ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -eq $expectedHash) -and
                    ($choice -ne 'Keep both' -or ((Test-Path -LiteralPath $copyPath) -and (Get-FileHash -LiteralPath $copyPath -Algorithm SHA256).Hash -eq $sourceHash))
                }
                Start-Sleep -Milliseconds 300
                $preserved = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -eq $sourceHash -and
                    (Get-FileHash -LiteralPath $sentinel -Algorithm SHA256).Hash -eq $sentinelHash
                if (-not $outcome.Pass -or -not $preserved) { throw ('Wrong filesystem result for '+$id) }
                if ($choice -ne 'Keep both' -and (Test-Path -LiteralPath $copyPath)) { throw 'Unexpected duplicate after a different conflict decision.' }
                [void]$results.Add(@{id=$id;pass=$true;initialSkipFocus=$true;tabOrder=$tabOrder;sourceAndSentinelPreserved=$preserved;bounds=$bounds})
                Write-Host ('PASS: '+$id+'; named controls, physical bounds, Tab traversal, exact content outcome')
            }
        } finally {
            if ($dialog -and $ctx) { $null=Send-UiaInput -Driver $ctx.Driver -Keys '{ESC}' }
            if ($ctx) {
                $cleanup=Remove-UiaScenarioContext -Context $ctx -LogFn {param($m) Write-Host $m}
                [void]$cleanups.Add($cleanup)
                if (-not $cleanup.Pass) { throw 'Owned lifecycle cleanup failed.' }
            }
        }
    }
    if ($means['Light'] -le 128 -or $means['Dark'] -ge 128) { throw 'Actual conflict pixels did not establish light and dark themes.' }
} catch {
    $failure=$_.Exception.Message
} finally {
    @{passed=$results.Count;failure=$failure;cases=@($results.ToArray());cleanup=@($cleanups.ToArray());brightness=$means;pixelReviewRequired=$true} |
        ConvertTo-Json -Depth 14 | Set-Content (Join-Path $artifacts 'result.json') -Encoding utf8NoBOM
}
if ($failure) { throw $failure }
Write-Host 'PASS: eight real conflict decisions; actual light/dark screenshot review is still required.'
