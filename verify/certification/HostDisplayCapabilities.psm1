<#
    Host display-capability detection for the FastFiles hardening campaign
    (Phase 6: DPI/display/accessibility certification matrix).

    OBSERVES host facts; never hardcodes them. Every fact carries an
    observability flag: callers map unobservable or absent prerequisites to
    HOST_UNSUPPORTED, never to PASS.

    Injectable for headless tests: pass -Probe with any of the keys
    Monitors, SystemDpi, PerMonitorDpi, MixedDpi, AppsUseLightTheme,
    SystemUsesLightTheme, HighContrastFlags. A PRESENT key replaces live
    detection for that fact (including $null, which forces unobservable).
    An ABSENT key (or no -Probe at all) performs live Windows detection.

    Live sources (all best-effort, failure yields unobservable):
      Monitors        System.Windows.Forms.Screen::AllScreens (device name,
                      bounds, primary flag).
      SystemDpi       user32 GetDpiForSystem, fallback HKCU Desktop LogPixels.
      PerMonitorDpi   HKCU Control Panel\Desktop\PerMonitorSettings subkeys
                      (raw values recorded; value semantics not asserted).
      Theme           HKCU ...\Themes\Personalize AppsUseLightTheme and
                      SystemUsesLightTheme.
      HighContrast    HKCU Control Panel\Accessibility\HighContrast Flags
                      bit 0 (HCF_HIGHCONTRASTON).

    Reuse map into existing visual machinery (consumed by
    verify/certification/Invoke-DisplayMatrix.ps1):
      VisualCapture.psm1 Test-VisualDpiMatch ......... requested-vs-observed
                      DPI gate for dpi-* cells (dpi never claimed, always
                      compared).
      VisualCapture.psm1 Get-VisualGitState .......... headSha/tree provenance
                      stamped into matrix evidence.
      VisualValidation.psm1 Get-VisualScenarioCatalog  per-cell live scenario
                      mapping (shell-home-light/dark, resize rows, and the
                      Block rows dpi-*-live / high-contrast-live /
                      mixed-dpi-transition whose Block text documents why the
                      campaign never mutates those host settings itself).
      Invoke-VisualCatalog.ps1 Set-CatalogWindowSize / Set-CatalogTheme /
                      Get-CatalogMeanBrightness ... window-state and theme
                      cell actuation + pixel-change proof.
      BaselineManifest.psm1 Compare-AgainstBaseline + Compare-VisualBaseline.ps1
                      deterministic per-cell regression comparison.
      UiaDriver.psm1 Get-UiaDriverAvailability ........ -Execute preflight.
#>

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

