<#
    Disposable Hyper-V Windows guest environment provider (task 6.5 / design.md D5).

    Implements the Environment Provider contract behind the core's provider
    interface (preflight -> provision -> activate -> collectLogs -> cleanup -> snapshotRestore)
    with supportsSnapshotRestore=true. It is designed to be run against a clean
    reference VHDX template so a full FastFiles validation can execute in an
    isolated guest and then be restored/reset.

    Provisioning is honest about prerequisites: if no Windows install media or a
    clean reference template is present, provision reports Ready=false with a
    machine-readable reason (REQUIRED-BUT-UNAVAILABLE) rather than fabricating a
    guest. This keeps VM-gated tasks truthful until lawful media is supplied.
#>

$script:HyperVModule = 'Hyper-V'
$script:DefaultSwitch = 'FastFilesSwitch'
$script:MinimumVhdBytes = 8GB

function Assert-HyperVModuleAvailable {
    if (-not (Get-Module -ListAvailable -Name $script:HyperVModule)) {
        throw "Hyper-V PowerShell module is not available. Enable the Hyper-V feature (Microsoft-Hyper-V) and install the Windows Hyper-V PowerShell module."
    }
}

function Get-HyperVSourcePath {
    param([pscustomobject] $ProviderContext)

    $candidate = if ($ProviderContext -and $ProviderContext.SourcePath) { [string]$ProviderContext.SourcePath } else { [string]$env:FASTFILES_HYPERV_IMAGE }
    if ([string]::IsNullOrWhiteSpace($candidate)) { return $null }
    try {
        if (-not [IO.Path]::IsPathFullyQualified($candidate)) { return $null }
        return [IO.Path]::GetFullPath($candidate)
    } catch {
        return $null
    }
}

function Get-HyperVVmName {
    param([pscustomobject] $ProviderContext)

    if ($ProviderContext -and $ProviderContext.VmName) { return [string]$ProviderContext.VmName }
    $suffix = if ($ProviderContext -and $ProviderContext.RunContext.Timestamp) { [string]$ProviderContext.RunContext.Timestamp } else { 'probe' }
    $safeSuffix = $suffix -replace '[^A-Za-z0-9-]', '-'
    return "FastFiles-Validation-$safeSuffix"
}

function Test-HyperVWindowsImage {
    param([Parameter(Mandatory)] [string] $Path)

    $extension = [IO.Path]::GetExtension($Path).ToLowerInvariant()
    if ($extension -eq '.iso') {
        return [pscustomobject]@{
            Verified = $false
            ReasonCode = 'GUEST_TOOLCHAIN_UNAVAILABLE'
            Detail = 'An ISO is accepted for bootstrap-plan generation only; it is not an installed guest template.'
        }
    }
    if (-not (Get-Command Mount-DiskImage -ErrorAction SilentlyContinue) -or
        -not (Get-Command Dismount-DiskImage -ErrorAction SilentlyContinue)) {
        return [pscustomobject]@{
            Verified = $false
            ReasonCode = 'PROVIDER_DEPENDENCY_UNAVAILABLE'
            Detail = 'Mount-DiskImage/Dismount-DiskImage are required for read-only Windows-installation inspection.'
        }
    }

    $mounted = $null
    try {
        $mounted = Mount-DiskImage -ImagePath $Path -Access ReadOnly -PassThru -ErrorAction Stop
        $disk = $mounted | Get-Disk -ErrorAction Stop
        $volumes = @($disk | Get-Partition -ErrorAction Stop | Get-Volume -ErrorAction SilentlyContinue)
        foreach ($volume in $volumes) {
            if ([string]::IsNullOrWhiteSpace([string]$volume.Path)) { continue }
            $systemHive = Join-Path ([string]$volume.Path) 'Windows\System32\config\SYSTEM'
            if (Test-Path -LiteralPath $systemHive -PathType Leaf) {
                return [pscustomobject]@{ Verified = $true; ReasonCode = $null; Detail = 'Read-only inspection found Windows\System32\config\SYSTEM.' }
            }
        }
        [pscustomobject]@{ Verified = $false; ReasonCode = 'VHDX_NOT_WINDOWS_INSTALLATION'; Detail = 'Read-only image inspection did not find Windows\System32\config\SYSTEM.' }
    } catch {
        [pscustomobject]@{ Verified = $false; ReasonCode = 'VHDX_NOT_WINDOWS_INSTALLATION'; Detail = 'Read-only image inspection failed; the provider will not treat the image as a usable Windows installation.' }
    } finally {
        if ($mounted) {
            try { Dismount-DiskImage -ImagePath $Path -ErrorAction Stop | Out-Null } catch { }
        }
    }
}

