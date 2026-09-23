<#
    FastFiles install-certification harness entry point (hardening-campaign Phase 5).

    Read-only orchestration skeleton over the section-11 certification step
    list. It NEVER installs, registers, starts, launches, handshakes, operates,
    shuts down, or uninstalls anything: every mutating step classifies
    BLOCKED_EXTERNAL with a reason, and overall PASS is unreachable until real
    execution is wired on an authorized clean elevated host (future owner work).

    What it does today (all read-only):
      step-0 preflight  Get-HostContamination (vetoes on CONTAMINATED/UNKNOWN)
      provenance        RepositoryState source record + Provenance binary binding
                        over the five default build/debug exes when present
      eligibility       Resolve-UiaPrivilegedScanEligibility when its module is
                        importable, else equivalent read-only probes
      residue scan      second contamination reading bound into the evidence
      evidence bundle   JSON + Markdown under verify/runs/clean-host/<stamp>/
                        (write-only; prior runs are never read)

    Section-11 coverage lives in InstallCertificationPlan.psm1
    (Get-InstallCertificationStepCatalog); each emitted step carries its
    MapsTo section-11 label.

    Exit codes: 0 = PASS (unreachable until live execution exists),
    1 = FAIL, 2 = BLOCKED_EXTERNAL.

    Absolute prohibitions: no elevation acquisition, no certificate
    provisioning, no trust-store mutation, no fake production pins, no service /
    group / registry / filesystem mutation. Probes observe only.
#>
[CmdletBinding()]
param(
    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,

    [string] $OutputRoot = '',

    [string] $BuildDirectory = '',

    [string] $CandidatePath = '',

    [string] $Stamp = '',

    [switch] $RequireExactTree,

    [hashtable] $ProbeOverrides = @{}
)

$ErrorActionPreference = 'Stop'

Import-Module (Join-Path $RepoRoot 'verify\core\RepositoryState.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $RepoRoot 'verify\core\Provenance.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $PSScriptRoot 'HostContamination.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $PSScriptRoot 'InstallCertificationPlan.psm1') -Force -DisableNameChecking

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $RepoRoot 'verify\runs\clean-host'
}
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $RepoRoot 'build\debug'
}

$startUtc = [DateTime]::UtcNow
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()

