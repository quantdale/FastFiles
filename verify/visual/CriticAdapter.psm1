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

function New-VisualCriticStatusFinding {
    param(
        [Parameter(Mandatory)][string]$FindingId,
        [Parameter(Mandatory)][string]$Surface,
        [Parameter(Mandatory)][string]$ObservedProblem,
        [Parameter(Mandatory)][string]$ExpectedRule
    )
    return New-VisualFinding -FindingId $FindingId -Severity suggestion -Confidence 0.0 `
        -Surface $Surface -Category layout -ObservedProblem $ObservedProblem `
        -ExpectedRule $ExpectedRule -RequiresHumanTasteDecision $true -RegressionCandidate $false
}

function Get-VisualCriticInvoker {
    param([hashtable]$Context, [scriptblock]$Invoker)
    if ($null -ne $Invoker) { return $Invoker }
    if ($Context -and $Context.ContainsKey('Invoker') -and $Context['Invoker'] -is [scriptblock]) {
        return [scriptblock]$Context['Invoker']
    }
    return $null
}

function Test-VisualCriticProviderState {
    param(
        [ValidateSet('fake','real')][string]$Provider = 'fake',
        [hashtable]$Context = @{},
        [scriptblock]$Invoker
    )
    if ($Provider -eq 'fake') {
        return [pscustomobject]@{
            State = 'Available'
            Provider = 'fake'
            Reason = 'deterministic fixture-driven FakeCritic'
            Configured = $true
            AdapterAvailable = $true
            CanSubmit = $false
        }
    }
    $keyPresent = -not [string]::IsNullOrWhiteSpace([string]$env:FASTFILES_VISION_PROVIDER_KEY)
    $endpointValue = [string]$env:FASTFILES_VISION_PROVIDER_ENDPOINT
    $endpointPresent = -not [string]::IsNullOrWhiteSpace($endpointValue)
    if (-not $keyPresent -and -not $endpointPresent) {
        return [pscustomobject]@{
            State = 'BlockedEnvironment'; Provider = 'real'
            Reason = 'BLOCKED_ENVIRONMENT: no vision provider configured'
            Configured = $false; AdapterAvailable = $false; CanSubmit = $false
        }
    }
    if (-not $keyPresent -or -not $endpointPresent) {
        return [pscustomobject]@{
            State = 'Misconfigured'; Provider = 'real'
            Reason = 'MISCONFIGURED: provider key and endpoint must be supplied together'
            Configured = $false; AdapterAvailable = $false; CanSubmit = $false
        }
    }
    $endpoint = $null
    try { $endpoint = [Uri]$endpointValue } catch { }
    if ($null -eq $endpoint -or -not $endpoint.IsAbsoluteUri -or $endpoint.Scheme -notin @('http','https')) {
        return [pscustomobject]@{
            State = 'Misconfigured'; Provider = 'real'
            Reason = 'MISCONFIGURED: provider endpoint must be an absolute HTTP(S) URI'
            Configured = $false; AdapterAvailable = $false; CanSubmit = $false
        }
    }
    $effectiveInvoker = Get-VisualCriticInvoker -Context $Context -Invoker $Invoker
    if ($null -eq $effectiveInvoker) {
        return [pscustomobject]@{
            State = 'NotImplemented'; Provider = 'real'
            Reason = 'NOT_IMPLEMENTED: no real provider adapter is registered'
            Configured = $true; AdapterAvailable = $false; CanSubmit = $false
        }
    }
    return [pscustomobject]@{
        State = 'Available'; Provider = 'real'
        Reason = 'credentials and an explicit provider adapter are configured'
        Configured = $true; AdapterAvailable = $true; CanSubmit = $true
    }
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
        foreach ($f in @($findings)) {
            $v = Test-VisualFinding -Finding $f
            if (-not $v.Valid) { throw "critic returned invalid finding '$($f.findingId)': $($v.Errors -join '; ')" }
        }
        return @($findings)
    }

    $invoker = Get-VisualCriticInvoker -Context $Context -Invoker $null
    $state = Test-VisualCriticProviderState -Provider real -Context $Context -Invoker $invoker
    switch ($state.State) {
        'BlockedEnvironment' {
            return @(New-VisualCriticStatusFinding -FindingId 'BLOCKED-001' -Surface $Surface `
                -ObservedProblem $state.Reason -ExpectedRule 'n/a - provider unavailable')
        }
        'Misconfigured' {
            return @(New-VisualCriticStatusFinding -FindingId 'MISCONFIGURED-001' -Surface $Surface `
                -ObservedProblem $state.Reason -ExpectedRule 'n/a - provider configuration is invalid')
        }
        'NotImplemented' {
            return @(New-VisualCriticStatusFinding -FindingId 'NOT-IMPLEMENTED-001' -Surface $Surface `
                -ObservedProblem $state.Reason -ExpectedRule 'n/a - provider adapter is unavailable')
        }
    }
    $privacyApproved = $Context -and $Context['ExternalReviewAllowed'] -eq $true `
        -and $Context['RedactionPerformed'] -eq $true `
        -and [string]$Context['PrivacyClassification'] -eq 'sanitized'
    if (-not $privacyApproved) {
        return @(New-VisualCriticStatusFinding -FindingId 'PRIVACY-REJECTED-001' -Surface $Surface `
            -ObservedProblem 'PRIVACY_REJECTED: external review requires a sanitized artifact and explicit approval' `
            -ExpectedRule 'raw host paths and user data must never leave the local verification boundary')
    }
    if (-not (Test-Path -LiteralPath $ImagePath -PathType Leaf)) {
        return @(New-VisualCriticStatusFinding -FindingId 'PROVIDER-ERROR-001' -Surface $Surface `
            -ObservedProblem "provider error: image not found: $ImagePath" `
            -ExpectedRule 'provider input must be a readable captured artifact')
    }
    try {
        $rawFindings = @(& $invoker $ImagePath $Surface $Context)
        $normalized = [System.Collections.Generic.List[object]]::new()
        foreach ($raw in $rawFindings) {
            $finding = ConvertTo-VisualFinding -Raw $raw
            $validation = Test-VisualFinding -Finding $finding
            if (-not $validation.Valid) { throw "provider returned invalid finding: $($validation.Errors -join '; ')" }
            [void]$normalized.Add($finding)
        }
        return @($normalized)
    } catch {
        return @(New-VisualCriticStatusFinding -FindingId 'PROVIDER-ERROR-001' -Surface $Surface `
            -ObservedProblem "PROVIDER_ERROR: $($_.Exception.Message)" `
            -ExpectedRule 'provider responses must be available and schema-valid')
    }
}

Export-ModuleMember -Function Invoke-FakeCritic, Invoke-VisualCritic, Test-VisualCriticProviderState
