[CmdletBinding()]
param(
    [string] $InstallDirectory = (Join-Path $env:ProgramFiles 'FastFiles'),
    [switch] $RunUninstall,
    [int] $TimeoutSeconds = 180,
    [ValidateSet('human', 'json')]
    [string] $OutputFormat = 'human'
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'ReleaseCommon.psm1') -Force -DisableNameChecking

try {
    $installRoot = [IO.Path]::GetFullPath($InstallDirectory).TrimEnd('\')
    $expectedRoot = [IO.Path]::GetFullPath((Join-Path $env:ProgramFiles 'FastFiles')).TrimEnd('\')
    if ($installRoot -ine $expectedRoot) { throw "CLEANUP_TARGET_REJECTED: only the exact Program Files FastFiles installation directory is allowed." }
    $setup = Join-Path $installRoot 'FastFilesSetup.exe'
    $preflight = Get-CleanHostPreflight
    if (-not $RunUninstall) {
        $result = [pscustomobject]@{ status = 'DRY_RUN'; target = $installRoot; uninstallExecuted = $false; host = $preflight; note = 'Pass -RunUninstall to invoke the installed FastFilesSetup.exe; no broad deletion is performed.' }
        if ($OutputFormat -eq 'json') { $result | ConvertTo-Json -Depth 12 } else { Write-Host "DRY RUN: exact uninstall target is $setup" }
        exit 0
    }
    if (-not (Test-Path -LiteralPath $setup -PathType Leaf)) { throw "CLEANUP_TARGET_MISSING: exact installed setup executable not found: $setup" }
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $setup
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.ArgumentList.Add('/uninstall')
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    if (-not $process.Start()) { throw 'UNINSTALL_FAILED: setup did not start.' }
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) { $process.Kill($true); throw 'UNINSTALL_FAILED: setup timed out.' }
    $postflight = Get-CleanHostPreflight
    $residual = @()
    if ($postflight.resources.service.present) { $residual += 'service' }
    if (@($postflight.resources.processes).Count -gt 0) { $residual += 'processes' }
    if ($postflight.resources.scheduledTask.present) { $residual += 'scheduledTask' }
    if ($postflight.resources.clientGroup.present) { $residual += 'clientGroup' }
    if ($postflight.resources.installDirectory.present) { $residual += 'installDirectory' }
    if ($postflight.resources.programData.present) { $residual += 'programData' }
    if ($postflight.resources.uninstallRegistry.present) { $residual += 'uninstallRegistry' }
    $clean = $process.ExitCode -eq 0 -and @($residual).Count -eq 0
    $result = [pscustomobject]@{ status = if ($clean) { 'PASS' } else { 'FAIL' }; target = $installRoot; uninstallExecuted = $true; setupExitCode = $process.ExitCode; host = $postflight; residual = @($residual); preservedUserData = $postflight.resources.userData.present; note = 'Cleanup is limited to the installer contract; intentionally preserved user data is reported separately.' }
    if ($OutputFormat -eq 'json') { $result | ConvertTo-Json -Depth 12 } else { Write-Host "$(if ($clean) { 'PASS' } else { 'FAIL' }): uninstall exit code $($process.ExitCode); clean=$($postflight.clean)" }
    exit $(if ($clean) { 0 } else { 1 })
} catch {
    if ($OutputFormat -eq 'json') { [pscustomobject]@{ status = 'FAIL'; error = $_.Exception.Message } | ConvertTo-Json -Depth 8 } else { Write-Error $_.Exception.Message }
    exit 1
}
