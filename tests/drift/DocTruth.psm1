# tests/drift/DocTruth.psm1 -- semantic truth contracts for current-facing guidance.
#
# Derives expected facts from authoritative executable seams (verify.ps1 verb /
# exit-code definitions, CMakePresets.json, first-party CMake target
# declarations, openspec/lifecycle.json, snapshot publisher source seam) and
# validates marker-delimited fenced claims plus whole-file anti-staleness rules
# in declared current-facing guidance surfaces.
#
# Contract: deterministic, offline, repository-relative diagnostics only.
# Every failure carries a stable reason code. Unreadable authority input fails
# closed (AUTHORITY_UNREADABLE) instead of passing vacuously.
#
# Design record: openspec/changes/repository-truth-surface-and-doc-drift-convergence/design.md

Set-StrictMode -Version Latest

$script:TruthSurfaces = @(
    'README.md',
    'AGENTS.md',
    'CLAUDE.md',
    'CODE_INDEX.md',
    'AUTONOMOUS.md',
    'verify/README.md',
    'progress.md'
)

# Surfaces that must reference the lifecycle manifest as the current-work
# authority (stable-pointer contract).
$script:LifecyclePointerSurfaces = @(
    'README.md',
    'AGENTS.md',
    'CLAUDE.md',
    'CODE_INDEX.md',
    'AUTONOMOUS.md',
    'verify/README.md',
    'progress.md'
)

# Removed snapshot mechanics banned from current-facing surfaces. Tied to the
# source-derived fact that SnapshotPublisher has no slot mechanic; if the
# publisher shape ever changes again this list and the source seam contract
# change together in one review.
$script:RemovedMechanics = @(
    'double-buffered',
    'double-buffering',
    'activeSlot',
    'inactive slot'
)

function New-TruthFinding {
    param(
        [string] $Code,
        [string] $Surface,
        [string] $Detail
    )
    [pscustomobject]@{
        Code    = $Code
        Surface = $Surface.Replace('\', '/')
        Detail  = $Detail
    }
}

function Resolve-TruthRepoPath {
    param(
        [string] $RepoRoot,
        [string] $Relative
    )
    return Join-Path $RepoRoot ($Relative -replace '/', [System.IO.Path]::DirectorySeparatorChar)
}

function Read-TruthText {
    param(
        [string] $RepoRoot,
        [string] $Relative,
        [System.Collections.Generic.List[object]] $Findings
    )
    $path = Resolve-TruthRepoPath -RepoRoot $RepoRoot -Relative $Relative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        $Findings.Add((New-TruthFinding -Code 'SURFACE_MISSING' -Surface $Relative -Detail 'declared current-facing surface is missing')) | Out-Null
        return $null
    }
    try {
        return [System.IO.File]::ReadAllText($path)
    } catch {
        $Findings.Add((New-TruthFinding -Code 'SURFACE_UNREADABLE' -Surface $Relative -Detail $_.Exception.Message)) | Out-Null
        return $null
    }
}

# ---------------------------------------------------------------------------
# Authority seams. Each extractor throws Write-AuthorityError on unreadable or
# structurally unexpected input; callers convert that into AUTHORITY_UNREADABLE.
# ---------------------------------------------------------------------------

function Write-AuthorityError {
    param([string] $Seam, [string] $Why)
    throw [InvalidOperationException]::new("authority:$Seam|$Why")
}

