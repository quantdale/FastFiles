[CmdletBinding()]
param(
    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
)

$ErrorActionPreference = 'Stop'

Write-Host "Running Test-ExternalFrontierInventory.ps1..." -ForegroundColor Cyan

$frontierPath = Join-Path $RepoRoot 'verify\certification\external-frontier.json'
$schemaPath = Join-Path $RepoRoot 'verify\certification\schemas\external-frontier.schema.json'

if (-not (Test-Path -LiteralPath $frontierPath)) { throw "Frontier file not found: $frontierPath" }
if (-not (Test-Path -LiteralPath $schemaPath)) { throw "Schema file not found: $schemaPath" }

# Test 1: JSON Schema Validation
Write-Host "  Test 1: JSON schema validation..."
$pyCmd = "import json, jsonschema; s = json.load(open(r'$schemaPath')); d = json.load(open(r'$frontierPath')); jsonschema.validate(instance=d, schema=s); print('OK')"
$val = & (Get-Command python).Source -c $pyCmd 2>&1
if ($val -notmatch 'OK') { throw "Schema validation failed: $val" }
Write-Host "    PASS (Schema compliant)" -ForegroundColor Green

# Test 2: Task Count and Structure
Write-Host "  Test 2: Task counts and structural integrity..."
$frontier = Get-Content -LiteralPath $frontierPath -Raw | ConvertFrom-Json
if ($frontier.tasks.Count -ne 25) { throw "Expected 25 tasks, found $($frontier.tasks.Count)" }
if ($frontier.summary.totalTasks -ne 25) { throw "Expected summary.totalTasks == 25, got $($frontier.summary.totalTasks)" }

$groupSum = 0
foreach ($prop in $frontier.summary.byFrontierGroup.PSObject.Properties) {
    $groupSum += $prop.Value
}
if ($groupSum -ne 25) { throw "Frontier group breakdown does not sum to 25 (got $groupSum)" }
Write-Host "    PASS (25 tasks accounted for)" -ForegroundColor Green

# Test 3: Command Executability and Parameter Validation (Section 49)
Write-Host "  Test 3: Command executability, script resolution, and parameter verification..."
foreach ($task in $frontier.tasks) {
    $cmd = $task.executionCommand
    if ([string]::IsNullOrWhiteSpace($cmd)) {
        throw "Task $($task.taskId) has empty executionCommand"
    }

    # Verify result schema exists
    $relSchema = $task.resultSchema
    $fullSchema = Join-Path $RepoRoot $relSchema
    if (-not (Test-Path -LiteralPath $fullSchema)) {
        throw "Task $($task.taskId) references nonexistent resultSchema: $relSchema"
    }

    # Extract script path from command (pwsh -File <script> ...)
    if ($cmd -match 'verify/certification/[A-Za-z0-9_-]+\.ps1') {
        $relScript = $Matches[0]
        $fullScript = Join-Path $RepoRoot $relScript
        if (-not (Test-Path -LiteralPath $fullScript)) {
            throw "Task $($task.taskId) references nonexistent script: $relScript"
        }

        # Check parameter names against script AST
        $ast = [System.Management.Automation.Language.Parser]::ParseFile($fullScript, [ref]$null, [ref]$null)
        $scriptParams = @()
        if ($ast.ParamBlock -and $ast.ParamBlock.Parameters) {
            $scriptParams = @($ast.ParamBlock.Parameters | ForEach-Object { $_.Name.VariablePath.UserPath })
        }

        # Extract parameters used in executionCommand AFTER the script path
        $afterScript = $cmd.Substring($cmd.IndexOf($relScript) + $relScript.Length)
        $usedParams = [regex]::Matches($afterScript, '(?<=\s)-([A-Za-z0-9]+)') | ForEach-Object { $_.Groups[1].Value }
        foreach ($p in $usedParams) {
            if ($scriptParams -notcontains $p) {
                throw "Task $($task.taskId) command passes parameter -$p not accepted by $relScript. Valid parameters: $($scriptParams -join ', ')"
            }
        }
    } else {
        throw "Task $($task.taskId) command does not match expected certification script pattern: $cmd"
    }

    # Placeholder vs requiresOperatorInput contract
    $hasPlaceholder = ($cmd -match '<[^>]+>')
    if ($hasPlaceholder -and -not $task.requiresOperatorInput) {
        throw "Task $($task.taskId) command contains placeholder token but requiresOperatorInput is false: $cmd"
    }
    if (-not $hasPlaceholder -and $task.requiresOperatorInput) {
        throw "Task $($task.taskId) is marked requiresOperatorInput=true but command has no placeholder tokens: $cmd"
    }
}
Write-Host "    PASS (All 25 commands resolve to valid scripts and declared parameters)" -ForegroundColor Green

