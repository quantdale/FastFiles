<#
    Environment Provider contract (task 2.1 / D6).

    A provider owns the verification execution target. Its versioned interface is:
      preflight(context) -> readiness and stable reason codes
      provision(context) -> provider state
      activate(context, state) -> active target metadata
      collect-logs(context, state) -> artifact declarations
      cleanup(context, state) -> idempotently restores/tears down the target
      snapshot-restore(context, state) -> optional; required only when declared
        supportsSnapshotRestore=true

    The core deliberately does not perform provider-specific work. Provider adapters
    added in task 2.3 supply these entry points. The local adapter is implemented in
    task 2.2; until then, its descriptor records the current host as the target but
    no lifecycle action is invoked by this module.
#>

$script:SupportedEnvironmentProviderInterfaceMajors = @(1)
$script:RequiredLifecycleEntryPoints = @('preflight', 'provision', 'activate', 'collectLogs', 'cleanup')
$script:EnvironmentProviderReasonCodes = @(
    'HYPERV_FEATURE_UNAVAILABLE',
    'NO_USABLE_SOURCE_IMAGE',
    'SOURCE_IMAGE_PATH_INVALID',
    'VHDX_NOT_WINDOWS_INSTALLATION',
    'VM_ABSENT',
    'UNSUPPORTED_VM_GENERATION',
    'NO_CLEAN_REFERENCE_CHECKPOINT',
    'GUEST_NOT_BOOTABLE',
    'WINRM_UNAVAILABLE',
    'CREDENTIALS_ABSENT',
    'GUEST_TOOLCHAIN_UNAVAILABLE',
    'INSUFFICIENT_HOST_RESOURCES',
    'SNAPSHOT_RESTORE_UNSUPPORTED',
    'LOG_COLLECTION_UNAVAILABLE',
    'CLEANUP_INCOMPLETE',
    'PROVIDER_DESCRIPTOR_INVALID',
    'PROVIDER_INTERFACE_UNSUPPORTED',
    'PROVIDER_DEPENDENCY_UNAVAILABLE'
)

Import-Module (Join-Path $PSScriptRoot 'Schema.psm1') -Force -Global

function Test-EnvironmentProviderInterfaceVersionSupported {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [string] $InterfaceVersion)

    if ($InterfaceVersion -notmatch '^(\d+)\.\d+\.\d+$') { return $false }
    return $script:SupportedEnvironmentProviderInterfaceMajors -contains [int] $Matches[1]
}

function New-EnvironmentProviderDescriptor {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $Id,
        [Parameter(Mandatory)] [string] $InterfaceVersion,
        [Parameter(Mandatory)] [hashtable] $EntryPoints,
        [bool] $SupportsSnapshotRestore = $false,
        [string] $DisplayName = $Id,
        [string] $TargetIdentity = "$($env:COMPUTERNAME):$Id",
        [string[]] $SourceKinds = @(),
        [string[]] $ReasonCodes = @($script:EnvironmentProviderReasonCodes)
    )

    [pscustomobject]@{
        Id                      = $Id
        InterfaceVersion        = $InterfaceVersion
        DisplayName             = $DisplayName
        TargetIdentity          = $TargetIdentity
        SupportsSnapshotRestore = $SupportsSnapshotRestore
        SourceKinds             = @($SourceKinds)
        ReasonCodes             = @($ReasonCodes)
        EntryPoints             = [pscustomobject] $EntryPoints
    }
}

