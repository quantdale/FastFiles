<#
    Test-ExecutableTreeEquivalence.ps1

    Proves that two commits produce the same executable-impacting source tree,
    so a documentation-or-evidence-only commit can be shown NOT to invalidate a
    binary certification taken at another SHA.

    The set of executable-impacting files is DERIVED, not hard-coded:

      1. Every translation unit the build actually compiles is read from the
         generated compile_commands.json, which is the authoritative record of
         what the compiler consumes.
      2. Every '#include' in those translation units is resolved recursively
         against the include paths that same compile_commands.json records, so
         the header closure is discovered rather than guessed.
      3. The build inputs themselves are added: every CMakeLists.txt under the
         two commits, the CMake modules they reference, the compile database,
         and the version identity inputs the build identity generator reads.

    Coverage is then VERIFIED, not assumed: the script fails unless every
    derived path exists in both commits, every CMakeLists.txt under src/ and
    cmake/ is present in the derivation, and the derivation covers at least one
    translation unit. A path list that cannot be proven complete is a failure,
    not a pass.

    Output is a machine-readable equivalence record.
#>
[CmdletBinding()]
param(
    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [Parameter(Mandatory)] [string] $ImplementationSha,
    [Parameter(Mandatory)] [string] $EvidenceSha,
    [string] $OutputPath,
    [string] $CompileCommandsPath,
    [ValidateSet('human', 'json')]
    [string] $Format = 'json'
)

$ErrorActionPreference = 'Stop'

if (-not $OutputPath) {
    $OutputPath = Join-Path $RepoRoot 'verify\status\generated\executable-tree-equivalence.json'
}

function Invoke-RepositoryGit {
    param([string[]] $Arguments)
    $out = & git -C $RepoRoot @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw "git $($Arguments -join ' ') failed: $($out -join ' ')" }
    return @($out | ForEach-Object { $_.ToString() })
}

function Get-TrackedPaths {
    param([string] $Sha)
    # NUL-delimited so paths containing unusual characters survive.
    $raw = & git -C $RepoRoot ls-tree -r --name-only -z $Sha 2>&1
    if ($LASTEXITCODE -ne 0) { throw "git ls-tree failed for ${Sha}: $($raw -join ' ')" }
    $text = ($raw -join "`0")
    return @($text -split "`0" | Where-Object { $_ -ne '' } | Sort-Object -Unique)
}

# Resolves each path to its git blob object id at a revision. A blob id is a
# content hash, so two revisions with identical path SETS but different
# CONTENT produce different mappings. Hashing only path names (an earlier
# revision of this script) would declare any pair with the same file layout
# equivalent, which silently passed a real product-source change.
function Get-BlobIdMap {
    param([string] $Sha, [string[]] $Paths)
    $map = @{}
    if (-not $Paths -or $Paths.Count -eq 0) { return $map }
    $null = & git -C $RepoRoot ls-tree -r $Sha -- @($Paths) 2>&1
    if ($LASTEXITCODE -ne 0) { throw "git ls-tree <blobs> failed for ${Sha}" }
    foreach ($line in @(& git -C $RepoRoot ls-tree -r $Sha -- @($Paths) 2>&1 | ForEach-Object { $_.ToString() })) {
        # "<mode> SP <type> SP <object> TAB <path>"
        $tabIndex = $line.IndexOf("`t")
        if ($tabIndex -lt 0) { continue }
        $meta = $line.Substring(0, $tabIndex)
        $path = $line.Substring($tabIndex + 1)
        $parts = $meta -split '\s+'
        if ($parts.Length -lt 3) { continue }
        $map[$path] = $parts[2].ToLowerInvariant()
    }
    return $map
}

