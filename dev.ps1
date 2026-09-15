<#
FastFiles developer entry point. It discovers a VC-capable Visual Studio
installation, imports its x64 build environment, and keeps the normal build,
test, run, fixture, and diagnostics commands in one place.
#>
[CmdletBinding()]
param(
    [ValidateSet('doctor', 'setup', 'bootstrap', 'configure', 'build', 'run',
                 'test', 'test-fast', 'test-full', 'clean', 'rebuild',
                 'package', 'logs', 'stop', 'fixture', 'fixture-clean')]
    [string] $Command = 'doctor',
    [ValidateSet('debug', 'debug-diagnostic', 'release', 'release-test', 'analyze', 'asan')]
    [string] $Preset = '',
    [string] $Target = '',
    [string] $Filter = '',
    [int] $Parallel = 0,
    [switch] $SkipBuild,
    [switch] $ResetFixture,
    [switch] $Gallery
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath($PSScriptRoot)
if (-not (Test-Path -LiteralPath (Join-Path $repoRoot 'CMakeLists.txt') -PathType Leaf)) {
    throw "FastFiles repository root was not found at '$repoRoot'."
}

$toolchainModule = Join-Path $repoRoot 'verify/core/Toolchain.psm1'
if (-not (Test-Path -LiteralPath $toolchainModule -PathType Leaf)) {
    throw "Toolchain discovery module is missing: '$toolchainModule'."
}
Import-Module $toolchainModule -Force

$toolchain = $null

function Initialize-FastFilesToolchain {
    if ($null -ne $script:toolchain) {
        return $script:toolchain
    }

    $found = Find-VSToolchain
    if ($null -eq $found) {
        throw ('No complete VC-capable Visual Studio installation with CMake, Ninja, and CTest was found. ' +
               'Install the Desktop development with C++ workload, including CMake tools for Windows.')
    }
    Enter-DevEnvironment -Toolchain $found -Arch 'x64'
    $script:toolchain = $found
    return $script:toolchain
}

function Invoke-FastFilesTool {
    param(
        [Parameter(Mandatory)] [string] $FilePath,
        [Parameter(Mandatory)] [string[]] $Arguments,
        [Parameter(Mandatory)] [string] $Label
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE."
    }
}

function Invoke-FastFilesCMake {
    param([Parameter(Mandatory)] [string[]] $Arguments)
    $null = Initialize-FastFilesToolchain
    Invoke-FastFilesTool -FilePath $script:toolchain.CMakeExe -Arguments $Arguments `
        -Label ('cmake ' + ($Arguments -join ' '))
}

function Get-FastFilesBuildPreset {
    if (-not [string]::IsNullOrWhiteSpace($Preset)) {
        return $Preset
    }
    if ($Command -eq 'run') {
        return 'debug-diagnostic'
    }
    if ($Command -eq 'package') {
        return 'release-test'
    }
    return 'debug'
}

function Get-FastFilesBuildDirectory {
    param([Parameter(Mandatory)] [string] $BuildPreset)
    return [IO.Path]::GetFullPath((Join-Path $repoRoot ('build\' + $BuildPreset)))
}

function Ensure-FastFilesConfigured {
    param([Parameter(Mandatory)] [string] $BuildPreset)
    $buildDirectory = Get-FastFilesBuildDirectory -BuildPreset $BuildPreset
    if (-not (Test-Path -LiteralPath (Join-Path $buildDirectory 'CMakeCache.txt') -PathType Leaf)) {
        Write-Host "Configuring preset '$BuildPreset'..." -ForegroundColor Yellow
        Invoke-FastFilesCMake -Arguments @('--preset', $BuildPreset)
    }
}

function Build-FastFiles {
    param(
        [Parameter(Mandatory)] [string] $BuildPreset,
        [string] $BuildTarget = ''
    )
    Ensure-FastFilesConfigured -BuildPreset $BuildPreset
    $arguments = @('--build', '--preset', $BuildPreset)
    if (-not [string]::IsNullOrWhiteSpace($BuildTarget)) {
        $arguments += @('--target', $BuildTarget)
    }
    if ($Parallel -gt 0) {
        $arguments += @('--parallel', [string]$Parallel)
    }
    Write-Host "Building '$BuildPreset'..." -ForegroundColor Yellow
    Invoke-FastFilesCMake -Arguments $arguments
}

function Test-FastFilesBuildTarget {
    param(
        [Parameter(Mandatory)] [string] $BuildPreset,
        [Parameter(Mandatory)] [string] $BuildTarget
    )
    Ensure-FastFilesConfigured -BuildPreset $BuildPreset
    $help = & $script:toolchain.CMakeExe '--build' '--preset' $BuildPreset '--target' 'help' 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Could not inspect the '$BuildPreset' build targets."
    }
    $helpText = $help -join "`n"
    $escapedTarget = [regex]::Escape($BuildTarget)
    return $helpText -match "(?m)^\s*$escapedTarget(?:\s|:|$)"
}

function Get-FastFilesPowerShell {
    $pwsh = Get-Command -Name 'pwsh.exe' -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($null -ne $pwsh) {
        return $pwsh.Path
    }
    $windowsPowerShell = Get-Command -Name 'powershell.exe' -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($null -ne $windowsPowerShell) {
        return $windowsPowerShell.Path
    }
    return $null
}

function Invoke-FastFilesPowerShellScript {
    param(
        [Parameter(Mandatory)] [string] $ScriptPath,
        [Parameter(Mandatory)] [string[]] $Arguments
    )
    $shell = Get-FastFilesPowerShell
    if ($null -eq $shell) {
        throw 'PowerShell was not found. Install PowerShell 7 from https://learn.microsoft.com/powershell/scripting/install/installing-powershell-on-windows.'
    }
    & $shell -NoProfile -File $ScriptPath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "PowerShell helper '$ScriptPath' failed with exit code $LASTEXITCODE."
    }
}

