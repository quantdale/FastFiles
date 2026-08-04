<#
    Environment Provider contract (task 2.1 / D6).

    A provider owns the verification execution target. Its versioned interface is:
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
$script:RequiredLifecycleEntryPoints = @('provision', 'activate', 'collectLogs', 'cleanup')

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
        [string] $TargetIdentity = "$($env:COMPUTERNAME):$Id"
    )

    [pscustomobject]@{
        Id                      = $Id
        InterfaceVersion        = $InterfaceVersion
        DisplayName             = $DisplayName
        TargetIdentity          = $TargetIdentity
        SupportsSnapshotRestore = $SupportsSnapshotRestore
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
            -EntryPoints @{
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
            -EntryPoints @{
                provision       = $manifest.entryPoints.provision
                activate        = $manifest.entryPoints.activate
                collectLogs     = $manifest.entryPoints.collectLogs
                cleanup         = $manifest.entryPoints.cleanup
                snapshotRestore = $manifest.entryPoints.snapshotRestore
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
        [Parameter(Mandatory)] [ValidateSet('provision', 'activate', 'collectLogs', 'cleanup', 'snapshotRestore')] [string] $Phase,
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

function Get-EnvironmentProviderManifestRecord {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [pscustomobject] $Provider)

    [pscustomobject]@{
        id                      = $Provider.Id
        interfaceVersion        = $Provider.InterfaceVersion
        displayName             = $Provider.DisplayName
        targetIdentity          = $Provider.TargetIdentity
        supportsSnapshotRestore = $Provider.SupportsSnapshotRestore
    }
}

Export-ModuleMember -Function Get-EnvironmentProvider, Get-EnvironmentProviderManifestRecord, Invoke-EnvironmentProviderLifecycle, New-EnvironmentProviderDescriptor, Test-EnvironmentProviderDescriptor, Test-EnvironmentProviderInterfaceVersionSupported
