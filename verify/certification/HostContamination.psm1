<#
    FastFiles clean-host contamination detector (hardening-campaign Phase 5).

    Read-only host-state authority for install/service certification: reports
    whether the current machine carries FastFiles residue that disqualifies it
    as a clean host. NEVER mutates service, group, registry, trust stores, or
    the filesystem; every probe is a read (Get-*/Test-Path/directory listing).

    Verdicts (explicit tri-state, never inferred):
      CLEAN        - every marker probed ABSENT (clean for the covered set)
      CONTAMINATED - at least one marker PRESENT (vetoes install certification)
      UNKNOWN      - no marker present, but at least one probe errored, so a
                     clean verdict cannot be proven (fail-closed: vetoes too)

    A probe error NEVER reads as ABSENT: unknown markers force UNKNOWN, which
    vetoes exactly like CONTAMINATED. The detector therefore can never report
    CLEAN when a marker exists or when coverage is incomplete.

    Marker inventory (sources):
      processes .......... FastFiles* processes (Get-Process)
      service ............ FastFilesIndexSvc SCM entry (Identifiers.h kServiceName)
      install-dir ........ %ProgramFiles%\FastFiles (InstallSteps.cpp install dir)
      program-data ....... %ProgramData%\FastFiles (InstallSteps.cpp RemoveDiagnosticState)
      user-data .......... %LOCALAPPDATA%\FastFiles (engine index.db, logs, settings)
      uia-sessions ....... per-user UIA session sandboxes (Get-UiaSessionsRoot /
                           FASTFILES_UIA_SESSIONS_ROOT)
      scheduled-task ..... \FastFiles\ task folder incl. FastFilesEngine
                           (Identifiers.h kEngineTaskFolder/kEngineTaskName)
      client-group ....... FastFilesUsers (Identifiers.h kAuthorizedClientGroupName)
      named-pipes ........ \\.\pipe\FastFiles.* (Identifiers.h kCtrlPipeName /
                           kUiCtrlPipeNameFormat)
      uninstall-registry . HKLM Uninstall\FastFiles (InstallSteps.cpp RegisterUninstallMetadata)
      wer-registry ....... HKLM WER LocalDumps\FastFilesIndexSvc.exe
                           (InstallSteps.cpp RemoveDiagnosticState)

    Deliberately NOT a marker: the Local\FastFiles.Install.Mutex single-instance
    guard (InstallLifecycle.h) is transient by design, not residue.

    This detector EXTENDS verify/release/ReleaseCommon.psm1 Get-CleanHostPreflight
    (8 resources as booleans; no pipes/WER/UIA-sessions markers, no UNKNOWN
    state, no injectable probes) -- it does not replace it.

    Fixture injection: every marker accepts an override via -ProbeOverrides
    (hashtable id -> scriptblock | @{ Present=; Detail= }), so headless tests
    prove state transitions without touching real service/group/registry state.
    Filesystem roots and registry keys are likewise parameters, never hardcoded.
#>

$script:HostContaminationMarkerIds = @(
    'processes',
    'service',
    'install-dir',
    'program-data',
    'user-data',
    'uia-sessions',
    'scheduled-task',
    'client-group',
    'named-pipes',
    'uninstall-registry',
    'wer-registry'
)

$script:DefaultServiceName = 'FastFilesIndexSvc'
$script:DefaultTaskPath = '\FastFiles\'
$script:DefaultTaskName = 'FastFilesEngine'
$script:DefaultGroupName = 'FastFilesUsers'
$script:DefaultRegistryKeys = @(
    'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\FastFiles',
    'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\FastFilesIndexSvc.exe'
)

function Test-IsWindowsHost {
    # $IsWindows exists only on PowerShell 6+; fall back to the OS env marker
    # so Windows PowerShell 5.1 still classifies correctly.
    if ($PSVersionTable.PSVersion.Major -ge 6) { return [bool]$IsWindows }
    return ($env:OS -eq 'Windows_NT')
}

function Get-DefaultUiaSessionsDir {
    if ($env:FASTFILES_UIA_SESSIONS_ROOT) { return [string]$env:FASTFILES_UIA_SESSIONS_ROOT }
    if ($env:LOCALAPPDATA) { return (Join-Path $env:LOCALAPPDATA 'FastFilesUiaSessions') }
    return (Join-Path ([IO.Path]::GetTempPath()) 'FastFilesUiaSessions')
}

