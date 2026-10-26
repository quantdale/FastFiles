$ErrorActionPreference='Stop'
$script:failures=0
function Check([bool]$Condition,[string]$Description) {
    if($Condition) { Write-Host ('  ok: '+$Description) } else { $script:failures++; Write-Host ('FAIL: '+$Description) }
}
$repo=(Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $repo 'verify/capabilities/windows-ui-automation-validation/UiAutomationValidation.psm1'),[ref]$null,[ref]$null)
$function=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Get-UiaPaletteSemanticState'},$true)
. ([scriptblock]::Create($function.Extent.Text))
$script:recursiveCalls=0; $script:enumerations=0; $script:unreadable=$false
$script:rows=@(0..4095 | ForEach-Object { @{Name=('file-'+$_)} })
$main=@{Name='main'}
$script:children=@(
    @{AutomationId='Pane.0';ClassName='';ProcessId=4242;Children=$script:rows},
    @{AutomationId='6301';ClassName='Edit';ProcessId=4242;HasKeyboardFocus=$true;IsOffscreen=$false;Text='Cancel File Operations'},
    @{AutomationId='6302';ClassName='ListBox';ProcessId=4242;IsEnabled=$true;IsOffscreen=$false;NativeWindowHandle=123}
)
function Get-UiaSafeInteractiveState {param($Driver,$ProcessId) return @{focusedAutomationId='6301'} }
function Get-UiaChildren {param($Driver,$Element)
    $script:enumerations++
    if($script:unreadable) { throw 'Provider enumeration failed' }
    if($Element -ne $main) { throw 'Palette discovery must not enumerate file rows' }
    return $script:children
}
function Get-UiaElementIdentity {param($Driver,$Element) return [pscustomobject]$Element }
function Get-UiaElementProperty {param($Driver,$Element,$Name) return $Element[$Name] }
function Get-UiaElementTextValue {param($Driver,$Element) return $Element.Text }
function Get-UiaListBoxState {param($ListHandle) return @{Available=$true;Visible=$true;Items=@('Cancel File Operations');SelectedIndex=0} }
function Find-UiaElement {param($Driver,$AutomationId,$FromElement,$TimeoutMs)
    $script:recursiveCalls++
    throw 'Earlier4096-row sibling exhausts recursive traversal budget'
}
$state=Get-UiaPaletteSemanticState -Driver @{} -Main $main -ProcessId 4242 -ExpectedCommandNames @('Cancel File Operations')
Check ($state.editPresent -and $state.listPresent -and $state.listVisible -and $state.selectedName -eq 'Cancel File Operations') 'palette sibling remains discoverable after4096 earlier file rows'
Check ($script:recursiveCalls -eq 0 -and $script:enumerations -eq 1) 'palette observation enumerates only direct siblings once'
Check ($state.PSObject.Properties['discoverySucceeded'] -and $state.discoverySucceeded) 'successful enumeration establishes discovery authority'
$script:children[1].ProcessId=9999
$foreign=Get-UiaPaletteSemanticState -Driver @{} -Main $main -ProcessId 4242
Check (-not $foreign.editPresent) 'same-ID foreign process control is rejected'
$script:unreadable=$true
$failed=Get-UiaPaletteSemanticState -Driver @{} -Main $main -ProcessId 4242
Check ($failed.PSObject.Properties['discoverySucceeded'] -and -not $failed.discoverySucceeded) 'provider failure is unresolved rather than certified palette absence'
exit $(if($script:failures) {1} else {0})