function Assert-FastFilesBuildTarget {
    param([Parameter(Mandatory)] [string] $BuildPreset)
    $buildRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'build'))
    $target = Get-FastFilesBuildDirectory -BuildPreset $BuildPreset
    $rootPrefix = $buildRoot.TrimEnd('\') + '\'
    if (-not $target.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        $target -eq $buildRoot) {
        throw "Refusing to operate outside the generated build tree: '$target'."
    }
    return $target
}

function Show-FastFilesDoctor {
    Write-Host 'FastFiles developer doctor' -ForegroundColor Cyan
    Write-Host "Repository: $repoRoot"
    try {
        $null = Initialize-FastFilesToolchain
    } catch {
        Write-Host 'MSVC: missing' -ForegroundColor Red
        Write-Host 'Windows SDK: unknown' -ForegroundColor Red
        Write-Host 'CMake: missing' -ForegroundColor Red
        Write-Host 'Ninja: missing' -ForegroundColor Red
        Write-Host 'CTest: missing' -ForegroundColor Red
        Write-Host 'Ready: NO' -ForegroundColor Red
        Write-Host "Missing: $($_.Exception.Message)" -ForegroundColor Yellow
        Write-Host 'Suggested fix: Visual Studio Installer -> Desktop development with C++ -> C++ CMake tools for Windows.'
        return $false
    }

    $compiler = Get-Command -Name 'cl.exe' -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    $sdkPath = $env:WindowsSdkDir
    if ([string]::IsNullOrWhiteSpace($sdkPath)) {
        $sdkInclude = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Include'
        if (Test-Path -LiteralPath $sdkInclude -PathType Container) {
            $sdkPath = (Get-ChildItem -LiteralPath $sdkInclude -Directory |
                Sort-Object -Property Name -Descending | Select-Object -First 1).FullName
        }
    }
    $pwsh = Get-Command -Name 'pwsh.exe' -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    $compilerPath = if ($null -ne $compiler) { $compiler.Path } else { '' }
    $sdkReady = -not [string]::IsNullOrWhiteSpace($sdkPath) -and (Test-Path -LiteralPath $sdkPath -PathType Container)

    Write-Host "MSVC: $([bool]($null -ne $compiler))$(if ($compilerPath) { " at $compilerPath" })"
    Write-Host "Windows SDK: $sdkReady$(if ($sdkReady) { " at $sdkPath" })"
    Write-Host "CMake: found at $($toolchain.CMakeExe)"
    Write-Host "Ninja: found at $($toolchain.NinjaExe)"
    Write-Host "CTest: found at $($toolchain.CTestExe)"
    if ($null -ne $pwsh) {
        Write-Host "PowerShell 7: found at $($pwsh.Path)"
    } else {
        Write-Host 'PowerShell 7: missing (PowerShell-gated tests will be unavailable)' -ForegroundColor Yellow
    }
    Write-Host "Build tree: $(Join-Path $repoRoot 'build')"
    $ready = ($null -ne $compiler) -and $sdkReady
    if ($ready) {
        Write-Host 'Ready: YES' -ForegroundColor Green
    } else {
        Write-Host 'Ready: NO' -ForegroundColor Red
    }
    return $ready
}

