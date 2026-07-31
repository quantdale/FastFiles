<#
    Capability registry (tasks 1.2/1.3/1.9/1.10/1.11): scans verify/capabilities/*/capability.json,
    validates each manifest against the capability-interface schema, loads only
    manifests whose interfaceVersion major is within the core's supported range, and
    records an incompatible/invalid manifest as a load diagnostic rather than a
    silent drop (D11). Adding a capability is dropping in a conforming module — the
    core never special-cases a capability id.

    Registry hardening (D17): before any capability module is imported or executed,
    the full set of discovered manifests is validated as a set — duplicate ids,
    duplicate id+version pairs, unresolved dependsOn entries, and dependency cycles
    are all rejected as load diagnostics (never a silent drop, never a partial
    execution of a broken graph).
#>

Import-Module (Join-Path $PSScriptRoot 'Schema.psm1') -Force -Global

# Supported capability interfaceVersion major versions. Bump when the core makes a
# breaking change to the capability contract; capabilities declaring a major outside
# this set are refused (see design.md D11, open question "interface version policy").
$script:SupportedInterfaceMajors = @(1)

# Repair postures a capability manifest may declare (D7/D11/D17, task 1.11).
$script:ValidRepairPostures = @('repair-supported', 'repair-unsupported', 'repair-unavailable')

function Test-InterfaceVersionSupported {
    param([string] $InterfaceVersion)
    if ($InterfaceVersion -notmatch '^(\d+)\.\d+\.\d+$') { return $false }
    $major = [int]$Matches[1]
    return $script:SupportedInterfaceMajors -contains $major
}

function Resolve-CandidateManifests {
    <#
        Phase A: parse + per-manifest-validate every capability.json in isolation
        (JSON well-formedness, schema, interfaceVersion support, entryModule
        existence, repair-posture/entry-point consistency). Returns candidates that
        passed *individually* — duplicate/dependency/cycle checks happen next, across
        the whole candidate set, before anything is imported.
    #>
    param(
        [Parameter(Mandatory)] [string] $CapabilitiesRoot,
        [pscustomobject] $Schema
    )

    $candidates = @()
    $diagnostics = @()

    $manifestFiles = Get-ChildItem -Path $CapabilitiesRoot -Filter 'capability.json' -Recurse
    foreach ($manifestFile in $manifestFiles) {
        $capDir = $manifestFile.Directory.FullName
        $raw = Get-Content $manifestFile.FullName -Raw
        try {
            $manifest = $raw | ConvertFrom-Json
        } catch {
            $diagnostics += [pscustomobject]@{
                capabilityPath = $manifestFile.FullName
                reason         = "invalid-json: $($_.Exception.Message)"
            }
            continue
        }

        if ($Schema) {
            $validation = Test-JsonSchema -Data $manifest -Schema $Schema
            if (-not $validation.Valid) {
                $diagnostics += [pscustomobject]@{
                    capabilityId   = $manifest.id
                    capabilityPath = $manifestFile.FullName
                    reason         = "manifest-schema-invalid: $($validation.Errors -join '; ')"
                }
                continue
            }
        }

        if (-not (Test-InterfaceVersionSupported -InterfaceVersion $manifest.interfaceVersion)) {
            $diagnostics += [pscustomobject]@{
                capabilityId   = $manifest.id
                capabilityPath = $manifestFile.FullName
                reason         = "unsupported-interface-version: capability declares '$($manifest.interfaceVersion)', core supports majors [$($script:SupportedInterfaceMajors -join ',')]"
            }
            continue
        }

        $modulePath = Join-Path $capDir $manifest.entryModule
        if (-not (Test-Path $modulePath)) {
            $diagnostics += [pscustomobject]@{
                capabilityId   = $manifest.id
                capabilityPath = $manifestFile.FullName
                reason         = "entry-module-missing: '$modulePath' does not exist"
            }
            continue
        }

        if ($script:ValidRepairPostures -notcontains $manifest.repairPosture) {
            $diagnostics += [pscustomobject]@{
                capabilityId   = $manifest.id
                capabilityPath = $manifestFile.FullName
                reason         = "invalid-repair-posture: '$($manifest.repairPosture)' not in [$($script:ValidRepairPostures -join ', ')]"
            }
            continue
        }

        if ($manifest.repairPosture -eq 'repair-supported' -and -not $manifest.entryPoints.repair) {
            $diagnostics += [pscustomobject]@{
                capabilityId   = $manifest.id
                capabilityPath = $manifestFile.FullName
                reason         = "repair-entry-point-missing: repairPosture is 'repair-supported' but entryPoints.repair is not declared"
            }
            continue
        }

        $dependsOn = @()
        if ($manifest.dependsOn) { $dependsOn = @($manifest.dependsOn) }

        $candidates += [pscustomobject]@{
            Id               = $manifest.id
            InterfaceVersion = $manifest.interfaceVersion
            Tier             = $manifest.tier
            DisplayName      = $manifest.displayName
            Description      = $manifest.description
            ModulePath       = $modulePath
            CapabilityDir    = $capDir
            ManifestPath     = $manifestFile.FullName
            Manifest         = $manifest
            DependsOn        = $dependsOn
            RepairPosture    = $manifest.repairPosture
        }
    }

    [pscustomobject]@{ Candidates = $candidates; Diagnostics = $diagnostics }
}

function Resolve-DuplicateCapabilityIds {
    <#
        Task 1.9: reject every capability id that is declared by more than one
        manifest — including the "same id, different version" case (D17) — before
        any of them execute. All manifests sharing a contested id are rejected, not
        just the "extra" ones, since which is authoritative is not knowable here.
    #>
    param([Parameter(Mandatory)] [array] $Candidates)

    $diagnostics = @()
    $survivors = @()
    $byId = $Candidates | Group-Object -Property Id

    foreach ($group in $byId) {
        if ($group.Count -gt 1) {
            $siblings = $group.Group | ForEach-Object { "$($_.ManifestPath) (v$($_.InterfaceVersion))" }
            foreach ($dup in $group.Group) {
                $diagnostics += [pscustomobject]@{
                    capabilityId   = $dup.Id
                    capabilityPath = $dup.ManifestPath
                    reason         = "duplicate-capability-id: '$($dup.Id)' is declared by $($group.Count) manifests: $($siblings -join '; ')"
                }
            }
        } else {
            $survivors += $group.Group[0]
        }
    }

    [pscustomobject]@{ Survivors = $survivors; Diagnostics = $diagnostics }
}

function Resolve-DependsOn {
    <#
        Task 1.10: every dependsOn entry must resolve to a capability id that is
        (still) loaded. A missing dependency excludes only the dependent capability
        (a load diagnostic, not a silent drop) — it never cascades to unrelated
        capabilities. Runs to a fixpoint because excluding a dependent can itself
        break a capability that depended on it.
    #>
    param([Parameter(Mandatory)] [array] $Candidates)

    $diagnostics = @()
    $survivors = [System.Collections.Generic.List[object]]::new()
    foreach ($c in $Candidates) { $survivors.Add($c) }

    $rejectedIds = New-Object System.Collections.Generic.HashSet[string]

    $changed = $true
    while ($changed) {
        $changed = $false
        $survivorIds = New-Object System.Collections.Generic.HashSet[string]
        foreach ($s in $survivors) { [void]$survivorIds.Add($s.Id) }

        foreach ($candidate in @($survivors)) {
            $missing = @($candidate.DependsOn | Where-Object { -not $survivorIds.Contains($_) })
            if ($missing.Count -gt 0 -and -not $rejectedIds.Contains($candidate.Id)) {
                $diagnostics += [pscustomobject]@{
                    capabilityId   = $candidate.Id
                    capabilityPath = $candidate.ManifestPath
                    reason         = "missing-dependency: capability '$($candidate.Id)' depends on unknown/unloaded capabilit$(if ($missing.Count -eq 1) { 'y' } else { 'ies' }) [$($missing -join ', ')]"
                }
                [void]$rejectedIds.Add($candidate.Id)
                [void]$survivors.Remove($candidate)
                $changed = $true
            }
        }
    }

    [pscustomobject]@{ Survivors = @($survivors); Diagnostics = $diagnostics }
}

function Resolve-DependencyCycles {
    <#
        Task 1.10: reject every capability participating in a dependency cycle.
        A cycle is a load diagnostic naming every capability id in it; none of the
        cycle's members execute (they are excluded from the returned survivors).
    #>
    param([Parameter(Mandatory)] [array] $Candidates)

    $byId = @{}
    foreach ($c in $Candidates) { $byId[$c.Id] = $c }

    $state = @{}   # id -> 'visiting' | 'done'
    $stack = [System.Collections.Generic.List[string]]::new()
    $cycleIdSets = @()

    function Find-CycleFrom {
        param([string] $Id)
        if ($state[$Id] -eq 'done') { return }
        if ($stack.Contains($Id)) {
            $cycleStart = $stack.IndexOf($Id)
            $cycleMembers = @($stack.GetRange($cycleStart, $stack.Count - $cycleStart))
            $script:cycleIdSets += , @($cycleMembers)
            return
        }
        $state[$Id] = 'visiting'
        $stack.Add($Id)
        foreach ($dep in $byId[$Id].DependsOn) {
            if ($byId.ContainsKey($dep)) { Find-CycleFrom -Id $dep }
        }
        $stack.RemoveAt($stack.Count - 1)
        $state[$Id] = 'done'
    }

    $script:cycleIdSets = @()
    foreach ($id in $byId.Keys) {
        if ($state[$id] -ne 'done') { Find-CycleFrom -Id $id }
    }

    $inCycle = New-Object System.Collections.Generic.HashSet[string]
    $diagnostics = @()
    foreach ($cycle in $script:cycleIdSets) {
        $uniqueCycleMembers = @($cycle | Select-Object -Unique)
        foreach ($id in $uniqueCycleMembers) { [void]$inCycle.Add($id) }
    }

    if ($inCycle.Count -gt 0) {
        $memberList = ($inCycle | Sort-Object) -join ', '
        foreach ($id in $inCycle) {
            $diagnostics += [pscustomobject]@{
                capabilityId   = $id
                capabilityPath = $byId[$id].ManifestPath
                reason         = "dependency-cycle: capability '$id' participates in a dependency cycle with [$memberList]; none of these capabilities execute"
            }
        }
    }

    $survivors = @($Candidates | Where-Object { -not $inCycle.Contains($_.Id) })
    [pscustomobject]@{ Survivors = $survivors; Diagnostics = $diagnostics }
}

function Find-Capabilities {
    <#
        Returns @{ Capabilities = [...]; LoadDiagnostics = [...] }. Capabilities is an
        array of objects exposing the resolved functions (Availability/Run/Diagnostics
        /Baseline/RepairHints/Repair) so the caller never touches module internals
        directly. Registry hardening (duplicate id/version, dependsOn, cycles) is
        resolved across the full candidate set before any module is imported.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $CapabilitiesRoot,
        [string] $ManifestSchemaPath
    )

    $capabilities = @()
    $loadDiagnostics = @()

    if (-not (Test-Path $CapabilitiesRoot)) {
        return [pscustomobject]@{ Capabilities = $capabilities; LoadDiagnostics = $loadDiagnostics }
    }

    $schema = $null
    if ($ManifestSchemaPath -and (Test-Path $ManifestSchemaPath)) {
        $schema = Get-Content $ManifestSchemaPath -Raw | ConvertFrom-Json
    }

    # Phase A: per-manifest validation, no imports yet.
    $resolved = Resolve-CandidateManifests -CapabilitiesRoot $CapabilitiesRoot -Schema $schema
    $loadDiagnostics += $resolved.Diagnostics
    $candidates = $resolved.Candidates

    # Phase B: registry hardening across the full candidate set (D17, tasks 1.9/1.10).
    $dedup = Resolve-DuplicateCapabilityIds -Candidates $candidates
    $loadDiagnostics += $dedup.Diagnostics
    $candidates = $dedup.Survivors

    $depsResolved = Resolve-DependsOn -Candidates $candidates
    $loadDiagnostics += $depsResolved.Diagnostics
    $candidates = $depsResolved.Survivors

    $cyclesResolved = Resolve-DependencyCycles -Candidates $candidates
    $loadDiagnostics += $cyclesResolved.Diagnostics
    $candidates = $cyclesResolved.Survivors

    # Re-check dependsOn once more: removing cycle members can strand capabilities
    # outside the cycle that depended on a now-removed cycle member.
    $depsResolved2 = Resolve-DependsOn -Candidates $candidates
    $loadDiagnostics += $depsResolved2.Diagnostics
    $candidates = $depsResolved2.Survivors

    # Phase C: import modules and resolve entry-point functions only for
    # capabilities that survived registry hardening.
    foreach ($candidate in $candidates) {
        try {
            Import-Module $candidate.ModulePath -Force -Global -ErrorAction Stop
        } catch {
            $loadDiagnostics += [pscustomobject]@{
                capabilityId   = $candidate.Id
                capabilityPath = $candidate.ManifestPath
                reason         = "module-import-failed: $($_.Exception.Message)"
            }
            continue
        }

        $entryPoints = $candidate.Manifest.entryPoints
        $missingFns = @()
        foreach ($required in @('availability', 'run', 'diagnostics')) {
            $fnName = $entryPoints.$required
            if (-not $fnName -or -not (Get-Command $fnName -ErrorAction SilentlyContinue)) {
                $missingFns += "$required -> '$fnName'"
            }
        }
        if ($candidate.RepairPosture -eq 'repair-supported') {
            $fnName = $entryPoints.repair
            if (-not $fnName -or -not (Get-Command $fnName -ErrorAction SilentlyContinue)) {
                $missingFns += "repair -> '$fnName'"
            }
        }
        if ($missingFns.Count -gt 0) {
            $loadDiagnostics += [pscustomobject]@{
                capabilityId   = $candidate.Id
                capabilityPath = $candidate.ManifestPath
                reason         = "entry-point-functions-missing: $($missingFns -join '; ')"
            }
            continue
        }

        $capabilities += [pscustomobject]@{
            Id               = $candidate.Id
            InterfaceVersion = $candidate.InterfaceVersion
            Tier             = $candidate.Tier
            DisplayName      = $candidate.DisplayName
            Description      = $candidate.Description
            ModulePath       = $candidate.ModulePath
            CapabilityDir    = $candidate.CapabilityDir
            Manifest         = $candidate.Manifest
            DependsOn        = $candidate.DependsOn
            RepairPosture    = $candidate.RepairPosture
            Fn               = [pscustomobject]@{
                Availability = $entryPoints.availability
                Run          = $entryPoints.run
                Diagnostics  = $entryPoints.diagnostics
                Baseline     = $entryPoints.baseline
                RepairHints  = $entryPoints.repairHints
                Repair       = $entryPoints.repair
            }
        }
    }

    [pscustomobject]@{
        Capabilities    = $capabilities
        LoadDiagnostics = $loadDiagnostics
    }
}

Export-ModuleMember -Function Find-Capabilities, Test-InterfaceVersionSupported
