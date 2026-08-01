<#
    Tier-1 installer/service capability (tasks 5.1-5.5). It refuses to mutate a
    pre-existing FastFiles installation, stages the native installer beside its
    payload, captures every operation, and always performs idempotent teardown.
#>

$script:ServiceName = 'FastFilesIndexSvc'
$script:InstallDir = Join-Path $env:ProgramFiles 'FastFiles'
$script:TaskPath = '\FastFiles\'
$script:TaskName = 'FastFilesEngine'
$script:ClientGroup = 'FastFilesUsers'
$script:UninstallRegistryPath = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\FastFiles'

function New-InstallSubResult {
    param([string] $Id, [string] $Status, [string] $Reason, [double] $DurationMs, [string] $Detail, [array] $Diagnostics = @())
    [pscustomobject]@{ id = $Id; tier = 1; status = $Status; reason = $Reason; requiredContext = $null; durationMs = [math]::Round($DurationMs, 0); detail = $Detail; diagnostics = $Diagnostics }
}

function Test-InstallServiceCapabilityAvailability {
    param([Parameter(Mandatory)] $Fingerprint)
    if (-not $IsWindows) {
        return [pscustomobject]@{ Available = $false; Reason = 'windows-required'; RequiredContext = [pscustomobject]@{ needs = 'Windows SCM and Task Scheduler' } }
    }
    if (-not $Fingerprint.IsElevated) {
        return [pscustomobject]@{ Available = $false; Reason = 'elevation-required'; RequiredContext = [pscustomobject]@{ needs = 'Elevated administrator token for install/service mutation' } }
    }
    return [pscustomobject]@{ Available = $true; Reason = $null; RequiredContext = $null }
}

function Get-InstallServiceCapabilityDiagnostics {
    return @('installer-operation-logs', 'integrity-snapshot', 'service-config', 'service-security-sddl', 'service-lifecycle', 'service-control-manager-events', 'teardown-state')
}

function Test-FastFilesHostState {
    $service = Get-Service -Name $script:ServiceName -ErrorAction SilentlyContinue
    $task = Get-ScheduledTask -TaskPath $script:TaskPath -TaskName $script:TaskName -ErrorAction SilentlyContinue
    $group = Get-LocalGroup -Name $script:ClientGroup -ErrorAction SilentlyContinue
    [pscustomobject]@{
        ServicePresent = $null -ne $service
        ServiceStatus = if ($service) { $service.Status.ToString() } else { $null }
        InstallDirectoryPresent = Test-Path -LiteralPath $script:InstallDir
        ScheduledTaskPresent = $null -ne $task
        ClientGroupPresent = $null -ne $group
        UninstallRegistryPresent = Test-Path -LiteralPath $script:UninstallRegistryPath
        ProgramDataPresent = Test-Path -LiteralPath (Join-Path $env:ProgramData 'FastFiles')
        WerLocalDumpsPresent = Test-Path -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\FastFilesIndexSvc.exe'
        RelayInstallerPresent = @(Get-ChildItem -LiteralPath $env:TEMP -Filter 'FastFilesSetup-Uninstall-*.exe' -File -ErrorAction SilentlyContinue).Count -gt 0
    }
}

function Test-IsCleanFastFilesHost {
    param([Parameter(Mandatory)] $State)
    return -not ($State.ServicePresent -or $State.InstallDirectoryPresent -or $State.ScheduledTaskPresent -or $State.ClientGroupPresent -or $State.UninstallRegistryPresent -or $State.ProgramDataPresent -or $State.WerLocalDumpsPresent -or $State.RelayInstallerPresent)
}

function Get-InstallerPayload {
    param([Parameter(Mandatory)] [string] $RepoRoot, [Parameter(Mandatory)] [string] $StagingDir)

    $sources = [ordered]@{
        'FastFilesSetup.exe' = Join-Path $RepoRoot 'build\debug\src\installer\FastFilesSetup.exe'
        'FastFilesIndexSvc.exe' = Join-Path $RepoRoot 'build\debug\src\indexsvc\FastFilesIndexSvc.exe'
        'FastFilesEngine.exe' = Join-Path $RepoRoot 'build\debug\src\engine\FastFilesEngine.exe'
        'FastFiles.exe' = Join-Path $RepoRoot 'build\debug\src\ui\FastFiles.exe'
    }
    $missing = @($sources.GetEnumerator() | Where-Object { -not (Test-Path -LiteralPath $_.Value) } | ForEach-Object Value)
    if ($missing.Count -gt 0) { throw "Installer payload is not built: $($missing -join '; ')" }
    New-Item -ItemType Directory -Path $StagingDir -Force | Out-Null
    foreach ($entry in $sources.GetEnumerator()) { Copy-Item -LiteralPath $entry.Value -Destination (Join-Path $StagingDir $entry.Key) -Force }
    return Join-Path $StagingDir 'FastFilesSetup.exe'
}