$script:IsWindowsPlatform = [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform(
    [System.Runtime.InteropServices.OSPlatform]::Windows)

function Get-DisplayCellVocabulary {
    return @('PASS', 'FAIL', 'HOST_UNSUPPORTED', 'NOT_RUN')
}

function Test-DisplayProbeKey {
    param([hashtable] $Probe, [string] $Name)
    if ($null -eq $Probe) { return $false }
    return $Probe.ContainsKey($Name)
}

function Get-DisplayRawProperty {
    param($Raw, [string] $Name, $Default = $null)
    if ($null -eq $Raw) { return $Default }
    if ($Raw -is [System.Collections.IDictionary]) {
        if ($Raw.Contains($Name)) { return $Raw[$Name] }
        return $Default
    }
    $property = $Raw.PSObject.Properties[$Name]
    if ($null -ne $property) { return $property.Value }
    return $Default
}

function ConvertTo-DisplayPercent {
    param([int] $Dpi)
    if ($Dpi -le 0) { return 0 }
    return [int][Math]::Round($Dpi * 100.0 / 96.0)
}

function Get-DisplaySystemDpiLive {
    $result = [pscustomobject]@{
        Dpi        = 0
        Observable = $false
        Reason     = 'unprobed'
        Source     = $null
    }
    if (-not $script:IsWindowsPlatform) {
        $result.Reason = 'non-windows-host'
        return $result
    }
    try {
        if (-not ('FfDisplaySystemDpiNative' -as [type])) {
            Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class FfDisplaySystemDpiNative
{
    [DllImport("user32.dll")]
    public static extern uint GetDpiForSystem();
}
'@
        }
        $dpi = [uint32][FfDisplaySystemDpiNative]::GetDpiForSystem()
        if ($dpi -gt 0) {
            $result.Dpi = [int]$dpi
            $result.Observable = $true
            $result.Reason = $null
            $result.Source = 'GetDpiForSystem'
            return $result
        }
    } catch { }
    try {
        $logPixels = Get-ItemPropertyValue -LiteralPath 'HKCU:\Control Panel\Desktop' -Name 'LogPixels' -ErrorAction Stop
        $dpi = [int]$logPixels
        if ($dpi -gt 0) {
            $result.Dpi = $dpi
            $result.Observable = $true
            $result.Reason = $null
            $result.Source = 'HKCU\Desktop\LogPixels'
            return $result
        }
    } catch { }
    $result.Reason = 'system-dpi-unobservable'
    return $result
}

function Get-DisplayMonitors {
    param([hashtable] $Probe)
    if (Test-DisplayProbeKey -Probe $Probe -Name 'Monitors') {
        $raw = $Probe['Monitors']
        if ($null -eq $raw) {
            return [pscustomobject]@{
                Monitors   = @()
                Observable = $false
                Reason     = 'probe-forced-unobservable'
                Source     = 'probe'
            }
        }
        $items = @(foreach ($entry in @($raw)) { ConvertTo-DisplayMonitor -Raw $entry -Index -1 })
        $index = 0
        foreach ($monitor in $items) {
            if ([string]::IsNullOrWhiteSpace([string]$monitor.Id)) { $monitor.Id = "monitor-$index" }
            $index++
        }
        return [pscustomobject]@{
            Monitors   = @($items)
            Observable = $true
            Reason     = $null
            Source     = 'probe'
        }
    }
    if (-not $script:IsWindowsPlatform) {
        return [pscustomobject]@{
            Monitors   = @()
            Observable = $false
            Reason     = 'non-windows-host'
            Source     = $null
        }
    }
    try {
        Add-Type -AssemblyName System.Windows.Forms -ErrorAction Stop
        $screens = [System.Windows.Forms.Screen]::AllScreens
        $items = @()
        $index = 0
        foreach ($screen in @($screens)) {
            $bounds = $screen.Bounds
            $items += [pscustomobject]@{
                Id         = if ($screen.DeviceName) { [string]$screen.DeviceName } else { "monitor-$index" }
                DeviceName = [string]$screen.DeviceName
                Bounds     = [pscustomobject]@{
                    X      = [int]$bounds.X
                    Y      = [int]$bounds.Y
                    Width  = [int]$bounds.Width
                    Height = [int]$bounds.Height
                }
                Primary    = [bool]$screen.Primary
                Dpi        = $null
            }
            $index++
        }
        if ($items.Count -eq 0) {
            return [pscustomobject]@{
                Monitors   = @()
                Observable = $false
                Reason     = 'no-screens-enumerated'
                Source     = 'System.Windows.Forms.Screen'
            }
        }
        return [pscustomobject]@{
            Monitors   = @($items)
            Observable = $true
            Reason     = $null
            Source     = 'System.Windows.Forms.Screen'
        }
    } catch {
        return [pscustomobject]@{
            Monitors   = @()
            Observable = $false
            Reason     = 'monitor-topology-unobservable'
            Source     = $null
        }
    }
}

function ConvertTo-DisplayMonitor {
    param($Raw, [int] $Index = -1)
    $boundsRaw = Get-DisplayRawProperty -Raw $Raw -Name 'Bounds'
    $bounds = [pscustomobject]@{
        X      = [int](Get-DisplayRawProperty -Raw $boundsRaw -Name 'X' -Default 0)
        Y      = [int](Get-DisplayRawProperty -Raw $boundsRaw -Name 'Y' -Default 0)
        Width  = [int](Get-DisplayRawProperty -Raw $boundsRaw -Name 'Width' -Default 0)
        Height = [int](Get-DisplayRawProperty -Raw $boundsRaw -Name 'Height' -Default 0)
    }
    $name = [string](Get-DisplayRawProperty -Raw $Raw -Name 'DeviceName' -Default '')
    if ([string]::IsNullOrWhiteSpace($name)) { $name = [string](Get-DisplayRawProperty -Raw $Raw -Name 'Id' -Default '') }
    if ([string]::IsNullOrWhiteSpace($name)) { $name = "monitor-$Index" }
    $dpiRaw = Get-DisplayRawProperty -Raw $Raw -Name 'Dpi'
    $dpi = $null
    if ($null -ne $dpiRaw) {
        try { $parsed = [int]$dpiRaw; if ($parsed -gt 0) { $dpi = $parsed } } catch { }
    }
    return [pscustomobject]@{
        Id         = $name
        DeviceName = $name
        Bounds     = $bounds
        Primary    = [bool](Get-DisplayRawProperty -Raw $Raw -Name 'Primary' -Default $false)
        Dpi        = $dpi
    }
}

function Get-DisplayPerMonitorDpi {
    param([hashtable] $Probe, [object[]] $Monitors = @())
    if (Test-DisplayProbeKey -Probe $Probe -Name 'PerMonitorDpi') {
        $raw = $Probe['PerMonitorDpi']
        if ($null -eq $raw) {
            return [pscustomobject]@{
                Values     = @()
                Observable = $false
                Reason     = 'probe-forced-unobservable'
                Source     = 'probe'
            }
        }
        $values = @()
        foreach ($entry in @($raw)) {
            try { $parsed = [int]$entry; if ($parsed -gt 0) { $values += $parsed } } catch { }
        }
        return [pscustomobject]@{
            Values     = @($values)
            Observable = ($values.Count -gt 0)
            Reason     = if ($values.Count -gt 0) { $null } else { 'probe-per-monitor-dpi-empty' }
            Source     = 'probe'
        }
    }
    if (-not $script:IsWindowsPlatform) {
        return [pscustomobject]@{
            Values     = @()
            Observable = $false
            Reason     = 'non-windows-host'
            Source     = $null
        }
    }
    try {
        $key = Get-ChildItem -LiteralPath 'HKCU:\Control Panel\Desktop\PerMonitorSettings' -ErrorAction Stop
        $ids = @($key | ForEach-Object { $_.PSChildName })
        if ($ids.Count -eq 0) {
            return [pscustomobject]@{
                Values     = @()
                Observable = $false
                Reason     = 'per-monitor-settings-empty'
                Source     = 'HKCU\Desktop\PerMonitorSettings'
            }
        }
        $values = @()
        foreach ($id in $ids) {
            $props = Get-ItemProperty -LiteralPath ("HKCU:\Control Panel\Desktop\PerMonitorSettings\$id") -ErrorAction SilentlyContinue
            if ($null -eq $props) { continue }
            foreach ($property in @($props.PSObject.Properties)) {
                if ($property.Name -in @('PSPath', 'PSParentPath', 'PSChildName', 'PSDrive', 'PSProvider')) { continue }
                try {
                    $number = [int64]$property.Value
                    if ($number -gt 0) { $values += [int]$number }
                } catch { }
            }
        }
        return [pscustomobject]@{
            Values     = @($values)
            Observable = ($values.Count -gt 0)
            Reason     = if ($values.Count -gt 0) { $null } else { 'per-monitor-dpi-values-unreadable' }
            Source     = 'HKCU\Desktop\PerMonitorSettings'
            MonitorIds = @($ids)
        }
    } catch {
        return [pscustomobject]@{
            Values     = @()
            Observable = $false
            Reason     = 'per-monitor-settings-unobservable'
            Source     = $null
        }
    }
}

function Get-DisplayThemeState {
    param([hashtable] $Probe)
    $apps = $null
    $system = $null
    $observable = $true
    $reason = $null
    $source = 'probe'
    if ((Test-DisplayProbeKey -Probe $Probe -Name 'AppsUseLightTheme') -or
        (Test-DisplayProbeKey -Probe $Probe -Name 'SystemUsesLightTheme')) {
        if (Test-DisplayProbeKey -Probe $Probe -Name 'AppsUseLightTheme') { $apps = $Probe['AppsUseLightTheme'] }
        if (Test-DisplayProbeKey -Probe $Probe -Name 'SystemUsesLightTheme') { $system = $Probe['SystemUsesLightTheme'] }
        if ($null -eq $apps -and $null -eq $system) {
            $observable = $false
            $reason = 'probe-forced-unobservable'
        }
    } elseif (-not $script:IsWindowsPlatform) {
        $observable = $false
        $reason = 'non-windows-host'
        $source = $null
    } else {
        $source = 'HKCU\Themes\Personalize'
        try { $apps = [int](Get-ItemPropertyValue -LiteralPath 'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Themes\Personalize' -Name 'AppsUseLightTheme' -ErrorAction Stop) } catch { $apps = $null }
        try { $system = [int](Get-ItemPropertyValue -LiteralPath 'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Themes\Personalize' -Name 'SystemUsesLightTheme' -ErrorAction Stop) } catch { $system = $null }
        if ($null -eq $apps -and $null -eq $system) {
            $observable = $false
            $reason = 'theme-values-unobservable'
        }
    }
    $name = 'Unknown'
    if ($observable -and $null -ne $apps) { $name = if ([int]$apps -eq 0) { 'Dark' } else { 'Light' } }
    return [pscustomobject]@{
        ThemeName            = $name
        AppsUseLightTheme    = $apps
        SystemUsesLightTheme = $system
        Observable           = $observable
        Reason               = $reason
        Source               = $source
    }
}

function Get-DisplayHighContrastState {
    param([hashtable] $Probe)
    if (Test-DisplayProbeKey -Probe $Probe -Name 'HighContrastFlags') {
        $flags = $Probe['HighContrastFlags']
        if ($null -eq $flags) {
            return [pscustomobject]@{
                Enabled    = $false
                Flags      = $null
                Observable = $false
                Reason     = 'probe-forced-unobservable'
                Source     = 'probe'
            }
        }
        try { $number = [int]$flags } catch { $number = 0 }
        return [pscustomobject]@{
            Enabled    = (($number -band 1) -ne 0)
            Flags      = $number
            Observable = $true
            Reason     = $null
            Source     = 'probe'
        }
    }
    if (-not $script:IsWindowsPlatform) {
        return [pscustomobject]@{
            Enabled    = $false
            Flags      = $null
            Observable = $false
            Reason     = 'non-windows-host'
            Source     = $null
        }
    }
    try {
        $raw = Get-ItemPropertyValue -LiteralPath 'HKCU:\Control Panel\Accessibility\HighContrast' -Name 'Flags' -ErrorAction Stop
        $number = [int][string]$raw
        return [pscustomobject]@{
            Enabled    = (($number -band 1) -ne 0)
            Flags      = $number
            Observable = $true
            Reason     = $null
            Source     = 'HKCU\Accessibility\HighContrast'
        }
    } catch {
        return [pscustomobject]@{
            Enabled    = $false
            Flags      = $null
            Observable = $false
            Reason     = 'high-contrast-flags-unobservable'
            Source     = $null
        }
    }
}

function Get-HostDisplayFacts {
    param([hashtable] $Probe, [string] $RepoRoot = '')
    $topology = Get-DisplayMonitors -Probe $Probe
    $systemDpi = $null
    if (Test-DisplayProbeKey -Probe $Probe -Name 'SystemDpi') {
        $raw = $Probe['SystemDpi']
        if ($null -eq $raw) {
            $systemDpi = [pscustomobject]@{ Dpi = 0; Observable = $false; Reason = 'probe-forced-unobservable'; Source = 'probe' }
        } else {
            try { $parsed = [int]$raw } catch { $parsed = 0 }
            $systemDpi = [pscustomobject]@{
                Dpi        = $parsed
                Observable = ($parsed -gt 0)
                Reason     = if ($parsed -gt 0) { $null } else { 'probe-system-dpi-invalid' }
                Source     = 'probe'
            }
        }
    } else {
        $systemDpi = Get-DisplaySystemDpiLive
    }
    $perMonitor = Get-DisplayPerMonitorDpi -Probe $Probe -Monitors @($topology.Monitors)
    $theme = Get-DisplayThemeState -Probe $Probe
    $contrast = Get-DisplayHighContrastState -Probe $Probe
    $mixed = $null
    if (Test-DisplayProbeKey -Probe $Probe -Name 'MixedDpi') {
        $mixedRaw = $Probe['MixedDpi']
        $mixed = [pscustomobject]@{
            Supported  = [bool]$mixedRaw
            Observable = ($null -ne $mixedRaw)
            Reason     = if ($null -ne $mixedRaw) { $null } else { 'probe-forced-unobservable' }
        }
    } else {
        $distinct = @($perMonitor.Values | Sort-Object -Unique)
        if ($topology.Observable -and @($topology.Monitors).Count -ge 2 -and $perMonitor.Observable -and $distinct.Count -ge 2) {
            $mixed = [pscustomobject]@{ Supported = $true; Observable = $true; Reason = $null }
        } elseif ($topology.Observable -and @($topology.Monitors).Count -ge 2 -and $perMonitor.Observable) {
            $mixed = [pscustomobject]@{ Supported = $false; Observable = $true; Reason = $null }
        } else {
            $mixed = [pscustomobject]@{
                Supported  = $false
                Observable = $false
                Reason     = 'mixed-dpi-unobservable-on-host'
            }
        }
    }
    $primary = @($topology.Monitors | Where-Object { [bool]$_.Primary }) | Select-Object -First 1
    if ($null -eq $primary) { $primary = @($topology.Monitors) | Select-Object -First 1 }
    $primaryResolution = ''
    if ($null -ne $primary -and $null -ne $primary.Bounds) {
        $primaryResolution = "$($primary.Bounds.Width)x$($primary.Bounds.Height)"
    }
    return [pscustomobject]@{
        MonitorCount            = @($topology.Monitors).Count
        Monitors                = @($topology.Monitors)
        TopologyObservable      = [bool]$topology.Observable
        TopologyReason          = $topology.Reason
        TopologySource          = $topology.Source
        SystemDpi               = [int]$systemDpi.Dpi
        SystemDpiPercent        = (ConvertTo-DisplayPercent -Dpi ([int]$systemDpi.Dpi))
        SystemDpiObservable     = [bool]$systemDpi.Observable
        SystemDpiReason         = $systemDpi.Reason
        SystemDpiSource         = $systemDpi.Source
        PerMonitorDpi           = @($perMonitor.Values)
        PerMonitorDpiObservable = [bool]$perMonitor.Observable
        PerMonitorDpiReason     = $perMonitor.Reason
        MixedDpiSupported       = [bool]$mixed.Supported
        MixedDpiObservable      = [bool]$mixed.Observable
        MixedDpiReason          = $mixed.Reason
        ThemeName               = [string]$theme.ThemeName
        AppsUseLightTheme       = $theme.AppsUseLightTheme
        SystemUsesLightTheme    = $theme.SystemUsesLightTheme
        ThemeObservable         = [bool]$theme.Observable
        ThemeReason             = $theme.Reason
        HighContrast            = [bool]$contrast.Enabled
        HighContrastFlags       = $contrast.Flags
        HighContrastObservable  = [bool]$contrast.Observable
        HighContrastReason      = $contrast.Reason
        PrimaryResolution       = $primaryResolution
        ObservedUtc             = ([DateTime]::UtcNow.ToString('o'))
    }
}

function Get-DisplayMatrixCells {
    return @(
        [pscustomobject]@{ Id = 'dpi-100'; Title = 'DPI 100 percent (96 device DPI)'; Category = 'dpi'; Requires = @('dpi-observable'); RequestedDpi = 100; VisualScenario = 'shell-home-light'; OwnerAction = 'Set Settings > Display > Scale to 100% in the interactive user session, then re-run with -Execute.'; Description = 'Baseline scale. Reuses the catalog shell-home-light row and the Test-VisualDpiMatch requested-vs-observed gate.' },
        [pscustomobject]@{ Id = 'dpi-125'; Title = 'DPI 125 percent (120 device DPI)'; Category = 'dpi'; Requires = @('dpi-observable', 'dpi-125-active'); RequestedDpi = 125; VisualScenario = 'shell-home-light'; OwnerAction = 'Set Settings > Display > Scale to 125% in the interactive user session, sign out/in if prompted, then re-run with -Execute.'; Description = '125% scale. Changing display scale is a user-session action; this harness only observes.' },
        [pscustomobject]@{ Id = 'dpi-150'; Title = 'DPI 150 percent (144 device DPI)'; Category = 'dpi'; Requires = @('dpi-observable', 'dpi-150-active'); RequestedDpi = 150; VisualScenario = 'shell-home-light'; OwnerAction = 'Set Settings > Display > Scale to 150% in the interactive user session, sign out/in if prompted, then re-run with -Execute.'; Description = '150% scale. Changing display scale is a user-session action; this harness only observes.' },
        [pscustomobject]@{ Id = 'dpi-200'; Title = 'DPI 200 percent (192 device DPI)'; Category = 'dpi'; Requires = @('dpi-observable', 'dpi-200-active'); RequestedDpi = 200; VisualScenario = 'shell-home-light'; OwnerAction = 'Set Settings > Display > Scale to 200% in the interactive user session, sign out/in if prompted, then re-run with -Execute.'; Description = '200% scale. Changing display scale is a user-session action; this harness only observes.' },
        [pscustomobject]@{ Id = 'high-contrast'; Title = 'High Contrast theme'; Category = 'accessibility'; Requires = @('high-contrast-observable'); RequestedDpi = 0; VisualScenario = 'high-contrast-live'; OwnerAction = 'Enable a High Contrast theme in the interactive user session (safe restoration cannot be proven by automation), then re-run with -Execute.'; Description = 'High Contrast is never toggled by this campaign because safe restoration cannot be proven; the catalog high-contrast-live Block row stays authoritative.' },
        [pscustomobject]@{ Id = 'light-theme'; Title = 'Light application theme'; Category = 'theme'; Requires = @('theme-observable'); RequestedDpi = 0; VisualScenario = 'shell-home-light'; OwnerAction = 'None: exercised through the product Settings theme combo by Set-VisualTheme/Set-CatalogTheme.'; Description = 'Light theme capture with pixel-change proof via Get-CatalogMeanBrightness.' },
        [pscustomobject]@{ Id = 'dark-theme'; Title = 'Dark application theme'; Category = 'theme'; Requires = @('theme-observable'); RequestedDpi = 0; VisualScenario = 'shell-home-dark'; OwnerAction = 'None: exercised through the product Settings theme combo by Set-VisualTheme/Set-CatalogTheme.'; Description = 'Dark theme capture with pixel-change proof via Get-CatalogMeanBrightness.' },
        [pscustomobject]@{ Id = 'single-monitor'; Title = 'Single-monitor topology'; Category = 'topology'; Requires = @('topology-observable'); RequestedDpi = 0; VisualScenario = 'shell-home-light'; OwnerAction = 'None for detection; live capture runs on the primary monitor.'; Description = 'Single-monitor baseline. Topology observed via System.Windows.Forms.Screen enumeration.' },
        [pscustomobject]@{ Id = 'multi-monitor'; Title = 'Multi-monitor topology'; Category = 'topology'; Requires = @('topology-observable', 'multi-monitor-present'); RequestedDpi = 0; VisualScenario = 'shell-home-light'; OwnerAction = 'Attach a second monitor in the interactive user session, then re-run with -Execute.'; Description = 'Requires two or more observed monitors; a single-monitor host reports HOST_UNSUPPORTED, never PASS.' },
        [pscustomobject]@{ Id = 'mixed-dpi'; Title = 'Mixed-DPI multi-monitor'; Category = 'topology'; Requires = @('topology-observable', 'mixed-dpi-present'); RequestedDpi = 0; VisualScenario = 'shell-home-light'; OwnerAction = 'Attach monitors with different scale factors in the interactive user session, then re-run with -Execute.'; Description = 'Requires distinct observed per-monitor DPI values; the catalog mixed-dpi-transition Block row stays authoritative until then.' },
        [pscustomobject]@{ Id = 'monitor-transition'; Title = 'Cross-monitor window transition'; Category = 'topology'; Requires = @('topology-observable', 'multi-monitor-present'); RequestedDpi = 0; VisualScenario = 'shell-home-light'; OwnerAction = 'Attach a second monitor in the interactive user session, then re-run with -Execute.'; Description = 'Window moved across monitors via Set-UiaWindowPosition geometry; DWM bounds recorded before and after.' },
        [pscustomobject]@{ Id = 'window-maximize'; Title = 'Maximized window state'; Category = 'window-state'; Requires = @('topology-observable'); RequestedDpi = 0; VisualScenario = 'shell-wide-window'; OwnerAction = 'None for detection; live capture drives the wide-window geometry row.'; Description = 'Maximized/wide geometry via the catalog shell-wide-window row and Set-VisualWindowSize machinery.' },
        [pscustomobject]@{ Id = 'window-restore'; Title = 'Restored (normal) window state'; Category = 'window-state'; Requires = @('topology-observable'); RequestedDpi = 0; VisualScenario = 'shell-normal-window'; OwnerAction = 'None for detection; live capture drives the normal-window geometry row.'; Description = 'Normal geometry via the catalog shell-normal-window row and Set-VisualWindowSize machinery.' },
        [pscustomobject]@{ Id = 'window-minimize'; Title = 'Minimized then restored window'; Category = 'window-state'; Requires = @('topology-observable'); RequestedDpi = 0; VisualScenario = 'shell-normal-window'; OwnerAction = 'None for detection; live execution minimizes via ShowWindow, restores, then captures the normal-window row.'; Description = 'A minimized window has no visible DWM frame, so evidence is the post-restore capture plus the minimize/restore transition record.' },
        [pscustomobject]@{ Id = 'window-resize'; Title = 'Resized (narrow) window'; Category = 'window-state'; Requires = @('topology-observable'); RequestedDpi = 0; VisualScenario = 'shell-narrow-window'; OwnerAction = 'None for detection; live capture drives the narrow-window geometry row.'; Description = 'Narrow geometry via the catalog shell-narrow-window row and Set-CatalogWindowSize machinery.' }
    )
}

function Test-DisplayCellRequirement {
    param($Cell, $Facts)
    $unsatisfied = [System.Collections.Generic.List[string]]::new()
    foreach ($requirement in @($Cell.Requires)) {
        switch ([string]$requirement) {
            'dpi-observable' {
                if (-not $Facts.SystemDpiObservable) { [void]$unsatisfied.Add('system-dpi-unobservable') }
            }
            'topology-observable' {
                if (-not $Facts.TopologyObservable) { [void]$unsatisfied.Add('monitor-topology-unobservable') }
            }
            'theme-observable' {
                if (-not $Facts.ThemeObservable) { [void]$unsatisfied.Add('theme-unobservable') }
            }
            'high-contrast-observable' {
                if (-not $Facts.HighContrastObservable) { [void]$unsatisfied.Add('high-contrast-state-unobservable') }
            }
            'multi-monitor-present' {
                if ($Facts.TopologyObservable -and [int]$Facts.MonitorCount -lt 2) {
                    [void]$unsatisfied.Add("single-monitor-host (observed $($Facts.MonitorCount) monitor(s))")
                }
            }
            'mixed-dpi-present' {
                if ($Facts.MixedDpiObservable) {
                    if (-not $Facts.MixedDpiSupported) { [void]$unsatisfied.Add('no-distinct-per-monitor-dpi-observed') }
                } else {
                    [void]$unsatisfied.Add('mixed-dpi-unobservable-on-host')
                }
            }
            default {
                if ($_ -match '^dpi-(\d+)-active$') {
                    $requested = [int]$Matches[1]
                    if ($Facts.SystemDpiObservable -and [int]$Facts.SystemDpiPercent -ne $requested) {
                        [void]$unsatisfied.Add("requested-dpi-$requested-not-active (observed $($Facts.SystemDpiPercent)%)")
                    }
                } else {
                    [void]$unsatisfied.Add("unknown-requirement:$_")
                }
            }
        }
    }
    $dpiRequirements = @($Cell.Requires | Where-Object { [string]$_ -match '^dpi-\d+-active$' })
    return [pscustomobject]@{
        Satisfied   = ($unsatisfied.Count -eq 0)
        Reasons     = @($unsatisfied)
        DeferredDpi = ($dpiRequirements.Count -gt 0)
    }
}

function Resolve-DisplayCellStatus {
    param($Cell, $Facts)
    $check = Test-DisplayCellRequirement -Cell $Cell -Facts $Facts
    if (-not $check.Satisfied) {
        $dpiOnly = ($check.DeferredDpi -and (@($check.Reasons | Where-Object { $_ -notmatch '^requested-dpi-' }).Count -eq 0))
        if ($dpiOnly) {
            return [pscustomobject]@{
                Status      = 'NOT_RUN'
                Reason      = "deferred-behind-user-session-dpi-change ($($check.Reasons -join '; '); display-scale changes are user-session actions)"
                EvidenceRef = $null
            }
        }
        return [pscustomobject]@{
            Status      = 'HOST_UNSUPPORTED'
            Reason      = ($check.Reasons -join '; ')
            EvidenceRef = $null
        }
    }
    if ($Cell.Id -eq 'high-contrast' -and -not $Facts.HighContrast) {
        return [pscustomobject]@{
            Status      = 'NOT_RUN'
            Reason      = 'deferred-owner-sequenced-live-execution (enabling High Contrast is a user-session action; safe restoration cannot be proven by automation)'
            EvidenceRef = $null
        }
    }
    return [pscustomobject]@{
        Status      = 'NOT_RUN'
        Reason      = 'deferred-owner-sequenced-live-execution'
        EvidenceRef = $null
    }
}

Export-ModuleMember -Function Get-DisplayCellVocabulary, Get-HostDisplayFacts,
    Get-DisplayMonitors, Get-DisplayMatrixCells, Test-DisplayCellRequirement,
    Resolve-DisplayCellStatus
