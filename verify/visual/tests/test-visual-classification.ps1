<#
    Tests for VisualReleaseClassification.psm1
#>
$ErrorActionPreference='Stop'
$script:failures=0
function Check{param([bool]$c,[string]$d) if($c){Write-Host "  ok: $d"} else{$script:failures++; Write-Host "FAIL: $d"} }
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
Import-Module (Join-Path $repo 'verify\visual\VisualReleaseClassification.psm1') -Force

$c1=New-VisualReleaseClassification -Functional PASS -Accessibility PASS -VisualRegression PASS -VisualReview PASS -Commit ('a'*40)
$v1=Test-VisualReleaseClassification -Classification $c1
Check $v1.Valid 'all PASS valid'
Check ((Get-VisualReleaseOverall -Classification $c1) -eq 'PASS') 'overall PASS'

$c2=New-VisualReleaseClassification -Functional PASS -Accessibility PASS -VisualRegression FAIL -VisualReview PASS
Check ((Get-VisualReleaseOverall -Classification $c2) -eq 'FAIL') 'visual regression FAIL => overall FAIL'

$c3=New-VisualReleaseClassification -Functional PASS -Accessibility BLOCKED -VisualRegression BLOCKED -VisualReview BLOCKED
Check ((Get-VisualReleaseOverall -Classification $c3) -eq 'BLOCKED') 'blocked legs => BLOCKED'

$c4=New-VisualReleaseClassification -Functional PASS -Accessibility PASS -VisualRegression PASS -VisualReview ADVISORY
Check ((Get-VisualReleaseOverall -Classification $c4) -eq 'ADVISORY') 'visual review ADVISORY => overall ADVISORY'

# FakeCritic alone must not produce VISUAL_REVIEW_PASS if spec says visual review from fake is not real
# Classification itself allows PASS but caller must document that fake is not real review
$c5=New-VisualReleaseClassification -Functional PASS -Accessibility PASS -VisualRegression PASS -VisualReview PASS
$v5=Test-VisualReleaseClassification -Classification $c5
Check $v5.Valid 'visual review PASS allowed (but should be from real evidence)'

# Invalid enum rejected by ValidateSet at construction; test via raw object
$bad=[pscustomobject]@{ functional='UNKNOWN'; accessibility='PASS'; visualRegression='PASS'; visualReview='PASS' }
$v6=Test-VisualReleaseClassification -Classification $bad
Check (-not $v6.Valid) 'invalid functional enum rejected'

$fmt=Format-VisualReleaseClassification -Classification $c1
Check ($fmt -like '*FUNCTIONAL: PASS*' -and $fmt -like '*VISUAL_REGRESSION: PASS*') 'format contains legs'

if($script:failures -gt 0){ Write-Host "RESULT: $script:failures FAILED"; exit 1 }
Write-Host 'RESULT: all classification tests passed'; exit 0