function Invoke-LoggedProcess {
    param(
        [Parameter(Mandatory)] [string] $FilePath,
        [Parameter(Mandatory)] [string[]] $ArgumentList,
        [Parameter(Mandatory)] [string] $LogRoot,
        [Parameter(Mandatory)] [string] $Operation,
        [ValidateRange(1, 1800)] [int] $TimeoutSeconds = 180
    )

    $started = Get-Date
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $FilePath
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($argument in $ArgumentList) { $startInfo.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) { throw "Failed to launch installer operation '$Operation'." }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $timedOut = -not $process.WaitForExit($TimeoutSeconds * 1000)
    if ($timedOut) { $process.Kill($true); $process.WaitForExit() }
    $finished = Get-Date
    $stdout = $stdoutTask.Result
    $stderr = $stderrTask.Result
    $stdoutPath = Join-Path $LogRoot "$Operation.stdout.log"
    $stderrPath = Join-Path $LogRoot "$Operation.stderr.log"
    $recordPath = Join-Path $LogRoot "$Operation.operation.json"
    $stdout | Set-Content -LiteralPath $stdoutPath -Encoding utf8
    $stderr | Set-Content -LiteralPath $stderrPath -Encoding utf8
    $record = [pscustomobject]@{
        operation = $Operation
        arguments = $ArgumentList
        startedAtUtc = $started.ToUniversalTime().ToString('o')
        finishedAtUtc = $finished.ToUniversalTime().ToString('o')
        durationMs = [math]::Round(($finished - $started).TotalMilliseconds, 0)
        exitCode = $process.ExitCode
        timedOut = $timedOut
        stdout = [IO.Path]::GetFileName($stdoutPath)
        stderr = [IO.Path]::GetFileName($stderrPath)
    }
    $record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $recordPath -Encoding utf8
    return $record
}

function Wait-ServiceState {
    param([Parameter(Mandatory)] [string] $Status, [ValidateRange(1, 300)] [int] $TimeoutSeconds = 30, [int] $DifferentProcessId = 0)

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $service = Get-CimInstance Win32_Service -Filter "Name='$script:ServiceName'" -ErrorAction SilentlyContinue
        if ($service -and $service.State -eq $Status -and ($DifferentProcessId -eq 0 -or $service.ProcessId -ne $DifferentProcessId)) { return $service }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    return $null
}

