<#
    Toolchain discovery (task 4.1 / D3): locate a VC-capable VS install via vswhere,
    resolve its bundled CMake/Ninja, and activate its Developer environment into the
    current process — with zero dependency on anything already being on PATH.
#>

function Find-VSToolchain {
    [CmdletBinding()]
    param()

    $vswhereCandidates = @(
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe"
    )
    $vswhere = $vswhereCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $vswhere) {
        return $null
    }

    $json = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -format json 2>$null
    if (-not $json) {
        return $null
    }

    $installs = $json | ConvertFrom-Json
    if (-not $installs -or $installs.Count -eq 0) {
        return $null
    }
    $install = $installs[0]
    $installPath = $install.installationPath

    $vcvarsall = Join-Path $installPath 'VC\Auxiliary\Build\vcvarsall.bat'
    $vsDevCmd   = Join-Path $installPath 'Common7\Tools\VsDevCmd.bat'
    $cmakeExe   = Join-Path $installPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    $ninjaExe   = Join-Path $installPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'

    if (-not (Test-Path $vcvarsall)) { return $null }
    if (-not (Test-Path $cmakeExe))  { return $null }
    if (-not (Test-Path $ninjaExe))  { return $null }

    $sdkVersion = $null
    $sdkRegRoot = 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Microsoft SDKs\Windows\v10.0'
    if (Test-Path $sdkRegRoot) {
        $sdkVersion = (Get-ItemProperty -Path $sdkRegRoot -Name ProductVersion -ErrorAction SilentlyContinue).ProductVersion
    }

    [pscustomobject]@{
        DisplayName        = $install.displayName
        InstallationPath   = $installPath
        InstallationVersion = $install.installationVersion
        VcVarsAllPath      = $vcvarsall
        VsDevCmdPath       = $vsDevCmd
        CMakeExe           = $cmakeExe
        NinjaExe           = $ninjaExe
        WindowsSdkVersion  = $sdkVersion
    }
}

function Enter-DevEnvironment {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [pscustomobject] $Toolchain,
        [string] $Arch = 'x64'
    )

    $marker = '___FASTFILES_VERIFY_ENV_BEGIN___'
    $cmdLine = "call `"$($Toolchain.VcVarsAllPath)`" $Arch >nul 2>&1 && echo $marker && set"
    $output = & cmd.exe /c $cmdLine
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to activate Developer environment via vcvarsall.bat (exit $LASTEXITCODE)"
    }

    $idx = -1
    for ($i = 0; $i -lt $output.Count; $i++) {
        if ($output[$i].ToString().Trim() -eq $marker) { $idx = $i; break }
    }
    if ($idx -lt 0) {
        throw 'vcvarsall.bat did not produce the expected environment marker; activation failed.'
    }

    foreach ($line in $output[($idx + 1)..($output.Count - 1)]) {
        if ($line -match '^([^=]+)=(.*)$') {
            Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2]
        }
    }

    # Prepend the VS-bundled CMake/Ninja dirs so a bare `cmake`/`ninja` also resolves
    # to the toolset-matched binaries, without relying on any global install.
    $cmakeDir = Split-Path $Toolchain.CMakeExe -Parent
    $ninjaDir = Split-Path $Toolchain.NinjaExe -Parent
    $env:PATH = "$cmakeDir;$ninjaDir;$env:PATH"

    # CMakePresets.json resolves CMAKE_MAKE_PROGRAM via this env var so the preset
    # file itself never hard-codes a host-specific path.
    $env:FASTFILES_NINJA_EXE = $Toolchain.NinjaExe
}

Export-ModuleMember -Function Find-VSToolchain, Enter-DevEnvironment
