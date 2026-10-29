<#
    zero-touch-autonomous-engineering (uia-driver): error contract classes and
    shared element helpers. Classes are guarded so module re-import in one session
    does not throw "type already exists".
#>

if (-not ('UiaOperationTimeoutException' -as [type])) {
    class UiaOperationTimeoutException : System.Exception {
        [hashtable]$Criteria
        [object]$SearchedFrom
        [string]$View
        [int]$TimeoutMs
        UiaOperationTimeoutException([string]$message, [hashtable]$criteria, [object]$searchedFrom, [string]$view, [int]$timeoutMs)
            : base($message) {
            $this.Criteria = $criteria
            $this.SearchedFrom = $searchedFrom
            $this.View = $view
            $this.TimeoutMs = $timeoutMs
        }
    }
}

if (-not ('UiaPatternNotSupportedException' -as [type])) {
    class UiaPatternNotSupportedException : System.Exception {
        [string]$PatternName
        UiaPatternNotSupportedException([string]$message, [string]$patternName) : base($message) {
            $this.PatternName = $patternName
        }
    }
}

if (-not ('UiaDriverUnavailableException' -as [type])) {
    class UiaDriverUnavailableException : System.Exception {
        [string]$Reason
        UiaDriverUnavailableException([string]$message, [string]$reason) : base($message) {
            $this.Reason = $reason
        }
    }
}

function Get-UiaStandardProxyIdentity {
    # repair-native-ui-verification-fidelity 1.1: metadata-only policy, also
    # exercised by portable tests without loading any Windows UIA assembly.
    param([System.Reflection.AssemblyName]$InstalledIdentity, [string[]]$ExportedTypeNames)
    $standardName = 'UIAutomationClientSideProviders'
    if ($null -eq $InstalledIdentity -or
        -not [string]::Equals($InstalledIdentity.Name, $standardName, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Only the installed standard UIAutomationClientSideProviders assembly is supported.'
    }
    $entries = @($ExportedTypeNames | Where-Object {
        $_ -cmatch '^([^.]+)\.UIAutomationClientSideProviders$' -and
        [string]::Equals($Matches[1], $standardName, [StringComparison]::OrdinalIgnoreCase)
    })
    if ($entries.Count -ne 1) { throw 'The installed standard provider must export exactly one standard entry type.' }
    $requestedIdentity = [System.Reflection.AssemblyName]$InstalledIdentity.Clone()
    $requestedIdentity.Name = $entries[0].Substring(0, $entries[0].LastIndexOf('.'))
    return $requestedIdentity
}

function Initialize-UiaStandardProxy {
    param([System.Reflection.Assembly]$ClientAssembly)
    # The AppDomain marker survives Import-Module -Force. Its installed-client
    # MVID key and provider MVID bind the record to this runtime, not a module
    # import or arbitrary global configuration. Failures are deliberately cached.
    $key = 'FastFiles.UiaStandardProxy.' + $ClientAssembly.ManifestModule.ModuleVersionId.ToString('D')
    $domain = [AppDomain]::CurrentDomain
    $prior = $domain.GetData($key)
    if ($null -ne $prior) {
        if ($prior.Status -eq 'Ready') { return }
        throw [UiaDriverUnavailableException]::new($prior.Error, 'standard-uia-proxy-unavailable')
    }
    $state = @{ Status = 'Initializing'; Error = 'Standard UIA proxy initialization is already in progress.' }
    $domain.SetData($key, $state)
    try {
        $providerPath = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetDirectoryName($ClientAssembly.Location)) 'UIAutomationClientSideProviders.dll'))
        $installedIdentity = [Reflection.AssemblyName]::GetAssemblyName($providerPath)
        if (-not [string]::Equals($installedIdentity.Name, 'UIAutomationClientSideProviders', [StringComparison]::OrdinalIgnoreCase)) {
            throw 'The adjacent assembly is not the installed standard UIA provider.'
        }
        $provider = [Reflection.Assembly]::LoadFrom($providerPath)
        if (-not [string]::Equals([IO.Path]::GetFullPath($provider.Location), $providerPath, [StringComparison]::OrdinalIgnoreCase) -or
            -not [string]::Equals($provider.FullName, $installedIdentity.FullName, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'The loaded standard UIA provider does not match the installed client directory and identity.'
        }
        $requestedIdentity = Get-UiaStandardProxyIdentity -InstalledIdentity $installedIdentity -ExportedTypeNames @($provider.GetExportedTypes() | ForEach-Object { $_.FullName })
        $providerMvid = $provider.ManifestModule.ModuleVersionId
        # The public ClientSettings surface has no registration-table getter.
        # Register the installed standard provider once for this client module;
        # the AppDomain marker prevents duplication by subsequent driver imports.
        [System.Windows.Automation.ClientSettings]::RegisterClientSideProviderAssembly($requestedIdentity)
        $state.ProviderMvid = $providerMvid.ToString('D')
        $state.ProviderPath = $providerPath
        $state.RequestedIdentity = $requestedIdentity.FullName
        $state.RegisteredByBootstrap = $true
        $state.Error = $null
        $state.Status = 'Ready'
    } catch {
        $state.Error = 'Installed standard UIA proxy initialization failed: ' + $_.Exception.Message
        $state.Status = 'Failed'
        throw [UiaDriverUnavailableException]::new($state.Error, 'standard-uia-proxy-unavailable')
    }
}

