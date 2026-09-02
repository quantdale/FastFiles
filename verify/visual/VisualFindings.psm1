Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function New-VisualFinding {
    param(
        [Parameter(Mandatory)][string]$FindingId,
        [Parameter(Mandatory)][ValidateSet('blocker','major','moderate','minor','suggestion')][string]$Severity,
        [Parameter(Mandatory)][double]$Confidence,
        [Parameter(Mandatory)][string]$Surface,
        [string]$Component,
        [string]$Category = 'layout',
        [Parameter(Mandatory)][string]$ObservedProblem,
        [Parameter(Mandatory)][string]$ExpectedRule,
        [string]$UserImpact = '',
        [string]$SuggestedCorrection = '',
        [bool]$RequiresHumanTasteDecision = $false,
        [bool]$RegressionCandidate = $false
    )
    [PSCustomObject]@{
        findingId = $FindingId
        severity = $Severity
        confidence = $Confidence
        surface = $Surface
        component = $Component
        category = $Category
        observedProblem = $ObservedProblem
        expectedDesignRule = $ExpectedRule
        userImpact = $UserImpact
        suggestedCorrection = $SuggestedCorrection
        requiresHumanTasteDecision = $RequiresHumanTasteDecision
        regressionCandidate = $RegressionCandidate
    }
}

Export-ModuleMember -Function New-VisualFinding
