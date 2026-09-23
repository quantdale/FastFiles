<#
    hardening-campaign Phase 5: headless tests for the clean-host/install/
    service certification harness (HostContamination.psm1 +
    InstallCertificationPlan.psm1).

    Fully fixture-driven through injectable probe overrides and stub
    source/binding/eligibility records: never touches the real service,
    local group, scheduled tasks, named pipes, registry, or filesystem
    markers, and never reads prior verify/runs evidence.
#>

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
Import-Module (Join-Path $repoRoot 'verify\certification\HostContamination.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $repoRoot 'verify\certification\InstallCertificationPlan.psm1') -Force -DisableNameChecking

$failures = 0
function Check {
    param([bool] $Condition, [string] $Description)
    if ($Condition) { Write-Host "ok: $Description" } else { Write-Host "FAIL: $Description"; $script:failures++ }
}

$markerIds = Get-HostContaminationMarkerIds
$headSha = 'c' * 40
$treeSha = 'd' * 40

function New-AbsentOverrides {
    $overrides = @{}
    foreach ($id in $markerIds) { $overrides[$id] = @{ Present = $false; Detail = "fixture-absent:$id" } }
    return $overrides
}

function New-StubSource {
    return [pscustomobject]@{
        headSha = $headSha; treeSha = $treeSha; treeSemantic = 'HEAD^{tree}'
        sourceClean = $true; stateDigest = ('e' * 64); mode = 'development'
    }
}

function New-StubBinding {
    param([bool] $Valid = $true, [string[]] $IssueCodes = @())
    return [pscustomobject]@{ valid = $Valid; issueCodes = @($IssueCodes); setIdentity = $headSha }
}

function New-StubBinary {
    return [pscustomobject]@{
        filename = 'FastFiles.exe'; path = 'C:\fixture\FastFiles.exe'
        sha256 = ('ab' * 32); buildIdentity = $headSha
        peArchitecture = 'x64'; issues = @()
    }
}

function New-StubEligibility {
    param([string[]] $Absences = @('clean-host-requirement', 'host-contamination'))
    return [pscustomobject]@{
        Absences = @($Absences); Eligible = ($Absences.Count -eq 0)
        ServiceState = 'absent'; GroupState = 'group-missing'
        PinState = 'absent'; Elevated = $false
    }
}

$evidenceRoots = [System.Collections.Generic.List[string]]::new()

try {
    # 1. Marker inventory covers the installer-written surface.
    Check ($markerIds.Count -eq 11) 'detector exposes eleven contamination markers'
    foreach ($expected in @('processes', 'service', 'install-dir', 'program-data', 'user-data', 'uia-sessions', 'scheduled-task', 'client-group', 'named-pipes', 'uninstall-registry', 'wer-registry')) {
        Check ($markerIds -contains $expected) "marker inventory contains '$expected'"
    }

    # 2. No markers => CLEAN (all probes fixture-absent; the real host is untouched).
    $cleanPreflight = Get-HostContamination -ProbeOverrides (New-AbsentOverrides)
    Check ($cleanPreflight.hostState -eq 'CLEAN') 'all-absent fixtures report HOST_STATE=CLEAN'
    Check ($cleanPreflight.clean -and (-not $cleanPreflight.veto)) 'CLEAN preflight carries clean=true and veto=false'
    Check (@($cleanPreflight.presentMarkers).Count -eq 0 -and @($cleanPreflight.unknownMarkers).Count -eq 0) 'CLEAN preflight lists no present or unknown markers'
    Check (@($cleanPreflight.markers).Count -eq 11) 'CLEAN preflight returns one evidence row per marker'

    # 3. Any single marker present => CONTAMINATED, never CLEAN.
    foreach ($id in $markerIds) {
        $overrides = New-AbsentOverrides
        $overrides[$id] = @{ Present = $true; Detail = "fixture-present:$id" }
        $reading = Get-HostContamination -ProbeOverrides $overrides
        Check ($reading.hostState -eq 'CONTAMINATED') "marker '$id' present implies HOST_STATE=CONTAMINATED"
        Check ($reading.hostState -ne 'CLEAN') "marker '$id' present is never reported CLEAN"
        Check ($reading.veto -and (-not $reading.clean)) "marker '$id' present vetoes install certification"
        Check (@($reading.presentMarkers) -contains $id) "marker '$id' appears in presentMarkers"
    }

    # 4. Probe error => UNKNOWN (fail-closed), never CLEAN.
    $errorOverrides = New-AbsentOverrides
    $errorOverrides['named-pipes'] = { throw 'fixture-probe-boom' }
    $unknownReading = Get-HostContamination -ProbeOverrides $errorOverrides
    Check ($unknownReading.hostState -eq 'UNKNOWN') 'errored probe with no present markers reports HOST_STATE=UNKNOWN'
    Check ($unknownReading.hostState -ne 'CLEAN') 'errored probe is never reported CLEAN'
    Check ($unknownReading.veto) 'UNKNOWN preflight vetoes install certification'
    Check (@($unknownReading.unknownMarkers) -contains 'named-pipes') 'errored probe id appears in unknownMarkers'

    # 5. A present marker dominates concurrent probe errors (still CONTAMINATED).
    $mixedOverrides = New-AbsentOverrides
    $mixedOverrides['service'] = @{ Present = $true; Detail = 'fixture-present:service' }
    $mixedOverrides['named-pipes'] = { throw 'fixture-probe-boom' }
    $mixedReading = Get-HostContamination -ProbeOverrides $mixedOverrides
    Check ($mixedReading.hostState -eq 'CONTAMINATED') 'present marker plus probe error still reports CONTAMINATED'

    # 6. The catalog covers all twelve section-11 steps with mapping labels.
    $catalog = Get-InstallCertificationStepCatalog
    $expectedSteps = @('preflight-contamination', 'provenance-binding', 'install-execution', 'service-registration-startup', 'app-startup', 'handshake-verification', 'privileged-eligibility', 'representative-operation', 'shutdown', 'uninstall-execution', 'cleanup-verification', 'residue-scan')
    Check (@($catalog).Count -eq 12) 'step catalog has twelve section-11 steps'
    Check ((@($catalog | ForEach-Object { $_.Id }) -join ',') -eq ($expectedSteps -join ',')) 'step catalog order matches the section-11 journey'
    foreach ($entry in @($catalog)) {
        Check (-not [string]::IsNullOrWhiteSpace($entry.MapsTo)) "step '$($entry.Id)' carries its section-11 mapping label"
    }

    # 7. The skeleton on a clean preflight: vocabulary-valid statuses, reasons, no veto.
    $vocab = Get-InstallCertificationStepVocabulary
    $skeleton = New-InstallCertificationSkeletonResult -Preflight $cleanPreflight -SourceState (New-StubSource) `
        -Binding (New-StubBinding) -BinaryRecords @((New-StubBinary)) -Eligibility (New-StubEligibility) `
        -Residual $cleanPreflight -CandidatePath 'C:\fixture\FastFilesSetup.exe'
    Check (@($skeleton.steps).Count -eq 12) 'skeleton emits one result per catalog step'
    $stepIds = @($skeleton.steps | ForEach-Object { $_.id })
    Check (($stepIds -join ',') -eq ($expectedSteps -join ',')) 'skeleton step ids match the catalog in order'
    foreach ($step in @($skeleton.steps)) {
        Check ($vocab -contains $step.status) "step '$($step.id)' status '$($step.status)' is in the vocabulary"
        Check (-not [string]::IsNullOrWhiteSpace($step.reason)) "step '$($step.id)' carries a machine-readable reason"
    }
    Check ((-not $skeleton.veto) -and ($skeleton.hostState -eq 'CLEAN')) 'clean preflight does not veto the skeleton'
    Check ($skeleton.overallStatus -eq 'BLOCKED_EXTERNAL') 'skeleton without live execution classifies BLOCKED_EXTERNAL overall'
    Check ($vocab -contains $skeleton.overallStatus) 'skeleton overall status is vocabulary-valid'

    # 8. BLOCKED_EXTERNAL is never reported as PASS.
    $blockedAsPass = @($skeleton.steps | Where-Object { $_.requiresExternal -and $_.status -eq 'PASS' })
    Check ($blockedAsPass.Count -eq 0) 'no externally-gated skeleton step reports PASS'
    $threwExternalPass = $false
    try { New-CertStepResult -Id 'install-execution' -Status 'PASS' -Reason 'fixture' -RequiresExternal | Out-Null } catch { $threwExternalPass = ($_.Exception.Message -match 'cert-step-external-pass-violation') }
    Check ($threwExternalPass) 'externally-gated PASS is rejected by the constructor'
    $threwVocab = $false
    try { New-CertStepResult -Id 'install-execution' -Status 'MAYBE' -Reason 'fixture' | Out-Null } catch { $threwVocab = ($_.Exception.Message -match 'cert-step-vocabulary-violation') }
    Check ($threwVocab) 'out-of-vocabulary status is rejected by the constructor'
    $threwReason = $false
    try { New-CertStepResult -Id 'install-execution' -Status 'SKIPPED' -Reason '' | Out-Null } catch { $threwReason = ($_.Exception.Message -match 'cert-step-reason-required') }
    Check ($threwReason) 'missing reason is rejected by the constructor'
    $threwId = $false
    try { New-CertStepResult -Id 'not-a-step' -Status 'SKIPPED' -Reason 'fixture' | Out-Null } catch { $threwId = ($_.Exception.Message -match 'cert-step-unknown-id') }
    Check ($threwId) 'off-catalog step id is rejected by the constructor'

    # 9. A contaminated preflight vetoes the skeleton (BLOCKED_EXTERNAL, never PASS).
    $contamOverrides = New-AbsentOverrides
    $contamOverrides['service'] = @{ Present = $true; Detail = 'fixture-present:service' }
    $contamPreflight = Get-HostContamination -ProbeOverrides $contamOverrides
    $vetoed = New-InstallCertificationSkeletonResult -Preflight $contamPreflight -SourceState (New-StubSource) `
        -Binding (New-StubBinding) -BinaryRecords @((New-StubBinary)) -Eligibility (New-StubEligibility) `
        -Residual $contamPreflight -CandidatePath 'C:\fixture\FastFilesSetup.exe'
    Check ($vetoed.veto -and ($vetoed.hostState -eq 'CONTAMINATED')) 'contaminated preflight vetoes the skeleton'
    Check ($vetoed.overallStatus -eq 'BLOCKED_EXTERNAL') 'vetoed skeleton classifies BLOCKED_EXTERNAL overall'
    Check ($vetoed.overallStatus -ne 'PASS') 'vetoed skeleton never reports PASS'
    $preflightStep = @($vetoed.steps | Where-Object { $_.id -eq 'preflight-contamination' })[0]
    Check (($preflightStep.status -eq 'BLOCKED_EXTERNAL') -and ($preflightStep.reason -eq 'host-contaminated-veto')) 'veto step records host-contaminated-veto'
    $installStep = @($vetoed.steps | Where-Object { $_.id -eq 'install-execution' })[0]
    Check (($installStep.status -eq 'BLOCKED_EXTERNAL') -and ($installStep.reason -match 'preflight-veto')) 'install step records the preflight veto reason'

    # 10. A failed binding fails the provenance step and the skeleton overall.
    $badBinding = New-StubBinding -Valid $false -IssueCodes @('STALE_BINARY')
    $badSkeleton = New-InstallCertificationSkeletonResult -Preflight $cleanPreflight -SourceState (New-StubSource) `
        -Binding $badBinding -BinaryRecords @((New-StubBinary)) -Eligibility (New-StubEligibility) `
        -Residual $cleanPreflight
    $provStep = @($badSkeleton.steps | Where-Object { $_.id -eq 'provenance-binding' })[0]
    Check ($provStep.status -eq 'FAIL') 'invalid binding fails the provenance step'
    Check ($badSkeleton.overallStatus -eq 'FAIL') 'invalid binding fails the skeleton overall'

    # 11. The evidence bundle JSON parses and carries headSha; the Markdown exists.
    $evidenceRoot = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-cleanhost-evidence-' + [guid]::NewGuid().ToString('N'))
    $evidenceRoots.Add($evidenceRoot)
    $bundle = Write-InstallCertificationEvidence -OutputRoot $evidenceRoot -SourceState (New-StubSource) `
        -BinaryRecords @((New-StubBinary)) -Binding (New-StubBinding) -Steps $skeleton.steps `
        -OverallStatus $skeleton.overallStatus -OverallReason $skeleton.overallReason `
        -Preflight $cleanPreflight -Residual $cleanPreflight -Eligibility (New-StubEligibility) `
        -EligibilitySource 'fixture' -RepoRoot $repoRoot -CandidatePath 'C:\fixture\FastFilesSetup.exe' -Stamp 'fixture-stamp'
    Check ((Test-Path -LiteralPath $bundle.JsonPath -PathType Leaf)) 'evidence bundle writes the JSON file'
    Check ((Test-Path -LiteralPath $bundle.MarkdownPath -PathType Leaf)) 'evidence bundle writes the Markdown file'
    $parsed = Get-Content -LiteralPath $bundle.JsonPath -Raw | ConvertFrom-Json
    Check ($parsed.source.headSha -eq $headSha) 'evidence JSON carries the source headSha'
    Check ($parsed.source.treeSha -eq $treeSha) 'evidence JSON carries the source treeSha'
    Check ($parsed.overall.status -eq $skeleton.overallStatus) 'evidence JSON carries the skeleton overall status'
    Check ($vocab -contains $parsed.overall.status) 'evidence overall status is vocabulary-valid'
    Check (@($parsed.steps).Count -eq 12) 'evidence JSON binds all twelve per-step results'
    Check ($parsed.preflight.hostState -eq 'CLEAN') 'evidence JSON binds the preflight HOST_STATE'
    Check ($parsed.residual.hostState -eq 'CLEAN') 'evidence JSON binds the residual HOST_STATE'
    Check ($parsed.binaries.Count -eq 1 -and $parsed.binaries[0].sha256 -eq ('ab' * 32)) 'evidence JSON binds binary SHA-256s'
    $mdText = Get-Content -LiteralPath $bundle.MarkdownPath -Raw
    Check (($mdText -match $headSha) -and ($mdText -match $skeleton.overallStatus)) 'evidence Markdown records headSha and overall status'
}
finally {
    foreach ($root in $evidenceRoots) {
        Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
    }
}

if ($failures -gt 0) {
    Write-Host "FAILED: $failures check(s)"
    exit 1
}
Write-Host 'All clean-host harness checks passed.'
exit 0