function Get-InstallIntegritySnapshot {
    $service = Get-CimInstance Win32_Service -Filter "Name='$script:ServiceName'" -ErrorAction SilentlyContinue
    $task = Get-ScheduledTask -TaskPath $script:TaskPath -TaskName $script:TaskName -ErrorAction SilentlyContinue
    $files = @('FastFilesIndexSvc.exe', 'FastFilesEngine.exe', 'FastFiles.exe', 'FastFilesSetup.exe') | ForEach-Object {
        $path = Join-Path $script:InstallDir $_
        [pscustomobject]@{ name = $_; present = Test-Path -LiteralPath $path; hash = if (Test-Path -LiteralPath $path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash } else { $null } }
    }
    $acl = if (Test-Path -LiteralPath $script:InstallDir) { Get-Acl -LiteralPath $script:InstallDir } else { $null }
    $writeViolations = @()
    if ($acl) {
        $allowedWriteSids = @('S-1-5-18', 'S-1-5-32-544', 'S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464')
        foreach ($rule in @($acl.Access | Where-Object AccessControlType -eq Allow)) {
            $sid = try { $rule.IdentityReference.Translate([Security.Principal.SecurityIdentifier]).Value } catch { [string]$rule.IdentityReference }
            $writeMask = [Security.AccessControl.FileSystemRights]::WriteData -bor
                [Security.AccessControl.FileSystemRights]::AppendData -bor
                [Security.AccessControl.FileSystemRights]::WriteExtendedAttributes -bor
                [Security.AccessControl.FileSystemRights]::WriteAttributes -bor
                [Security.AccessControl.FileSystemRights]::Delete -bor
                [Security.AccessControl.FileSystemRights]::DeleteSubdirectoriesAndFiles -bor
                [Security.AccessControl.FileSystemRights]::ChangePermissions -bor
                [Security.AccessControl.FileSystemRights]::TakeOwnership
            $writes = ($rule.FileSystemRights -band $writeMask) -ne 0
            if ($writes -and $allowedWriteSids -notcontains $sid) { $writeViolations += [pscustomobject]@{ identity = [string]$rule.IdentityReference; sid = $sid; rights = [string]$rule.FileSystemRights } }
        }
    }
    $delayed = (Get-ItemProperty -LiteralPath "HKLM:\SYSTEM\CurrentControlSet\Services\$script:ServiceName" -Name DelayedAutostart -ErrorAction SilentlyContinue).DelayedAutostart
    [pscustomobject]@{
        files = $files
        installDirectoryAclProtected = if ($acl) { $acl.AreAccessRulesProtected } else { $false }
        installDirectoryWriteViolations = $writeViolations
        uninstallRegistryPresent = Test-Path -LiteralPath $script:UninstallRegistryPath
        scheduledTaskPresent = $null -ne $task
        scheduledTaskExecute = if ($task) { @($task.Actions.Execute) } else { @() }
        servicePresent = $null -ne $service
        service = if ($service) { [pscustomobject]@{ startMode = $service.StartMode; startName = $service.StartName; state = $service.State; pathName = $service.PathName; processId = $service.ProcessId; delayedAutoStart = [bool]$delayed } } else { $null }
    }
}

function Add-IntegrityResults {
    param([Parameter(Mandatory)] $Snapshot, [Parameter(Mandatory)] [ref] $Results)
    $missingFiles = @($Snapshot.files | Where-Object { -not $_.present } | ForEach-Object name)
    $Results.Value += New-InstallSubResult -Id 'installed-files' -Status $(if ($missingFiles.Count -eq 0) { 'PASS' } else { 'FAIL' }) -Reason $(if ($missingFiles.Count) { 'installed-files-missing' }) -DurationMs 0 -Detail $(if ($missingFiles.Count) { $missingFiles -join ', ' } else { 'All expected executables are present.' })
    $aclOk = $Snapshot.installDirectoryAclProtected -and @($Snapshot.installDirectoryWriteViolations).Count -eq 0
    $Results.Value += New-InstallSubResult -Id 'install-directory-acl' -Status $(if ($aclOk) { 'PASS' } else { 'FAIL' }) -Reason $(if (-not $aclOk) { 'install-directory-acl-mismatch' }) -DurationMs 0 -Detail "protected=$($Snapshot.installDirectoryAclProtected); writeViolations=$(@($Snapshot.installDirectoryWriteViolations).Count)"
    $Results.Value += New-InstallSubResult -Id 'uninstall-registry' -Status $(if ($Snapshot.uninstallRegistryPresent) { 'PASS' } else { 'FAIL' }) -Reason $(if (-not $Snapshot.uninstallRegistryPresent) { 'uninstall-registry-entry-missing' }) -DurationMs 0 -Detail $script:UninstallRegistryPath
    $Results.Value += New-InstallSubResult -Id 'engine-scheduled-task' -Status $(if ($Snapshot.scheduledTaskPresent) { 'PASS' } else { 'FAIL' }) -Reason $(if (-not $Snapshot.scheduledTaskPresent) { 'scheduled-task-missing' }) -DurationMs 0 -Detail "$script:TaskPath$script:TaskName"
    $Results.Value += New-InstallSubResult -Id 'service-presence' -Status $(if ($Snapshot.servicePresent) { 'PASS' } else { 'FAIL' }) -Reason $(if (-not $Snapshot.servicePresent) { 'service-missing' }) -DurationMs 0 -Detail $script:ServiceName
}

