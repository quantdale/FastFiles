<#
    Install-certification orchestration plan (hardening-campaign Phase 5).

    Pure orchestration vocabulary for clean-host install/service certification:
    the fixed section-11 step catalog, the step-result constructor with its
    vocabulary guard, the skeleton classifier that maps injected preflight /
    provenance / eligibility / residual readings onto per-step statuses, and
    the evidence-bundle writer (JSON + Markdown).

    Section-11 step mapping (catalog order):
      preflight-contamination .... §11 preflight (step-0 veto)
      provenance-binding ......... §11 provenance (source + binary binding)
      install-execution .......... §11 install
      service-registration-startup  §11 service registration/startup
      app-startup ................ §11 app startup
      handshake-verification ..... §11 handshake
      privileged-eligibility ..... §11 privileged eligibility
      representative-operation ... §11 representative op
      shutdown ................... §11 shutdown
      uninstall-execution ........ §11 uninstall
      cleanup-verification ....... §11 service/task/registry/filesystem cleanup
      residue-scan ............... §11 residue scan

    Status vocabulary (every step, no exceptions):
      PASS | FAIL | SKIPPED | BLOCKED_EXTERNAL

    Structural guards (fail-closed, enforced by New-CertStepResult):
      - an unknown status string throws (cert-step-vocabulary-violation);
      - a missing reason throws (cert-step-reason-required);
      - an externally-gated step (RequiresExternal) reporting PASS throws
        (cert-step-external-pass-violation): blocked work is BLOCKED_EXTERNAL,
        deferred work is SKIPPED, attempted-but-broken work is FAIL -- never PASS.

    No-mutation policy: nothing in this module executes installers, starts or
    stops services, launches applications, or writes outside the caller-chosen
    evidence directory. Live elevated/clean-room execution stays
    BLOCKED_EXTERNAL; the skeleton never reports overall PASS because its
    mutating steps never execute. Wiring real execution is future owner work
    on an authorized clean elevated host -- this module must never be taught
    to fake it.
#>

$script:StepStatusVocabulary = @('PASS', 'FAIL', 'SKIPPED', 'BLOCKED_EXTERNAL')

$script:StepCatalog = @(
    [pscustomobject]@{ Id = 'preflight-contamination';      Title = 'Step 0: clean-host preflight (contamination veto)'; MapsTo = 'section-11 preflight';                     RequiresExternal = $false }
    [pscustomobject]@{ Id = 'provenance-binding';           Title = 'Provenance binding (source + binary identity)';      MapsTo = 'section-11 provenance';                   RequiresExternal = $false }
    [pscustomobject]@{ Id = 'install-execution';            Title = 'Install execution';                                 MapsTo = 'section-11 install';                      RequiresExternal = $true }
    [pscustomobject]@{ Id = 'service-registration-startup'; Title = 'Service registration and startup';                 MapsTo = 'section-11 service registration/startup';   RequiresExternal = $true }
    [pscustomobject]@{ Id = 'app-startup';                  Title = 'Application startup';                               MapsTo = 'section-11 app startup';                  RequiresExternal = $true }
    [pscustomobject]@{ Id = 'handshake-verification';       Title = 'Engine/service handshake verification';            MapsTo = 'section-11 handshake';                    RequiresExternal = $true }
    [pscustomobject]@{ Id = 'privileged-eligibility';       Title = 'Privileged-scan eligibility';                      MapsTo = 'section-11 privileged eligibility';       RequiresExternal = $true }
    [pscustomobject]@{ Id = 'representative-operation';     Title = 'Representative product operation';                 MapsTo = 'section-11 representative op';            RequiresExternal = $true }
    [pscustomobject]@{ Id = 'shutdown';                     Title = 'Runtime shutdown';                                  MapsTo = 'section-11 shutdown';                     RequiresExternal = $false }
    [pscustomobject]@{ Id = 'uninstall-execution';          Title = 'Uninstall execution';                               MapsTo = 'section-11 uninstall';                    RequiresExternal = $true }
    [pscustomobject]@{ Id = 'cleanup-verification';         Title = 'Service/task/registry/filesystem cleanup check';   MapsTo = 'section-11 cleanup';                     RequiresExternal = $false }
    [pscustomobject]@{ Id = 'residue-scan';                 Title = 'Post-run residue scan';                             MapsTo = 'section-11 residue scan';                RequiresExternal = $false }
)