# Test 4: Task-to-Lane Semantic Alignment (Section 50)
Write-Host "  Test 4: Task-to-lane semantic consistency and anti-mismatch gates..."
foreach ($task in $frontier.tasks) {
    $group = $task.frontierGroup
    $lane = $task.lane
    $cmd = $task.executionCommand

    # Gate 1: Nonexistent CTest target ffuia_smoke_tests must NEVER be referenced
    if ($cmd -match 'ffuia_smoke_tests') {
        throw "Task $($task.taskId) references nonexistent target 'ffuia_smoke_tests'. Prohibited by CT-007."
    }

    # Gate 2: Hyper-V VM provider tasks must NEVER reference vision critic script
    if ($group -eq 'external-provider' -and $task.description -match 'Hyper-V|disposable.*provider|VM|guest') {
        if ($cmd -match 'Invoke-VisionProviderCertification' -or ($cmd -match 'Invoke-ProviderCertification' -and $cmd -notmatch 'Environment')) {
            throw "Task $($task.taskId) is a VM/guest environment task but references vision critic provider script. Prohibited by CT-007."
        }
        if ($lane -ne 'environment-provider') {
            throw "Task $($task.taskId) is a VM/guest task but lane is '$lane'. Expected 'environment-provider'."
        }
    }

    # Gate 3: Interactive / physical display tasks must use UIA long run runner or visual matrix
    if ($group -eq 'physical-environment') {
        if ($cmd -notmatch 'Invoke-UiaLongRunCertification') {
            throw "Task $($task.taskId) is in physical-environment but does not use Invoke-UiaLongRunCertification: $cmd"
        }
    }

    # Gate 4: Production signing tasks must use signing certification
    if ($group -eq 'production-signing') {
        if ($cmd -notmatch 'Invoke-SigningCertification') {
            throw "Task $($task.taskId) is in production-signing but does not use Invoke-SigningCertification: $cmd"
        }
    }

    # Gate 5: Privileged NTFS tasks must use privileged NTFS certification
    if ($group -eq 'privileged-ntfs') {
        if ($cmd -notmatch 'Invoke-PrivilegedNtfsCertification') {
            throw "Task $($task.taskId) is in privileged-ntfs but does not use Invoke-PrivilegedNtfsCertification: $cmd"
        }
    }

    # Gate 6: Clean host tasks must use clean host certification
    if ($group -eq 'clean-host') {
        if ($cmd -notmatch 'Invoke-CleanHostCertification') {
            throw "Task $($task.taskId) is in clean-host but does not use Invoke-CleanHostCertification: $cmd"
        }
    }
}
Write-Host "    PASS (Zero semantic mismatches across all 25 frontier tasks)" -ForegroundColor Green

# Test 5: Capability-aware execution contract (campaign sections 12-13, 26.14-16)
Write-Host "  Test 5: Runner capability coverage for every task contract..."
$manifestPath = Join-Path $RepoRoot 'verify\certification\runner-capabilities.json'
if (-not (Test-Path -LiteralPath $manifestPath)) { throw "Runner capability manifest not found: $manifestPath" }
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json