# Reads a blob's text at a given revision. Returns $null when the path is
# absent from that revision, which is itself an executable-impacting signal.
function Get-BlobText {
    param([string] $Revision, [string] $Path)
    $stdout = [IO.Path]::GetTempFileName()
    $stderr = [IO.Path]::GetTempFileName()
    try {
        $proc = Start-Process -FilePath 'git' -ArgumentList @('-C', $RepoRoot, 'cat-file', '-p', "$($Revision):$($Path)") `
            -NoNewWindow -Wait -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        if ($proc.ExitCode -ne 0) { return $null }
        # -Raw returns the blob as ONE string. Returning a per-line array here
        # silently broke the include closure once: [regex]::Matches coerces a
        # string[] argument by joining with spaces, which destroys the (?m)^
        # line anchors, so only a file's very first line could ever match an
        # #include and nested headers were silently excluded from the digest.
        $text = Get-Content -LiteralPath $stdout -Raw -ErrorAction SilentlyContinue
        if ($null -eq $text) { return '' }
        return [string]$text
    } finally {
        Remove-Item -LiteralPath $stdout -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $stderr -Force -ErrorAction SilentlyContinue
    }
}

# A stable, path-set-derived digest. Only the executable-impacting paths and
# their blob ids participate, so an unrelated documentation commit cannot move
# it while any executable input change always does.
function Get-ExecutableTreeDigest {
    param([string] $Sha, [string[]] $Paths, [hashtable] $BlobMap)
    $hasher = [Security.Cryptography.SHA256]::Create()
    try {
        $sb = [Text.StringBuilder]::new()
        foreach ($p in ($Paths | Sort-Object -Unique)) {
            # Hash "<path>\0<blobId>". Including the blob id is what makes this
            # content-sensitive: a content change under an unchanged path moves
            # the digest, which hashing path names alone could never detect.
            $blob = if ($BlobMap -and $BlobMap.ContainsKey($p)) { [string]$BlobMap[$p] } else { 'MISSING' }
            [void]$sb.Append($p).Append("`0").Append($blob).Append("`n")
        }
        $bytes = [Text.Encoding]::UTF8.GetBytes($sb.ToString())
        $digest = ([BitConverter]::ToString($hasher.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
        return @{ digest = $digest; pathCount = @($Paths).Count }
    } finally { $hasher.Dispose() }
}

# The single rule for whether a CMake manifest can affect a compiled output.
# Shared by the derivation and the coverage verification so the two can never
# disagree about the executable-impacting set.
function Test-ExecutableManifestIsExecutable {
    param(
        [Parameter(Mandatory)] [string] $Manifest,
        [Parameter(Mandatory)] [hashtable] $OwningDirectories
    )
    if ($Manifest -match '(?i)^cmake/') { return $true }              # build system modules
    if ($Manifest -eq 'CMakeLists.txt') { return $true }              # top-level project manifest
    if ($Manifest -match '(?i)^(src/|third_party/)') { return $true } # product-tree manifest
    # A manifest owns a translation unit when its directory is a prefix of the
    # unit's directory: tests/ui/CMakeLists.txt owns tests/ui/test_*.cpp, and
    # src/protocol/CMakeLists.txt owns src/protocol/src/*.cpp.
    $dir = (Split-Path $Manifest -Parent).Replace([string][char]92, '/')
    if ([string]::IsNullOrWhiteSpace($dir)) { return $false }
    foreach ($owning in $OwningDirectories.Keys) {
        if ($owning -eq $dir -or $owning.StartsWith($dir + '/')) { return $true }
    }
    return $false
}

function Resolve-Include {
    param(
        [string] $IncludeName,
        [string[]] $IncludeDirs,
        [string[]] $Known,
        [string] $FromFile,
        [string] $RepoRootResolved,
        [bool] $IsQuoted = $true
    )
    $bs = [string][char]92
    $name = $IncludeName.Replace($bs, '/')

    # 1. A quoted include resolves relative to the including file's own
    #    directory FIRST (MSVC searches the includer's directory for any
    #    quoted name, not only for './'-prefixed ones — e.g. SnapshotFormat.h
    #    includes "WindowsPath.h" from its own ffprotocol/ directory). Angle
    #    includes skip this step exactly like the compiler.
    if ($IsQuoted -or $name -match '^\.\.?/') {
        $fromDir = (Split-Path $FromFile -Parent).Replace($bs, '/')
        $base = if ([string]::IsNullOrWhiteSpace($fromDir)) { $RepoRootResolved } else { $RepoRootResolved + '/' + $fromDir }
        try { $combined = [IO.Path]::GetFullPath((Join-Path $base $name)).Replace($bs, '/') } catch { $combined = $null }
        if ($combined -and $combined.StartsWith($RepoRootResolved + '/', [System.StringComparison]::OrdinalIgnoreCase)) {
            $rel = $combined.Substring($RepoRootResolved.Length + 1)
            if ($Known -contains $rel) { return $rel }
            if ($rel -match '(?i)^build/') {
                # Build output is outside the git-controlled claim, but only
                # when it actually EXISTS. Returning GENERATED for a name that
                # resolves to nothing lets an unresolved quoted include hide
                # behind the build-output escape and never fail closed.
                if (Test-Path -LiteralPath $combined -PathType Leaf) { return 'GENERATED:' + $rel }
            }
        }
    }

    # 2. Against every recorded include root. The caller normalises roots to
    #    repository-relative form so they compare equal to the tracked paths.
    foreach ($root in $IncludeDirs) {
        if ([string]::IsNullOrWhiteSpace($root)) { continue }
        $candidate = $root.TrimEnd('/') + '/' + $name.TrimStart('/')
        if ($Known -contains $candidate) { return $candidate }
        if ($candidate -match '(?i)^build/') {
            $candidateFull = if ([IO.Path]::IsPathRooted($candidate)) { $candidate } else { $RepoRootResolved + '/' + $candidate }
            try { $candidateFull = [IO.Path]::GetFullPath($candidateFull) } catch { $candidateFull = $null }
            if ($candidateFull -and (Test-Path -LiteralPath $candidateFull -PathType Leaf)) { return 'GENERATED:' + $candidate }
        }
    }

    # 3. Bare repository-relative fallback.
    if ($Known -contains $name) { return $name }
    return $null
}

$steps = [System.Collections.Generic.List[object]]::new()
function Add-Step {
    param([string] $Id, [string] $Status, [string] $Reason, [string] $Detail)
    $script:steps.Add([pscustomobject]@{ capabilityId = $Id; status = $Status; reason = $Reason; detail = $Detail })
}

try {
    foreach ($sha in @($ImplementationSha, $EvidenceSha)) {
        if ($sha -notmatch '^[0-9a-f]{40}$') { throw "Not a full SHA: $sha" }
    }

    $implementationPaths = Get-TrackedPaths -Sha $ImplementationSha
    $evidencePaths = Get-TrackedPaths -Sha $EvidenceSha

    # --- Derivation step 1: translation units from the compile database ---
    $compileDb = if ($CompileCommandsPath) {
        [IO.Path]::GetFullPath($CompileCommandsPath)
    } else {
        Join-Path $RepoRoot 'build\release-test\compile_commands.json'
    }
    if (-not (Test-Path -LiteralPath $compileDb -PathType Leaf)) {
        throw "compile_commands.json not found at $compileDb; build with CMAKE_EXPORT_COMPILE_COMMANDS so the executable-impacting set can be derived."
    }
    $commands = Get-Content -LiteralPath $compileDb -Raw | ConvertFrom-Json

    $translationUnits = [System.Collections.Generic.List[string]]::new()
    $includeDirs = [System.Collections.Generic.List[string]]::new()
    # Normalise both sides to a forward-slash, fully-resolved form so an
    # absolute compile-database path and the repo root compare equal.
    $repoRootResolved = [IO.Path]::GetFullPath($RepoRoot).Replace([string][char]92, '/').TrimEnd('/')
    foreach ($entry in $commands) {
        $raw = ([string]$entry.file).Replace([string][char]92, '/')
        $full = if ([IO.Path]::IsPathRooted($raw)) { [IO.Path]::GetFullPath($raw).Replace([string][char]92, '/').TrimEnd('/') } else { $raw.TrimStart('/') }
        # Make the path repository-relative first, then drop build-tree
        # artifacts (generated version resources, build-tree sources), which
        # the compile database records but which are not repository inputs.
        $repoRel = $full
        if ($repoRel.StartsWith($repoRootResolved + '/')) {
            $repoRel = $repoRel.Substring($repoRootResolved.Length + 1)
        }
        if ($repoRel -match '(?i)^build/') { continue }
        [void]$translationUnits.Add($repoRel)
        # The -I flags are the authoritative include roots for this TU.
        $command = [string]$entry.command
        # MSVC/CMake emits the include flag GLUED to the path (-ID:\...), and
        # may use /I as well as -I. Matching only the spaced form silently
        # collects zero roots, which empties the header closure entirely.
        foreach ($m in [regex]::Matches($command, '(?i)(?:^|\s)[/-]I\s*"?([^"\s]+)"?')) {
            $root = $m.Groups[1].Value.Replace('\', '/')
            # Canonicalise before comparison: MSVC emits roots like
            # 'D:\...\tests\ui\..', and a literal 'tests/ui/..' never matches
            # the normalised tracked path 'tests/TestSupport.h'.
            try { $root = [IO.Path]::GetFullPath($root).Replace('\', '/') } catch { }
            if ($root.StartsWith($repoRootResolved + '/', [System.StringComparison]::OrdinalIgnoreCase)) {
                $root = $root.Substring($repoRootResolved.Length + 1)
            }
            if ($root -and -not $root.StartsWith('/')) { [void]$includeDirs.Add($root) }
        }
    }

    $executableImpacting = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $missingFromBoth = [System.Collections.Generic.List[string]]::new()

    foreach ($tu in $translationUnits) {
        [void]$executableImpacting.Add($tu)
        if (($implementationPaths -notcontains $tu) -or ($evidencePaths -notcontains $tu)) {
            [void]$missingFromBoth.Add($tu)
        }
    }
    if ($missingFromBoth.Count -gt 0) {
        throw "Derived translation units are absent from one of the commits: $($missingFromBoth -join ', ')"
    }

    # Declared before the closure loop so an unresolved project include is a
    # coverage failure the moment it is seen, not something the verification
    # step later discovers is missing.
    $coverageFailures = [System.Collections.Generic.List[string]]::new()
    $coverageNotes = [System.Collections.Generic.List[string]]::new()
    # --- Derivation step 2: recursive header closure over the include roots ---
    $pending = [System.Collections.Generic.Queue[string]]::new()
    foreach ($tu in $translationUnits) { $pending.Enqueue($tu) }
    $visited = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $includeMatcher = [regex]::new('(?m)^\s*#\s*include\s*"([^"]+)"|^\s*#\s*include\s*<([^>]+)>', 'Compiled')
    while ($pending.Count -gt 0) {
        $current = $pending.Dequeue()
        if (-not $visited.Add($current)) { continue }
        $text = Get-BlobText -Revision $ImplementationSha -Path $current
        if ($null -eq $text) {
            # A header reachable from the derivation but absent from the
            # implementation SHA is an executable-impacting difference.
            Add-Step -Id 'header-resolution' -Status 'FAIL' -Reason 'derived-input-missing-in-implementation-sha' -Detail $current
            continue
        }
        foreach ($m in $includeMatcher.Matches($text)) {
            $isQuoted = $m.Groups[1].Success
            $name = if ($isQuoted) { $m.Groups[1].Value } else { $m.Groups[2].Value }
            if ([string]::IsNullOrWhiteSpace($name)) { continue }
            $resolved = Resolve-Include -IncludeName $name -IncludeDirs @($includeDirs) `
                -Known $implementationPaths -FromFile $current -RepoRootResolved $repoRootResolved -IsQuoted $isQuoted
            if ($resolved -like 'GENERATED:*') { continue }   # build output, covered by its generator module
            if ($null -eq $resolved) {
                # Angle includes may legitimately name SDK/STL headers outside
                # the repository. A QUOTED include is a project header and must
                # resolve: silently skipping it is exactly how the closure came
                # to cover zero headers while reporting full coverage. Vendored
                # amalgamations are the one recorded exception: third_party
                # amalgamation sources carry platform-conditional quoted
                # includes (mingw.h, tclsqlite.h, ...) that name toolchain
                # files outside this repository and are not compiled here.
                if ($isQuoted) {
                    if ($current -match '(?i)^third_party/') {
                        [void]$coverageNotes.Add("unresolved-vendored-quoted-include: " + $current + " -> " + $name)
                    } else {
                        [void]$coverageFailures.Add("unresolved-quoted-include: " + $current + " -> " + $name)
                    }
                }
                continue
            }
            if ($executableImpacting.Add($resolved)) { $pending.Enqueue($resolved) }
        }
    }

    # --- Derivation step 3: build and version-identity inputs ---
    # A CMakeLists.txt is executable-impacting when it can affect a compiled
    # output: the build system's own files, a manifest in a product tree
    # (src/, third_party/), or a manifest whose directory is a prefix of a
    # derived translation unit's directory (tests/ manifests own sources in
    # their own directory). Test-ExecutableManifestIsExecutable encodes this
    # single rule so the derivation and the verification below can never
    # disagree about what belongs in the set.
    $owningDirectories = @{}
    foreach ($tu in $translationUnits) {
        $dir = (Split-Path $tu -Parent).Replace([string][char]92, '/')
        if ($dir) { $owningDirectories[$dir] = $true }
    }
    $buildInputs = [System.Collections.Generic.List[string]]::new()
    foreach ($p in $implementationPaths) {
        # Every tracked build-system file under cmake/ is executable-impacting
        # whatever its extension: version headers/resources, identity writers,
        # and helper modules all feed compiled outputs.
        if ($p -match '(?i)^cmake/') {
            [void]$buildInputs.Add($p); continue
        }
        if ($p -match '(?i)^(CMakeLists\.txt|CMakePresets\.json|VERSION)$') {
            [void]$buildInputs.Add($p); continue
        }
        if ($p -match '(?i)CMakeLists\.txt$') {
            if (Test-ExecutableManifestIsExecutable -Manifest $p -OwningDirectories $owningDirectories) {
                [void]$buildInputs.Add($p)
            }
        }
    }
    # review-E finding P1-a: configure_file templates are executable-impacting
    # build inputs -- the generated header/resource is compiled, yet a
    # template is not a TU, not #included, and not a CMakeLists, so without
    # this rule a change confined to a template moved no digest. Literal first
    # arguments are resolved against the implementation revision; a
    # variable-driven argument is recorded as a coverage failure rather than
    # silently skipped.
    foreach ($cmakeFile in $implementationPaths) {
        if (-not ($cmakeFile -match '(?i)(CMakeLists\.txt$|^cmake/)')) { continue }
        $cmakeText = Get-BlobText -Revision $ImplementationSha -Path $cmakeFile
        if ($null -eq $cmakeText) { continue }
        foreach ($m in [regex]::Matches([string]$cmakeText, '(?i)configure_file\s*\(\s*"?([^"\s\)]+\.in)"?')) {
            $candidate = $m.Groups[1].Value.Replace([string][char]92, '/')
            # Strip the common source-root variable prefixes: the remainder is
            # repository-relative. Any other variable-driven argument is a
            # coverage failure rather than a silent skip.
            $candidate = $candidate -replace '^\$\{(CMAKE_SOURCE_DIR|CMAKE_CURRENT_SOURCE_DIR|PROJECT_SOURCE_DIR|CMAKE_CURRENT_LIST_DIR)\}/', ''
            if ($candidate.StartsWith('$')) {
                [void]$coverageFailures.Add("configure-template-argument-not-resolved: $candidate (from $cmakeFile)")
                continue
            }
            if ($implementationPaths -contains $candidate) {
                if (-not $buildInputs.Contains($candidate)) { [void]$buildInputs.Add($candidate) }
            } else {
                [void]$coverageFailures.Add("configure-template-not-derived: $candidate (from $cmakeFile)")
            }
        }
    }
    foreach ($p in $buildInputs) { [void]$executableImpacting.Add($p) }

    # --- Verification step: prove the derivation is complete ---
    if ($translationUnits.Count -lt 1) { [void]$coverageFailures.Add('no-translation-units-derived') }
    # A CMakeLists.txt is executable-impacting only when it can affect a
    # compiled product output: the build system's own files, the product trees
    # under src/ and third_party/, or a manifest that owns at least one derived
    # translation unit. A manifest that owns nothing is inert and demanding its
    # presence would make every doc-only change fail coverage.
    $allCmakeLists = @($implementationPaths | Where-Object { $_ -match '(?i)CMakeLists\.txt$' })
    foreach ($c in $allCmakeLists) {
        if (-not (Test-ExecutableManifestIsExecutable -Manifest $c -OwningDirectories $owningDirectories)) { continue }
        if ($executableImpacting -notcontains $c) {
            [void]$coverageFailures.Add("cmake-input-not-in-derivation: $c")
        }
    }
    foreach ($p in $executableImpacting) {
        if (($implementationPaths -notcontains $p) -or ($evidencePaths -notcontains $p)) {
            [void]$coverageFailures.Add("derived-path-missing-in-a-commit: $p")
        }
    }

    # --- The equivalence computation itself ---
    # Both sides are digested over "<path>\0<blobId>" pairs, so the result is
    # sensitive to content and not merely to which paths are present.
    $implementationSet = @($implementationPaths | Where-Object { $executableImpacting -contains $_ })
    $evidenceSet = @($evidencePaths | Where-Object { $executableImpacting -contains $_ })
    $implementationBlobs = Get-BlobIdMap -Sha $ImplementationSha -Paths $implementationSet
    $evidenceBlobs = Get-BlobIdMap -Sha $EvidenceSha -Paths $evidenceSet
    $implementationDigests = Get-ExecutableTreeDigest -Sha $ImplementationSha -Paths $implementationSet -BlobMap $implementationBlobs
    $evidenceDigests = Get-ExecutableTreeDigest -Sha $EvidenceSha -Paths $evidenceSet -BlobMap $evidenceBlobs
    $implementationDigest = $implementationDigests.digest
    $evidenceDigest = $evidenceDigests.digest
    $equivalent = ($implementationDigest -eq $evidenceDigest) -and ($implementationSet.Count -eq $evidenceSet.Count)

    $onlyImplementation = @($implementationSet | Where-Object { $evidenceSet -notcontains $_ })
    $onlyEvidence = @($evidenceSet | Where-Object { $implementationSet -notcontains $_ })

    # Content-level differences: same path present in both revisions but a
    # different blob. This is the signal a path-set comparison can never see.
    $contentChanged = [System.Collections.Generic.List[string]]::new()
    foreach ($p in $implementationSet) {
        if ($evidenceBlobs.ContainsKey($p) -and $implementationBlobs[$p] -ne $evidenceBlobs[$p]) {
            [void]$contentChanged.Add($p)
        }
    }

    if ($coverageFailures.Count -gt 0) {
        foreach ($f in $coverageFailures) { Add-Step -Id 'derivation-coverage' -Status 'FAIL' -Reason 'derivation-coverage-unproven' -Detail $f }
        $equivalent = $false
    } else {
        Add-Step -Id 'derivation-coverage' -Status 'PASS' -Reason $null -Detail "derived and covered $($executableImpacting.Count) executable-impacting paths"
    }

    if ($equivalent) {
        Add-Step -Id 'executable-tree-equivalence' -Status 'PASS' -Reason $null -Detail "$($ImplementationSha.Substring(0,12)) == $($EvidenceSha.Substring(0,12))"
    } else {
        Add-Step -Id 'executable-tree-equivalence' -Status 'FAIL' -Reason 'executable-tree-differs' `
            -Detail ("implementation-only: $(@($onlyImplementation).Count); evidence-only: $(@($onlyEvidence).Count); content-changed: $(@($contentChanged).Count)")
    }

    $failed = @($steps | Where-Object { $_.status -eq 'FAIL' })
    $status = if ($failed.Count -eq 0) { 'PASS' } else { 'FAIL' }

    $result = [pscustomobject]@{
        schema = 'fastfiles-executable-tree-equivalence-v1'
        status = $status
        classification = if ($status -eq 'PASS') { 'PASS' } else { 'ASSERTION_FAILURE' }
        implementationSha = $ImplementationSha.ToLowerInvariant()
        evidenceSha = $EvidenceSha.ToLowerInvariant()
        implementationTreeSha = ((Invoke-RepositoryGit -Arguments @('rev-parse', '--verify', "$ImplementationSha^{tree}")).Trim()).ToLowerInvariant()
        evidenceTreeSha = ((Invoke-RepositoryGit -Arguments @('rev-parse', '--verify', "$EvidenceSha^{tree}")).Trim()).ToLowerInvariant()
        executableTreeEquivalent = $equivalent
        implementationExecutableTreeDigest = $implementationDigest
        evidenceExecutableTreeDigest = $evidenceDigest
        executableImpactingPathCount = @($executableImpacting).Count
        translationUnitCount = $translationUnits.Count
        buildInputCount = @($buildInputs).Count
        pathsOnlyInImplementation = @($onlyImplementation)
        pathsOnlyInEvidence = @($onlyEvidence)
        pathsWithChangedContent = @($contentChanged)
        derivation = @{
            method = 'compile_commands.json translation units + recursive #include closure over recorded -I roots + CMake/version build inputs'
            coverageVerified = $coverageFailures.Count -eq 0
            coverageFailures = @($coverageFailures)
            coverageNotes = @($coverageNotes)
        }
        capabilityVerification = @($steps | ForEach-Object { [pscustomobject]@{ capabilityId = $_.capabilityId; status = $_.status; reason = $_.reason; detail = $_.detail } })
        statement = if ($equivalent) {
            "product_tree($($ImplementationSha.Substring(0,12))) == product_tree($($EvidenceSha.Substring(0,12))): the executable-impacting source tree is unchanged between the implementation SHA and the evidence tip, so a binary certified at the implementation SHA is not invalidated by the evidence-only commit."
        } elseif ($coverageFailures.Count -gt 0) {
            "product_tree($($ImplementationSha.Substring(0,12))) and product_tree($($EvidenceSha.Substring(0,12))) digests compare as $($implementationDigest -eq $evidenceDigest), but derivation coverage is UNPROVEN ($($coverageFailures.Count) failure(s)): certification MUST NOT be carried forward."
        } else {
            "product_tree($($ImplementationSha.Substring(0,12))) != product_tree($($EvidenceSha.Substring(0,12))): an executable-impacting input differs, so certification MUST NOT be carried forward."
        }
    }

    $outputDir = Split-Path $OutputPath -Parent
    if ($outputDir) { New-Item -ItemType Directory -Path $outputDir -Force | Out-Null }
    $result | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $OutputPath -Encoding UTF8

    if ($Format -eq 'json') { $result | ConvertTo-Json -Depth 20 }
    else {
        Write-Host "Executable-tree equivalence: $status"
        Write-Host "implementationSha: $($result.implementationSha)"
        Write-Host "evidenceSha:      $($result.evidenceSha)"
        Write-Host "digest(impl):     $implementationDigest"
        Write-Host "digest(evidence): $evidenceDigest"
        Write-Host "paths covered:    $($result.executableImpactingPathCount) (TUs: $($result.translationUnitCount), build inputs: $($result.buildInputCount))"
        Write-Host $result.statement
    }
    if ($status -eq 'PASS') { exit 0 }
    exit 1
} catch {
    Write-Host "Executable-tree equivalence: FAIL ($($_.Exception.Message))"
    exit 1
}