function Get-TruthVerifierContract {
    param([string] $RepoRoot)
    $path = Resolve-TruthRepoPath -RepoRoot $RepoRoot -Relative 'verify/verify.ps1'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        Write-AuthorityError -Seam 'verify.ps1' -Why 'missing'
    }
    $text = [System.IO.File]::ReadAllText($path)

    $verbMatch = [regex]::Match($text, '\[ValidateSet\((?<body>[^\]]*?)\)\]\s*\r?\n\s*\[string\]\s*\$Verb\b')
    if (-not $verbMatch.Success) {
        Write-AuthorityError -Seam 'verify.ps1' -Why 'verb ValidateSet not found'
    }
    $verbs = @([regex]::Matches($verbMatch.Groups['body'].Value, "'(?<v>[A-Za-z][A-Za-z0-9-]*)'") |
        ForEach-Object { $_.Groups['v'].Value })
    if ($verbs.Count -eq 0) {
        Write-AuthorityError -Seam 'verify.ps1' -Why 'verb ValidateSet empty'
    }

    $labelMap = @{
        'ExitPass'           = 'PASS'
        'ExitFail'           = 'FAIL'
        'ExitSkipped'        = 'SKIPPED'
        'ExitError'          = 'HARNESS ERROR'
        'ExitNotImplemented' = 'NOT-YET-IMPLEMENTED'
    }
    $exitCodes = @{}
    foreach ($name in $labelMap.Keys) {
        $m = [regex]::Match($text, ('\$\(' + $name + '\)\s*=\s*(?<n>\d+)'))
        if (-not $m.Success) {
            $m = [regex]::Match($text, ('\$' + $name + '\s*=\s*(?<n>\d+)'))
        }
        if (-not $m.Success) {
            Write-AuthorityError -Seam 'verify.ps1' -Why ("exit constant missing: " + $name)
        }
        $exitCodes[[int]$m.Groups['n'].Value] = $labelMap[$name]
    }
    if ($exitCodes.Count -ne 5) {
        Write-AuthorityError -Seam 'verify.ps1' -Why 'duplicate exit-code values'
    }

    [pscustomobject]@{ Verbs = $verbs; ExitCodes = $exitCodes }
}

function Get-TruthCmakePresets {
    param([string] $RepoRoot)
    $path = Resolve-TruthRepoPath -RepoRoot $RepoRoot -Relative 'CMakePresets.json'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        Write-AuthorityError -Seam 'CMakePresets.json' -Why 'missing'
    }
    try {
        $json = ConvertFrom-Json ([System.IO.File]::ReadAllText($path)) -ErrorAction Stop
    } catch {
        Write-AuthorityError -Seam 'CMakePresets.json' -Why ('unparseable: ' + $_.Exception.Message)
    }
    $configure = @()
    foreach ($p in @($json.configurePresets)) {
        $hiddenProp = $p.PSObject.Properties['hidden']
        if ($null -eq $hiddenProp -or -not $hiddenProp.Value) { $configure += [string]$p.name }
    }
    $test = @()
    foreach ($p in @($json.testPresets)) { $test += [string]$p.name }
    if ($configure.Count -eq 0) {
        Write-AuthorityError -Seam 'CMakePresets.json' -Why 'no visible configure presets'
    }
    [pscustomobject]@{ ConfigurePresets = $configure; TestPresets = $test }
}

function Get-TruthCmakeTargets {
    param([string] $RepoRoot)
    $srcDir = Resolve-TruthRepoPath -RepoRoot $RepoRoot -Relative 'src'
    if (-not (Test-Path -LiteralPath $srcDir -PathType Container)) {
        Write-AuthorityError -Seam 'src/**/CMakeLists.txt' -Why 'src directory missing'
    }
    $executables = New-Object 'System.Collections.Generic.SortedSet[string]'
    $staticLibs = New-Object 'System.Collections.Generic.SortedSet[string]'
    foreach ($file in (Get-ChildItem -LiteralPath $srcDir -Recurse -Filter 'CMakeLists.txt' -File)) {
        $text = [System.IO.File]::ReadAllText($file.FullName)
        foreach ($m in [regex]::Matches($text, 'add_executable\(\s*(?<name>[A-Za-z0-9_-]+)')) {
            if ($m.Groups['name'].Value.StartsWith('FastFiles')) {
                $executables.Add($m.Groups['name'].Value) | Out-Null
            }
        }
        foreach ($m in [regex]::Matches($text, 'add_library\(\s*(?<name>[A-Za-z0-9_-]+)(?<rest>[^\r\n]*)')) {
            if ($m.Groups['name'].Value.StartsWith('ff') -and $m.Groups['rest'].Value -match '\bSTATIC\b') {
                $staticLibs.Add($m.Groups['name'].Value) | Out-Null
            }
        }
    }
    if ($executables.Count -eq 0 -or $staticLibs.Count -eq 0) {
        Write-AuthorityError -Seam 'src/**/CMakeLists.txt' -Why 'no first-party targets derived'
    }
    [pscustomobject]@{ Executables = @($executables); StaticLibs = @($staticLibs) }
}

