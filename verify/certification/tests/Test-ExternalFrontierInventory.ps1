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

Write-Host "All Test-ExternalFrontierInventory tests PASSED!" -ForegroundColor Green
