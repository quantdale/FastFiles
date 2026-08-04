<#
    Disposable Hyper-V Windows guest environment provider (task 6.5 / design.md D5).

    Implements the Environment Provider contract behind the core's provider
    interface (provision -> activate -> collectLogs -> cleanup -> snapshotRestore)
    with supportsSnapshotRestore=true. It is designed to be run against a clean
    reference VHDX template so a full FastFiles validation can execute in an
    isolated guest and then be restored/reset.

    Provisioning is honest about prerequisites: if no Windows install media or a
    clean reference template is present, provision reports Ready=false with a
    machine-readable reason (REQUIRED-BUT-UNAVAILABLE) rather than fabricating a
    guest. This keeps VM-gated tasks truthful until lawful media is supplied.
#>

$script:HyperVModule = 'Hyper-V'
$script:DefaultVmName = 'FastFiles-Validation'
$script:DefaultVhdx = 'C:\VMs\FastFiles-Matrix.vhdx'
$script:DefaultSwitch = 'FastFilesSwitch'

function Assert-HyperVModuleAvailable {
    if (-not (Get-Module -ListAvailable -Name $script:HyperVModule)) {
        throw "Hyper-V PowerShell module is not available. Enable the Hyper-V feature (Microsoft-Hyper-V) and install the Windows Hyper-V PowerShell module."
    }
}

function Test-HyperVGuestProvisionable {
    <# Returns a structured readiness verdict. #>
    $provisionable = $false
    $reasons = @()
    try {
        Assert-HyperVModuleAvailable
    } catch {
        $reasons += 'hyperv-module-unavailable'
    }
    if (-not (Test-Path -LiteralPath $script:DefaultVhdx) -or (Get-Item -LiteralPath $script:DefaultVhdx).Length -lt 1GB) {
        $reasons += 'guest-image-absent'
    }
    $service = Get-Service -Name 'vmms' -ErrorAction SilentlyContinue
    if (-not $service -or $service.Status -ne 'Running') {
        $reasons += 'vmms-not-running'
    }
    $provisionable = $reasons.Count -eq 0
    [pscustomobject]@{
        Provisionable = $provisionable
        Reasons = $reasons
        VhdxPath = $script:DefaultVhdx
        VmName = $script:DefaultVmName
    }
}

function New-HyperVProviderState {
    [pscustomobject]@{
        VmName = $script:DefaultVmName
        Provisioned = $false
        ProvisionedAtUtc = $null
        RestoreCompleted = $false
        CleanupCompleted = $false
        Diagnostics = [System.Collections.Generic.List[string]]::new()
    }
}

function Invoke-HyperVProviderProvision {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [pscustomobject] $ProviderContext)

    $readiness = Test-HyperVGuestProvisionable
    $state = New-HyperVProviderState
    $ProviderContext.State = $state

    if (-not $readiness.Provisionable) {
        $state.Diagnostics.Add("provision-unavailable: $($readiness.Reasons -join ', ')")
        return [pscustomobject]@{
            Ready = $false
            Reason = 'REQUIRED-BUT-UNAVAILABLE'
            Detail = "Hyper-V guest provisioning is not possible in this environment: $($readiness.Reasons -join ', '). A clean Windows VHDX/install media is required."
            VhdxPath = $readiness.VhdxPath
            VmName = $readiness.VmName
        }
    }

    Assert-HyperVModuleAvailable
    $existing = Get-VM -Name $state.VmName -ErrorAction SilentlyContinue
    if ($existing) {
        $state.Provisioned = $true
        $state.ProvisionedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
        return [pscustomobject]@{ Ready = $true; Reused = $true; VmName = $state.VmName; Detail = "Existing VM '$($state.VmName)' reused." }
    }

    $switch = Get-VMSwitch -Name $script:DefaultSwitch -ErrorAction SilentlyContinue
    $switchParam = @()
    if ($switch) { $switchParam = @('-SwitchName', $script:DefaultSwitch) }

    New-VM -Name $state.VmName -VHDPath $script:DefaultVhdx -MemoryStartupBytes 4GB -Generation 2 @switchParam -ErrorAction Stop | Out-Null
    Enable-VMIntegrationService -VMName $state.VmName -Name 'Guest Service Interface' -ErrorAction SilentlyContinue | Out-Null
    $state.Provisioned = $true
    $state.ProvisionedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
    [pscustomobject]@{ Ready = $true; Reused = $false; VmName = $state.VmName; Detail = "Provisioned VM '$($state.VmName)' from VHDX." }
}

