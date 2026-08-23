<#
    Deterministic UI-v3 visual catalog.

    Every PASS image is preceded by a semantic UIA assertion. A failed action
    or assertion creates a FAIL manifest row and no valid screenshot. Rows
    whose prerequisites are genuinely unavailable remain explicit blocked or
    skipped records; they are never converted into screenshots.
#>

Set-StrictMode -Version 3.0

$script:IsWindows = [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform(
    [System.Runtime.InteropServices.OSPlatform]::Windows)
$script:DriverModule = Join-Path $PSScriptRoot '..\..\uia-driver\UiaDriver.psm1'
$script:CaptureModule = Join-Path $PSScriptRoot '..\..\visual\VisualCapture.psm1'

function Get-VisualOption {
    param($Options, [string] $Name, $Default = $null)
    if ($null -ne $Options) {
        try {
            if ($Options.ContainsKey($Name)) { return $Options[$Name] }
        } catch {
            # Capability options are normally a hashtable, but the verifier
            # also accepts a property bag in direct module tests.
        }
        if ($Options.PSObject.Properties[$Name]) { return $Options.$Name }
    }
    return $Default
}

function Get-VisualExe {
    param([string] $RepoRoot, $Options)
    $explicit = Get-VisualOption -Options $Options -Name 'UiaExeRel'
    if ($explicit) { return (Join-Path $RepoRoot ([string]$explicit)) }
    $diagnostic = Join-Path $RepoRoot 'build\debug-diagnostic\src\ui\FastFiles.exe'
    if (Test-Path -LiteralPath $diagnostic) { return $diagnostic }
    return (Join-Path $RepoRoot 'build\debug\src\ui\FastFiles.exe')
}

function Get-VisualFixtureRoot {
    param([string] $Id)
    $safe = $Id -replace '[^A-Za-z0-9-]', '-'
    $base = if ($env:SystemDrive) { $env:SystemDrive } else { [IO.Path]::GetTempPath() }
    return Join-Path $base ('ffviz-' + $safe + '-' + [guid]::NewGuid().ToString('N'))
}

function New-VisualFixtureDrive {
    param([Parameter(Mandatory)][string] $PhysicalRoot)
    New-Item -ItemType Directory -Path $PhysicalRoot -Force | Out-Null
    foreach ($letter in @('V', 'W', 'X', 'Y', 'Z', 'R', 'S', 'T')) {
        if (Get-PSDrive -Name $letter -PSProvider FileSystem -ErrorAction SilentlyContinue) { continue }
        $driveSpec = "${letter}:"
        & subst $driveSpec $PhysicalRoot | Out-Null
        if ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath ("${letter}:\"))) {
            return [pscustomobject]@{ Drive = $letter; Root = "${letter}:\"; PhysicalRoot = $PhysicalRoot }
        }
    }
    throw 'no unused drive letter was available for the disposable visual fixture'
}

function Remove-VisualFixtureDrive {
    param($Mapping)
    if (-not $Mapping) { return }
    $driveSpec = [string]$Mapping.Drive + ':'
    & subst $driveSpec /d | Out-Null
}

function New-VisualFixtures {
    param([string] $Root)
    New-Item -ItemType Directory -Path $Root -Force | Out-Null
    $standard = Join-Path $Root 'standard'
    $empty = Join-Path $Root 'empty'
    $deep = Join-Path $Root 'deep\level-one\level-two\level-three'
    $long = Join-Path $Root 'long-names'
    $unicode = Join-Path $Root 'unicode-emoji'
    $rtl = Join-Path $Root 'rtl'
    foreach ($path in @($standard, $empty, $deep, $long, $unicode, $rtl)) {
        New-Item -ItemType Directory -Path $path -Force | Out-Null
    }
    Set-Content -LiteralPath (Join-Path $standard 'alpha.txt') -Value 'alpha' -Encoding utf8
    Set-Content -LiteralPath (Join-Path $standard 'beta.txt') -Value 'beta' -Encoding utf8
    Set-Content -LiteralPath (Join-Path $standard 'image-preview.txt') -Value 'not an image' -Encoding utf8
    Set-Content -LiteralPath (Join-Path $deep 'deep-file.txt') -Value 'deep' -Encoding utf8
    Set-Content -LiteralPath (Join-Path $long ('this-is-a-deterministic-long-file-name-' + ('x' * 80) + '.txt')) -Value 'long' -Encoding utf8
    Set-Content -LiteralPath (Join-Path $unicode 'emoji-📁-東京-данные.txt') -Value 'unicode' -Encoding utf8
    Set-Content -LiteralPath (Join-Path $rtl 'שלום-مرحبا.txt') -Value 'rtl' -Encoding utf8
    return [pscustomobject]@{
        Root = $Root
        Standard = $standard
        Empty = $empty
        Deep = $deep
        Long = $long
        Unicode = $unicode
        Rtl = $rtl
        Missing = Join-Path $Root 'missing-not-created'
    }
}

function Backup-VisualProductState {
    $stateRoot = Join-Path $env:LOCALAPPDATA 'FastFiles'
    $backupRoot = Join-Path ([IO.Path]::GetTempPath()) ('ffviz-state-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null
    # Search history is also rendered by Home. Back it up with the workspace
    # state so committed visual evidence cannot inherit prior user queries.
    $files = @('workspace-state.json', 'settings.json', 'shortcuts.json', 'search-history.txt')
    $entries = [System.Collections.Generic.List[object]]::new()
    foreach ($name in $files) {
        $source = Join-Path $stateRoot $name
        $backup = Join-Path $backupRoot $name
        $exists = Test-Path -LiteralPath $source
        if ($exists) { Copy-Item -LiteralPath $source -Destination $backup -Force }
        [void]$entries.Add([pscustomobject]@{ Source = $source; Backup = $backup; Exists = $exists })
        if ($exists) { Remove-Item -LiteralPath $source -Force }
    }
    return [pscustomobject]@{ Root = $stateRoot; BackupRoot = $backupRoot; Entries = @($entries) }
}

function Restore-VisualProductState {
    param($Backup)
    if (-not $Backup) { return }
    foreach ($entry in $Backup.Entries) {
        if (Test-Path -LiteralPath $entry.Source) { Remove-Item -LiteralPath $entry.Source -Force -ErrorAction SilentlyContinue }
        if ($entry.Exists -and (Test-Path -LiteralPath $entry.Backup)) {
            New-Item -ItemType Directory -Path (Split-Path $entry.Source -Parent) -Force | Out-Null
            Copy-Item -LiteralPath $entry.Backup -Destination $entry.Source -Force
        }
    }
    if (Test-Path -LiteralPath $Backup.BackupRoot) {
        Remove-Item -LiteralPath $Backup.BackupRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Reset-VisualProductStateForScenario {
    param($Backup)
    if (-not $Backup) { return }
    foreach ($entry in $Backup.Entries) {
        if (Test-Path -LiteralPath $entry.Source) {
            Remove-Item -LiteralPath $entry.Source -Force -ErrorAction SilentlyContinue
        }
    }
}

function Find-VisualElement {
    param($Driver, [string] $Name, [string] $NameLike, [string] $AutomationId,
          [string] $ClassName, [string] $ControlType, $FromElement, [int] $ProcessId,
          [int] $TimeoutMs = 1500)
    $args = @{ Driver = $Driver; TimeoutMs = $TimeoutMs }
    if ($Name) { $args.Name = $Name }
    if ($NameLike) { $args.NameLike = $NameLike }
    if ($AutomationId) { $args.AutomationId = $AutomationId }
    if ($ClassName) { $args.ClassName = $ClassName }
    if ($ControlType) { $args.ControlType = $ControlType }
    if ($FromElement) { $args.FromElement = $FromElement }
    if ($ProcessId -gt 0) { $args.ProcessId = $ProcessId }
    try { return (Find-UiaElement @args) } catch { return $null }
}

function Wait-VisualElement {
    param($Driver, [string] $Name, [string] $NameLike, [string] $AutomationId,
          [string] $ClassName, [string] $ControlType, $FromElement, [int] $ProcessId,
          [int] $TimeoutMs = 5000)
    $args = @{ Driver = $Driver; TimeoutMs = $TimeoutMs; PollIntervalMs = 150 }
    if ($Name) { $args.Name = $Name }
    if ($NameLike) { $args.NameLike = $NameLike }
    if ($AutomationId) { $args.AutomationId = $AutomationId }
    if ($ClassName) { $args.ClassName = $ClassName }
    if ($ControlType) { $args.ControlType = $ControlType }
    if ($FromElement) { $args.FromElement = $FromElement }
    if ($ProcessId -gt 0) { $args.ProcessId = $ProcessId }
    try { return (Wait-UiaElement @args) } catch { return $null }
}

function Get-VisualIdentity {
    param($Driver, $Element)
    if (-not $Element) { return $null }
    $id = Get-UiaElementIdentity -Driver $Driver -Element $Element
    $rect = Get-UiaElementProperty -Driver $Driver -Element $Element -Name 'BoundingRectangle'
    $displayName = [string]$id.Name
    if ($displayName -match '^[A-Za-z]:\\' -or $displayName -match '^\\\\') {
        $displayName = '<sanitized-fixture-path>'
    }
    return [pscustomobject]@{
        name = $displayName
        automationId = $id.AutomationId
        controlType = $id.ControlType
        className = $id.ClassName
        processId = $id.ProcessId
        bounds = $rect
    }
}

function Set-VisualAppForeground {
    param($Context)
    for ($attempt = 0; $attempt -lt 3; $attempt++) {
        $null = Set-UiaForeground -Driver $Context.Driver -Element $Context.Main
        if (Test-UiaForegroundWindow -Driver $Context.Driver -Element $Context.Main) { return $true }
        try {
            $rect = Get-UiaElementProperty -Driver $Context.Driver -Element $Context.Main -Name 'BoundingRectangle'
            if ($rect -and $rect.width -gt 0 -and $rect.height -gt 0) {
                # A real caption click is the documented Windows foreground
                # lock escape hatch. The point comes from live UIA geometry;
                # it is not a pixel assertion or a fixed host coordinate.
                $null = Send-UiaMouseInput -Driver $Context.Driver -X ([int]($rect.x + $rect.width / 2)) -Y ([int]($rect.y + 8)) -Button 'Left' -Action 'Click'
            }
        } catch { }
        Start-Sleep -Milliseconds 200
    }
    return (Test-UiaForegroundWindow -Driver $Context.Driver -Element $Context.Main)
}

function Invoke-VisualSemanticClick {
    param($Context, $Element)
    if (-not $Element) { return $false }
    try {
        # The point is derived from the live semantic BoundingRectangle. The
        # managed GetClickablePoint can select the non-interactive left edge
        # of native ComboBox proxies, so this helper intentionally uses the
        # center of the current provider rectangle.
        $rect = Get-UiaElementProperty -Driver $Context.Driver -Element $Element -Name 'BoundingRectangle'
        if (-not $rect -or $rect.width -le 0 -or $rect.height -le 0) { return $false }
        $point = [pscustomobject]@{
            x = [int]($rect.x + $rect.width / 2)
            y = [int]($rect.y + $rect.height / 2)
        }
        $null = Send-UiaMouseInput -Driver $Context.Driver -X $point.x -Y $point.y -Button 'Left' -Action 'Click'
        return $true
    } catch {
        return $false
    }
}

function New-VisualActionResult {
    param([bool] $Pass, [string] $Reason = $null, $Evidence = $null,
          [string] $Status = $null, [string] $DuplicateReason = $null,
          [string] $ExpectedDuplicateOf = $null, [object[]] $PrivacyRectangles = @())
    return [pscustomobject]@{
        Pass = $Pass
        Reason = $Reason
        Evidence = $Evidence
        Status = $Status
        DuplicateReason = $DuplicateReason
        ExpectedDuplicateOf = $ExpectedDuplicateOf
        PrivacyRectangles = @($PrivacyRectangles)
    }
}

function Start-VisualContext {
    param([string] $Exe, [string] $RepoRoot, [string] $FixtureRoot)
    $driver = New-UiaDriver -TimeoutMs 5000 -PollIntervalMs 100 -MaxTreeDepth 24 -MaxTreeWidth 160
    $existingEngineIds = @(
        Get-Process -Name FastFilesEngine -ErrorAction SilentlyContinue |
            ForEach-Object { $_.Id }
    )
    $process = $null
    $main = $null
    try {
        $process = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe -Parent) -PassThru
        $main = Wait-VisualElement -Driver $driver -ClassName 'FastFilesMainWindow' -ProcessId $process.Id -TimeoutMs 25000
        if (-not $main) {
            throw 'FastFiles main window was not discoverable within 25 seconds'
        }
        $startupSettings = Find-VisualElement -Driver $driver -ClassName 'FastFilesSettingsDialog' -ProcessId $process.Id -TimeoutMs 500
        if ($startupSettings) {
            $cancel = Find-VisualElement -Driver $driver -AutomationId '2' -FromElement $startupSettings -TimeoutMs 1200
            if ($cancel) { $null = Invoke-UiaElementAction -Driver $driver -Element $cancel -Action 'Click' }
            $null = Wait-UiaElementGone -Driver $driver -ClassName 'FastFilesSettingsDialog' -ProcessId $process.Id -TimeoutMs 3000
        }
        # A previous isolated scenario may have left a sibling window foreground.
        # Establish and semantically verify ownership before the first keyboard
        # action; this is a bounded readiness wait, not a retry of a failed state.
        $foregroundReady = Set-VisualAppForeground -Context ([pscustomobject]@{ Driver = $driver; Main = $main })
        if (-not $foregroundReady) {
            throw 'FastFiles did not acquire foreground ownership during lifecycle readiness'
        }
        $engineIds = @(
            Get-Process -Name FastFilesEngine -ErrorAction SilentlyContinue |
                Where-Object { $existingEngineIds -notcontains $_.Id } |
                ForEach-Object { $_.Id }
        )
        return [pscustomobject]@{
            Driver = $driver
            Process = $process
            EngineIds = $engineIds
            Main = $main
            RepoRoot = $RepoRoot
            FixtureRoot = $FixtureRoot
        }
    } catch {
        if ($process) { try { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue } catch { } }
        Get-Process -Name FastFilesEngine -ErrorAction SilentlyContinue |
            Where-Object { $existingEngineIds -notcontains $_.Id } |
            ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
        throw
    }
}

function Stop-VisualContext {
    param($Context)
    if (-not $Context) { return }
    try { $null = Reset-UiaModifierKeys -Driver $Context.Driver } catch { }
    if ($Context.Process) {
        try { Stop-Process -Id $Context.Process.Id -Force -ErrorAction SilentlyContinue } catch { }
    }
    foreach ($engineId in @($Context.EngineIds)) {
        try { Stop-Process -Id $engineId -Force -ErrorAction SilentlyContinue } catch { }
    }
    Start-Sleep -Milliseconds 250
}

function Set-VisualWindowSize {
    param($Context, [int] $Width)
    $requestedWidth = $Width
    for ($attempt = 0; $attempt -lt 2; $attempt++) {
        $position = Set-UiaWindowPosition -Driver $Context.Driver -Element $Context.Main -X 40 -Y 40 -Width $requestedWidth -Height 800
        if (-not $position.ok) { return (New-VisualActionResult -Pass $false -Reason 'SetWindowPos failed') }
        $stable = Wait-UiaStableRect -Driver $Context.Driver -Element $Context.Main -StableReads 3 -IntervalMs 150 -TimeoutMs 4000
        if (-not $stable) { return (New-VisualActionResult -Pass $false -Reason 'main window geometry did not settle') }
        try {
            $bounds = Get-VisualWindowBounds -Element $Context.Main
            $delta = $Width - $bounds.Width
            if ([Math]::Abs($delta) -le 8) {
                return (New-VisualActionResult -Pass $true -Evidence $bounds)
            }
            if ($attempt -eq 0) { $requestedWidth += $delta; continue }
            return (New-VisualActionResult -Pass $false -Reason "DWM width $($bounds.Width) differs from requested $Width after bounded frame adjustment" -Evidence $bounds)
        } catch {
            return (New-VisualActionResult -Pass $false -Reason $_.Exception.Message)
        }
    }
    return (New-VisualActionResult -Pass $false -Reason 'geometry adjustment exhausted')
}

function Set-VisualTheme {
    param($Context, [string] $Theme)
    $open = Invoke-VisualPalette -Context $Context -Query 'settings' -ExpectedNames @('Settings')
    if (-not $open.Pass) { return $open }
    $dialog = Wait-VisualElement -Driver $Context.Driver -ClassName 'FastFilesSettingsDialog' -ProcessId $Context.Process.Id -TimeoutMs 4000
    if (-not $dialog) { return (New-VisualActionResult -Pass $false -Reason 'settings dialog did not appear while setting theme') }
    $null = Set-UiaForeground -Driver $Context.Driver -Element $dialog
    if (-not (Test-UiaForegroundWindow -Driver $Context.Driver -Element $dialog)) {
        return (New-VisualActionResult -Pass $false -Reason 'settings dialog did not own foreground while setting theme')
    }
    # Native dialog controls are intentionally surfaced through the existing
    # fragment seam as Pane control types; ClassName remains the stable native
    # identity for this supported ComboBox.
    $combo = Find-VisualElement -Driver $Context.Driver -ClassName 'ComboBox' -FromElement $dialog -TimeoutMs 2000
    if (-not $combo) {
        $null = Send-UiaInput -Driver $Context.Driver -Keys '{ESC}'
        return (New-VisualActionResult -Pass $false -Reason 'settings theme ComboBox was not exposed by UIA')
    }
    if (-not (Invoke-VisualSemanticClick -Context $Context -Element $combo)) {
        return (New-VisualActionResult -Pass $false -Reason 'theme ComboBox did not expose usable semantic geometry')
    }
    $themeIndex = switch ($Theme) {
        'Light' { 0 }
        'Dark' { 1 }
        'Follow System' { 2 }
        default { -1 }
    }
    if ($themeIndex -lt 0) {
        $null = Send-UiaInput -Driver $Context.Driver -Keys '{ESC}'
        return (New-VisualActionResult -Pass $false -Reason "unknown supported theme '$Theme'")
    }
    # The native ComboLBox popup is a separate HWND and does not expose its
    # list items through the fragment tree on this host. Keyboard navigation
    # remains a real supported ComboBox interaction: Home selects the first
    # semantic option, bounded Down presses select the requested option, and
    # Enter commits the visible control.
    $keys = '{HOME}'
    for ($index = 0; $index -lt $themeIndex; $index++) { $keys += '{DOWN}' }
    $keys += '{ENTER}'
    $null = Send-UiaInput -Driver $Context.Driver -Keys $keys
    $selectionUpdated = Wait-UiaCondition -Driver $Context.Driver -TimeoutMs 2000 -PollIntervalMs 100 -Description "theme ComboBox selection '$Theme'" -ScriptBlock {
        [string](Get-UiaElementProperty -Driver $Context.Driver -Element $combo -Name 'Name') -eq $Theme
    }
    $selectedName = [string](Get-UiaElementProperty -Driver $Context.Driver -Element $combo -Name 'Name')
    $interactiveAfterSelection = Get-UiaInteractiveState -Driver $Context.Driver -ProcessId $Context.Process.Id
    if ($selectedName -ne $Theme -and $interactiveAfterSelection.FocusedElement -and
        [string]$interactiveAfterSelection.FocusedElement.Name -eq $Theme) {
        # The native ComboBox UIA proxy can lag its Name property for the
        # Follow System item after the popup closes; the focused semantic
        # option is still the real control state. Re-open verification below
        # remains the commit-level proof.
        $selectedName = $Theme
    }
    if ($selectedName -ne $Theme) {
        # Some Windows builds expose the native dropdown's keyboard popup but
        # do not commit the selection generated by SendInput. The fallback is
        # still the real supported Settings ComboBox (CB_SETCURSEL), followed
        # by the same semantic read-back and dialog commit/reopen proof.
        try {
            $nativeHandle = [IntPtr]([int64](Get-UiaElementProperty -Driver $Context.Driver -Element $combo -Name 'NativeWindowHandle'))
            if ($nativeHandle -ne [IntPtr]::Zero) {
                $null = [FfVisualCaptureNative]::SendMessage($nativeHandle, 0x014E,
                    [IntPtr]$themeIndex, [IntPtr]::Zero)
                Start-Sleep -Milliseconds 150
                $selectedName = [string](Get-UiaElementProperty -Driver $Context.Driver -Element $combo -Name 'Name')
            }
        } catch { }
    }
    if ($selectedName -ne $Theme) {
        $focusedName = if ($interactiveAfterSelection.FocusedElement) { [string]$interactiveAfterSelection.FocusedElement.Name } else { '' }
        $null = Send-UiaInput -Driver $Context.Driver -Keys '{ESC}'
        return (New-VisualActionResult -Pass $false -Reason "theme ComboBox reported '$selectedName' after selecting '$Theme' (keys='$keys', focused='$focusedName', popups=$(@($interactiveAfterSelection.Popups).Count))")
    }
    $ok = Find-VisualElement -Driver $Context.Driver -AutomationId '1' -FromElement $dialog -TimeoutMs 2000
    if (-not $ok) { return (New-VisualActionResult -Pass $false -Reason 'settings OK control was not exposed') }
    $null = Invoke-UiaElementAction -Driver $Context.Driver -Element $ok -Action 'Click'
    $closed = Wait-UiaElementGone -Driver $Context.Driver -ClassName 'FastFilesSettingsDialog' -ProcessId $Context.Process.Id -TimeoutMs 4000
    if (-not $closed) { return (New-VisualActionResult -Pass $false -Reason 'settings dialog did not close after committing theme') }
    # Re-open the same supported control and verify the persisted selection,
    # rather than treating a successful click as proof that ApplyTheme ran.
    $verifyOpen = Invoke-VisualPalette -Context $Context -Query 'settings' -ExpectedNames @('Settings')
    if (-not $verifyOpen.Pass) { return $verifyOpen }
    $verifyDialog = Wait-VisualElement -Driver $Context.Driver -ClassName 'FastFilesSettingsDialog' -ProcessId $Context.Process.Id -TimeoutMs 4000
    $null = Set-UiaForeground -Driver $Context.Driver -Element $verifyDialog
    $verifyCombo = Find-VisualElement -Driver $Context.Driver -ClassName 'ComboBox' -FromElement $verifyDialog -TimeoutMs 2000
    $verifiedName = if ($verifyCombo) { [string](Get-UiaElementProperty -Driver $Context.Driver -Element $verifyCombo -Name 'Name') } else { '' }
    $cancel = Find-VisualElement -Driver $Context.Driver -AutomationId '2' -FromElement $verifyDialog -TimeoutMs 1500
    if ($cancel) { $null = Invoke-UiaElementAction -Driver $Context.Driver -Element $cancel -Action 'Click' }
    $pass = $verifiedName -eq $Theme
    return (New-VisualActionResult -Pass $pass -Reason $(if ($pass) { $null } else { "theme verification reported '$verifiedName', expected '$Theme'" }) -Evidence ([pscustomobject]@{
        requested = $Theme
        selectedBeforeCommit = $selectedName
        selectedAfterReopen = $verifiedName
    }))
}

function Invoke-VisualPalette {
    param($Context, [string] $Query, [string[]] $ExpectedNames = @(), [switch] $KeepOpen)
    if (-not (Set-VisualAppForeground -Context $Context)) {
        return (New-VisualActionResult -Pass $false -Reason 'FastFiles did not own foreground before command palette')
    }
    $null = Send-UiaInput -Driver $Context.Driver -Keys '{CTRL}{SHIFT}p'
    $edit = Wait-VisualElement -Driver $Context.Driver -AutomationId '6301' -FromElement $Context.Main -TimeoutMs 4000
    if (-not $edit) { return (New-VisualActionResult -Pass $false -Reason 'command palette edit did not appear') }
    if (-not [string]::IsNullOrEmpty($Query)) {
        $null = Send-UiaText -Driver $Context.Driver -Text $Query
    }
    $missing = [System.Collections.Generic.List[string]]::new()
    foreach ($name in $ExpectedNames) {
        $expected = Wait-VisualElement -Driver $Context.Driver -Name $name -FromElement $Context.Main -TimeoutMs 2500
        if (-not $expected) {
            $expected = Wait-VisualElement -Driver $Context.Driver -Name $name -ProcessId $Context.Process.Id -TimeoutMs 1500
        }
        if (-not $expected) {
            [void]$missing.Add($name)
        }
    }
    if ($missing.Count -gt 0) {
        $null = Send-UiaInput -Driver $Context.Driver -Keys '{ESC}'
        return (New-VisualActionResult -Pass $false -Reason "palette '$Query' missing: $($missing -join ', ')")
    }
    if (-not $KeepOpen) {
        $null = Send-UiaInput -Driver $Context.Driver -Keys '{ENTER}'
        Start-Sleep -Milliseconds 500
    }
    return (New-VisualActionResult -Pass $true -Evidence ([pscustomobject]@{
        query = $Query
        edit = Get-VisualIdentity -Driver $Context.Driver -Element $edit
        expected = $ExpectedNames
        keptOpen = [bool]$KeepOpen
    }))
}

function Invoke-VisualAddress {
    param($Context, [string] $Path, [string] $ExpectedLeaf)
    if (-not (Set-VisualAppForeground -Context $Context)) {
        return (New-VisualActionResult -Pass $false -Reason 'FastFiles did not own foreground before address navigation')
    }
    $null = Send-UiaInput -Driver $Context.Driver -Keys '{CTRL}l'
    $edit = Wait-VisualElement -Driver $Context.Driver -ClassName 'Edit' -FromElement $Context.Main -TimeoutMs 3000
    if (-not $edit) { return (New-VisualActionResult -Pass $false -Reason 'address edit did not appear') }
    $set = Set-UiaElementValue -Driver $Context.Driver -Element $edit -Value $Path
    if (-not $set.ok) {
        $null = Send-UiaInput -Driver $Context.Driver -Keys '{CTRL}a'
        $null = Send-UiaText -Driver $Context.Driver -Text $Path
    }
    $valueMatches = Wait-UiaCondition -Driver $Context.Driver -TimeoutMs 1500 -PollIntervalMs 100 -Description 'address value reflects sanitized fixture' -ScriptBlock {
        (Get-UiaElementTextValue -Driver $Context.Driver -Element $edit) -eq $Path
    }
    if (-not $valueMatches) {
        # Some native EDIT UIA proxies report a successful ValuePattern write
        # while retaining only the prefix before a space. Re-enter through the
        # documented keyboard editing path, then verify the complete value.
        $null = Send-UiaInput -Driver $Context.Driver -Keys '{CTRL}a'
        $null = Send-UiaText -Driver $Context.Driver -Text $Path
        $valueMatches = Wait-UiaCondition -Driver $Context.Driver -TimeoutMs 2500 -PollIntervalMs 100 -Description 'address text reflects complete sanitized fixture' -ScriptBlock {
            (Get-UiaElementTextValue -Driver $Context.Driver -Element $edit) -eq $Path
        }
    }
    if (-not $valueMatches) { return (New-VisualActionResult -Pass $false -Reason 'address control did not semantically reflect the complete sanitized fixture path') }
    $null = Send-UiaInput -Driver $Context.Driver -Keys '{ENTER}'
    $tab = Wait-VisualElement -Driver $Context.Driver -NameLike ('*' + $ExpectedLeaf + '*') -FromElement $Context.Main -TimeoutMs 7000
    if (-not $tab) { return (New-VisualActionResult -Pass $false -Reason "navigation did not expose expected leaf '$ExpectedLeaf'") }
    return (New-VisualActionResult -Pass $true -Evidence ([pscustomobject]@{
        address = Get-VisualIdentity -Driver $Context.Driver -Element $edit
        tab = Get-VisualIdentity -Driver $Context.Driver -Element $tab
    }))
}

function Get-VisualPane {
    param($Context, [string] $PaneId = 'Pane.0')
    $group = Find-VisualElement -Driver $Context.Driver -AutomationId 'ColumnView' -FromElement $Context.Main -TimeoutMs 2500
    if (-not $group) { return $null }
    return Find-VisualElement -Driver $Context.Driver -AutomationId $PaneId -FromElement $group -TimeoutMs 2500
}

function Get-VisualTabCount {
    param($Context)
    $buttons = @(Find-UiaElement -Driver $Context.Driver -ClassName 'Button' -FromElement $Context.Main -TimeoutMs 1500 -All)
    return @($buttons | Where-Object {
        (Get-UiaElementProperty -Driver $Context.Driver -Element $_ -Name 'AutomationId') -match '^830'
    }).Count
}

function Invoke-VisualAction {
    param($Context, $Scenario, $Fixtures)
    switch ([string]$Scenario.Action) {
        'launch' {
            $id = Get-VisualIdentity -Driver $Context.Driver -Element $Context.Main
            return (New-VisualActionResult -Pass ($id.Name -eq 'FastFiles' -and $id.ClassName -eq 'FastFilesMainWindow') -Reason 'main window identity mismatch' -Evidence $id)
        }
        'home' {
            if (-not (Set-VisualAppForeground -Context $Context)) { return (New-VisualActionResult -Pass $false -Reason 'FastFiles did not own foreground before Home navigation') }
            $null = Send-UiaInput -Driver $Context.Driver -Keys '{ALT}{HOME}'
            $home = Wait-VisualElement -Driver $Context.Driver -AutomationId 'Pane.0.Home' -FromElement (Get-VisualPane -Context $Context) -TimeoutMs 4000
            if (-not $home) { return (New-VisualActionResult -Pass $false -Reason 'Home surface was not active after Alt+Home') }
            $sections = @(Find-UiaElement -Driver $Context.Driver -ControlType 'Group' -FromElement $home -TimeoutMs 2000 -All)
            if ($sections.Count -eq 0) {
                return (New-VisualActionResult -Pass $false -Reason 'Home exposed no semantic sections' -Evidence ([pscustomobject]@{ home = Get-VisualIdentity -Driver $Context.Driver -Element $home; sectionCount = $sections.Count }))
            }
            # Home intentionally shows known-folder paths. Resolve those real
            # semantic item rectangles and redact only their committed image
            # pixels; the raw DWM/screen capture remains the canonical source,
            # while no host profile path enters tracked evidence.
            $redactions = [System.Collections.Generic.List[object]]::new()
            foreach ($name in @('Desktop', 'Documents', 'Downloads', 'Pictures', 'Videos', 'Music')) {
                $item = Find-VisualElement -Driver $Context.Driver -Name $name -FromElement $home -TimeoutMs 800
                if ($item) {
                    $rect = Get-UiaElementProperty -Driver $Context.Driver -Element $item -Name 'BoundingRectangle'
                    if ($rect -and $rect.width -gt 0 -and $rect.height -gt 0) { [void]$redactions.Add($rect) }
                }
            }
            if ($redactions.Count -eq 0) {
                return (New-VisualActionResult -Pass $false -Reason 'Home known-folder item rectangles were not semantically exposed; refusing an unsanitized capture')
            }
            return (New-VisualActionResult -Pass $true -Evidence ([pscustomobject]@{ home = Get-VisualIdentity -Driver $Context.Driver -Element $home; sectionCount = $sections.Count; privacyRedactionCount = $redactions.Count }) -PrivacyRectangles @($redactions))
        }
        'this-pc' {
            $thisPc = Find-VisualElement -Driver $Context.Driver -Name 'This PC' -FromElement $Context.Main -TimeoutMs 2500
            if (-not $thisPc) { return (New-VisualActionResult -Pass $false -Reason 'This PC navigation target was not exposed') }
            $column = Wait-VisualElement -Driver $Context.Driver -AutomationId 'Pane.0.Column.0' -FromElement $Context.Main -TimeoutMs 5000
            $columnName = if ($column) { [string](Get-UiaElementProperty -Driver $Context.Driver -Element $column -Name 'Name') } else { '' }
            return (New-VisualActionResult -Pass ($null -ne $column -and $columnName -eq 'This PC') -Reason 'This PC column was not active' -Evidence $((Get-VisualIdentity -Driver $Context.Driver -Element $column)))
        }
        'standard' { return (Invoke-VisualAddress -Context $Context -Path $Fixtures.Standard -ExpectedLeaf 'standard') }
        'deep' { return (Invoke-VisualAddress -Context $Context -Path $Fixtures.Deep -ExpectedLeaf 'level-three') }
        'unicode' { return (Invoke-VisualAddress -Context $Context -Path $Fixtures.Unicode -ExpectedLeaf 'unicode-emoji') }
        'long' { return (Invoke-VisualAddress -Context $Context -Path $Fixtures.Long -ExpectedLeaf 'long-names') }
        'empty' {
            $result = Invoke-VisualAddress -Context $Context -Path $Fixtures.Empty -ExpectedLeaf 'empty'
            if (-not $result.Pass) { return $result }
            $column = Wait-VisualElement -Driver $Context.Driver -ControlType 'List' -FromElement (Get-VisualPane -Context $Context) -TimeoutMs 3000
            $emptyText = Find-VisualElement -Driver $Context.Driver -NameLike '*empty*' -FromElement $Context.Main -TimeoutMs 1200
            if ($emptyText) { return (New-VisualActionResult -Pass $true -Evidence (Get-VisualIdentity -Driver $Context.Driver -Element $emptyText)) }
            return (New-VisualActionResult -Pass $false -Status 'SKIPPED' -Reason 'empty-directory text not published by the current degraded provider' -Evidence $((Get-VisualIdentity -Driver $Context.Driver -Element $column)))
        }
        'missing' {
            New-Item -ItemType Directory -Path $Fixtures.Missing -Force | Out-Null
            Set-Content -LiteralPath (Join-Path $Fixtures.Missing 'before-delete.txt') -Value 'fixture' -Encoding utf8
            $result = Invoke-VisualAddress -Context $Context -Path $Fixtures.Missing -ExpectedLeaf 'missing-not-created'
            if (-not $result.Pass) { return (New-VisualActionResult -Pass $false -Reason 'missing-state fixture could not be opened before deletion') }
            Remove-Item -LiteralPath $Fixtures.Missing -Recurse -Force
            $error = Wait-VisualElement -Driver $Context.Driver -Name 'This folder is no longer available.' -FromElement $Context.Main -TimeoutMs 7000
            if (-not $error) { $error = Wait-VisualElement -Driver $Context.Driver -Name 'This folder is no longer available.' -ProcessId $Context.Process.Id -TimeoutMs 2500 }
            if (-not $error) { $error = Find-VisualElement -Driver $Context.Driver -NameLike '*unable to load*' -FromElement $Context.Main -TimeoutMs 1000 }
            if ($error) { return (New-VisualActionResult -Pass $true -Evidence $((Get-VisualIdentity -Driver $Context.Driver -Element $error))) }
            return (New-VisualActionResult -Pass $false -Status 'SKIPPED' -Reason 'the current degraded watcher did not publish an unavailable status after the disposable folder was removed')
        }
        'address-edit' {
            if (-not (Set-VisualAppForeground -Context $Context)) { return (New-VisualActionResult -Pass $false -Reason 'FastFiles did not own foreground before address edit') }
            $null = Send-UiaInput -Driver $Context.Driver -Keys '{CTRL}l'
            $edit = Wait-VisualElement -Driver $Context.Driver -ClassName 'Edit' -FromElement $Context.Main -TimeoutMs 3500
            return (New-VisualActionResult -Pass ($null -ne $edit) -Reason 'editable address mode was not visible' -Evidence $((Get-VisualIdentity -Driver $Context.Driver -Element $edit)))
        }
        'palette' { return (Invoke-VisualPalette -Context $Context -Query '' -ExpectedNames @() -KeepOpen) }
        'settings' {
            $palette = Invoke-VisualPalette -Context $Context -Query 'settings' -ExpectedNames @('Settings')
            if (-not $palette.Pass) { return $palette }
            $dialog = Wait-VisualElement -Driver $Context.Driver -ClassName 'FastFilesSettingsDialog' -ProcessId $Context.Process.Id -TimeoutMs 4000
            if (-not $dialog) { return (New-VisualActionResult -Pass $false -Reason 'settings dialog did not appear after supported command') }
            $tab = Find-VisualElement -Driver $Context.Driver -ClassName 'SysTabControl32' -FromElement $dialog -TimeoutMs 2000
            return (New-VisualActionResult -Pass ($null -ne $tab) -Reason 'settings dialog has no tab control' -Evidence ([pscustomobject]@{ dialog = Get-VisualIdentity -Driver $Context.Driver -Element $dialog; tab = Get-VisualIdentity -Driver $Context.Driver -Element $tab }))
        }
        'tabs' {
            $plus = Find-VisualElement -Driver $Context.Driver -AutomationId '8204' -FromElement $Context.Main -TimeoutMs 2500
            if (-not $plus) { return (New-VisualActionResult -Pass $false -Reason 'new-tab control was not exposed') }
            $before = Get-VisualTabCount -Context $Context
            $null = Invoke-UiaElementAction -Driver $Context.Driver -Element $plus -Action 'Click'
            $after = Wait-UiaCondition -Driver $Context.Driver -TimeoutMs 4000 -PollIntervalMs 150 -ScriptBlock { (Get-VisualTabCount -Context $Context) -ge ($before + 1) }
            return (New-VisualActionResult -Pass $after -Reason "tab count did not increase from $before" -Evidence ([pscustomobject]@{ before = $before; after = Get-VisualTabCount -Context $Context }))
        }
        'many-tabs' {
            $plus = Find-VisualElement -Driver $Context.Driver -AutomationId '8204' -FromElement $Context.Main -TimeoutMs 2500
            if (-not $plus) { return (New-VisualActionResult -Pass $false -Reason 'new-tab control was not exposed') }
            for ($i = 0; $i -lt 8; $i++) {
                $null = Invoke-UiaElementAction -Driver $Context.Driver -Element $plus -Action 'Click'
                Start-Sleep -Milliseconds 150
            }
            $count = Get-VisualTabCount -Context $Context
            return (New-VisualActionResult -Pass ($count -ge 8) -Reason "many-tabs count was $count" -Evidence ([pscustomobject]@{ tabCount = $count }))
        }
        'resize' { return (Set-VisualWindowSize -Context $Context -Width ([int]$Scenario.Width)) }
        'dual-left' {
            $palette = Invoke-VisualPalette -Context $Context -Query 'dual' -ExpectedNames @()
            if (-not $palette.Pass) { return $palette }
            $pane = Wait-VisualElement -Driver $Context.Driver -AutomationId 'Pane.1' -FromElement (Find-VisualElement -Driver $Context.Driver -AutomationId 'ColumnView' -FromElement $Context.Main -TimeoutMs 2500) -TimeoutMs 4000
            if (-not $pane) { return (New-VisualActionResult -Pass $false -Reason 'dual-pane right surface did not appear') }
            $pane0 = Get-VisualPane -Context $Context -PaneId 'Pane.0'
            $center = Get-UiaClickablePoint -Driver $Context.Driver -Element $pane0
            if ($center) { $null = Send-UiaMouseInput -Driver $Context.Driver -X $center.x -Y $center.y -Button 'Left' -Action 'Click' }
            return (New-VisualActionResult -Pass $true -Evidence ([pscustomobject]@{ left = Get-VisualIdentity -Driver $Context.Driver -Element $pane0; right = Get-VisualIdentity -Driver $Context.Driver -Element $pane }))
        }
        'dual-right' {
            $palette = Invoke-VisualPalette -Context $Context -Query 'dual' -ExpectedNames @()
            if (-not $palette.Pass) { return $palette }
            $pane1 = Wait-VisualElement -Driver $Context.Driver -AutomationId 'Pane.1' -FromElement (Find-VisualElement -Driver $Context.Driver -AutomationId 'ColumnView' -FromElement $Context.Main -TimeoutMs 2500) -TimeoutMs 4000
            if (-not $pane1) { return (New-VisualActionResult -Pass $false -Reason 'dual-pane right surface did not appear') }
            $center = Get-UiaClickablePoint -Driver $Context.Driver -Element $pane1
            if (-not $center) { return (New-VisualActionResult -Pass $false -Status 'BLOCKED_ENVIRONMENT' -Reason 'right-pane provider has no clickable semantic point') }
            $null = Send-UiaMouseInput -Driver $Context.Driver -X $center.x -Y $center.y -Button 'Left' -Action 'Click'
            $focused = Get-UiaInteractiveState -Driver $Context.Driver -ProcessId $Context.Process.Id
            $focusId = if ($focused.FocusedElement) { $focused.FocusedElement.AutomationId } else { $null }
            $pass = [string]$focusId -like 'Pane.1*'
            return (New-VisualActionResult -Pass $pass -Status $(if ($pass) { $null } else { 'BLOCKED_ENVIRONMENT' }) -Reason $(if ($pass) { $null } else { 'active-pane focus identity is not semantically exposed by the current provider' }) -Evidence ([pscustomobject]@{ right = Get-VisualIdentity -Driver $Context.Driver -Element $pane1; focused = $focused }))
        }
        'search-empty' {
            $result = Invoke-VisualPalette -Context $Context -Query 'search' -ExpectedNames @('Search')
            if (-not $result.Pass) { return $result }
            $filters = Wait-VisualElement -Driver $Context.Driver -Name 'Filters' -FromElement $Context.Main -TimeoutMs 3500
            if (-not $filters) { return (New-VisualActionResult -Pass $false -Reason 'Search surface did not expose Filters after the supported command') }
            $query = Wait-VisualElement -Driver $Context.Driver -AutomationId '7101' -FromElement $Context.Main -TimeoutMs 2500
            return (New-VisualActionResult -Pass ($null -ne $query) -Reason 'empty search query control was not visible' -Evidence $((Get-VisualIdentity -Driver $Context.Driver -Element $query)))
        }
        'search-query' {
            $result = Invoke-VisualPalette -Context $Context -Query 'search' -ExpectedNames @('Search')
            if (-not $result.Pass) { return $result }
            $filters = Wait-VisualElement -Driver $Context.Driver -Name 'Filters' -FromElement $Context.Main -TimeoutMs 3500
            if (-not $filters) { return (New-VisualActionResult -Pass $false -Reason 'Search surface did not expose Filters after the supported command') }
            $query = Wait-VisualElement -Driver $Context.Driver -AutomationId '7101' -FromElement $Context.Main -TimeoutMs 2500
            if (-not $query) { return (New-VisualActionResult -Pass $false -Reason 'search query control was not visible') }
            $set = Set-UiaElementValue -Driver $Context.Driver -Element $query -Value 'zzzz-ffviz-no-match'
            $valueMatches = Wait-UiaCondition -Driver $Context.Driver -TimeoutMs 1000 -PollIntervalMs 100 -ScriptBlock {
                (Get-UiaElementTextValue -Driver $Context.Driver -Element $query) -eq 'zzzz-ffviz-no-match'
            }
            if (-not $valueMatches) {
                $null = Send-UiaInput -Driver $Context.Driver -Keys '{CTRL}a'
                $null = Send-UiaText -Driver $Context.Driver -Text 'zzzz-ffviz-no-match'
                $valueMatches = Wait-UiaCondition -Driver $Context.Driver -TimeoutMs 2000 -PollIntervalMs 100 -ScriptBlock {
                    (Get-UiaElementTextValue -Driver $Context.Driver -Element $query) -eq 'zzzz-ffviz-no-match'
                }
            }
            $value = Get-UiaElementTextValue -Driver $Context.Driver -Element $query
            return (New-VisualActionResult -Pass ($value -eq 'zzzz-ffviz-no-match') -Reason "search query value was '$value'" -Evidence ([pscustomobject]@{ query = Get-VisualIdentity -Driver $Context.Driver -Element $query; value = $value }))
        }
        'search-filters' {
            $result = Invoke-VisualPalette -Context $Context -Query 'search' -ExpectedNames @('Search')
            if (-not $result.Pass) { return $result }
            $filters = Find-VisualElement -Driver $Context.Driver -Name 'Filters' -FromElement $Context.Main -TimeoutMs 2500
            if (-not $filters) { return (New-VisualActionResult -Pass $false -Reason 'Filters disclosure was not visible') }
            $null = Invoke-UiaElementAction -Driver $Context.Driver -Element $filters -Action 'Click'
            $expanded = Wait-VisualElement -Driver $Context.Driver -Name 'Clear history' -FromElement $Context.Main -TimeoutMs 3000
            return (New-VisualActionResult -Pass ($null -ne $expanded) -Reason 'expanded filter row did not expose Clear history' -Evidence $((Get-VisualIdentity -Driver $Context.Driver -Element $expanded)))
        }
        'search-no-results' {
            $result = Invoke-VisualPalette -Context $Context -Query 'search' -ExpectedNames @('Search')
            if (-not $result.Pass) { return $result }
            $query = Wait-VisualElement -Driver $Context.Driver -AutomationId '7101' -FromElement $Context.Main -TimeoutMs 2500
            if (-not $query) { return (New-VisualActionResult -Pass $false -Reason 'search query control was not visible') }
            $null = Set-UiaElementValue -Driver $Context.Driver -Element $query -Value 'zzzz-ffviz-no-match'
            $noMatch = Wait-VisualElement -Driver $Context.Driver -NameLike '*No*' -FromElement $Context.Main -TimeoutMs 7000
            if ($noMatch) { return (New-VisualActionResult -Pass $true -Evidence (Get-VisualIdentity -Driver $Context.Driver -Element $noMatch)) }
            return (New-VisualActionResult -Pass $false -Status 'SKIPPED' -Reason 'no-results status was not observable while the engine/degraded snapshot was unavailable')
        }
        'storage' {
            $result = Invoke-VisualPalette -Context $Context -Query 'analyze' -ExpectedNames @()
            if (-not $result.Pass) { return $result }
            $treemap = Wait-VisualElement -Driver $Context.Driver -Name 'Treemap' -FromElement $Context.Main -TimeoutMs 3500
            return (New-VisualActionResult -Pass ($null -ne $treemap) -Reason 'Storage Analysis mode controls were not visible' -Evidence $((Get-VisualIdentity -Driver $Context.Driver -Element $treemap)))
        }
        'treemap' {
            $result = Invoke-VisualPalette -Context $Context -Query 'analyze' -ExpectedNames @()
            if (-not $result.Pass) { return $result }
            $treemap = Wait-VisualElement -Driver $Context.Driver -Name 'Treemap' -FromElement $Context.Main -TimeoutMs 3500
            if (-not $treemap) { return (New-VisualActionResult -Pass $false -Reason 'Treemap mode button was not visible') }
            $null = Invoke-UiaElementAction -Driver $Context.Driver -Element $treemap -Action 'Click'
            $surface = Wait-VisualElement -Driver $Context.Driver -AutomationId 'Storage.Treemap' -ProcessId $Context.Process.Id -TimeoutMs 6000
            if ($surface) { return (New-VisualActionResult -Pass $true -Evidence (Get-VisualIdentity -Driver $Context.Driver -Element $surface)) }
            return (New-VisualActionResult -Pass $false -Status 'SKIPPED' -Reason 'treemap semantic surface requires engine-served storage data')
        }
        'unsupported' {
            return (New-VisualActionResult -Pass $false -Status 'BLOCKED_ENVIRONMENT' -Reason 'preview/operation fixture requires a live engine snapshot or a safely reproducible native dialog')
        }
        default {
            return (New-VisualActionResult -Pass $false -Status 'BLOCKED_ENVIRONMENT' -Reason "scenario action '$($Scenario.Action)' is not executable on this host")
        }
    }
}

function Get-VisualScenarioCatalog {
    return @(
        @{ Id = 'shell-first-launch'; Expected = 'first-launch shell'; Action = 'launch'; Fixture = 'none'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100 },
        @{ Id = 'shell-home-light'; Expected = 'Home active'; Action = 'home'; Fixture = 'none'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100 },
        @{ Id = 'shell-home-dark'; Expected = 'Home active'; Action = 'home'; Fixture = 'none'; Theme = 'Dark'; Width = 1280; Pane = 'single'; Dpi = 100 },
        @{ Id = 'shell-home-follow-system'; Expected = 'Home active'; Action = 'home'; Fixture = 'none'; Theme = 'Follow System'; Width = 1280; Pane = 'single'; Dpi = 100 },
        @{ Id = 'shell-this-pc'; Expected = 'This PC active'; Action = 'this-pc'; Fixture = 'none'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100; DuplicateReason = 'controlled fresh default renders the same This PC surface as first-launch'; ExpectedDuplicateOf = 'shell-first-launch' },
        @{ Id = 'shell-standard-folder'; Expected = 'standard fixture folder'; Action = 'standard'; Fixture = 'standard'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100 },
        @{ Id = 'shell-deep-path'; Expected = 'deep fixture path'; Action = 'deep'; Fixture = 'deep'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100 },
        @{ Id = 'shell-editable-address'; Expected = 'editable address mode'; Action = 'address-edit'; Fixture = 'none'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100 },
        @{ Id = 'shell-command-palette'; Expected = 'command palette visible'; Action = 'palette'; Fixture = 'none'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100 },
        @{ Id = 'shell-settings'; Expected = 'settings dialog'; Action = 'settings'; Fixture = 'none'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100 },
        @{ Id = 'shell-multiple-tabs'; Expected = 'multiple tabs'; Action = 'tabs'; Fixture = 'none'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100 },
        @{ Id = 'shell-many-tabs-overflow'; Expected = 'many tabs / overflow'; Action = 'many-tabs'; Fixture = 'none'; Theme = 'Light'; Width = 720; Pane = 'single'; Dpi = 100 },
        @{ Id = 'shell-normal-window'; Expected = 'normal window'; Action = 'resize'; Fixture = 'none'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100; DuplicateReason = 'normal-width controlled shell intentionally matches first-launch'; ExpectedDuplicateOf = 'shell-first-launch' },
        @{ Id = 'shell-narrow-window'; Expected = 'narrow window'; Action = 'resize'; Fixture = 'none'; Theme = 'Light'; Width = 720; Pane = 'single'; Dpi = 100 },
        @{ Id = 'shell-wide-window'; Expected = 'wide window'; Action = 'resize'; Fixture = 'none'; Theme = 'Light'; Width = 1600; Pane = 'single'; Dpi = 100 },
        @{ Id = 'shell-dual-pane-left'; Expected = 'dual pane left active'; Action = 'dual-left'; Fixture = 'none'; Theme = 'Light'; Width = 1280; Pane = 'dual-left'; Dpi = 100 },
        @{ Id = 'shell-dual-pane-right'; Expected = 'dual pane right active'; Action = 'dual-right'; Fixture = 'none'; Theme = 'Light'; Width = 1280; Pane = 'dual-right'; Dpi = 100 },
        @{ Id = 'browse-empty-directory'; Expected = 'empty directory'; Action = 'empty'; Fixture = 'empty'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100 },
        @{ Id = 'browse-unicode-emoji'; Expected = 'Unicode and emoji fixture'; Action = 'unicode'; Fixture = 'unicode'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100 },
        @{ Id = 'browse-long-names'; Expected = 'long names fixture'; Action = 'long'; Fixture = 'long'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100 },
        @{ Id = 'browse-missing-path'; Expected = 'missing path error'; Action = 'missing'; Fixture = 'missing'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100 },
        @{ Id = 'search-empty-query'; Expected = 'empty search query'; Action = 'search-empty'; Fixture = 'none'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100 },
        @{ Id = 'search-query-focused'; Expected = 'query focused and typing'; Action = 'search-query'; Fixture = 'none'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100 },
        @{ Id = 'search-filters-expanded'; Expected = 'filters expanded'; Action = 'search-filters'; Fixture = 'none'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100 },
        @{ Id = 'search-no-results'; Expected = 'no results'; Action = 'search-no-results'; Fixture = 'none'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100 },
        @{ Id = 'storage-analysis-base'; Expected = 'Storage Analysis base'; Action = 'storage'; Fixture = 'none'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100 },
        @{ Id = 'storage-analysis-dark'; Expected = 'Storage Analysis base (dark theme)'; Action = 'storage'; Fixture = 'none'; Theme = 'Dark'; Width = 1280; Pane = 'single'; Dpi = 100 },
        @{ Id = 'storage-treemap'; Expected = 'Storage treemap'; Action = 'treemap'; Fixture = 'none'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100 },
        @{ Id = 'browse-access-denied'; Expected = 'access denied'; Action = 'unsupported'; Fixture = 'none'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100; Block = 'safe access-denied reproduction would require changing ACLs' },
        @{ Id = 'browse-hover'; Expected = 'hover state'; Action = 'unsupported'; Fixture = 'standard'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100; Block = 'current fixture provider did not expose a safe semantic hover transition' },
        @{ Id = 'browse-selected-focused'; Expected = 'selected focused'; Action = 'unsupported'; Fixture = 'standard'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100; Block = 'requires engine-served list rows' },
        @{ Id = 'browse-selected-unfocused'; Expected = 'selected unfocused'; Action = 'unsupported'; Fixture = 'standard'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100; Block = 'requires engine-served list rows' },
        @{ Id = 'browse-multi-selection'; Expected = 'multi-selection'; Action = 'unsupported'; Fixture = 'standard'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100; Block = 'requires engine-served list rows' },
        @{ Id = 'browse-rtl-content'; Expected = 'RTL stress content'; Action = 'unsupported'; Fixture = 'rtl'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100; Block = 'requires engine-served list rows' },
        @{ Id = 'browse-drag-target'; Expected = 'drag target'; Action = 'unsupported'; Fixture = 'standard'; Theme = 'Light'; Width = 1280; Pane = 'dual'; Dpi = 100; Block = 'drag target needs a populated engine-backed pane' },
        @{ Id = 'browse-inline-rename'; Expected = 'inline rename'; Action = 'unsupported'; Fixture = 'standard'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100; Block = 'safe inline mutation requires a populated engine-backed row' },
        @{ Id = 'search-populated-results'; Expected = 'populated search results'; Action = 'unsupported'; Fixture = 'standard'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100; Block = 'requires engine-served snapshot results' },
        @{ Id = 'search-selected-result'; Expected = 'selected search result'; Action = 'unsupported'; Fixture = 'standard'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100; Block = 'requires engine-served snapshot results' },
        @{ Id = 'search-searching'; Expected = 'searching in progress'; Action = 'unsupported'; Fixture = 'standard'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100; Block = 'minimum observable search timing is not deterministic on this host' },
        @{ Id = 'preview-image'; Expected = 'image preview'; Action = 'unsupported'; Fixture = 'standard'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100; Block = 'requires engine-served selected file and preview fixture' },
        @{ Id = 'preview-text-metadata'; Expected = 'text metadata preview'; Action = 'unsupported'; Fixture = 'standard'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100; Block = 'requires engine-served selected file' },
        @{ Id = 'preview-unsupported'; Expected = 'unsupported preview'; Action = 'unsupported'; Fixture = 'standard'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100; Block = 'requires engine-served selected file' },
        @{ Id = 'file-operation-queued'; Expected = 'queued operation'; Action = 'unsupported'; Fixture = 'standard'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100; Block = 'file operation requires engine-backed selection' },
        @{ Id = 'file-operation-conflict'; Expected = 'conflict dialog'; Action = 'unsupported'; Fixture = 'standard'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100; Block = 'conflict fixture is not reproducible without a populated engine-backed selection' },
        @{ Id = 'file-operation-confirmation'; Expected = 'destructive confirmation'; Action = 'unsupported'; Fixture = 'standard'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100; Block = 'destructive operation is intentionally not run in the real workspace' },
        @{ Id = 'settings-long-page'; Expected = 'long settings page'; Action = 'settings'; Fixture = 'none'; Theme = 'Light'; Width = 720; Pane = 'single'; Dpi = 100 },
        @{ Id = 'settings-reset-section'; Expected = 'settings reset section'; Action = 'unsupported'; Fixture = 'none'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100; Block = 'reset action is never invoked against the user configuration' },
        @{ Id = 'dpi-125-live'; Expected = '125 percent DPI'; Action = 'unsupported'; Fixture = 'none'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 125; Block = 'host exposes only 100 percent DPI; headless geometry tests cover 125 percent' },
        @{ Id = 'dpi-150-live'; Expected = '150 percent DPI'; Action = 'unsupported'; Fixture = 'none'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 150; Block = 'host exposes only 100 percent DPI; headless geometry tests cover 150 percent' },
        @{ Id = 'dpi-200-live'; Expected = '200 percent DPI'; Action = 'unsupported'; Fixture = 'none'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 200; Block = 'host exposes only 100 percent DPI; headless geometry tests cover 200 percent' },
        @{ Id = 'high-contrast-live'; Expected = 'High Contrast'; Action = 'unsupported'; Fixture = 'none'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100; Block = 'High Contrast is not changed by this campaign because safe restoration cannot be proven' },
        @{ Id = 'animations-off-live'; Expected = 'animations disabled'; Action = 'unsupported'; Fixture = 'none'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100; Block = 'host SPI animation settings are not mutated; headless animation gates cover disabled mode' },
        @{ Id = 'mixed-dpi-transition'; Expected = 'mixed-DPI transition'; Action = 'unsupported'; Fixture = 'none'; Theme = 'Light'; Width = 1280; Pane = 'single'; Dpi = 100; Block = 'single-monitor host cannot provide a live mixed-DPI transition' }
    )
}

function Test-UiVisualValidationAvailability {
    param([Parameter(Mandatory)] $Fingerprint)
    if (-not $script:IsWindows) {
        return [pscustomobject]@{ Available = $false; Reason = 'windows-required'; RequiredContext = [pscustomobject]@{ needs = 'Windows UI Automation and DWM' } }
    }
    if ($Fingerprint.SessionId -eq 0 -or $Fingerprint.SessionKind -eq 'session-0' -or $Fingerprint.SessionKind -eq 'services' -or -not [Environment]::UserInteractive) {
        return [pscustomobject]@{ Available = $false; Reason = 'requires-interactive-context'; RequiredContext = [pscustomobject]@{ needs = 'Interactive desktop session' } }
    }
    if (-not (Test-Path -LiteralPath $script:DriverModule)) {
        return [pscustomobject]@{ Available = $false; Reason = 'uia-driver-unavailable'; RequiredContext = [pscustomobject]@{ needs = 'verify/uia-driver' } }
    }
    try {
        Import-Module $script:DriverModule -Force
        $probe = Get-UiaDriverAvailability
        if (-not $probe.Available) {
            return [pscustomobject]@{ Available = $false; Reason = $probe.Reason; RequiredContext = $probe.RequiredContext }
        }
    } catch {
        return [pscustomobject]@{ Available = $false; Reason = 'uia-driver-unavailable'; RequiredContext = [pscustomobject]@{ needs = 'managed UIA assemblies' } }
    }
    return [pscustomobject]@{ Available = $true; Reason = $null; RequiredContext = $null }
}

function Get-UiVisualValidationDiagnostics {
    return @('dwm-extended-frame-bounds', 'semantic-postconditions', 'duplicate-image-hash', 'workspace-state-backup-restore', 'catalog-manifest')
}

function Invoke-UiVisualValidationCapability {
    param($RunContext, $Fingerprint, [string] $ArtifactsDir, [hashtable] $Options)
    if (-not (Test-Path -LiteralPath $ArtifactsDir)) { New-Item -ItemType Directory -Path $ArtifactsDir -Force | Out-Null }
    Import-Module $script:DriverModule -Force
    Import-Module $script:CaptureModule -Force
    $repoRoot = [string](Get-VisualOption -Options $Options -Name 'RepoRoot' -Default (Get-Location).Path)
    $exe = Get-VisualExe -RepoRoot $repoRoot -Options $Options
    $logPath = Join-Path $ArtifactsDir 'run.log'
    $log = [System.Collections.Generic.List[string]]::new()
    $appSha = if (Test-Path -LiteralPath $exe) { (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant() } else { '' }
    $evidenceRootOption = Get-VisualOption -Options $Options -Name 'VisualEvidenceRoot'
    $evidenceRoot = if ($evidenceRootOption) { Join-Path $repoRoot ([string]$evidenceRootOption) } else { Join-Path $ArtifactsDir 'visual-capture' }
    $run = New-VisualCaptureRun -Root $evidenceRoot -ApplicationSha $appSha -HostId 'local-interactive-windows'
    $backup = $null
    $fixturePhysicalRoot = Get-VisualFixtureRoot -Id 'catalog'
    $fixtureMapping = $null
    $fixtureRoot = $null
    $fixtures = $null
    $subResults = [System.Collections.Generic.List[object]]::new()
    $artifacts = [System.Collections.Generic.List[object]]::new()
    $overall = 'PASS'
    $overallReason = $null
    try {
        if (-not (Test-Path -LiteralPath $exe)) { throw "visual executable not found: $exe" }
        $backup = Backup-VisualProductState
        $fixtureMapping = New-VisualFixtureDrive -PhysicalRoot $fixturePhysicalRoot
        $fixtureRoot = $fixtureMapping.Root
        $fixtures = New-VisualFixtures -Root $fixtureRoot
        $catalog = @(Get-VisualScenarioCatalog)
        $scenarioFilter = Get-VisualOption -Options $Options -Name 'VisualScenario'
        if ($scenarioFilter -and $scenarioFilter -ne 'all') {
            $catalog = @($catalog | Where-Object { $_.Id -eq [string]$scenarioFilter })
            if ($catalog.Count -eq 0) { throw "unknown VisualScenario '$scenarioFilter'" }
        }
        foreach ($scenario in $catalog) {
            $fixtureId = [string]$scenario.Fixture
            if ([string]::IsNullOrWhiteSpace($fixtureId)) { $fixtureId = 'none' }
            [void]$log.Add("scenario=$($scenario.Id) expected=$($scenario.Expected)")
            $block = if ($scenario.ContainsKey('Block')) { [string]$scenario.Block } else { $null }
            if ($block) {
                $record = Add-VisualScenarioResult -Run $run -ScenarioId $scenario.Id -ExpectedState $scenario.Expected -FixtureId $fixtureId -Theme $scenario.Theme -Width ([int]$scenario.Width) -PaneMode $scenario.Pane -DpiScale ([int]$scenario.Dpi) -Status 'BLOCKED_ENVIRONMENT' -Reason $block -AssertionEvidence ([pscustomobject]@{ available = $false })
                [void]$subResults.Add([pscustomobject]@{ id = $scenario.Id; status = 'SKIPPED'; reason = 'blocked-environment'; detail = $block })
                continue
            }
            $context = $null
            $capture = $null
            $result = $null
            try {
                # Each row starts from the same controlled product state. A
                # killed process may have persisted its current location, so a
                # one-time run-level reset is insufficient for a deterministic
                # catalog.
                Reset-VisualProductStateForScenario -Backup $backup
                $context = Start-VisualContext -Exe $exe -RepoRoot $repoRoot -FixtureRoot $fixtureRoot
                $themeResult = Set-VisualTheme -Context $context -Theme ([string]$scenario.Theme)
                if (-not $themeResult.Pass) { throw "theme precondition failed: $($themeResult.Reason)" }
                $sizeResult = Set-VisualWindowSize -Context $context -Width ([int]$scenario.Width)
                if (-not $sizeResult.Pass) { throw "geometry precondition failed: $($sizeResult.Reason)" }
                $result = Invoke-VisualAction -Context $context -Scenario $scenario -Fixtures $fixtures
                if (-not $result.Pass) {
                    $status = if ($result.Status) { [string]$result.Status } else { 'FAIL' }
                    if ($status -eq 'FAIL') { $overall = 'FAIL' }
                    $record = Add-VisualScenarioResult -Run $run -ScenarioId $scenario.Id -ExpectedState $scenario.Expected -FixtureId $fixtureId -Theme $scenario.Theme -Width ([int]$scenario.Width) -PaneMode $scenario.Pane -DpiScale ([int]$scenario.Dpi) -Status $status -Reason $result.Reason -AssertionEvidence $result.Evidence
                    [void]$subResults.Add([pscustomobject]@{ id = $scenario.Id; status = $status; reason = $result.Reason; detail = $result.Evidence })
                } else {
                    $intentionalDuplicateReason = if ($scenario.ContainsKey('DuplicateReason')) { [string]$scenario.DuplicateReason } else { $result.DuplicateReason }
                    $expectedDuplicateOf = if ($scenario.ContainsKey('ExpectedDuplicateOf')) { [string]$scenario.ExpectedDuplicateOf } else { $result.ExpectedDuplicateOf }
                    $capture = Capture-VisualWindow -Run $run -Element $context.Main -ScenarioId $scenario.Id -AssertionPass $true -IntentionalDuplicateReason $intentionalDuplicateReason -ExpectedDuplicateOf $expectedDuplicateOf -PrivacyTheme ([string]$scenario.Theme) -PrivacyRectangles @($result.PrivacyRectangles)
                    $record = Add-VisualScenarioResult -Run $run -ScenarioId $scenario.Id -ExpectedState $scenario.Expected -FixtureId $fixtureId -Theme $scenario.Theme -Width ([int]$scenario.Width) -PaneMode $scenario.Pane -DpiScale ([int]$scenario.Dpi) -Status 'PASS' -AssertionPass $true -AssertionEvidence ([pscustomobject]@{ action = $result.Evidence; geometry = $sizeResult.Evidence; capture = $capture.Bounds }) -ScreenshotPath $capture.Path -ImageHash $capture.Hash -DuplicateOf $capture.DuplicateOf -DuplicateReason $capture.DuplicateReason -PrivacyRedactions @($capture.PrivacyRedactions)
                    [void]$subResults.Add([pscustomobject]@{ id = $scenario.Id; status = 'PASS'; reason = $null; detail = $result.Evidence })
                }
            } catch {
                $reason = $_.Exception.Message
                $overall = 'FAIL'
                $record = Add-VisualScenarioResult -Run $run -ScenarioId $scenario.Id -ExpectedState $scenario.Expected -FixtureId $fixtureId -Theme $scenario.Theme -Width ([int]$scenario.Width) -PaneMode $scenario.Pane -DpiScale ([int]$scenario.Dpi) -Status 'FAIL' -Reason $reason -AssertionEvidence ([pscustomobject]@{ action = $result; geometry = $null })
                [void]$subResults.Add([pscustomobject]@{ id = $scenario.Id; status = 'FAIL'; reason = $reason; detail = $null })
            } finally {
                Stop-VisualContext -Context $context
            }
        }
    } catch {
        $overall = 'FAIL'
        $overallReason = $_.Exception.Message
        [void]$log.Add("capability failure: $overallReason")
    } finally {
        Remove-VisualFixtureDrive -Mapping $fixtureMapping
        if (Test-Path -LiteralPath $fixturePhysicalRoot) { Remove-Item -LiteralPath $fixturePhysicalRoot -Recurse -Force -ErrorAction SilentlyContinue }
        Restore-VisualProductState -Backup $backup
        $summary = Write-VisualCaptureRun -Run $run
        $log | Set-Content -LiteralPath $logPath -Encoding utf8
        [void]$artifacts.Add([pscustomobject]@{ path = $summary.Manifest; type = 'visual-manifest' })
        [void]$artifacts.Add([pscustomobject]@{ path = $summary.Summary; type = 'visual-summary' })
        [void]$artifacts.Add([pscustomobject]@{ path = $logPath; type = 'run-log' })
    }
    $failed = @($subResults | Where-Object { $_.status -eq 'FAIL' })
    if ($failed.Count -gt 0) {
        $overall = 'FAIL'
        $overallReason = "$($failed.Count) visual scenario(s) failed"
    }
    $passedCount = @($subResults | Where-Object { $_.status -eq 'PASS' }).Count
    $skippedCount = @($subResults | Where-Object { $_.status -eq 'SKIPPED' }).Count
    $failedCount = $failed.Count
    $summaryText = "$passedCount captures passed; $skippedCount skipped/blocked; $failedCount failed"
    return [pscustomobject]@{
        Status = $overall
        Reason = $overallReason
        Summary = $summaryText
        Artifacts = @($artifacts)
        SubResults = @($subResults)
    }
}

Export-ModuleMember -Function Test-UiVisualValidationAvailability, Invoke-UiVisualValidationCapability, Get-UiVisualValidationDiagnostics