function Get-TruthLifecycleStates {
    param([string] $RepoRoot)
    $path = Resolve-TruthRepoPath -RepoRoot $RepoRoot -Relative 'openspec/lifecycle.json'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        Write-AuthorityError -Seam 'openspec/lifecycle.json' -Why 'missing'
    }
    try {
        $json = ConvertFrom-Json ([System.IO.File]::ReadAllText($path)) -ErrorAction Stop
    } catch {
        Write-AuthorityError -Seam 'openspec/lifecycle.json' -Why ('unparseable: ' + $_.Exception.Message)
    }
    if ([int]$json.schemaVersion -ne 2) {
        Write-AuthorityError -Seam 'openspec/lifecycle.json' -Why 'unsupported schemaVersion'
    }
    $states = @{}
    foreach ($c in @($json.changes)) { $states[[string]$c.name] = [string]$c.state }
    [pscustomobject]@{ States = $states }
}

function Get-TruthSnapshotSeam {
    param([string] $RepoRoot)
    $path = Resolve-TruthRepoPath -RepoRoot $RepoRoot -Relative 'src/engine/src/SnapshotPublisher.cpp'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        Write-AuthorityError -Seam 'SnapshotPublisher.cpp' -Why 'missing'
    }
    $text = [System.IO.File]::ReadAllText($path)
    $hasGenerationNaming = $text.Contains('GenerationSectionName(')
    $hasCommitBeforeHandoff = $text.Contains('CommitGeneration(&header->generation')
    $hasSlotMechanic = $text.Contains('activeSlot')
    if (-not $hasGenerationNaming -or -not $hasCommitBeforeHandoff -or $hasSlotMechanic) {
        # The publisher shape moved away from the model the doc contracts are
        # anchored to. Fail closed so the seam markers and the guidance
        # contracts are updated together.
        Write-AuthorityError -Seam 'SnapshotPublisher.cpp' -Why 'publication seam markers changed'
    }
}

# ---------------------------------------------------------------------------
# Fence parsing
# ---------------------------------------------------------------------------