function Test-HyperVGuestProvisionable {
    <# Returns a structured readiness verdict with stable reason codes. #>
    param([pscustomobject] $ProviderContext)

    $reasons = [System.Collections.Generic.List[string]]::new()
    $information = [System.Collections.Generic.List[string]]::new()
    $sourcePath = Get-HyperVSourcePath -ProviderContext $ProviderContext
    $vmName = Get-HyperVVmName -ProviderContext $ProviderContext
    $vm = $null
    try {
        Assert-HyperVModuleAvailable
    } catch {
        $reasons.Add('HYPERV_FEATURE_UNAVAILABLE')
    }
    if ($reasons.Count -eq 0) {
        $service = Get-Service -Name 'vmms' -ErrorAction SilentlyContinue
        if (-not $service -or $service.Status -ne 'Running') {
            $reasons.Add('PROVIDER_DEPENDENCY_UNAVAILABLE')
        }
        $vm = Get-VM -Name $vmName -ErrorAction SilentlyContinue
        if ($vm -and [string]$vm.Generation -ne '2') {
            $reasons.Add('UNSUPPORTED_VM_GENERATION')
        }
    }
    if (-not $vm) {
        $information.Add('VM_ABSENT')
        if (-not $sourcePath) {
            $reasons.Add('NO_USABLE_SOURCE_IMAGE')
        } elseif (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
            $reasons.Add('SOURCE_IMAGE_PATH_INVALID')
        } else {
            $source = Get-Item -LiteralPath $sourcePath -ErrorAction SilentlyContinue
            $extension = [IO.Path]::GetExtension($sourcePath).ToLowerInvariant()
            if (-not $source -or $source.Length -lt 1MB -or @('.vhd', '.vhdx', '.iso') -notcontains $extension) {
                $reasons.Add('NO_USABLE_SOURCE_IMAGE')
            } elseif ($extension -eq '.iso') {
                # An ISO is accepted as a user-supplied source for bootstrap-plan
                # generation, but this provider never claims an unattended install
                # ran without an authorized guest bootstrap step.
                $reasons.Add('GUEST_TOOLCHAIN_UNAVAILABLE')
            } elseif (Get-Command Get-VHD -ErrorAction SilentlyContinue) {
                try {
                    $vhd = Get-VHD -Path $sourcePath -ErrorAction Stop
                    if ([string]$vhd.VhdFormat -notin @('VHD', 'VHDX') -or [int64]$vhd.Size -lt $script:MinimumVhdBytes) {
                        $reasons.Add('NO_USABLE_SOURCE_IMAGE')
                    }
                    else {
                        $imageCheck = Test-HyperVWindowsImage -Path $sourcePath
                        if (-not $imageCheck.Verified) { $reasons.Add($imageCheck.ReasonCode) }
                    }
                } catch {
                    $reasons.Add('NO_USABLE_SOURCE_IMAGE')
                }
            } else {
                $reasons.Add('PROVIDER_DEPENDENCY_UNAVAILABLE')
            }
        }
    }
    try {
        $memory = Get-CimInstance -ClassName Win32_ComputerSystem -ErrorAction Stop
        if ([int64]$memory.TotalPhysicalMemory -lt 8GB) { $reasons.Add('INSUFFICIENT_HOST_RESOURCES') }
    } catch {
        $information.Add('INSUFFICIENT_HOST_RESOURCES')
    }
    $reasons = @($reasons | Select-Object -Unique)
    $information = @($information | Select-Object -Unique)
    [pscustomobject]@{
        Provisionable = ($reasons.Count -eq 0)
        Ready = ($reasons.Count -eq 0)
        Reasons = $reasons
        ReasonCodes = $reasons
        InformationCodes = $information
        SourcePath = $sourcePath
        VmName = $vmName
        SourceVerified = ($null -ne $sourcePath -and $reasons.Count -eq 0)
    }
}

function Invoke-HyperVProviderPreflight {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [pscustomobject] $ProviderContext)

    $readiness = Test-HyperVGuestProvisionable -ProviderContext $ProviderContext
    [pscustomobject]@{
        Ready = $readiness.Ready
        Provider = 'hyperv'
        ReasonCodes = @($readiness.ReasonCodes)
        InformationCodes = @($readiness.InformationCodes)
        Detail = if ($readiness.Ready) { 'Hyper-V and a user-supplied source are available for disposable provisioning.' } else { "Hyper-V preflight is unavailable: $($readiness.ReasonCodes -join ', ')." }
        SourcePath = $readiness.SourcePath
        VmName = $readiness.VmName
        SourceVerified = $readiness.SourceVerified
    }
}

