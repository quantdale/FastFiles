<# Metadata and cache policy only: no UIA assemblies, HWNDs or desktop required. #>
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
Import-Module (Join-Path $repo 'verify/uia-driver/UiaDriver.psm1') -Force
$module = Get-Module UiaDriver
$uiaAssembliesBefore = @([AppDomain]::CurrentDomain.GetAssemblies() | Where-Object { $_.GetName().Name -like 'UIAutomation*' } | ForEach-Object { $_.FullName })
$checks = 0
function Assert([bool]$Condition, [string]$Description) {
    if (-not $Condition) { throw "FAIL: $Description" }
    $script:checks++
}
function Select-Identity($identity, $types) {
    & $module { param($i, $t) Get-UiaStandardProxyIdentity -InstalledIdentity $i -ExportedTypeNames $t } $identity $types
}
function Assert-Rejected($identity, $types, [string]$description) {
    $rejected = $false
    try { $null = Select-Identity $identity $types } catch { $rejected = $true }
    Assert $rejected $description
}

$identity = [Reflection.AssemblyName]::new('UIAutomationClientSideProviders, Version=10.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089')
$before = $identity.FullName
$selected = Select-Identity $identity @('Other.Type', 'UIAutomationClientsideProviders.UIAutomationClientSideProviders')
Assert ($selected.Name -ceq 'UIAutomationClientsideProviders') 'entry namespace determines exact requested name casing'
Assert ($identity.FullName -ceq $before) 'selection does not mutate installed AssemblyName'
Assert (-not [object]::ReferenceEquals($identity, $selected)) 'selection returns a cloned identity'
Assert ($selected.Version -eq $identity.Version -and $selected.CultureName -ceq $identity.CultureName) 'version and culture preserved'
Assert ([Convert]::ToHexString($selected.GetPublicKeyToken()) -ceq [Convert]::ToHexString($identity.GetPublicKeyToken())) 'installed token preserved'
$exact = Select-Identity $identity @('UIAutomationClientSideProviders.UIAutomationClientSideProviders')
Assert ($exact.FullName -ceq $identity.FullName) 'matching spelling remains unchanged'
Assert-Rejected ([Reflection.AssemblyName]::new('Unrelated, Version=10.0.0.0')) @('UIAutomationClientsideProviders.UIAutomationClientSideProviders') 'unrelated assembly rejected'
Assert-Rejected $identity @('Unrelated.UIAutomationClientSideProviders') 'unrelated namespace rejected'
Assert-Rejected $identity @('UIAutomationClientsideProviders.uiAutomationClientSideProviders') 'entry class case mismatch rejected'
Assert-Rejected $identity @('UIAutomationClientsideProviders.UIAutomationClientSideProviders', 'UIAutomationClientSideProviders.UIAutomationClientSideProviders') 'ambiguous entry metadata rejected'
Assert-Rejected $identity @() 'missing entry metadata rejected'
Assert-Rejected $null @('UIAutomationClientsideProviders.UIAutomationClientSideProviders') 'missing assembly metadata rejected'

# An in-memory assembly has no installed directory. This exercises the real
# failure path deterministically on every runtime without loading Windows UIA
# assemblies or modifying any files (a framework assembly may share PSHOME).
$client = [Reflection.Emit.AssemblyBuilder]::DefineDynamicAssembly([Reflection.AssemblyName]::new('FastFilesPortableProxyCacheTest'), [Reflection.Emit.AssemblyBuilderAccess]::Run)
$key = 'FastFiles.UiaStandardProxy.' + $client.ManifestModule.ModuleVersionId.ToString('D')
$domain = [AppDomain]::CurrentDomain
$original = $domain.GetData($key)
try {
    $domain.SetData($key, $null)
    $firstError = ''
    try { & $module { param($a) Initialize-UiaStandardProxy -ClientAssembly $a } $client } catch { $firstError = $_.Exception.Message }
    $failed = $domain.GetData($key)
    Assert ($failed.Status -eq 'Failed' -and $firstError -like '*Installed standard UIA proxy initialization failed*') 'metadata failure is explicit and cached'
    $secondError = ''
    try { & $module { param($a) Initialize-UiaStandardProxy -ClientAssembly $a } $client } catch { $secondError = $_.Exception.Message }
    Assert ($firstError -ceq $secondError -and [object]::ReferenceEquals($failed, $domain.GetData($key))) 'second attempt rethrows same cached failure without reinitialization'
    Assert (-not (& $module { $script:UiaManagedApiLoaded })) 'failure never marks managed API loaded'
    $ready = @{ Status = 'Ready'; ProviderMvid = [guid]::NewGuid().ToString('D') }
    $domain.SetData($key, $ready)
    Import-Module (Join-Path $repo 'verify/uia-driver/UiaDriver.psm1') -Force
    $module = Get-Module UiaDriver
    $output = @(& $module { param($a) Initialize-UiaStandardProxy -ClientAssembly $a } $client)
    Assert ($output.Count -eq 0 -and [object]::ReferenceEquals($ready, $domain.GetData($key))) 'successful AppDomain marker survives reload and initialization emits no output'
    $mock = New-UiaDriver -Provider mock
    Import-UiaRecordedTree -Driver $mock -Path (Join-Path $PSScriptRoot 'recorded-trees/sample-files-window.json') | Out-Null
    $null = Get-UiaRootElement -Driver $mock
    Assert (-not (& $module { $script:UiaManagedApiLoaded })) 'mock tree remains usable without native initialization'
} finally { $domain.SetData($key, $original) }
$uiaAssembliesAfter = @([AppDomain]::CurrentDomain.GetAssemblies() | Where-Object { $_.GetName().Name -like 'UIAutomation*' } | ForEach-Object { $_.FullName })
Assert (($uiaAssembliesBefore -join ';') -ceq ($uiaAssembliesAfter -join ';')) 'portable metadata/cache/mock tests load no UIA assemblies'
Write-Host "PASS: standard proxy metadata/cache policy ($checks checks)"