function Initialize-UiaManagedApi {
    if ($script:UiaManagedApiLoaded) { return }
    Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
    # Initialize the public UIA runtime before registering proxies. Registering
    # on a completely cold client otherwise hits uninitialized runtime state
    # on some installed versions; no standard-control semantics are used here.
    $null = [System.Windows.Automation.AutomationElement]::RootElement
    Initialize-UiaStandardProxy -ClientAssembly ([System.Windows.Automation.AutomationElement].Assembly)
    $script:UiaManagedApiLoaded = $true
}

function Test-UiaMockElement {
    param($Element)
    return ($null -ne $Element -and $Element.PSObject.Properties['_isMock'] -and $Element._isMock -eq $true)
}

function Get-UiaMockRecord {
    param($Driver, $Element)
    if (-not (Test-UiaMockElement $Element)) { return $null }
    return $Driver.MockNodes[$Element._id]
}

function Format-UiaControlType {
    param($ControlType)
    if ($null -eq $ControlType) { return $null }
    $name = $ControlType.ToString()
    if ($name.StartsWith('ControlType.')) { $name = $name.Substring('ControlType.'.Length) }
    return $name
}

function Get-UiaElementIdentity {
    param($Driver, $Element)
    if (Test-UiaMockElement $Element) {
        $rec = Get-UiaMockRecord -Driver $Driver -Element $Element
        return [pscustomobject]@{
            Name = $rec.name
            AutomationId = $rec.automationId
            ControlType = $rec.controlType
            ClassName = $rec.className
            ProcessId = $rec.processId
        }
    }
    Initialize-UiaManagedApi
    try {
        $ct = $null
        if ($null -ne $Element.Current.ControlType) { $ct = Format-UiaControlType $Element.Current.ControlType.ProgrammaticName }
        return [pscustomobject]@{
            Name = $Element.Current.Name
            AutomationId = $Element.Current.AutomationId
            ControlType = $ct
            ClassName = $Element.Current.ClassName
            ProcessId = $Element.Current.ProcessId
        }
    } catch {
        return [pscustomobject]@{ Name = $null; AutomationId = $null; ControlType = $null; ClassName = $null; ProcessId = $null }
    }
}

function Format-UiaElementIdentity {
    param($Driver, $Element)
    $id = Get-UiaElementIdentity -Driver $Driver -Element $Element
    $parts = @()
    if ($id.Name) { $parts += "`"$($id.Name)`"" }
    if ($id.AutomationId) { $parts += "AutomationId=$($id.AutomationId)" }
    if ($id.ControlType) { $parts += $id.ControlType }
    if ($id.ClassName) { $parts += "ClassName=$($id.ClassName)" }
    if ($id.ProcessId) { $parts += "pid=$($id.ProcessId)" }
    return $parts -join ' '
}

function Get-UiaDriverSetting {
    param($Driver, [string]$Name, $Fallback)
    if ($Driver.PSObject.Properties[$Name]) { return $Driver.$Name }
    return $Fallback
}

function Resolve-UiaView {
    param($Driver, [string]$View)
    if ($View) { return $View }
    return Get-UiaDriverSetting -Driver $Driver -Name 'View' -Fallback 'Control'
}

function Resolve-UiaTimeout {
    param($Driver, $TimeoutMs)
    if ($null -ne $TimeoutMs -and $TimeoutMs -gt 0) { return $TimeoutMs }
    return Get-UiaDriverSetting -Driver $Driver -Name 'TimeoutMs' -Fallback 5000
}