function Stop-FastFilesDevelopmentProcesses {
    $buildRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'build')).TrimEnd('\') + '\'
    $processes = @(Get-CimInstance -ClassName Win32_Process -Filter "Name='FastFiles.exe' OR Name='FastFilesEngine.exe'" |
        Where-Object {
            if ([string]::IsNullOrWhiteSpace($_.ExecutablePath)) { return $false }
            $executablePath = $_.ExecutablePath
            # Win32_Process may expose a repository path with the extended-length
            # prefix; normalize it before applying the exact build-root fence.
            if ($executablePath.StartsWith('\\?\UNC\', [StringComparison]::OrdinalIgnoreCase)) {
                $executablePath = '\\' + $executablePath.Substring(8)
            } elseif ($executablePath.StartsWith('\\?\', [StringComparison]::OrdinalIgnoreCase)) {
                $executablePath = $executablePath.Substring(4)
            }
            ([IO.Path]::GetFullPath($executablePath)).StartsWith($buildRoot, [StringComparison]::OrdinalIgnoreCase)
        })
    if ($processes.Count -eq 0) {
        Write-Host 'No FastFiles development processes are running.'
        return
    }
    foreach ($process in $processes | Sort-Object -Property Name) {
        Write-Host "Stopping $($process.Name) (PID $($process.ProcessId))..."
        Stop-Process -Id ([int]$process.ProcessId) -Force -ErrorAction Stop
    }
}

try {
    Push-Location $repoRoot
    $buildPreset = Get-FastFilesBuildPreset

    switch ($Command) {
        'doctor' {
            if (-not (Show-FastFilesDoctor)) { exit 1 }
        }
        { $_ -in @('setup', 'bootstrap') } {
            $null = Initialize-FastFilesToolchain
            Write-Host 'Bootstrapping the Debug and disposable diagnostic configure trees...' -ForegroundColor Cyan
            Invoke-FastFilesCMake -Arguments @('--preset', 'debug')
            Invoke-FastFilesCMake -Arguments @('--preset', 'debug-diagnostic')
        }
        'configure' {
            $null = Initialize-FastFilesToolchain
            Invoke-FastFilesCMake -Arguments @('--preset', $buildPreset)
        }
        'build' {
            $null = Initialize-FastFilesToolchain
            Build-FastFiles -BuildPreset $buildPreset -BuildTarget $Target
        }
        'run' {
            $null = Initialize-FastFilesToolchain
            if (-not $SkipBuild) {
                Build-FastFiles -BuildPreset $buildPreset
            }
            $executable = Join-Path (Get-FastFilesBuildDirectory -BuildPreset $buildPreset) 'src/ui/FastFiles.exe'
            if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
                throw "FastFiles.exe is missing at '$executable'. Run .\dev.ps1 build -Preset $buildPreset."
            }
            $workingDirectory = [IO.Path]::GetDirectoryName($executable)
            if ($Gallery) {
                $started = Start-Process -FilePath $executable -WorkingDirectory $workingDirectory -ArgumentList '--gallery' -PassThru
            } else {
                $started = Start-Process -FilePath $executable -WorkingDirectory $workingDirectory -PassThru
            }
            Write-Host "FastFiles started (PID $($started.Id))." -ForegroundColor Green
            Write-Host "Executable: $executable"
            Write-Host 'Use .\dev.ps1 stop when the manual run is complete.'
        }
        { $_ -in @('test', 'test-full') } {
            $null = Initialize-FastFilesToolchain
            Build-FastFiles -BuildPreset $buildPreset
            $testDirectory = Get-FastFilesBuildDirectory -BuildPreset $buildPreset
            Invoke-FastFilesTool -FilePath $toolchain.CTestExe `
                -Arguments @('--test-dir', $testDirectory, '--output-on-failure') `
                -Label "CTest '$buildPreset'"
        }
        'test-fast' {
            $null = Initialize-FastFilesToolchain
            $focusedTarget = if (-not [string]::IsNullOrWhiteSpace($Target)) { $Target } else { $Filter }
            if ([string]::IsNullOrWhiteSpace($focusedTarget)) { $focusedTarget = 'ffpreview_tests' }
            $testFilter = if (-not [string]::IsNullOrWhiteSpace($Filter)) { $Filter } else { $focusedTarget }
            if (-not $SkipBuild) {
                if (Test-FastFilesBuildTarget -BuildPreset $buildPreset -BuildTarget $focusedTarget) {
                    Build-FastFiles -BuildPreset $buildPreset -BuildTarget $focusedTarget
                } else {
                    # PowerShell-only CTest entries are registered without a
                    # corresponding Ninja target; build the preset, then run
                    # the requested CTest filter instead of treating that as a
                    # broken product target.
                    Write-Host "'$focusedTarget' is CTest-only; building the full '$buildPreset' preset..." -ForegroundColor Yellow
                    Build-FastFiles -BuildPreset $buildPreset
                }
            }
            $testDirectory = Get-FastFilesBuildDirectory -BuildPreset $buildPreset
            Invoke-FastFilesTool -FilePath $toolchain.CTestExe `
                -Arguments @('--test-dir', $testDirectory, '-R', $testFilter, '--output-on-failure') `
                -Label "Focused CTest '$testFilter'"
        }
        'clean' {
            $null = Initialize-FastFilesToolchain
            $buildDirectory = Assert-FastFilesBuildTarget -BuildPreset $buildPreset
            if (Test-Path -LiteralPath $buildDirectory) {
                Remove-Item -LiteralPath $buildDirectory -Recurse -Force
                Write-Host "Removed generated build tree: $buildDirectory"
            } else {
                Write-Host "Build tree does not exist: $buildDirectory"
            }
        }
        'rebuild' {
            $null = Initialize-FastFilesToolchain
            $buildDirectory = Assert-FastFilesBuildTarget -BuildPreset $buildPreset
            if (Test-Path -LiteralPath $buildDirectory) {
                Remove-Item -LiteralPath $buildDirectory -Recurse -Force
            }
            Build-FastFiles -BuildPreset $buildPreset -BuildTarget $Target
        }
        'package' {
            $null = Initialize-FastFilesToolchain
            if ($buildPreset -ne 'release-test') {
                throw "The package command is reserved for the unsigned 'release-test' preset. Use .\dev.ps1 build -Preset $buildPreset for another build."
            }
            Build-FastFiles -BuildPreset $buildPreset
            Write-Host 'Development package build complete. This is the unsigned release-test artifact, not a production installer.' -ForegroundColor Green
            Write-Host "FastFiles.exe: $(Join-Path (Get-FastFilesBuildDirectory -BuildPreset $buildPreset) 'src/ui/FastFiles.exe')"
        }
        'logs' {
            $logPath = Join-Path (Join-Path $env:LOCALAPPDATA 'FastFiles') 'logs/diagnostics.log'
            Write-Host "Diagnostics: $logPath"
            if (Test-Path -LiteralPath $logPath -PathType Leaf) {
                Get-Content -LiteralPath $logPath -Tail 80
            } else {
                Write-Host 'No diagnostic log has been created yet.'
            }
        }
        'stop' {
            Stop-FastFilesDevelopmentProcesses
        }
        'fixture' {
            $fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) 'FastFiles-dogfood'
            $fixtureScript = Join-Path $repoRoot 'verify/dogfood/New-DogfoodFixture.ps1'
            $arguments = @('-Root', $fixtureRoot, '-Profile', 'Small')
            if ($ResetFixture) { $arguments += '-Reset' }
            Invoke-FastFilesPowerShellScript -ScriptPath $fixtureScript -Arguments $arguments
            Write-Host "Safe fixture: $fixtureRoot" -ForegroundColor Green
            Write-Host 'Open it in FastFiles with Ctrl+L; it is outside the repository and marked for safe cleanup.'
        }
        'fixture-clean' {
            $fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) 'FastFiles-dogfood'
            $fixtureScript = Join-Path $repoRoot 'verify/dogfood/Reset-DogfoodFixture.ps1'
            Invoke-FastFilesPowerShellScript -ScriptPath $fixtureScript -Arguments @('-Root', $fixtureRoot)
        }
    }
} catch {
    Write-Error $_.Exception.Message
    exit 1
} finally {
    Pop-Location
}
