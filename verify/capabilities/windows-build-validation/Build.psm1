<#
    windows-build-validation capability (Tier 0): toolchain discovery + Developer-
    environment activation, Debug/Release/analyze builds via CMake presets (clean or
    incremental), compiler/linker diagnostics parsing, and the ctest unit+fuzz suites
    with JUnit output. Implements the capability interface (D11): availability(), run(),
    diagnostics().
#>

Import-Module (Join-Path $PSScriptRoot '..\..\core\Toolchain.psm1') -Force -Global
Import-Module (Join-Path $PSScriptRoot '..\..\core\Descriptors.psm1') -Force -Global

function Test-BuildCapabilityAvailability {
    [CmdletBinding()]
    param([Parameter(Mandatory)] $Fingerprint)

    if (-not $Fingerprint.Toolchain) {
        return [pscustomobject]@{
            Available       = $false
            Reason          = 'vs-toolchain-not-found'
            RequiredContext = [pscustomobject]@{ needs = 'VS install with VC.Tools.x86.x64 + bundled CMake/Ninja (discoverable via vswhere)' }
        }
    }
    return [pscustomobject]@{ Available = $true; Reason = $null; RequiredContext = $null }
}

function Get-BuildCapabilityDiagnostics {
    [CmdletBinding()]
    param()
    return @('configure-log', 'build-log', 'ctest-log', 'junit-xml', 'build-summary')
}

function ConvertTo-CompilerDiagnostics {
    param([string[]] $Lines)
    $diagnostics = @()
    foreach ($line in $Lines) {
        $text = $line.ToString()
        if ($text -match '^(?<file>.+?)\((?<line>\d+)(,\d+)?\):\s+(?<sev>error|warning)\s+(?<code>[A-Za-z]+\d+):\s+(?<msg>.*)$') {
            $diagnostics += [pscustomobject]@{
                file = $Matches['file']; line = [int]$Matches['line']
                severity = $Matches['sev']; code = $Matches['code']; message = $Matches['msg'].Trim()
            }
        } elseif ($text -match '^(?<file>.+?)\s*:\s+(?<sev>error|warning)\s+(?<code>[A-Za-z]+\d+):\s+(?<msg>.*)$') {
            $diagnostics += [pscustomobject]@{
                file = $Matches['file']; line = $null
                severity = $Matches['sev']; code = $Matches['code']; message = $Matches['msg'].Trim()
            }
        }
    }
    return $diagnostics
}

function Get-FirstFailingTarget {
    param([string[]] $Lines)
    foreach ($line in $Lines) {
        if ($line.ToString() -match '^FAILED:\s*(.+)$') { return $Matches[1].Trim() }
    }
    return $null
}

