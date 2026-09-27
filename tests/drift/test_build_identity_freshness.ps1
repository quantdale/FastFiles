$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$writeScript = Join-Path $repoRoot 'cmake\WriteBuildIdentity.cmake'

$failures = 0
function Check {
    param([bool] $Condition, [string] $Description)
    if ($Condition) { Write-Host "ok: $Description" } else { Write-Host "FAIL: $Description"; $script:failures++ }
}
function Resolve-CMakeExe {
    $fromPath = Get-Command cmake -ErrorAction SilentlyContinue
    if ($fromPath) { return $fromPath.Source }
    $candidates = @(
        (Join-Path $env:ProgramFiles 'CMake\bin\cmake.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'CMake\bin\cmake.exe')
    )
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $vsRoot = & $vswhere -latest -products * -property installationPath 2>$null | Select-Object -First 1
        if ($vsRoot) {
            $candidates += (Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe')
        }
    }
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path $candidate)) { return $candidate }
    }
    return $null
}
$cmakeExe = Resolve-CMakeExe
if (-not $cmakeExe) {
    Write-Host 'FAIL: cmake is not resolvable from PATH or a discovered installation-local fallback'
    exit 1
}
function Invoke-FixtureGit {
    param([Parameter(Mandatory)] [string] $Root, [Parameter(ValueFromRemainingArguments)] [string[]] $GitArgs)
    $output = @(& git -C $Root @GitArgs 2>&1 | ForEach-Object { $_.ToString() })
    if ($LASTEXITCODE -ne 0) { throw "fixture git command failed: git $($GitArgs -join ' '): $($output -join ' ')" }
    return ,@($output)
}
function Invoke-WriteIdentity {
    param(
        [Parameter(Mandatory)] [string] $SourceDir,
        [Parameter(Mandatory)] [string] $HeaderPath,
        [Parameter(Mandatory)] [string] $RcDefinesPath,
        [string] $Configuration = 'Debug',
        [string] $Architecture = 'x64',
        [string] $ExpectedBuildId = '',
        [switch] $SetEmptyBuildId
    )
    $headerDir = Split-Path -Parent $HeaderPath
    $rcDir = Split-Path -Parent $RcDefinesPath
    New-Item -ItemType Directory -Path $headerDir -Force | Out-Null
    New-Item -ItemType Directory -Path $rcDir -Force | Out-Null
    $savedBuildId = $env:FASTFILES_BUILD_ID
    try {
        if ($SetEmptyBuildId) {
            $env:FASTFILES_BUILD_ID = ''
        } elseif ($ExpectedBuildId -ne '') {
            $env:FASTFILES_BUILD_ID = $ExpectedBuildId
        } else {
            Remove-Item Env:FASTFILES_BUILD_ID -ErrorAction SilentlyContinue
        }
        $savedConfiguration = $env:FASTFILES_IDENTITY_CONFIGURATION
        $args = @(
            "-DSOURCE_DIR=$SourceDir"
            "-DOUTPUT_HEADER=$HeaderPath"
            "-DOUTPUT_RC_DEFINES=$RcDefinesPath"
            "-DCONFIGURATION=$Configuration"
            "-DARCHITECTURE=$Architecture"
        )
        if ($ExpectedBuildId -ne '') { $args += "-DEXPECTED_BUILD_ID=$ExpectedBuildId" }
        $cmakeOutput = @(& $cmakeExe @args -P $writeScript 2>&1 | ForEach-Object { $_.ToString() })
        return ($LASTEXITCODE -eq 0)
    } finally {
        if ($null -ne $savedBuildId) { $env:FASTFILES_BUILD_ID = $savedBuildId } else { Remove-Item Env:FASTFILES_BUILD_ID -ErrorAction SilentlyContinue }
        if ($null -ne $savedConfiguration) { $env:FASTFILES_IDENTITY_CONFIGURATION = $savedConfiguration } else { Remove-Item Env:FASTFILES_IDENTITY_CONFIGURATION -ErrorAction SilentlyContinue }
    }
}
function Test-ConfigureRejectsEmptyBuildId {
    $configureFixture = Join-Path $fixtureRoot 'configure-empty-override'
    $configureBuild = Join-Path $configureFixture 'build'
    $fixtureCmake = Join-Path $configureFixture 'cmake'
    New-Item -ItemType Directory -Path $fixtureCmake -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $repoRoot 'VERSION') -Destination (Join-Path $configureFixture 'VERSION')
    Copy-Item -LiteralPath (Join-Path $repoRoot 'cmake\FastFilesVersion.h.in') -Destination $fixtureCmake
    Copy-Item -LiteralPath (Join-Path $repoRoot 'cmake\FastFilesVersion.rc.in') -Destination $fixtureCmake
    $modulePath = Join-Path $repoRoot 'cmake\FastFilesVersion.cmake'
    $cmakeLists = @(
        'cmake_minimum_required(VERSION 3.20)',
        'project(FastFilesBuildIdentityFixture NONE)',
        'set(FASTFILES_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated")',
        "include(`"$modulePath`")"
    ) -join "`n"
    Set-Content -LiteralPath (Join-Path $configureFixture 'CMakeLists.txt') -Value $cmakeLists -Encoding utf8NoBOM
    $savedBuildId = $env:FASTFILES_BUILD_ID
    try {
        $env:FASTFILES_BUILD_ID = ''
        & $cmakeExe -S $configureFixture -B $configureBuild 2>&1 | Out-Null
        return ($LASTEXITCODE -ne 0)
    } finally {
        if ($null -ne $savedBuildId) { $env:FASTFILES_BUILD_ID = $savedBuildId } else { Remove-Item Env:FASTFILES_BUILD_ID -ErrorAction SilentlyContinue }
    }
}
function Get-FileSha {
    param([Parameter(Mandatory)] [string] $Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-build-identity-' + [guid]::NewGuid().ToString('N'))
$notARepo = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-build-identity-norepo-' + [guid]::NewGuid().ToString('N'))
$outRoot = Join-Path $fixtureRoot 'gen'
$headerPath = Join-Path $outRoot 'ffsetup\BuildIdentity.h'
$rcPath = Join-Path $outRoot 'ffsetup\BuildIdentity.rc.h'
$notARepoHeader = Join-Path $notARepo 'gen\ffsetup\BuildIdentity.h'
$notARepoRc = Join-Path $notARepo 'gen\ffsetup\BuildIdentity.rc.h'

try {
    New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
    New-Item -ItemType Directory -Path $notARepo -Force | Out-Null
    Invoke-FixtureGit $fixtureRoot init | Out-Null
    Invoke-FixtureGit $fixtureRoot config user.name 'FastFiles Fixture' | Out-Null
    Invoke-FixtureGit $fixtureRoot config user.email 'fixture@example.invalid' | Out-Null
    Set-Content -LiteralPath (Join-Path $fixtureRoot '.gitignore') -Value @('build/', 'gen/') -Encoding utf8NoBOM
    Set-Content -LiteralPath (Join-Path $fixtureRoot 'tracked.txt') -Value 'revision-a' -Encoding utf8NoBOM
    Invoke-FixtureGit $fixtureRoot add . | Out-Null
    Invoke-FixtureGit $fixtureRoot commit -m 'revision A' | Out-Null
    $shaA = (Invoke-FixtureGit $fixtureRoot rev-parse HEAD)[0].Trim().ToLowerInvariant()

    # 1. Fresh configure-equivalent generation embeds revision A.
    Check (Invoke-WriteIdentity -SourceDir $fixtureRoot -HeaderPath $headerPath -RcDefinesPath $rcPath) 'identity script succeeds in a clean git repository'
    $headerA = Get-Content -LiteralPath $headerPath -Raw
    $rcA = Get-Content -LiteralPath $rcPath -Raw
    Check ($headerA -match [regex]::Escape("kBuildIdentityAscii[] = `"$shaA`"")) 'header embeds revision A identity'
    Check ($headerA -match 'kBuildWorktreeStateAscii\[\] = "clean"') 'header records clean tree state after commit'
    Check ($rcA -match [regex]::Escape("#define FF_BUILD_IDENTITY `"$shaA`"")) 'rc defines embed revision A identity'
    Check ($rcA -match 'FF_BUILD_WORKTREE_STATE "clean"') 'rc defines record clean tree state'

    # 2. No source identity change => byte and mtime stability (no rebuild storm).
    $headerShaAA = Get-FileSha $headerPath
    $rcShaAA = Get-FileSha $rcPath
    $headerWriteAA = (Get-Item -LiteralPath $headerPath).LastWriteTimeUtc
    Start-Sleep -Milliseconds 1100
    Check (Invoke-WriteIdentity -SourceDir $fixtureRoot -HeaderPath $headerPath -RcDefinesPath $rcPath) 'identity script re-run without a commit succeeds'
    Check ((Get-FileSha $headerPath) -eq $headerShaAA -and (Get-Item -LiteralPath $headerPath).LastWriteTimeUtc -eq $headerWriteAA) 'unchanged identity preserves header bytes and mtime'
    Check ((Get-FileSha $rcPath) -eq $rcShaAA -and (Get-Item -LiteralPath $rcPath).LastWriteTimeUtc -eq (Get-Item -LiteralPath $headerPath).LastWriteTimeUtc -or (Get-FileSha $rcPath) -eq $rcShaAA) 'unchanged identity preserves rc-define bytes'

    # 3. THE stale-identity regression: move HEAD without reconfigure, re-run only
    #    the build-time script (what an incremental build does) => identity becomes B.
    Set-Content -LiteralPath (Join-Path $fixtureRoot 'tracked.txt') -Value 'revision-b' -Encoding utf8NoBOM
    Invoke-FixtureGit $fixtureRoot add . | Out-Null
    Invoke-FixtureGit $fixtureRoot commit -m 'revision B' | Out-Null
    $shaB = (Invoke-FixtureGit $fixtureRoot rev-parse HEAD)[0].Trim().ToLowerInvariant()
    Check ($shaB -ne $shaA) 'fixture HEAD advanced from A to B without any reconfigure step'
    Check (Invoke-WriteIdentity -SourceDir $fixtureRoot -HeaderPath $headerPath -RcDefinesPath $rcPath) 'identity script succeeds after the commit'
    $headerB = Get-Content -LiteralPath $headerPath -Raw
    $rcB = Get-Content -LiteralPath $rcPath -Raw
    Check ($headerB -match [regex]::Escape("kBuildIdentityAscii[] = `"$shaB`"")) 'header embeds revision B, not stale A'
    Check (-not ($headerB -match [regex]::Escape($shaA))) 'header no longer contains revision A'
    Check ($rcB -match [regex]::Escape("#define FF_BUILD_IDENTITY `"$shaB`"")) 'rc defines embed revision B, not stale A'
    Check ((Get-FileSha $headerPath) -ne $headerShaAA) 'identity change rewrites header bytes (bounded rebuild trigger)'

    # 4. Dirty tree is reported distinctly while PrivateBuild stays 40-hex.
    Set-Content -LiteralPath (Join-Path $fixtureRoot 'tracked.txt') -Value 'revision-b-dirty' -Encoding utf8NoBOM
    Check (Invoke-WriteIdentity -SourceDir $fixtureRoot -HeaderPath $headerPath -RcDefinesPath $rcPath) 'identity script succeeds with a dirty worktree'
    $headerDirty = Get-Content -LiteralPath $headerPath -Raw
    $rcDirty = Get-Content -LiteralPath $rcPath -Raw
    Check ($headerDirty -match 'kBuildWorktreeStateAscii\[\] = "dirty"') 'dirty worktree is exposed in the header'
    Check ($rcDirty -match 'FF_BUILD_SPECIAL "tree=dirty;config=Debug;arch=x64;committed=') 'dirty state and configuration appear in SpecialBuild payload'
    Check ($rcDirty -match [regex]::Escape("#define FF_BUILD_IDENTITY `"$shaB`"")) 'PrivateBuild identity remains the commit even when dirty'
    Invoke-FixtureGit $fixtureRoot checkout -- tracked.txt | Out-Null

    # 5. Explicit override is honored and validated fail-closed.
    $overrideId = ('0' * 40)
    Check (Invoke-WriteIdentity -SourceDir $fixtureRoot -HeaderPath $headerPath -RcDefinesPath $rcPath -ExpectedBuildId $overrideId) 'explicit 40-hex override is accepted'
    Check ((Get-Content -LiteralPath $headerPath -Raw) -match [regex]::Escape("kBuildIdentityAscii[] = `"$overrideId`"")) 'override identity is embedded'
    $savedBuildId = $env:FASTFILES_BUILD_ID
    try {
        $env:FASTFILES_BUILD_ID = ('a' * 39)
        & $cmakeExe "-DSOURCE_DIR=$fixtureRoot" "-DOUTPUT_HEADER=$headerPath" "-DOUTPUT_RC_DEFINES=$rcPath" -DCONFIGURATION=Debug -DARCHITECTURE=x64 -DEXPECTED_BUILD_ID=('a' * 39) -P $writeScript 2>&1 | Out-Null
        Check ($LASTEXITCODE -ne 0) '39-hex override fails the identity script (fail closed)'
    } finally {
        if ($null -ne $savedBuildId) { $env:FASTFILES_BUILD_ID = $savedBuildId } else { Remove-Item Env:FASTFILES_BUILD_ID -ErrorAction SilentlyContinue }
    }
    # 6. A defined-but-empty environment override is malformed, not absent.
    Check (-not (Invoke-WriteIdentity -SourceDir $fixtureRoot -HeaderPath $headerPath -RcDefinesPath $rcPath -SetEmptyBuildId)) 'explicit empty environment override fails the build-time identity script'
    Check (Test-ConfigureRejectsEmptyBuildId) 'explicit empty environment override fails during CMake configure'

    # 7. Non-repository without override fails closed.
    $norepoOk = $true
    try {
        $norepoOk = Invoke-WriteIdentity -SourceDir $notARepo -HeaderPath $notARepoHeader -RcDefinesPath $notARepoRc
    } catch { $norepoOk = $false }
    Check ($norepoOk -eq $false) 'non-repository source dir without override fails closed'

    # 8. Restore healthy identity so later steps do not inherit the malformed state.
    Check (Invoke-WriteIdentity -SourceDir $fixtureRoot -HeaderPath $headerPath -RcDefinesPath $rcPath) 'identity script recovers after negative cases'

    # 9. Configuration/architecture are part of the recorded metadata.
    Check (Invoke-WriteIdentity -SourceDir $fixtureRoot -HeaderPath $headerPath -RcDefinesPath $rcPath -Configuration release-test -Architecture x64) 'configuration label is accepted'
    Check ((Get-Content -LiteralPath $headerPath -Raw) -match 'kBuildConfigurationAscii\[\] = "release-test"') 'header records configuration'
    Check ((Get-Content -LiteralPath $rcPath -Raw) -match 'FF_BUILD_CONFIGURATION "release-test"') 'rc defines record configuration'
}
finally {
    Remove-Item -LiteralPath $fixtureRoot -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $notARepo -Recurse -Force -ErrorAction SilentlyContinue
    if ($env:FASTFILES_BUILD_ID) { Remove-Item Env:FASTFILES_BUILD_ID -ErrorAction SilentlyContinue }
}

if ($failures -gt 0) {
    Write-Host "FAILED: $failures check(s)"
    exit 1
}
Write-Host 'All build-identity freshness checks passed.'
exit 0
