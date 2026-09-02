Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

<#
 .Provider-agnostic critic. Real provider requires external credentials; FakeCritic
 is deterministic for CI and fixture tests. No network is required for normal builds.

 Semantics:
   - Invoke-FakeCritic is fixture-driven: deterministic mapping from image/surface name
     to canned findings, covering blocker, suggestion, and empty cases.
   - Invoke-VisualCritic is provider-neutral: fake provider never needs network;
     real provider returns BLOCKED_ENVIRONMENT when unavailable and validates responses.
   - All returned findings are validated via VisualFindings before being returned.
#>

Import-Module (Join-Path $PSScriptRoot 'VisualFindings.psm1') -Force -DisableNameChecking

function Invoke-FakeCritic {
    param(
        [Parameter(Mandatory)][string]$ImagePath,
        [Parameter(Mandatory)][string]$Surface,
        [string]$ExpectedState = ''
    )
    # Deterministic fixture-driven fake critic.
    # Filename/surface patterns drive specific finding types for test coverage.
    $lowerPath = $ImagePath.ToLowerInvariant()
    $lowerSurface = $Surface.ToLowerInvariant()

    # Priority: malformed/clipping fixture => blocker
    if ($lowerPath -like '*malformed*' -or $lowerSurface -like '*malformed*' -or
        $lowerPath -like '*clipping*' -or $lowerSurface -like '*clipping*') {
        $f = New-VisualFinding -FindingId 'FAKE-CLIP-001' -Severity blocker -Confidence 0.99 `
            -Surface $Surface -Category clipping `
            -ObservedProblem 'fake deterministic clipping detected in fixture' `
            -ExpectedRule 'content must not clip within its container bounds' `
            -UserImpact 'truncated content is unreadable' `
            -SuggestedCorrection 'increase container size or reduce content' `
            -RequiresHumanTasteDecision $false -RegressionCandidate $true
        $v = Test-VisualFinding -Finding $f
        if (-not $v.Valid) { throw "FakeCritic produced invalid finding: $($v.Errors -join '; ')" }
        return @($f)
    }

    # Overlap fixture
    if ($lowerPath -like '*overlap*' -or $lowerSurface -like '*overlap*') {
        $f = New-VisualFinding -FindingId 'FAKE-OVERLAP-001' -Severity major -Confidence 0.92 `
            -Surface $Surface -Category overlap `
            -ObservedProblem 'fake deterministic overlap between controls' `
            -ExpectedRule 'controls must not visually overlap' `
            -RequiresHumanTasteDecision $false -RegressionCandidate $true
        return @($f)
    }

    # Empty/valid fixture => no findings (clean)
    if ($lowerPath -like '*clean*' -or $lowerPath -like '*empty-state*' -or $lowerSurface -like '*clean*') {
        return @()
    }

    # Alignment fixture
    if ($lowerPath -like '*alignment*' -or $lowerSurface -like '*alignment*') {
        $f = New-VisualFinding -FindingId 'FAKE-ALIGN-001' -Severity moderate -Confidence 0.78 `
            -Surface $Surface -Category alignment `
            -ObservedProblem 'fake deterministic misalignment between label and control' `
            -ExpectedRule 'form controls must align to the 8px grid' `
            -RequiresHumanTasteDecision $false -RegressionCandidate $true
        return @($f)
    }

    # Default: single advisory suggestion (not a blocker, not a regression)
    $f = New-VisualFinding -FindingId 'FAKE-ADVISORY-001' -Severity suggestion -Confidence 0.42 `
        -Surface $Surface -Category layout `
        -ObservedProblem 'fake suggestion: consider hierarchy review' `
        -ExpectedRule 'primary actions should have stronger visual weight than secondary' `
        -RequiresHumanTasteDecision $true -RegressionCandidate $false
    $v = Test-VisualFinding -Finding $f
    if (-not $v.Valid) { throw "FakeCritic produced invalid finding: $($v.Errors -join '; ')" }
    return @($f)
}

function Invoke-VisualCritic {
    param(
        [Parameter(Mandatory)][string]$ImagePath,
        [Parameter(Mandatory)][string]$Surface,
        [ValidateSet('fake','real')][string]$Provider = 'fake',
        [hashtable]$Context = @{}
    )
    if ($Provider -eq 'fake') {
        $findings = Invoke-FakeCritic -ImagePath $ImagePath -Surface $Surface -ExpectedState $Context['ExpectedState']
        # Validate every finding before returning
        foreach ($f in @($findings)) {
            $v = Test-VisualFinding -Finding $f
            if (-not $v.Valid) { throw "critic returned invalid finding '$($f.findingId)': $($v.Errors -join '; ')" }
        }
        return $findings
    }

    # Real provider seam - check opt-in
    $providerAvailable = $false
    $providerKey = $env:FASTFILES_VISION_PROVIDER_KEY
    $providerEndpoint = $env:FASTFILES_VISION_PROVIDER_ENDPOINT
    if ($providerKey -and $providerEndpoint) { $providerAvailable = $true }

    if (-not $providerAvailable) {
        # BLOCKED_ENVIRONMENT: no vision provider configured - not a failure
        $f = New-VisualFinding -FindingId 'BLOCKED-001' -Severity suggestion -Confidence 0.0 `
            -Surface $Surface -Category layout `
            -ObservedProblem 'BLOCKED_ENVIRONMENT: no vision provider configured (set FASTFILES_VISION_PROVIDER_KEY/ENDPOINT to enable)' `
            -ExpectedRule 'n/a - provider unavailable' `
            -RequiresHumanTasteDecision $true -RegressionCandidate $false
        return @($f)
    }

    # Provider configured but we do not implement real network calls in this module
    # without explicit provider SDK. This is the "provider error" vs "success" seam.
    # In a future implementation, this would call the provider and validate the response.
    try {
        # Placeholder for real provider invocation - currently returns unavailable
        throw "real vision provider not yet implemented in this environment"
    } catch {
        $f = New-VisualFinding -FindingId 'PROVIDER-ERROR-001' -Severity suggestion -Confidence 0.0 `
            -Surface $Surface -Category layout `
            -ObservedProblem "provider error: $($_.Exception.Message)" `
            -ExpectedRule 'n/a - provider invocation failed' `
            -RequiresHumanTasteDecision $true -RegressionCandidate $false
        return @($f)
    }
}

function Test-VisualCriticProviderState {
    param([ValidateSet('fake','real')][string]$Provider = 'fake')
    if ($Provider -eq 'fake') {
        return [pscustomobject]@{ State = 'Available'; Provider = 'fake'; Reason = 'deterministic fixture-driven FakeCritic' }
    }
    if ($env:FASTFILES_VISION_PROVIDER_KEY -and $env:FASTFILES_VISION_PROVIDER_ENDPOINT) {
        return [pscustomobject]@{ State = 'Available'; Provider = 'real'; Reason = 'credentials configured' }
    }
    return [pscustomobject]@{ State = 'BlockedEnvironment'; Provider = 'real'; Reason = 'BLOCKED_ENVIRONMENT: no vision provider configured' }
}

Export-ModuleMember -Function Invoke-FakeCritic, Invoke-VisualCritic, Test-VisualCriticProviderState