function Get-InstallCertificationStepCatalog {
    return @($script:StepCatalog | ForEach-Object {
        [pscustomobject]@{ Id = $_.Id; Title = $_.Title; MapsTo = $_.MapsTo; RequiresExternal = $_.RequiresExternal }
    })
}

function Get-InstallCertificationStepVocabulary {
    return @($script:StepStatusVocabulary)
}

function Get-InstallCertificationExternalBoundary {
    return @(
        'elevation: installer execution, SCM service registration/startup, and uninstall require an Administrator token this harness never acquires',
        'clean-room host: install certification requires HOST_STATE=CLEAN; this development host is CONTAMINATED (stale FastFilesIndexSvc install and prior installs)',
        'production signing and pins: signer pins are observed read-only from the CMake cache; certificates are never provisioned and trust stores are never mutated',
        'live execution: candidate install, service start, app launch, handshake, representative operation, live shutdown, and uninstall are never executed by this skeleton',
        'interactive session and disposable-guest matrix: physical-display, foreground, and guest-provider evidence remain external',
        'privileged volume validation: real MFT/USN operation is outside this harness and stays external'
    )
}

function New-CertStepResult {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $Id,
        [Parameter(Mandatory)] [string] $Status,
        [Parameter(Mandatory)] [AllowEmptyString()] [string] $Reason,
        [string] $Detail = '',
        [double] $DurationMs = 0,
        [switch] $RequiresExternal
    )
    $catalogEntry = @($script:StepCatalog | Where-Object { $_.Id -eq $Id })
    if ($catalogEntry.Count -eq 0) {
        throw "cert-step-unknown-id: step '$Id' is not in the section-11 catalog"
    }
    if ($script:StepStatusVocabulary -notcontains $Status) {
        throw "cert-step-vocabulary-violation: step '$Id' status '$Status' is not in ($($script:StepStatusVocabulary -join ','))"
    }
    if ([string]::IsNullOrWhiteSpace($Reason)) {
        throw "cert-step-reason-required: step '$Id' must carry a machine-readable reason"
    }
    if ($RequiresExternal -and $Status -eq 'PASS') {
        throw "cert-step-external-pass-violation: externally-gated step '$Id' must never report PASS (use BLOCKED_EXTERNAL, SKIPPED, or FAIL)"
    }
    return [pscustomobject]@{
        id               = $Id
        title            = $catalogEntry[0].Title
        mapsTo           = $catalogEntry[0].MapsTo
        status           = $Status
        reason           = $Reason
        detail           = [string]$Detail
        durationMs       = [math]::Round($DurationMs, 0)
        requiresExternal = [bool]$RequiresExternal
    }
}