function Get-ServiceSecurityCheck {
    $group = Get-LocalGroup -Name $script:ClientGroup -ErrorAction SilentlyContinue
    if (-not $group) { return [pscustomobject]@{ passed = $false; detail = 'FastFilesUsers group is missing.'; sddl = $null } }
    $sid = $group.SID.Value
    $output = (& sc.exe sdshow $script:ServiceName 2>&1 | ForEach-Object ToString) -join "`n"
    $ace = [regex]::Match($output, "\(A;;([^;]*);;;$([regex]::Escape($sid))\)", [Text.RegularExpressions.RegexOptions]::IgnoreCase)
    if (-not $ace.Success) { return [pscustomobject]@{ passed = $false; detail = "No allow ACE for $sid."; sddl = $output } }
    $rights = $ace.Groups[1].Value
    $dangerous = @('DC', 'RP', 'WP', 'DT', 'SD', 'WD', 'WO') | Where-Object { $rights.Contains($_) }
    [pscustomobject]@{ passed = $dangerous.Count -eq 0; detail = "groupSid=$sid; rights=$rights; dangerous=$($dangerous -join ',')"; sddl = $output }
}

function Initialize-ServiceAccessProbeType {
    if ('FastFiles.Verify.ServiceAccessProbe' -as [type]) { return }
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;

namespace FastFiles.Verify {
    public sealed class ServiceAccessResult {
        public string Name { get; set; }
        public bool Opened { get; set; }
        public int Error { get; set; }
    }

    public static class ServiceAccessProbe {
        [DllImport("advapi32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern bool LogonUser(string user, string domain, string password, int logonType, int provider, out IntPtr token);
        [DllImport("advapi32.dll", SetLastError = true)]
        private static extern bool ImpersonateLoggedOnUser(IntPtr token);
        [DllImport("advapi32.dll", SetLastError = true)]
        private static extern bool RevertToSelf();
        [DllImport("kernel32.dll")]
        private static extern bool CloseHandle(IntPtr handle);
        [DllImport("advapi32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern IntPtr OpenSCManager(string machine, string database, uint access);
        [DllImport("advapi32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern IntPtr OpenService(IntPtr scm, string name, uint access);
        [DllImport("advapi32.dll")]
        private static extern bool CloseServiceHandle(IntPtr handle);

        public static ServiceAccessResult[] Probe(string user, string password, string serviceName) {
            IntPtr token;
            if (!LogonUser(user, ".", password, 2, 0, out token)) throw new Win32Exception(Marshal.GetLastWin32Error(), "LogonUser failed");
            try {
                if (!ImpersonateLoggedOnUser(token)) throw new Win32Exception(Marshal.GetLastWin32Error(), "Impersonation failed");
                try {
                    IntPtr scm = OpenSCManager(null, null, 0x0001);
                    if (scm == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error(), "OpenSCManager failed");
                    try {
                        var rights = new Dictionary<string, uint> {
                            { "query", 0x0001 | 0x0004 }, { "change-config", 0x0002 },
                            { "start", 0x0010 }, { "stop", 0x0020 },
                            { "write-dac", 0x00040000 }, { "write-owner", 0x00080000 }
                        };
                        var results = new List<ServiceAccessResult>();
                        foreach (var pair in rights) {
                            IntPtr service = OpenService(scm, serviceName, pair.Value);
                            int error = service == IntPtr.Zero ? Marshal.GetLastWin32Error() : 0;
                            if (service != IntPtr.Zero) CloseServiceHandle(service);
                            results.Add(new ServiceAccessResult { Name = pair.Key, Opened = service != IntPtr.Zero, Error = error });
                        }
                        return results.ToArray();
                    } finally { CloseServiceHandle(scm); }
                } finally { RevertToSelf(); }
            } finally { CloseHandle(token); }
        }
    }
}
'@
}

function Invoke-ClientGroupServiceAccessProbe {
    Initialize-ServiceAccessProbeType
    $suffix = [guid]::NewGuid().ToString('n').Substring(0, 10)
    $userName = "FFVerify_$suffix"
    $password = "Ff!$([guid]::NewGuid().ToString('n'))9a"
    $securePassword = ConvertTo-SecureString $password -AsPlainText -Force
    try {
        New-LocalUser -Name $userName -Password $securePassword -AccountNeverExpires -PasswordNeverExpires -UserMayNotChangePassword | Out-Null
        Add-LocalGroupMember -Group $script:ClientGroup -Member $userName
        $results = @([FastFiles.Verify.ServiceAccessProbe]::Probe($userName, $password, $script:ServiceName))
        $query = $results | Where-Object Name -eq query
        $forbidden = @($results | Where-Object Name -ne query)
        $passed = $query.Opened -and @($forbidden | Where-Object { $_.Opened -or $_.Error -ne 5 }).Count -eq 0
        return [pscustomobject]@{ passed = $passed; user = $userName; results = $results }
    } finally {
        Remove-LocalUser -Name $userName -ErrorAction SilentlyContinue
        $password = $null
        $securePassword = $null
    }
}

function Invoke-ServiceLifecycleChecks {
    param([Parameter(Mandatory)] [datetime] $Since, [Parameter(Mandatory)] [string] $ArtifactsDir)
    $results = @()
    $timeline = @()
    $started = Get-Date
    try {
        Stop-Service -Name $script:ServiceName -Force -ErrorAction Stop
        $stopped = Wait-ServiceState -Status Stopped
        $timeline += [pscustomobject]@{ action = 'stop'; reached = $null -ne $stopped; atUtc = (Get-Date).ToUniversalTime().ToString('o') }
        Start-Service -Name $script:ServiceName -ErrorAction Stop
        $running = Wait-ServiceState -Status Running
        $timeline += [pscustomobject]@{ action = 'start'; reached = $null -ne $running; atUtc = (Get-Date).ToUniversalTime().ToString('o') }
        Stop-Service -Name $script:ServiceName -Force -ErrorAction Stop
        $restopped = Wait-ServiceState -Status Stopped
        Start-Service -Name $script:ServiceName -ErrorAction Stop
        $restarted = Wait-ServiceState -Status Running
        $timeline += [pscustomobject]@{ action = 'restart'; reached = $null -ne $restopped -and $null -ne $restarted; atUtc = (Get-Date).ToUniversalTime().ToString('o') }
        $transitionsOk = $null -ne $stopped -and $null -ne $running -and $null -ne $restopped -and $null -ne $restarted
        $results += New-InstallSubResult -Id 'service-start-stop-restart' -Status $(if ($transitionsOk) { 'PASS' } else { 'FAIL' }) -Reason $(if (-not $transitionsOk) { 'service-transition-timeout' }) -DurationMs ((Get-Date)-$started).TotalMilliseconds -Detail 'Bounded stop/start/restart transitions through SCM.'

        if ($restarted) {
            $oldPid = [int]$restarted.ProcessId
            Stop-Process -Id $oldPid -Force -ErrorAction Stop
            $recovered = Wait-ServiceState -Status Running -TimeoutSeconds 45 -DifferentProcessId $oldPid
            $timeline += [pscustomobject]@{ action = 'unexpected-termination'; oldProcessId = $oldPid; newProcessId = if ($recovered) { $recovered.ProcessId } else { 0 }; recovered = $null -ne $recovered; atUtc = (Get-Date).ToUniversalTime().ToString('o') }
            $results += New-InstallSubResult -Id 'service-recovery-action' -Status $(if ($recovered) { 'PASS' } else { 'FAIL' }) -Reason $(if (-not $recovered) { 'service-recovery-not-observed' }) -DurationMs 0 -Detail "oldPid=$oldPid; newPid=$(if ($recovered) { $recovered.ProcessId } else { 0 })"
        }
    } catch {
        $results += New-InstallSubResult -Id 'service-lifecycle-exception' -Status FAIL -Reason 'service-lifecycle-error' -DurationMs ((Get-Date)-$started).TotalMilliseconds -Detail $_.Exception.Message
    }
    $timelinePath = Join-Path $ArtifactsDir 'service-lifecycle.json'
    $timeline | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $timelinePath -Encoding utf8

    $events = @(Get-WinEvent -FilterHashtable @{ LogName = 'System'; ProviderName = 'Service Control Manager'; StartTime = $Since } -ErrorAction SilentlyContinue |
        Where-Object { $_.Message -match 'FastFiles Index Service|FastFilesIndexSvc' } |
        Select-Object TimeCreated, Id, LevelDisplayName, Message)
    $eventPath = Join-Path $ArtifactsDir 'service-control-manager-events.json'
    $events | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $eventPath -Encoding utf8
    $results += New-InstallSubResult -Id 'service-event-viewer' -Status $(if ($events.Count -gt 0) { 'PASS' } else { 'FAIL' }) -Reason $(if ($events.Count -eq 0) { 'service-lifecycle-events-missing' }) -DurationMs 0 -Detail "$($events.Count) matching SCM events captured."
    $serviceLogRoot = Join-Path $env:ProgramData 'FastFiles\logs'
    $serviceLogs = @(Get-ChildItem -LiteralPath $serviceLogRoot -File -ErrorAction SilentlyContinue)
    $serviceLogArtifacts = @()
    $lifecycleRecordsPresent = $false
    if ($serviceLogs.Count -gt 0) {
        $archiveRoot = Join-Path $ArtifactsDir 'service-logs'
        New-Item -ItemType Directory -Path $archiveRoot -Force | Out-Null
        foreach ($log in $serviceLogs) {
            $content = Get-Content -LiteralPath $log.FullName -Raw -Encoding Unicode
            if ($content -match 'service started' -and $content -match 'service stop requested' -and $content -match 'service stopped') { $lifecycleRecordsPresent = $true }
            Copy-Item -LiteralPath $log.FullName -Destination (Join-Path $archiveRoot $log.Name) -Force
            $serviceLogArtifacts += [pscustomobject]@{ path = "artifacts/windows-install-service-validation/service-logs/$($log.Name)"; type = 'service-log' }
        }
    }
    $logsOk = $serviceLogs.Count -gt 0 -and $lifecycleRecordsPresent
    $results += New-InstallSubResult -Id 'service-owned-logs' -Status $(if ($logsOk) { 'PASS' } else { 'FAIL' }) -Reason $(if (-not $logsOk) { 'service-lifecycle-log-missing' }) -DurationMs 0 -Detail "files=$($serviceLogs.Count); lifecycleRecords=$lifecycleRecordsPresent; source=$serviceLogRoot"
    [pscustomobject]@{ Results = $results; Artifacts = @(
        [pscustomobject]@{ path = 'artifacts/windows-install-service-validation/service-lifecycle.json'; type = 'service-lifecycle' }
        [pscustomobject]@{ path = 'artifacts/windows-install-service-validation/service-control-manager-events.json'; type = 'event-viewer-log' }
    ) + $serviceLogArtifacts }
}

function Invoke-InstallServiceCapability {
    param($RunContext, $Fingerprint, [string] $ArtifactsDir, [hashtable] $Options)

    $initialState = Test-FastFilesHostState
    if (-not (Test-IsCleanFastFilesHost -State $initialState)) {
        $statePath = Join-Path $ArtifactsDir 'preexisting-state.json'
        $initialState | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $statePath -Encoding utf8
        return [pscustomobject]@{
            Status = 'SKIPPED'; Reason = 'preexisting-installation-protected'; Summary = 'Refused to mutate an existing FastFiles installation.'
            Artifacts = @([pscustomobject]@{ path = 'artifacts/windows-install-service-validation/preexisting-state.json'; type = 'host-state' })
            SubResults = @([pscustomobject]@{ id = 'clean-host-precondition'; tier = 1; status = 'SKIPPED'; reason = 'preexisting-installation-protected'; requiredContext = [pscustomobject]@{ needs = 'Clean disposable host or explicit removal of the existing FastFiles installation' }; durationMs = 0; detail = ($initialState | ConvertTo-Json -Compress); diagnostics = @() })
        }
    }

    $stagingDir = Join-Path $ArtifactsDir 'staging'
    $logDir = Join-Path $ArtifactsDir 'installer-logs'
    New-Item -ItemType Directory -Path $logDir -Force | Out-Null
    $results = @()
    $artifacts = @()
    $installed = $false
    $setupPath = $null
    try {
        $setupPath = Get-InstallerPayload -RepoRoot $Options.RepoRoot -StagingDir $stagingDir
        foreach ($operation in @('install', 'upgrade')) {
            $record = Invoke-LoggedProcess -FilePath $setupPath -ArgumentList @('/install') -LogRoot $logDir -Operation $operation
            $operationOk = -not $record.timedOut -and $record.exitCode -eq 0
            if ($operation -eq 'install') { $installed = $operationOk }
            $results += New-InstallSubResult -Id "installer-$operation" -Status $(if ($operationOk) { 'PASS' } else { 'FAIL' }) -Reason $(if (-not $operationOk) { 'installer-operation-failed' }) -DurationMs $record.durationMs -Detail "mode=$operation; exitCode=$($record.exitCode); timedOut=$($record.timedOut)"
            foreach ($suffix in @('stdout.log','stderr.log','operation.json')) { $artifacts += [pscustomobject]@{ path = "artifacts/windows-install-service-validation/installer-logs/$operation.$suffix"; type = 'installer-log' } }
            if (-not $operationOk) { break }
            if ($operation -eq 'install') {
                $integrity = Get-InstallIntegritySnapshot
                $integrityPath = Join-Path $ArtifactsDir 'post-install-integrity.json'
                $integrity | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $integrityPath -Encoding utf8
                $artifacts += [pscustomobject]@{ path = 'artifacts/windows-install-service-validation/post-install-integrity.json'; type = 'install-integrity' }
                Add-IntegrityResults -Snapshot $integrity -Results ([ref]$results)
            }
        }

        if ($installed -and (Test-Path -LiteralPath (Join-Path $script:InstallDir 'FastFiles.exe'))) {
            $installedUi = Join-Path $script:InstallDir 'FastFiles.exe'
            $sourceHash = (Get-FileHash -LiteralPath (Join-Path $stagingDir 'FastFiles.exe') -Algorithm SHA256).Hash
            Copy-Item -LiteralPath $setupPath -Destination $installedUi -Force
            $tamperedHash = (Get-FileHash -LiteralPath $installedUi -Algorithm SHA256).Hash
            $repair = Invoke-LoggedProcess -FilePath $setupPath -ArgumentList @('/install') -LogRoot $logDir -Operation repair
            $repairedHash = if (Test-Path -LiteralPath $installedUi) { (Get-FileHash -LiteralPath $installedUi -Algorithm SHA256).Hash } else { $null }
            $repairOk = -not $repair.timedOut -and $repair.exitCode -eq 0 -and $tamperedHash -ne $sourceHash -and $repairedHash -eq $sourceHash
            $results += New-InstallSubResult -Id 'installer-repair' -Status $(if ($repairOk) { 'PASS' } else { 'FAIL' }) -Reason $(if (-not $repairOk) { 'installer-repair-did-not-restore-file' }) -DurationMs $repair.durationMs -Detail "mode=repair-via-reinstall; exitCode=$($repair.exitCode); tamperObserved=$($tamperedHash -ne $sourceHash); restored=$($repairedHash -eq $sourceHash)"
            foreach ($suffix in @('stdout.log','stderr.log','operation.json')) { $artifacts += [pscustomobject]@{ path = "artifacts/windows-install-service-validation/installer-logs/repair.$suffix"; type = 'installer-log' } }

            $config = Get-InstallIntegritySnapshot
            $configPath = Join-Path $ArtifactsDir 'service-config.json'
            $config | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $configPath -Encoding utf8
            $artifacts += [pscustomobject]@{ path = 'artifacts/windows-install-service-validation/service-config.json'; type = 'service-config' }
            $serviceAccountOk = $config.service.startName -ieq 'NT SERVICE\FastFilesIndexSvc'
            $startModeOk = $config.service.startMode -eq 'Auto'
            $delayedOk = [bool]$config.service.delayedAutoStart
            $results += New-InstallSubResult -Id 'service-account-start-mode' -Status $(if ($serviceAccountOk -and $startModeOk) { 'PASS' } else { 'FAIL' }) -Reason $(if (-not ($serviceAccountOk -and $startModeOk)) { 'service-account-or-start-mode-mismatch' }) -DurationMs 0 -Detail "startName=$($config.service.startName); startMode=$($config.service.startMode)"
            $results += New-InstallSubResult -Id 'service-delayed-start' -Status $(if ($delayedOk) { 'PASS' } else { 'FAIL' }) -Reason $(if (-not $delayedOk) { 'delayed-auto-start-not-configured' }) -DurationMs 0 -Detail "delayedAutoStart=$delayedOk"

            $security = Get-ServiceSecurityCheck
            $securityPath = Join-Path $ArtifactsDir 'service-security-sddl.txt'
            $security.sddl | Set-Content -LiteralPath $securityPath -Encoding utf8
            $artifacts += [pscustomobject]@{ path = 'artifacts/windows-install-service-validation/service-security-sddl.txt'; type = 'service-security-descriptor' }
            $results += New-InstallSubResult -Id 'client-group-service-control-rights' -Status $(if ($security.passed) { 'PASS' } else { 'FAIL' }) -Reason $(if (-not $security.passed) { 'client-group-service-rights-mismatch' }) -DurationMs 0 -Detail $security.detail
            $accessProbe = Invoke-ClientGroupServiceAccessProbe
            $accessProbePath = Join-Path $ArtifactsDir 'client-group-service-access.json'
            $accessProbe | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $accessProbePath -Encoding utf8
            $artifacts += [pscustomobject]@{ path = 'artifacts/windows-install-service-validation/client-group-service-access.json'; type = 'service-access-probe' }
            $results += New-InstallSubResult -Id 'client-group-service-control-attempts' -Status $(if ($accessProbe.passed) { 'PASS' } else { 'FAIL' }) -Reason $(if (-not $accessProbe.passed) { 'client-group-service-control-not-denied' }) -DurationMs 0 -Detail (($accessProbe.results | Select-Object Name,Opened,Error) | ConvertTo-Json -Compress)

            $lifecycle = Invoke-ServiceLifecycleChecks -Since $Fingerprint.StartedAtUtc -ArtifactsDir $ArtifactsDir
            $results += $lifecycle.Results
            $artifacts += $lifecycle.Artifacts
        }
    } catch {
        $results += New-InstallSubResult -Id 'installer-capability-exception' -Status FAIL -Reason 'installer-capability-error' -DurationMs 0 -Detail $_.Exception.Message
    } finally {
        if ($setupPath -and (Test-Path -LiteralPath $setupPath)) {
            try {
                Stop-Service -Name $script:ServiceName -Force -ErrorAction SilentlyContinue
                Wait-ServiceState -Status Stopped -TimeoutSeconds 30 | Out-Null
                $installedUninstaller = Join-Path $script:InstallDir 'FastFilesSetup.exe'
                $uninstallPath = if (Test-Path -LiteralPath $installedUninstaller) { $installedUninstaller } else { $setupPath }
                $uninstall = Invoke-LoggedProcess -FilePath $uninstallPath -ArgumentList @('/uninstall') -LogRoot $logDir -Operation uninstall
                foreach ($suffix in @('stdout.log','stderr.log','operation.json')) { $artifacts += [pscustomobject]@{ path = "artifacts/windows-install-service-validation/installer-logs/uninstall.$suffix"; type = 'installer-log' } }
                $deadline = (Get-Date).AddSeconds(30)
                do {
                    Start-Sleep -Milliseconds 250
                    $finalState = Test-FastFilesHostState
                } while (-not (Test-IsCleanFastFilesHost -State $finalState) -and (Get-Date) -lt $deadline)
                $teardownOk = $uninstall.exitCode -eq 0 -and -not $uninstall.timedOut -and (Test-IsCleanFastFilesHost -State $finalState)
                $results += New-InstallSubResult -Id 'installer-uninstall-teardown' -Status $(if ($teardownOk) { 'PASS' } else { 'FAIL' }) -Reason $(if (-not $teardownOk) { 'teardown-left-host-state' }) -DurationMs $uninstall.durationMs -Detail ($finalState | ConvertTo-Json -Compress)
                $teardownPath = Join-Path $ArtifactsDir 'teardown-state.json'
                $finalState | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $teardownPath -Encoding utf8
                $artifacts += [pscustomobject]@{ path = 'artifacts/windows-install-service-validation/teardown-state.json'; type = 'teardown-state' }
            } catch {
                $results += New-InstallSubResult -Id 'installer-uninstall-teardown' -Status FAIL -Reason 'teardown-exception' -DurationMs 0 -Detail $_.Exception.Message
            }
        }
        if (Test-Path -LiteralPath $stagingDir) { Remove-Item -LiteralPath $stagingDir -Recurse -Force }
    }

    $failureCount = @($results | Where-Object status -eq FAIL).Count
    return [pscustomobject]@{
        Status = if ($failureCount -gt 0) { 'FAIL' } else { 'PASS' }
        Reason = if ($failureCount -gt 0) { 'installer-or-service-validation-failed' } else { $null }
        Summary = "$(@($results | Where-Object status -eq PASS).Count) passed, $failureCount failed, $(@($results | Where-Object status -eq SKIPPED).Count) skipped"
        Artifacts = $artifacts
        SubResults = $results
    }
}

Export-ModuleMember -Function Test-InstallServiceCapabilityAvailability, Invoke-InstallServiceCapability, Get-InstallServiceCapabilityDiagnostics
