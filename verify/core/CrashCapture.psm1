<#
    Run-scoped Windows Error Reporting (WER) dump capture for monitored processes.
    Configuration is restored exactly in a finally block, including removal of keys
    created by the harness. Dumps and crash contexts are written beneath the run tree.
#>

function Test-CrashCaptureElevated {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Set-AdminOnlyDirectoryAcl {
    param([Parameter(Mandatory)] [string] $Path)

    New-Item -ItemType Directory -Path $Path -Force | Out-Null
    $security = [Security.AccessControl.DirectorySecurity]::new()
    $security.SetAccessRuleProtection($true, $false)
    $rights = [Security.AccessControl.FileSystemRights]::FullControl
    $inheritance = [Security.AccessControl.InheritanceFlags]'ContainerInherit, ObjectInherit'
    $propagation = [Security.AccessControl.PropagationFlags]::None
    $allow = [Security.AccessControl.AccessControlType]::Allow
    foreach ($sidValue in @('S-1-5-18', 'S-1-5-32-544')) {
        $sid = [Security.Principal.SecurityIdentifier]::new($sidValue)
        $rule = [Security.AccessControl.FileSystemAccessRule]::new($sid, $rights, $inheritance, $propagation, $allow)
        $security.AddAccessRule($rule) | Out-Null
    }
    Set-Acl -LiteralPath $Path -AclObject $security
}

function Get-RegistryValueSnapshot {
    param([Parameter(Mandatory)] [string] $Path, [Parameter(Mandatory)] [string] $Name)

    $item = Get-ItemProperty -LiteralPath $Path -ErrorAction SilentlyContinue
    $present = $null -ne $item -and $item.PSObject.Properties.Name -contains $Name
    [pscustomobject]@{
        Name = $Name
        Present = $present
        Value = if ($present) { $item.$Name } else { $null }
        Kind = if ($present) { (Get-Item -LiteralPath $Path).GetValueKind($Name).ToString() } else { $null }
    }
}

function Restore-RegistryValueSnapshot {
    param([Parameter(Mandatory)] [string] $Path, [Parameter(Mandatory)] $Snapshot)

    if (-not $Snapshot.Present) {
        Remove-ItemProperty -LiteralPath $Path -Name $Snapshot.Name -ErrorAction SilentlyContinue
        return
    }
    $kind = switch ($Snapshot.Kind) {
        'DWord' { 'DWord' }
        'QWord' { 'QWord' }
        'ExpandString' { 'ExpandString' }
        'MultiString' { 'MultiString' }
        'Binary' { 'Binary' }
        default { 'String' }
    }
    New-ItemProperty -LiteralPath $Path -Name $Snapshot.Name -Value $Snapshot.Value -PropertyType $kind -Force | Out-Null
}

function Enable-CrashDumpCapture {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [pscustomobject] $RunContext,
        [Parameter(Mandatory)] [string[]] $TargetExecutableNames,
        [ValidateSet(1, 2)] [int] $DumpType = 2,
        [ValidateRange(1, 100)] [int] $DumpCount = 10
    )

    if (-not $IsWindows) { throw 'WER crash capture requires Windows.' }
    if (-not (Test-CrashCaptureElevated)) { throw 'WER crash capture requires an elevated token.' }

    $captureRoot = Join-Path $RunContext.ArtifactsRoot 'crash-capture'
    $dumpRoot = Join-Path $captureRoot 'dumps'
    Set-AdminOnlyDirectoryAcl -Path $dumpRoot
    $states = @()

    try {
        foreach ($rawName in $TargetExecutableNames) {
            $name = [IO.Path]::GetFileName($rawName)
            if (-not $name -or [IO.Path]::GetExtension($name) -ne '.exe') {
                throw "Crash-capture target must be an executable filename: '$rawName'"
            }
            $keyPath = "HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\$name"
            $keyExisted = Test-Path -LiteralPath $keyPath
            if (-not $keyExisted) { New-Item -Path $keyPath -Force | Out-Null }
            $states += [pscustomobject]@{
                ExecutableName = $name
                KeyPath = $keyPath
                KeyExisted = $keyExisted
                Values = @(
                    Get-RegistryValueSnapshot -Path $keyPath -Name 'DumpFolder'
                    Get-RegistryValueSnapshot -Path $keyPath -Name 'DumpType'
                    Get-RegistryValueSnapshot -Path $keyPath -Name 'DumpCount'
                )
            }
            New-ItemProperty -LiteralPath $keyPath -Name DumpFolder -Value $dumpRoot -PropertyType ExpandString -Force | Out-Null
            New-ItemProperty -LiteralPath $keyPath -Name DumpType -Value $DumpType -PropertyType DWord -Force | Out-Null
            New-ItemProperty -LiteralPath $keyPath -Name DumpCount -Value $DumpCount -PropertyType DWord -Force | Out-Null
        }
    } catch {
        if ($states.Count -gt 0) {
            Disable-CrashDumpCapture -CaptureState ([pscustomobject]@{ RegistryStates = $states })
        }
        throw
    }

    [pscustomobject]@{
        CaptureRoot = $captureRoot
        DumpRoot = $dumpRoot
        RegistryStates = $states
        EnabledAtUtc = (Get-Date).ToUniversalTime().ToString('o')
    }
}

