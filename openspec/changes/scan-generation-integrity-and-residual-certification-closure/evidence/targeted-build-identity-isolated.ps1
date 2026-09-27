[CmdletBinding()]
param(
    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path,
    [string] $OutputPath = ''
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

$repo = [IO.Path]::GetFullPath($RepoRoot)
$changeEvidence = $PSScriptRoot
$reportPath = if ($OutputPath) { [IO.Path]::GetFullPath($OutputPath) } else {
    Join-Path $changeEvidence 'targeted-build-identity-isolated.json'
}
Import-Module (Join-Path $repo 'verify\core\Toolchain.psm1') -Force
$toolchain = Find-VSToolchain
if (-not $toolchain) { throw 'No VC-capable toolchain found.' }
Enter-DevEnvironment -Toolchain $toolchain -Arch x64

$candidate = (& git -C $repo rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $candidate -notmatch '^[0-9a-fA-F]{40}$') {
    throw 'Could not resolve a 40-character candidate identity.'
}
$runId = [Guid]::NewGuid().ToString('N')
$scratchParent = 'D:\ffp'
$null = New-Item -ItemType Directory -Path $scratchParent -Force
$fixtureRoot = [IO.Path]::GetFullPath((Join-Path $scratchParent $runId.Substring(0, 12)))
$scratchPrefix = [IO.Path]::GetFullPath($scratchParent).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
if (-not $fixtureRoot.StartsWith($scratchPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    (Test-Path -LiteralPath $fixtureRoot)) {
    throw "Refusing a colliding or out-of-scope identity fixture root: $fixtureRoot"
}
$controlRoot = Join-Path $fixtureRoot 'controls'
$null = New-Item -ItemType Directory -Path $fixtureRoot -Force
$null = New-Item -ItemType Directory -Path $controlRoot -Force

$identityTargets = @(
    [pscustomobject]@{ name = 'FastFiles-release-test'; preset = 'release-test'; target = 'FastFiles'; relativePath = 'src\ui\FastFiles.exe'; kind = 'version-resource'; privilegedTrust = $false },
    [pscustomobject]@{ name = 'FastFilesEngine-release-test'; preset = 'release-test'; target = 'FastFilesEngine'; relativePath = 'src\engine\FastFilesEngine.exe'; kind = 'version-resource'; privilegedTrust = $false },
    [pscustomobject]@{ name = 'FastFilesIndexSvc-release-test'; preset = 'release-test'; target = 'FastFilesIndexSvc'; relativePath = 'src\indexsvc\FastFilesIndexSvc.exe'; kind = 'version-resource'; privilegedTrust = $false },
    [pscustomobject]@{ name = 'FastFilesSetup-release-test'; preset = 'release-test'; target = 'FastFilesSetup'; relativePath = 'src\installer\FastFilesSetup.exe'; kind = 'version-resource'; privilegedTrust = $false },
    [pscustomobject]@{ name = 'fftest-release-test'; preset = 'release-test'; target = 'fftest'; relativePath = 'src\fftest\fftest.exe'; kind = 'version-resource'; privilegedTrust = $false },
    [pscustomobject]@{ name = 'package-trust-release-test'; preset = 'release-test'; target = 'ffsetup_package_trust_tests'; relativePath = 'tests\setup\ffsetup_package_trust_tests.exe'; kind = 'embedded-header'; privilegedTrust = $false },
    [pscustomobject]@{ name = 'package-trust-integration-release-test'; preset = 'release-test'; target = 'ffsetup_package_trust_wintrust_integration_tests'; relativePath = 'tests\setup\ffsetup_package_trust_wintrust_integration_tests.exe'; kind = 'embedded-header'; privilegedTrust = $true },
    [pscustomobject]@{ name = 'FastFiles-debug-diagnostic'; preset = 'debug-diagnostic'; target = 'FastFiles'; relativePath = 'src\ui\FastFiles.exe'; kind = 'version-resource'; privilegedTrust = $false }
)

$asanTargets = @(
    'ffprotocol_fuzz_tests',
    'ffipc_framing_tests',
    'ffipc_listener_lifecycle_tests',
    'ffvolumescanner_terminal_tests',
    'ffengine_volume_session_manager_tests',
    'ffengine_legacy_scan_failure_binding_tests'
)
$asanTargetDirectories = @{
    ffprotocol_fuzz_tests = 'protocol'
    ffipc_framing_tests = 'ipc'
    ffipc_listener_lifecycle_tests = 'ipc'
    ffvolumescanner_terminal_tests = 'indexsvc'
    ffengine_volume_session_manager_tests = 'engine'
    ffengine_legacy_scan_failure_binding_tests = 'engine'
}
$ownedWorktrees = [System.Collections.Generic.List[string]]::new()
$hadBuildId = Test-Path Env:\FASTFILES_BUILD_ID
$previousBuildId = if ($hadBuildId) { $env:FASTFILES_BUILD_ID } else { $null }
Remove-Item Env:\FASTFILES_BUILD_ID -ErrorAction SilentlyContinue

$report = [ordered]@{
    schema = 'fastfiles-targeted-build-identity-isolated-v1'
    candidateRevisionA = $candidate.ToLowerInvariant()
    candidateWorkingTree = 'clean commit worktree; no FASTFILES_BUILD_ID override'
    isolatedTargetResults = @()
    asanTargetResults = $null
    staleControls = @()
    overrideChecks = @()
    cleanup = [ordered]@{ fixtureRoot = $fixtureRoot; ownedWorktreesRemoved = $false }
    failure = $null
}

function Invoke-Process {
    param(
        [string] $FileName,
        [string[]] $Arguments,
        [string] $WorkingDirectory,
        [int] $TimeoutSeconds = 3600
    )
    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $FileName
    foreach ($argument in $Arguments) { $psi.ArgumentList.Add($argument) }
    $psi.WorkingDirectory = $WorkingDirectory
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.UseShellExecute = $false
    $process = [System.Diagnostics.Process]::Start($psi)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        try { $process.Kill($true) } catch { }
        return [pscustomobject]@{ exitCode = 124; output = "process timed out after $TimeoutSeconds seconds" }
    }
    return [pscustomobject]@{ exitCode = $process.ExitCode; output = ($stdout.Result + $stderr.Result) }
}

function Invoke-CMake {
    param([string] $WorkingDirectory, [string[]] $Arguments)
    return Invoke-Process -FileName $toolchain.CMakeExe -Arguments $Arguments -WorkingDirectory $WorkingDirectory
}

function Get-BinaryIdentity {
    param([string] $Path, [string] $Kind, [string] $Identity)
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    if ($Kind -eq 'version-resource') {
        return [string](Get-Item -LiteralPath $Path).VersionInfo.PrivateBuild
    }
    $bytes = [IO.File]::ReadAllBytes($Path)
    $asciiText = [Text.Encoding]::ASCII.GetString($bytes)
    $wideText = [Text.Encoding]::Unicode.GetString($bytes)
    $needle = $Identity.ToLowerInvariant()
    return [pscustomobject]@{
        ascii = $asciiText.Contains($needle) -or $asciiText.Contains($needle.ToUpperInvariant())
        utf16 = $wideText.Contains($needle) -or $wideText.Contains($needle.ToUpperInvariant())
        embedded = ($asciiText.Contains($needle) -or $asciiText.Contains($needle.ToUpperInvariant()) -or
                    $wideText.Contains($needle) -or $wideText.Contains($needle.ToUpperInvariant()))
    }
}

function Get-OutputTail {
    param([string] $Output)
    return (($Output -split "`r?`n") | Select-Object -Last 30) -join "`n"
}

function Assert-OwnedBuildPath {
    param([string] $Path)
    $fullPath = [IO.Path]::GetFullPath($Path)
    $rootPrefix = $fixtureRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if ($fullPath -ne $fixtureRoot -and -not $fullPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing cleanup outside this run's owned fixture: $fullPath"
    }
    return $fullPath
}

try {
    $targetIndex = 0
    foreach ($spec in $identityTargets) {
        $targetIndex++
        $worktree = [IO.Path]::GetFullPath((Join-Path $fixtureRoot ("w{0:D2}" -f $targetIndex)))
        $null = Assert-OwnedBuildPath $worktree
        $add = Invoke-Process -FileName 'git' -Arguments @('-C', $repo, 'worktree', 'add', '--detach', $worktree, $candidate) -WorkingDirectory $repo
        if ($add.exitCode -ne 0) { throw "worktree add failed for $($spec.name): $(Get-OutputTail $add.output)" }
        $ownedWorktrees.Add($worktree)

        $configureArgs = @('--preset', $spec.preset)
        if ($spec.privilegedTrust) { $configureArgs += '-DFASTFILES_ENABLE_PRIVILEGED_TRUST_TESTS=ON' }
        $configure = Invoke-CMake -WorkingDirectory $worktree -Arguments $configureArgs
        if ($configure.exitCode -ne 0) { throw "configure failed for $($spec.name): $(Get-OutputTail $configure.output)" }

        $buildDirectory = Join-Path $worktree ("build\" + $spec.preset)
        $buildA = Invoke-CMake -WorkingDirectory $worktree -Arguments @('--build', '--preset', $spec.preset, '--target', $spec.target)
        if ($buildA.exitCode -ne 0) { throw "revision-A target build failed for $($spec.name): $(Get-OutputTail $buildA.output)" }
        $binaryPath = Join-Path $buildDirectory $spec.relativePath
        $identityA = Get-BinaryIdentity -Path $binaryPath -Kind $spec.kind -Identity $candidate
        $aMatches = if ($spec.kind -eq 'version-resource') { $identityA -eq $candidate } else { [bool]$identityA.embedded }
        if (-not $aMatches) { throw "$($spec.name) did not embed revision A '$candidate': $(ConvertTo-Json -InputObject $identityA -Compress)" }

        $staleCopy = Join-Path $controlRoot ($spec.name + '-revisionA.exe')
        Copy-Item -LiteralPath $binaryPath -Destination $staleCopy

        $commitB = Invoke-Process -FileName 'git' -Arguments @('-C', $worktree, '-c', 'commit.gpgsign=false', 'commit', '--allow-empty', '-m', "identity fixture B $($spec.name)") -WorkingDirectory $worktree
        if ($commitB.exitCode -ne 0) { throw "revision-B empty commit failed for $($spec.name): $(Get-OutputTail $commitB.output)" }
        $revisionB = (& git -C $worktree rev-parse HEAD).Trim().ToLowerInvariant()
        if ($LASTEXITCODE -ne 0 -or $revisionB -eq $candidate.ToLowerInvariant()) { throw "revision B did not advance for $($spec.name)" }

        $buildB = Invoke-CMake -WorkingDirectory $worktree -Arguments @('--build', '--preset', $spec.preset, '--target', $spec.target)
        if ($buildB.exitCode -ne 0) { throw "revision-B targeted build failed for $($spec.name): $(Get-OutputTail $buildB.output)" }
        $identityB = Get-BinaryIdentity -Path $binaryPath -Kind $spec.kind -Identity $revisionB
        $bMatches = if ($spec.kind -eq 'version-resource') { $identityB -eq $revisionB } else { [bool]$identityB.embedded }
        $stillA = if ($spec.kind -eq 'version-resource') {
            (Get-BinaryIdentity -Path $binaryPath -Kind $spec.kind -Identity $candidate) -eq $candidate
        } else {
            [bool](Get-BinaryIdentity -Path $binaryPath -Kind $spec.kind -Identity $candidate).embedded
        }
        if (-not $bMatches -or $stillA) {
            throw "$($spec.name) targeted build failed identity transition: A=$candidate B=$revisionB embedded=$(ConvertTo-Json -InputObject $identityB -Compress) stillA=$stillA"
        }

        $staleIdentity = Get-BinaryIdentity -Path $staleCopy -Kind $spec.kind -Identity $candidate
        $staleA = if ($spec.kind -eq 'version-resource') { $staleIdentity -eq $candidate } else { [bool]$staleIdentity.embedded }
        $staleB = if ($spec.kind -eq 'version-resource') {
            (Get-BinaryIdentity -Path $staleCopy -Kind $spec.kind -Identity $revisionB) -eq $revisionB
        } else {
            [bool](Get-BinaryIdentity -Path $staleCopy -Kind $spec.kind -Identity $revisionB).embedded
        }
        if (-not $staleA -or $staleB) { throw "stale control for $($spec.name) did not remain bound to revision A" }

        $report.isolatedTargetResults += [pscustomobject]@{
            name = $spec.name
            preset = $spec.preset
            target = $spec.target
            kind = $spec.kind
            revisionA = $candidate.ToLowerInvariant()
            revisionB = $revisionB
            binary = $spec.relativePath
            revisionAIdentity = $identityA
            revisionBIdentity = $identityB
            revisionBTargetRefreshPass = $bMatches -and -not $stillA
            staleRevisionAControlPass = $staleA -and -not $staleB
            configureExitCode = $configure.exitCode
            targetedBuildAExitCode = $buildA.exitCode
            targetedBuildBExitCode = $buildB.exitCode
        }
        Write-Host "PASS isolated target $($spec.name): A=$($candidate.Substring(0, 8)) B=$($revisionB.Substring(0, 8))"
    }

    # The ASan command targets parser/IPC/engine tests that do not include
    # FastFilesVersion.h. Build the actual local/hosted target sets in one
    # dedicated ASan worktree and assert none falsely claim either revision.
    $asanWorktree = [IO.Path]::GetFullPath((Join-Path $fixtureRoot 'w09'))
    $null = Assert-OwnedBuildPath $asanWorktree
    $asanAdd = Invoke-Process -FileName 'git' -Arguments @('-C', $repo, 'worktree', 'add', '--detach', $asanWorktree, $candidate) -WorkingDirectory $repo
    if ($asanAdd.exitCode -ne 0) { throw "ASan worktree add failed: $(Get-OutputTail $asanAdd.output)" }
    $ownedWorktrees.Add($asanWorktree)
    $asanConfigure = Invoke-CMake -WorkingDirectory $asanWorktree -Arguments @('--preset', 'asan')
    if ($asanConfigure.exitCode -ne 0) { throw "ASan configure failed: $(Get-OutputTail $asanConfigure.output)" }
    $asanBuildA = Invoke-CMake -WorkingDirectory $asanWorktree -Arguments (@('--build', '--preset', 'asan', '--target') + $asanTargets)
    if ($asanBuildA.exitCode -ne 0) { throw "ASan target build at A failed: $(Get-OutputTail $asanBuildA.output)" }
    $asanBuildPath = Join-Path $asanWorktree 'build\asan\tests'
    $asanAtA = @()
    foreach ($target in $asanTargets) {
        $exe = Join-Path (Join-Path $asanBuildPath $asanTargetDirectories[$target]) ($target + '.exe')
        $idA = Get-BinaryIdentity -Path $exe -Kind 'embedded-header' -Identity $candidate
        $asanAtA += [pscustomobject]@{ target = $target; exists = Test-Path -LiteralPath $exe; containsRevisionA = [bool]$idA.embedded }
        if (-not (Test-Path -LiteralPath $exe) -or $idA.embedded) { throw "ASan test target $target is missing or embeds revision A unexpectedly" }
    }
    $asanCommitB = Invoke-Process -FileName 'git' -Arguments @('-C', $asanWorktree, '-c', 'commit.gpgsign=false', 'commit', '--allow-empty', '-m', 'identity fixture B ASan test targets') -WorkingDirectory $asanWorktree
    if ($asanCommitB.exitCode -ne 0) { throw "ASan revision-B commit failed: $(Get-OutputTail $asanCommitB.output)" }
    $asanRevisionB = (& git -C $asanWorktree rev-parse HEAD).Trim().ToLowerInvariant()
    if ($LASTEXITCODE -ne 0 -or $asanRevisionB -eq $candidate.ToLowerInvariant()) { throw 'ASan revision B did not advance' }
    $asanBuildB = Invoke-CMake -WorkingDirectory $asanWorktree -Arguments (@('--build', '--preset', 'asan', '--target') + $asanTargets)
    if ($asanBuildB.exitCode -ne 0) { throw "ASan target build at B failed: $(Get-OutputTail $asanBuildB.output)" }
    $asanAtB = @()
    foreach ($target in $asanTargets) {
        $exe = Join-Path (Join-Path $asanBuildPath $asanTargetDirectories[$target]) ($target + '.exe')
        $idA = Get-BinaryIdentity -Path $exe -Kind 'embedded-header' -Identity $candidate
        $idB = Get-BinaryIdentity -Path $exe -Kind 'embedded-header' -Identity $asanRevisionB
        $asanAtB += [pscustomobject]@{ target = $target; exists = Test-Path -LiteralPath $exe; containsRevisionA = [bool]$idA.embedded; containsRevisionB = [bool]$idB.embedded }
        if (-not (Test-Path -LiteralPath $exe) -or $idA.embedded -or $idB.embedded) { throw "ASan test target $target has an unexpected embedded source identity" }
    }
    $report.asanTargetResults = [pscustomobject]@{
        preset = 'asan'
        commandA = 'cmake --build --preset asan --target ' + ($asanTargets -join ' ')
        commandB = 'cmake --build --preset asan --target ' + ($asanTargets -join ' ')
        revisionA = $candidate.ToLowerInvariant()
        revisionB = $asanRevisionB
        binariesAtA = $asanAtA
        binariesAtB = $asanAtB
        result = 'PASS: targeted ASan binaries have no source-revision carrier and contain neither A nor B'
    }
    Write-Host "PASS ASan targeted test set: $($asanTargets.Count) binaries have no source identity carrier"

    # Verify override semantics on a separate target-specific release-test tree.
    # The normal build-ID freshness suite covers explicit-empty and malformed
    # values; this check confirms a well-formed override reaches the binary.
    $overrideSpec = $identityTargets[1]
    $overrideWorktree = [IO.Path]::GetFullPath((Join-Path $fixtureRoot 'w10'))
    $null = Assert-OwnedBuildPath $overrideWorktree
    $overrideAdd = Invoke-Process -FileName 'git' -Arguments @('-C', $repo, 'worktree', 'add', '--detach', $overrideWorktree, $candidate) -WorkingDirectory $repo
    if ($overrideAdd.exitCode -ne 0) { throw "override worktree add failed: $(Get-OutputTail $overrideAdd.output)" }
    $ownedWorktrees.Add($overrideWorktree)
    $overrideConfigure = Invoke-CMake -WorkingDirectory $overrideWorktree -Arguments @('--preset', 'release-test')
    if ($overrideConfigure.exitCode -ne 0) { throw "override configure failed: $(Get-OutputTail $overrideConfigure.output)" }
    $syntheticId = '0123456789abcdef0123456789abcdef01234567'
    $env:FASTFILES_BUILD_ID = $syntheticId
    $overrideBuild = Invoke-CMake -WorkingDirectory $overrideWorktree -Arguments @('--build', '--preset', 'release-test', '--target', $overrideSpec.target)
    Remove-Item Env:\FASTFILES_BUILD_ID -ErrorAction SilentlyContinue
    if ($overrideBuild.exitCode -ne 0) { throw "well-formed override build failed: $(Get-OutputTail $overrideBuild.output)" }
    $overrideExe = Join-Path (Join-Path $overrideWorktree 'build\release-test') $overrideSpec.relativePath
    $overrideIdentity = Get-BinaryIdentity -Path $overrideExe -Kind 'version-resource' -Identity $syntheticId
    if ($overrideIdentity -ne $syntheticId) { throw "well-formed FASTFILES_BUILD_ID was not embedded as given: $overrideIdentity" }
    $report.overrideChecks += [pscustomobject]@{
        command = 'FASTFILES_BUILD_ID=<40-hex> cmake --build --preset release-test --target FastFilesEngine'
        embeddedIdentity = $overrideIdentity
        embeddedAsGiven = $overrideIdentity -eq $syntheticId
        warnsAboutHeadMismatch = $overrideBuild.output -match 'differs from git HEAD'
        result = 'PASS'
    }
    Write-Host 'PASS well-formed build-ID override is embedded as given'
} catch {
    $report.failure = $_.Exception.Message
    Write-Error $report.failure
} finally {
    if ($hadBuildId) { $env:FASTFILES_BUILD_ID = $previousBuildId }
    else { Remove-Item Env:\FASTFILES_BUILD_ID -ErrorAction SilentlyContinue }

    $cleanupSucceeded = $true
    foreach ($worktree in $ownedWorktrees) {
        $ownedPath = Assert-OwnedBuildPath $worktree
        $remove = Invoke-Process -FileName 'git' -Arguments @('-C', $repo, 'worktree', 'remove', '--force', $ownedPath) -WorkingDirectory $repo
        if ($remove.exitCode -ne 0) {
            $cleanupSucceeded = $false
            Write-Warning "Could not remove owned probe worktree '$ownedPath': $(Get-OutputTail $remove.output)"
        }
    }
    if ($cleanupSucceeded -and (Test-Path -LiteralPath $fixtureRoot)) {
        $ownedFixture = Assert-OwnedBuildPath $fixtureRoot
        if ($ownedFixture -ne $fixtureRoot) { throw 'Fixture cleanup path changed unexpectedly.' }
        Remove-Item -LiteralPath $ownedFixture -Recurse -Force
    }
    $report.cleanup.ownedWorktreesRemoved = $cleanupSucceeded
    $report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $reportPath -Encoding utf8NoBOM
    Write-Host "report written: $reportPath"
}

if ($report.failure) { exit 1 }