function Split-TruthFences {
    param(
        [string] $Text,
        [string] $Surface,
        [System.Collections.Generic.List[object]] $Findings,
        [System.Collections.Generic.HashSet[string]] $KnownContracts
    )
    # Returns hashtable contractId -> inner text (unique per surface).
    $result = @{}
    $matchesFound = [regex]::Matches($Text, '<!--\s*(?<close>/?)ff-truth:(?<id>[A-Za-z0-9-]+)\s*-->')
    $openId = $null
    $startIdx = 0
    foreach ($m in $matchesFound) {
        $id = $m.Groups['id'].Value
        $isClose = $m.Groups['close'].Success -and $m.Groups['close'].Value -eq '/'
        if ($null -eq $openId) {
            if ($isClose) {
                $Findings.Add((New-TruthFinding -Code 'FENCE_ORPHAN_CLOSE' -Surface $Surface -Detail ("ff-truth:" + $id))) | Out-Null
                continue
            }
            if (-not $KnownContracts.Contains($id)) {
                $Findings.Add((New-TruthFinding -Code 'FENCE_UNKNOWN_CONTRACT' -Surface $Surface -Detail ("ff-truth:" + $id))) | Out-Null
                # Skip to its closing marker so structural validation continues.
                $openId = $id
                $startIdx = $m.Index + $m.Length
                $skipUnknown = $true
                continue
            }
            $openId = $id
            $startIdx = $m.Index + $m.Length
            $skipUnknown = $false
        } else {
            if (-not $isClose) {
                # A new opening marker arrived before the pending fence was
                # closed: the pending fence is unclosed (edit damage).
                if (-not $skipUnknown) {
                    $Findings.Add((New-TruthFinding -Code 'FENCE_UNCLOSED' -Surface $Surface -Detail ("ff-truth:" + $openId))) | Out-Null
                }
                $openId = $id
                $startIdx = $m.Index + $m.Length
                $skipUnknown = (-not $KnownContracts.Contains($id))
                if ($skipUnknown) {
                    $Findings.Add((New-TruthFinding -Code 'FENCE_UNKNOWN_CONTRACT' -Surface $Surface -Detail ("ff-truth:" + $id))) | Out-Null
                }
                continue
            }
            if ($isClose -and $id -ne $openId) {
                $Findings.Add((New-TruthFinding -Code 'FENCE_MISMATCHED_CLOSE' -Surface $Surface -Detail ("/ff-truth:" + $id + " closes ff-truth:" + $openId))) | Out-Null
                $openId = $null
                continue
            }
            if (-not $skipUnknown) {
                if ($result.ContainsKey($openId)) {
                    $Findings.Add((New-TruthFinding -Code 'FENCE_DUPLICATE' -Surface $Surface -Detail ("ff-truth:" + $openId))) | Out-Null
                } else {
                    $result[$openId] = $Text.Substring($startIdx, $m.Index - $startIdx)
                }
            }
            $openId = $null
        }
    }
    if ($null -ne $openId) {
        $Findings.Add((New-TruthFinding -Code 'FENCE_UNCLOSED' -Surface $Surface -Detail ("ff-truth:" + $openId))) | Out-Null
    }
    return $result
}

# ---------------------------------------------------------------------------
# Fence validators. Each receives the fence inner text plus the authorities it
# needs and appends findings. Tokens are backtick-quoted identifiers.
# ---------------------------------------------------------------------------

function Get-TruthBacktickTokens {
    param([string] $Inner)
    return @([regex]::Matches($Inner, '`(?<t>[^`\r\n]+)`') | ForEach-Object { $_.Groups['t'].Value })
}

function Test-TruthVerbSet {
    param($InnerTokens, $ExpectedVerbs, $Surface, $Findings)
    # Case-sensitive: verb identifiers are lowercase; this rejects stray
    # CamelCase identifiers such as 'ValidateSet' that merely appear nearby.
    $verbs = @($InnerTokens | Where-Object { $_ -cmatch '^[a-z][a-z0-9-]*$' } | Sort-Object -Unique)
    $expected = @($ExpectedVerbs | Sort-Object -Unique)
    foreach ($v in (Compare-Object -ReferenceObject $expected -DifferenceObject $verbs)) {
        if ($v.SideIndicator -eq '<=') {
            $Findings.Add((New-TruthFinding -Code 'VERB_MISSING' -Surface $Surface -Detail $v.InputObject)) | Out-Null
        } else {
            $Findings.Add((New-TruthFinding -Code 'VERB_UNKNOWN' -Surface $Surface -Detail $v.InputObject)) | Out-Null
        }
    }
}