function Disable-CrashDumpCapture {
    [CmdletBinding()]
    param([Parameter(Mandatory)] $CaptureState)

    $errors = @()
    foreach ($state in @($CaptureState.RegistryStates)) {
        try {
            if (-not (Test-Path -LiteralPath $state.KeyPath)) { continue }
            if (-not $state.KeyExisted) {
                Remove-Item -LiteralPath $state.KeyPath -Recurse -Force
                continue
            }
            foreach ($value in @($state.Values)) {
                Restore-RegistryValueSnapshot -Path $state.KeyPath -Snapshot $value
            }
        } catch {
            $errors += "$($state.KeyPath): $($_.Exception.Message)"
        }
    }
    if ($errors.Count -gt 0) { throw "WER crash-capture teardown failed: $($errors -join '; ')" }
}

function Start-CapturedProcess {
    param(
        [Parameter(Mandatory)] [string] $FilePath,
        [string[]] $ArgumentList = @(),
        [string] $WorkingDirectory,
        [ValidateRange(1, 86400)] [int] $TimeoutSeconds = 300
    )

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $FilePath
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    if ($WorkingDirectory) { $startInfo.WorkingDirectory = $WorkingDirectory }
    foreach ($argument in $ArgumentList) { $startInfo.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) { throw "Failed to start monitored process '$FilePath'." }
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill($true)
        $process.WaitForExit()
        return [pscustomobject]@{ ProcessId = $process.Id; ExitCode = $process.ExitCode; TimedOut = $true }
    }
    [pscustomobject]@{ ProcessId = $process.Id; ExitCode = $process.ExitCode; TimedOut = $false }
}

function Invoke-CrashMonitoredProcess {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [pscustomobject] $RunContext,
        [Parameter(Mandatory)] [string] $FilePath,
        [string[]] $ArgumentList = @(),
        [string] $WorkingDirectory,
        [string] $SourceCapability = 'manual',
        [string] $Phase = 'run',
        [hashtable] $Inputs = @{},
        [ValidateSet(1, 2)] [int] $DumpType = 2,
        [ValidateRange(1, 86400)] [int] $TimeoutSeconds = 300
    )

    $capture = Enable-CrashDumpCapture -RunContext $RunContext -TargetExecutableNames @([IO.Path]::GetFileName($FilePath)) -DumpType $DumpType
    $before = @(Get-ChildItem -LiteralPath $capture.DumpRoot -Filter '*.dmp' -File -ErrorAction SilentlyContinue | ForEach-Object FullName)
    try {
        $processResult = Start-CapturedProcess -FilePath $FilePath -ArgumentList $ArgumentList `
            -WorkingDirectory $WorkingDirectory -TimeoutSeconds $TimeoutSeconds
        # WER dump generation may finish just after the crashed process exits.
        $deadline = (Get-Date).AddSeconds(15)
        do {
            $after = @(Get-ChildItem -LiteralPath $capture.DumpRoot -Filter '*.dmp' -File -ErrorAction SilentlyContinue | ForEach-Object FullName)
            $newDumps = @($after | Where-Object { $before -notcontains $_ })
            if ($newDumps.Count -gt 0 -or $processResult.ExitCode -eq 0) { break }
            Start-Sleep -Milliseconds 250
        } while ((Get-Date) -lt $deadline)
    } finally {
        Disable-CrashDumpCapture -CaptureState $capture
    }

    $crashed = -not $processResult.TimedOut -and $processResult.ExitCode -ne 0
    $contextPath = $null
    if ($crashed) {
        $contextId = '{0}-{1}' -f [IO.Path]::GetFileNameWithoutExtension($FilePath), (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmssfff')
        $contextPath = Join-Path $capture.CaptureRoot "crash-context-$contextId.json"
        [pscustomobject]@{
            schemaVersion = '1.0.0'
            observedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
            executable = [IO.Path]::GetFullPath($FilePath)
            processId = $processResult.ProcessId
            exitCode = $processResult.ExitCode
            exceptionCode = ('0x{0:X8}' -f $processResult.ExitCode)
            sourceCapability = $SourceCapability
            phase = $Phase
            inputs = $Inputs
            dumpPaths = @($newDumps)
        } | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $contextPath -Encoding utf8
    }

    [pscustomobject]@{
        Crashed = $crashed
        TimedOut = $processResult.TimedOut
        ProcessId = $processResult.ProcessId
        ExitCode = $processResult.ExitCode
        DumpPaths = @($newDumps)
        ContextPath = $contextPath
    }
}

Export-ModuleMember -Function Enable-CrashDumpCapture, Disable-CrashDumpCapture, Invoke-CrashMonitoredProcess
