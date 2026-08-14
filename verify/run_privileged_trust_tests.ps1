<#[
.SYNOPSIS
    Opt-in runner for the privileged CurrentUser\Root WinTrust trust-store
    integration tests. NOT part of ordinary FastFiles development.

.DESCRIPTION
    Normal Debug/release-test/analyze development, and hosted CI, never
    register or run these tests -- see AGENTS.md "Normal FastFiles
    development". They exist to independently prove the real Windows
    WinTrust decision (trusted-signer acceptance, Authenticode expiry
    transition, RFC3161 timestamp longevity) against disposable,
    non-production certificates that this script installs into and removes
    from CurrentUser\Root.

    Requirements:
      - An Administrator PowerShell is NOT required (CurrentUser\Root is a
        per-user store), but the host's ProtectedRoots ACL/policy must
        actually allow a CurrentUser\Root write; some hardened or hosted
        Windows images block this even for a non-elevated user. This script
        fails clearly rather than fabricating a pass when that is blocked.
      - `signtool.exe` (Windows SDK) reachable via PATH or a discovered SDK
        install.
      - `pwsh.exe` on PATH.
      - A disposable/approved Windows environment: this suite installs and
        removes uniquely-named "TEST CERTIFICATE / NON-PRODUCTION" material
        in CurrentUser\My and CurrentUser\Root for the duration of the run.

    This does NOT exercise production release signing, production pins,
    clean-host validation, or privileged MFT/USN service evidence -- those
    remain separate, explicitly deferred campaigns (see
    .agent/execplans/fastfiles-production-validation.md).

.PARAMETER BuildRoot
    Build directory to configure/build into. Defaults to build/privileged-trust
    so it never collides with the ordinary debug/release-test/analyze presets.

.PARAMETER SkipBuild
    Reuse an existing configured/built BuildRoot instead of reconfiguring.
#>

[CmdletBinding()]
param(
    [string] $RepoRoot = (Split-Path $PSScriptRoot -Parent),
    [string] $BuildRoot = (Join-Path (Split-Path $PSScriptRoot -Parent) 'build/privileged-trust'),
    [switch] $SkipBuild
)

$ErrorActionPreference = 'Stop'

function Write-Section {
    param([string] $Text)
    Write-Host ''
    Write-Host "=== $Text ===" -ForegroundColor Cyan
}

Write-Section 'This validation is not part of ordinary FastFiles development'
Write-Host 'Ordinary Debug/release-test/analyze builds and hosted CI never need this runner.'
Write-Host 'See AGENTS.md "Normal FastFiles development" before running this outside an explicit'
Write-Host 'privileged/production-validation task.'

Import-Module (Join-Path $RepoRoot 'verify/core/Toolchain.psm1') -Force

$toolchain = Find-VSToolchain
if (-not $toolchain) {
    Write-Host 'FAIL: no VC-capable Visual Studio installation (with CMake/Ninja/CTest) was discovered.'
    exit 1
}
Enter-DevEnvironment -Toolchain $toolchain -Arch x64

Write-Section 'Environment classification (informational; elevation is not required)'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
$isAdmin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
Write-Host "Administrator token: $isAdmin (informational only -- CurrentUser\Root does not require it)"

$signtool = $null
try {
    $signtoolCommand = Get-Command signtool.exe -ErrorAction Stop
    $signtool = $signtoolCommand.Source
} catch {
    $programFilesX86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
    if ($programFilesX86) {
        $kitRoot = Join-Path $programFilesX86 'Windows Kits\10\bin'
        if (Test-Path -LiteralPath $kitRoot -PathType Container) {
            $candidate = Get-ChildItem -LiteralPath $kitRoot -Recurse -Filter 'signtool.exe' -File -ErrorAction SilentlyContinue |
                Where-Object { $_.FullName -match '\\x64\\signtool\.exe$' } | Select-Object -First 1
            if ($candidate) { $signtool = $candidate.FullName }
        }
    }
}
if (-not $signtool) {
    Write-Host 'FAIL: signtool.exe was not found on PATH or under a discovered Windows Kits install.'
    Write-Host 'Install the Windows SDK (signing tools component) or add signtool.exe to PATH, then retry.'
    exit 1
}
Write-Host "signtool: $signtool"

if (-not (Get-Command pwsh.exe -ErrorAction SilentlyContinue)) {
    Write-Host 'FAIL: pwsh.exe was not found on PATH; the fixture scripts require PowerShell 7+.'
    exit 1
}

Write-Section 'Configure (FASTFILES_ENABLE_PRIVILEGED_TRUST_TESTS=ON)'
if (-not $SkipBuild) {
    & $toolchain.CMakeExe -S $RepoRoot -B $BuildRoot -G Ninja -DCMAKE_BUILD_TYPE=Debug `
        -DFASTFILES_ENABLE_PRIVILEGED_TRUST_TESTS=ON
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Write-Section 'Build (only the privileged trust-integration test targets)'
    & $toolchain.CMakeExe --build $BuildRoot --target `
        ffengine_authenticode_wintrust_integration_tests `
        ffsetup_package_trust_wintrust_integration_tests
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} else {
    Write-Host 'Skipping configure/build (-SkipBuild); reusing existing BuildRoot.'
}

Write-Section 'Run (label: privileged-trust)'
& $toolchain.CTestExe --test-dir $BuildRoot -L privileged-trust --output-on-failure
$testExitCode = $LASTEXITCODE

Write-Section 'Residue check'
$residueFailures = 0
$rootStorePath = 'Cert:\CurrentUser\Root'
$nonProductionRoots = @(Get-ChildItem -Path $rootStorePath -ErrorAction SilentlyContinue |
    Where-Object { $_.Subject -match 'FastFiles TEST CERTIFICATE NON-PRODUCTION' })
if ($nonProductionRoots.Count -gt 0) {
    Write-Host "FAIL: $($nonProductionRoots.Count) non-production FastFiles test certificate(s) remain in CurrentUser\Root."
    $residueFailures++
} else {
    Write-Host 'PASS: no non-production FastFiles test certificates remain in CurrentUser\Root.'
}
$myStorePath = 'Cert:\CurrentUser\My'
$nonProductionMy = @(Get-ChildItem -Path $myStorePath -ErrorAction SilentlyContinue |
    Where-Object { $_.Subject -match 'FastFiles TEST CERTIFICATE NON-PRODUCTION' })
if ($nonProductionMy.Count -gt 0) {
    Write-Host "FAIL: $($nonProductionMy.Count) non-production FastFiles test certificate(s) remain in CurrentUser\My."
    $residueFailures++
} else {
    Write-Host 'PASS: no non-production FastFiles test certificates remain in CurrentUser\My.'
}
$residueTempFiles = @(Get-ChildItem -Path ([IO.Path]::GetTempPath()) -Filter 'FastFiles-test-root-*.cer' -ErrorAction SilentlyContinue)
if ($residueTempFiles.Count -gt 0) {
    Write-Host "FAIL: $($residueTempFiles.Count) temporary root-certificate file(s) remain under %TEMP%."
    $residueFailures++
} else {
    Write-Host 'PASS: no temporary root-certificate files remain under %TEMP%.'
}

Write-Section 'Result'
if ($testExitCode -eq 0 -and $residueFailures -eq 0) {
    Write-Host 'PASS: privileged trust-store integration tests passed and left no residue.'
    exit 0
}
Write-Host 'FAIL: see test output and residue check above.'
exit 1