function Test-TruthExitCodes {
    param([string] $Inner, $ExpectedExitCodes, $Surface, $Findings)
    # Parenthetical asides may follow the last mapping; strip them so the
    # per-part match stays anchored.
    $clean = $Inner -replace '\([^)]*\)', ' '
    $observed = @{}
    foreach ($part in ($clean -split '[,\r\n]+')) {
        # Unanchored start: the first comma-part may carry a prose label such
        # as 'Exit codes:' before the first mapping.
        $m = [regex]::Match($part.Trim(), '\b`?(?<n>\d+)`?\s*[=:]\s*`?(?<label>[A-Za-z][A-Za-z -]*?)`?\.?\s*$')
        if ($m.Success) {
            $observed[[int]$m.Groups['n'].Value] = $m.Groups['label'].Value.Trim()
        }
    }
    foreach ($code in $ExpectedExitCodes.Keys) {
        if (-not $observed.ContainsKey($code)) {
            $Findings.Add((New-TruthFinding -Code 'EXITCODE_MISSING' -Surface $Surface -Detail ([string]$code))) | Out-Null
        } elseif ($observed[$code] -ne $ExpectedExitCodes[$code]) {
            $Findings.Add((New-TruthFinding -Code 'EXITCODE_MISMATCH' -Surface $Surface -Detail ([string]$code + ':' + $observed[$code]))) | Out-Null
        }
    }
    foreach ($code in $observed.Keys) {
        if (-not $ExpectedExitCodes.ContainsKey($code)) {
            $Findings.Add((New-TruthFinding -Code 'EXITCODE_UNKNOWN' -Surface $Surface -Detail ([string]$code))) | Out-Null
        }
    }
}

function Test-TruthNameSet {
    param($InnerTokens, $Expected, $Surface, $Findings, [string] $MissingCode, [string] $UnknownCode, [scriptblock] $TokenFilter)
    $seen = New-Object 'System.Collections.Generic.List[string]'
    foreach ($t in $InnerTokens) {
        if (& $TokenFilter $t) { $seen.Add($t) }
    }
    $expectedSet = New-Object 'System.Collections.Generic.HashSet[string]'
    foreach ($e in $Expected) { $expectedSet.Add($e) | Out-Null }
    $distinct = @($seen | Sort-Object -Unique)
    foreach ($name in $distinct) {
        if (-not $expectedSet.Contains($name)) {
            $Findings.Add((New-TruthFinding -Code $UnknownCode -Surface $Surface -Detail $name)) | Out-Null
        }
    }
    if ($MissingCode -eq 'PRESET_MISSING_ALL' -or $MissingCode -eq 'TARGET_MISSING_ALL' -or $MissingCode -eq 'TESTPRESET_MISSING_ALL') {
        foreach ($e in $Expected) {
            if ($distinct -notcontains $e) {
                $Findings.Add((New-TruthFinding -Code ($MissingCode -replace '_ALL$', '') -Surface $Surface -Detail $e)) | Out-Null
            }
        }
    }
}

# ---------------------------------------------------------------------------
# Whole-file anti-staleness
# ---------------------------------------------------------------------------

function Get-TruthSegments {
    # Splits text into deterministic segments: lines merge while they do not
    # end with sentence/table/heading-terminal punctuation, so wrapped prose
    # stays one segment while table rows and headings stay isolated.
    param([string] $Text)
    $segments = New-Object 'System.Collections.Generic.List[string]'
    $sb = New-Object System.Text.StringBuilder
    foreach ($line in ($Text -split "\r?\n")) {
        $trim = $line.Trim()
        if ($trim.Length -eq 0) {
            if ($sb.Length -gt 0) { $segments.Add($sb.ToString()) | Out-Null; [void]$sb.Clear() }
            continue
        }
        [void]$sb.Append($trim).Append(' ')
        if ($trim -match '[.!?:;)*|]$') {
            $segments.Add($sb.ToString()) | Out-Null
            [void]$sb.Clear()
        }
    }
    if ($sb.Length -gt 0) { $segments.Add($sb.ToString()) | Out-Null }
    return ,@($segments)
}

function Resolve-TruthClaimSlug {
    # Normalizes a claimed reference to a campaign slug; returns $null when the
    # token cannot plausibly be a campaign name.
    param([string] $Raw)
    $slug = ($Raw -replace '^[Oo]penspec/changes/', '' -replace '/+$', '')
    if ($slug -match '/') { $slug = ($slug -split '/')[0] }
    $slug = $slug.ToLowerInvariant().Trim('`"''.,()')
    if ($slug -match '^[0-9.]+$' -or $slug.Length -lt 4) { return $null }
    if ($slug -notmatch '^[a-z0-9][a-z0-9-]*$') { return $null }
    return $slug
}

