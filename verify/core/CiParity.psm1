# residual-defect-closure-and-certification-completion 2.1:
# Pure, testable classification for the local CI-parity orchestrator. The
# orchestrator owns process launch and evidence; this module owns only the
# truth table so a missing required capability cannot be laundered into PASS.

Set-StrictMode -Version Latest

function Get-CiParityStepStatus {
    [OutputType([string])]
    param(
        [Parameter(Mandatory)] [int] $ExitCode,
        [bool] $TimedOut = $false,
        [bool] $AllowUnavailableExit = $false
    )

    if ($TimedOut) { return 'FAIL' }
    if ($ExitCode -eq 0) { return 'PASS' }
    if ($AllowUnavailableExit -and $ExitCode -eq 2) { return 'SKIPPED' }
    return 'FAIL'
}

function Get-CiParityOverallStatus {
    [OutputType([string])]
    param([object[]] $Steps)

    $records = @($Steps)
    if ($records.Count -eq 0) { return 'FAIL' }
    $statuses = @($records | ForEach-Object { [string]$_.status })
    if (@($statuses | Where-Object { $_ -notin @('PASS', 'SKIPPED', 'FAIL') }).Count -gt 0) { return 'FAIL' }
    if ($statuses -contains 'FAIL') { return 'FAIL' }
    if ($statuses -contains 'SKIPPED') { return 'SKIPPED' }
    return 'PASS'
}

function Get-OpenSpecToolResolution {
    [OutputType([string])]
    param(
        [bool] $NpmAvailable,
        [bool] $NpxAvailable
    )

    if ($NpmAvailable -and $NpxAvailable) { return 'AVAILABLE' }
    return 'UNAVAILABLE'
}

function Test-OpenSpecToolUnavailable {
    [OutputType([bool])]
    param([AllowNull()] [AllowEmptyString()] [string] $Output)

    if ([string]::IsNullOrWhiteSpace($Output)) { return $false }

    # Only classify failures emitted by the npm/npx bootstrap. A validator is
    # allowed to mention ENOENT while checking a project file; that is a real
    # validation failure, not evidence that the tool is missing.
    $patterns = @(
        '(?im)^npm ERR!.*could not determine executable',
        '(?im)^npm ERR!.*command not found:\s*openspec',
        '(?im)^npm ERR! 404\b',
        '(?im)^npm ERR! code (?:EAI_AGAIN|ECONNREFUSED|ENETUNREACH|ETIMEDOUT|ENOENT|EACCES)\b',
        '(?im)^Error:\s+Cannot find module\b.*openspec',
        '(?im)^npx:\s+command not found\b'
    )
    foreach ($pattern in $patterns) {
        if ($Output -match $pattern) { return $true }
    }
    return $false
}

Export-ModuleMember -Function Get-CiParityStepStatus, Get-CiParityOverallStatus,
    Get-OpenSpecToolResolution, Test-OpenSpecToolUnavailable
