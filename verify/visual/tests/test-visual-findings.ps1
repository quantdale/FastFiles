<#
    Tests for VisualFindings.psm1 validation and objective/subjective separation.
#>
$ErrorActionPreference='Stop'
$script:failures=0
function Check{param([bool]$c,[string]$d) if($c){Write-Host "  ok: $d"} else{$script:failures++; Write-Host "FAIL: $d"} }
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
Import-Module (Join-Path $repo 'verify\visual\VisualFindings.psm1') -Force

# Valid finding
$f=New-VisualFinding -FindingId 'T-001' -Severity blocker -Confidence 0.95 -Surface 'home' -Category clipping -ObservedProblem 'content clips' -ExpectedRule 'content must not clip'
$v=Test-VisualFinding -Finding $f
Check $v.Valid 'valid blocker finding passes'

# Invalid confidence
$badConf=$false; try{ $null=New-VisualFinding -FindingId 'T-002' -Severity minor -Confidence 1.5 -Surface 'home' -Category layout -ObservedProblem 'x' -ExpectedRule 'y' } catch{$badConf=$true}
Check $badConf 'confidence >1 rejected at construction'

# Invalid category
$badCat=$false; try{ $null=New-VisualFinding -FindingId 'T-003' -Severity minor -Confidence 0.5 -Surface 'home' -Category 'not-a-category' -ObservedProblem 'x' -ExpectedRule 'y' } catch{$badCat=$true}
Check $badCat 'invalid category rejected'

# Missing required fields (via manual object)
$incomplete=[pscustomobject]@{ findingId='T-004'; severity='minor'; confidence=0.5; surface='home'; category='layout' }
$v2=Test-VisualFinding -Finding $incomplete
Check (-not $v2.Valid) 'missing observedProblem/expectedRule fails validation'

# Subjective blocker must be rejected
$subjBlocker=New-VisualFinding -FindingId 'T-005' -Severity suggestion -Confidence 0.4 -Surface 'home' -Category layout -ObservedProblem 'hierarchy suggestion' -ExpectedRule 'consider hierarchy' -RequiresHumanTasteDecision $true -RegressionCandidate $false
$v3=Test-VisualFinding -Finding $subjBlocker
Check $v3.Valid 'subjective suggestion is valid'

# Subjective must not be blocker
$subjBad=[pscustomobject]@{ findingId='T-006'; severity='blocker'; confidence=0.9; surface='home'; category='layout'; observedProblem='x'; expectedDesignRule='y'; requiresHumanTasteDecision=$true; regressionCandidate=$false }
$v4=Test-VisualFinding -Finding $subjBad
Check (-not $v4.Valid) 'subjective blocker rejected'

# Subjective must not be regression candidate
$subjReg=[pscustomobject]@{ findingId='T-007'; severity='suggestion'; confidence=0.5; surface='home'; category='layout'; observedProblem='x'; expectedDesignRule='y'; requiresHumanTasteDecision=$true; regressionCandidate=$true }
$v5=Test-VisualFinding -Finding $subjReg
Check (-not $v5.Valid) 'subjective regression candidate rejected'

# Objective blocker valid
$objBlocker=New-VisualFinding -FindingId 'T-008' -Severity blocker -Confidence 0.99 -Surface 'search' -Category clipping -ObservedProblem 'filter content clipped at 320px' -ExpectedRule 'filter content must be visible at narrow width' -RequiresHumanTasteDecision $false -RegressionCandidate $true
$v6=Test-VisualFinding -Finding $objBlocker
Check $v6.Valid 'objective blocker valid'

# Advisory subjective advisory
$advisory=New-VisualFinding -FindingId 'T-009' -Severity suggestion -Confidence 0.3 -Surface 'home' -Category hierarchy -ObservedProblem 'consider stronger hierarchy' -ExpectedRule 'primary action more prominent' -RequiresHumanTasteDecision $true -RegressionCandidate $false
$v7=Test-VisualFinding -Finding $advisory
Check ($v7.Valid -and $advisory.severity -eq 'suggestion') 'subjective advisory is suggestion, not blocker'

if($script:failures -gt 0){ Write-Host "RESULT: $script:failures FAILED"; exit 1 }
Write-Host 'RESULT: all visual-findings tests passed'; exit 0