function Assert-TruthClaimAllowed {
    # Emits STALE_CURRENT_CLAIM unless the manifest state permits the claim.
    # mode 'authoritative-current': only lifecycle state 'active' may be named
    #   as the current change/campaign/work.
    # mode 'open-tasks': completed/superseded/unrecorded/unknown changes may
    #   not be presented as currently having open tasks.
    param(
        [string] $Slug,
        [string] $Mode,
        [string] $Surface,
        $LifecycleStates,
        $Findings
    )
    if ($LifecycleStates.ContainsKey($Slug)) {
        $state = $LifecycleStates[$Slug]
        $allowed = if ($Mode -eq 'authoritative-current') { $state -eq 'active' } else { $state -in @('active', 'external-evidence-only', 'ambiguous') }
        if (-not $allowed) {
            $Findings.Add((New-TruthFinding -Code 'STALE_CURRENT_CLAIM' -Surface $Surface -Detail ($Slug + ' manifest-state=' + $state))) | Out-Null
        }
        return
    }
    $dir = Resolve-TruthRepoPath -RepoRoot $script:CurrentRepoRoot -Relative ('openspec/changes/' + $Slug)
    $manifestState = if (Test-Path -LiteralPath $dir -PathType Container) { 'unrecorded' } else { 'unknown' }
    $Findings.Add((New-TruthFinding -Code 'STALE_CURRENT_CLAIM' -Surface $Surface -Detail ($Slug + ' manifest-state=' + $manifestState))) | Out-Null
}

function Test-TruthCurrentClaims {
    param([string] $Text, [string] $Surface, $LifecycleStates, $Findings)

    foreach ($segment in (Get-TruthSegments $Text)) {
        # Trigger-word guard: 'active'/'current' must be standalone words, not
        # fragments of hyphenated compounds such as 'active-local' or 'active-path'.
        $trigger = '(?<![A-Za-z-])(?i:current(?:ly)?|active)(?![A-Za-z-])'
        # "... current/active ... change/campaign/work is|are|: <ref>"
        foreach ($m in [regex]::Matches($segment, $trigger + '.{0,120}?\b(?:change|campaign|work)\b\s*(?:is|are|:)\s*[`"]?(?<ref>[A-Za-z0-9][A-Za-z0-9._/-]*)')) {
            $slug = Resolve-TruthClaimSlug -Raw $m.Groups['ref'].Value
            if ($null -ne $slug) {
                Assert-TruthClaimAllowed -Slug $slug -Mode 'authoritative-current' -Surface $Surface -LifecycleStates $LifecycleStates -Findings $Findings
            }
        }
        # "... current/active ... `<slug>` ... change/campaign" (backticks required)
        foreach ($m in [regex]::Matches($segment, $trigger + '.{0,120}?[`](?<ref>[a-z0-9][a-z0-9-]{5,})`.{0,60}?\b(?:change|campaign)\b')) {
            $slug = Resolve-TruthClaimSlug -Raw $m.Groups['ref'].Value
            if ($null -ne $slug) {
                Assert-TruthClaimAllowed -Slug $slug -Mode 'authoritative-current' -Surface $Surface -LifecycleStates $LifecycleStates -Findings $Findings
            }
        }
        # "... current(ly)/active ... `<slugs>` ... have/has open tasks"
        foreach ($m in [regex]::Matches($segment, $trigger + '.{0,260}?\b(?:have|has)\s+open\s+tasks\b')) {
            $spans = [regex]::Matches($m.Value, '`(?<t>[a-z0-9][a-z0-9-]{4,})`')
            $found = $false
            foreach ($s in $spans) {
                $slug = Resolve-TruthClaimSlug -Raw $s.Groups['t'].Value
                if ($null -ne $slug) {
                    $found = $true
                    Assert-TruthClaimAllowed -Slug $slug -Mode 'open-tasks' -Surface $Surface -LifecycleStates $LifecycleStates -Findings $Findings
                }
            }
            if (-not $found) {
                $Findings.Add((New-TruthFinding -Code 'STALE_CURRENT_CLAIM' -Surface $Surface -Detail 'unresolved-current-open-task-enumeration')) | Out-Null
            }
        }
    }
}

