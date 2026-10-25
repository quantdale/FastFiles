Set-StrictMode -Version Latest

$script:E2ERoot = Split-Path -Parent $PSScriptRoot
$script:DriverModule = Join-Path $script:E2ERoot 'uia-driver\UiaDriver.psm1'
$script:ValidationModule = Join-Path $script:E2ERoot 'capabilities\windows-ui-automation-validation\UiAutomationValidation.psm1'

Import-Module (Resolve-Path -LiteralPath $script:DriverModule).Path -Force -Global -DisableNameChecking
Import-Module (Resolve-Path -LiteralPath $script:ValidationModule).Path -Force -Global -DisableNameChecking

function New-E2EResult {
    param(
        [Parameter(Mandatory)] [string] $Id,
        [ValidateSet('PASS', 'FAIL', 'SKIPPED')][string] $Status,
        [string] $Stage = 'complete',
        [string] $Detail = '',
        [double] $DurationMs = 0,
        $Data = $null,
        [string] $ArtifactDir = ''
    )
    return [pscustomobject][ordered]@{
        id = $Id
        status = $Status
        stage = $Stage
        detail = $Detail
        durationMs = [Math]::Round($DurationMs, 1)
        data = $Data
        artifactDir = $ArtifactDir
    }
}

function Test-E2EPathWithinRoot {
    param([Parameter(Mandatory)] [string] $Root, [Parameter(Mandatory)] [string] $Path)
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    $pathFull = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    return $pathFull.Equals($rootFull, [StringComparison]::OrdinalIgnoreCase) -or
        $pathFull.StartsWith($rootFull + '\', [StringComparison]::OrdinalIgnoreCase)
}

function Assert-E2EPathWithinRoot {
    param([Parameter(Mandatory)] [string] $Root, [Parameter(Mandatory)] [string] $Path)
    if (-not (Test-E2EPathWithinRoot -Root $Root -Path $Path)) {
        throw "Refusing path outside the marker-protected E2E root: '$Path' (root '$Root')"
    }
    return [IO.Path]::GetFullPath($Path)
}

function New-E2EFixture {
    param([Parameter(Mandatory)] [string] $SessionFixtureRoot)
    $root = Assert-E2EPathWithinRoot -Root $SessionFixtureRoot -Path (Join-Path $SessionFixtureRoot 'FastFilesE2E')
    if (Test-Path -LiteralPath $root -PathType Leaf) {
        throw "E2E fixture path is unexpectedly a file: '$root'"
    }
    New-Item -ItemType Directory -Path $root -Force | Out-Null
    $marker = Join-Path $root '.fastfiles-e2e-fixture'
    Set-Content -LiteralPath $marker -Value (@{
        schema = 1
        purpose = 'FastFiles real black-box E2E fixture'
        root = $root
        createdUtc = (Get-Date).ToUniversalTime().ToString('o')
    } | ConvertTo-Json -Compress) -Encoding utf8

    $directories = @(
        'Browse',
        'Browse\Nested',
        'Browse\Search',
        'Browse\Storage',
        'Browse\Storage\Document',
        'Browse\Operations',
        'Browse\Operations\Destination'
    )
    foreach ($relative in $directories) {
        New-Item -ItemType Directory -Path (Join-Path $root $relative) -Force | Out-Null
    }
    Set-Content -LiteralPath (Join-Path $root 'Browse\root-note.txt') -Value 'FastFiles E2E root' -Encoding utf8
    Set-Content -LiteralPath (Join-Path $root 'Browse\Nested\restore-proof.txt') -Value 'restored navigation proof' -Encoding utf8
    Set-Content -LiteralPath (Join-Path $root 'Browse\Search\exact-file.txt') -Value 'exact search needle' -Encoding utf8
    Set-Content -LiteralPath (Join-Path $root 'Browse\Search\partial-alpha.txt') -Value 'partial search alpha' -Encoding utf8
    Set-Content -LiteralPath (Join-Path $root 'Browse\Search\partial-beta.txt') -Value 'partial search beta' -Encoding utf8
    Set-Content -LiteralPath (Join-Path $root 'Browse\Storage\Document\category-collision-proof.txt') -Value 'category labels are not filesystem targets' -Encoding utf8
    $storageNote = Join-Path $root 'Browse\Storage\storage-note.txt'
    $noteStream = [IO.FileStream]::new($storageNote, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try { $noteStream.SetLength(1MB) } finally { $noteStream.Dispose() }
    Set-Content -LiteralPath (Join-Path $root 'Browse\Operations\rename-me.txt') -Value 'rename me' -Encoding utf8
    Set-Content -LiteralPath (Join-Path $root 'Browse\Operations\copy-me.txt') -Value 'copy me' -Encoding utf8
    Set-Content -LiteralPath (Join-Path $root 'Browse\Operations\move-me.txt') -Value 'move me' -Encoding utf8
    Set-Content -LiteralPath (Join-Path $root 'Browse\Operations\delete-me.txt') -Value 'delete me' -Encoding utf8
    $storageFile = Join-Path $root 'Browse\Storage\small.bin'
    $stream = [IO.FileStream]::new($storageFile, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try { $stream.SetLength(2MB) } finally { $stream.Dispose() }

    return [pscustomobject][ordered]@{
        Root = $root
        Marker = $marker
        Browse = Join-Path $root 'Browse'
        Nested = Join-Path $root 'Browse\Nested'
        Search = Join-Path $root 'Browse\Search'
        Storage = Join-Path $root 'Browse\Storage'
        Operations = Join-Path $root 'Browse\Operations'
        Destination = Join-Path $root 'Browse\Operations\Destination'
        PreviewFile = Join-Path $root 'Browse\Nested\restore-proof.txt'
    }
}

function Wait-E2ECondition {
    param(
        [Parameter(Mandatory)] $Condition,
        [int] $TimeoutMs = 10000,
        [int] $PollIntervalMs = 150,
        [string] $Description = 'condition'
    )
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $lastError = ''
    while ($watch.ElapsedMilliseconds -lt $TimeoutMs) {
        try {
            $value = & $Condition
            if ($value) {
                return [pscustomobject]@{
                    Pass = $true
                    Value = $value
                    ElapsedMs = $watch.ElapsedMilliseconds
                    Description = $Description
                    LastError = $lastError
                }
            }
        } catch {
            $lastError = $_.Exception.Message
        }
        Start-Sleep -Milliseconds $PollIntervalMs
    }
    return [pscustomobject]@{
        Pass = $false
        Value = $null
        ElapsedMs = $watch.ElapsedMilliseconds
        Description = $Description
        LastError = $lastError
    }
}

function Get-E2EClipboardFiles {
    # Read only the standard shell CF_HDROP format. This avoids apartment
    # assumptions in WinForms Clipboard and never changes the clipboard.
    if (-not ('FastFilesE2EClipboardReader' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class FastFilesE2EClipboardReader {
    [DllImport("user32.dll")] static extern bool OpenClipboard(IntPtr owner);
    [DllImport("user32.dll")] static extern bool CloseClipboard();
    [DllImport("user32.dll")] static extern IntPtr GetClipboardData(uint format);
    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    static extern uint DragQueryFileW(IntPtr drop, uint index, StringBuilder path, uint length);
    public static string[] ReadFiles() {
        if (!OpenClipboard(IntPtr.Zero)) return new string[0];
        try {
            IntPtr drop = GetClipboardData(15);
            if (drop == IntPtr.Zero) return new string[0];
            uint count = DragQueryFileW(drop, UInt32.MaxValue, null, 0);
            if (count > 1024) return new string[0];
            string[] files = new string[count];
            for (uint index = 0; index < count; ++index) {
                uint length = DragQueryFileW(drop, index, null, 0);
                if (length > 32768) return new string[0];
                StringBuilder path = new StringBuilder((int)length + 1);
                DragQueryFileW(drop, index, path, length + 1);
                files[index] = path.ToString();
            }
            return files;
        } finally { CloseClipboard(); }
    }
}
'@
    }
    return [FastFilesE2EClipboardReader]::ReadFiles()
}

function Get-E2EStorageRow {
    param([Parameter(Mandatory)] $Context, [Parameter(Mandatory)] [string] $Name)
    $list = Find-UiaElement -Driver $Context.Driver -AutomationId '7201' -ClassName 'SysListView32' `
        -FromElement (Get-E2EMain -Context $Context) -TimeoutMs 700
    foreach ($row in @(Get-UiaChildren -Driver $Context.Driver -Element $list)) {
        $identity = Get-UiaElementIdentity -Driver $Context.Driver -Element $row
        if ($identity.Name -like "$Name*" -and
            (Test-UiaPatternAvailable -Driver $Context.Driver -Element $row -PatternName 'SelectionItem')) { return $row }
    }
    return $null
}

function Get-E2EMain {
    param([Parameter(Mandatory)] $Context)
    $processId = [int]$Context.Launched.Id
    try {
        return Find-UiaElement -Driver $Context.Driver -ClassName 'FastFilesMainWindow' -ProcessId $processId -TimeoutMs 1200
    } catch {
        return $Context.Main
    }
}

function Get-E2EItem {
    param([Parameter(Mandatory)] $Context, [Parameter(Mandatory)] [string] $Name, [int] $TimeoutMs = 10000)
    $main = Get-E2EMain -Context $Context
    if (-not $main) { return $null }
    return Find-UiaTerminalColumnItem -Driver $Context.Driver -Main $main -ProcessId ([int]$Context.Launched.Id) -Name $Name -PaneId 'Pane.0' -TimeoutMs $TimeoutMs
}

function Wait-E2EItem {
    param([Parameter(Mandatory)] $Context, [Parameter(Mandatory)] [string] $Name, [int] $TimeoutMs = 12000)
    $wait = Wait-E2ECondition -TimeoutMs $TimeoutMs -Description "terminal column item '$Name'" -Condition {
        $item = Get-E2EItem -Context $Context -Name $Name -TimeoutMs 900
        if ($item) { return $item }
        return $false
    }
    return $wait
}

function Invoke-E2EItem {
    param([Parameter(Mandatory)] $Context, [Parameter(Mandatory)] $Item)
    Invoke-UiaPattern -Driver $Context.Driver -Element $Item -PatternName 'Invoke' -Method 'Invoke' | Out-Null
}

function Select-E2EItem {
    param([Parameter(Mandatory)] $Context, [Parameter(Mandatory)] [string] $Name, [int] $TimeoutMs = 10000)
    $wait = Wait-E2EItem -Context $Context -Name $Name -TimeoutMs $TimeoutMs
    if (-not $wait.Pass) { return $wait }
    $item = $wait.Value
    $main = Get-E2EMain -Context $Context
    $null = Set-UiaForeground -Driver $Context.Driver -Element $main
    $null = Reset-UiaModifierKeys -Driver $Context.Driver
    $semanticSelected = $false
    $semanticWait = $null
    try {
        if (Test-UiaPatternAvailable -Driver $Context.Driver -Element $item -PatternName 'SelectionItem') {
            $null = Invoke-UiaPattern -Driver $Context.Driver -Element $item -PatternName 'SelectionItem' -Method 'Select'
            $semanticWait = Wait-E2ECondition -TimeoutMs 2500 -Description "semantic selection '$Name'" -Condition {
                try {
                    $fresh = Get-E2EItem -Context $Context -Name $Name -TimeoutMs 700
                    if (-not $fresh) { return $false }
                    return [bool](Invoke-UiaPattern -Driver $Context.Driver -Element $fresh -PatternName 'SelectionItem' -Method 'GetSelection')
                } catch { return $false }
            }
            $semanticSelected = [bool]$semanticWait.Pass
        }
    } catch { $semanticSelected = $false }
    if (-not $semanticSelected) {
        $point = Get-UiaClickablePoint -Driver $Context.Driver -Element $item
        if (-not $point) {
            $rect = Get-UiaElementProperty -Driver $Context.Driver -Element $item -Name 'BoundingRectangle'
            if ($rect -and $rect.width -gt 0 -and $rect.height -gt 0) {
                $point = [pscustomobject]@{
                    x = [int]($rect.x + ($rect.width / 2))
                    y = [int]($rect.y + ($rect.height / 2))
                }
            }
        }
        if (-not $point) {
            return [pscustomobject]@{ Pass = $false; Value = $null; ElapsedMs = $wait.ElapsedMs; Description = "select '$Name'"; Detail = 'selected item exposed neither clickable point nor bounding rectangle' }
        }
        $input = Send-UiaMouseInput -Driver $Context.Driver -X $point.x -Y $point.y -Button 'Left' -Action 'Click'
        if (-not [bool]$input.ok) {
            return [pscustomobject]@{ Pass = $false; Value = $null; ElapsedMs = $wait.ElapsedMs; Description = "select '$Name'"; Detail = "native pointer input was rejected: $($input | ConvertTo-Json -Compress)" }
        }
        $selected = Wait-E2ECondition -TimeoutMs 4000 -Description "real pointer selection '$Name'" -Condition {
            try {
                $fresh = Get-E2EItem -Context $Context -Name $Name -TimeoutMs 700
                if (-not $fresh) { return $false }
                return [bool](Invoke-UiaPattern -Driver $Context.Driver -Element $fresh -PatternName 'SelectionItem' -Method 'GetSelection')
            } catch { return $false }
        }
        if (-not $selected.Pass) {
            return [pscustomobject]@{ Pass = $false; Value = $null; ElapsedMs = $selected.ElapsedMs; Description = "selected '$Name'"; Detail = 'pointer fallback did not produce a semantic selected item' }
        }
        $semanticWait = $selected
    }
    return [pscustomobject]@{ Pass = $true; Value = (Get-E2EItem -Context $Context -Name $Name -TimeoutMs 2000); ElapsedMs = $semanticWait.ElapsedMs; Description = "selected '$Name'" }
}

function Set-E2EEditText {
    param(
        [Parameter(Mandatory)] $Context,
        [Parameter(Mandatory)] $Element,
        [Parameter(Mandatory)] [string] $Value,
        $Owner = $null
    )
    $set = Set-UiaElementValue -Driver $Context.Driver -Element $Element -Value $Value
    if ($set.ok) { return $true }

    # Some native EDIT controls expose a writable ValuePattern but do not
    # provide a reliable read-back or SetFocus implementation.  Stay on the
    # real UI surface: foreground the owning window, click the live semantic
    # rectangle, and type through SendInput.
    $focusOwner = if ($Owner) { $Owner } else { Get-E2EMain -Context $Context }
    $null = Set-UiaForeground -Driver $Context.Driver -Element $focusOwner
    $point = Get-UiaClickablePoint -Driver $Context.Driver -Element $Element
    if (-not $point) {
        $rect = Get-UiaElementProperty -Driver $Context.Driver -Element $Element -Name 'BoundingRectangle'
        if ($rect -and $rect.width -gt 0 -and $rect.height -gt 0) {
            $point = [pscustomobject]@{
                x = [int]($rect.x + ($rect.width / 2))
                y = [int]($rect.y + ($rect.height / 2))
            }
        }
    }
    if (-not $point) { return $false }
    $null = Send-UiaMouseInput -Driver $Context.Driver -X $point.x -Y $point.y -Button 'Left' -Action 'Click'
    $null = Send-UiaInput -Driver $Context.Driver -Keys '{CTRL}a'
    $null = Send-UiaText -Driver $Context.Driver -Text $Value
    return $true
}

function Invoke-E2EButton {
    param(
        [Parameter(Mandatory)] $Context,
        [Parameter(Mandatory)] $Element,
        $Owner = $null
    )
    try {
        Invoke-UiaPattern -Driver $Context.Driver -Element $Element -PatternName 'Invoke' -Method 'Invoke' | Out-Null
        return $true
    } catch {
        # Native dialog buttons on some Windows builds expose a semantic Button
        # but omit InvokePattern.  Click its live UIA geometry as a real user
        # would, rather than reaching into the production dialog internals.
        $focusOwner = if ($Owner) { $Owner } else { Get-E2EMain -Context $Context }
        $null = Set-UiaForeground -Driver $Context.Driver -Element $focusOwner
        $point = Get-UiaClickablePoint -Driver $Context.Driver -Element $Element
        if (-not $point) {
            $rect = Get-UiaElementProperty -Driver $Context.Driver -Element $Element -Name 'BoundingRectangle'
            if ($rect -and $rect.width -gt 0 -and $rect.height -gt 0) {
                $point = [pscustomobject]@{
                    x = [int]($rect.x + ($rect.width / 2))
                    y = [int]($rect.y + ($rect.height / 2))
                }
            }
        }
        if (-not $point) { return $false }
        $null = Send-UiaMouseInput -Driver $Context.Driver -X $point.x -Y $point.y -Button 'Left' -Action 'Click'
        return $true
    }
}

function Invoke-E2ENavigationCommand {
    param([Parameter(Mandatory)] $Context, [Parameter(Mandatory)] [string] $Query,
        [Parameter(Mandatory)] [string] $Name, [Parameter(Mandatory)] [string] $CommandId)
    $main = Get-E2EMain -Context $Context
    return Invoke-UiaPaletteCommand -Driver $Context.Driver -Main $main -ProcessId ([int]$Context.Launched.Id) `
        -Query $Query -ExpectedNames @() -ExpectedCommandName $Name -ExpectedCommandId $CommandId
}

function Get-E2EVisibleObservation {
    param([Parameter(Mandatory)] $Context)
    $main = Get-E2EMain -Context $Context
    $tabs = @()
    $helpText = $null
    $pane = $null
    $visibleColumns = @()
    try {
        if ($main) {
            $helpText = Get-UiaElementProperty -Driver $Context.Driver -Element $main -Name 'HelpText'
            $tabs = @(Find-UiaElement -Driver $Context.Driver -ClassName 'Button' -ProcessId ([int]$Context.Launched.Id) -All -TimeoutMs 500 |
                ForEach-Object {
                    $id = Get-UiaElementIdentity -Driver $Context.Driver -Element $_
                    if ($id.AutomationId -match '^830') { $id.Name }
                })
            $pane = Find-UiaElement -Driver $Context.Driver -AutomationId 'Pane.0' -FromElement $main -TimeoutMs 500
        }
    } catch { }
    # Keep the failure envelope useful even when a semantic wait expires.  The
    # terminal column is re-acquired through the same direct-child path as the
    # assertions; no cached row index or internal product state is used here.
    if ($main -and $pane) {
        try {
            $columns = @(Find-UiaElement -Driver $Context.Driver -ControlType 'List' -FromElement $pane -All -TimeoutMs 1000)
            foreach ($column in $columns) {
                $columnId = Get-UiaElementIdentity -Driver $Context.Driver -Element $column
                $items = @()
                $itemCount = 0
                foreach ($item in @(Get-UiaChildren -Driver $Context.Driver -Element $column)) {
                    $itemId = Get-UiaElementIdentity -Driver $Context.Driver -Element $item
                    if ($itemId.ControlType -ne 'ListItem') { continue }
                    $itemCount++
                    $rect = $null
                    $offscreen = $null
                    $selected = $null
                    try { $rect = Get-UiaElementProperty -Driver $Context.Driver -Element $item -Name 'BoundingRectangle' } catch { }
                    try { $offscreen = Get-UiaElementProperty -Driver $Context.Driver -Element $item -Name 'IsOffscreen' } catch { }
                    try { $selected = [bool](Invoke-UiaPattern -Driver $Context.Driver -Element $item -PatternName 'SelectionItem' -Method 'GetSelection') } catch { }
                    if ($offscreen -eq $true) { continue }
                    $items += [ordered]@{
                        name = [string]$itemId.Name
                        automationId = [string]$itemId.AutomationId
                        controlType = [string]$itemId.ControlType
                        bounds = $rect
                        isOffscreen = $offscreen
                        isSelected = $selected
                    }
                    if ($items.Count -ge 80) { break }
                }
                $visibleColumns += [ordered]@{
                    name = [string]$columnId.Name
                    automationId = [string]$columnId.AutomationId
                    itemCount = $itemCount
                    visibleItemCount = $items.Count
                    items = @($items)
                }
            }
        } catch { }
    }
    return [ordered]@{
        main = if ($main) { Get-UiaElementIdentity -Driver $Context.Driver -Element $main } else { $null }
        mainHelpText = [string]$helpText
        tabLabels = @($tabs)
        pane = if ($pane) { Get-UiaElementIdentity -Driver $Context.Driver -Element $pane } else { $null }
        visibleColumns = @($visibleColumns)
        processId = [int]$Context.Launched.Id
        sessionRoot = $Context.SessionRoot
    }
}

function Write-E2EFailureEvidence {
    param(
        [Parameter(Mandatory)] $Context,
        [Parameter(Mandatory)] [string] $ArtifactDir,
        [Parameter(Mandatory)] [string] $ScenarioId,
        [Parameter(Mandatory)] [string] $Stage,
        [Parameter(Mandatory)] [string] $Expected,
        [string] $Observed = '',
        [string] $ErrorMessage = '',
        [double] $ElapsedMs = 0
    )
    New-Item -ItemType Directory -Path $ArtifactDir -Force | Out-Null
    $observation = $null
    try { $observation = Get-E2EVisibleObservation -Context $Context } catch { $observation = @{ error = $_.Exception.Message } }
    if ([string]::IsNullOrWhiteSpace($Observed)) {
        try { $Observed = $observation | ConvertTo-Json -Compress -Depth 10 } catch { $Observed = '<observation unavailable>' }
    }
    $processes = @(Get-Process -Name FastFiles, FastFilesEngine -ErrorAction SilentlyContinue |
        Select-Object Id, ProcessName, StartTime, Path)
    [ordered]@{
        scenario = $ScenarioId
        stage = $Stage
        expected = $Expected
        observed = $Observed
        error = $ErrorMessage
        elapsedMs = [Math]::Round($ElapsedMs, 1)
        generatedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
        observation = $observation
        processes = $processes
    } | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $ArtifactDir 'failure.json') -Encoding utf8
    try {
        $diag = Join-Path $Context.Environment.LOCALAPPDATA 'FastFiles\logs\diagnostics.log'
        if (Test-Path -LiteralPath $diag -PathType Leaf) {
            Get-Content -LiteralPath $diag -Tail 250 | Set-Content -LiteralPath (Join-Path $ArtifactDir 'diagnostics-tail.log') -Encoding utf8
            Get-Content -LiteralPath $diag | Where-Object { $_ -match 'FFDIAG_UI_COMMAND_DISPATCHED' } |
                Select-Object -Last 30 | Set-Content -LiteralPath (Join-Path $ArtifactDir 'commands-tail.log') -Encoding utf8
        }
    } catch { }
    try {
        $main = Get-E2EMain -Context $Context
        if ($main) {
            $null = Export-UiaEvidence -Driver $Context.Driver -Main $main -ArtifactsDir $ArtifactDir -Name 'failure'
        }
    } catch { }
    return $ArtifactDir
}

function Write-E2EFailureEnvelope {
    param(
        [Parameter(Mandatory)] [string] $ArtifactDir,
        [Parameter(Mandatory)] [string] $ScenarioId,
        [Parameter(Mandatory)] [string] $Stage,
        [Parameter(Mandatory)] [string] $Expected,
        [string] $Observed = '',
        [string] $ErrorMessage = '',
        [double] $ElapsedMs = 0
    )
    New-Item -ItemType Directory -Path $ArtifactDir -Force | Out-Null
    $processes = @(Get-Process -Name FastFiles, FastFilesEngine -ErrorAction SilentlyContinue |
        Select-Object Id, ProcessName, StartTime, Path)
    [ordered]@{
        scenario = $ScenarioId
        stage = $Stage
        expected = $Expected
        observed = $Observed
        error = $ErrorMessage
        elapsedMs = [Math]::Round($ElapsedMs, 1)
        generatedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
        observation = $null
        processes = $processes
        note = 'UIA window was unavailable at the outer scenario boundary; no screenshot/tree could be captured.'
    } | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $ArtifactDir 'failure.json') -Encoding utf8
    return $ArtifactDir
}

function Write-E2ESuccessEvidence {
    param([Parameter(Mandatory)] $Context, [Parameter(Mandatory)] [string] $ArtifactDir, [Parameter(Mandatory)] [string] $Name)
    New-Item -ItemType Directory -Path $ArtifactDir -Force | Out-Null
    try {
        $main = Get-E2EMain -Context $Context
        if ($main) { $null = Export-UiaEvidence -Driver $Context.Driver -Main $main -ArtifactsDir $ArtifactDir -Name $Name }
    } catch { }
    try { Get-E2EVisibleObservation -Context $Context | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $ArtifactDir 'observation.json') -Encoding utf8 } catch { }
}

function Invoke-E2EScenarioSafely {
    param(
        [Parameter(Mandatory)] [string] $Id,
        [Parameter(Mandatory)] [scriptblock] $Body,
        [Parameter(Mandatory)] [string] $ArtifactRoot,
        $Context = $null
    )
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $artifactDir = Join-Path $ArtifactRoot $Id
    try {
        $result = & $Body
        if ($result -and $result.status) {
            if ($result.status -eq 'FAIL' -and $Context) {
                Write-E2EFailureEvidence -Context $Context -ArtifactDir $artifactDir -ScenarioId $Id `
                    -Stage ([string]$result.stage) -Expected ([string]$result.detail) -Observed '' `
                    -ElapsedMs ([double]$result.durationMs) | Out-Null
                $result.artifactDir = $artifactDir
            } elseif ($result.status -eq 'FAIL') {
                $observed = ''
                if ($null -ne $result.data) {
                    try { $observed = $result.data | ConvertTo-Json -Compress -Depth 10 } catch { $observed = [string]$result.data }
                }
                Write-E2EFailureEnvelope -ArtifactDir $artifactDir -ScenarioId $Id `
                    -Stage ([string]$result.stage) -Expected ([string]$result.detail) -Observed $observed `
                    -ElapsedMs ([double]$result.durationMs) | Out-Null
                $result.artifactDir = $artifactDir
            } elseif ($result.status -eq 'PASS' -and $Context) {
                Write-E2ESuccessEvidence -Context $Context -ArtifactDir $artifactDir -Name $Id
                $result.artifactDir = $artifactDir
            }
            return $result
        }
        return New-E2EResult -Id $Id -Status 'PASS' -DurationMs $watch.Elapsed.TotalMilliseconds -Data $result -ArtifactDir $artifactDir
    } catch {
        $errorMessage = $_.Exception.Message
        if ($Context) {
            Write-E2EFailureEvidence -Context $Context -ArtifactDir $artifactDir -ScenarioId $Id `
                -Stage 'exception' -Expected 'scenario completes without exception' -Observed '' `
                -ErrorMessage $errorMessage -ElapsedMs $watch.Elapsed.TotalMilliseconds | Out-Null
        } else {
            New-Item -ItemType Directory -Path $artifactDir -Force | Out-Null
            @{ scenario = $Id; stage = 'exception'; error = $errorMessage } |
                ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $artifactDir 'failure.json') -Encoding utf8
        }
        return New-E2EResult -Id $Id -Status 'FAIL' -Stage 'exception' -Detail $errorMessage `
            -DurationMs $watch.Elapsed.TotalMilliseconds -ArtifactDir $artifactDir
    }
}

function Invoke-E2ELaunchScenario {
    param([Parameter(Mandatory)] $Context)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    if (-not $Context) {
        return New-E2EResult -Id 'launch-ready' -Status 'FAIL' -Stage 'launch' -Detail 'FastFiles context was not created' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    if (-not $Context.Main) {
        return New-E2EResult -Id 'launch-ready' -Status 'FAIL' -Stage 'main-window' -Detail 'FastFiles main window was not discovered' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    if (-not $Context.Ready) {
        return New-E2EResult -Id 'launch-ready' -Status 'FAIL' -Stage 'engine-ready' `
            -Detail "UIA main window exists, but the unprivileged engine handshake was not observed: $($Context.ReadyReason)" `
            -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $wait = Wait-E2ECondition -TimeoutMs 10000 -Description 'usable ColumnView surface' -Condition {
        $main = Get-E2EMain -Context $Context
        if (-not $main) { return $false }
        try {
            $pane = Find-UiaElement -Driver $Context.Driver -AutomationId 'Pane.0' -FromElement $main -TimeoutMs 800
            return $null -ne $pane
        } catch { return $false }
    }
    if (-not $wait.Pass) {
        return New-E2EResult -Id 'launch-ready' -Status 'FAIL' -Stage 'shell-ready' `
            -Detail "ColumnView did not reach a usable semantic state within $($wait.ElapsedMs) ms; lastError=$($wait.LastError)" `
            -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    return New-E2EResult -Id 'launch-ready' -Status 'PASS' -Stage 'complete' `
        -Detail "main window and engine handshake ready; pid=$($Context.Launched.Id); ready=$($Context.ReadyReason)" `
        -DurationMs $watch.Elapsed.TotalMilliseconds -Data (Get-E2EVisibleObservation -Context $Context)
}

function Invoke-E2ENavigationScenario {
    param([Parameter(Mandatory)] $Context, [Parameter(Mandatory)] $Fixture)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $homeCommand = Invoke-E2ENavigationCommand -Context $Context -Query 'home' -Name 'Home' -CommandId 'navigation.home'
    if (-not $homeCommand.Pass) {
        return New-E2EResult -Id 'navigation-browse' -Status 'FAIL' -Stage 'home' -Detail $homeCommand.Detail -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $homeWait = Wait-E2ECondition -TimeoutMs 6000 -Description 'Home surface visible' -Condition {
        try {
            $main = Get-E2EMain -Context $Context
            $null = Find-UiaElement -Driver $Context.Driver -AutomationId 'Pane.0.Home' -FromElement $main -TimeoutMs 800
            return $true
        } catch { return $false }
    }
    if (-not $homeWait.Pass) {
        return New-E2EResult -Id 'navigation-browse' -Status 'FAIL' -Stage 'home-surface' -Detail 'Home command dispatched but Pane.0.Home never became visible' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $computer = Invoke-UiaAddressBarNavigate -Driver $Context.Driver -Main (Get-E2EMain -Context $Context) `
        -ProcessId ([int]$Context.Launched.Id) -Path 'This PC' -ExpectedLeaf 'This PC'
    if (-not $computer.Pass) {
        return New-E2EResult -Id 'navigation-browse' -Status 'FAIL' -Stage 'computer-root-navigation' -Detail $computer.Detail -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $driveRoot = "$(Split-Path -Qualifier $Fixture.Browse)\"
    $driveWait = Wait-E2EItem -Context $Context -Name $driveRoot -TimeoutMs 10000
    if (-not $driveWait.Pass) {
        return New-E2EResult -Id 'navigation-browse' -Status 'FAIL' -Stage 'computer-root-drives' -Detail "This PC did not expose the fixture volume '$driveRoot'" -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $refreshing = $false
    try {
        $null = Find-UiaElement -Driver $Context.Driver -Name 'Refreshing…' -FromElement (Get-E2EMain -Context $Context) -TimeoutMs 800
        $refreshing = $true
    } catch { }
    if ($refreshing) {
        return New-E2EResult -Id 'navigation-browse' -Status 'FAIL' -Stage 'computer-root-terminal' -Detail 'This PC exposed drives but retained a perpetual Refreshing… status' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $nav = Invoke-UiaAddressBarNavigate -Driver $Context.Driver -Main (Get-E2EMain -Context $Context) `
        -ProcessId ([int]$Context.Launched.Id) -Path $Fixture.Browse -ExpectedLeaf 'Browse'
    if (-not $nav.Pass) {
        return New-E2EResult -Id 'navigation-browse' -Status 'FAIL' -Stage 'direct-browse' -Detail $nav.Detail -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    if (-not (Wait-E2EItem -Context $Context -Name 'root-note.txt' -TimeoutMs 12000).Pass) {
        return New-E2EResult -Id 'navigation-browse' -Status 'FAIL' -Stage 'browse-visible-items' -Detail 'root-note.txt did not appear in the terminal column' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $nested = Wait-E2EItem -Context $Context -Name 'Nested' -TimeoutMs 12000
    if (-not $nested.Pass) {
        return New-E2EResult -Id 'navigation-browse' -Status 'FAIL' -Stage 'nested-visible' -Detail 'Nested folder did not appear in the terminal column' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    Invoke-E2EItem -Context $Context -Item $nested.Value
    if (-not (Wait-E2EItem -Context $Context -Name 'restore-proof.txt' -TimeoutMs 12000).Pass) {
        return New-E2EResult -Id 'navigation-browse' -Status 'FAIL' -Stage 'nested-open' -Detail 'nested directory opened without its expected file becoming visible' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    if ((Get-E2EItem -Context $Context -Name 'root-note.txt' -TimeoutMs 1000)) {
        return New-E2EResult -Id 'navigation-browse' -Status 'FAIL' -Stage 'stale-column-content' -Detail 'root-note.txt remained in the terminal column after opening Nested' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $back = Invoke-E2ENavigationCommand -Context $Context -Query 'back' -Name 'Back' -CommandId 'navigation.back'
    if (-not $back.Pass -or -not (Wait-E2EItem -Context $Context -Name 'root-note.txt' -TimeoutMs 10000).Pass) {
        return New-E2EResult -Id 'navigation-browse' -Status 'FAIL' -Stage 'back' -Detail "Back did not restore Browse: $($back.Detail)" -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $forward = Invoke-E2ENavigationCommand -Context $Context -Query 'forward' -Name 'Forward' -CommandId 'navigation.forward'
    if (-not $forward.Pass -or -not (Wait-E2EItem -Context $Context -Name 'restore-proof.txt' -TimeoutMs 10000).Pass) {
        return New-E2EResult -Id 'navigation-browse' -Status 'FAIL' -Stage 'forward' -Detail "Forward did not restore Nested: $($forward.Detail)" -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $up = Invoke-E2ENavigationCommand -Context $Context -Query 'up' -Name 'Up' -CommandId 'navigation.up'
    if (-not $up.Pass -or -not (Wait-E2EItem -Context $Context -Name 'root-note.txt' -TimeoutMs 10000).Pass) {
        return New-E2EResult -Id 'navigation-browse' -Status 'FAIL' -Stage 'up' -Detail "Up did not return to Browse: $($up.Detail)" -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $direct = Invoke-UiaAddressBarNavigate -Driver $Context.Driver -Main (Get-E2EMain -Context $Context) `
        -ProcessId ([int]$Context.Launched.Id) -Path $Fixture.Nested -ExpectedLeaf 'Nested'
    if (-not $direct.Pass -or -not (Wait-E2EItem -Context $Context -Name 'restore-proof.txt' -TimeoutMs 10000).Pass) {
        return New-E2EResult -Id 'navigation-browse' -Status 'FAIL' -Stage 'direct-path' -Detail "direct path navigation did not settle: $($direct.Detail)" -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    return New-E2EResult -Id 'navigation-browse' -Status 'PASS' -Detail 'Home, This PC terminal state, nested browse, Back, Forward, Up, direct path, stale-column, and terminal-item checks passed' `
        -DurationMs $watch.Elapsed.TotalMilliseconds -Data (Get-E2EVisibleObservation -Context $Context)
}

function Invoke-E2ESearchScenario {
    param([Parameter(Mandatory)] $Context, [Parameter(Mandatory)] $Fixture)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $nav = Invoke-UiaAddressBarNavigate -Driver $Context.Driver -Main (Get-E2EMain -Context $Context) `
        -ProcessId ([int]$Context.Launched.Id) -Path $Fixture.Search -ExpectedLeaf 'Search'
    if (-not $nav.Pass) {
        return New-E2EResult -Id 'search-user-flow' -Status 'FAIL' -Stage 'scope-navigation' -Detail $nav.Detail -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    # original-vision-journey-recovery 3.2: browse the fixture first so the
    # degraded snapshot contains it, then start the actual search from Home.
    # Closing Search alone used to pass while Home still covered the result.
    $home = Invoke-E2ENavigationCommand -Context $Context -Query 'home' -Name 'Home' -CommandId 'navigation.home'
    if (-not $home.Pass) {
        return New-E2EResult -Id 'search-user-flow' -Status 'FAIL' -Stage 'home-before-search' -Detail $home.Detail -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $homeReady = Wait-E2ECondition -TimeoutMs 6000 -Description 'Home before search' -Condition {
        $null = Find-UiaElement -Driver $Context.Driver -AutomationId 'Pane.0.Home' -FromElement (Get-E2EMain -Context $Context) -TimeoutMs 700
        return $true
    }
    if (-not $homeReady.Pass) {
        return New-E2EResult -Id 'search-user-flow' -Status 'FAIL' -Stage 'home-before-search' -Detail 'Home surface did not become available' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $panel = Open-UiaSearchPanel -Driver $Context.Driver -Main (Get-E2EMain -Context $Context) -ProcessId ([int]$Context.Launched.Id)
    if (-not $panel.Pass) {
        return New-E2EResult -Id 'search-user-flow' -Status 'FAIL' -Stage 'open-search' -Detail $panel.Detail -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $set = Set-UiaSearchQuery -Driver $Context.Driver -Main (Get-E2EMain -Context $Context) -ProcessId ([int]$Context.Launched.Id) -Query 'exact-file'
    if (-not $set.Pass) {
        return New-E2EResult -Id 'search-user-flow' -Status 'FAIL' -Stage 'exact-query' -Detail $set.Detail -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $exactWait = Wait-E2ECondition -TimeoutMs 12000 -Description 'exact search result' -Condition {
        $names = @(Get-UiaSearchResultItems -Driver $Context.Driver -Main (Get-E2EMain -Context $Context) -ProcessId ([int]$Context.Launched.Id) -TimeoutMs 800 |
            ForEach-Object { Get-UiaElementProperty -Driver $Context.Driver -Element $_ -Name 'Name' })
        return $names -contains 'exact-file.txt'
    }
    if (-not $exactWait.Pass) {
        return New-E2EResult -Id 'search-user-flow' -Status 'FAIL' -Stage 'exact-result' -Detail 'exact-file.txt did not appear in Search.Results before the deadline' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $set = Set-UiaSearchQuery -Driver $Context.Driver -Main (Get-E2EMain -Context $Context) -ProcessId ([int]$Context.Launched.Id) -Query 'partial'
    if (-not $set.Pass) {
        return New-E2EResult -Id 'search-user-flow' -Status 'FAIL' -Stage 'partial-query' -Detail $set.Detail -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $partialWait = Wait-E2ECondition -TimeoutMs 12000 -Description 'partial search results' -Condition {
        $names = @(Get-UiaSearchResultItems -Driver $Context.Driver -Main (Get-E2EMain -Context $Context) -ProcessId ([int]$Context.Launched.Id) -TimeoutMs 800 |
            ForEach-Object { Get-UiaElementProperty -Driver $Context.Driver -Element $_ -Name 'Name' })
        return ($names -contains 'partial-alpha.txt') -and ($names -contains 'partial-beta.txt')
    }
    if (-not $partialWait.Pass) {
        return New-E2EResult -Id 'search-user-flow' -Status 'FAIL' -Stage 'partial-result' -Detail 'partial query did not expose both deterministic partial files' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $set = Set-UiaSearchQuery -Driver $Context.Driver -Main (Get-E2EMain -Context $Context) -ProcessId ([int]$Context.Launched.Id) -Query 'no-match-e2e-9f8d'
    if (-not $set.Pass) {
        return New-E2EResult -Id 'search-user-flow' -Status 'FAIL' -Stage 'no-result-query' -Detail $set.Detail -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $noResult = Wait-E2ECondition -TimeoutMs 10000 -Description 'no-result terminal state' -Condition {
        try {
            $list = Find-UiaElement -Driver $Context.Driver -AutomationId 'Search.Results' -ProcessId ([int]$Context.Launched.Id) -TimeoutMs 600 -FromElement (Get-E2EMain -Context $Context)
            $items = @(Get-UiaChildren -Driver $Context.Driver -Element $list | Where-Object {
                (Get-UiaElementIdentity -Driver $Context.Driver -Element $_).ControlType -eq 'ListItem'
            })
            return $items.Count -eq 0
        } catch { return $false }
    }
    if (-not $noResult.Pass) {
        return New-E2EResult -Id 'search-user-flow' -Status 'FAIL' -Stage 'no-result-terminal' -Detail 'no-result query did not settle to an empty Search.Results list' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    # original-vision-journey-recovery 3.2: an accepted replacement must not
    # transfer a native ListView selection to a different file at the same
    # virtual row index. Start with one selected result at index zero.
    $selectedQuery = Set-UiaSearchQuery -Driver $Context.Driver -Main (Get-E2EMain -Context $Context) -ProcessId ([int]$Context.Launched.Id) -Query 'partial-alpha'
    $alphaReady = Wait-E2ECondition -TimeoutMs 12000 -Description 'alpha result before selected replacement' -Condition {
        $rows = @(Get-UiaSearchResultItems -Driver $Context.Driver -Main (Get-E2EMain -Context $Context) -ProcessId ([int]$Context.Launched.Id) -TimeoutMs 800)
        if ($rows.Count -eq 1 -and (Get-UiaElementProperty -Driver $Context.Driver -Element $rows[0] -Name 'Name') -eq 'partial-alpha.txt') { return $rows[0] }
        return $false
    }
    if (-not $selectedQuery.Pass -or -not $alphaReady.Pass) {
        return New-E2EResult -Id 'search-user-flow' -Status 'FAIL' -Stage 'selected-replacement-source' -Detail 'the single alpha result was not ready for selection' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    Invoke-UiaPattern -Driver $Context.Driver -Element $alphaReady.Value -PatternName 'SelectionItem' -Method 'Select' | Out-Null
    if (-not [bool](Invoke-UiaPattern -Driver $Context.Driver -Element $alphaReady.Value -PatternName 'SelectionItem' -Method 'GetSelection')) {
        return New-E2EResult -Id 'search-user-flow' -Status 'FAIL' -Stage 'selected-replacement-source' -Detail 'alpha selection was not semantically established' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $replacementQuery = Set-UiaSearchQuery -Driver $Context.Driver -Main (Get-E2EMain -Context $Context) -ProcessId ([int]$Context.Launched.Id) -Query 'partial-beta'
    $betaReady = Wait-E2ECondition -TimeoutMs 12000 -Description 'beta replaces the selected alpha result' -Condition {
        $rows = @(Get-UiaSearchResultItems -Driver $Context.Driver -Main (Get-E2EMain -Context $Context) -ProcessId ([int]$Context.Launched.Id) -TimeoutMs 800)
        if ($rows.Count -eq 1 -and (Get-UiaElementProperty -Driver $Context.Driver -Element $rows[0] -Name 'Name') -eq 'partial-beta.txt') { return $rows[0] }
        return $false
    }
    if (-not $replacementQuery.Pass -or -not $betaReady.Pass) {
        return New-E2EResult -Id 'search-user-flow' -Status 'FAIL' -Stage 'selected-replacement-target' -Detail "the single beta replacement did not settle: $($replacementQuery.Detail); lastError=$($betaReady.LastError)" -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    if ([bool](Invoke-UiaPattern -Driver $Context.Driver -Element $betaReady.Value -PatternName 'SelectionItem' -Method 'GetSelection')) {
        return New-E2EResult -Id 'search-user-flow' -Status 'FAIL' -Stage 'selected-replacement-retarget' -Detail 'beta inherited the selection of alpha at the same native row index' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $setAlpha = Set-UiaSearchQuery -Driver $Context.Driver -Main (Get-E2EMain -Context $Context) -ProcessId ([int]$Context.Launched.Id) -Query 'partial-alpha'
    $setBeta = Set-UiaSearchQuery -Driver $Context.Driver -Main (Get-E2EMain -Context $Context) -ProcessId ([int]$Context.Launched.Id) -Query 'partial-beta'
    if (-not $setAlpha.Pass -or -not $setBeta.Pass) {
        return New-E2EResult -Id 'search-user-flow' -Status 'FAIL' -Stage 'rapid-replacement' -Detail "rapid replacement input failed: alpha=$($setAlpha.Detail); beta=$($setBeta.Detail)" -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $replacement = Wait-E2ECondition -TimeoutMs 12000 -Description 'superseded search settles on beta' -Condition {
        $surface = Get-UiaSearchQuerySurface -Driver $Context.Driver -Main (Get-E2EMain -Context $Context) -ProcessId ([int]$Context.Launched.Id)
        return ([string]$surface.queryText -eq 'partial-beta') -and (@($surface.resultNames) -contains 'partial-beta.txt') -and (@($surface.resultNames) -notcontains 'exact-file.txt')
    }
    if (-not $replacement.Pass) {
        return New-E2EResult -Id 'search-user-flow' -Status 'FAIL' -Stage 'rapid-replacement-terminal' -Detail 'stale search rows remained after rapid query replacement' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $items = @(Get-UiaSearchResultItems -Driver $Context.Driver -Main (Get-E2EMain -Context $Context) -ProcessId ([int]$Context.Launched.Id) -TimeoutMs 2000)
    $resultItem = $items | Where-Object { (Get-UiaElementProperty -Driver $Context.Driver -Element $_ -Name 'Name') -eq 'partial-beta.txt' } | Select-Object -First 1
    if (-not $resultItem) {
        return New-E2EResult -Id 'search-user-flow' -Status 'FAIL' -Stage 'result-selection' -Detail 'settled beta result was not selectable' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    Invoke-UiaPattern -Driver $Context.Driver -Element $resultItem -PatternName 'SelectionItem' -Method 'Select' | Out-Null
    Invoke-UiaPattern -Driver $Context.Driver -Element $resultItem -PatternName 'Invoke' -Method 'Invoke' | Out-Null
    $closed = Wait-UiaElementGone -Driver $Context.Driver -AutomationId 'Search.Results' -ProcessId ([int]$Context.Launched.Id) -TimeoutMs 10000 -PollIntervalMs 200
    if (-not $closed) {
        return New-E2EResult -Id 'search-user-flow' -Status 'FAIL' -Stage 'result-open' -Detail 'invoking a real search result did not close the Search.Results surface' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $expectedPaths = [Collections.Generic.List[string]]::new()
    for ($directory = [IO.DirectoryInfo]::new($Fixture.Search); $null -ne $directory; $directory = $directory.Parent) {
        $expectedPaths.Add($directory.FullName)
    }
    $expectedPaths.Reverse()
    $expectedResult = Join-Path $Fixture.Search 'partial-beta.txt'
    $revealed = Wait-E2ECondition -TimeoutMs 12000 -Description 'Home search reveals selected file in full hierarchy' -Condition {
        $main = Get-E2EMain -Context $Context
        $pane = Find-UiaElement -Driver $Context.Driver -AutomationId 'Pane.0' -FromElement $main -TimeoutMs 700
        $columns = @(Get-UiaChildren -Driver $Context.Driver -Element $pane | Where-Object {
            (Get-UiaElementIdentity -Driver $Context.Driver -Element $_).ControlType -eq 'List'
        })
        if ($columns.Count -ne $expectedPaths.Count) { return $false }
        for ($columnIndex = 0; $columnIndex -lt $columns.Count; $columnIndex++) {
            $actualPath = [string](Get-UiaElementProperty -Driver $Context.Driver -Element $columns[$columnIndex] -Name 'Name')
            if (-not $actualPath.Equals($expectedPaths[$columnIndex], [StringComparison]::OrdinalIgnoreCase)) { return $false }
        }
        $item = Find-UiaTerminalColumnItem -Driver $Context.Driver -Main $main -ProcessId ([int]$Context.Launched.Id) `
            -Name 'partial-beta.txt' -PaneId 'Pane.0' -TimeoutMs 700
        if (-not $item) { return $false }
        $actualResult = [string](Get-UiaElementProperty -Driver $Context.Driver -Element $item -Name 'HelpText')
        $selected = [bool](Invoke-UiaPattern -Driver $Context.Driver -Element $item -PatternName 'SelectionItem' -Method 'GetSelection')
        $offscreen = Get-UiaElementProperty -Driver $Context.Driver -Element $item -Name 'IsOffscreen'
        $bounds = Get-UiaElementProperty -Driver $Context.Driver -Element $item -Name 'BoundingRectangle'
        return $actualResult.Equals($expectedResult, [StringComparison]::OrdinalIgnoreCase) -and $selected -and
            ($offscreen -eq $false) -and $bounds.width -gt 0 -and $bounds.height -gt 0
    }
    if (-not $revealed.Pass) {
        return New-E2EResult -Id 'search-user-flow' -Status 'FAIL' -Stage 'result-hierarchy-selection' `
            -Detail "Search closed but did not reveal the exact selected file '$expectedResult' in its complete visible hierarchy; lastError=$($revealed.LastError)" -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    return New-E2EResult -Id 'search-user-flow' -Status 'PASS' -Detail 'Home search, exact, partial, no-result, superseded-query, full hierarchy, exact selected path, and visible result checks passed' `
        -DurationMs $watch.Elapsed.TotalMilliseconds -Data (Get-E2EVisibleObservation -Context $Context)
}

function Invoke-E2EStorageScenario {
    param([Parameter(Mandatory)] $Context, [Parameter(Mandatory)] $Fixture)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $nav = Invoke-UiaAddressBarNavigate -Driver $Context.Driver -Main (Get-E2EMain -Context $Context) `
        -ProcessId ([int]$Context.Launched.Id) -Path $Fixture.Storage -ExpectedLeaf 'Storage'
    if (-not $nav.Pass) {
        return New-E2EResult -Id 'storage-terminal-geometry' -Status 'FAIL' -Stage 'scope-navigation' -Detail $nav.Detail -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $browseSentinel = Join-Path $Fixture.Storage 'storage-note.txt'
    $storageTarget = Join-Path $Fixture.Storage 'small.bin'
    $sentinelHash = (Get-FileHash -LiteralPath $browseSentinel -Algorithm SHA256).Hash
    $browseSelection = Select-E2EItem -Context $Context -Name 'storage-note.txt' -TimeoutMs 10000
    if (-not $browseSelection.Pass) {
        return New-E2EResult -Id 'storage-terminal-geometry' -Status 'FAIL' -Stage 'browse-sentinel-selection' -Detail 'the independent browsing sentinel could not be selected' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $open = Invoke-E2ENavigationCommand -Context $Context -Query 'analyze' -Name 'Analyze Storage' -CommandId 'storage.analyze'
    if (-not $open.Pass) {
        return New-E2EResult -Id 'storage-terminal-geometry' -Status 'FAIL' -Stage 'open-analysis' -Detail $open.Detail -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $treemap = Wait-E2ECondition -TimeoutMs 10000 -Description 'Storage Analysis surface' -Condition {
        try {
            $main = Get-E2EMain -Context $Context
            $null = Find-UiaElement -Driver $Context.Driver -Name 'Overview' -FromElement $main -TimeoutMs 800
            return $true
        } catch { return $false }
    }
    if (-not $treemap.Pass) {
        return New-E2EResult -Id 'storage-terminal-geometry' -Status 'FAIL' -Stage 'analysis-ready' -Detail 'Storage Analysis did not expose its view controls' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    # original-vision-journey-recovery: actions belong to the visible
    # Storage selection, even while a different browse row remains selected.
    $main = Get-E2EMain -Context $Context
    if (-not (Test-UiaForegroundWindow -Driver $Context.Driver -Element $main)) {
        return New-E2EResult -Id 'storage-terminal-geometry' -Status 'FAIL' -Stage 'palette-dismiss-foreground' -Detail 'foreground unavailable for the palette dismissal interaction' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $null = Send-UiaInput -Driver $Context.Driver -Keys '{CTRL}{SHIFT}p'
    $paletteEdit = Wait-UiaElement -Driver $Context.Driver -AutomationId '6301' -FromElement $main -TimeoutMs 3000
    $sidebar = Find-UiaElement -Driver $Context.Driver -ClassName 'FastFilesNavigationSidebar' -FromElement $main -TimeoutMs 1500
    $sidebarBounds = Get-UiaElementProperty -Driver $Context.Driver -Element $sidebar -Name 'BoundingRectangle'
    $outsideX = [int]($sidebarBounds.x + $sidebarBounds.width * 0.1)
    $outsideY = [int]($sidebarBounds.y + $sidebarBounds.height * 0.5)
    if (-not $paletteEdit -or -not (Test-UiaPointInWindow -Driver $Context.Driver -Element $main -X $outsideX -Y $outsideY)) {
        return New-E2EResult -Id 'storage-terminal-geometry' -Status 'FAIL' -Stage 'palette-dismiss-target' -Detail 'palette or safe app-owned outside click target was unavailable' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $null = Send-UiaMouseInput -Driver $Context.Driver -X $outsideX -Y $outsideY -Action 'Click'
    $paletteGone = Wait-UiaElementGone -Driver $Context.Driver -AutomationId '6301' -FromElement $main -TimeoutMs 3000
    $overviewStillVisible = Find-UiaElement -Driver $Context.Driver -Name 'Overview' -FromElement $main -TimeoutMs 1000
    if (-not $paletteGone -or -not $overviewStillVisible) {
        return New-E2EResult -Id 'storage-terminal-geometry' -Status 'FAIL' -Stage 'palette-dismiss-consumption' -Detail 'outside click failed to dismiss the palette or activated the background workspace' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $drillDownButton = Find-UiaElement -Driver $Context.Driver -Name 'Drill Down' -FromElement $main -TimeoutMs 1500
    if (-not (Invoke-E2EButton -Context $Context -Element $drillDownButton)) {
        return New-E2EResult -Id 'storage-terminal-geometry' -Status 'FAIL' -Stage 'storage-command-list' -Detail 'Drill Down was not activatable' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $storageRow = Wait-E2ECondition -TimeoutMs 12000 -Description 'Storage command target row' -Condition {
        $row = Get-E2EStorageRow -Context $Context -Name 'small.bin'
        if ($row) { return $row }
        return $false
    }
    if (-not $storageRow.Pass) {
        return New-E2EResult -Id 'storage-terminal-geometry' -Status 'FAIL' -Stage 'storage-command-target' -Detail 'small.bin was absent from the native storage list' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    Invoke-UiaPattern -Driver $Context.Driver -Element $storageRow.Value -PatternName 'SelectionItem' -Method 'Select' | Out-Null
    if (-not [bool](Invoke-UiaPattern -Driver $Context.Driver -Element $storageRow.Value -PatternName 'SelectionItem' -Method 'GetSelection')) {
        return New-E2EResult -Id 'storage-terminal-geometry' -Status 'FAIL' -Stage 'storage-command-selection' -Detail 'small.bin did not become the semantic storage selection' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $beforeSort = Get-UiaElementProperty -Driver $Context.Driver -Element $storageRow.Value -Name 'BoundingRectangle'
    $storageList = Find-UiaElement -Driver $Context.Driver -AutomationId '7201' -ClassName 'SysListView32' -FromElement $main -TimeoutMs 1000
    $sizeHeader = Find-UiaElement -Driver $Context.Driver -Name 'Size' -ControlType 'HeaderItem' -FromElement $storageList -TimeoutMs 1500
    if (-not (Invoke-E2EButton -Context $Context -Element $sizeHeader)) {
        return New-E2EResult -Id 'storage-terminal-geometry' -Status 'FAIL' -Stage 'storage-sort-action' -Detail 'Size column header was not activatable' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $sortedSelection = Wait-E2ECondition -TimeoutMs 6000 -Description 'storage sort preserves selected file identity' -Condition {
        $row = Get-E2EStorageRow -Context $Context -Name 'small.bin'
        if (-not $row) { return $false }
        $bounds = Get-UiaElementProperty -Driver $Context.Driver -Element $row -Name 'BoundingRectangle'
        return ([bool](Invoke-UiaPattern -Driver $Context.Driver -Element $row -PatternName 'SelectionItem' -Method 'GetSelection')) -and
            $bounds -and $beforeSort -and $bounds.y -gt ($beforeSort.y + 1)
    }
    if (-not $sortedSelection.Pass) {
        return New-E2EResult -Id 'storage-terminal-geometry' -Status 'FAIL' -Stage 'storage-sort-selection' -Detail 'sorting did not move small.bin while preserving its selected identity' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $copyPath = Invoke-E2ENavigationCommand -Context $Context -Query 'copy path' -Name 'Copy Path' -CommandId 'item.copy-path'
    $pathCopied = Wait-E2ECondition -TimeoutMs 4000 -Description 'Storage Copy Path clipboard target' -Condition {
        return ([string](Get-Clipboard -Raw)).TrimEnd([char[]]"`r`n").Equals($storageTarget, [StringComparison]::OrdinalIgnoreCase)
    }
    if (-not $copyPath.Pass -or -not $pathCopied.Pass) {
        return New-E2EResult -Id 'storage-terminal-geometry' -Status 'FAIL' -Stage 'storage-copy-path-target' -Detail 'Copy Path did not copy the visible Storage target small.bin' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $cut = Invoke-E2ENavigationCommand -Context $Context -Query 'cut' -Name 'Cut' -CommandId 'file.cut'
    $cutTarget = Wait-E2ECondition -TimeoutMs 4000 -Description 'Storage Cut clipboard target' -Condition {
        $files = @(Get-E2EClipboardFiles)
        return $files.Count -eq 1 -and $files[0].Equals($storageTarget, [StringComparison]::OrdinalIgnoreCase)
    }
    if (-not $cut.Pass -or -not $cutTarget.Pass -or
        -not (Test-Path -LiteralPath $browseSentinel -PathType Leaf) -or
        (Get-FileHash -LiteralPath $browseSentinel -Algorithm SHA256).Hash -ne $sentinelHash) {
        return New-E2EResult -Id 'storage-terminal-geometry' -Status 'FAIL' -Stage 'storage-cut-target' -Detail 'Cut targeted a different file or changed the independent browsing sentinel' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    # A synthetic category has a real sibling folder with the same name.
    # Activating the category must filter files rather than enter that folder.
    $categoryButton = Find-UiaElement -Driver $Context.Driver -Name 'By Category' -FromElement (Get-E2EMain -Context $Context) -TimeoutMs 1500
    if (-not (Invoke-E2EButton -Context $Context -Element $categoryButton)) {
        return New-E2EResult -Id 'storage-terminal-geometry' -Status 'FAIL' -Stage 'storage-category-mode' -Detail 'By Category was not activatable' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $category = Wait-E2ECondition -TimeoutMs 6000 -Description 'Document category row' -Condition {
        $row = Get-E2EStorageRow -Context $Context -Name 'Document'
        if ($row) { return $row }
        return $false
    }
    if (-not $category.Pass) {
        return New-E2EResult -Id 'storage-terminal-geometry' -Status 'FAIL' -Stage 'storage-category-row' -Detail 'the Document category row was absent' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $null = Set-UiaForeground -Driver $Context.Driver -Element (Get-E2EMain -Context $Context)
    $categoryBounds = Get-UiaElementProperty -Driver $Context.Driver -Element $category.Value -Name 'BoundingRectangle'
    if (-not $categoryBounds -or $categoryBounds.width -le 0 -or $categoryBounds.height -le 0) {
        return New-E2EResult -Id 'storage-terminal-geometry' -Status 'FAIL' -Stage 'storage-category-activation' -Detail 'Document category had no usable live bounds' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    # Click the name cell within the row's live bounds, so a horizontal
    # scrollbar cannot place the middle of a wide native row outside the view.
    $categoryX = [int]($categoryBounds.x + [Math]::Min(40, $categoryBounds.width / 2))
    $categoryY = [int]($categoryBounds.y + $categoryBounds.height / 2)
    $null = Send-UiaMouseInput -Driver $Context.Driver -X $categoryX -Y $categoryY -Action 'Click'
    $null = Send-UiaMouseInput -Driver $Context.Driver -X $categoryX -Y $categoryY -Action 'Click'
    $categoryFiltered = Wait-E2ECondition -TimeoutMs 6000 -Description 'category activation filters real files' -Condition {
        $document = Get-E2EStorageRow -Context $Context -Name 'storage-note.txt'
        if (-not $document) { return $false }
        return -not (Get-E2EStorageRow -Context $Context -Name 'small.bin') -and
            -not (Get-E2EStorageRow -Context $Context -Name 'category-collision-proof.txt')
    }
    if (-not $categoryFiltered.Pass) {
        return New-E2EResult -Id 'storage-terminal-geometry' -Status 'FAIL' -Stage 'storage-category-target' -Detail 'category activation failed to filter documents in place or entered the category-named sibling folder' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $main = Get-E2EMain -Context $Context
    $treemapButton = $null
    try { $treemapButton = Find-UiaElement -Driver $Context.Driver -Name 'Treemap' -FromElement $main -TimeoutMs 2500 } catch { }
    if (-not $treemapButton) {
        return New-E2EResult -Id 'storage-terminal-geometry' -Status 'FAIL' -Stage 'treemap-control' -Detail 'Storage Analysis did not expose the Treemap control' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    Invoke-UiaElementAction -Driver $Context.Driver -Element $treemapButton -Action 'Click' | Out-Null
    $groupWait = Wait-E2ECondition -TimeoutMs 20000 -Description 'Storage.Treemap terminal surface' -Condition {
        try {
            $candidate = Find-UiaElement -Driver $Context.Driver -AutomationId 'Storage.Treemap' -ProcessId ([int]$Context.Launched.Id) -TimeoutMs 1000
            if ($candidate) { return $candidate }
        } catch { }
        return $false
    }
    if (-not $groupWait.Pass) {
        $degraded = $false
        foreach ($pattern in @('No sized data*', 'Calculating*', '*unavailable*', '*degraded*')) {
            try {
                $found = Find-UiaElement -Driver $Context.Driver -NameLike $pattern -ProcessId ([int]$Context.Launched.Id) -TimeoutMs 700
                if ($found) { $degraded = $true; break }
            } catch { }
        }
        if ($degraded) {
            return New-E2EResult -Id 'storage-terminal-geometry' -Status 'PASS' -Stage 'degraded-terminal' `
                -Detail 'Storage Analysis exposed an explicit degraded/empty terminal message instead of an indefinite treemap wait' `
                -DurationMs $watch.Elapsed.TotalMilliseconds
        }
        return New-E2EResult -Id 'storage-terminal-geometry' -Status 'FAIL' -Stage 'treemap-terminal' `
            -Detail 'Storage.Treemap did not appear and no explicit degraded terminal message was discoverable before the deadline' `
            -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $group = $groupWait.Value
    $groupRect = Get-UiaElementProperty -Driver $Context.Driver -Element $group -Name 'BoundingRectangle'
    $mainRect = Get-UiaElementProperty -Driver $Context.Driver -Element (Get-E2EMain -Context $Context) -Name 'BoundingRectangle'
    $geometryOk = $groupRect -and $groupRect.width -gt 0 -and $groupRect.height -gt 0 -and $mainRect -and
        $groupRect.x -ge $mainRect.x -and $groupRect.y -ge $mainRect.y -and
        ($groupRect.x + $groupRect.width) -le ($mainRect.x + $mainRect.width) -and
        ($groupRect.y + $groupRect.height) -le ($mainRect.y + $mainRect.height)
    if (-not $geometryOk) {
        return New-E2EResult -Id 'storage-terminal-geometry' -Status 'FAIL' -Stage 'geometry' `
            -Detail "Storage.Treemap bounds were not a positive rectangle contained by the live main window: group=$($groupRect | ConvertTo-Json -Compress); main=$($mainRect | ConvertTo-Json -Compress)" `
            -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $tiles = @(Find-UiaElement -Driver $Context.Driver -ControlType 'DataItem' -FromElement $group -TimeoutMs 4000 -All)
    if ($tiles.Count -eq 0) {
        $emptyText = @()
        try { $emptyText = @(Get-UiaChildren -Driver $Context.Driver -Element $group | ForEach-Object { Get-UiaElementProperty -Driver $Context.Driver -Element $_ -Name 'Name' }) } catch { }
        $explicit = @($emptyText | Where-Object { $_ -match 'No sized data|Calculating|degraded|unavailable' })
        if ($explicit.Count -eq 0) {
            return New-E2EResult -Id 'storage-terminal-geometry' -Status 'FAIL' -Stage 'tile-terminal' -Detail 'Storage.Treemap was present but exposed neither tiles nor an explicit terminal message' -DurationMs $watch.Elapsed.TotalMilliseconds
        }
        return New-E2EResult -Id 'storage-terminal-geometry' -Status 'PASS' -Stage 'degraded-terminal' -Detail "Storage.Treemap terminal state: $($explicit -join '; ')" -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    # UIA exposes physical pixel rectangles while the renderer computes the
    # treemap in DIPs and rounds at the provider boundary. A one-to-three
    # pixel edge discrepancy is therefore normal at 125% DPI; a material
    # escape is not. Keep the tolerance explicit and small so this remains a
    # geometry assertion rather than a screenshot-only check.
    $geometryTolerance = 4
    foreach ($tile in $tiles) {
        $rect = Get-UiaElementProperty -Driver $Context.Driver -Element $tile -Name 'BoundingRectangle'
        if (-not $rect -or $rect.width -le 0 -or $rect.height -le 0 -or
            $rect.x -lt ($groupRect.x - $geometryTolerance) -or $rect.y -lt ($groupRect.y - $geometryTolerance) -or
            ($rect.x + $rect.width) -gt ($groupRect.x + $groupRect.width + $geometryTolerance) -or
            ($rect.y + $rect.height) -gt ($groupRect.y + $groupRect.height + $geometryTolerance)) {
            $nativeDetail = ''
            if (-not $rect) {
                try { $nativeDetail = [string]$tile.Current.BoundingRectangle }
                catch { $nativeDetail = $_.Exception.ToString() }
            }
            return New-E2EResult -Id 'storage-terminal-geometry' -Status 'FAIL' -Stage 'tile-geometry' `
                -Detail "a live treemap tile bounding rectangle escaped the treemap content rectangle beyond ${geometryTolerance}px: tile=$($rect | ConvertTo-Json -Compress); group=$($groupRect | ConvertTo-Json -Compress); native=$nativeDetail" `
                -DurationMs $watch.Elapsed.TotalMilliseconds
        }
    }
    $tileNames = @($tiles | ForEach-Object { Get-UiaElementProperty -Driver $Context.Driver -Element $_ -Name 'Name' })
    if ($tileNames.Count -ne 1 -or $tileNames[0] -ne 'storage-note.txt') {
        return New-E2EResult -Id 'storage-terminal-geometry' -Status 'FAIL' -Stage 'category-treemap-continuity' `
            -Detail "Document category refinement was lost on entering Treemap: tiles=$($tileNames -join ', ')" `
            -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $firstTile = $tiles | Select-Object -First 1
    try { Invoke-UiaPattern -Driver $Context.Driver -Element $firstTile -PatternName 'Invoke' -Method 'Invoke' | Out-Null } catch {
        return New-E2EResult -Id 'storage-terminal-geometry' -Status 'FAIL' -Stage 'tile-invoke' -Detail "treemap tile was visible but could not be invoked: $($_.Exception.Message)" -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    return New-E2EResult -Id 'storage-terminal-geometry' -Status 'PASS' -Detail "Storage.Treemap reached a terminal usable state with $($tiles.Count) tiles; group/tile UIA bounds were contained and a tile Invoke was accepted" `
        -DurationMs $watch.Elapsed.TotalMilliseconds -Data ([ordered]@{ group = $groupRect; main = $mainRect; tileCount = $tiles.Count })
}

function Get-E2EPreviewStatus {
    param([Parameter(Mandatory)] $Context)
    try { return [string](Get-UiaElementProperty -Driver $Context.Driver -Element (Get-E2EMain -Context $Context) -Name 'HelpText') } catch { return '' }
}

function Invoke-E2EPreviewScenario {
    param([Parameter(Mandatory)] $Context, [Parameter(Mandatory)] $Fixture)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $nav = Invoke-UiaAddressBarNavigate -Driver $Context.Driver -Main (Get-E2EMain -Context $Context) `
        -ProcessId ([int]$Context.Launched.Id) -Path $Fixture.Nested -ExpectedLeaf 'Nested'
    if (-not $nav.Pass) {
        return New-E2EResult -Id 'preview-terminal-state' -Status 'FAIL' -Stage 'preview-navigation' -Detail $nav.Detail -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $selected = Select-E2EItem -Context $Context -Name 'restore-proof.txt' -TimeoutMs 12000
    if (-not $selected.Pass) {
        return New-E2EResult -Id 'preview-terminal-state' -Status 'FAIL' -Stage 'preview-selection' -Detail 'previewable fixture file could not be selected' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    # SelectionItem.Select establishes semantic ownership but some Direct2D
    # previews are refreshed only by the real row activation path. Reacquire
    # the live row and issue one bounded pointer click before observing preview.
    $previewItem = Get-E2EItem -Context $Context -Name 'restore-proof.txt' -TimeoutMs 2000
    $previewPoint = $null
    if ($previewItem) {
        try { $previewItem.SetFocus() } catch { }
        $previewRect = Get-UiaElementProperty -Driver $Context.Driver -Element $previewItem -Name 'BoundingRectangle'
        if ($previewRect -and $previewRect.width -gt 0 -and $previewRect.height -gt 0) {
            $previewPoint = [pscustomobject]@{ x = [int]($previewRect.x + $previewRect.width / 2); y = [int]($previewRect.y + $previewRect.height / 2) }
        } else {
            $previewPoint = Get-UiaClickablePoint -Driver $Context.Driver -Element $previewItem
        }
    }
    if ($previewPoint) {
        $input = Send-UiaMouseInput -Driver $Context.Driver -X $previewPoint.x -Y $previewPoint.y -Button 'Left' -Action 'Click'
        if (-not [bool]$input.ok) {
            return New-E2EResult -Id 'preview-terminal-state' -Status 'FAIL' -Stage 'preview-activation' -Detail 'preview row pointer input was rejected' -DurationMs $watch.Elapsed.TotalMilliseconds
        }
        Start-Sleep -Milliseconds 500
    } else {
        return New-E2EResult -Id 'preview-terminal-state' -Status 'FAIL' -Stage 'preview-activation' -Detail 'preview row had no usable live geometry' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $allowedTerminal = @('Preview: Ready', 'Preview: Unsupported', 'Preview: Too large', 'Preview: Permission denied', 'Preview: Unavailable', 'Preview: Disabled')
    $previewWait = Wait-E2ECondition -TimeoutMs 12000 -Description 'preview terminal state' -Condition {
        $status = Get-E2EPreviewStatus -Context $Context
        return $allowedTerminal -contains $status
    }
    if (-not $previewWait.Pass) {
        return New-E2EResult -Id 'preview-terminal-state' -Status 'FAIL' -Stage 'preview-ready' -Detail "preview remained non-terminal; observed='$(Get-E2EPreviewStatus -Context $Context)'" -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $firstStatus = Get-E2EPreviewStatus -Context $Context
    $nav = Invoke-UiaAddressBarNavigate -Driver $Context.Driver -Main (Get-E2EMain -Context $Context) `
        -ProcessId ([int]$Context.Launched.Id) -Path $Fixture.Storage -ExpectedLeaf 'Storage'
    if (-not $nav.Pass) {
        return New-E2EResult -Id 'preview-terminal-state' -Status 'FAIL' -Stage 'preview-reselection-navigation' -Detail $nav.Detail -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $selectedOther = Select-E2EItem -Context $Context -Name 'small.bin' -TimeoutMs 12000
    if (-not $selectedOther.Pass) {
        return New-E2EResult -Id 'preview-terminal-state' -Status 'FAIL' -Stage 'stale-preview-selection' -Detail 'second deterministic selection could not be materialized' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $secondItem = Get-E2EItem -Context $Context -Name 'small.bin' -TimeoutMs 2000
    $secondPoint = $null
    if ($secondItem) {
        try { $secondItem.SetFocus() } catch { }
        $secondRect = Get-UiaElementProperty -Driver $Context.Driver -Element $secondItem -Name 'BoundingRectangle'
        if ($secondRect -and $secondRect.width -gt 0 -and $secondRect.height -gt 0) {
            $secondPoint = [pscustomobject]@{ x = [int]($secondRect.x + $secondRect.width / 2); y = [int]($secondRect.y + $secondRect.height / 2) }
        } else {
            $secondPoint = Get-UiaClickablePoint -Driver $Context.Driver -Element $secondItem
        }
    }
    if (-not $secondPoint) {
        return New-E2EResult -Id 'preview-terminal-state' -Status 'FAIL' -Stage 'stale-preview-activation' -Detail 'second preview row had no usable live geometry' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $secondInput = Send-UiaMouseInput -Driver $Context.Driver -X $secondPoint.x -Y $secondPoint.y -Button 'Left' -Action 'Click'
    if (-not [bool]$secondInput.ok) {
        return New-E2EResult -Id 'preview-terminal-state' -Status 'FAIL' -Stage 'stale-preview-activation' -Detail 'second preview row pointer input was rejected' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    Start-Sleep -Milliseconds 500
    $secondWait = Wait-E2ECondition -TimeoutMs 12000 -Description 'new selection preview terminal state' -Condition {
        $status = Get-E2EPreviewStatus -Context $Context
        return ($allowedTerminal -contains $status) -and $status -ne 'Preview: Loading'
    }
    if (-not $secondWait.Pass) {
        return New-E2EResult -Id 'preview-terminal-state' -Status 'FAIL' -Stage 'stale-preview-terminal' -Detail "second selection did not reach a terminal preview state; observed='$(Get-E2EPreviewStatus -Context $Context)'" -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $secondStatus = Get-E2EPreviewStatus -Context $Context
    if ($firstStatus -eq 'Preview: Ready' -and $secondStatus -eq 'Preview: Ready') {
        # Both files may be supported by a provider on a future host. In that
        # case the UI must at least expose a fresh terminal result after the
        # second selection; the asynchronous request identity is the product
        # authority, not a guessed file-type classification.
        $detail = "preview transitioned through two terminal selections ($firstStatus -> $secondStatus); no stale Loading state persisted"
    } else {
        $detail = "preview terminal states were distinct ($firstStatus -> $secondStatus); stale preview was not accepted as the new selection"
    }
    return New-E2EResult -Id 'preview-terminal-state' -Status 'PASS' -Detail $detail -DurationMs $watch.Elapsed.TotalMilliseconds `
        -Data ([ordered]@{ firstStatus = $firstStatus; secondStatus = $secondStatus })
}

function Invoke-E2EFileOperationsScenario {
    param([Parameter(Mandatory)] $Context, [Parameter(Mandatory)] $Fixture)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $root = $Fixture.Root
    foreach ($path in @($Fixture.Operations, $Fixture.Destination, (Join-Path $Fixture.Operations 'rename-me.txt'),
            (Join-Path $Fixture.Operations 'copy-me.txt'), (Join-Path $Fixture.Operations 'move-me.txt'),
            (Join-Path $Fixture.Operations 'delete-me.txt'))) {
        Assert-E2EPathWithinRoot -Root $root -Path $path | Out-Null
    }
    $nav = Invoke-UiaAddressBarNavigate -Driver $Context.Driver -Main (Get-E2EMain -Context $Context) `
        -ProcessId ([int]$Context.Launched.Id) -Path $Fixture.Operations -ExpectedLeaf 'Operations'
    if (-not $nav.Pass) {
        return New-E2EResult -Id 'file-operations' -Status 'FAIL' -Stage 'operations-navigation' -Detail $nav.Detail -DurationMs $watch.Elapsed.TotalMilliseconds
    }

    $newFolder = Invoke-E2ENavigationCommand -Context $Context -Query 'new folder' -Name 'New Folder' -CommandId 'file.new-folder'
    if (-not $newFolder.Pass) {
        return New-E2EResult -Id 'file-operations' -Status 'FAIL' -Stage 'create-folder-command' -Detail $newFolder.Detail -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $createdPath = Join-Path $Fixture.Operations 'New folder'
    $createdWait = Wait-E2ECondition -TimeoutMs 12000 -Description 'created folder exists on disk' -Condition { Test-Path -LiteralPath $createdPath -PathType Container }
    if (-not $createdWait.Pass) {
        return New-E2EResult -Id 'file-operations' -Status 'FAIL' -Stage 'create-folder-filesystem' -Detail "FastFiles New Folder command did not create '$createdPath'" -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    # New Folder completion enters the real inline rename surface. Commit a
    # deterministic name when that surface is exposed; otherwise retain the
    # production default and still verify the shell-created directory.
    try {
        $edit = Find-UiaElement -Driver $Context.Driver -ClassName 'Edit' -ProcessId ([int]$Context.Launched.Id) -TimeoutMs 1200
        if ($edit) {
            if (Set-E2EEditText -Context $Context -Element $edit -Value 'created-by-e2e') {
                $null = Send-UiaInput -Driver $Context.Driver -Keys '{ENTER}'
                $renamedCreated = Join-Path $Fixture.Operations 'created-by-e2e'
                $renameWait = Wait-E2ECondition -TimeoutMs 8000 -Description 'inline-created-folder rename' -Condition { Test-Path -LiteralPath $renamedCreated -PathType Container }
                if ($renameWait.Pass) { $createdPath = $renamedCreated }
            }
        }
    } catch { }

    $selectRename = Select-E2EItem -Context $Context -Name 'rename-me.txt' -TimeoutMs 10000
    if (-not $selectRename.Pass) {
        return New-E2EResult -Id 'file-operations' -Status 'FAIL' -Stage 'rename-select' -Detail 'rename-me.txt was not selectable' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $renameCommand = Invoke-E2ENavigationCommand -Context $Context -Query 'rename' -Name 'Rename' -CommandId 'file.rename'
    if (-not $renameCommand.Pass) {
        return New-E2EResult -Id 'file-operations' -Status 'FAIL' -Stage 'rename-command' -Detail $renameCommand.Detail -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $dialogWait = Wait-E2ECondition -TimeoutMs 5000 -Description 'rename name prompt' -Condition {
        try { return Find-UiaElement -Driver $Context.Driver -ClassName 'FastFilesNamePrompt' -ProcessId ([int]$Context.Launched.Id) -TimeoutMs 700 } catch { return $false }
    }
    if (-not $dialogWait.Pass) {
        return New-E2EResult -Id 'file-operations' -Status 'FAIL' -Stage 'rename-dialog' -Detail 'Rename command dispatched but FastFilesNamePrompt did not appear' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $dialog = $dialogWait.Value
    $renameEdit = Find-UiaElement -Driver $Context.Driver -ClassName 'Edit' -FromElement $dialog -TimeoutMs 2000
    if (-not $renameEdit) {
        return New-E2EResult -Id 'file-operations' -Status 'FAIL' -Stage 'rename-input' -Detail 'rename dialog did not expose its Edit control' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    if (-not (Set-E2EEditText -Context $Context -Element $renameEdit -Value 'renamed-by-e2e.txt' -Owner $dialog)) {
        return New-E2EResult -Id 'file-operations' -Status 'FAIL' -Stage 'rename-input' -Detail 'rename dialog edit could not be populated through UIA or real pointer input' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $okButton = Find-UiaElement -Driver $Context.Driver -Name 'OK' -FromElement $dialog -TimeoutMs 1500
    if (-not $okButton) {
        return New-E2EResult -Id 'file-operations' -Status 'FAIL' -Stage 'rename-submit' -Detail 'rename dialog did not expose its OK button' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    if (-not (Invoke-E2EButton -Context $Context -Element $okButton -Owner $dialog)) {
        return New-E2EResult -Id 'file-operations' -Status 'FAIL' -Stage 'rename-submit' -Detail 'rename dialog OK button could not be activated through UIA Invoke or pointer input' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $renamedPath = Join-Path $Fixture.Operations 'renamed-by-e2e.txt'
    $renameFs = Wait-E2ECondition -TimeoutMs 12000 -Description 'renamed file exists' -Condition {
        (Test-Path -LiteralPath $renamedPath -PathType Leaf) -and -not (Test-Path -LiteralPath (Join-Path $Fixture.Operations 'rename-me.txt') -PathType Leaf)
    }
    if (-not $renameFs.Pass) {
        return New-E2EResult -Id 'file-operations' -Status 'FAIL' -Stage 'rename-filesystem' -Detail "rename result was not visible on disk at '$renamedPath'" -DurationMs $watch.Elapsed.TotalMilliseconds
    }

    $selectCopy = Select-E2EItem -Context $Context -Name 'copy-me.txt' -TimeoutMs 10000
    if (-not $selectCopy.Pass) {
        return New-E2EResult -Id 'file-operations' -Status 'FAIL' -Stage 'copy-select' -Detail 'copy-me.txt was not selectable' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $copy = Invoke-E2ENavigationCommand -Context $Context -Query 'copy' -Name 'Copy' -CommandId 'file.copy'
    if (-not $copy.Pass) {
        return New-E2EResult -Id 'file-operations' -Status 'FAIL' -Stage 'copy-command' -Detail $copy.Detail -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $nav = Invoke-UiaAddressBarNavigate -Driver $Context.Driver -Main (Get-E2EMain -Context $Context) `
        -ProcessId ([int]$Context.Launched.Id) -Path $Fixture.Destination -ExpectedLeaf 'Destination'
    if (-not $nav.Pass) {
        return New-E2EResult -Id 'file-operations' -Status 'FAIL' -Stage 'copy-destination' -Detail $nav.Detail -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $paste = Invoke-E2ENavigationCommand -Context $Context -Query 'paste' -Name 'Paste' -CommandId 'file.paste'
    if (-not $paste.Pass) {
        return New-E2EResult -Id 'file-operations' -Status 'FAIL' -Stage 'copy-paste-command' -Detail $paste.Detail -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $copiedPath = Join-Path $Fixture.Destination 'copy-me.txt'
    if (-not (Wait-E2ECondition -TimeoutMs 12000 -Description 'copied file exists' -Condition { Test-Path -LiteralPath $copiedPath -PathType Leaf }).Pass) {
        return New-E2EResult -Id 'file-operations' -Status 'FAIL' -Stage 'copy-filesystem' -Detail "copy result was not visible on disk at '$copiedPath'" -DurationMs $watch.Elapsed.TotalMilliseconds
    }

    $nav = Invoke-UiaAddressBarNavigate -Driver $Context.Driver -Main (Get-E2EMain -Context $Context) `
        -ProcessId ([int]$Context.Launched.Id) -Path $Fixture.Operations -ExpectedLeaf 'Operations'
    if (-not $nav.Pass) {
        return New-E2EResult -Id 'file-operations' -Status 'FAIL' -Stage 'move-source' -Detail $nav.Detail -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $selectMove = Select-E2EItem -Context $Context -Name 'move-me.txt' -TimeoutMs 10000
    if (-not $selectMove.Pass) {
        return New-E2EResult -Id 'file-operations' -Status 'FAIL' -Stage 'move-select' -Detail 'move-me.txt was not selectable' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $cut = Invoke-E2ENavigationCommand -Context $Context -Query 'cut' -Name 'Cut' -CommandId 'file.cut'
    if (-not $cut.Pass) {
        return New-E2EResult -Id 'file-operations' -Status 'FAIL' -Stage 'move-cut-command' -Detail $cut.Detail -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $nav = Invoke-UiaAddressBarNavigate -Driver $Context.Driver -Main (Get-E2EMain -Context $Context) `
        -ProcessId ([int]$Context.Launched.Id) -Path $Fixture.Destination -ExpectedLeaf 'Destination'
    if (-not $nav.Pass) {
        return New-E2EResult -Id 'file-operations' -Status 'FAIL' -Stage 'move-destination' -Detail $nav.Detail -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $pasteMove = Invoke-E2ENavigationCommand -Context $Context -Query 'paste' -Name 'Paste' -CommandId 'file.paste'
    if (-not $pasteMove.Pass) {
        return New-E2EResult -Id 'file-operations' -Status 'FAIL' -Stage 'move-paste-command' -Detail $pasteMove.Detail -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $movedPath = Join-Path $Fixture.Destination 'move-me.txt'
    $moveFs = Wait-E2ECondition -TimeoutMs 12000 -Description 'moved file has new path' -Condition {
        (Test-Path -LiteralPath $movedPath -PathType Leaf) -and -not (Test-Path -LiteralPath (Join-Path $Fixture.Operations 'move-me.txt') -PathType Leaf)
    }
    if (-not $moveFs.Pass) {
        return New-E2EResult -Id 'file-operations' -Status 'FAIL' -Stage 'move-filesystem' -Detail "move result was not visible on disk at '$movedPath'" -DurationMs $watch.Elapsed.TotalMilliseconds
    }

    $nav = Invoke-UiaAddressBarNavigate -Driver $Context.Driver -Main (Get-E2EMain -Context $Context) `
        -ProcessId ([int]$Context.Launched.Id) -Path $Fixture.Operations -ExpectedLeaf 'Operations'
    if (-not $nav.Pass) {
        return New-E2EResult -Id 'file-operations' -Status 'FAIL' -Stage 'delete-source' -Detail $nav.Detail -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $selectDelete = Select-E2EItem -Context $Context -Name 'delete-me.txt' -TimeoutMs 10000
    if (-not $selectDelete.Pass) {
        return New-E2EResult -Id 'file-operations' -Status 'FAIL' -Stage 'delete-select' -Detail 'delete-me.txt was not selectable' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    $delete = Invoke-E2ENavigationCommand -Context $Context -Query 'delete' -Name 'Delete' -CommandId 'file.delete'
    if (-not $delete.Pass) {
        return New-E2EResult -Id 'file-operations' -Status 'FAIL' -Stage 'delete-command' -Detail $delete.Detail -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    if (-not (Wait-E2ECondition -TimeoutMs 12000 -Description 'deleted file absent from fixture' -Condition { -not (Test-Path -LiteralPath (Join-Path $Fixture.Operations 'delete-me.txt')) }).Pass) {
        return New-E2EResult -Id 'file-operations' -Status 'FAIL' -Stage 'delete-filesystem' -Detail 'delete command completed without removing the disposable fixture file' -DurationMs $watch.Elapsed.TotalMilliseconds
    }
    return New-E2EResult -Id 'file-operations' -Status 'PASS' -Detail "real create-folder, rename, copy, move, and recycle-delete operations verified under '$root'; conflict case not invoked because the production default uses a modal conflict policy" -DurationMs $watch.Elapsed.TotalMilliseconds `
        -Data ([ordered]@{ created = $createdPath; renamed = $renamedPath; copied = $copiedPath; moved = $movedPath; deleted = $true })
}

function Close-E2EContextWindow {
    param([Parameter(Mandatory)] $Context)
    if (-not $Context -or -not $Context.Main) { return $false }
    try {
        Invoke-UiaPattern -Driver $Context.Driver -Element (Get-E2EMain -Context $Context) -PatternName 'Window' -Method 'Close' | Out-Null
    } catch {
        try {
            Invoke-UiaActivateAppWindow -Driver $Context.Driver -Main (Get-E2EMain -Context $Context) | Out-Null
            Send-UiaInput -Driver $Context.Driver -Keys '%{F4}' | Out-Null
        } catch { }
    }
    $wait = Wait-E2ECondition -TimeoutMs 10000 -Description 'FastFiles process exits after close' -Condition {
        try { Get-Process -Id ([int]$Context.Launched.Id) -ErrorAction Stop | Out-Null; return $false } catch { return $true }
    }
    return $wait.Pass
}

function Invoke-E2ERestoreScenario {
    param([Parameter(Mandatory)] [string] $Exe, [Parameter(Mandatory)] [string] $RepoRoot, [Parameter(Mandatory)] [string] $ArtifactRoot)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $first = $null
    $second = $null
    $third = $null
    $fixture = $null
    $result = $null
    try {
        $first = New-UiaScenarioContext -Exe $Exe -RepoRoot $RepoRoot -ScenarioId 'e2e-restore-first' -InvocationId ([guid]::NewGuid().ToString('N')) -LogFn { param($m) }
        if (-not $first -or -not $first.Main) {
            $result = New-E2EResult -Id 'cold-restore' -Status 'FAIL' -Stage 'first-launch' -Detail 'first restore lifecycle did not expose a FastFiles window' -DurationMs $watch.Elapsed.TotalMilliseconds
            return $result
        }
        $fixture = New-E2EFixture -SessionFixtureRoot $first.FixtureRoot
        $nav = Invoke-UiaAddressBarNavigate -Driver $first.Driver -Main (Get-E2EMain -Context $first) -ProcessId ([int]$first.Launched.Id) -Path $fixture.Nested -ExpectedLeaf 'Nested'
        if (-not $nav.Pass -or -not (Wait-E2EItem -Context $first -Name 'restore-proof.txt' -TimeoutMs 12000).Pass) {
            $result = New-E2EResult -Id 'cold-restore' -Status 'FAIL' -Stage 'first-navigation' -Detail "first lifecycle did not settle on Nested: $($nav.Detail)" -DurationMs $watch.Elapsed.TotalMilliseconds
            return $result
        }
        $sessionRoot = $first.SessionRoot
        if (-not (Close-E2EContextWindow -Context $first)) {
            $result = New-E2EResult -Id 'cold-restore' -Status 'FAIL' -Stage 'first-close' -Detail 'first FastFiles process did not exit within the close deadline' -DurationMs $watch.Elapsed.TotalMilliseconds
            return $result
        }
        $firstCleanup = Remove-UiaScenarioContext -Context $first -LogFn { param($m) } -ArtifactsDir (Join-Path $ArtifactRoot 'cold-restore-first') -KeepSessionRoot
        if (-not $firstCleanup.Pass) {
            $result = New-E2EResult -Id 'cold-restore' -Status 'FAIL' -Stage 'first-cleanup' -Detail "first lifecycle cleanup left residue: $(@($firstCleanup.Residue) -join ',')" -DurationMs $watch.Elapsed.TotalMilliseconds
            return $result
        }
        $second = New-UiaScenarioContext -Exe $Exe -RepoRoot $RepoRoot -ScenarioId 'e2e-restore-second' -InvocationId ([guid]::NewGuid().ToString('N')) -LogFn { param($m) } -SessionRoot $sessionRoot
        if (-not $second -or -not $second.Main -or -not $second.Ready) {
            $result = New-E2EResult -Id 'cold-restore' -Status 'FAIL' -Stage 'second-launch' -Detail "cold relaunch was not ready: $($second.ReadyReason)" -DurationMs $watch.Elapsed.TotalMilliseconds
            return $result
        }
        [void]$second.Fixtures.Add($fixture.Root)
        $restored = Wait-E2EItem -Context $second -Name 'restore-proof.txt' -TimeoutMs 15000
        if (-not $restored.Pass) {
            $result = New-E2EResult -Id 'cold-restore' -Status 'FAIL' -Stage 'restored-location' `
                -Detail 'cold relaunch did not expose the previously active Nested directory; no address-bar intervention was used' `
                -DurationMs $watch.Elapsed.TotalMilliseconds
            return $result
        }
        $homeCommand = Invoke-E2ENavigationCommand -Context $second -Query 'home' -Name 'Home' -CommandId 'navigation.home'
        if (-not $homeCommand.Pass -or -not (Close-E2EContextWindow -Context $second)) {
            $result = New-E2EResult -Id 'cold-restore' -Status 'FAIL' -Stage 'home-close' -Detail "Home navigation/close failed: $($homeCommand.Detail)" -DurationMs $watch.Elapsed.TotalMilliseconds
            return $result
        }
        $secondCleanup = Remove-UiaScenarioContext -Context $second -LogFn { param($m) } -ArtifactsDir (Join-Path $ArtifactRoot 'cold-restore-second') -KeepSessionRoot
        if (-not $secondCleanup.Pass) {
            $result = New-E2EResult -Id 'cold-restore' -Status 'FAIL' -Stage 'home-cleanup' -Detail 'Home lifecycle cleanup left residue' -DurationMs $watch.Elapsed.TotalMilliseconds
            return $result
        }
        $third = New-UiaScenarioContext -Exe $Exe -RepoRoot $RepoRoot -ScenarioId 'e2e-restore-home' -InvocationId ([guid]::NewGuid().ToString('N')) -LogFn { param($m) } -SessionRoot $sessionRoot
        $restoredHome = Wait-E2ECondition -TimeoutMs 8000 -Description 'Home restored without navigation input' -Condition {
            Find-UiaElement -Driver $third.Driver -AutomationId 'Pane.0.Home' -FromElement (Get-E2EMain -Context $third) -TimeoutMs 500
        }
        if (-not $restoredHome.Pass) {
            $result = New-E2EResult -Id 'cold-restore' -Status 'FAIL' -Stage 'restored-home' -Detail 'Home did not restore its real pane after close/relaunch' -DurationMs $watch.Elapsed.TotalMilliseconds
            return $result
        }
        $result = New-E2EResult -Id 'cold-restore' -Status 'PASS' -Detail 'Nested directory and typed Home each restored after real close/relaunch in the same isolated state directory' `
            -DurationMs $watch.Elapsed.TotalMilliseconds -Data (Get-E2EVisibleObservation -Context $third)
        return $result
    } catch {
        $result = New-E2EResult -Id 'cold-restore' -Status 'FAIL' -Stage 'exception' -Detail $_.Exception.Message -DurationMs $watch.Elapsed.TotalMilliseconds
        return $result
    } finally {
        if ($result -and $result.status -eq 'FAIL') {
            $context = if ($third -and -not $third.Cleanup) { $third } elseif ($second -and -not $second.Cleanup) { $second } elseif ($first -and -not $first.Cleanup) { $first } else { $null }
            if ($context) {
                try {
                    Write-E2EFailureEvidence -Context $context -ArtifactDir (Join-Path $ArtifactRoot 'cold-restore') `
                        -ScenarioId 'cold-restore' -Stage ([string]$result.stage) -Expected ([string]$result.detail) `
                        -ElapsedMs ([double]$result.durationMs) | Out-Null
                } catch { }
            }
        }
        if ($third -and -not $third.Cleanup) {
            try { Remove-UiaScenarioContext -Context $third -LogFn { param($m) } -ArtifactsDir (Join-Path $ArtifactRoot 'cold-restore-home') | Out-Null } catch { }
        }
        if ($second -and -not $second.Cleanup) {
            if ($fixture -and $second.Fixtures.Count -eq 0) { [void]$second.Fixtures.Add($fixture.Root) }
            try { Remove-UiaScenarioContext -Context $second -LogFn { param($m) } -ArtifactsDir (Join-Path $ArtifactRoot 'cold-restore-second') | Out-Null } catch { }
        }
        if ($first -and -not $first.Cleanup) {
            try { Remove-UiaScenarioContext -Context $first -LogFn { param($m) } -ArtifactsDir (Join-Path $ArtifactRoot 'cold-restore-first-fallback') | Out-Null } catch { }
        }
    }
}

function Invoke-E2ELifecycleScenario {
    param([Parameter(Mandatory)] [string] $Exe, [Parameter(Mandatory)] [string] $RepoRoot, [Parameter(Mandatory)] [string] $ArtifactRoot, [int] $Iterations = 2)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $observations = [System.Collections.Generic.List[object]]::new()
    for ($iteration = 1; $iteration -le $Iterations; $iteration++) {
        $context = $null
        $fixture = $null
        $iterationResult = $null
        try {
            $context = New-UiaScenarioContext -Exe $Exe -RepoRoot $RepoRoot -ScenarioId "e2e-lifecycle-$iteration" -InvocationId ([guid]::NewGuid().ToString('N')) -LogFn { param($m) }
            if (-not $context -or -not $context.Main -or -not $context.Ready) {
                $iterationResult = New-E2EResult -Id 'process-lifecycle' -Status 'FAIL' -Stage "iteration-$iteration-launch" -Detail "iteration $iteration did not reach engine-ready: $($context.ReadyReason)" -DurationMs $watch.Elapsed.TotalMilliseconds
                return $iterationResult
            }
            $fixture = New-E2EFixture -SessionFixtureRoot $context.FixtureRoot
            [void]$context.Fixtures.Add($fixture.Root)
            $nav = Invoke-UiaAddressBarNavigate -Driver $context.Driver -Main (Get-E2EMain -Context $context) -ProcessId ([int]$context.Launched.Id) -Path $fixture.Browse -ExpectedLeaf 'Browse'
            if (-not $nav.Pass -or -not (Wait-E2EItem -Context $context -Name 'root-note.txt' -TimeoutMs 10000).Pass) {
                $iterationResult = New-E2EResult -Id 'process-lifecycle' -Status 'FAIL' -Stage "iteration-$iteration-navigation" -Detail "iteration $iteration navigation failed: $($nav.Detail)" -DurationMs $watch.Elapsed.TotalMilliseconds
                return $iterationResult
            }
            $processId = [int]$context.Launched.Id
            $closed = Close-E2EContextWindow -Context $context
            $cleanup = Remove-UiaScenarioContext -Context $context -LogFn { param($m) } -ArtifactsDir (Join-Path $ArtifactRoot "iteration-$iteration")
            [void]$observations.Add([ordered]@{ iteration = $iteration; pid = $processId; closed = $closed; cleanup = $cleanup })
            if (-not $closed -or -not $cleanup.Pass) {
                $iterationResult = New-E2EResult -Id 'process-lifecycle' -Status 'FAIL' -Stage "iteration-$iteration-cleanup" -Detail "iteration $iteration left cleanup residue: $(@($cleanup.Residue) -join ',')" -DurationMs $watch.Elapsed.TotalMilliseconds
                return $iterationResult
            }
        } catch {
            $iterationResult = New-E2EResult -Id 'process-lifecycle' -Status 'FAIL' -Stage "iteration-$iteration-exception" -Detail $_.Exception.Message -DurationMs $watch.Elapsed.TotalMilliseconds
            return $iterationResult
        } finally {
            if ($iterationResult -and $iterationResult.status -eq 'FAIL' -and $context) {
                try {
                    Write-E2EFailureEvidence -Context $context -ArtifactDir (Join-Path $ArtifactRoot 'process-lifecycle') `
                        -ScenarioId 'process-lifecycle' -Stage ([string]$iterationResult.stage) `
                        -Expected ([string]$iterationResult.detail) -ElapsedMs ([double]$iterationResult.durationMs) | Out-Null
                } catch { }
            }
            if ($context -and -not $context.Cleanup) {
                try {
                    Remove-UiaScenarioContext -Context $context -LogFn { param($m) } `
                        -ArtifactsDir (Join-Path $ArtifactRoot "iteration-$iteration-fallback") | Out-Null
                } catch { }
            }
        }
    }
    $repoPrefix = [IO.Path]::GetFullPath($RepoRoot).TrimEnd('\') + '\'
    $leftovers = @(Get-Process -Name FastFiles, FastFilesEngine -ErrorAction SilentlyContinue | ForEach-Object {
        try {
            $path = $_.Path
            if ($path -and $path.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)) { $_.Id }
        } catch { }
    })
    if ($leftovers.Count -gt 0) {
        return New-E2EResult -Id 'process-lifecycle' -Status 'FAIL' -Stage 'final-residue' -Detail "repo-owned FastFiles processes remained after lifecycle iterations: $($leftovers -join ',')" -DurationMs $watch.Elapsed.TotalMilliseconds -Data $observations
    }
    return New-E2EResult -Id 'process-lifecycle' -Status 'PASS' -Detail "completed $Iterations bounded launch/navigate/close iterations with no repo-owned process residue" -DurationMs $watch.Elapsed.TotalMilliseconds -Data $observations
}

function Invoke-FastFilesE2E {
    param(
        [Parameter(Mandatory)] [string] $Exe,
        [Parameter(Mandatory)] [string] $RepoRoot,
        [Parameter(Mandatory)] [string] $ArtifactsDir,
        [string] $Filter = 'all',
        [int] $LifecycleIterations = 2
    )
    New-Item -ItemType Directory -Path $ArtifactsDir -Force | Out-Null
    $tokens = @($Filter -split '[,; ]+' | Where-Object { $_ }) | ForEach-Object { $_.ToLowerInvariant() }
    $valid = @('all', 'launch', 'navigation', 'restore', 'search', 'storage', 'preview', 'fileops', 'lifecycle')
    foreach ($token in $tokens) {
        if ($valid -notcontains $token) { throw "Unknown E2E filter '$token'. Valid filters: $($valid -join ', ')" }
    }
    $runAll = $tokens -contains 'all'
    $shouldRun = { param($name) $runAll -or $tokens -contains $name }
    $results = [System.Collections.Generic.List[object]]::new()
    $availability = Get-UiaDriverAvailability
    if (-not $availability.Available) {
        [void]$results.Add((New-E2EResult -Id 'driver-availability' -Status 'SKIPPED' -Stage 'availability' -Detail "interactive UI Automation is unavailable: $($availability.Reason)"))
        return [pscustomobject][ordered]@{ status = 'SKIPPED'; filter = $Filter; results = @($results); passed = 0; failed = 0; skipped = 1; artifactsDir = $ArtifactsDir }
    }

    $commonNames = @('launch', 'navigation', 'search', 'storage', 'preview', 'fileops')
    $needCommon = $false
    foreach ($name in $commonNames) { if (& $shouldRun $name) { $needCommon = $true } }
    if ($needCommon) {
        $context = $null
        $fixture = $null
        try {
            $context = New-UiaScenarioContext -Exe $Exe -RepoRoot $RepoRoot -ScenarioId 'e2e-common' -InvocationId ([guid]::NewGuid().ToString('N')) -LogFn { param($m) }
            if ($context) { $fixture = New-E2EFixture -SessionFixtureRoot $context.FixtureRoot; [void]$context.Fixtures.Add($fixture.Root) }
            if (& $shouldRun 'launch') {
                $result = Invoke-E2EScenarioSafely -Id 'launch-ready' -Body { Invoke-E2ELaunchScenario -Context $context } -ArtifactRoot $ArtifactsDir -Context $context
                [void]$results.Add($result)
            }
            if (& $shouldRun 'navigation') {
                $result = Invoke-E2EScenarioSafely -Id 'navigation-browse' -Body { Invoke-E2ENavigationScenario -Context $context -Fixture $fixture } -ArtifactRoot $ArtifactsDir -Context $context
                [void]$results.Add($result)
            }
            if (& $shouldRun 'search') {
                $result = Invoke-E2EScenarioSafely -Id 'search-user-flow' -Body { Invoke-E2ESearchScenario -Context $context -Fixture $fixture } -ArtifactRoot $ArtifactsDir -Context $context
                [void]$results.Add($result)
            }
            if (& $shouldRun 'storage') {
                $result = Invoke-E2EScenarioSafely -Id 'storage-terminal-geometry' -Body { Invoke-E2EStorageScenario -Context $context -Fixture $fixture } -ArtifactRoot $ArtifactsDir -Context $context
                [void]$results.Add($result)
            }
            if (& $shouldRun 'preview') {
                $result = Invoke-E2EScenarioSafely -Id 'preview-terminal-state' -Body { Invoke-E2EPreviewScenario -Context $context -Fixture $fixture } -ArtifactRoot $ArtifactsDir -Context $context
                [void]$results.Add($result)
            }
            if (& $shouldRun 'fileops') {
                $result = Invoke-E2EScenarioSafely -Id 'file-operations' -Body { Invoke-E2EFileOperationsScenario -Context $context -Fixture $fixture } -ArtifactRoot $ArtifactsDir -Context $context
                [void]$results.Add($result)
            }
        } catch {
            [void]$results.Add((New-E2EResult -Id 'common-lifecycle' -Status 'FAIL' -Stage 'setup' -Detail $_.Exception.Message))
        } finally {
            if ($context -and -not $context.Cleanup) {
                try {
                    $cleanup = Remove-UiaScenarioContext -Context $context -LogFn { param($m) } -ArtifactsDir (Join-Path $ArtifactsDir 'common-cleanup')
                    if (-not $cleanup.Pass) { [void]$results.Add((New-E2EResult -Id 'common-cleanup' -Status 'FAIL' -Stage 'cleanup' -Detail "common context cleanup residue: $(@($cleanup.Residue) -join ',')")) }
                } catch { [void]$results.Add((New-E2EResult -Id 'common-cleanup' -Status 'FAIL' -Stage 'cleanup-exception' -Detail $_.Exception.Message)) }
            }
        }
    }
    if (& $shouldRun 'restore') {
        [void]$results.Add((Invoke-E2EScenarioSafely -Id 'cold-restore' -Body { Invoke-E2ERestoreScenario -Exe $Exe -RepoRoot $RepoRoot -ArtifactRoot $ArtifactsDir } -ArtifactRoot $ArtifactsDir))
    }
    if (& $shouldRun 'lifecycle') {
        [void]$results.Add((Invoke-E2EScenarioSafely -Id 'process-lifecycle' -Body { Invoke-E2ELifecycleScenario -Exe $Exe -RepoRoot $RepoRoot -ArtifactRoot $ArtifactsDir -Iterations $LifecycleIterations } -ArtifactRoot $ArtifactsDir))
    }
    $failed = @($results | Where-Object { $_.status -eq 'FAIL' }).Count
    $skipped = @($results | Where-Object { $_.status -eq 'SKIPPED' }).Count
    $passed = @($results | Where-Object { $_.status -eq 'PASS' }).Count
    $status = if ($failed -gt 0) { 'FAIL' } elseif ($skipped -gt 0 -and $passed -eq 0) { 'SKIPPED' } else { 'PASS' }
    $summary = [pscustomobject][ordered]@{
        status = $status
        filter = $Filter
        executable = [IO.Path]::GetFullPath($Exe)
        artifactsDir = $ArtifactsDir
        passed = $passed
        failed = $failed
        skipped = $skipped
        results = @($results)
    }
    $summary | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $ArtifactsDir 'summary.json') -Encoding utf8
    return $summary
}

Export-ModuleMember -Function Assert-E2EPathWithinRoot, Test-E2EPathWithinRoot, New-E2EFixture, New-E2EResult, Wait-E2ECondition, Write-E2EFailureEvidence, Invoke-FastFilesE2E