# 5a. Manifest grounding: every advertised capability must appear literally in runner source
foreach ($runnerProp in $manifest.runners.PSObject.Properties) {
    $runnerFile = Join-Path $RepoRoot ("verify\certification\" + $runnerProp.Name)
    if (-not (Test-Path -LiteralPath $runnerFile)) { throw "Manifest runner missing: $($runnerProp.Name)" }
    $source = Get-Content -LiteralPath $runnerFile -Raw
    $claimed = @()
    foreach ($modeProp in $runnerProp.Value.modes.PSObject.Properties) { $claimed += @($modeProp.Value) }
    if ($runnerProp.Value.matrixDimensions) { $claimed += @($runnerProp.Value.matrixDimensions) }
    foreach ($cap in @($claimed | Select-Object -Unique)) {
        if ($source -notmatch [regex]::Escape($cap)) {
            throw "Manifest claims '$cap' for $($runnerProp.Name) but the string never appears in its source (ungrounded advertisement)."
        }
    }
}
Write-Host "    PASS (manifest capabilities grounded in runner sources)" -ForegroundColor Green

# 5b. Per-task coverage: requiredCapabilities must be a subset of the mapped runner mode set
foreach ($task in $frontier.tasks) {
    $cmd = $task.executionCommand
    $scriptName = $null
    if ($cmd -match '(Invoke-[A-Za-z0-9_-]+\.ps1)') { $scriptName = $Matches[1] }
    if (-not $scriptName -or -not $manifest.runners.PSObject.Properties.Name.Contains($scriptName)) {
        throw "Task $($task.taskId) maps to unknown runner '$scriptName' (no manifest entry)."
    }
    $entry = $manifest.runners.$scriptName
    $mode = switch ($scriptName) {
        'Invoke-UiaLongRunCertification.ps1' {
            if ($cmd -match '-Scenario\s+([A-Za-z-]+)') { $Matches[1] } else { 'core-smoke' }
        }
        'Invoke-EnvironmentProviderCertification.ps1' { 'hyperv-execution' }
        'Invoke-CleanHostCertification.ps1' { if ($cmd -match '-InstallCandidate') { 'install' } else { 'audit' } }
        'Invoke-PrivilegedNtfsCertification.ps1' { 'ground-truth' }
        'Invoke-SigningCertification.ps1' { 'verification' }
    }
    $provided = @($entry.modes.$mode)
    if ($entry.matrixDimensions -and ($cmd -match '-Theme|-DpiPercent|-Contrast|-Viewport|-PaneTopology')) {
        $provided += @($entry.matrixDimensions)
    }
    $missing = @($task.requiredCapabilities | Where-Object { $provided -notcontains $_ })
    if ($missing.Count -gt 0 -and $task.readiness -ne 'NOT_IMPLEMENTED') {
        throw "Task $($task.change)/$($task.taskId) requires [$($missing -join ',')] but '$scriptName' mode '$mode' provides [$($provided -join ',')]. Command: $cmd"
    }
    # 5c. Dimension-specific gates: DPI/contrast requirements need explicit matrix args
    if (@($task.requiredCapabilities | Where-Object { $_ -like 'dpi-*' }).Count -gt 0 -and $cmd -notmatch '-DpiPercent') {
        throw "Task $($task.change)/$($task.taskId) requires DPI capability but command has no -DpiPercent matrix argument."
    }
    if ($task.requiredCapabilities -contains 'high-contrast' -and $cmd -notmatch '-Contrast') {
        throw "Task $($task.change)/$($task.taskId) requires high-contrast but command has no -Contrast matrix argument."
    }
    if (@($task.requiredCapabilities | Where-Object { $_ -in @('clean-install', 'installed-runtime', 'uninstall-cleanup') }).Count -gt 0 -and $cmd -notmatch '-InstallCandidate') {
        throw "Task $($task.change)/$($task.taskId) requires install capabilities but command lacks -InstallCandidate."
    }
    # 5d. Readiness truth: unprovable capabilities force NOT_IMPLEMENTED with a recorded gap
    $never = @($entry.neverAdvertised)
    $unprovable = @($task.requiredCapabilities | Where-Object { $never -contains $_ })
    if ($unprovable.Count -gt 0 -and $task.readiness -ne 'NOT_IMPLEMENTED') {
        throw "Task $($task.change)/$($task.taskId) requires [$($unprovable -join ',')] no runner advertises, yet readiness is '$($task.readiness)' (must be NOT_IMPLEMENTED)."
    }
    if ($task.readiness -eq 'NOT_IMPLEMENTED' -and [string]::IsNullOrWhiteSpace([string]$task.implementationGap)) {
        throw "Task $($task.change)/$($task.taskId) is NOT_IMPLEMENTED but records no implementationGap."
    }
    if ($task.readiness -eq 'READY_EXTERNAL' -and ([string]::IsNullOrWhiteSpace([string]$task.successPredicate) -or @($task.requiredAssertions).Count -eq 0 -or @($task.cleanupRequirements).Count -eq 0)) {
        throw "Task $($task.change)/$($task.taskId) is READY_EXTERNAL but its execution contract is incomplete (predicate/assertions/cleanup required)."
    }
}
Write-Host "    PASS (all 25 task contracts covered by runner capabilities with honest readiness)" -ForegroundColor Green

Write-Host "All Test-ExternalFrontierInventory tests PASSED!" -ForegroundColor Green