function Invoke-ToolInRepo {
    param([string] $Exe, [string[]] $ArgList, [string] $RepoRoot)
    Push-Location $RepoRoot
    try {
        $output = & $Exe @ArgList 2>&1 | ForEach-Object { $_.ToString() }
        $exitCode = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    [pscustomobject]@{ ExitCode = $exitCode; Output = @($output) }
}

function New-SubResult {
    param([string] $Id, [int] $Tier, [string] $Status, [string] $Reason, [double] $DurationMs, [string] $Detail, [array] $Diagnostics = @())
    [pscustomobject]@{
        id = $Id; tier = $Tier; status = $Status; reason = $Reason
        requiredContext = $null; durationMs = [math]::Round($DurationMs, 0)
        detail = $Detail; diagnostics = $Diagnostics
    }
}

function Invoke-ConfigurationPipeline {
    <#
        Configure -> build -> (optional) ctest for one CMake preset. Each step is
        skipped, not silently attempted, once an earlier step in the same pipeline
        has failed (task 4.3/4.4: report which mode was used and pinpoint the first
        failing target/diagnostic rather than only an overall exit code).
    #>
    param(
        [Parameter(Mandatory)] $Toolchain,
        [Parameter(Mandatory)] [string] $RepoRoot,
        [Parameter(Mandatory)] [string] $ArtifactsDir,
        [Parameter(Mandatory)] [string] $Config,
        [bool] $Clean,
        [bool] $RunTests
    )

    $subResults = @()
    $artifacts = @()
    $buildDir = Join-Path $RepoRoot "build\$Config"
    $mode = if ($Clean) { 'clean' } else { 'incremental' }
    if ($Clean -and (Test-Path $buildDir)) {
        Remove-Item -Path $buildDir -Recurse -Force
    }

    $t0 = Get-Date
    $cfg = Invoke-ToolInRepo -Exe $Toolchain.CMakeExe -ArgList @('-S', $RepoRoot, '--preset', $Config) -RepoRoot $RepoRoot
    $t1 = Get-Date
    $cfgLogName = "$Config-configure.log"
    $cfg.Output | Set-Content -Path (Join-Path $ArtifactsDir $cfgLogName) -Encoding utf8
    $artifacts += @{ path = "artifacts/windows-build-validation/$cfgLogName"; type = 'configure-log' }
    $configureOk = ($cfg.ExitCode -eq 0)
    $subResults += New-SubResult -Id "configure-$Config" -Tier 0 -Status $(if ($configureOk) { 'PASS' } else { 'FAIL' }) -Reason $null `
        -DurationMs ($t1 - $t0).TotalMilliseconds -Detail "mode=$mode; exitCode=$($cfg.ExitCode)"

    if (-not $configureOk) {
        $subResults += New-SubResult -Id "build-$Config" -Tier 0 -Status 'SKIPPED' -Reason 'configure-failed' -DurationMs 0 -Detail $null
        if ($RunTests) {
            $subResults += New-SubResult -Id "ctest-$Config" -Tier 0 -Status 'SKIPPED' -Reason 'configure-failed' -DurationMs 0 -Detail $null
        }
        return [pscustomobject]@{ SubResults = $subResults; Artifacts = $artifacts }
    }

    $t0 = Get-Date
    $bld = Invoke-ToolInRepo -Exe $Toolchain.CMakeExe -ArgList @('--build', '--preset', $Config) -RepoRoot $RepoRoot
    $t1 = Get-Date
    $bldLogName = "$Config-build.log"
    $bld.Output | Set-Content -Path (Join-Path $ArtifactsDir $bldLogName) -Encoding utf8
    $artifacts += @{ path = "artifacts/windows-build-validation/$bldLogName"; type = 'build-log' }

    $buildDiagnostics = ConvertTo-CompilerDiagnostics -Lines $bld.Output
    $buildOk = ($bld.ExitCode -eq 0)
    $warnCount = @($buildDiagnostics | Where-Object { $_.severity -eq 'warning' }).Count
    $errCount = @($buildDiagnostics | Where-Object { $_.severity -eq 'error' }).Count
    $firstFailingTarget = if (-not $buildOk) { Get-FirstFailingTarget -Lines $bld.Output } else { $null }

    $detail = "mode=$mode; exitCode=$($bld.ExitCode); warnings=$warnCount; errors=$errCount"
    if ($firstFailingTarget) { $detail += "; firstFailingTarget=$firstFailingTarget" }

    $subResults += New-SubResult -Id "build-$Config" -Tier 0 -Status $(if ($buildOk) { 'PASS' } else { 'FAIL' }) -Reason $null `
        -DurationMs ($t1 - $t0).TotalMilliseconds -Detail $detail -Diagnostics $buildDiagnostics

    if ($RunTests) {
        if (-not $buildOk) {
            $subResults += New-SubResult -Id "ctest-$Config" -Tier 0 -Status 'SKIPPED' -Reason 'build-failed' -DurationMs 0 -Detail $null
        } else {
            $ctestExe = Join-Path (Split-Path $Toolchain.CMakeExe -Parent) 'ctest.exe'
            $junitName = "$Config-junit.xml"
            $junitPath = Join-Path $ArtifactsDir $junitName
            $t0 = Get-Date
            $ct = Invoke-ToolInRepo -Exe $ctestExe -ArgList @('--preset', $Config, '--output-junit', $junitPath) -RepoRoot $RepoRoot
            $t1 = Get-Date
            $ctLogName = "$Config-ctest.log"
            $ct.Output | Set-Content -Path (Join-Path $ArtifactsDir $ctLogName) -Encoding utf8
            $artifacts += @{ path = "artifacts/windows-build-validation/$ctLogName"; type = 'ctest-log' }
            if (Test-Path $junitPath) {
                $artifacts += @{ path = "artifacts/windows-build-validation/$junitName"; type = 'junit-xml' }
            }
            $ctestOk = ($ct.ExitCode -eq 0)
            $subResults += New-SubResult -Id "ctest-$Config" -Tier 0 -Status $(if ($ctestOk) { 'PASS' } else { 'FAIL' }) -Reason $null `
                -DurationMs ($t1 - $t0).TotalMilliseconds -Detail "exitCode=$($ct.ExitCode)"
        }
    }

    return [pscustomobject]@{ SubResults = $subResults; Artifacts = $artifacts }
}

function Invoke-BuildCapability {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] $RunContext,
        [Parameter(Mandatory)] $Fingerprint,
        [Parameter(Mandatory)] [string] $ArtifactsDir,
        [Parameter(Mandatory)] [hashtable] $Options
    )

    $repoRoot = $Options.RepoRoot
    $configurations = if ($Options.Configurations) { @($Options.Configurations) } else { @('debug', 'release') }
    $clean = [bool]$Options.Clean
    $runAnalyze = -not [bool]$Options.SkipAnalyze
    $runTests = -not [bool]$Options.SkipTests
    $toolchain = $Fingerprint.Toolchain

    Enter-DevEnvironment -Toolchain $toolchain

    $subResults = @()
    $artifacts = @()

    $subResults += Invoke-SuiteDescriptors -Fingerprint $Fingerprint -Context $Fingerprint -Descriptors @(
        (New-SuiteDescriptor -Id 'toolchain-discovery' -Tier 0 -Predicate {
            param($fp)
            if ($fp.Toolchain) {
                return @{ Status = 'PASS'; Detail = "$($fp.Toolchain.DisplayName) $($fp.Toolchain.InstallationVersion); cmake=$($fp.Toolchain.CMakeExe); ninja=$($fp.Toolchain.NinjaExe); sdk=$($fp.Toolchain.WindowsSdkVersion)" }
            }
            return @{ Status = 'FAIL'; Detail = 'VS toolchain with VC.Tools.x86.x64 not found' }
        })
    )

    foreach ($config in $configurations) {
        $pipeline = Invoke-ConfigurationPipeline -Toolchain $toolchain -RepoRoot $repoRoot -ArtifactsDir $ArtifactsDir -Config $config -Clean $clean -RunTests $runTests
        $subResults += $pipeline.SubResults
        $artifacts += $pipeline.Artifacts
    }

    if ($runAnalyze) {
        $pipeline = Invoke-ConfigurationPipeline -Toolchain $toolchain -RepoRoot $repoRoot -ArtifactsDir $ArtifactsDir -Config 'analyze' -Clean $clean -RunTests $false
        $subResults += $pipeline.SubResults
        $artifacts += $pipeline.Artifacts
    }

    $buildSummary = [pscustomobject]@{
        configurations = $configurations
        analyzeRun     = $runAnalyze
        testsRun       = $runTests
        toolchain      = $toolchain
        results        = $subResults
    }
    $summaryPath = Join-Path $ArtifactsDir 'build-summary.json'
    $buildSummary | ConvertTo-Json -Depth 12 | Set-Content -Path $summaryPath -Encoding utf8
    $artifacts += @{ path = 'artifacts/windows-build-validation/build-summary.json'; type = 'build-summary' }

    $passCount = @($subResults | Where-Object { $_.status -eq 'PASS' }).Count
    $failCount = @($subResults | Where-Object { $_.status -eq 'FAIL' }).Count
    $skipCount = @($subResults | Where-Object { $_.status -eq 'SKIPPED' }).Count
    $overallStatus = if ($failCount -gt 0) { 'FAIL' } else { 'PASS' }
    $analyzeNote = if ($runAnalyze) { ' +analyze' } else { '' }

    [pscustomobject]@{
        Status     = $overallStatus
        Reason     = $null
        Summary    = "$passCount passed, $failCount failed, $skipCount skipped across $($configurations -join '/')$analyzeNote"
        Artifacts  = $artifacts
        SubResults = $subResults
    }
}

Export-ModuleMember -Function Test-BuildCapabilityAvailability, Invoke-BuildCapability, Get-BuildCapabilityDiagnostics