function Get-ProcessesContamination {
    $found = @(Get-Process -Name 'FastFiles*' -ErrorAction SilentlyContinue)
    if ($found.Count -gt 0) {
        $desc = ($found | ForEach-Object { "$($_.Name):$($_.Id)" } | Sort-Object -Unique) -join ', '
        return @{ Present = $true; Detail = "running FastFiles processes: $desc" }
    }
    return @{ Present = $false; Detail = 'no FastFiles* processes observed' }
}

function Get-ServiceContamination {
    param([Parameter(Mandatory)] [hashtable] $Context)
    if (-not (Test-IsWindowsHost)) { throw 'non-windows-host: SCM state cannot be proven absent' }
    $service = Get-Service -Name $Context.ServiceName -ErrorAction SilentlyContinue
    if ($null -ne $service) {
        $extra = ''
        try {
            $cim = Get-CimInstance -ClassName Win32_Service -Filter ("Name='" + $Context.ServiceName + "'") -ErrorAction Stop
            if ($cim) { $extra = "; startName=$($cim.StartName); state=$($cim.State)" }
        } catch { $extra = '; cim-detail-unavailable' }
        return @{ Present = $true; Detail = "service present (status=$($service.Status)$extra)" }
    }
    return @{ Present = $false; Detail = 'service not installed' }
}

function Get-DirectoryContamination {
    param(
        [Parameter(Mandatory)] [hashtable] $Context,
        [Parameter(Mandatory)] [string] $Key,
        [Parameter(Mandatory)] [string] $Label
    )
    $dir = [string]$Context[$Key]
    if ([string]::IsNullOrWhiteSpace($dir)) { throw "no directory configured for '$Label'" }
    if (Test-Path -LiteralPath $dir -ErrorAction Stop) {
        $count = 'unreadable'
        try { $count = [string]@(Get-ChildItem -LiteralPath $dir -Force -ErrorAction Stop).Count } catch { $count = 'unreadable' }
        return @{ Present = $true; Detail = "directory exists: $dir (entries=$count)" }
    }
    return @{ Present = $false; Detail = "directory absent: $dir" }
}

function Get-ScheduledTaskContamination {
    param([Parameter(Mandatory)] [hashtable] $Context)
    if (-not (Test-IsWindowsHost)) { throw 'non-windows-host: task state cannot be proven absent' }
    $tasks = @(Get-ScheduledTask -TaskPath $Context.TaskPath -ErrorAction SilentlyContinue)
    $exact = @($tasks | Where-Object { $_.TaskName -ieq $Context.TaskName })
    if ($exact.Count -gt 0) {
        return @{ Present = $true; Detail = "scheduled task present: $($Context.TaskPath)$($Context.TaskName)" }
    }
    if ($tasks.Count -gt 0) {
        $names = ($tasks | ForEach-Object { $_.TaskName } | Sort-Object -Unique) -join ', '
        return @{ Present = $true; Detail = "FastFiles task folder non-empty ($names); exact task '$($Context.TaskName)' not found" }
    }
    return @{ Present = $false; Detail = "no tasks under $($Context.TaskPath)" }
}

function Get-ClientGroupContamination {
    param([Parameter(Mandatory)] [hashtable] $Context)
    if (-not (Test-IsWindowsHost)) { throw 'non-windows-host: group state cannot be proven absent' }
    $group = Get-LocalGroup -Name $Context.GroupName -ErrorAction SilentlyContinue
    if ($null -ne $group) {
        return @{ Present = $true; Detail = "local group present: $($Context.GroupName)" }
    }
    return @{ Present = $false; Detail = 'group not present' }
}

