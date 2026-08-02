#Requires -RunAsAdministrator
#Requires -Version 7.0

<#
.SYNOPSIS
    Run the FastFiles raw-volume privilege candidate matrix on the current machine.

.DESCRIPTION
    Stops FastFilesIndexSvc, temporarily replaces its binary with a diagnostic
    build that supports --run-candidate-matrix, reconfigures the service
    account/rights/groups for each candidate, starts the service to capture one
    matrix row, and restores the original configuration when done.

    This is a quick local diagnostic (not a clean VM) to determine whether any
    narrow documented right or group membership opens the raw volume, or whether
    a constrained broker is required.

.PARAMETER NewBinaryPath
    Path to the rebuilt FastFilesIndexSvc.exe that contains the matrix mode.

.PARAMETER ServiceName
    Name of the FastFiles privileged service (default: FastFilesIndexSvc).

.PARAMETER DriveLetter
    Drive letter to test against (default: C).

.PARAMETER OutputDirectory
    Directory where one JSON file per candidate and a summary.json are written.

.EXAMPLE
    pwsh -File openspec/changes/resolve-raw-volume-privilege-insufficiency/Run-PrivilegeCandidateMatrix.ps1 `
         -NewBinaryPath build/debug/src/indexsvc/FastFilesIndexSvc.exe
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$NewBinaryPath,

    [string]$ServiceName = 'FastFilesIndexSvc',

    [char]$DriveLetter = 'C',

    [string]$OutputDirectory = "$env:ProgramData\FastFiles\matrix-run"
)

$ErrorActionPreference = 'Stop'

# -----------------------------------------------------------------------------
# P/Invoke for LSA account rights and local group membership.
# Service configuration is done via Win32_Service.Change (WMI) instead of P/Invoke
# to avoid Win32 marshalling quirks in PowerShell 7.
# -----------------------------------------------------------------------------
Add-Type -TypeDefinition @"
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Security.Principal;

public class PrivilegeMatrixApi {
    // ---- LSA ----
    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern uint LsaOpenPolicy(IntPtr SystemName, ref LSA_OBJECT_ATTRIBUTES ObjectAttributes, uint DesiredAccess, out IntPtr PolicyHandle);

    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern uint LsaAddAccountRights(IntPtr PolicyHandle, byte[] AccountSid, LSA_UNICODE_STRING[] UserRights, uint CountOfRights);

    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern uint LsaRemoveAccountRights(IntPtr PolicyHandle, byte[] AccountSid, byte AllRights, LSA_UNICODE_STRING[] UserRights, uint CountOfRights);

    [DllImport("advapi32.dll")]
    public static extern uint LsaClose(IntPtr PolicyHandle);

    [DllImport("advapi32.dll")]
    public static extern uint LsaNtStatusToWinError(uint Status);

    [StructLayout(LayoutKind.Sequential)]
    public struct LSA_OBJECT_ATTRIBUTES {
        public uint Length;
        public IntPtr RootDirectory;
        public IntPtr ObjectName;
        public uint Attributes;
        public IntPtr SecurityDescriptor;
        public IntPtr SecurityQualityOfService;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct LSA_UNICODE_STRING {
        public ushort Length;
        public ushort MaximumLength;
        public IntPtr Buffer;
    }

    public const uint POLICY_CREATE_ACCOUNT = 0x00000010;
    public const uint POLICY_LOOKUP_NAMES = 0x00000800;

    // ---- NetApi local groups ----
    [DllImport("netapi32.dll", CharSet = CharSet.Unicode, SetLastError = false)]
    public static extern uint NetLocalGroupAddMembers(string servername, string groupname, uint level, LOCALGROUP_MEMBERS_INFO_0[] buf, uint totalentries);

    [DllImport("netapi32.dll", CharSet = CharSet.Unicode, SetLastError = false)]
    public static extern uint NetLocalGroupDelMembers(string servername, string groupname, uint level, LOCALGROUP_MEMBERS_INFO_0[] buf, uint totalentries);

    [StructLayout(LayoutKind.Sequential)]
    public struct LOCALGROUP_MEMBERS_INFO_0 {
        public IntPtr lgrmi0_sid;
    }

    public const uint NERR_Success = 0;
    public const uint NERR_UserInGroup = 2236;
    public const uint ERROR_MEMBER_IN_ALIAS = 1378;
}
"@

# -----------------------------------------------------------------------------
# Helper functions
# -----------------------------------------------------------------------------
function Grant-Privilege {
    param([byte[]]$Sid, [string]$Privilege)
    $buf = [System.Runtime.InteropServices.Marshal]::StringToHGlobalUni($Privilege)
    $rights = [PrivilegeMatrixApi+LSA_UNICODE_STRING[]]::new(1)
    $rights[0] = [PrivilegeMatrixApi+LSA_UNICODE_STRING]@{
        Length = [ushort]($Privilege.Length * 2)
        MaximumLength = [ushort](($Privilege.Length + 1) * 2)
        Buffer = $buf
    }
    $attrs = [PrivilegeMatrixApi+LSA_OBJECT_ATTRIBUTES]@{
        Length = [uint][System.Runtime.InteropServices.Marshal]::SizeOf([PrivilegeMatrixApi+LSA_OBJECT_ATTRIBUTES])
    }
    $policy = [IntPtr]::Zero
    $status = [PrivilegeMatrixApi]::LsaOpenPolicy([IntPtr]::Zero, [ref]$attrs,
        [PrivilegeMatrixApi]::POLICY_CREATE_ACCOUNT -bor [PrivilegeMatrixApi]::POLICY_LOOKUP_NAMES, [ref]$policy)
    if ($status -ne 0) { throw "LsaOpenPolicy failed: 0x$([Convert]::ToString($status, 16))" }
    try {
        $status = [PrivilegeMatrixApi]::LsaAddAccountRights($policy, $Sid, $rights, 1)
        if ($status -ne 0) { throw "LsaAddAccountRights for $Privilege failed: 0x$([Convert]::ToString($status, 16))" }
    } finally {
        [System.Runtime.InteropServices.Marshal]::FreeHGlobal($buf)
        [PrivilegeMatrixApi]::LsaClose($policy) | Out-Null
    }
}

function Revoke-Privilege {
    param([byte[]]$Sid, [string]$Privilege)
    $buf = [System.Runtime.InteropServices.Marshal]::StringToHGlobalUni($Privilege)
    $rights = [PrivilegeMatrixApi+LSA_UNICODE_STRING[]]::new(1)
    $rights[0] = [PrivilegeMatrixApi+LSA_UNICODE_STRING]@{
        Length = [ushort]($Privilege.Length * 2)
        MaximumLength = [ushort](($Privilege.Length + 1) * 2)
        Buffer = $buf
    }
    $attrs = [PrivilegeMatrixApi+LSA_OBJECT_ATTRIBUTES]@{
        Length = [uint][System.Runtime.InteropServices.Marshal]::SizeOf([PrivilegeMatrixApi+LSA_OBJECT_ATTRIBUTES])
    }
    $policy = [IntPtr]::Zero
    $status = [PrivilegeMatrixApi]::LsaOpenPolicy([IntPtr]::Zero, [ref]$attrs,
        [PrivilegeMatrixApi]::POLICY_CREATE_ACCOUNT -bor [PrivilegeMatrixApi]::POLICY_LOOKUP_NAMES, [ref]$policy)
    if ($status -ne 0) { throw "LsaOpenPolicy failed: 0x$([Convert]::ToString($status, 16))" }
    try {
        $status = [PrivilegeMatrixApi]::LsaRemoveAccountRights($policy, $Sid, 0, $rights, 1)
        if ($status -ne 0) { throw "LsaRemoveAccountRights for $Privilege failed: 0x$([Convert]::ToString($status, 16))" }
    } finally {
        [System.Runtime.InteropServices.Marshal]::FreeHGlobal($buf)
        [PrivilegeMatrixApi]::LsaClose($policy) | Out-Null
    }
}

function Get-LocalizedGroupName {
    param([string]$Sid)
    $sidObj = New-Object System.Security.Principal.SecurityIdentifier($Sid)
    $account = $sidObj.Translate([System.Security.Principal.NTAccount])
    $name = $account.Value
    $slash = $name.IndexOf('\')
    if ($slash -ge 0) { $name = $name.Substring($slash + 1) }
    return $name
}

function Add-GroupMemberSid {
    param([byte[]]$SidBytes, [string]$GroupName)
    $handle = [System.Runtime.InteropServices.GCHandle]::Alloc($SidBytes, [System.Runtime.InteropServices.GCHandleType]::Pinned)
    try {
        $member = [PrivilegeMatrixApi+LOCALGROUP_MEMBERS_INFO_0]@{ lgrmi0_sid = $handle.AddrOfPinnedObject() }
        $members = [PrivilegeMatrixApi+LOCALGROUP_MEMBERS_INFO_0[]]::new(1)
        $members[0] = $member
        $status = [PrivilegeMatrixApi]::NetLocalGroupAddMembers($null, $GroupName, 0, $members, 1)
        if ($status -ne 0 -and $status -ne [PrivilegeMatrixApi]::NERR_UserInGroup -and $status -ne [PrivilegeMatrixApi]::ERROR_MEMBER_IN_ALIAS) {
            throw "NetLocalGroupAddMembers to $GroupName failed: 0x$([Convert]::ToString($status, 16))"
        }
    } finally {
        $handle.Free()
    }
}

function Remove-GroupMemberSid {
    param([byte[]]$SidBytes, [string]$GroupName)
    $handle = [System.Runtime.InteropServices.GCHandle]::Alloc($SidBytes, [System.Runtime.InteropServices.GCHandleType]::Pinned)
    try {
        $member = [PrivilegeMatrixApi+LOCALGROUP_MEMBERS_INFO_0]@{ lgrmi0_sid = $handle.AddrOfPinnedObject() }
        $members = [PrivilegeMatrixApi+LOCALGROUP_MEMBERS_INFO_0[]]::new(1)
        $members[0] = $member
        $status = [PrivilegeMatrixApi]::NetLocalGroupDelMembers($null, $GroupName, 0, $members, 1)
        if ($status -ne 0 -and $status -ne [PrivilegeMatrixApi]::NERR_Success) {
            # Ignore "member not in group" codes; we just want it removed.
            if ($status -ne 2220 -and $status -ne 1377) {
                Write-Warning "NetLocalGroupDelMembers from $GroupName returned 0x$([Convert]::ToString($status, 16))"
            }
        }
    } finally {
        $handle.Free()
    }
}

function Set-ServiceConfig {
    param(
        [string]$ServiceName,
        [string]$BinaryPathName = $null,
        [string]$ServiceStartName = $null,
        [string]$Password = $null
    )
    # Write directly to the service registry key instead of using sc.exe or WMI.
    # The SCM reads these values on the next service start, so the changes take
    # effect after we stop/start the service between candidates.
    $key = "HKLM:\SYSTEM\CurrentControlSet\Services\$ServiceName"
    if (-not (Test-Path -Path $key)) {
        throw "Service registry key not found: $key"
    }
    if ($null -ne $BinaryPathName) {
        Set-ItemProperty -Path $key -Name "ImagePath" -Value $BinaryPathName -Type ExpandString
    }
    if ($null -ne $ServiceStartName) {
        Set-ItemProperty -Path $key -Name "ObjectName" -Value $ServiceStartName -Type String
    }
    # Service-account passwords are not stored in the registry for virtual/local
    # system accounts; the Password parameter is ignored.
}

function Stop-ServiceAndWait {
    param([string]$Name, [int]$TimeoutSeconds = 120)
    $s = Get-Service -Name $Name -ErrorAction SilentlyContinue
    if (-not $s -or $s.Status -eq 'Stopped') { return }
    Stop-Service -Name $Name -ErrorAction Stop
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        $s = Get-Service -Name $Name
        if ($s.Status -eq 'Stopped') { break }
        Start-Sleep -Milliseconds 200
    }
    if ($s.Status -ne 'Stopped') {
        throw "Timeout waiting for service $Name to report Stopped status"
    }
    # The SCM status can flip to Stopped before the process actually exits.
    # Wait for the executable image to be released before replacing it.
    while ($sw.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        $proc = Get-Process -Name $Name -ErrorAction SilentlyContinue
        if (-not $proc) { return }
        Start-Sleep -Milliseconds 200
    }
    throw "Timeout waiting for $Name process to terminate after service reported Stopped"
}

function Start-ServiceAndWait {
    param([string]$Name, [int]$TimeoutSeconds = 60)
    Start-Service -Name $Name -ErrorAction Stop
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        $s = Get-Service -Name $Name
        if ($s.Status -eq 'Running') { return }
        Start-Sleep -Milliseconds 200
    }
    throw "Timeout waiting for service $Name to start"
}

function Start-ServiceMatrixRun {
    param([string]$Name, [int]$TimeoutSeconds = 60)
    Start-Service -Name $Name -ErrorAction Stop
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        $s = Get-Service -Name $Name
        if ($s.Status -eq 'Stopped') {
            $exit = (Get-CimInstance Win32_Service -Filter "Name='$Name'").ExitCode
            if ($exit -ne 0) { throw "Matrix service $Name exited with code $exit" }
            return
        }
        Start-Sleep -Milliseconds 200
    }
    throw "Timeout waiting for matrix service $Name to stop"
}

# -----------------------------------------------------------------------------
# Main runner
# -----------------------------------------------------------------------------
Write-Host "Privilege candidate matrix runner"
Write-Host "Service: $ServiceName"
Write-Host "Drive:   ${DriveLetter}:"
Write-Host "Output:  $OutputDirectory"
Write-Host ""

$NewBinaryPath = Resolve-Path -Path $NewBinaryPath | Select-Object -ExpandProperty Path
if (-not (Test-Path -LiteralPath $NewBinaryPath)) { throw "New binary not found: $NewBinaryPath" }

$service = Get-CimInstance Win32_Service -Filter "Name='$ServiceName'"
if (-not $service) { throw "Service '$ServiceName' not found." }

# Extract installed executable path from the original binary path name.
$originalPathName = $service.PathName
if ($originalPathName.StartsWith('"')) {
    $installedExePath = ($originalPathName -split '"')[1]
} else {
    $installedExePath = ($originalPathName -split ' ')[0]
}
if (-not (Test-Path -LiteralPath $installedExePath)) { throw "Installed executable not found: $installedExePath" }

$originalStartName = $service.StartName
if (-not $originalStartName) { $originalStartName = 'LocalSystem' }
Write-Host "Original service account: $originalStartName"
Write-Host "Original binary path:     $originalPathName"
Write-Host "Installed executable:     $installedExePath"
Write-Host ""

# Resolve the service virtual account SID (used for group membership and LSA rights).
$serviceAccount = "NT SERVICE\$ServiceName"
$serviceSid = (New-Object System.Security.Principal.NTAccount($serviceAccount)).Translate([System.Security.Principal.SecurityIdentifier]).Value
$serviceSidObj = New-Object System.Security.Principal.SecurityIdentifier($serviceSid)
$serviceSidBytes = New-Object byte[] $serviceSidObj.BinaryLength
$serviceSidObj.GetBinaryForm($serviceSidBytes, 0)
Write-Host "Service SID: $serviceSid"
Write-Host ""

$null = New-Item -ItemType Directory -Force -Path $OutputDirectory

# Copy the diagnostic build to a separate path under ProgramData so we do not
# have to wait for the locked installed binary to be released. The service
# account already has access to this location because the service logs there.
$matrixExePath = Join-Path $OutputDirectory 'FastFilesIndexSvc.matrix.exe'
Copy-Item -LiteralPath $NewBinaryPath -Destination $matrixExePath -Force -ErrorAction Stop
Write-Host "Diagnostic matrix binary copied to $matrixExePath"
Write-Host ""

$adminGroupName = Get-LocalizedGroupName -Sid 'S-1-5-32-544'
$backupGroupName = Get-LocalizedGroupName -Sid 'S-1-5-32-551'
Write-Host "Localized group names: Administrators=$adminGroupName, BackupOperators=$backupGroupName"

$candidates = @(
    @{ Id = 'baseline-sebackup';      PrivilegeToGrant = $null;                       PrivilegeToTest = 'SeBackupPrivilege';      GroupName = $null;            Account = $null;           Order = 1 },
    @{ Id = 'sebackup-restore';       PrivilegeToGrant = 'SeRestorePrivilege';        PrivilegeToTest = 'SeRestorePrivilege';     GroupName = $null;            Account = $null;           Order = 2 },
    @{ Id = 'semanage-volume';        PrivilegeToGrant = 'SeManageVolumePrivilege';   PrivilegeToTest = 'SeManageVolumePrivilege';  GroupName = $null;            Account = $null;           Order = 3 },
    @{ Id = 'sebackup-semanage';      PrivilegeToGrant = 'SeManageVolumePrivilege';   PrivilegeToTest = 'SeManageVolumePrivilege';  GroupName = $null;            Account = $null;           Order = 4 },
    @{ Id = 'se-security';            PrivilegeToGrant = 'SeSecurityPrivilege';       PrivilegeToTest = 'SeSecurityPrivilege';     GroupName = $null;            Account = $null;           Order = 5 },
    @{ Id = 'backup-operators';       PrivilegeToGrant = $null;                       PrivilegeToTest = 'SeBackupPrivilege';      GroupName = $backupGroupName; Account = $null;           Order = 6 },
    @{ Id = 'control-administrators'; PrivilegeToGrant = $null;                       PrivilegeToTest = 'SeBackupPrivilege';      GroupName = $adminGroupName;  Account = $null;           Order = 7 },
    @{ Id = 'control-localsystem';    PrivilegeToGrant = $null;                       PrivilegeToTest = 'SeBackupPrivilege';      GroupName = $null;            Account = 'LocalSystem';   Order = 8 }
)

$results = [System.Collections.Generic.List[object]]::new()
$addedPrivileges = [System.Collections.Generic.List[string]]::new()
$addedGroups = [System.Collections.Generic.List[string]]::new()

try {
    Stop-ServiceAndWait -Name $ServiceName

    foreach ($c in $candidates) {
        Write-Host "=== Candidate $($c.Order): $($c.Id) ==="

        # Apply candidate identity / rights.
        if ($c.PrivilegeToGrant) {
            Write-Host "Granting LSA right: $($c.PrivilegeToGrant)"
            Grant-Privilege -Sid $serviceSidBytes -Privilege $c.PrivilegeToGrant
            $addedPrivileges.Add($c.PrivilegeToGrant)
        }
        if ($c.GroupName) {
            Write-Host "Adding service SID to group: $($c.GroupName)"
            Add-GroupMemberSid -SidBytes $serviceSidBytes -GroupName $c.GroupName
            $addedGroups.Add($c.GroupName)
        }
        if ($c.Account) {
            Write-Host "Changing service account to: $($c.Account)"
            if ($c.Account -eq 'LocalSystem') {
                Set-ServiceConfig -ServiceName $ServiceName -ServiceStartName 'LocalSystem'
            } else {
                Set-ServiceConfig -ServiceName $ServiceName -ServiceStartName $c.Account -Password ''
            }
        }

        # Build the matrix-mode binary path for this candidate.
        $outputPath = Join-Path $OutputDirectory "$($c.Id).json"
        $matrixBinPath = '"' + $matrixExePath + '" --run-candidate-matrix ' + $c.Id + ' ' + $c.PrivilegeToTest + ' ' + $DriveLetter + ' ' + $c.Order + ' --matrix-output "' + $outputPath + '"'
        Write-Host "Matrix binary path: $matrixBinPath"
        Set-ServiceConfig -ServiceName $ServiceName -BinaryPathName $matrixBinPath

        # Start the service; it runs the matrix row and exits.
        Start-ServiceMatrixRun -Name $ServiceName -TimeoutSeconds 60
        Write-Host "Service stopped after matrix run"

        # Restore the original binary path immediately so the service is left in
        # a sane state if the rest of the loop fails.
        Set-ServiceConfig -ServiceName $ServiceName -BinaryPathName $originalPathName

        # Undo candidate-specific account/rights/groups before the next candidate.
        if ($c.Account) {
            if ($originalStartName -eq 'LocalSystem') {
                Set-ServiceConfig -ServiceName $ServiceName -ServiceStartName 'LocalSystem'
            } else {
                Set-ServiceConfig -ServiceName $ServiceName -ServiceStartName $originalStartName -Password ''
            }
        }
        if ($c.PrivilegeToGrant) {
            Write-Host "Revoking $($c.PrivilegeToGrant)"
            Revoke-Privilege -Sid $serviceSidBytes -Privilege $c.PrivilegeToGrant
            $addedPrivileges.Remove($c.PrivilegeToGrant)
        }
        if ($c.GroupName) {
            Write-Host "Removing service SID from group $($c.GroupName)"
            Remove-GroupMemberSid -SidBytes $serviceSidBytes -GroupName $c.GroupName
            $addedGroups.Remove($c.GroupName)
        }

        # Collect the row.
        if (Test-Path -LiteralPath $outputPath) {
            $row = Get-Content -LiteralPath $outputPath -Raw | ConvertFrom-Json
            $results.Add($row)
            Write-Host "Result: outcome=$($row.outcome) volumeOpenError=0x$('{0:X}' -f $row.volumeOpenError) journalQueryError=0x$('{0:X}' -f $row.journalQueryError) journalReadError=0x$('{0:X}' -f $row.journalReadError)"
        } else {
            Write-Warning "No output file for candidate $($c.Id) at $outputPath"
        }
        Write-Host ""
    }
} finally {
    # Best-effort restoration of the original service state and binary.
    Write-Host ""
    Write-Host "Restoring original service configuration..."
    try {
        Stop-ServiceAndWait -Name $ServiceName -ErrorAction SilentlyContinue
    } catch { Write-Warning "Stop during restore failed: $_" }

    try {
        Set-ServiceConfig -ServiceName $ServiceName -BinaryPathName $originalPathName
    } catch { Write-Warning "Binary path restore failed: $_" }

    try {
        if ($originalStartName -eq 'LocalSystem') {
            Set-ServiceConfig -ServiceName $ServiceName -ServiceStartName 'LocalSystem'
        } else {
            Set-ServiceConfig -ServiceName $ServiceName -ServiceStartName $originalStartName -Password ''
        }
    } catch { Write-Warning "Service account restore failed: $_" }

    foreach ($priv in $addedPrivileges) {
        try { Revoke-Privilege -Sid $serviceSidBytes -Privilege $priv } catch { Write-Warning "Failed to revoke $priv`: $_" }
    }
    foreach ($grp in $addedGroups) {
        try { Remove-GroupMemberSid -SidBytes $serviceSidBytes -GroupName $grp } catch { Write-Warning "Failed to remove from $grp`: $_" }
    }

    try {
        Start-ServiceAndWait -Name $ServiceName -TimeoutSeconds 120
        Write-Host "Service restarted normally."
    } catch { Write-Warning "Failed to restart service: $_" }
}

# Clean up the diagnostic matrix binary; it is only needed for the run.
try {
    Remove-Item -LiteralPath $matrixExePath -Force -ErrorAction SilentlyContinue
} catch { Write-Warning "Failed to remove matrix binary: $_" }

# -----------------------------------------------------------------------------
# Summary
# -----------------------------------------------------------------------------
$summaryPath = Join-Path $OutputDirectory 'summary.json'
$results | ConvertTo-Json -Depth 10 | Set-Content -Path $summaryPath -Encoding UTF8

Write-Host ""
Write-Host "Candidate matrix complete."
Write-Host "Summary written to: $summaryPath"
Write-Host ""
$results | Select-Object candidateId, outcome, @{N='volumeOpenError'; E={"0x{0:X}" -f $_.volumeOpenError}}, @{N='journalQueryError'; E={"0x{0:X}" -f $_.journalQueryError}}, @{N='journalReadError'; E={"0x{0:X}" -f $_.journalReadError}} | Format-Table -AutoSize

# Return the summary as the script output for easy capture.
$summaryPath