function Invoke-HyperVProviderActivate {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [pscustomobject] $ProviderContext)

    if (-not $ProviderContext.State.Provisioned) {
        return [pscustomobject]@{
            Ready = $false
            Reason = 'REQUIRED-BUT-UNAVAILABLE'
            Detail = 'No provisioned Hyper-V guest to activate.'
            TargetIdentity = "$($env:COMPUTERNAME):hyperv"
        }
    }
    $vm = Get-VM -Name $ProviderContext.State.VmName -ErrorAction SilentlyContinue
    [pscustomobject]@{
        Ready = $true
        TargetIdentity = "$($env:COMPUTERNAME):hyperv:$($ProviderContext.State.VmName)"
        VmName = $ProviderContext.State.VmName
        State = if ($vm) { $vm.State.ToString() } else { 'unknown' }
    }
}

function Invoke-HyperVProviderCollectLogs {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [pscustomobject] $ProviderContext)

    if (-not $ProviderContext.State.Provisioned) { return @() }
    $logs = @()
    $vm = Get-VM -Name $ProviderContext.State.VmName -ErrorAction SilentlyContinue
    if ($vm) {
        $logs += [pscustomobject]@{ path = "artifacts/hyperv-provider/vm-state.json"; type = 'hyperv-vm-state' }
    }
    return $logs
}

function Invoke-HyperVProviderCleanup {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [pscustomobject] $ProviderContext)

    if ($ProviderContext.State.CleanupCompleted) { return [pscustomobject]@{ provider = 'hyperv'; succeeded = $true; alreadyCompleted = $true } }
    $entries = @()
    if ($ProviderContext.State.Provisioned) {
        Assert-HyperVModuleAvailable
        $vm = Get-VM -Name $ProviderContext.State.VmName -ErrorAction SilentlyContinue
        if ($vm) {
            try {
                Stop-VM -Name $vm.Name -Force -ErrorAction SilentlyContinue
                Remove-VM -Name $vm.Name -Force -ErrorAction Stop | Out-Null
                $entries += [pscustomobject]@{ name = 'remove-vm'; status = 'PASS'; error = $null }
            } catch {
                $entries += [pscustomobject]@{ name = 'remove-vm'; status = 'FAIL'; error = $_.Exception.Message }
            }
        }
    }
    $result = [pscustomobject]@{
        provider = 'hyperv'
        completedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
        mutatedSystemState = $ProviderContext.State.Provisioned
        actions = $entries
        succeeded = (@($entries | Where-Object { $_.status -eq 'FAIL' }).Count -eq 0)
    }
    $ProviderContext.State.CleanupCompleted = $true
    return $result
}

function Invoke-HyperVProviderSnapshotRestore {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [pscustomobject] $ProviderContext)

    if (-not $ProviderContext.State.Provisioned) {
        return [pscustomobject]@{ Restored = $false; Reason = 'REQUIRED-BUT-UNAVAILABLE'; Detail = 'No provisioned guest to restore.' }
    }
    Assert-HyperVModuleAvailable
    $vm = Get-VM -Name $ProviderContext.State.VmName -ErrorAction SilentlyContinue
    if (-not $vm) {
        return [pscustomobject]@{ Restored = $false; Reason = 'vm-not-found'; Detail = "VM '$($ProviderContext.State.VmName)' not present." }
    }
    $snapshot = Get-VMSnapshot -VMName $vm.Name -ErrorAction SilentlyContinue | Where-Object { $_.Name -like 'FastFiles-Clean*' } | Select-Object -First 1
    if (-not $snapshot) {
        return [pscustomobject]@{ Restored = $false; Reason = 'clean-snapshot-absent'; Detail = 'No FastFiles-Clean* snapshot exists on the guest.' }
    }
    Restore-VMSnapshot -VMName $vm.Name -Name $snapshot.Name -Confirm:$false -ErrorAction Stop | Out-Null
    $ProviderContext.State.RestoreCompleted = $true
    [pscustomobject]@{ Restored = $true; Snapshot = $snapshot.Name; Detail = "Restored guest '$($vm.Name)' to snapshot '$($snapshot.Name)'." }
}

Export-ModuleMember -Function Invoke-HyperVProviderProvision, Invoke-HyperVProviderActivate, Invoke-HyperVProviderCollectLogs, Invoke-HyperVProviderCleanup, Invoke-HyperVProviderSnapshotRestore, Test-HyperVGuestProvisionable