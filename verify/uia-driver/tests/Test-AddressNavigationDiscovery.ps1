$ErrorActionPreference='Stop'
$script:failures=0
function Check([bool]$Condition,[string]$Description) {
    if($Condition) { Write-Host ('ok: '+$Description) }
    else { $script:failures++;Write-Host ('FAIL: '+$Description) }
}
$repo=(Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $repo 'verify/capabilities/windows-ui-automation-validation/UiAutomationValidation.psm1'),[ref]$null,[ref]$null)
$function=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Invoke-UiaAddressBarNavigate'},$true)
. ([scriptblock]::Create($function.Extent.Text))
# Closed public-UIA fixture. The recursive lookup exhausts its bounded search;
# direct native chrome remains readable. No native process or input is used.
$main=@{Name='main';ClassName='FastFiles';ProcessId=4242}
$chrome=@{Name='chrome';ClassName='FastFilesNavigationChrome';ProcessId=4242}
$pane=@{Name='pane';ClassName='';ProcessId=4242;Children=@(0..4095)}
$edit=@{Name='address';ClassName='Edit';AutomationId='';ProcessId=4242}
$script:buttons=@()
$script:editFocus=$false;$script:throwFocus=$false;$script:throwChildren=$false
$script:enters=0;$script:recursiveCalls=0;$script:rowReads=0
function Start-Sleep {param($Milliseconds)}
function Close-UiaSearchPanelIfOpen {param($Driver,$Main,$ProcessId) return @{Pass=$true} }
function Reset-UiaModifierKeys {param($Driver)}
function Ensure-UiaInputForeground {param($Driver,$Main,$Attempts) return $true }
function Send-UiaInput {param($Driver,$Keys) if($Keys -eq '{ENTER}') {$script:enters++};return @{ok=$true} }
function Wait-UiaElement {param($Driver,$ClassName,$ProcessId,$FromElement,$TimeoutMs,$PollIntervalMs) return $edit }
function Get-UiaElementIdentity {param($Driver,$Element) return [pscustomobject]$Element }
function Set-UiaElementValue {param($Driver,$Element,$Value) return @{ok=$true} }
function Find-UiaElement {param($Driver,$ClassName,$FromElement,$TimeoutMs,[switch]$All) $script:recursiveCalls++;return @() }
function Wait-UiaCondition {param($Driver,$TimeoutMs,$PollIntervalMs,$Description,$ScriptBlock) return (& $ScriptBlock) }
function Set-UiaForeground {param($Driver,$Element) return $true }
function Test-UiaForegroundWindow {param($Driver,$Element) return $true }
function Get-UiaMainWindowElement {param($Driver,$ProcessId) return $main }
function Get-UiaSafeInteractiveState {param($Driver,$ProcessId) return @{foregroundTitle='FastFiles';focusedControlType='List'} }
function Get-UiaElementProperty {param($Driver,$Element,$Name)
    if($script:throwFocus) {throw 'stale edit'}
    if($Name -eq 'HasKeyboardFocus') {return $script:editFocus}
    throw 'Unexpected property read'
}
function Get-UiaChildren {param($Driver,$Element)
    if($script:throwChildren) {throw 'unreadable provider'}
    if($Element.Name -eq 'main') {return @($pane,$chrome)}
    if($Element.Name -eq 'chrome') {return $script:buttons}
    $script:rowReads++;throw 'Navigation must not walk file rows'
}
function Run-Case([string]$Leaf = 'Source') {
    $script:enters=0;$script:recursiveCalls=0;$script:rowReads=0
    return Invoke-UiaAddressBarNavigate -Driver @{} -Main $main -ProcessId 4242 -Path ('C:\owned\'+$Leaf) -ExpectedLeaf $Leaf
}
$script:buttons=@(@{Name='1: Source';AutomationId='8300';ClassName='Button';ProcessId=4242})
$result=Run-Case
Check $result.Pass 'direct native tab is discovered when recursive search is exhausted'
Check ($script:enters -eq 1 -and $script:recursiveCalls -eq 0 -and $script:rowReads -eq 0) 'successful navigation avoids recursive rows and a second Enter'
$script:buttons=@(@{Name='1: Other';AutomationId='8300';ClassName='Button';ProcessId=4242})
$result=Run-Case
Check (-not $result.Pass -and $script:enters -eq 1) 'unmatched tab remains failure without Enter on a focused file list'
$script:editFocus=$true
$result=Run-Case
Check (-not $result.Pass -and $script:enters -eq 2) 'one bounded recommit remains available while the exact address edit has focus'
$script:editFocus=$false
$script:buttons=@(@{Name='1: Source';AutomationId='8300';ClassName='Button';ProcessId=9999})
$result=Run-Case
Check (-not $result.Pass -and $script:enters -eq 1) 'foreign process tab cannot satisfy navigation'
$script:buttons=@(@{Name='Source';AutomationId='8400';ClassName='Button';ProcessId=4242})
$result=Run-Case
Check (-not $result.Pass) 'tab close control cannot satisfy a tab label'
$script:throwChildren=$true
$result=Run-Case
Check (-not $result.Pass -and $script:enters -eq 1) 'provider failure remains failure without unsafe input'
$script:throwChildren=$false;$script:throwFocus=$true
$result=Run-Case
Check (-not $result.Pass -and $script:enters -eq 1) 'stale edit cannot authorize a recommit'
$script:throwFocus=$false
$script:editFocus='True'
$result=Run-Case
Check (-not $result.Pass -and $script:enters -eq 1) 'non-boolean focus data cannot authorize input'
$script:editFocus=$false
$script:buttons=@(@{Name='11: Source';AutomationId='8310';ClassName='Button';ProcessId=4242})
$result=Run-Case
Check $result.Pass 'valid tab indices beyond the first ten are discovered'
$chrome.ProcessId=9999
$result=Run-Case
Check (-not $result.Pass -and $script:enters -eq 1) 'foreign chrome cannot authorize a matching tab'
$chrome.ProcessId=4242
$script:buttons=@(@{Name='1: S';AutomationId='8300';ClassName='Button';ProcessId=4242})
$result=Run-Case '[Source]'
Check (-not $result.Pass) 'literal bracketed folder cannot match a wildcard character class'
$script:buttons=@(@{Name='1: [Source]';AutomationId='8300';ClassName='Button';ProcessId=4242})
$result=Run-Case '[Source]'
Check $result.Pass 'literal bracketed folder remains discoverable'
exit $(if($script:failures) {1} else {0})
