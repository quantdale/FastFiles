<#
    Toolchain discovery (task 4.1 / D3): locate a VC-capable VS install via vswhere,
    resolve CMake/Ninja/CTest from PATH or a discovered installation-local fallback,
    and activate the Developer environment into the current process. The compiler
    toolset is tied to Visual Studio; the build front ends are intentionally not tied
    to one VS product layout because hosted runner images and local installations may
    provide them independently.
#>

function Resolve-ToolExecutable {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string[]] $Names,
        [string[]] $Candidates = @()
    )

    foreach ($name in $Names) {
        $command = Get-Command -Name $name -CommandType Application -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($command) {
            $commandPath = if ($command.Path) { $command.Path } else { $command.Source }
            if ($commandPath -and (Test-Path -LiteralPath $commandPath -PathType Leaf)) {
                return [IO.Path]::GetFullPath($commandPath)
            }
        }
    }

    foreach ($candidate in $Candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return [IO.Path]::GetFullPath($candidate)
        }
    }

    return $null
}

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

    $jsonLines = @(& $vswhere -all -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -format json 2>$null)
    if ($LASTEXITCODE -ne 0 -or $jsonLines.Count -eq 0) {
        return $null
    }
    # ConvertFrom-Json must receive the complete document. Piping the raw
    # line array works accidentally for a single installation but can parse
    # each line independently when the runner exposes multiple instances.
    $json = $jsonLines -join [Environment]::NewLine

    try {
        $installs = @($json | ConvertFrom-Json)
    } catch {
        return $null
    }
    if ($installs.Count -eq 0) {
        return $null
    }

    # Prefer the newest complete VC installation, but do not assume that the
    # newest instance is the only usable one (preview/partial installs are
    # common on developer machines and hosted images).
    $install = $installs |
        Sort-Object -Property installationVersion -Descending |
        Where-Object {
            $_.installationPath -and
            (Test-Path -LiteralPath (Join-Path $_.installationPath 'VC\Auxiliary\Build\vcvarsall.bat') -PathType Leaf)
        } |
        Select-Object -First 1
    if (-not $install) {
        return $null
    }
    $installPath = $install.installationPath

    $vcvarsall = Join-Path $installPath 'VC\Auxiliary\Build\vcvarsall.bat'
    $vsDevCmd   = Join-Path $installPath 'Common7\Tools\VsDevCmd.bat'

    # Hosted images have moved CMake/Ninja between VS versions and also ship
    # them as independent PATH tools. Prefer the active PATH because that is
    # the supported public contract of CMake presets; retain the historical
    # VS-local paths only as a discovered fallback for older local installs.
    $cmakeLocal = Join-Path $installPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    $ninjaLocal = Join-Path $installPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
    $cmakeExe = Resolve-ToolExecutable -Names @('cmake.exe', 'cmake') -Candidates @(
        $cmakeLocal,
        (Join-Path $env:ProgramFiles 'CMake\bin\cmake.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'CMake\bin\cmake.exe')
    )
    $ninjaExe = Resolve-ToolExecutable -Names @('ninja.exe', 'ninja') -Candidates @(
        $ninjaLocal,
        (Join-Path $env:ProgramFiles 'Ninja\ninja.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Ninja\ninja.exe')
    )
    if (-not $cmakeExe -or -not $ninjaExe) {
        return $null
    }

    $ctestCandidates = @()
    if ($cmakeExe) {
        $ctestCandidates += Join-Path (Split-Path $cmakeExe -Parent) 'ctest.exe'
    }
    $ctestExe = Resolve-ToolExecutable -Names @('ctest.exe', 'ctest') -Candidates $ctestCandidates
    if (-not $ctestExe) {
        return $null
    }

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
        CTestExe           = $ctestExe
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

    # Re-resolve after vcvarsall in case the tools are supplied by a developer
    # environment path rather than the pre-activation process. This keeps the
    # returned object useful to the verification harness as well as to CI.
    $cmakeExe = Resolve-ToolExecutable -Names @('cmake.exe', 'cmake') -Candidates @($Toolchain.CMakeExe)
    $ninjaExe = Resolve-ToolExecutable -Names @('ninja.exe', 'ninja') -Candidates @($Toolchain.NinjaExe)
    $ctestCandidates = @($Toolchain.CTestExe)
    if ($cmakeExe) {
        $ctestCandidates += Join-Path (Split-Path $cmakeExe -Parent) 'ctest.exe'
    }
    $ctestExe = Resolve-ToolExecutable -Names @('ctest.exe', 'ctest') -Candidates $ctestCandidates
    if (-not $cmakeExe -or -not $ninjaExe -or -not $ctestExe) {
        throw 'CMake, Ninja, and CTest were not resolvable from PATH or the discovered toolchain.'
    }
    $Toolchain.CMakeExe = $cmakeExe
    $Toolchain.NinjaExe = $ninjaExe
    $Toolchain.CTestExe = $ctestExe

    $toolDirectories = @(
        (Split-Path $cmakeExe -Parent),
        (Split-Path $ninjaExe -Parent),
        (Split-Path $ctestExe -Parent)
    ) | Select-Object -Unique
    $env:PATH = (($toolDirectories -join ';') + ";$env:PATH")

    # GetTempPathW can reject an 8.3-form TEMP/TMP value on this host even when
    # the corresponding long directory exists. Canonicalize only existing
    # process-local directories so CTest child processes see the same usable
    # temp root as interactive PowerShell, without changing the user's profile.
    foreach ($temporaryVariable in @('TEMP', 'TMP')) {
        $temporaryPath = [Environment]::GetEnvironmentVariable($temporaryVariable, 'Process')
        if ([string]::IsNullOrWhiteSpace($temporaryPath)) {
            continue
        }
        try {
            $resolvedTemporaryPath = (Get-Item -LiteralPath $temporaryPath -ErrorAction Stop).FullName
            if (Test-Path -LiteralPath $resolvedTemporaryPath -PathType Container) {
                [Environment]::SetEnvironmentVariable($temporaryVariable, $resolvedTemporaryPath, 'Process')
            }
        } catch {
            # Preserve the original value; the child tool will report a real
            # temp-directory failure if it cannot use it.
        }
    }

    # Preserve the optional compatibility variable for older local scripts.
    # Current CMakePresets.json deliberately uses normal CMake/Ninja discovery;
    # the base preset does not require or consume this variable.
    $env:FASTFILES_NINJA_EXE = $Toolchain.NinjaExe
}

Export-ModuleMember -Function Find-VSToolchain, Enter-DevEnvironment