function New-HyperVProviderState {
    param([Parameter(Mandatory)] [pscustomobject] $ProviderContext)
    [pscustomobject]@{
        VmName = Get-HyperVVmName -ProviderContext $ProviderContext
        SourcePath = Get-HyperVSourcePath -ProviderContext $ProviderContext
        Provisioned = $false
        OwnedByRun = $false
        ProvisionedAtUtc = $null
        RestoreCompleted = $false
        CleanupCompleted = $false
        Diagnostics = [System.Collections.Generic.List[string]]::new()
    }
}

function Invoke-HyperVProviderProvision {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [pscustomobject] $ProviderContext)

    $readiness = Test-HyperVGuestProvisionable -ProviderContext $ProviderContext
    $state = New-HyperVProviderState -ProviderContext $ProviderContext
    $ProviderContext.State = $state

    if (-not $readiness.Provisionable) {
        $state.Diagnostics.Add("provision-unavailable: $($readiness.Reasons -join ', ')")
        return [pscustomobject]@{
            Ready = $false
            Reason = 'REQUIRED-BUT-UNAVAILABLE'
            ReasonCodes = @($readiness.ReasonCodes)
            Detail = "Hyper-V guest provisioning is not possible in this environment: $($readiness.Reasons -join ', '). A clean Windows VHDX/install media is required."
            VhdxPath = $readiness.SourcePath
            VmName = $readiness.VmName
        }
    }

    Assert-HyperVModuleAvailable
    $existing = Get-VM -Name $state.VmName -ErrorAction SilentlyContinue
    if ($existing) {
        $state.Provisioned = $true
        $state.ProvisionedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
        return [pscustomobject]@{ Ready = $true; Reused = $true; VmName = $state.VmName; Detail = "Existing VM '$($state.VmName)' reused; it remains outside this run's ownership." }
    }

    $switch = Get-VMSwitch -Name $script:DefaultSwitch -ErrorAction SilentlyContinue
    $switchParam = @()
    if ($switch) { $switchParam = @('-SwitchName', $script:DefaultSwitch) }

    New-VM -Name $state.VmName -VHDPath $state.SourcePath -MemoryStartupBytes 4GB -Generation 2 @switchParam -ErrorAction Stop | Out-Null
    Enable-VMIntegrationService -VMName $state.VmName -Name 'Guest Service Interface' -ErrorAction SilentlyContinue | Out-Null
    $state.Provisioned = $true
    $state.OwnedByRun = $true
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
    if ($ProviderContext.State.Provisioned -and $ProviderContext.State.OwnedByRun) {
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
        mutatedSystemState = ($ProviderContext.State.Provisioned -and $ProviderContext.State.OwnedByRun)
        actions = $entries
        succeeded = (@($entries | Where-Object { $_.status -eq 'FAIL' }).Count -eq 0)
    }
    $ProviderContext.State.CleanupCompleted = $true
    return $result
}

function Invoke-HyperVProviderBootstrapPlan {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [pscustomobject] $ProviderContext)

    $sourcePath = Get-HyperVSourcePath -ProviderContext $ProviderContext
    [pscustomobject]@{
        provider = 'hyperv'
        generated = $true
        executed = $false
        sourcePath = if ($sourcePath) { '<user-supplied-source>' } else { $null }
        requiredInputs = @('FASTFILES_HYPERV_IMAGE or -ProviderSource', 'guest administrator credential injected at execution time', 'authorized clean reference checkpoint')
        artifacts = @('unattend.xml', 'bootstrap.ps1')
        secretInputs = @('guestCredential', 'winrmCertificate')
        detail = 'Plan only. No ISO/VHDX was mounted, no guest was started, and no credentials were materialized.'
    }
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

Export-ModuleMember -Function Invoke-HyperVProviderPreflight, Invoke-HyperVProviderProvision, Invoke-HyperVProviderActivate, Invoke-HyperVProviderCollectLogs, Invoke-HyperVProviderCleanup, Invoke-HyperVProviderSnapshotRestore, Invoke-HyperVProviderBootstrapPlan, Test-HyperVGuestProvisionable
