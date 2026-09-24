# residual-defect-closure-and-certification-completion 2.2: standalone
# strict OpenSpec child used by hosted/local parity. It owns tool resolution
# and classification; the parent only records the child's exit code.
[CmdletBinding()]
param([Parameter(Mandatory)][string] $RepoRoot)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Set-Location ([IO.Path]::GetFullPath($RepoRoot))
Import-Module (Join-Path $RepoRoot 'verify\core\CiParity.psm1') -Force -DisableNameChecking

$npm = Get-Command npm -ErrorAction SilentlyContinue
$npx = Get-Command npx -ErrorAction SilentlyContinue
$toolResolution = Get-OpenSpecToolResolution -NpmAvailable ($null -ne $npm) -NpxAvailable ($null -ne $npx)
if ($toolResolution -ne 'AVAILABLE') {
    Write-Host "openspec CLI unavailable on this host (tool-resolution=$toolResolution)"
    exit 2
}

# npm 11 on Windows requires the separator after the package specification;
# invoking the Roaming npx shim through the call operator can otherwise
# produce `exec px` and the misleading "could not determine executable".
$output = npx --yes --package=@fission-ai/openspec@1.8.0 -- openspec validate --all --strict --no-interactive 2>&1
$code = $LASTEXITCODE
$output | ForEach-Object { Write-Host $_ }
if ($code -ne 0 -and (Test-OpenSpecToolUnavailable -Output "$output")) {
    Write-Host 'openspec CLI unavailable on this host (npx bootstrap failed)'
    exit 2
}
if ($code -eq 0) { exit 0 }
exit 1
