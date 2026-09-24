<#
    Agent session governance for FastFiles.

    Provides mechanical enforcement of FASTFILES_AGENT_MODE=readonly so a
    read-only audit cannot silently mutate, commit, or push repository state,
    and resolves campaign-document authority so archived prompts cannot
    redirect an explicit session mission.
#>

Import-Module (Join-Path $PSScriptRoot 'RepositoryState.psm1') -Force -DisableNameChecking

function Get-AgentMode {
    [CmdletBinding()]
    param([string] $RepoRoot = '')

    $raw = $env:FASTFILES_AGENT_MODE
    $modeRoot = if ([string]::IsNullOrWhiteSpace($RepoRoot)) { (Get-Location).Path } else { [IO.Path]::GetFullPath($RepoRoot) }
    $sentinelPath = Join-Path $modeRoot 'verify\runs\agent-mode.readonly'
    $repoSentinel = $null
    try {
        $inside = Invoke-RepositoryGit -RepoRoot $modeRoot -Arguments @('rev-parse', '--show-toplevel') -AllowFailure
        if ($inside.exitCode -eq 0 -and $inside.lines.Count -gt 0) {
            $top = ([string]$inside.lines[0]).Trim()
            if ($top) { $repoSentinel = Join-Path $top 'verify\runs\agent-mode.readonly' }
        }
    } catch { }

    $sentinelPresent = (Test-Path -LiteralPath $sentinelPath -PathType Leaf)
    if (-not $sentinelPresent -and $repoSentinel) {
        $sentinelPresent = (Test-Path -LiteralPath $repoSentinel -PathType Leaf)
    }

    if (-not [string]::IsNullOrWhiteSpace($raw)) {
        $normalized = $raw.Trim()
        if ($normalized -ieq 'readonly') { return 'readonly' }
        if ($normalized -ieq 'normal') { return $(if ($sentinelPresent) { 'readonly' } else { 'normal' }) }
        # Unknown value fails closed: treat as readonly and report unrecognized.
        return "unrecognized:$normalized"
    }
    if ($sentinelPresent) { return 'readonly' }
    return 'normal'
}

function Test-AgentModeReadOnly {
    [CmdletBinding()]
    param([string] $RepoRoot = '')
    $mode = Get-AgentMode -RepoRoot $RepoRoot
    return ($mode -eq 'readonly' -or $mode.StartsWith('unrecognized:'))
}

function Get-AgentWritableAllowlist {
    [CmdletBinding()]
    param()
    return @(
        'build/',
        'out/',
        'dist/',
        'verify/runs/',
        'verify/baselines/',
        'verify/status/generated/',
        'ci-status/',
        '.tmp-ff/'
    )
}

function Test-WritablePathAllowlist {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RepoRoot,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [string[]] $Paths
    )

    $root = [IO.Path]::GetFullPath($RepoRoot).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $allow = @(Get-AgentWritableAllowlist)
    $decisions = foreach ($path in @($Paths)) {
        if ([string]::IsNullOrWhiteSpace($path)) { continue }
        $full = if ([IO.Path]::IsPathRooted($path)) { [IO.Path]::GetFullPath($path) } else { [IO.Path]::GetFullPath((Join-Path $RepoRoot $path)) }
        $relative = $full
        if ($full.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
            $relative = $full.Substring($root.Length).Replace('\', '/')
        } else {
            # Outside the repository (temp evidence) is not a tracked mutation.
            [pscustomobject]@{ path = $full; relative = '<outside-repository>'; writable = $true; reason = 'outside-repository' }
            continue
        }
        $writable = $false
        $reason = 'tracked-path-not-allowlisted'
        foreach ($prefix in $allow) {
            if ($relative.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -or
                ($relative + '/').StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
                $writable = $true
                $reason = "allowlisted:$prefix"
                break
            }
        }
        [pscustomobject]@{ path = $full; relative = $relative; writable = $writable; reason = $reason }
    }
    return @($decisions)
}

