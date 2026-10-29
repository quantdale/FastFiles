param([string]$SourcePath='')
$ErrorActionPreference='Stop'
$script:failures=0
function Check([bool]$Condition,[string]$Description) {
    if($Condition) { Write-Host ('  ok: '+$Description) } else { $script:failures++; Write-Host ('FAIL: '+$Description) }
}
if(-not $SourcePath) { $SourcePath=Join-Path $PSScriptRoot '../../capabilities/windows-ui-automation-validation/UiAutomationValidation.psm1' }
$ast=[Management.Automation.Language.Parser]::ParseFile((Resolve-Path $SourcePath),[ref]$null,[ref]$null)
$function=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Get-UiaCommandDispatchEvidence'},$true)
. ([scriptblock]::Create($function.Extent.Text))
$path=[IO.Path]::GetTempFileName()
function Get-UiaDiagnosticsLogPath { return $path }
function Event([string]$Command,[int]$Owner,[long]$Epoch,[string]$Outcome='accepted') {
    return (@{codeName='FFDIAG_UI_COMMAND_DISPATCHED';message=('command='+$Command+';accepted='+($Outcome -eq 'accepted').ToString().ToLowerInvariant());state='actual fixture';outcome=$Outcome;pid=$Owner;epochMs=$Epoch} | ConvertTo-Json -Compress)+"`n"
}
try {
    $prefix=Event file.cancel-operations 4242 50
    $offset=[Text.Encoding]::UTF8.GetByteCount($prefix)
    $fresh=(Event file.cancel-operations 9999 101)+(Event file.cancel-operations-extra 4242 101)+(Event file.cancel-operations 4242 51)+"invalid JSON`n"+(Event file.cancel-operations 4242 101)
    $traffic=("{`"codeName`":`"FFDIAG_UI_PROVIDER_CHILD_QUERY`"}`n"*3000)
    [IO.File]::WriteAllText($path,$prefix+$fresh+$traffic,[Text.UTF8Encoding]::new($false))
    $found=Get-UiaCommandDispatchEvidence -CommandId file.cancel-operations -LogPath $path -LogOffset $offset -ProcessId 4242 -AfterEpochMs 100
    Check ($found -and $found.pid -eq 4242 -and $found.epochMs -eq 101 -and $found.outcome -eq 'accepted') 'forward offset read retains exact fresh owned dispatch beyond tail traffic'
    Check (-not (Get-UiaCommandDispatchEvidence -CommandId file.cancel-operations -LogPath $path -LogOffset $offset -ProcessId 8888 -AfterEpochMs 100)) 'foreign PID cannot certify dispatch'
    Check (-not (Get-UiaCommandDispatchEvidence -CommandId file.cancel-operations -LogPath $path -LogOffset $offset -ProcessId 4242 -AfterEpochMs 200)) 'pre-input timestamp cannot certify dispatch'
    $end=(Get-Item -LiteralPath $path).Length
    Check (-not (Get-UiaCommandDispatchEvidence -CommandId file.cancel-operations -LogPath $path -LogOffset $end -ProcessId 4242 -AfterEpochMs 100)) 'pre-offset receipt cannot certify a later action'
    [IO.File]::WriteAllText($path,(Event file.cancel-operations 4242 201 rejected),[Text.UTF8Encoding]::new($false))
    Check (-not (Get-UiaCommandDispatchEvidence -CommandId file.cancel-operations -LogPath $path -LogOffset $end -ProcessId 4242 -AfterEpochMs 100)) 'truncated log cannot borrow an old offset'
    $rejected=Get-UiaCommandDispatchEvidence -CommandId file.cancel-operations -LogPath $path -LogOffset 0 -ProcessId 4242 -AfterEpochMs 200
    Check ($rejected -and $rejected.outcome -eq 'rejected') 'product rejection remains rejected'
} catch { Check $false ('actual evidence reader error: '+$_.Exception.Message) }
finally { Remove-Item -LiteralPath $path -Force }
exit $(if($script:failures) {1} else {0})
