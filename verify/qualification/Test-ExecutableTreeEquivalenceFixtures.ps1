# qualify-external-build-executable-equivalence: real child guard, owned Git
# fixture and generated build artifacts. Never reads runtime/signing evidence.
$ErrorActionPreference = 'Stop'
$guard = Join-Path $PSScriptRoot 'Test-ExecutableTreeEquivalence.ps1'
$pwsh = (Get-Command pwsh -ErrorAction Stop).Source
$script:Failures = 0
function Check([bool] $Condition, [string] $Description) {
    if ($Condition) { Write-Host "PASS: $Description" } else { Write-Host "FAIL: $Description"; $script:Failures++ }
}
function Write-Text([string] $Path, [string] $Text) {
    $null = New-Item -ItemType Directory -Force -Path (Split-Path $Path -Parent)
    [IO.File]::WriteAllText($Path, $Text)
}
function Invoke-Git([string[]] $Arguments) {
    $output = @(& git -C $source @Arguments 2>&1 | ForEach-Object { $_.ToString() })
    if ($LASTEXITCODE) { throw "fixture Git failed: $($output -join ' ')" }
    return ($output -join "`n").Trim()
}
function New-FixtureCommit([string] $Message) {
    $null = Invoke-Git @('add', '.')
    $null = Invoke-Git @('commit', '-m', $Message)
    return Invoke-Git @('rev-parse', 'HEAD')
}
function Write-Cache([string] $Root, [string] $SourceTree = $source, [string] $CacheDir = $Root) {
    Write-Text (Join-Path $Root 'CMakeCache.txt') "CMAKE_HOME_DIRECTORY:INTERNAL=$SourceTree`nCMAKE_CACHEFILE_DIR:INTERNAL=$CacheDir`n"
}
function Write-Commands([string] $Root, [switch] $RelativePaths, [switch] $RelativeDirectory, [string] $AdditionalFile = '') {
    $src = $source.Replace('\', '/')
    $bin = $Root.Replace('\', '/')
    $file = "$src/src/main.cpp"
    $includes = "/I`"$src/src`" /I`"$bin/generated`""
    if ($RelativePaths) {
        $file = [IO.Path]::GetRelativePath($Root, "$source/src/main.cpp").Replace('\', '/')
        $relativeInclude = [IO.Path]::GetRelativePath($Root, "$source/src").Replace('\', '/')
        $includes = "/I`"$relativeInclude`" /Igenerated"
    }
    $directory = if ($RelativeDirectory) { '.' } else { $Root }
    $entries = @(
        @{directory=$directory; file=$file; command="cl $includes /c `"$file`""},
        @{directory=$Root; file="$src/src/app.rc"; command="rc /I`"$src/src`" `"$src/src/app.rc`""},
        @{directory=$Root; file="$bin/generated/version.rc"; command="rc `"$bin/generated/version.rc`""}
    )
    if ($AdditionalFile) {
        $entries += @{directory=$Root; file=$AdditionalFile; command="cl /I`"$src/src`" /c `"$AdditionalFile`""}
    }
    Write-Text (Join-Path $Root 'compile_commands.json') ($entries | ConvertTo-Json -Depth 5)
}
function Invoke-Guard([string] $Name, [string] $Root, [string] $Implementation = $baseline, [string] $Evidence = $docs) {
    $output = Join-Path $session "$Name.json"
    $console = @(& $pwsh -NoProfile -NonInteractive -File $guard -RepoRoot $source `
        -ImplementationSha $Implementation -EvidenceSha $Evidence `
        -CompileCommandsPath (Join-Path $Root 'compile_commands.json') -OutputPath $output -Format human 2>&1 |
        ForEach-Object { $_.ToString() })
    $code = $LASTEXITCODE
    if ($code -ne 0) { Write-Host "guard ${Name}: $($console -join ' ')" }
    $console | Set-Content -LiteralPath (Join-Path $session "$Name.log")
    $record = if (Test-Path -LiteralPath $output) { Get-Content -LiteralPath $output -Raw | ConvertFrom-Json } else { $null }
    return [pscustomobject]@{code=$code; record=$record; text=($console -join "`n")}
}

$session = Join-Path ([IO.Path]::GetTempPath()) ('ff-equivalence-' + [guid]::NewGuid().ToString('N'))
$source = Join-Path $session 'source tree'
$external = Join-Path $session 'binary tree'
$internal = Join-Path $source 'build/custom'
try {
    $null = New-Item -ItemType Directory -Force -Path $source, $external
    $null = Invoke-Git @('init', '-q')
    $null = Invoke-Git @('config', 'user.name', 'FastFiles Fixture')
    $null = Invoke-Git @('config', 'user.email', 'fixture@example.invalid')
    Write-Text (Join-Path $source '.gitignore') "build/`n"
    Write-Text (Join-Path $source 'CMakeLists.txt') 'configure_file(cmake/Version.rc.in version.rc)'
    Write-Text (Join-Path $source 'cmake/Version.rc.in') 'version @VERSION@'
    Write-Text (Join-Path $source 'src/main.cpp') "#include `"root.h`"`n#include `"generated.h`"`nint main() { return 0; }`n"
    Write-Text (Join-Path $source 'src/root.h') "#include `"nested/detail.h`"`n"
    Write-Text (Join-Path $source 'src/nested/detail.h') '#define FIXTURE_VALUE 1'
    Write-Text (Join-Path $source 'src/app.rc') '1 RT_MANIFEST "app.manifest"'
    Write-Text (Join-Path $source 'src/app.manifest') '<assembly />'
    $baseline = New-FixtureCommit 'baseline'
    Write-Text (Join-Path $source 'README.md') 'documentation only'
    $docs = New-FixtureCommit 'docs'
    foreach ($root in @($external, $internal)) {
        Write-Text (Join-Path $root 'generated/version.rc') 'generated version resource'
        Write-Text (Join-Path $root 'generated/generated.h') '#define GENERATED 1'
        Write-Cache $root
        Write-Commands $root
    }
    $externalPass = Invoke-Guard 'external' $external
    Check ($externalPass.code -eq 0 -and $externalPass.record.derivation.coverageVerified) 'external CMake build with generated resource/header proves docs-only equivalence'
    $internalPass = Invoke-Guard 'internal' $internal
    Check ($internalPass.code -eq 0 -and $internalPass.record.derivation.coverageVerified) 'conventional in-repository build retains complete equivalence'
    if ($externalPass.code -eq 0 -and $internalPass.code -eq 0) {
        Check ($externalPass.record.implementationExecutableTreeDigest -eq $internalPass.record.implementationExecutableTreeDigest) 'build location cannot change the derived source digest'
    }
    Write-Commands $external -RelativePaths
    $relative = Invoke-Guard 'relative' $external
    Check ($relative.code -eq 0) 'relative TU and include paths resolve from the compilation working directory'
    Write-Commands $external -RelativePaths -RelativeDirectory
    Check ((Invoke-Guard 'relative-directory' $external).code -eq 0) 'relative compilation directory resolves against the compile database directory'
    Write-Commands $external

    Remove-Item -LiteralPath (Join-Path $external 'generated/generated.h')
    $missingHeader = Invoke-Guard 'missing-generated-header' $external
    Check ($missingHeader.code -ne 0 -and $missingHeader.record.derivation.coverageFailures -contains 'unresolved-quoted-include: src/main.cpp -> generated.h') 'missing generated quoted header fails closed at header coverage'
    Write-Text (Join-Path $external 'generated/generated.h') '#define GENERATED 1'
    Remove-Item -LiteralPath (Join-Path $external 'generated/version.rc')
    $missingResource = Invoke-Guard 'missing-generated-tu' $external
    Check ($missingResource.code -ne 0 -and $missingResource.text -like '*Derived translation units are absent*version.rc*') 'missing generated translation unit is not silently discarded'
    Write-Text (Join-Path $external 'generated/version.rc') 'generated version resource'

    Remove-Item -LiteralPath (Join-Path $external 'CMakeCache.txt')
    $noCache = Invoke-Guard 'missing-cache' $external
    Check ($noCache.code -ne 0 -and $noCache.text -like '*Derived translation units are absent*version.rc*') 'external artifacts without CMake provenance fail closed'
    Write-Cache $external -SourceTree (Join-Path $session 'other source')
    $foreignCache = Invoke-Guard 'foreign-source-cache' $external
    Check ($foreignCache.code -ne 0 -and $foreignCache.text -like '*provenance does not match*') 'cache for another source tree is rejected'
    Write-Cache $external -CacheDir (Join-Path $session 'other build')
    $movedCache = Invoke-Guard 'moved-cache' $external
    Check ($movedCache.code -ne 0 -and $movedCache.text -like '*provenance does not match*') 'cache claiming another binary directory is rejected'
    Write-Cache $external
    Add-Content -LiteralPath (Join-Path $external 'CMakeCache.txt') -Value "CMAKE_HOME_DIRECTORY:INTERNAL=$source"
    $duplicateCache = Invoke-Guard 'duplicate-cache' $external
    Check ($duplicateCache.code -ne 0 -and $duplicateCache.text -like '*provenance is missing or ambiguous*') 'ambiguous duplicate cache provenance is rejected'
    Write-Cache $external

    $foreign = Join-Path $session 'binary tree-sibling/foreign.cpp'
    Write-Text $foreign 'int foreign() { return 0; }'
    Write-Commands $external -AdditionalFile $foreign
    $foreignTu = Invoke-Guard 'foreign-tu' $external
    Check ($foreignTu.code -ne 0 -and $foreignTu.text -like '*Derived translation units are absent*foreign.cpp*') 'untracked TU in a prefix-sibling is not treated as generated'
    Write-Commands $external

    # Commit-based negative controls: the guard must hash CONTENT, retain
    # recursive header/RC/template closure, and notice newly added inputs.
    Write-Text (Join-Path $source 'src/nested/detail.h') '#define FIXTURE_VALUE 2'
    $nested = New-FixtureCommit 'nested header change'
    $nestedFail = Invoke-Guard 'nested-change' $external -Evidence $nested
    Check ($nestedFail.code -ne 0 -and $nestedFail.record.pathsWithChangedContent -contains 'src/nested/detail.h') 'nested header change is explicitly reported as executable-impacting'
    Write-Text (Join-Path $source 'src/nested/detail.h') '#define FIXTURE_VALUE 1'
    Write-Text (Join-Path $source 'src/app.manifest') '<assembly changed="true" />'
    $manifest = New-FixtureCommit 'manifest change'
    $manifestFail = Invoke-Guard 'manifest-change' $external -Evidence $manifest
    Check ($manifestFail.code -ne 0 -and $manifestFail.record.pathsWithChangedContent -contains 'src/app.manifest') 'RC manifest change is explicitly reported'
    Write-Text (Join-Path $source 'src/app.manifest') '<assembly />'
    Write-Text (Join-Path $source 'cmake/Version.rc.in') 'changed template @VERSION@'
    $template = New-FixtureCommit 'template change'
    $templateFail = Invoke-Guard 'template-change' $external -Evidence $template
    Check ($templateFail.code -ne 0 -and $templateFail.record.pathsWithChangedContent -contains 'cmake/Version.rc.in') 'generated-resource recipe change cannot hide behind generated output'
    Write-Text (Join-Path $source 'cmake/Version.rc.in') 'version @VERSION@'
    Write-Text (Join-Path $source 'src/shadow.h') '// new product header'
    $addition = New-FixtureCommit 'added product input'
    $additionFail = Invoke-Guard 'added-input' $external -Evidence $addition
    Check ($additionFail.code -ne 0 -and $additionFail.record.derivation.coverageFailures -contains 'product-path-added-in-evidence: src/shadow.h') 'evidence-side product additions remain fail-closed'
    Remove-Item -LiteralPath (Join-Path $source 'src/shadow.h')

    Write-Text (Join-Path $session 'binary tree-sibling/foreign.h') '// untracked foreign header'
    $escape = '../../binary tree/generated/../../binary tree-sibling/foreign.h'
    Write-Text (Join-Path $source 'src/root.h') "#include `"nested/detail.h`"`n#include `"$escape`"`n"
    $escaped = New-FixtureCommit 'outside quoted include'
    $escapeFail = Invoke-Guard 'escaping-header' $external -Implementation $escaped -Evidence $escaped
    Check ($escapeFail.code -ne 0 -and $escapeFail.record.derivation.coverageFailures -contains "unresolved-quoted-include: src/root.h -> $escape") 'normalized traversal to a prefix-sibling header cannot hide as generated'
    Write-Text (Join-Path $source 'src/root.h') "#include `"nested/detail.h`"`n"

    # Tracked inputs always win, even when the configured binary root happens
    # to contain them. Their CONTENT must still participate in the digest.
    Write-Text (Join-Path $source 'src/main.cpp') "#include `"root.h`"`n#include `"generated.h`"`n#include `"../build/custom/tracked.h`"`nint main() { return 0; }`n"
    Write-Text (Join-Path $internal 'tracked.h') '#define TRACKED 1'
    Write-Text (Join-Path $internal 'tracked.cpp') 'int tracked() { return 1; }'
    $null = Invoke-Git @('add', '-f', 'build/custom/tracked.h', 'build/custom/tracked.cpp')
    $trackedBase = New-FixtureCommit 'tracked inputs in binary tree'
    Write-Commands $internal -AdditionalFile (Join-Path $internal 'tracked.cpp')
    $trackedPass = Invoke-Guard 'tracked-base' $internal -Implementation $trackedBase -Evidence $trackedBase
    Check ($trackedPass.code -eq 0 -and $trackedPass.record.derivation.generatedTranslationUnitCount -eq 1) 'tracked translation unit inside binary root is not counted as generated'
    Write-Text (Join-Path $internal 'tracked.cpp') 'int tracked() { return 2; }'
    $trackedTu = New-FixtureCommit 'tracked binary-root TU change'
    $trackedTuFail = Invoke-Guard 'tracked-tu-change' $internal -Implementation $trackedBase -Evidence $trackedTu
    Check ($trackedTuFail.code -ne 0 -and $trackedTuFail.record.pathsWithChangedContent -contains 'build/custom/tracked.cpp') 'tracked TU beneath binary root remains content-sensitive'
    Write-Text (Join-Path $internal 'tracked.cpp') 'int tracked() { return 1; }'
    Write-Text (Join-Path $internal 'tracked.h') '#define TRACKED 2'
    $trackedHeader = New-FixtureCommit 'tracked binary-root header change'
    $trackedHeaderFail = Invoke-Guard 'tracked-header-change' $internal -Implementation $trackedBase -Evidence $trackedHeader
    Check ($trackedHeaderFail.code -ne 0 -and $trackedHeaderFail.record.pathsWithChangedContent -contains 'build/custom/tracked.h') 'tracked header beneath binary root remains content-sensitive'

    Write-Cache $session
    Write-Commands $session
    $ancestorFail = Invoke-Guard 'ancestor-root' $session
    Check ($ancestorFail.code -ne 0 -and $ancestorFail.text -like '*must not contain the source tree*') 'binary-root exemption cannot cover the entire source tree'
} finally {
    # This uniquely created test directory is the only cleanup authority.
    if (Test-Path -LiteralPath $session) { Remove-Item -LiteralPath $session -Recurse -Force }
}
if ($script:Failures) { exit 1 }
Write-Host 'All executable-equivalence fixtures passed.'
exit 0