function Assert-AgentGovernance {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [ValidateSet('git-add', 'git-commit', 'git-push', 'git-ref-write', 'git-unknown', 'tracked-write', 'implement-script', 'autonomous-run')]
        [string] $Operation,
        [string] $RepoRoot = '',
        [AllowEmptyCollection()] [string[]] $Paths = @(),
        [switch] $PassThru
    )

    if (-not (Test-AgentModeReadOnly -RepoRoot $RepoRoot)) {
        if ($PassThru) { return [pscustomobject]@{ ok = $true; mode = (Get-AgentMode -RepoRoot $RepoRoot); operation = $Operation; message = $null } }
        return
    }

    $mode = Get-AgentMode -RepoRoot $RepoRoot
    $message = "readonly-mode-violation:$Operation"
    if ($mode.StartsWith('unrecognized:')) {
        $message = "agent-mode-unrecognized:$($mode.Substring('unrecognized:'.Length))"
        if ($PassThru) { return [pscustomobject]@{ ok = $false; mode = $mode; operation = $Operation; message = $message; exitCode = 3 } }
        throw $message
    }

    $pathDetail = ''
    if ($Paths.Count -gt 0 -and $RepoRoot) {
        $decisions = @(Test-WritablePathAllowlist -RepoRoot $RepoRoot -Paths $Paths)
        $blocked = @($decisions | Where-Object { -not $_.writable })
        if ($blocked.Count -eq 0) {
            # Allowlisted writable destinations are still not git mutations in
            # readonly mode when the operation itself is git-commit/push/add.
            if ($Operation -notin @('git-add', 'git-commit', 'git-push', 'git-ref-write')) {
                if ($PassThru) { return [pscustomobject]@{ ok = $true; mode = $mode; operation = $Operation; message = $null } }
                return
            }
        } else {
            $pathDetail = '; paths=' + (($blocked | ForEach-Object { $_.relative }) -join ',')
        }
    }

    $detail = "$message$pathDetail"
    if ($PassThru) { return [pscustomobject]@{ ok = $false; mode = $mode; operation = $Operation; message = $detail; exitCode = 1 } }
    throw $detail
}

function Invoke-GuardedGit {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RepoRoot,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [string[]] $Arguments,
        [switch] $PassThru
    )

    $verb = ''
    if ($Arguments.Count -gt 0) { $verb = $Arguments[0] }
    $operation = switch -Regex ($verb) {
        '^add$' { 'git-add'; break }
        '^commit$' { 'git-commit'; break }
        '^push$' { 'git-push'; break }
        '^(tag|branch|update-ref|rebase|reset|merge|checkout|switch|restore|clean|apply|stash|cherry-pick|revert|worktree|config|gc|filter-branch)$' { 'git-ref-write'; break }
        '^(status|diff|log|show|rev-parse|ls-files|ls-tree|cat-file|remote|describe|blame|shortlog)$' { 'git-read'; break }
        default { 'git-unknown' }
    }
    if ($operation -and $operation -ne 'git-read') {
        $guard = Assert-AgentGovernance -Operation $operation -RepoRoot $RepoRoot -PassThru
        if (-not $guard.ok) {
            if ($PassThru) { return [pscustomobject]@{ ok = $false; exitCode = $guard.exitCode; message = $guard.message; output = @() } }
            throw $guard.message
        }
    }
    $output = @(& git -C $RepoRoot @Arguments 2>&1 | ForEach-Object { $_.ToString() })
    $exitCode = $LASTEXITCODE
    $result = [pscustomobject]@{ ok = ($exitCode -eq 0); exitCode = $exitCode; message = $(if ($exitCode -eq 0) { $null } else { ($output -join ' ') }); output = $output }
    if ($PassThru) { return $result }
    if (-not $result.ok) { throw $result.message }
    return $output
}

function Write-GovernedFile {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RepoRoot,
        [Parameter(Mandatory)] [string] $Path,
        [Parameter(Mandatory)] [AllowEmptyString()] [string] $Content,
        [switch] $PassThru
    )

    $guard = Assert-AgentGovernance -Operation 'tracked-write' -RepoRoot $RepoRoot -Paths @($Path) -PassThru
    if (-not $guard.ok) {
        if ($PassThru) { return $guard }
        throw $guard.message
    }
    $full = if ([IO.Path]::IsPathRooted($Path)) { [IO.Path]::GetFullPath($Path) } else { [IO.Path]::GetFullPath((Join-Path $RepoRoot $Path)) }
    $parent = Split-Path -Parent $full
    if ($parent -and -not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    [IO.File]::WriteAllText($full, $Content)
    if ($PassThru) { return [pscustomobject]@{ ok = $true; path = $full; message = $null } }
}

function Resolve-CampaignDocAuthority {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $Path,
        [switch] $ReadOnlyContext
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return [pscustomobject]@{
            path           = $Path
            exists         = $false
            statusHeader   = $null
            active         = $false
            hijackAttempt  = $false
            imperatives    = @()
            classification = 'absent'
        }
    }

    $text = [IO.File]::ReadAllText($Path)
    $statusHeader = $null
    if ($text -match '(?m)^Status:\s*\*{0,2}\s*([A-Za-z0-9_-]+)') {
        $statusHeader = $Matches[1]
    }
    $active = ($statusHeader -ieq 'ACTIVE')
    $inertStatuses = @('ARCHIVED', 'HISTORICAL', 'DEFERRED', 'COMPLETE', 'FROZEN')
    $imperatives = @()
    if ($text -match '(?m)NEXT EXACT ACTION:\s*(.+)') { $imperatives += "NEXT EXACT ACTION: $($Matches[1].Trim())" }
    if ($text -match '(?im)push\s+(?:`main`|main\b)') { $imperatives += 'push-main-directive' }
    if ($text -match '(?im)commit\s+this\s+terminal\s+checkpoint') { $imperatives += 'commit-checkpoint-directive' }

    $classification = if ($active) { 'active' } elseif ($statusHeader -and ($inertStatuses -contains $statusHeader.ToUpperInvariant())) { 'inert' } elseif ($statusHeader) { 'inert' } else { 'unmarked' }
    $hijack = $false
    if ($ReadOnlyContext -and (Test-AgentModeReadOnly)) {
        # Under readonly mode, any non-active document that still carries
        # imperative commit/push language is flagged as an attempted redirect.
        $hijack = ($classification -ne 'active' -and $imperatives.Count -gt 0)
        if ($classification -eq 'active') {
            # Even ACTIVE documents cannot authorize mutation under readonly.
            $hijack = ($imperatives.Count -gt 0)
        }
    }

    return [pscustomobject]@{
        path           = $Path
        exists         = $true
        statusHeader   = $statusHeader
        active         = $active
        hijackAttempt  = $hijack
        imperatives    = @($imperatives)
        classification = $classification
    }
}

