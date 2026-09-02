Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Categories must match verify/visual/schemas/visual-finding.schema.json
$script:ValidSeverities = @('blocker','major','moderate','minor','suggestion')
$script:ValidCategories = @('clipping','overlap','alignment','spacing','hierarchy','density','typography','contrast','truncation','focus','selection','layout','iconography','stateInconsistency','emptyState','responsiveFailure','noise')

function New-VisualFinding {
    param(
        [Parameter(Mandatory)][string]$FindingId,
        [Parameter(Mandatory)][ValidateSet('blocker','major','moderate','minor','suggestion')][string]$Severity,
        [Parameter(Mandatory)][double]$Confidence,
        [Parameter(Mandatory)][string]$Surface,
        [string]$Component,
        [string]$Region,
        [string]$Category = 'layout',
        [Parameter(Mandatory)][string]$ObservedProblem,
        [Parameter(Mandatory)][string]$ExpectedRule,
        [string]$UserImpact = '',
        [string]$SuggestedCorrection = '',
        [object]$Evidence = $null,
        [bool]$RequiresHumanTasteDecision = $false,
        [bool]$RegressionCandidate = $false
    )
    if ($Confidence -lt 0 -or $Confidence -gt 1) { throw "Confidence must be in [0,1], got $Confidence" }
    if ($Category -notin $script:ValidCategories) { throw "Category '$Category' not in valid set: $($script:ValidCategories -join ', ')" }
    $obj = [ordered]@{
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
        evidence = $Evidence
        requiresHumanTasteDecision = $RequiresHumanTasteDecision
        regressionCandidate = $RegressionCandidate
    }
    if ($Region) { $obj.region = $Region }
    return [pscustomobject]$obj
}

function Test-VisualFinding {
    param([Parameter(Mandatory)] $Finding)
    $errors = [System.Collections.Generic.List[string]]::new()
    $getProp = { param($obj,$name) try { if ($obj -is [System.Collections.IDictionary]) { if ($obj.Contains($name)) { return $obj[$name] } else { return $null } } $p=$obj.PSObject.Properties[$name]; if($null -ne $p){return $p.Value} else {return $null} } catch { $null } }
    $findingId = & $getProp $Finding 'findingId'
    if (-not $findingId) { [void]$errors.Add('findingId required') }
    $severity = & $getProp $Finding 'severity'
    if ($severity -notin $script:ValidSeverities) { [void]$errors.Add("severity must be one of $($script:ValidSeverities -join ',')") }
    $conf = & $getProp $Finding 'confidence'
    if ($null -eq $conf -or -not ($conf -is [double] -or $conf -is [int] -or $conf -is [float] -or $conf -is [decimal])) {
        try { $conf = [double]$conf } catch { [void]$errors.Add('confidence must be a number in [0,1]'); $conf = $null }
    }
    if ($null -ne $conf) {
        $c = [double]$conf
        if ($c -lt 0 -or $c -gt 1) { [void]$errors.Add("confidence $c out of range [0,1]") }
    }
    $surface = & $getProp $Finding 'surface'
    if (-not $surface) { [void]$errors.Add('surface required') }
    $category = & $getProp $Finding 'category'
    if (-not $category) { [void]$errors.Add('category required') }
    elseif ($category -notin $script:ValidCategories) { [void]$errors.Add("category '$category' invalid") }
    $observedProblem = & $getProp $Finding 'observedProblem'
    if (-not $observedProblem) { [void]$errors.Add('observedProblem required') }
    $expectedRule = & $getProp $Finding 'expectedDesignRule'
    if (-not $expectedRule) { [void]$errors.Add('expectedDesignRule required') }

    $isSubjective = [bool](& $getProp $Finding 'requiresHumanTasteDecision')
    $isBlocker = (& $getProp $Finding 'severity') -eq 'blocker'
    $isRegression = [bool](& $getProp $Finding 'regressionCandidate')
    if ($isSubjective -and $isBlocker) {
        [void]$errors.Add('subjective finding (requiresHumanTasteDecision=true) must not be severity blocker')
    }
    if ($isSubjective -and $isRegression) {
        [void]$errors.Add('subjective finding must not be regressionCandidate=true')
    }

    return [pscustomobject]@{ Valid = ($errors.Count -eq 0); Errors = @($errors) }
}

function ConvertTo-VisualFinding {
    param([Parameter(Mandatory)] $Raw)
    # Optional fields are genuinely optional in provider responses; avoid
    # strict-mode property access turning a valid minimal response into a
    # provider error.
    $get = {
        param($obj, [string]$name)
        if ($null -eq $obj) { return $null }
        if ($obj -is [System.Collections.IDictionary]) {
            if ($obj.Contains($name)) { return $obj[$name] }
            return $null
        }
        $property = $obj.PSObject.Properties[$name]
        if ($null -ne $property) { return $property.Value }
        return $null
    }
    $finding = New-VisualFinding -FindingId ([string](& $get $Raw 'findingId')) `
        -Severity ([string](& $get $Raw 'severity')) -Confidence ([double](& $get $Raw 'confidence')) `
        -Surface ([string](& $get $Raw 'surface')) -Component ([string](& $get $Raw 'component')) `
        -Category ([string](& $get $Raw 'category')) `
        -ObservedProblem ([string](& $get $Raw 'observedProblem')) `
        -ExpectedRule ([string](& $get $Raw 'expectedDesignRule')) `
        -UserImpact ([string](& $get $Raw 'userImpact')) `
        -SuggestedCorrection ([string](& $get $Raw 'suggestedCorrection')) `
        -Evidence (& $get $Raw 'evidence') `
        -RequiresHumanTasteDecision ([bool](& $get $Raw 'requiresHumanTasteDecision')) `
        -RegressionCandidate ([bool](& $get $Raw 'regressionCandidate'))
    $region = & $get $Raw 'region'
    if ($region) { $finding | Add-Member -NotePropertyName region -NotePropertyValue $region -Force }
    return $finding
}

Export-ModuleMember -Function New-VisualFinding, Test-VisualFinding, ConvertTo-VisualFinding