function Test-EnvironmentProviderDescriptor {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [pscustomobject] $Provider)

    $errors = [System.Collections.Generic.List[string]]::new()
    $schemaPath = Join-Path (Split-Path $PSScriptRoot -Parent) 'schemas\environment-provider.schema.json'
    $schema = Get-Content $schemaPath -Raw | ConvertFrom-Json
    $schemaValidation = Test-JsonSchema -Data $Provider -Schema $schema
    foreach ($error in $schemaValidation.Errors) { $errors.Add($error) }
    if (-not $Provider.Id -or $Provider.Id -notmatch '^[a-z][a-z0-9-]*$') {
        $errors.Add("provider id '$($Provider.Id)' is invalid")
    }
    if (-not (Test-EnvironmentProviderInterfaceVersionSupported -InterfaceVersion $Provider.InterfaceVersion)) {
        $errors.Add("provider '$($Provider.Id)' has unsupported interfaceVersion '$($Provider.InterfaceVersion)'")
    }
    foreach ($name in $script:RequiredLifecycleEntryPoints) {
        if (-not $Provider.EntryPoints.$name) {
            $errors.Add("provider '$($Provider.Id)' is missing required lifecycle entry point '$name'")
        }
    }
    foreach ($reasonCode in @($Provider.ReasonCodes)) {
        if ($script:EnvironmentProviderReasonCodes -notcontains $reasonCode) {
            $errors.Add("provider '$($Provider.Id)' declares unknown reason code '$reasonCode'")
        }
    }
    if ($Provider.SupportsSnapshotRestore -and -not $Provider.EntryPoints.snapshotRestore) {
        $errors.Add("snapshot-restorable provider '$($Provider.Id)' is missing entry point 'snapshotRestore'")
    }

    [pscustomobject]@{ Valid = ($errors.Count -eq 0); Errors = $errors }
}

function Get-EnvironmentProvider {
    <#
        Resolves the active provider selection from verify/providers/<id>/. The
        built-in 'local' provider is a well-known fallback. Other providers (e.g.
        'hyperv') are loaded from their provider.json manifest + entry module, so
        adding a disposable target is dropping in a conforming provider directory —
        the core never special-cases a provider id beyond 'local'.
    #>
    [CmdletBinding()]
    param([string] $ProviderId = 'local')

    $providersRoot = Join-Path (Split-Path $PSScriptRoot -Parent) 'providers'

    if ($ProviderId -eq 'local') {
        $modulePath = Join-Path $providersRoot 'local\Local.psm1'
        Import-Module $modulePath -Force -Global -ErrorAction Stop
        $provider = New-EnvironmentProviderDescriptor -Id 'local' -InterfaceVersion '1.0.0' `
            -DisplayName 'Local Windows host' -TargetIdentity "$($env:COMPUTERNAME):local" `
            -SourceKinds @('current-host') `
            -EntryPoints @{
                preflight       = 'Invoke-LocalProviderPreflight'
                provision       = 'Invoke-LocalProviderProvision'
                activate        = 'Invoke-LocalProviderActivate'
                collectLogs     = 'Invoke-LocalProviderCollectLogs'
                cleanup         = 'Invoke-LocalProviderCleanup'
            }
    } else {
        $manifestPath = Join-Path $providersRoot "$ProviderId\provider.json"
        if (-not (Test-Path -LiteralPath $manifestPath)) {
            throw "Environment provider '$ProviderId' is not registered. Available providers: local, hyperv."
        }
        $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        $moduleName = [IO.Path]::GetFileNameWithoutExtension($manifest.entryModule)
        $modulePath = Join-Path $providersRoot "$ProviderId\$($manifest.entryModule)"
        if (-not (Test-Path -LiteralPath $modulePath)) {
            throw "Environment provider '$ProviderId' declares entry module '$($manifest.entryModule)' which does not exist."
        }
        Import-Module $modulePath -Force -Global -ErrorAction Stop
        $provider = New-EnvironmentProviderDescriptor -Id $manifest.id -InterfaceVersion $manifest.interfaceVersion `
            -DisplayName $manifest.displayName -TargetIdentity "$($env:COMPUTERNAME):$($manifest.id)" `
            -SupportsSnapshotRestore ([bool]$manifest.supportsSnapshotRestore) `
            -SourceKinds @($manifest.sourceKinds) `
            -ReasonCodes @($manifest.reasonCodes) `
            -EntryPoints @{
                preflight       = $manifest.entryPoints.preflight
                provision       = $manifest.entryPoints.provision
                activate        = $manifest.entryPoints.activate
                collectLogs     = $manifest.entryPoints.collectLogs
                cleanup         = $manifest.entryPoints.cleanup
                snapshotRestore = $manifest.entryPoints.snapshotRestore
                bootstrapPlan   = $manifest.entryPoints.bootstrapPlan
            }
    }

    $validation = Test-EnvironmentProviderDescriptor -Provider $provider
    if (-not $validation.Valid) {
        throw "Environment provider '$ProviderId' is invalid: $($validation.Errors -join '; ')"
    }
    return $provider
}