function New-ReadOnlyBaseline {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RepoRoot,
        [Parameter(Mandatory)] [string] $OutputPath
    )

    $state = Get-RepositorySourceState -RepoRoot $RepoRoot -QualificationMode development
    $origin = $null
    try {
        $remote = Invoke-RepositoryGit -RepoRoot $RepoRoot -Arguments @('rev-parse', 'origin/main') -AllowFailure
        if ($remote.exitCode -eq 0 -and $remote.lines.Count -gt 0) {
            $origin = ([string]$remote.lines[0]).Trim().ToLowerInvariant()
        }
    } catch { }

    $baseline = [pscustomobject]@{
        schemaVersion = 1
        kind          = 'fastfiles-readonly-baseline'
        capturedAtUtc = [DateTime]::UtcNow.ToString('o')
        repoRoot      = [IO.Path]::GetFullPath($RepoRoot)
        headSha       = $state.headSha
        treeSha       = $state.treeSha
        stateDigest   = $state.stateDigest
        sourceClean   = $state.sourceClean
        stagedPaths   = $state.stagedPaths
        unstagedPaths = $state.unstagedPaths
        untrackedPaths = $state.untrackedPaths
        originMain    = $origin
    }
    $parent = Split-Path -Parent $OutputPath
    if ($parent -and -not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    $baseline | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $OutputPath -Encoding utf8NoBOM
    return $baseline
}

function Compare-ReadOnlyBaseline {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RepoRoot,
        [Parameter(Mandatory)] [string] $BaselinePath,
        [switch] $CheckRemote
    )

    if (-not (Test-Path -LiteralPath $BaselinePath -PathType Leaf)) {
        return [pscustomobject]@{ ok = $false; exitCode = 3; reasonCodes = @('baseline-missing'); message = "readonly baseline missing: $BaselinePath" }
    }
    $baseline = Get-Content -LiteralPath $BaselinePath -Raw | ConvertFrom-Json
    try {
        $after = Get-RepositorySourceState -RepoRoot $RepoRoot -QualificationMode development
    } catch {
        return [pscustomobject]@{ ok = $false; exitCode = 3; reasonCodes = @('source-state-unavailable'); message = $_.Exception.Message }
    }

    $reasons = [System.Collections.Generic.List[string]]::new()
    if ($after.headSha -ne $baseline.headSha) { [void]$reasons.Add('head-changed-during-validation') }
    if ($after.treeSha -ne $baseline.treeSha) { [void]$reasons.Add('commit-tree-changed-during-validation') }
    if ($after.stateDigest -ne $baseline.stateDigest) { [void]$reasons.Add('source-state-changed-during-validation') }

    if ($CheckRemote) {
        $originNow = $null
        try {
            $remote = Invoke-RepositoryGit -RepoRoot $RepoRoot -Arguments @('rev-parse', 'origin/main') -AllowFailure
            if ($remote.exitCode -eq 0 -and $remote.lines.Count -gt 0) {
                $originNow = ([string]$remote.lines[0]).Trim().ToLowerInvariant()
            }
        } catch { }
        if ($originNow -ne $baseline.originMain) { [void]$reasons.Add('remote-ref-changed') }
    }

    $unique = @($reasons | Sort-Object -Unique)
    $ok = ($unique.Count -eq 0)
    $message = if ($ok) { $null } else { "readonly-mode-violation:session-delta; reasons=$($unique -join ',')" }
    return [pscustomobject]@{
        ok          = $ok
        exitCode    = if ($ok) { 0 } else { 1 }
        reasonCodes = $unique
        message     = $message
        before      = $baseline
        after       = $after
    }
}

Export-ModuleMember -Function @(
    'Get-AgentMode',
    'Test-AgentModeReadOnly',
    'Get-AgentWritableAllowlist',
    'Test-WritablePathAllowlist',
    'Assert-AgentGovernance',
    'Invoke-GuardedGit',
    'Write-GovernedFile',
    'Resolve-CampaignDocAuthority',
    'New-ReadOnlyBaseline',
    'Compare-ReadOnlyBaseline'
)