function Test-TruthRemovedMechanics {
    param([string] $Text, [string] $Surface, $Findings)
    foreach ($mechanic in $script:RemovedMechanics) {
        if ($Text.IndexOf($mechanic, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
            $Findings.Add((New-TruthFinding -Code 'REMOVED_MECHANIC' -Surface $Surface -Detail $mechanic)) | Out-Null
        }
    }
}

# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

$script:CurrentRepoRoot = $null

function Get-DocTruthFindings {
    <#
    .SYNOPSIS
        Runs every truth contract against the repository at $RepoRoot.
    .OUTPUTS
        Deterministic list of finding objects (Code/Surface/Detail), sorted.
    #>
    param(
        [Parameter(Mandatory)]
        [string] $RepoRoot
    )
    $RepoRoot = (Resolve-Path -LiteralPath $RepoRoot).Path
    $script:CurrentRepoRoot = $RepoRoot
    $findings = New-Object 'System.Collections.Generic.List[object]'

    # ---- authorities (fail closed) ---------------------------------------
    $authorities = @{}
    foreach ($seam in @(
        @{ Key = 'verifier';   Script = { Get-TruthVerifierContract $RepoRoot } },
        @{ Key = 'presets';    Script = { Get-TruthCmakePresets $RepoRoot } },
        @{ Key = 'targets';    Script = { Get-TruthCmakeTargets $RepoRoot } },
        @{ Key = 'lifecycle';  Script = { Get-TruthLifecycleStates $RepoRoot } },
        @{ Key = 'snapshot';   Script = { Get-TruthSnapshotSeam $RepoRoot } }
    )) {
        try {
            $authorities[$seam.Key] = & $seam.Script
        } catch [InvalidOperationException] {
            $msg = $_.Exception.Message
            if ($msg.StartsWith('authority:')) {
                $parts = $msg.Substring(10) -split '\|', 2
                $findings.Add((New-TruthFinding -Code 'AUTHORITY_UNREADABLE' -Surface $parts[0] -Detail $parts[1])) | Out-Null
            } else {
                throw
            }
        }
    }

    $surfaceText = @{}

    # ---- fenced contracts -------------------------------------------------
    $contractIds = New-Object 'System.Collections.Generic.HashSet[string]'
    foreach ($id in @('verifier-verbs', 'exit-codes', 'configure-presets-all', 'presets-subset',
                      'test-presets-all', 'product-executables', 'shared-libs-subset')) {
        $contractIds.Add($id) | Out-Null
    }

    foreach ($surface in $script:TruthSurfaces) {
        $text = Read-TruthText -RepoRoot $RepoRoot -Relative $surface -Findings $findings
        if ($null -eq $text) { continue }
        $surfaceText[$surface] = $text

        $fences = Split-TruthFences -Text $text -Surface $surface -Findings $findings -KnownContracts $contractIds

        if ($fences.ContainsKey('verifier-verbs') -and $authorities.ContainsKey('verifier')) {
            Test-TruthVerbSet -InnerTokens (Get-TruthBacktickTokens $fences['verifier-verbs']) `
                -ExpectedVerbs $authorities['verifier'].Verbs -Surface $surface -Findings $findings
        }
        if ($fences.ContainsKey('exit-codes') -and $authorities.ContainsKey('verifier')) {
            Test-TruthExitCodes -Inner $fences['exit-codes'] -ExpectedExitCodes $authorities['verifier'].ExitCodes `
                -Surface $surface -Findings $findings
        }
        if ($authorities.ContainsKey('presets')) {
            $presetFilter = { param($t) $t -cmatch '^[a-z][a-z0-9-]*$' }
            if ($fences.ContainsKey('configure-presets-all')) {
                Test-TruthNameSet -InnerTokens (Get-TruthBacktickTokens $fences['configure-presets-all']) `
                    -Expected $authorities['presets'].ConfigurePresets -Surface $surface -Findings $findings `
                    -MissingCode 'PRESET_MISSING_ALL' -UnknownCode 'PRESET_UNKNOWN' -TokenFilter $presetFilter
            }
            if ($fences.ContainsKey('presets-subset')) {
                Test-TruthNameSet -InnerTokens (Get-TruthBacktickTokens $fences['presets-subset']) `
                    -Expected $authorities['presets'].ConfigurePresets -Surface $surface -Findings $findings `
                    -MissingCode 'PRESET_MISSING_NONE' -UnknownCode 'PRESET_UNKNOWN' -TokenFilter $presetFilter
            }
            if ($fences.ContainsKey('test-presets-all')) {
                Test-TruthNameSet -InnerTokens (Get-TruthBacktickTokens $fences['test-presets-all']) `
                    -Expected $authorities['presets'].TestPresets -Surface $surface -Findings $findings `
                    -MissingCode 'TESTPRESET_MISSING_ALL' -UnknownCode 'TESTPRESET_UNKNOWN' -TokenFilter $presetFilter
            }
        }
        if ($authorities.ContainsKey('targets')) {
            $exeFilter = { param($t) $t -cmatch '^FastFiles[A-Za-z0-9]*$' }
            $libFilter = { param($t) $t -cmatch '^ff[a-z0-9]*$' }
            # Product-executables fences may wrap Markdown tables where targets
            # are bolded rather than backticked; accept both spellings.
            $exeTokens = {
                param($inner)
                $tokens = New-Object 'System.Collections.Generic.List[string]'
                foreach ($t in (Get-TruthBacktickTokens $inner)) { $tokens.Add($t) }
                foreach ($m in [regex]::Matches($inner, '\*\*?(?<n>FastFiles[A-Za-z0-9]*)\*\*?')) { $tokens.Add($m.Groups['n'].Value) }
                return $tokens
            }
            if ($fences.ContainsKey('product-executables')) {
                Test-TruthNameSet -InnerTokens (& $exeTokens $fences['product-executables']) `
                    -Expected $authorities['targets'].Executables -Surface $surface -Findings $findings `
                    -MissingCode 'TARGET_MISSING_ALL' -UnknownCode 'TARGET_UNKNOWN' -TokenFilter $exeFilter
            }
            if ($fences.ContainsKey('shared-libs-subset')) {
                Test-TruthNameSet -InnerTokens (Get-TruthBacktickTokens $fences['shared-libs-subset']) `
                    -Expected $authorities['targets'].StaticLibs -Surface $surface -Findings $findings `
                    -MissingCode 'TARGET_MISSING_NONE' -UnknownCode 'TARGET_UNKNOWN' -TokenFilter $libFilter
            }
        }
    }

    # ---- whole-file anti-staleness ---------------------------------------
    if ($authorities.ContainsKey('lifecycle')) {
        foreach ($surface in $script:TruthSurfaces) {
            if (-not $surfaceText.ContainsKey($surface)) { continue }
            Test-TruthCurrentClaims -Text $surfaceText[$surface] -Surface $surface `
                -LifecycleStates $authorities['lifecycle'].States -Findings $findings
            Test-TruthRemovedMechanics -Text $surfaceText[$surface] -Surface $surface -Findings $findings
        }
    }

    # ---- stable lifecycle pointer ----------------------------------------
    foreach ($surface in $script:LifecyclePointerSurfaces) {
        if (-not $surfaceText.ContainsKey($surface)) { continue }
        if ($surfaceText[$surface] -notmatch 'openspec/lifecycle\.json') {
            $findings.Add((New-TruthFinding -Code 'LIFECYCLE_POINTER_MISSING' -Surface $surface -Detail 'surface must reference openspec/lifecycle.json as the current-work authority')) | Out-Null
        }
    }

    return @($findings | Sort-Object -Property Code, Surface, Detail)
}

Export-ModuleMember -Function Get-DocTruthFindings
Export-ModuleMember -Variable TruthSurfaces