function New-InstallCertificationSkeletonResult {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] $Preflight,
        $SourceState = $null,
        $Binding = $null,
        [AllowEmptyCollection()] [object[]] $BinaryRecords = @(),
        $Eligibility = $null,
        $Residual = $null,
        [string] $CandidatePath = ''
    )

    $steps = [System.Collections.Generic.List[object]]::new()

    $preflightState = [string]$Preflight.hostState
    $veto = ($preflightState -ne 'CLEAN')
    $presentList = ((@($Preflight.presentMarkers) | Where-Object { $_ }) -join ',')
    $unknownList = ((@($Preflight.unknownMarkers) | Where-Object { $_ }) -join ',')

    switch ($preflightState) {
        'CLEAN' {
            [void]$steps.Add((New-CertStepResult -Id 'preflight-contamination' -Status 'PASS' -Reason 'host-clean' `
                -Detail 'contamination scan completed; no markers present and no probe errors'))
        }
        'CONTAMINATED' {
            [void]$steps.Add((New-CertStepResult -Id 'preflight-contamination' -Status 'BLOCKED_EXTERNAL' -Reason 'host-contaminated-veto' `
                -Detail "HOST_STATE=CONTAMINATED; present=$presentList; install journey not attempted"))
        }
        default {
            [void]$steps.Add((New-CertStepResult -Id 'preflight-contamination' -Status 'BLOCKED_EXTERNAL' -Reason 'host-state-unknown-veto' `
                -Detail "HOST_STATE=$preflightState; unknown=$unknownList; clean state unprovable so install journey not attempted"))
        }
    }

    $binaries = @($BinaryRecords)
    if ($null -eq $SourceState -or [string]$SourceState.headSha -notmatch '^[0-9a-f]{40}$') {
        [void]$steps.Add((New-CertStepResult -Id 'provenance-binding' -Status 'FAIL' -Reason 'provenance-source-unresolvable' `
            -Detail 'repository HEAD could not be resolved; no binding is provable'))
    } elseif ($null -ne $Binding -and -not [bool]$Binding.valid) {
        $codes = ((@($Binding.issueCodes) | Where-Object { $_ }) -join ',')
        [void]$steps.Add((New-CertStepResult -Id 'provenance-binding' -Status 'FAIL' -Reason "provenance-binding-failed:$codes" `
            -Detail "binary-to-source binding invalid: $codes"))
    } elseif ($null -eq $Binding -and $binaries.Count -gt 0) {
        [void]$steps.Add((New-CertStepResult -Id 'provenance-binding' -Status 'FAIL' -Reason 'provenance-binding-missing' `
            -Detail 'binaries were hashed but no binding was evaluated; PASS without binding is not allowed'))
    } else {
        $head = [string]$SourceState.headSha
        $tree = [string]$SourceState.treeSha
        $cleanFlag = [bool]$SourceState.sourceClean
        [void]$steps.Add((New-CertStepResult -Id 'provenance-binding' -Status 'PASS' -Reason 'source-bound' `
            -Detail "head=$head; tree=$tree; sourceClean=$cleanFlag; binaries=$($binaries.Count)"))
    }

    $candidateNote = if ([string]::IsNullOrWhiteSpace($CandidatePath)) { 'no candidate supplied' } else { "candidate recorded-only: $CandidatePath" }
    $installReason = if ($veto) { 'preflight-veto; install-requires-clean-elevated-host' } else { 'install-requires-elevation-clean-host-and-authorized-candidate' }
    [void]$steps.Add((New-CertStepResult -Id 'install-execution' -Status 'BLOCKED_EXTERNAL' -Reason $installReason -RequiresExternal `
        -Detail "skeleton never executes installers ($candidateNote); live installer execution stays external"))
    [void]$steps.Add((New-CertStepResult -Id 'service-registration-startup' -Status 'BLOCKED_EXTERNAL' -Reason 'service-registration-requires-elevated-install' -RequiresExternal `
        -Detail 'SCM registration and service startup mutate host state and require elevation plus a prior install'))
    [void]$steps.Add((New-CertStepResult -Id 'app-startup' -Status 'BLOCKED_EXTERNAL' -Reason 'app-startup-requires-installed-runtime' -RequiresExternal `
        -Detail 'no installed runtime exists on this host; launching a staged copy would not certify the installed product'))
    [void]$steps.Add((New-CertStepResult -Id 'handshake-verification' -Status 'BLOCKED_EXTERNAL' -Reason 'handshake-requires-live-service-and-installed-runtime' -RequiresExternal `
        -Detail 'mutual-auth handshake needs a live FastFilesIndexSvc paired with its installed engine'))
    if ($null -eq $Eligibility) {
        [void]$steps.Add((New-CertStepResult -Id 'privileged-eligibility' -Status 'SKIPPED' -Reason 'eligibility-unobserved' -RequiresExternal `
            -Detail 'no eligibility reading was supplied; the privileged capability stays unevaluated'))
    } else {
        $absences = @($Eligibility.Absences | Where-Object { $_ })
        if ($absences.Count -eq 0) {
            [void]$steps.Add((New-CertStepResult -Id 'privileged-eligibility' -Status 'PASS' -Reason 'privileged-eligible-observed' `
                -Detail 'eligibility probe reported zero absences (fixture path; campaign policy always declares absences on real hosts)'))
        } else {
            [void]$steps.Add((New-CertStepResult -Id 'privileged-eligibility' -Status 'BLOCKED_EXTERNAL' -Reason 'privileged-scan-unavailable' -RequiresExternal `
                -Detail ("absences=" + ($absences -join ',') + '; eligible=false')))
        }
    }
    [void]$steps.Add((New-CertStepResult -Id 'representative-operation' -Status 'BLOCKED_EXTERNAL' -Reason 'representative-op-requires-live-runtime' -RequiresExternal `
        -Detail 'no live runtime exists to exercise; a staged binary cannot stand in for the installed product'))
    [void]$steps.Add((New-CertStepResult -Id 'shutdown' -Status 'SKIPPED' -Reason 'no-runtime-launched' `
        -Detail 'the skeleton launches no processes, so there is nothing to shut down'))
    [void]$steps.Add((New-CertStepResult -Id 'uninstall-execution' -Status 'BLOCKED_EXTERNAL' -Reason 'uninstall-requires-installed-product' -RequiresExternal `
        -Detail 'nothing was installed by this harness; uninstall would mutate host service/group/registry state and stays external'))
    [void]$steps.Add((New-CertStepResult -Id 'cleanup-verification' -Status 'PASS' -Reason 'no-harness-owned-mutations' `
        -Detail 'skeleton creates no service, task, registry, or filesystem state, so no harness-owned cleanup is due'))

    if ($null -eq $Residual -or [string]::IsNullOrWhiteSpace([string]$Residual.hostState)) {
        [void]$steps.Add((New-CertStepResult -Id 'residue-scan' -Status 'FAIL' -Reason 'residue-scan-unavailable' `
            -Detail 'post-run contamination scan produced no reading; residual state is unproven'))
    } else {
        $before = @($Preflight.presentMarkers | Where-Object { $_ })
        $after = @($Residual.presentMarkers | Where-Object { $_ })
        $introduced = @($after | Where-Object { $before -notcontains $_ })
        $residualState = [string]$Residual.hostState
        if ($introduced.Count -gt 0) {
            [void]$steps.Add((New-CertStepResult -Id 'residue-scan' -Status 'FAIL' -Reason 'residue-introduced' `
                -Detail ("harness run introduced markers: " + ($introduced -join ','))))
        } elseif ($residualState -eq 'UNKNOWN') {
            $resUnknown = ((@($Residual.unknownMarkers) | Where-Object { $_ }) -join ',')
            [void]$steps.Add((New-CertStepResult -Id 'residue-scan' -Status 'FAIL' -Reason 'residue-scan-inconclusive' `
                -Detail "residual scan errored on: $resUnknown; clean residue unprovable"))
        } else {
            [void]$steps.Add((New-CertStepResult -Id 'residue-scan' -Status 'PASS' -Reason 'no-new-residue' `
                -Detail ("HOST_STATE=$residualState; present=" + ($after -join ','))))
        }
    }

    $failedIds = @($steps | Where-Object { $_.status -eq 'FAIL' } | ForEach-Object { $_.id })
    $blockedIds = @($steps | Where-Object { $_.status -eq 'BLOCKED_EXTERNAL' } | ForEach-Object { $_.id })
    if ($veto) {
        $overallStatus = 'BLOCKED_EXTERNAL'
        $overallReason = "preflight-veto: HOST_STATE=$preflightState; install journey not attempted"
    } elseif ($failedIds.Count -gt 0) {
        $overallStatus = 'FAIL'
        $overallReason = 'cert-step-failed: ' + ($failedIds -join ',')
    } elseif ($blockedIds.Count -gt 0) {
        $overallStatus = 'BLOCKED_EXTERNAL'
        $overallReason = 'externally-gated-steps-pending: ' + ($blockedIds -join ',')
    } else {
        $overallStatus = 'PASS'
        $overallReason = 'install-certified'
    }

    return [pscustomobject]@{
        steps         = @($steps)
        overallStatus = $overallStatus
        overallReason = $overallReason
        veto          = [bool]$veto
        hostState     = $preflightState
    }
}

