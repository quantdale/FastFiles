<#
    Independently runnable Diagnostics capability (task 3.1 / D10). Tool discovery
    deliberately uses the command resolver and registered App Paths, never a single
    fixed installation path. An optional tool's absence is reported as a SKIPPED
    sub-result, not as a failure of diagnostics itself.
#>

function Find-DiagnosticTool {
    param([Parameter(Mandatory)] [string[]] $CommandNames)

    foreach ($name in $CommandNames) {
        $command = Get-Command $name -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($command) {
            $path = if ($command.Path) { $command.Path } else { $command.Source }
            return [pscustomobject]@{ found = $true; path = $path; discovery = 'command-resolver' }
        }
        foreach ($root in @('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths', 'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths')) {
            $appPath = Join-Path $root "$name.exe"
            $registered = Get-ItemProperty -Path $appPath -ErrorAction SilentlyContinue
            if ($registered -and $registered.'(default)' -and (Test-Path $registered.'(default)')) {
                return [pscustomobject]@{ found = $true; path = $registered.'(default)'; discovery = 'app-paths-registry' }
            }
        }
    }
    return [pscustomobject]@{ found = $false; path = $null; discovery = $null }
}

function Get-ToolVersion {
    param([string] $Path)
    if (-not $Path -or -not (Test-Path $Path)) { return $null }
    try { return (Get-Item $Path).VersionInfo.ProductVersion } catch { return $null }
}

function New-DiagnosticToolResult {
    param([string] $Id, [string[]] $CommandNames, [string] $Detail = $null)
    $tool = Find-DiagnosticTool -CommandNames $CommandNames
    if ($tool.found) {
        return [pscustomobject]@{ id = $Id; status = 'PASS'; reason = $null; path = $tool.path; version = Get-ToolVersion $tool.path; discovery = $tool.discovery; detail = $Detail }
    }
    return [pscustomobject]@{ id = $Id; status = 'SKIPPED'; reason = 'tool-not-found'; path = $null; version = $null; discovery = $null; detail = $Detail }
}

function Test-DiagnosticsCapabilityAvailability {
    param([Parameter(Mandatory)] $Fingerprint)
    if (-not $IsWindows) {
        return [pscustomobject]@{ Available = $false; Reason = 'windows-required'; RequiredContext = [pscustomobject]@{ needs = 'Windows diagnostic APIs' } }
    }
    return [pscustomobject]@{ Available = $true; Reason = $null; RequiredContext = $null }
}

function Get-DiagnosticsCapabilityDiagnostics {
    return @('diagnostic-tool-inventory', 'event-viewer', 'etw', 'wpr', 'wpa', 'procmon', 'procdump', 'application-verifier', 'pageheap', 'windbg', 'wer', 'installer-logs', 'ipc-traces')
}

function Get-DiagnosticToolInventory {
    [CmdletBinding()]
    param()
    $inventory = @()
    $inventory += New-DiagnosticToolResult -Id 'event-viewer' -CommandNames @('Get-WinEvent') -Detail 'PowerShell Event Viewer query cmdlet'
    $inventory += New-DiagnosticToolResult -Id 'etw-logman' -CommandNames @('logman') -Detail 'ETW collection through logman'
    $inventory += New-DiagnosticToolResult -Id 'wpr' -CommandNames @('wpr') -Detail 'Windows Performance Recorder; produces WPA-openable ETL traces'
    $inventory += New-DiagnosticToolResult -Id 'wpa' -CommandNames @('wpa') -Detail 'Windows Performance Analyzer'
    $inventory += New-DiagnosticToolResult -Id 'procmon' -CommandNames @('procmon') -Detail 'Sysinternals Process Monitor'
    $inventory += New-DiagnosticToolResult -Id 'procdump' -CommandNames @('procdump') -Detail 'Sysinternals ProcDump'
    $inventory += New-DiagnosticToolResult -Id 'application-verifier' -CommandNames @('appverif') -Detail 'Application Verifier'
    $inventory += New-DiagnosticToolResult -Id 'pageheap' -CommandNames @('gflags') -Detail 'PageHeap configuration is exposed by gflags'
    $inventory += New-DiagnosticToolResult -Id 'windbg-cdb' -CommandNames @('cdb', 'windbg') -Detail 'Windows debugger command-line interface'

    $wer = [pscustomobject]@{ id = 'wer-local-dumps'; status = 'PASS'; reason = $null; path = 'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps'; version = $null; discovery = 'windows-error-reporting'; detail = 'CrashCapture configures per-executable WER dumps in an admin-only run-local directory and restores prior registry state.' }
    $inventory += $wer
    $inventory += [pscustomobject]@{ id = 'installer-msi-logs'; status = 'SKIPPED'; reason = 'no-installer-operation-in-run'; path = $null; version = $null; discovery = $null; detail = 'Installer capability supplies MSI logs when an install operation runs.' }
    $inventory += [pscustomobject]@{ id = 'ipc-traces'; status = 'SKIPPED'; reason = 'no-ipc-trace-provider-registered'; path = $null; version = $null; discovery = $null; detail = 'IPC capability supplies its trace artifact when implemented.' }
    return $inventory
}

function Invoke-DiagnosticsCapability {
    param($RunContext, $Fingerprint, [string] $ArtifactsDir, [hashtable] $Options)

    $inventory = Get-DiagnosticToolInventory

    $inventoryPath = Join-Path $ArtifactsDir 'diagnostic-tool-inventory.json'
    [pscustomobject]@{ generatedAtUtc = (Get-Date).ToUniversalTime().ToString('o'); tools = $inventory } |
        ConvertTo-Json -Depth 8 | Set-Content -Path $inventoryPath -Encoding utf8
    $toolVersionsPath = Join-Path $ArtifactsDir 'tool-version-metadata.json'
    [pscustomobject]@{
        tools = @($inventory | Where-Object { $_.status -eq 'PASS' } | ForEach-Object {
            [pscustomobject]@{ id = $_.id; version = $_.version; path = $_.path }
        })
    } | ConvertTo-Json -Depth 6 | Set-Content -Path $toolVersionsPath -Encoding utf8

    $subResults = @($inventory | ForEach-Object {
        [pscustomobject]@{ id = $_.id; tier = 0; status = $_.status; reason = $_.reason; requiredContext = $null; durationMs = 0; detail = $_.detail; diagnostics = @() }
    })
    $available = @($inventory | Where-Object { $_.status -eq 'PASS' }).Count
    $skipped = @($inventory | Where-Object { $_.status -eq 'SKIPPED' }).Count
    return [pscustomobject]@{
        Status = 'PASS'; Reason = $null; Summary = "$available tools available; $skipped unavailable or not applicable";
        Artifacts = @(
            @{ path = 'artifacts/diagnostics/diagnostic-tool-inventory.json'; type = 'diagnostic-tool-inventory' }
            @{ path = 'artifacts/diagnostics/tool-version-metadata.json'; type = 'tool-version-metadata' }
        ); SubResults = $subResults
    }
}

Export-ModuleMember -Function Test-DiagnosticsCapabilityAvailability, Invoke-DiagnosticsCapability, Get-DiagnosticsCapabilityDiagnostics, Get-DiagnosticToolInventory