function Get-FallbackPrivilegedEligibility {
    # Equivalent read-only probes for Resolve-UiaPrivilegedScanEligibility,
    # used only when the UIA validation module is not importable. Same absence
    # codes, same shape, same campaign-policy absences; pins are observed from
    # the CMake cache, never provisioned.
    $absences = [System.Collections.Generic.List[string]]::new()

    $serviceState = 'absent'
    $service = Get-Service -Name 'FastFilesIndexSvc' -ErrorAction SilentlyContinue
    if ($null -eq $service) { $absences.Add('service-not-installed') }
    else { $serviceState = [string]$service.Status }

    $groupState = 'probe-unavailable'
    $group = Get-LocalGroup -Name 'FastFilesUsers' -ErrorAction SilentlyContinue
    if ($null -eq $group) {
        $groupState = 'group-missing'
        $absences.Add('service-not-authorized')
    } else {
        $members = @(Get-LocalGroupMember -Name 'FastFilesUsers' -ErrorAction SilentlyContinue)
        if ($null -eq $members -or $members.Count -eq 0) { $groupState = 'members-unreadable' }
        else {
            $who = [Environment]::UserName
            $isMember = $false
            foreach ($m in $members) {
                $n = [string]$m.Name
                if ($n -ieq $who -or $n.EndsWith("\" + $who, [StringComparison]::OrdinalIgnoreCase)) { $isMember = $true; break }
            }
            $groupState = if ($isMember) { 'member' } else { 'not-member' }
            if (-not $isMember) { $absences.Add('service-not-authorized') }
        }
    }

    $pinState = 'cache-missing'
    $cachePath = Join-Path $RepoRoot 'build\debug-diagnostic\CMakeCache.txt'
    if (Test-Path -LiteralPath $cachePath) {
        $cacheText = Get-Content -LiteralPath $cachePath -Raw
        $enginePin = ''
        $svcPin = ''
        if ($cacheText -match '(?m)^FASTFILES_ENGINE_SIGNATURE_THUMBPRINT:[^=]*=(.*)$') { $enginePin = $Matches[1].Trim() }
        if ($cacheText -match '(?m)^FASTFILES_INDEX_SVC_SIGNATURE_THUMBPRINT:[^=]*=(.*)$') { $svcPin = $Matches[1].Trim() }
        $engineOk = -not [string]::IsNullOrWhiteSpace($enginePin) -and $enginePin -notmatch 'NOTFOUND'
        $svcOk = -not [string]::IsNullOrWhiteSpace($svcPin) -and $svcPin -notmatch 'NOTFOUND'
        if ($engineOk -and $svcOk) { $pinState = 'available' } else { $pinState = 'absent' }
    }
    if ($pinState -ne 'available') { $absences.Add('signer-pins-unavailable') }

    $elevated = $false
    try {
        $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
        $principal = New-Object Security.Principal.WindowsPrincipal($identity)
        $elevated = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    } catch { $elevated = $false }
    if (-not $elevated) { $absences.Add('elevation-unavailable') }

    $absences.Add('clean-host-requirement')
    $absences.Add('host-contamination')

    return [pscustomobject]@{
        Absences     = @($absences)
        Eligible     = ($absences.Count -eq 0)
        ServiceState = $serviceState
        GroupState   = $groupState
        PinState     = $pinState
        Elevated     = $elevated
    }
}

Write-Host 'FastFiles install-certification harness (Phase 5 skeleton; read-only, never executes installers or services)'

# Step-0 preflight: contamination veto. A veto records BLOCKED_EXTERNAL and
# refuses the install journey; provenance/eligibility/residue evidence is
# still collected read-only so the bundle explains the veto.
$preflight = Get-HostContamination -ProbeOverrides $ProbeOverrides
Write-Host "  preflight HOST_STATE=$($preflight.hostState) (present=$($preflight.presentMarkers -join ','); unknown=$($preflight.unknownMarkers -join ','))"

# Provenance: source record plus binary binding over the five default exes.
$mode = if ($RequireExactTree) { 'exact-tree' } else { 'development' }
$sourceState = $null
try {
    $sourceState = Get-SourceProvenanceRecord -RepoRoot $RepoRoot -Mode $mode
} catch {
    Write-Host "  provenance source unresolvable: $($_.Exception.Message)"
}
$binaryPaths = Get-DefaultCertificationBinaries -RepoRoot $RepoRoot -BuildDirectory $BuildDirectory
$binaryRecords = @($binaryPaths | ForEach-Object { Get-BinaryProvenanceRecord -Path $_ })
$binding = $null
if ($null -ne $sourceState) {
    $bindingMode = if ($RequireExactTree) { 'strict' } else { 'development' }
    $binding = Test-BinarySourceBinding -BinaryRecords $binaryRecords -SourceRecord $sourceState -Mode $bindingMode
}
$hashedCount = @($binaryRecords | Where-Object { $_.sha256 }).Count
Write-Host "  provenance: head=$(if ($sourceState) { $sourceState.headSha } else { 'unresolvable' }); binaries hashed=$hashedCount/$($binaryRecords.Count); binding=$((if ($binding -and $binding.valid) { 'valid' } elseif ($binding) { 'INVALID:' + ($binding.issueCodes -join ',') } else { 'unevaluated' }))"

# Privileged eligibility: reuse the UIA resolver when importable, else the
# equivalent read-only probes above. Either way: observed, never mutated.
$eligibility = $null
$eligibilitySource = 'fallback-probes'
try {
    Import-Module (Join-Path $RepoRoot 'verify\capabilities\windows-ui-automation-validation\UiAutomationValidation.psm1') -Force -DisableNameChecking -ErrorAction Stop
    $eligibility = Resolve-UiaPrivilegedScanEligibility
    $eligibilitySource = 'uia-module:Resolve-UiaPrivilegedScanEligibility'
} catch {
    Write-Host "  eligibility module not importable ($($_.Exception.Message)); using equivalent read-only probes"
    $eligibility = Get-FallbackPrivilegedEligibility
}
Write-Host "  eligibility ($eligibilitySource): eligible=$($eligibility.Eligible); absences=$($eligibility.Absences -join ',')"

# Skeleton classification over every section-11 step.
$residual = Get-HostContamination -ProbeOverrides $ProbeOverrides
$skeleton = New-InstallCertificationSkeletonResult -Preflight $preflight -SourceState $sourceState `
    -Binding $binding -BinaryRecords $binaryRecords -Eligibility $eligibility `
    -Residual $residual -CandidatePath $CandidatePath

# Evidence bundle (write-only directory; prior runs never read).
$bundle = Write-InstallCertificationEvidence -OutputRoot $OutputRoot -SourceState $sourceState `
    -BinaryRecords $binaryRecords -Binding $binding -Steps $skeleton.steps `
    -OverallStatus $skeleton.overallStatus -OverallReason $skeleton.overallReason `
    -Preflight $preflight -Residual $residual -Eligibility $eligibility `
    -EligibilitySource $eligibilitySource -RepoRoot $RepoRoot -CandidatePath $CandidatePath -Stamp $Stamp

$stopwatch.Stop()

foreach ($step in @($skeleton.steps)) {
    $color = switch ($step.status) {
        'PASS' { 'Green' }
        'FAIL' { 'Red' }
        default { 'Yellow' }
    }
    Write-Host "  [$($step.status)] $($step.id): $($step.reason)" -ForegroundColor $color
}
Write-Host "Install-certification skeleton finished: $($skeleton.overallStatus) ($($skeleton.overallReason))"
Write-Host "Evidence bundle: $($bundle.Directory)"
Write-Host "Duration: $([math]::Round($stopwatch.Elapsed.TotalSeconds, 1))s (started $($startUtc.ToString('o')))"

$result = [pscustomobject]@{
    overallStatus = $skeleton.overallStatus
    overallReason = $skeleton.overallReason
    hostState     = $skeleton.hostState
    steps         = @($skeleton.steps)
    evidence      = $bundle
}
Write-Output $result

$exitCode = switch ($skeleton.overallStatus) {
    'PASS' { 0 }
    'BLOCKED_EXTERNAL' { 2 }
    default { 1 }
}
exit $exitCode