function Get-NamedPipeContamination {
    if (-not (Test-IsWindowsHost)) { throw 'non-windows-host: pipe namespace cannot be proven absent' }
    $pipes = @([IO.Directory]::GetFiles('\\.\pipe\', 'FastFiles.*'))
    if ($pipes.Count -gt 0) {
        $names = ($pipes | ForEach-Object { [IO.Path]::GetFileName($_) } | Sort-Object -Unique) -join ', '
        return @{ Present = $true; Detail = "FastFiles pipes present: $names" }
    }
    return @{ Present = $false; Detail = 'no FastFiles.* pipes observed' }
}

function Get-RegistryKeyContamination {
    param([Parameter(Mandatory)] [string] $Key)
    if (-not (Test-IsWindowsHost)) { throw 'non-windows-host: registry state cannot be proven absent' }
    if (Test-Path -LiteralPath $Key -ErrorAction Stop) {
        return @{ Present = $true; Detail = "registry key exists: $Key" }
    }
    return @{ Present = $false; Detail = "registry key absent: $Key" }
}

function Resolve-ContaminationMarker {
    param(
        [Parameter(Mandatory)] [string] $Id,
        [Parameter(Mandatory)] [string] $Category,
        [Parameter(Mandatory)] [string] $Target,
        [Parameter(Mandatory)] [scriptblock] $Probe,
        [Parameter(Mandatory)] [hashtable] $Context,
        [hashtable] $ProbeOverrides = @{}
    )
    $present = $false
    $state = 'UNKNOWN'
    $detail = 'probe-did-not-run'
    try {
        if ($ProbeOverrides.ContainsKey($Id)) {
            $override = $ProbeOverrides[$Id]
            if ($override -is [scriptblock]) { $observed = & $override }
            else { $observed = $override }
            if ($null -eq $observed) { throw "fixture-override for '$Id' returned no result" }
            $present = [bool]$observed.Present
            $suffix = if ($observed.Detail) { [string]$observed.Detail } else { 'fixture-override' }
            $detail = "override:$suffix"
            $state = if ($present) { 'PRESENT' } else { 'ABSENT' }
        } else {
            $observed = & $Probe $Context
            if ($null -eq $observed) { throw "probe for '$Id' returned no result" }
            $present = [bool]$observed.Present
            $detail = [string]$observed.Detail
            $state = if ($present) { 'PRESENT' } else { 'ABSENT' }
        }
    } catch {
        $present = $false
        $state = 'UNKNOWN'
        $detail = "probe-error: $($_.Exception.Message)"
    }
    return [pscustomobject]@{
        id       = $Id
        category = $Category
        target   = $Target
        present  = $present
        state    = $state
        detail   = $detail
    }
}

function Get-DirectoryContamination {
    param(
        [Parameter(Mandatory)] [hashtable] $Context,
        [Parameter(Mandatory)] [string] $Key,
        [Parameter(Mandatory)] [string] $Label
    )
    $dir = [string]$Context[$Key]
    if ([string]::IsNullOrWhiteSpace($dir)) { throw "no directory configured for '$Label'" }
    if (Test-Path -LiteralPath $dir -ErrorAction Stop) {
        $count = 'unreadable'
        try { $count = [string]@(Get-ChildItem -LiteralPath $dir -Force -ErrorAction Stop).Count } catch { $count = 'unreadable' }
        return @{ Present = $true; Detail = "directory exists: $dir (entries=$count)" }
    }
    return @{ Present = $false; Detail = "directory absent: $dir" }
}

function Get-HostContamination {
    [CmdletBinding()]
    param(
        [string] $InstallDir = '',
        [string] $ProgramDataDir = '',
        [string] $UserDataDir = '',
        [string] $UiaSessionsDir = '',
        [string] $ServiceName = 'FastFilesIndexSvc',
        [string] $TaskPath = '\FastFiles\',
        [string] $TaskName = 'FastFilesEngine',
        [string] $GroupName = 'FastFilesUsers',
        [string[]] $RegistryKeys = @(),
        [hashtable] $ProbeOverrides = @{}
    )

    if ([string]::IsNullOrWhiteSpace($InstallDir)) {
        if ($env:ProgramFiles) { $InstallDir = Join-Path $env:ProgramFiles 'FastFiles' }
        else { $InstallDir = Join-Path ([IO.Path]::GetTempPath()) 'FastFiles-Phase5-NoProgramFiles' }
    }
    if ([string]::IsNullOrWhiteSpace($ProgramDataDir)) {
        if ($env:ProgramData) { $ProgramDataDir = Join-Path $env:ProgramData 'FastFiles' }
        else { $ProgramDataDir = Join-Path ([IO.Path]::GetTempPath()) 'FastFiles-Phase5-NoProgramData' }
    }
    if ([string]::IsNullOrWhiteSpace($UserDataDir)) {
        if ($env:LOCALAPPDATA) { $UserDataDir = Join-Path $env:LOCALAPPDATA 'FastFiles' }
        else { $UserDataDir = Join-Path ([IO.Path]::GetTempPath()) 'FastFiles-Phase5-NoLocalAppData' }
    }
    if ([string]::IsNullOrWhiteSpace($UiaSessionsDir)) {
        $UiaSessionsDir = Get-DefaultUiaSessionsDir
    }
    if ($null -eq $RegistryKeys -or $RegistryKeys.Count -eq 0) {
        $RegistryKeys = @($script:DefaultRegistryKeys)
    }

    $uninstallKey = [string]$RegistryKeys[0]
    $werKey = if ($RegistryKeys.Count -ge 2) { [string]$RegistryKeys[1] } else { [string]$script:DefaultRegistryKeys[1] }
    $context = @{
        InstallDir     = $InstallDir
        ProgramDataDir = $ProgramDataDir
        UserDataDir    = $UserDataDir
        UiaSessionsDir = $UiaSessionsDir
        ServiceName    = $ServiceName
        TaskPath       = $TaskPath
        TaskName       = $TaskName
        GroupName      = $GroupName
        UninstallKey   = $uninstallKey
        WerKey         = $werKey
    }

    $markers = @()
    $markers += Resolve-ContaminationMarker -Id 'processes' -Category 'processes' -Target 'FastFiles* processes' `
        -Probe { param($c) Get-ProcessesContamination } -Context $context -ProbeOverrides $ProbeOverrides
    $markers += Resolve-ContaminationMarker -Id 'service' -Category 'service' -Target "service '$ServiceName'" `
        -Probe { param($c) Get-ServiceContamination -Context $c } -Context $context -ProbeOverrides $ProbeOverrides
    $markers += Resolve-ContaminationMarker -Id 'install-dir' -Category 'filesystem' -Target $InstallDir `
        -Probe { param($c) Get-DirectoryContamination -Context $c -Key 'InstallDir' -Label 'install dir' } -Context $context -ProbeOverrides $ProbeOverrides
    $markers += Resolve-ContaminationMarker -Id 'program-data' -Category 'filesystem' -Target $ProgramDataDir `
        -Probe { param($c) Get-DirectoryContamination -Context $c -Key 'ProgramDataDir' -Label 'program data' } -Context $context -ProbeOverrides $ProbeOverrides
    $markers += Resolve-ContaminationMarker -Id 'user-data' -Category 'filesystem' -Target $UserDataDir `
        -Probe { param($c) Get-DirectoryContamination -Context $c -Key 'UserDataDir' -Label 'user data' } -Context $context -ProbeOverrides $ProbeOverrides
    $markers += Resolve-ContaminationMarker -Id 'uia-sessions' -Category 'filesystem' -Target $UiaSessionsDir `
        -Probe { param($c) Get-DirectoryContamination -Context $c -Key 'UiaSessionsDir' -Label 'uia sessions' } -Context $context -ProbeOverrides $ProbeOverrides
    $markers += Resolve-ContaminationMarker -Id 'scheduled-task' -Category 'scheduled-task' -Target "$TaskPath$TaskName" `
        -Probe { param($c) Get-ScheduledTaskContamination -Context $c } -Context $context -ProbeOverrides $ProbeOverrides
    $markers += Resolve-ContaminationMarker -Id 'client-group' -Category 'local-group' -Target "group '$GroupName'" `
        -Probe { param($c) Get-ClientGroupContamination -Context $c } -Context $context -ProbeOverrides $ProbeOverrides
    $markers += Resolve-ContaminationMarker -Id 'named-pipes' -Category 'named-pipes' -Target '\\.\pipe\FastFiles.*' `
        -Probe { param($c) Get-NamedPipeContamination } -Context $context -ProbeOverrides $ProbeOverrides
    $markers += Resolve-ContaminationMarker -Id 'uninstall-registry' -Category 'registry' -Target $uninstallKey `
        -Probe { param($c) Get-RegistryKeyContamination -Key $c.UninstallKey } -Context $context -ProbeOverrides $ProbeOverrides
    $markers += Resolve-ContaminationMarker -Id 'wer-registry' -Category 'registry' -Target $werKey `
        -Probe { param($c) Get-RegistryKeyContamination -Key $c.WerKey } -Context $context -ProbeOverrides $ProbeOverrides

    $presentIds = @($markers | Where-Object { $_.state -eq 'PRESENT' } | ForEach-Object { $_.id })
    $unknownIds = @($markers | Where-Object { $_.state -eq 'UNKNOWN' } | ForEach-Object { $_.id })

    # Guard order is load-bearing: any PRESENT marker vetoes CLEAN even when
    # other probes errored; errors veto CLEAN even when nothing is present.
    if ($presentIds.Count -gt 0) { $hostState = 'CONTAMINATED' }
    elseif ($unknownIds.Count -gt 0) { $hostState = 'UNKNOWN' }
    else { $hostState = 'CLEAN' }

    return [pscustomobject]@{
        hostState       = $hostState
        clean           = ($hostState -eq 'CLEAN')
        veto            = ($hostState -ne 'CLEAN')
        markers         = @($markers)
        presentMarkers  = @($presentIds)
        unknownMarkers  = @($unknownIds)
        checkedAtUtc    = [DateTime]::UtcNow.ToString('o')
    }
}

function Get-HostContaminationMarkerIds {
    return @($script:HostContaminationMarkerIds)
}

Export-ModuleMember -Function Get-HostContamination, Get-HostContaminationMarkerIds
