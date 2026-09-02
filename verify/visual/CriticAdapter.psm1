Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

<#
.Provider-agnostic critic. Real provider requires external credentials; FakeCritic
is deterministic for CI and fixture tests. No network is required for normal builds.
#>

function Invoke-FakeCritic {
    param(
        [Parameter(Mandatory)][string]$ImagePath,
        [Parameter(Mandatory)][string]$Surface,
        [string]$ExpectedState = ''
    )
    # Deterministic fake: if image name contains 'malformed' emit a blocker,
    # otherwise emit a single suggestion for traceability.
    if ($ImagePath -like '*malformed*' -or $Surface -like '*malformed*') {
        return @([PSCustomObject]@{
            findingId = 'FAKE-001'
            severity = 'blocker'
            confidence = 0.99
            surface = $Surface
            component = $null
            category = 'clipping'
            observedProblem = 'fake deterministic clipping detected in fixture'
            expectedDesignRule = 'content must not clip'
            requiresHumanTasteDecision = $false
            regressionCandidate = $true
        })
    }
    return @([PSCustomObject]@{
        findingId = 'FAKE-002'
        severity = 'suggestion'
        confidence = 0.42
        surface = $Surface
        category = 'layout'
        observedProblem = 'fake suggestion: consider hierarchy review'
        expectedDesignRule = 'hierarchy rule'
        requiresHumanTasteDecision = $true
        regressionCandidate = $false
    })
}

function Invoke-VisualCritic {
    param(
        [Parameter(Mandatory)][string]$ImagePath,
        [Parameter(Mandatory)][string]$Surface,
        [ValidateSet('fake','real')][string]$Provider = 'fake',
        [hashtable]$Context = @{}
    )
    if ($Provider -eq 'fake') {
        return Invoke-FakeCritic -ImagePath $ImagePath -Surface $Surface
    }
    # Real provider not configured in this environment.
    return @([PSCustomObject]@{
        findingId = 'BLOCKED-001'
        severity = 'suggestion'
        confidence = 0.0
        surface = $Surface
        category = 'layout'
        observedProblem = 'BLOCKED_ENVIRONMENT: no vision provider configured'
        expectedDesignRule = 'n/a'
        requiresHumanTasteDecision = $true
        regressionCandidate = $false
    })
}

Export-ModuleMember -Function Invoke-FakeCritic, Invoke-VisualCritic
