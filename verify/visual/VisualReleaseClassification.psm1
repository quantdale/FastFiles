Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

<#
    Visual release classification with separate legs.

    Each leg is independently PASS/FAIL/BLOCKED/ADVISORY.
    Never collapse into a single green light.
#>

$script:ValidFunctional = @('PASS','FAIL','BLOCKED')
$script:ValidAccessibility = @('PASS','FAIL','BLOCKED')
$script:ValidVisualRegression = @('PASS','FAIL','BLOCKED')
$script:ValidVisualReview = @('PASS','FAIL','ADVISORY','BLOCKED')

function New-VisualReleaseClassification {
    param(
        [Parameter(Mandatory)][ValidateSet('PASS','FAIL','BLOCKED')][string] $Functional,
        [Parameter(Mandatory)][ValidateSet('PASS','FAIL','BLOCKED')][string] $Accessibility,
        [Parameter(Mandatory)][ValidateSet('PASS','FAIL','BLOCKED')][string] $VisualRegression,
        [Parameter(Mandatory)][ValidateSet('PASS','FAIL','ADVISORY','BLOCKED')][string] $VisualReview,
        [string] $FunctionalEvidence = '',
        [string] $AccessibilityEvidence = '',
        [string] $VisualRegressionEvidence = '',
        [string] $VisualReviewEvidence = '',
        [string] $Commit = '',
        [string] $Notes = ''
    )
    return [ordered]@{
        schemaVersion = 1
        schema = 'fastfiles.visual-release-classification.v1'
        timestampUtc = [DateTime]::UtcNow.ToString('o')
        commit = $Commit
        functional = $Functional
        accessibility = $Accessibility
        visualRegression = $VisualRegression
        visualReview = $VisualReview
        evidence = [ordered]@{
            functional = $FunctionalEvidence
            accessibility = $AccessibilityEvidence
            visualRegression = $VisualRegressionEvidence
            visualReview = $VisualReviewEvidence
        }
        notes = $Notes
    }
}

function Test-VisualReleaseClassification {
    param([Parameter(Mandatory)] $Classification)
    $errors = [System.Collections.Generic.List[string]]::new()
    if ($Classification.functional -notin $script:ValidFunctional) { [void]$errors.Add("functional must be $($script:ValidFunctional -join ',')") }
    if ($Classification.accessibility -notin $script:ValidAccessibility) { [void]$errors.Add("accessibility must be $($script:ValidAccessibility -join ',')") }
    if ($Classification.visualRegression -notin $script:ValidVisualRegression) { [void]$errors.Add("visualRegression must be $($script:ValidVisualRegression -join ',')") }
    if ($Classification.visualReview -notin $script:ValidVisualReview) { [void]$errors.Add("visualReview must be $($script:ValidVisualReview -join ',')") }
    return [pscustomobject]@{ Valid = ($errors.Count -eq 0); Errors = @($errors) }
}

function Get-VisualReleaseOverall {
    param([Parameter(Mandatory)] $Classification)
    # Overall is not a collapse: caller must inspect each leg separately.
    # This helper reports the most severe state for summary only.
    $legs = @($Classification.functional, $Classification.accessibility, $Classification.visualRegression)
    if ($legs -contains 'FAIL') { return 'FAIL' }
    if ($Classification.visualReview -eq 'FAIL') { return 'FAIL' }
    if ($legs -contains 'BLOCKED' -or $Classification.visualReview -eq 'BLOCKED') { return 'BLOCKED' }
    if ($Classification.visualReview -eq 'ADVISORY') { return 'ADVISORY' }
    return 'PASS'
}

function Format-VisualReleaseClassification {
    param([Parameter(Mandatory)] $Classification)
    $lines = [System.Collections.Generic.List[string]]::new()
    [void]$lines.Add('# Visual Release Classification')
    [void]$lines.Add("- Commit: $($Classification.commit)")
    [void]$lines.Add("- Timestamp: $($Classification.timestampUtc)")
    [void]$lines.Add("- FUNCTIONAL: $($Classification.functional)")
    [void]$lines.Add("- ACCESSIBILITY: $($Classification.accessibility)")
    [void]$lines.Add("- VISUAL_REGRESSION: $($Classification.visualRegression)")
    [void]$lines.Add("- VISUAL_REVIEW: $($Classification.visualReview)")
    [void]$lines.Add("- Overall: $(Get-VisualReleaseOverall -Classification $Classification)")
    return ($lines -join "`n")
}

Export-ModuleMember -Function New-VisualReleaseClassification, Test-VisualReleaseClassification,
    Get-VisualReleaseOverall, Format-VisualReleaseClassification
