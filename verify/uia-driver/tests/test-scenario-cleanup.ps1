param([string] $CapabilityPath = (Join-Path $PSScriptRoot '..\..\capabilities\windows-ui-automation-validation\UiAutomationValidation.psm1'))

# Extract the actual teardown orchestration; never import its live lifecycle.
# Every external effect is replaced below, including the historical desktop
# title fallback. Fixture paths and windows exist only as in-memory values.
& {
    $tokens = $null
    $parseErrors = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile(
        [IO.Path]::GetFullPath($CapabilityPath), [ref]$tokens, [ref]$parseErrors)
    if ($parseErrors.Count) { throw 'Scenario cleanup source did not parse' }
    $functionAst = $ast.Find({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq 'Remove-UiaScenarioContext'
    }, $true)
    if (-not $functionAst) { throw 'Actual scenario cleanup function missing' }
    . ([scriptblock]::Create($functionAst.Extent.Text))

    $owned = [pscustomobject]@{ Id = 'registered'; ProcessId = 4242; ClassName = 'CabinetWClass'; Name = 'FastFilesE2E - File Explorer' }
    $unrelated = [pscustomobject]@{ Id = 'unrelated'; ProcessId = 4242; ClassName = 'CabinetWClass'; Name = 'FastFilesE2E - File Explorer' }
    $fixture = 'C:\RecordedOnly\FastFilesE2E'
    $script:cleanupProbe = @{
        Closed = [Collections.Generic.List[string]]::new()
        Removed = [Collections.Generic.List[string]]::new()
        Desktop = @($owned, $unrelated)
        ThrowOnRegistered = $false
        DesktopReads = 0
        ModifierResets = 0
    }
    function Close-UiaExplorerWindow {
        param($Driver, $WindowElement)
        $script:cleanupProbe.Closed.Add($WindowElement.Id)
        if ($script:cleanupProbe.ThrowOnRegistered -and $WindowElement.Id -eq 'registered') {
            throw 'recorded-close-failure'
        }
    }
    function Remove-Item {
        [CmdletBinding()] param([string] $LiteralPath, [switch] $Recurse, [switch] $Force)
        $script:cleanupProbe.Removed.Add($LiteralPath)
    }
    function Get-UiaRootElement {
        param($Driver)
        $script:cleanupProbe.DesktopReads++
        return 'recorded-desktop'
    }
    function Get-UiaChildren { param($Driver, $Element) return $script:cleanupProbe.Desktop }
    function Get-UiaElementIdentity { param($Driver, $Element) return $Element }
    function Reset-UiaModifierKeys {
        param($Driver)
        $script:cleanupProbe.ModifierResets++
        return [pscustomobject]@{ ok = $true }
    }
    function Test-UiaEnginePipePresent { return $false }
    function Get-UiaSessionsRoot { return 'C:\RecordedOnly\Sessions' }
    # These paths must remain unreachable for this context. Stub them too so
    # future teardown changes cannot turn this regression into live cleanup.
    function Stop-UiaOwnedProcessIdentity { throw 'Unexpected process cleanup' }
    function Keep-UiaWindowTopmost { throw 'Unexpected window mutation' }
    function Wait-UiaEnginePipeAbsent { throw 'Unexpected pipe wait' }
    function Test-Path { throw 'Unexpected filesystem inspection' }
    function New-Item { throw 'Unexpected filesystem creation' }
    function Copy-Item { throw 'Unexpected filesystem copy' }

    $context = [pscustomobject]@{
        ScenarioId = 'recorded-cleanup'; Driver = 'recorded'; Main = $null
        ExplorerWindows = @($owned); Fixtures = @($fixture); OwnedProcesses = @()
        RepoRoot = 'C:\RecordedOnly'; PipePresentBefore = $false
        SessionRoot = $null; SessionMarker = $null; Cleanup = $null
    }
    $result = Remove-UiaScenarioContext -Context $context -LogFn { param($message) }
    Check ($result.Pass) 'registered Explorer cleanup succeeds'
    Check (($script:cleanupProbe.Closed -join ',') -eq 'registered') 'cleanup closes only the explicitly registered Explorer, despite an unrelated matching title and PID'
    Check (($script:cleanupProbe.Removed -join ',') -eq $fixture) 'cleanup still removes the owned filesystem fixture'

    $script:cleanupProbe.Closed.Clear()
    $script:cleanupProbe.Removed.Clear()
    $context.ExplorerWindows = @()
    $result = Remove-UiaScenarioContext -Context $context -LogFn { param($message) }
    Check ($result.Pass -and $script:cleanupProbe.Closed.Count -eq 0) 'fixtures alone authorize no Explorer window closes'
    Check (($script:cleanupProbe.Removed -join ',') -eq $fixture) 'fixture cleanup runs without registered Explorer windows'

    $script:cleanupProbe.Closed.Clear()
    $script:cleanupProbe.Removed.Clear()
    $script:cleanupProbe.ThrowOnRegistered = $true
    $context.ExplorerWindows = @($owned)
    $result = Remove-UiaScenarioContext -Context $context -LogFn { param($message) }
    Check (-not $result.Pass -and ($result.Residue -join ',') -eq 'explorer-close-exception:recorded-close-failure') 'registered close failure remains visible in cleanup residue'
    Check (($script:cleanupProbe.Closed -join ',') -eq 'registered') 'failed registered close never triggers replacement title-based closes'
    Check (($script:cleanupProbe.Removed -join ',') -eq $fixture) 'registered close failure does not skip filesystem fixture cleanup'
    Check ($script:cleanupProbe.DesktopReads -eq 0) 'cleanup never scans the desktop for fixture titles'
    Check ($script:cleanupProbe.ModifierResets -eq 3) 'all recorded cleanup paths restore modifier state'
}