function Write-InstallCertificationEvidence {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $OutputRoot,
        $SourceState = $null,
        [AllowEmptyCollection()] [object[]] $BinaryRecords = @(),
        $Binding = $null,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [object[]] $Steps,
        [Parameter(Mandatory)] [string] $OverallStatus,
        [Parameter(Mandatory)] [string] $OverallReason,
        $Preflight = $null,
        $Residual = $null,
        $Eligibility = $null,
        [string] $EligibilitySource = '',
        [string] $RepoRoot = '',
        [string] $CandidatePath = '',
        [string] $Stamp = ''
    )
    if ($script:StepStatusVocabulary -notcontains $OverallStatus) {
        throw "cert-step-vocabulary-violation: overall status '$OverallStatus' is not in ($($script:StepStatusVocabulary -join ','))"
    }
    if ([string]::IsNullOrWhiteSpace($Stamp)) {
        $Stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
    }
    # Write-only bundle: the stamp directory is always new (a numeric suffix
    # disambiguates reruns within the same second). Prior runs are never read.
    $bundleDir = Join-Path $OutputRoot $Stamp
    $suffix = 0
    while (Test-Path -LiteralPath $bundleDir) {
        $suffix++
        $bundleDir = (Join-Path $OutputRoot ("$Stamp-$suffix"))
    }
    New-Item -ItemType Directory -Path $bundleDir -Force | Out-Null

    if ($null -eq $SourceState -or [string]$SourceState.headSha -notmatch '^[0-9a-f]{40}$') {
        $sourceJson = [pscustomobject]@{ headSha = $null; treeSha = $null; sourceClean = $null; note = 'source-unresolvable' }
    } else {
        $sourceJson = [pscustomobject]@{
            headSha      = [string]$SourceState.headSha
            treeSha      = [string]$SourceState.treeSha
            treeSemantic = 'HEAD^{tree}'
            sourceClean  = [bool]$SourceState.sourceClean
            stateDigest  = [string]$SourceState.stateDigest
            mode         = [string]$SourceState.mode
        }
    }
    $binaryJson = @($BinaryRecords | ForEach-Object {
        [pscustomobject]@{
            filename   = [string]$_.filename
            path       = [string]$_.path
            sha256     = [string]$_.sha256
            buildIdentity = [string]$_.buildIdentity
            peArchitecture = [string]$_.peArchitecture
            issues     = @($_.issues)
        }
    })
    if ($null -eq $Binding) {
        $bindingJson = [pscustomobject]@{ evaluated = $false; valid = $null; issueCodes = @(); note = 'binding-not-evaluated' }
    } else {
        $bindingJson = [pscustomobject]@{
            evaluated  = $true
            valid      = [bool]$Binding.valid
            issueCodes = @($Binding.issueCodes)
            setIdentity = [string]$Binding.setIdentity
        }
    }
    if ($null -eq $Preflight) {
        $preflightJson = [pscustomobject]@{ hostState = 'UNKNOWN'; presentMarkers = @(); unknownMarkers = @(); note = 'preflight-unavailable' }
    } else {
        $preflightJson = [pscustomobject]@{
            hostState      = [string]$Preflight.hostState
            presentMarkers = @($Preflight.presentMarkers)
            unknownMarkers = @($Preflight.unknownMarkers)
            markers        = @($Preflight.markers)
            checkedAtUtc   = [string]$Preflight.checkedAtUtc
        }
    }
    if ($null -eq $Residual) {
        $residualJson = [pscustomobject]@{ hostState = 'UNKNOWN'; presentMarkers = @(); unknownMarkers = @(); note = 'residual-unavailable' }
    } else {
        $residualJson = [pscustomobject]@{
            hostState      = [string]$Residual.hostState
            presentMarkers = @($Residual.presentMarkers)
            unknownMarkers = @($Residual.unknownMarkers)
            markers        = @($Residual.markers)
            checkedAtUtc   = [string]$Residual.checkedAtUtc
        }
    }
    if ($null -eq $Eligibility) {
        $eligibilityJson = [pscustomobject]@{ observed = $false; eligible = $false; absences = @(); note = 'eligibility-unobserved' }
    } else {
        $eligibilityJson = [pscustomobject]@{
            observed     = $true
            source       = [string]$EligibilitySource
            eligible     = [bool]$Eligibility.Eligible
            absences     = @($Eligibility.Absences)
            serviceState = [string]$Eligibility.ServiceState
            groupState   = [string]$Eligibility.GroupState
            pinState     = [string]$Eligibility.PinState
            elevated     = [bool]$Eligibility.Elevated
        }
    }

    $evidence = [ordered]@{
        schemaVersion = 1
        kind          = 'fastfiles-install-certification'
        stamp         = [IO.Path]::GetFileName($bundleDir)
        generatedAtUtc = [DateTime]::UtcNow.ToString('o')
        repoRoot      = [string]$RepoRoot
        candidate     = [ordered]@{
            path = [string]$CandidatePath
            note = 'recorded-only; the skeleton never executes installer candidates'
        }
        source        = $sourceJson
        provenanceBinding = $bindingJson
        binaries      = @($binaryJson)
        preflight     = $preflightJson
        eligibility   = $eligibilityJson
        steps         = @($Steps)
        overall       = [ordered]@{ status = $OverallStatus; reason = $OverallReason }
        residual      = $residualJson
        externalBoundary = @(Get-InstallCertificationExternalBoundary)
        policy        = 'no-elevation; no trust-store mutation; no live installer/service execution; residue scans are read-only'
    }

    $jsonPath = Join-Path $bundleDir 'install-certification-evidence.json'
    ($evidence | ConvertTo-Json -Depth 12) | Set-Content -LiteralPath $jsonPath -Encoding utf8NoBOM

    $lines = [System.Collections.Generic.List[string]]::new()
    [void]$lines.Add('# FastFiles install-certification evidence')
    [void]$lines.Add('')
    [void]$lines.Add("- stamp: $([IO.Path]::GetFileName($bundleDir))")
    [void]$lines.Add("- generated (UTC): $($evidence.generatedAtUtc)")
    [void]$lines.Add("- overall: $($OverallStatus) ($($OverallReason))")
    [void]$lines.Add("- preflight HOST_STATE: $($preflightJson.hostState)")
    [void]$lines.Add("- residual HOST_STATE: $($residualJson.hostState)")
    [void]$lines.Add('')
    [void]$lines.Add('## Source binding')
    [void]$lines.Add('')
    [void]$lines.Add("- head: $($sourceJson.headSha)")
    [void]$lines.Add("- tree: $($sourceJson.treeSha) (HEAD^{tree})")
    [void]$lines.Add("- sourceClean: $($sourceJson.sourceClean)")
    [void]$lines.Add("- binding evaluated: $($bindingJson.evaluated); valid: $($bindingJson.valid); issues: $(($bindingJson.issueCodes -join ','))")
    [void]$lines.Add('')
    [void]$lines.Add('## Binaries (SHA-256)')
    [void]$lines.Add('')
    if (@($binaryJson).Count -eq 0) { [void]$lines.Add('- none (no build outputs were present to hash)') }
    foreach ($b in @($binaryJson)) { [void]$lines.Add("- $($b.filename): $($b.sha256)") }
    [void]$lines.Add('')
    [void]$lines.Add('## Steps')
    [void]$lines.Add('')
    [void]$lines.Add('| id | status | reason | detail |')
    [void]$lines.Add('|----|--------|--------|--------|')
    foreach ($s in @($Steps)) { [void]$lines.Add("| $($s.id) | $($s.status) | $($s.reason) | $($s.detail) |") }
    [void]$lines.Add('')
    [void]$lines.Add('## Privileged eligibility')
    [void]$lines.Add('')
    [void]$lines.Add("- observed: $($eligibilityJson.observed); eligible: $($eligibilityJson.eligible)")
    [void]$lines.Add("- absences: $(($eligibilityJson.absences -join ','))")
    [void]$lines.Add('')
    [void]$lines.Add('## External boundary (stays external)')
    [void]$lines.Add('')
    foreach ($item in @(Get-InstallCertificationExternalBoundary)) { [void]$lines.Add("- $item") }
    $mdPath = Join-Path $bundleDir 'evidence.md'
    ($lines -join "`r`n") | Set-Content -LiteralPath $mdPath -Encoding utf8NoBOM

    return [pscustomobject]@{ Directory = $bundleDir; JsonPath = $jsonPath; MarkdownPath = $mdPath }
}

Export-ModuleMember -Function @(
    'Get-InstallCertificationStepCatalog',
    'Get-InstallCertificationStepVocabulary',
    'Get-InstallCertificationExternalBoundary',
    'New-CertStepResult',
    'New-InstallCertificationSkeletonResult',
    'Write-InstallCertificationEvidence'
)
