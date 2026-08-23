# Phase-8 build/test environment helper (VS 2022 BuildTools + bundled CMake/Ninja).
# Usage: pwsh ./verify/phase8env.ps1 <command...>   e.g.: ... cmake --build --preset debug
$vsRoot = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
$vcvars = Join-Path $vsRoot "VC\Auxiliary\Build\vcvars64.bat"
$cmakeBin = Join-Path $vsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
$ninjaBin = Join-Path $vsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
$env:PATH = "$cmakeBin;$ninjaBin;$env:PATH"
$output = & cmd /c "`"$vcvars`" > nul 2>&1 && set" 2>&1
foreach ($line in $output) {
    if ($line -match '^([^=]+)=(.*)$') {
        [System.Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], "Process") | Out-Null
    }
}
& $args[0] @($args | Select-Object -Skip 1)
exit $LASTEXITCODE