function Invoke-EnvironmentProviderLifecycle {
    <# Invokes a provider-declared lifecycle entry point without provider-specific branching. #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [pscustomobject] $Provider,
        [Parameter(Mandatory)] [ValidateSet('preflight', 'provision', 'activate', 'collectLogs', 'cleanup', 'snapshotRestore', 'bootstrapPlan')] [string] $Phase,
        [Parameter(Mandatory)] [pscustomobject] $ProviderContext
    )

    $entryPoint = $Provider.EntryPoints.$Phase
    if (-not $entryPoint) {
        if ($Phase -eq 'snapshotRestore' -and -not $Provider.SupportsSnapshotRestore) {
            return $null
        }
        throw "Environment provider '$($Provider.Id)' does not declare lifecycle entry point '$Phase'."
    }
    if (-not (Get-Command $entryPoint -ErrorAction SilentlyContinue)) {
        throw "Environment provider '$($Provider.Id)' entry point '$Phase' -> '$entryPoint' is unavailable."
    }
    return & $entryPoint $ProviderContext
}

function Invoke-EnvironmentProviderContract {
    <#
        Runs the provider lifecycle with cleanup guaranteed after every attempted
        provision. The executor is deliberately a scriptblock supplied by the
        caller; providers never receive arbitrary product commands or file paths.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [pscustomobject] $Provider,
        [Parameter(Mandatory)] [pscustomobject] $ProviderContext,
        [scriptblock] $Execute,
        [switch] $RestoreSnapshot
    )

    $phases = [System.Collections.Generic.List[object]]::new()
    $provisionAttempted = $false
    $resultStatus = 'PASS'
    $resultReason = $null
    $reasonCodes = [System.Collections.Generic.List[string]]::new()
    $cleanup = $null

    try {
        $preflight = Invoke-EnvironmentProviderLifecycle -Provider $Provider -Phase preflight -ProviderContext $ProviderContext
        $preflightStatus = if ($preflight.Ready) { 'PASS' } else { 'REQUIRED-BUT-UNAVAILABLE' }
        $phases.Add([pscustomobject]@{ phase = 'preflight'; status = $preflightStatus; result = $preflight })
        if (-not $preflight.Ready) {
            $resultStatus = 'REQUIRED-BUT-UNAVAILABLE'
            $resultReason = $preflight.Detail
            foreach ($code in @($preflight.ReasonCodes)) { if ($code) { $reasonCodes.Add([string]$code) } }
        }
    } catch {
        $resultStatus = 'FAIL'
        $resultReason = $_.Exception.Message
        $phases.Add([pscustomobject]@{ phase = 'preflight'; status = 'FAIL'; result = $null; error = $resultReason })
    }

    if ($resultStatus -eq 'PASS') {
        $provisionAttempted = $true
        try {
            $provision = Invoke-EnvironmentProviderLifecycle -Provider $Provider -Phase provision -ProviderContext $ProviderContext
            $provisionStatus = if ($provision.Ready) { 'PASS' } else { 'REQUIRED-BUT-UNAVAILABLE' }
            $phases.Add([pscustomobject]@{ phase = 'provision'; status = $provisionStatus; result = $provision })
            if (-not $provision.Ready) {
                $resultStatus = 'REQUIRED-BUT-UNAVAILABLE'
                $resultReason = $provision.Detail
                foreach ($code in @($provision.ReasonCodes)) { if ($code) { $reasonCodes.Add([string]$code) } }
            }
        } catch {
            $resultStatus = 'FAIL'
            $resultReason = $_.Exception.Message
            $phases.Add([pscustomobject]@{ phase = 'provision'; status = 'FAIL'; result = $null; error = $resultReason })
        }
    }

    if ($resultStatus -eq 'PASS') {
        try {
            $activate = Invoke-EnvironmentProviderLifecycle -Provider $Provider -Phase activate -ProviderContext $ProviderContext
            $activateStatus = if ($activate.Ready -eq $false) { 'FAIL' } else { 'PASS' }
            $phases.Add([pscustomobject]@{ phase = 'activate'; status = $activateStatus; result = $activate })
            if ($activate.Ready -eq $false) {
                $resultStatus = 'FAIL'
                $resultReason = $activate.Detail
            }
        } catch {
            $resultStatus = 'FAIL'
            $resultReason = $_.Exception.Message
            $phases.Add([pscustomobject]@{ phase = 'activate'; status = 'FAIL'; result = $null; error = $resultReason })
        }
    }

    if ($resultStatus -eq 'PASS' -and $Execute) {
        try {
            $execution = & $Execute $ProviderContext
            $phases.Add([pscustomobject]@{ phase = 'execute'; status = 'PASS'; result = $execution })
        } catch {
            $resultStatus = if ($ProviderContext.Cancelled) { 'SKIPPED' } else { 'FAIL' }
            $resultReason = $_.Exception.Message
            $phases.Add([pscustomobject]@{ phase = 'execute'; status = $resultStatus; result = $null; error = $resultReason })
        }
    }

    if ($provisionAttempted -and $ProviderContext.State) {
        try {
            try {
                $logs = Invoke-EnvironmentProviderLifecycle -Provider $Provider -Phase collectLogs -ProviderContext $ProviderContext
                $phases.Add([pscustomobject]@{ phase = 'collectLogs'; status = 'PASS'; result = $logs })
            } catch {
                $resultStatus = 'FAIL'
                $resultReason = $_.Exception.Message
                $phases.Add([pscustomobject]@{ phase = 'collectLogs'; status = 'FAIL'; result = $null; error = $resultReason })
            }
            if ($RestoreSnapshot) {
                try {
                    $restored = Invoke-EnvironmentProviderLifecycle -Provider $Provider -Phase snapshotRestore -ProviderContext $ProviderContext
                    $restoreStatus = if ($restored.Restored) { 'PASS' } else { 'REQUIRED-BUT-UNAVAILABLE' }
                    $phases.Add([pscustomobject]@{ phase = 'snapshotRestore'; status = $restoreStatus; result = $restored })
                    if (-not $restored.Restored -and $resultStatus -eq 'PASS') {
                        $resultStatus = 'REQUIRED-BUT-UNAVAILABLE'
                        $resultReason = $restored.Detail
                    }
                } catch {
                    $resultStatus = 'FAIL'
                    $resultReason = $_.Exception.Message
                    $phases.Add([pscustomobject]@{ phase = 'snapshotRestore'; status = 'FAIL'; result = $null; error = $resultReason })
                }
            }
        } finally {
            try {
                $cleanup = Invoke-EnvironmentProviderLifecycle -Provider $Provider -Phase cleanup -ProviderContext $ProviderContext
                $cleanupStatus = if ($cleanup.succeeded -eq $false) { 'FAIL' } else { 'PASS' }
                $phases.Add([pscustomobject]@{ phase = 'cleanup'; status = $cleanupStatus; result = $cleanup })
                if ($cleanup.succeeded -eq $false) {
                    $resultStatus = 'FAIL'
                    $resultReason = 'Provider cleanup reported CLEANUP_INCOMPLETE.'
                    $reasonCodes.Add('CLEANUP_INCOMPLETE')
                }
            } catch {
                $resultStatus = 'FAIL'
                $resultReason = $_.Exception.Message
                $reasonCodes.Add('CLEANUP_INCOMPLETE')
                $phases.Add([pscustomobject]@{ phase = 'cleanup'; status = 'FAIL'; result = $null; error = $resultReason })
            }
        }
    }
    [pscustomobject]@{ status = $resultStatus; reasonCodes = @($reasonCodes | Select-Object -Unique); reason = $resultReason; phases = @($phases); cleanup = $cleanup }
}

function Get-EnvironmentProviderManifestRecord {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [pscustomobject] $Provider)

    [pscustomobject]@{
        id                      = $Provider.Id
        interfaceVersion        = $Provider.InterfaceVersion
        displayName             = $Provider.DisplayName
        targetIdentity          = $Provider.TargetIdentity
        supportsSnapshotRestore = $Provider.SupportsSnapshotRestore
        sourceKinds             = @($Provider.SourceKinds)
        reasonCodes             = @($Provider.ReasonCodes)
    }
}

Export-ModuleMember -Function Get-EnvironmentProvider, Get-EnvironmentProviderManifestRecord, Invoke-EnvironmentProviderLifecycle, Invoke-EnvironmentProviderContract, New-EnvironmentProviderDescriptor, Test-EnvironmentProviderDescriptor, Test-EnvironmentProviderInterfaceVersionSupported
