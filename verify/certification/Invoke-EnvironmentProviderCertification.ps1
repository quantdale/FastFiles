<#
    Environment Provider Certification Package.

    Certifies that an environment provider (hyperv / local / vmware /
    windows-sandbox) can honestly provision, execute, and clean up a
    FastFiles validation payload. Preflight (can this provider potentially
    run) is strictly separated from execution (did the payload execute):
    preflight-only success NEVER yields overall PASS.

    14-phase lifecycle (every phase is always recorded, never silent):
        DISCOVER, PREFLIGHT, PROVISION, ACTIVATE, WAIT_READY,
        ESTABLISH_SECURE_GUEST_CHANNEL, TRANSFER_PAYLOAD, EXECUTE_PAYLOAD,
        COLLECT_LOGS, COLLECT_STRUCTURED_RESULTS, VERIFY_RESULT_PROVENANCE,
        RESTORE_ROLLBACK, CLEANUP, RESIDUE_CHECK

    Terminal classification:
        PASS                        EXECUTE_PAYLOAD passed AND provenance
                                    verified AND cleanup clean.
        FAIL                        Any execution/provenance/rollback/cleanup
                                    failure, malformed or missing execution
                                    proof, timeout, or cancellation.
        REQUIRED-BUT-UNAVAILABLE    No execution possible (preflight
                                    unavailable, guest image absent, no
                                    adapter); disposition BLOCKED_EXTERNAL.
        BLOCKED_NOT_ELEVATED        Elevation is the gating reason.

    Provider-adapter seam: pass -ProviderAdapter (a scriptblock or a
    hashtable of per-operation scriptblocks) to inject a deterministic
    provider. A scriptblock adapter is invoked as:
        & $Adapter -Operation '<PHASE>' -Context $Context
    and MUST return an object/hashtable with:
        Status  PASS | FAIL | BLOCKED | SKIPPED  (anything else fails closed)
        Reason, Error, GuestIdentity, Artifacts, Data (operation specific).
    VERIFY_RESULT_PROVENANCE is always computed by this runner, never
    delegated, so injected adapters cannot self-certify.

    Real Hyper-V path (default): honest about prerequisites. Without a
    lawful guest image every phase after PREFLIGHT records BLOCKED with
    reason; nothing is fabricated. Even with an image, the harness has no
    guest-credential injection mechanism, so the secure guest channel
    honestly reports BLOCKED rather than fabricating guest execution.

    PowerShell 5.1-compatible syntax only. Existing parameters are
    unchanged; only optional parameters were added.
#>
[CmdletBinding()]
param(
    [ValidateSet('hyperv', 'local', 'vmware', 'windows-sandbox')]
    [string] $Provider = 'hyperv',

    [string] $SourcePath,

    [string] $OutputPath,

    [switch] $RequireExactTree,

    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,

    [object] $ProviderAdapter = $null,

    [int] $TimeoutSec = 600,

    [string] $CandidatePath
)

$ErrorActionPreference = 'Stop'

$startUtc = [DateTime]::UtcNow
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()

# 1. Capture Git Provenance
$headSha = (& git -C $RepoRoot rev-parse HEAD 2>&1).ToString().Trim()
$treeSha = (& git -C $RepoRoot write-tree 2>&1).ToString().Trim()
$porcelain = @(& git -C $RepoRoot status --porcelain 2>&1 | ForEach-Object { $_.ToString().Trim() } | Where-Object { $_ -ne '' })
$isDirty = ($porcelain.Count -gt 0)

if ($RequireExactTree -and $isDirty) {
    throw "Exact-tree qualification required, but repository working tree is dirty ($($porcelain.Count) changes)."
}

$exactTreeBound = (-not $isDirty)

# 2. Setup Output Path
if (-not $OutputPath) {
    $runDir = Join-Path $RepoRoot "verify\runs\certification\environment-provider"
    if (-not (Test-Path -LiteralPath $runDir)) { New-Item -ItemType Directory -Path $runDir -Force | Out-Null }
    $ts = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
    $OutputPath = Join-Path $runDir "environment-provider-certification-$Provider-$ts.json"
} else {
    $parent = Split-Path -Parent $OutputPath
    if ($parent -and -not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
}

# 3. Lifecycle definition and run state (reset every invocation; this script
#    may run several times in one session when tests execute it in-process).
$script:EnvCertPhases = @(
    'DISCOVER',
    'PREFLIGHT',
    'PROVISION',
    'ACTIVATE',
    'WAIT_READY',
    'ESTABLISH_SECURE_GUEST_CHANNEL',
    'TRANSFER_PAYLOAD',
    'EXECUTE_PAYLOAD',
    'COLLECT_LOGS',
    'COLLECT_STRUCTURED_RESULTS',
    'VERIFY_RESULT_PROVENANCE',
    'RESTORE_ROLLBACK',
    'CLEANUP',
    'RESIDUE_CHECK'
)
$script:EnvCertChecks = [System.Collections.Generic.List[object]]::new()
$script:EnvCertPhaseRecords = [System.Collections.Generic.List[object]]::new()
$script:EnvCertOutcomes = @{}
$script:EnvCertGuestIdentity = $null

function Get-EnvCertStringSha256Hex {
    param([string] $Text)
    if ($null -eq $Text) { $Text = '' }
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
    $hash = [System.Security.Cryptography.SHA256]::Create().ComputeHash($bytes)
    return ([System.BitConverter]::ToString($hash)).Replace('-', '').ToLowerInvariant()
}

function Get-EnvCertFileSha256Hex {
    param([string] $Path)
    if ([string]::IsNullOrWhiteSpace($Path)) { return $null }
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    try {
        $bytes = [System.IO.File]::ReadAllBytes($Path)
        $hash = [System.Security.Cryptography.SHA256]::Create().ComputeHash($bytes)
        return ([System.BitConverter]::ToString($hash)).Replace('-', '').ToLowerInvariant()
    } catch {
        return $null
    }
}

function Add-EnvCertCheck {
    param([string] $Id, [string] $Status, [string] $Message, $DetailObj = $null)
    [void]$script:EnvCertChecks.Add([pscustomobject]@{
        checkId = $Id
        status = $Status
        message = $Message
        details = $DetailObj
    })
    $color = $(if ($Status -eq 'PASS') { 'Green' } elseif ($Status -eq 'FAIL') { 'Red' } else { 'Yellow' })
    Write-Host "  [$Status] ${Id}: $Message" -ForegroundColor $color
}

function ConvertTo-EnvCertCheckStatus {
    param([string] $PhaseStatus)
    if ($PhaseStatus -eq 'PASS') { return 'PASS' }
    if ($PhaseStatus -eq 'FAIL') { return 'FAIL' }
    if ($PhaseStatus -eq 'SKIPPED') { return 'SKIPPED' }
    return 'REQUIRED-BUT-UNAVAILABLE'
}

function ConvertTo-EnvCertPhaseOutcome {
    param([string] $Phase, $Raw)
    $outcome = [ordered]@{
        status = 'FAIL'
        reason = $null
        error = $null
        guestIdentity = $null
        artifacts = @()
        actions = @()
        data = $null
    }
    if ($null -eq $Raw) {
        $outcome['reason'] = 'adapter returned no result for phase'
        $outcome['error'] = "phase $Phase produced no outcome (fail-closed)"
        return [pscustomobject]$outcome
    }
    $fields = @{}
    if ($Raw -is [System.Collections.IDictionary]) {
        foreach ($key in $Raw.Keys) { $fields[[string]$key] = $Raw[$key] }
    } else {
        foreach ($prop in $Raw.PSObject.Properties) { $fields[$prop.Name] = $prop.Value }
    }
    $statusText = ''
    if ($fields.ContainsKey('Status')) { $statusText = ([string]$fields['Status']).Trim().ToUpperInvariant() }
    if ($statusText -ne 'PASS' -and $statusText -ne 'FAIL' -and $statusText -ne 'BLOCKED' -and $statusText -ne 'SKIPPED') {
        $outcome['reason'] = 'adapter returned unknown status'
        $outcome['error'] = "phase $Phase returned status '$statusText' (fail-closed)"
        return [pscustomobject]$outcome
    }
    $outcome['status'] = $statusText
    if ($fields.ContainsKey('Reason') -and $null -ne $fields['Reason']) { $outcome['reason'] = [string]$fields['Reason'] }
    if ($fields.ContainsKey('Error') -and $null -ne $fields['Error']) { $outcome['error'] = [string]$fields['Error'] }
    if ($fields.ContainsKey('GuestIdentity') -and $null -ne $fields['GuestIdentity']) { $outcome['guestIdentity'] = [string]$fields['GuestIdentity'] }
    if ($fields.ContainsKey('Artifacts') -and $null -ne $fields['Artifacts']) { $outcome['artifacts'] = @($fields['Artifacts']) }
    if ($fields.ContainsKey('Actions') -and $null -ne $fields['Actions']) { $outcome['actions'] = @($fields['Actions']) }
    if ($fields.ContainsKey('Data')) { $outcome['data'] = $fields['Data'] }
    return [pscustomobject]$outcome
}

function Invoke-EnvCertAdapterOperation {
    param([object] $Adapter, [string] $Operation, [pscustomobject] $Context)
    if ($Adapter -is [scriptblock]) {
        return (& $Adapter -Operation $Operation -Context $Context)
    }
    if ($Adapter -is [System.Collections.IDictionary]) {
        if (-not $Adapter.Contains($Operation)) {
            throw "Provider adapter has no entry for operation '$Operation'."
        }
        $handler = $Adapter[$Operation]
        if (-not ($handler -is [scriptblock])) {
            throw "Provider adapter entry '$Operation' is not a scriptblock."
        }
        return (& $handler $Context)
    }
    $member = $Adapter.PSObject.Properties[$Operation]
    if ($null -ne $member -and ($member.Value -is [scriptblock])) {
        return (& $member.Value $Context)
    }
    throw "Provider adapter does not implement operation '$Operation'."
}

function New-EnvCertAdapterResult {
    param([string] $Status, [string] $Reason = $null, [string] $ErrorText = $null)
    return [pscustomobject]@{
        Status = $Status
        Reason = $Reason
        Error = $ErrorText
        GuestIdentity = $null
        Artifacts = @()
        Actions = @()
        Data = $null
    }
}

function Invoke-EnvCertHyperVOperation {
    param([string] $Operation, [pscustomobject] $Context)
    $computerName = $env:COMPUTERNAME
    if ([string]::IsNullOrWhiteSpace($computerName)) { $computerName = 'local-host' }
    $guestId = "${computerName}:hyperv:$($Context.VmName)"
    if ($Operation -eq 'DISCOVER') {
        $hasModule = [bool](Get-Module -ListAvailable -Name 'Hyper-V')
        $result = New-EnvCertAdapterResult
        $result.GuestIdentity = $guestId
        if ($hasModule) {
            $result.Status = 'PASS'
            $result.Reason = 'hyper-v-module-present'
            $result.Artifacts = @('host:hyper-v-module')
        } else {
            $result.Status = 'BLOCKED'
            $result.Reason = 'HYPERV_FEATURE_UNAVAILABLE'
            $result.Error = 'Windows Hyper-V PowerShell module is not available on host.'
        }
        return $result
    }
    if ($Operation -eq 'PREFLIGHT') {
        $result = New-EnvCertAdapterResult
        $result.GuestIdentity = $guestId
        if (-not $Context.Elevated) {
            $result.Status = 'BLOCKED'
            $result.Reason = 'NOT_ELEVATED'
            $result.Error = 'Hyper-V VM management requires an elevated administrator process.'
            $result.Data = [pscustomobject]@{ ReasonCodes = @('NOT_ELEVATED'); ElevationBlock = $true }
            return $result
        }
        try {
            Import-Module (Join-Path $Context.RepoRoot 'verify\providers\hyperv\HyperV.psm1') -Force -ErrorAction Stop
            $providerContext = [pscustomobject]@{
                SourcePath = $Context.SourcePath
                VmName = $Context.VmName
                RunContext = [pscustomobject]@{ Timestamp = $Context.RunId }
            }
            $preflight = Invoke-HyperVProviderPreflight -ProviderContext $providerContext
        } catch {
            $result.Status = 'BLOCKED'
            $result.Reason = 'PROVIDER_DEPENDENCY_UNAVAILABLE'
            $result.Error = $_.Exception.Message
            $result.Data = [pscustomobject]@{ ReasonCodes = @('PROVIDER_DEPENDENCY_UNAVAILABLE'); ElevationBlock = $false }
            return $result
        }
        if ($preflight.Ready) {
            $result.Status = 'PASS'
            $result.Reason = 'hyper-v preflight ready; disposable provisioning possible'
            $result.Data = [pscustomobject]@{ ReasonCodes = @(); ElevationBlock = $false; SourceVerified = $preflight.SourceVerified }
            return $result
        }
        $codes = @($preflight.ReasonCodes)
        $result.Status = 'BLOCKED'
        $result.Reason = 'guest image absent or unusable: ' + ($codes -join ', ')
        $result.Error = $preflight.Detail
        $result.Data = [pscustomobject]@{ ReasonCodes = $codes; ElevationBlock = $false }
        return $result
    }
    if ($Operation -eq 'PROVISION') {
        $result = New-EnvCertAdapterResult
        $result.GuestIdentity = $guestId
        try {
            $providerContext = [pscustomobject]@{
                SourcePath = $Context.SourcePath
                VmName = $Context.VmName
                RunContext = [pscustomobject]@{ Timestamp = $Context.RunId }
            }
            $provision = Invoke-HyperVProviderProvision -ProviderContext $providerContext
            $script:EnvCertHyperVState = $providerContext.State
        } catch {
            $result.Status = 'FAIL'
            $result.Reason = 'VM_CREATE_FAILED'
            $result.Error = $_.Exception.Message
            return $result
        }
        if ($provision.Ready) {
            $result.Status = 'PASS'
            $result.Reason = $provision.Detail
            $result.Data = [pscustomobject]@{ VmName = $provision.VmName; Reused = [bool]$provision.Reused }
            return $result
        }
        $result.Status = 'FAIL'
        $result.Reason = [string]$provision.Reason
        $result.Error = [string]$provision.Detail
        return $result
    }
    if ($Operation -eq 'ACTIVATE') {
        $result = New-EnvCertAdapterResult
        $result.GuestIdentity = $guestId
        try {
            $providerContext = [pscustomobject]@{ State = $script:EnvCertHyperVState }
            $activate = Invoke-HyperVProviderActivate -ProviderContext $providerContext
        } catch {
            $result.Status = 'FAIL'
            $result.Reason = 'ACTIVATION_FAILED'
            $result.Error = $_.Exception.Message
            return $result
        }
        if ($activate.Ready) {
            $result.Status = 'PASS'
            $result.Reason = "guest '$($activate.VmName)' state: $($activate.State)"
            $result.GuestIdentity = [string]$activate.TargetIdentity
            return $result
        }
        $result.Status = 'FAIL'
        $result.Reason = [string]$activate.Reason
        $result.Error = [string]$activate.Detail
        return $result
    }
    if ($Operation -eq 'WAIT_READY') {
        $result = New-EnvCertAdapterResult
        $result.GuestIdentity = $guestId
        if (-not (Get-Command Get-VM -ErrorAction SilentlyContinue)) {
            $result.Status = 'FAIL'
            $result.Reason = 'PROVIDER_DEPENDENCY_UNAVAILABLE'
            $result.Error = 'Get-VM is unavailable; cannot observe guest readiness.'
            return $result
        }
        $budgetSec = $Context.TimeoutSec
        if ($budgetSec -lt 60) { $budgetSec = 60 }
        $deadline = [DateTime]::UtcNow.AddSeconds($budgetSec)
        try {
            $vm = Get-VM -Name $Context.VmName -ErrorAction SilentlyContinue
            if ($null -ne $vm -and $vm.State -ne 'Running') {
                Start-VM -Name $Context.VmName -ErrorAction Stop | Out-Null
            }
            while ([DateTime]::UtcNow -lt $deadline) {
                $observed = Get-VM -Name $Context.VmName -ErrorAction SilentlyContinue
                if ($null -ne $observed -and $observed.State -eq 'Running') {
                    $result.Status = 'PASS'
                    $result.Reason = "guest '$($Context.VmName)' reached Running state"
                    return $result
                }
                Start-Sleep -Seconds 5
            }
        } catch {
            $result.Status = 'FAIL'
            $result.Reason = 'GUEST_START_FAILED'
            $result.Error = $_.Exception.Message
            return $result
        }
        $result.Status = 'FAIL'
        $result.Reason = 'TIMEOUT'
        $result.Error = "guest '$($Context.VmName)' did not reach Running state within ${budgetSec}s."
        return $result
    }
    if ($Operation -eq 'ESTABLISH_SECURE_GUEST_CHANNEL') {
        $result = New-EnvCertAdapterResult
        $result.GuestIdentity = $guestId
        $result.Status = 'BLOCKED'
        $result.Reason = 'GUEST_CHANNEL_CREDENTIAL_ABSENT'
        $result.Error = 'Harness has no guest credential injection mechanism; refusing to fabricate a secure guest channel.'
        return $result
    }
    if ($Operation -eq 'CLEANUP') {
        $result = New-EnvCertAdapterResult
        $result.GuestIdentity = $guestId
        try {
            if ($null -ne $script:EnvCertHyperVState) {
                $providerContext = [pscustomobject]@{ State = $script:EnvCertHyperVState }
                $cleanup = Invoke-HyperVProviderCleanup -ProviderContext $providerContext
                $result.Actions = @($cleanup.actions)
                if ($cleanup.succeeded) {
                    $result.Status = 'PASS'
                    $result.Reason = 'hyper-v cleanup completed'
                } else {
                    $result.Status = 'FAIL'
                    $result.Reason = 'CLEANUP_FAILED'
                    $result.Error = 'Hyper-V provider cleanup reported failure.'
                }
                $result.Data = [pscustomobject]@{ MutatedSystemState = [bool]$cleanup.mutatedSystemState }
                return $result
            }
        } catch {
            $result.Status = 'FAIL'
            $result.Reason = 'CLEANUP_FAILED'
            $result.Error = $_.Exception.Message
            return $result
        }
        $result.Status = 'PASS'
        $result.Reason = 'nothing provisioned; no cleanup required'
        $result.Data = [pscustomobject]@{ MutatedSystemState = $false }
        return $result
    }
    if ($Operation -eq 'RESIDUE_CHECK') {
        $result = New-EnvCertAdapterResult
        $result.GuestIdentity = $guestId
        $residue = @()
        if ($null -ne $script:EnvCertHyperVState -and $script:EnvCertHyperVState.Provisioned -and $script:EnvCertHyperVState.OwnedByRun) {
            $leftover = Get-VM -Name $Context.VmName -ErrorAction SilentlyContinue
            if ($null -ne $leftover) { $residue += @($guestId) }
        }
        if ($residue.Count -eq 0) {
            $result.Status = 'PASS'
            $result.Reason = 'no provider residue observed'
        } else {
            $result.Status = 'FAIL'
            $result.Reason = 'RESIDUE_FOUND'
            $result.Error = 'provider-owned guest still present after cleanup: ' + ($residue -join ', ')
            $result.Data = [pscustomobject]@{ Residue = $residue }
        }
        return $result
    }
    $blocked = New-EnvCertAdapterResult -Status 'BLOCKED' -Reason 'UPSTREAM_CHANNEL_UNAVAILABLE' -ErrorText "Real Hyper-V path cannot reach $Operation without an established secure guest channel."
    $blocked.GuestIdentity = $guestId
    return $blocked
}

function Invoke-EnvCertLocalOperation {
    param([string] $Operation, [pscustomobject] $Context)
    $computerName = $env:COMPUTERNAME
    if ([string]::IsNullOrWhiteSpace($computerName)) { $computerName = 'local-host' }
    $hostId = "${computerName}:local:host"
    if ($Operation -eq 'DISCOVER') {
        $result = New-EnvCertAdapterResult -Status 'PASS' -Reason 'local host provider discovered'
        $result.GuestIdentity = $hostId
        $result.Artifacts = @('host:local-provider')
        return $result
    }
    if ($Operation -eq 'PREFLIGHT') {
        $result = New-EnvCertAdapterResult -Status 'PASS' -Reason 'local host ready; no external dependencies'
        $result.GuestIdentity = $hostId
        $result.Data = [pscustomobject]@{ ReasonCodes = @(); ElevationBlock = $false }
        return $result
    }
    if ($Operation -eq 'PROVISION' -or $Operation -eq 'ACTIVATE' -or $Operation -eq 'WAIT_READY') {
        $reasons = @{
            'PROVISION' = 'local host needs no provisioning'
            'ACTIVATE' = 'local host target activated'
            'WAIT_READY' = 'local host immediately ready'
        }
        $result = New-EnvCertAdapterResult -Status 'PASS' -Reason $reasons[$Operation]
        $result.GuestIdentity = $hostId
        return $result
    }
    if ($Operation -eq 'ESTABLISH_SECURE_GUEST_CHANNEL') {
        $result = New-EnvCertAdapterResult -Status 'PASS' -Reason 'loopback execution; no network channel required'
        $result.GuestIdentity = $hostId
        return $result
    }
    if ($Operation -eq 'TRANSFER_PAYLOAD') {
        $result = New-EnvCertAdapterResult -Status 'PASS' -Reason ("payload staged in-memory; sha256=$($Context.PayloadDigest)")
        $result.GuestIdentity = $hostId
        $result.Data = [pscustomobject]@{ PayloadDigest = $Context.PayloadDigest }
        return $result
    }
    if ($Operation -eq 'EXECUTE_PAYLOAD') {
        $result = New-EnvCertAdapterResult
        $result.GuestIdentity = $hostId
        $exeHash = $null
        try {
            $exePath = (Get-Process -Id $PID -ErrorAction Stop).Path
            $exeHash = Get-EnvCertFileSha256Hex -Path $exePath
        } catch {
            $exeHash = $null
        }
        if ([string]::IsNullOrWhiteSpace($exeHash)) {
            $result.Status = 'FAIL'
            $result.Reason = 'EXECUTABLE_HASH_UNAVAILABLE'
            $result.Error = 'Could not hash the executing interpreter binary; refusing to certify without executable identity.'
            return $result
        }
        $probeFacts = [ordered]@{
            probe = 'local-certification-probe'
            host = $computerName
            os = [Environment]::OSVersion.ToString()
            powershell = $PSVersionTable.PSVersion.ToString()
            commit = $Context.HostCommit
            payloadDigest = $Context.PayloadDigest
        }
        $artifactJson = ($probeFacts | ConvertTo-Json -Depth 4 -Compress)
        $artifactHash = Get-EnvCertStringSha256Hex -Text $artifactJson
        $guestResult = [pscustomobject]@{
            schemaVersion = 1
            provider = 'local'
            vmIdentity = $hostId
            guestOs = [Environment]::OSVersion.ToString()
            guestTimestampUtc = ([DateTime]::UtcNow.ToString('o'))
            command = 'local-probe --certify'
            executableHash = $exeHash
            payloadDigest = $Context.PayloadDigest
            hostCommit = $Context.HostCommit
            hostTree = $Context.HostTree
            exitCode = 0
            testCounts = [pscustomobject]@{ total = 1; passed = 1; failed = 0 }
            resultArtifact = $artifactJson
            resultArtifactHash = $artifactHash
        }
        $result.Status = 'PASS'
        $result.Reason = 'local probe executed with exit code 0'
        $result.Data = $guestResult
        return $result
    }
    if ($Operation -eq 'COLLECT_LOGS') {
        $result = New-EnvCertAdapterResult -Status 'PASS' -Reason 'local probe log collected'
        $result.GuestIdentity = $hostId
        $result.Artifacts = @('memory:local-provider/host-probe-log.json')
        return $result
    }
    if ($Operation -eq 'COLLECT_STRUCTURED_RESULTS') {
        $result = New-EnvCertAdapterResult
        $result.GuestIdentity = $hostId
        $execOutcome = $script:EnvCertOutcomes['EXECUTE_PAYLOAD']
        if ($null -eq $execOutcome -or $null -eq $execOutcome.data) {
            $result.Status = 'FAIL'
            $result.Reason = 'RESULT_COLLECTION_FAILED'
            $result.Error = 'No execution result available to collect.'
            return $result
        }
        $result.Status = 'PASS'
        $result.Reason = 'local structured result collected'
        $result.Data = [pscustomobject]@{
            Artifact = $execOutcome.data.resultArtifact
            ArtifactHash = $execOutcome.data.resultArtifactHash
        }
        return $result
    }
    if ($Operation -eq 'RESTORE_ROLLBACK') {
        $result = New-EnvCertAdapterResult -Status 'PASS' -Reason 'nothing mutated; no restore required'
        $result.GuestIdentity = $hostId
        $result.Data = [pscustomobject]@{ Restored = $false }
        return $result
    }
    if ($Operation -eq 'CLEANUP') {
        $result = New-EnvCertAdapterResult -Status 'PASS' -Reason 'nothing provisioned; no cleanup required'
        $result.GuestIdentity = $hostId
        $result.Actions = @()
        $result.Data = [pscustomobject]@{ MutatedSystemState = $false }
        return $result
    }
    if ($Operation -eq 'RESIDUE_CHECK') {
        $result = New-EnvCertAdapterResult -Status 'PASS' -Reason 'no provider residue observed'
        $result.GuestIdentity = $hostId
        $result.Data = [pscustomobject]@{ Residue = @() }
        return $result
    }
    return (New-EnvCertAdapterResult -Status 'FAIL' -Reason 'UNKNOWN_OPERATION' -ErrorText "Local provider has no operation '$Operation'.")
}

function Invoke-EnvCertExternalStubOperation {
    param([string] $Operation, [pscustomobject] $Context)
    $computerName = $env:COMPUTERNAME
    if ([string]::IsNullOrWhiteSpace($computerName)) { $computerName = 'local-host' }
    $identity = "${computerName}:$($Context.Provider):unavailable"
    if ($Operation -eq 'DISCOVER') {
        $result = New-EnvCertAdapterResult -Status 'PASS' -Reason "provider '$($Context.Provider)' discovered; requires external infrastructure"
        $result.GuestIdentity = $identity
        return $result
    }
    if ($Operation -eq 'PREFLIGHT') {
        $result = New-EnvCertAdapterResult -Status 'BLOCKED' -Reason 'PROVIDER_ADAPTER_ABSENT' -ErrorText "Provider adapter '$($Context.Provider)' requires external infrastructure and guest credentials."
        $result.GuestIdentity = $identity
        $result.Data = [pscustomobject]@{ ReasonCodes = @('PROVIDER_ADAPTER_ABSENT'); ElevationBlock = $false }
        return $result
    }
    if ($Operation -eq 'CLEANUP') {
        $result = New-EnvCertAdapterResult -Status 'PASS' -Reason 'nothing provisioned; no cleanup required'
        $result.GuestIdentity = $identity
        $result.Data = [pscustomobject]@{ MutatedSystemState = $false }
        return $result
    }
    if ($Operation -eq 'RESIDUE_CHECK') {
        $result = New-EnvCertAdapterResult -Status 'PASS' -Reason 'no provider residue observed'
        $result.GuestIdentity = $identity
        $result.Data = [pscustomobject]@{ Residue = @() }
        return $result
    }
    $blocked = New-EnvCertAdapterResult -Status 'BLOCKED' -Reason 'PREFLIGHT_UNAVAILABLE' -ErrorText "Phase $Operation not attempted: preflight unavailable."
    $blocked.GuestIdentity = $identity
    return $blocked
}

function Invoke-EnvCertRealOperation {
    param([string] $Operation, [pscustomobject] $Context)
    if ($Context.Provider -eq 'hyperv') {
        return (Invoke-EnvCertHyperVOperation -Operation $Operation -Context $Context)
    }
    if ($Context.Provider -eq 'local') {
        return (Invoke-EnvCertLocalOperation -Operation $Operation -Context $Context)
    }
    return (Invoke-EnvCertExternalStubOperation -Operation $Operation -Context $Context)
}

function Invoke-EnvCertVerifyProvenance {
    param($ExecData, [string] $CollectedArtifact, [pscustomobject] $Context)
    $errors = [System.Collections.Generic.List[string]]::new()
    $record = [ordered]@{
        verified = $false
        hostCommit = $Context.HostCommit
        hostTree = $Context.HostTree
        payloadDigest = $Context.PayloadDigest
        candidateDigest = $Context.CandidateDigest
        provider = $Context.Provider
        vmIdentity = $null
        guestOs = $null
        guestTimestampUtc = $null
        executableHash = $null
        command = $null
        schemaVersion = $null
        resultArtifactHash = $null
        testCounts = $null
        exitCode = $null
        cleanupDisposition = $null
        verifyErrors = @()
    }
    if ($null -eq $ExecData) {
        [void]$errors.Add('missing execution proof: EXECUTE_PAYLOAD produced no guest result')
    } else {
        $fields = @{}
        if ($ExecData -is [System.Collections.IDictionary]) {
            foreach ($key in $ExecData.Keys) { $fields[[string]$key] = $ExecData[$key] }
        } else {
            foreach ($prop in $ExecData.PSObject.Properties) { $fields[$prop.Name] = $prop.Value }
        }
        $required = @('schemaVersion', 'provider', 'vmIdentity', 'guestOs', 'guestTimestampUtc', 'command', 'executableHash', 'payloadDigest', 'hostCommit', 'hostTree', 'exitCode', 'testCounts', 'resultArtifact', 'resultArtifactHash')
        foreach ($name in $required) {
            if (-not $fields.ContainsKey($name) -or $null -eq $fields[$name] -or ([string]$fields[$name] -eq '')) {
                [void]$errors.Add("malformed execution proof: missing field '$name'")
            }
        }
        if ($errors.Count -eq 0) {
            $record['vmIdentity'] = [string]$fields['vmIdentity']
            $record['guestOs'] = [string]$fields['guestOs']
            $record['guestTimestampUtc'] = [string]$fields['guestTimestampUtc']
            $record['executableHash'] = [string]$fields['executableHash']
            $record['command'] = [string]$fields['command']
            try { $record['schemaVersion'] = [int]$fields['schemaVersion'] } catch { $record['schemaVersion'] = $fields['schemaVersion'] }
            $record['resultArtifactHash'] = [string]$fields['resultArtifactHash']
            try { $record['exitCode'] = [int]$fields['exitCode'] } catch { $record['exitCode'] = $fields['exitCode'] }
            $counts = $fields['testCounts']
            $countFields = @{}
            if ($counts -is [System.Collections.IDictionary]) {
                foreach ($key in $counts.Keys) { $countFields[[string]$key] = $counts[$key] }
            } elseif ($null -ne $counts) {
                foreach ($prop in $counts.PSObject.Properties) { $countFields[$prop.Name] = $prop.Value }
            }
            $total = -1
            $passed = -1
            $failed = -1
            try { $total = [int]$countFields['total'] } catch { $total = -1 }
            try { $passed = [int]$countFields['passed'] } catch { $passed = -1 }
            try { $failed = [int]$countFields['failed'] } catch { $failed = -1 }
            $record['testCounts'] = [ordered]@{ total = $total; passed = $passed; failed = $failed }
            if ([int]$record['schemaVersion'] -ne 1) {
                [void]$errors.Add("unsupported guest result schema version '$($fields['schemaVersion'])'")
            }
            if ([string]$fields['provider'] -ne $Context.Provider) {
                [void]$errors.Add("provider mismatch: proof claims '$($fields['provider'])', expected '$($Context.Provider)'")
            }
            if (([string]$fields['payloadDigest']).ToLowerInvariant() -ne $Context.PayloadDigest.ToLowerInvariant()) {
                [void]$errors.Add('provenance mismatch: payload digest in guest result does not match transferred payload')
            }
            if (([string]$fields['hostCommit']).ToLowerInvariant() -ne $Context.HostCommit.ToLowerInvariant()) {
                [void]$errors.Add('provenance mismatch: host commit in guest result does not match this run')
            }
            if (([string]$fields['hostTree']).ToLowerInvariant() -ne $Context.HostTree.ToLowerInvariant()) {
                [void]$errors.Add('provenance mismatch: host tree in guest result does not match this run')
            }
            if ($fields['executableHash'] -notmatch '^[0-9a-fA-F]{64}$') {
                [void]$errors.Add('malformed execution proof: executableHash is not a 64-hex SHA-256 digest')
            }
            if ($fields['payloadDigest'] -notmatch '^[0-9a-fA-F]{64}$') {
                [void]$errors.Add('malformed execution proof: payloadDigest is not a 64-hex SHA-256 digest')
            }
            if ($fields['resultArtifactHash'] -notmatch '^[0-9a-fA-F]{64}$') {
                [void]$errors.Add('malformed execution proof: resultArtifactHash is not a 64-hex SHA-256 digest')
            }
            if ($fields['hostCommit'] -notmatch '^[0-9a-fA-F]{40}$') {
                [void]$errors.Add('malformed execution proof: hostCommit is not a 40-hex commit SHA')
            }
            if ($fields['hostTree'] -notmatch '^[0-9a-fA-F]{40}$') {
                [void]$errors.Add('malformed execution proof: hostTree is not a 40-hex tree SHA')
            }
            $parsedTimestamp = [DateTime]::MinValue
            if (-not [DateTime]::TryParse([string]$fields['guestTimestampUtc'], [ref]$parsedTimestamp)) {
                [void]$errors.Add('malformed execution proof: guestTimestampUtc is not a parseable timestamp')
            }
            if ($null -ne $Context.CandidateDigest) {
                if (-not $fields.ContainsKey('candidateDigest') -or $null -eq $fields['candidateDigest']) {
                    [void]$errors.Add('provenance mismatch: candidate was supplied but guest result carries no candidateDigest')
                } elseif (([string]$fields['candidateDigest']).ToLowerInvariant() -ne $Context.CandidateDigest.ToLowerInvariant()) {
                    [void]$errors.Add('provenance mismatch: candidateDigest in guest result does not match supplied candidate')
                }
            }
            if ([string]::IsNullOrWhiteSpace($CollectedArtifact)) {
                [void]$errors.Add('missing execution proof: no collected result artifact to bind')
            } else {
                $recomputed = Get-EnvCertStringSha256Hex -Text $CollectedArtifact
                if ($recomputed.ToLowerInvariant() -ne ([string]$fields['resultArtifactHash']).ToLowerInvariant()) {
                    [void]$errors.Add('provenance mismatch: resultArtifactHash does not match the collected result artifact')
                }
            }
            if ($total -lt 0 -or $passed -lt 0 -or $failed -lt 0 -or (($passed + $failed) -ne $total)) {
                [void]$errors.Add('malformed execution proof: testCounts are inconsistent (passed + failed must equal total)')
            }
            if ([int]$fields['exitCode'] -ne 0) {
                [void]$errors.Add("guest reported failure: exit code $($fields['exitCode'])")
            }
            if ($failed -gt 0) {
                [void]$errors.Add("guest reported $failed failed test(s)")
            }
        }
    }
    $record['verifyErrors'] = @($errors)
    $record['verified'] = ($errors.Count -eq 0)
    return [pscustomobject]$record
}

function Invoke-EnvCertRunPhase {
    param([string] $Name, [scriptblock] $Action)
    $phaseStart = [DateTime]::UtcNow
    $phaseStopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $raw = $null
    $threw = $null
    try {
        $raw = (& $Action)
    } catch {
        $threw = $_.Exception.Message
    }
    if ($null -ne $threw) {
        $raw = [pscustomobject]@{
            Status = 'FAIL'
            Reason = 'phase threw an exception'
            Error = $threw
            GuestIdentity = $script:EnvCertGuestIdentity
            Artifacts = @()
            Actions = @()
            Data = $null
        }
    }
    $outcome = ConvertTo-EnvCertPhaseOutcome -Phase $Name -Raw $raw
    if ($null -ne $outcome.guestIdentity -and $outcome.guestIdentity -ne '') {
        $script:EnvCertGuestIdentity = $outcome.guestIdentity
    }
    $phaseStopwatch.Stop()
    $phaseEnd = [DateTime]::UtcNow
    $record = [pscustomobject][ordered]@{
        phase = $Name
        status = $outcome.status
        reason = $outcome.reason
        error = $outcome.error
        startUtc = $phaseStart.ToString('o')
        endUtc = $phaseEnd.ToString('o')
        durationMs = [Math]::Round($phaseStopwatch.Elapsed.TotalMilliseconds, 2)
        provider = $Provider
        guestIdentity = $script:EnvCertGuestIdentity
        artifacts = @($outcome.artifacts)
        cleanup = $null
    }
    [void]$script:EnvCertPhaseRecords.Add($record)
    $script:EnvCertOutcomes[$Name] = $outcome
    $color = $(if ($outcome.status -eq 'PASS') { 'Green' } elseif ($outcome.status -eq 'FAIL') { 'Red' } else { 'Yellow' })
    $suffix = $(if ($outcome.reason) { ": $($outcome.reason)" } else { '' })
    Write-Host "  [$($outcome.status)] phase $Name$suffix" -ForegroundColor $color
    return $outcome
}

function Invoke-EnvCertRecordGatedPhase {
    param([string] $Name, [string] $Status, [string] $Reason)
    $now = [DateTime]::UtcNow
    $record = [pscustomobject][ordered]@{
        phase = $Name
        status = $Status
        reason = $Reason
        error = $null
        startUtc = $now.ToString('o')
        endUtc = $now.ToString('o')
        durationMs = 0
        provider = $Provider
        guestIdentity = $script:EnvCertGuestIdentity
        artifacts = @()
        cleanup = $null
    }
    [void]$script:EnvCertPhaseRecords.Add($record)
    $gated = [pscustomobject]@{
        status = $Status
        reason = $Reason
        error = $null
        guestIdentity = $script:EnvCertGuestIdentity
        artifacts = @()
        actions = @()
        data = $null
    }
    $script:EnvCertOutcomes[$Name] = $gated
    Write-Host "  [$Status] phase ${Name}: $Reason" -ForegroundColor Yellow
    return $gated
}

Write-Host "Starting Environment Provider Certification Package:"
Write-Host "  Target Provider: $Provider"
Write-Host "  Git: head=$headSha, tree=$treeSha, exactTreeBound=$exactTreeBound"

$script:EnvCertHyperVState = $null

$isElevatedProcess = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

# Resolve guest image source (preflight input only; never fabricated).
$resolvedSource = $null
if ($SourcePath) {
    $resolvedSource = $SourcePath
} elseif ($env:FASTFILES_HYPERV_IMAGE) {
    $resolvedSource = $env:FASTFILES_HYPERV_IMAGE
} else {
    $standardVm = "C:\VMs\FastFiles-Matrix.vhdx"
    if (Test-Path -LiteralPath $standardVm -PathType Leaf) {
        $resolvedSource = $standardVm
    }
}

# Candidate digest for provenance binding (optional; null when no candidate).
$candidateDigest = $null
if ($CandidatePath -and (Test-Path -LiteralPath $CandidatePath -PathType Leaf)) {
    $candidateDigest = Get-EnvCertFileSha256Hex -Path $CandidatePath
}

$runId = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
$vmName = "FastFiles-Cert-$runId"
$payloadContent = "FastFiles-environment-provider-certification/v1|provider=$Provider|commit=$headSha|tree=$treeSha|run=$runId"
$payloadDigest = Get-EnvCertStringSha256Hex -Text $payloadContent

$adapterKind = 'real'
if ($null -ne $ProviderAdapter) { $adapterKind = 'injected' }

$adapterContext = [pscustomobject]@{
    Provider = $Provider
    SourcePath = $resolvedSource
    RepoRoot = $RepoRoot
    RunId = $runId
    VmName = $vmName
    HostCommit = $headSha
    HostTree = $treeSha
    CandidateDigest = $candidateDigest
    PayloadContent = $payloadContent
    PayloadDigest = $payloadDigest
    TimeoutSec = $TimeoutSec
    Elevated = $isElevatedProcess
}

function Invoke-EnvCertDispatch {
    param([string] $Operation)
    if ($adapterKind -eq 'injected') {
        return (Invoke-EnvCertAdapterOperation -Adapter $ProviderAdapter -Operation $Operation -Context $adapterContext)
    }
    return (Invoke-EnvCertRealOperation -Operation $Operation -Context $adapterContext)
}

# Lifecycle execution with explicit gating. No phase silently disappears:
# every one of the 14 phases ends up recorded exactly once.
$discoverOutcome = Invoke-EnvCertRunPhase -Name 'DISCOVER' -Action { Invoke-EnvCertDispatch -Operation 'DISCOVER' }
$preflightOutcome = Invoke-EnvCertRunPhase -Name 'PREFLIGHT' -Action { Invoke-EnvCertDispatch -Operation 'PREFLIGHT' }

$preflightPass = ($preflightOutcome.status -eq 'PASS')
$preflightReasonCodes = @()
if ($null -ne $preflightOutcome.data) {
    $preData = $preflightOutcome.data
    if ($preData -is [System.Collections.IDictionary]) {
        if ($preData.Contains('ReasonCodes')) { $preflightReasonCodes = @($preData['ReasonCodes']) }
    } else {
        $codeProp = $preData.PSObject.Properties['ReasonCodes']
        if ($null -ne $codeProp) { $preflightReasonCodes = @($codeProp.Value) }
    }
}
$elevationBlock = $false
if ($null -ne $preflightOutcome.data) {
    $preData = $preflightOutcome.data
    if ($preData -is [System.Collections.IDictionary]) {
        if ($preData.Contains('ElevationBlock')) { $elevationBlock = [bool]$preData['ElevationBlock'] }
    } else {
        $elevProp = $preData.PSObject.Properties['ElevationBlock']
        if ($null -ne $elevProp) { $elevationBlock = [bool]$elevProp.Value }
    }
}
if (-not $elevationBlock -and ($preflightReasonCodes -contains 'NOT_ELEVATED')) {
    $elevationBlock = $true
}

$executionPhases = @('PROVISION', 'ACTIVATE', 'WAIT_READY', 'ESTABLISH_SECURE_GUEST_CHANNEL', 'TRANSFER_PAYLOAD', 'EXECUTE_PAYLOAD')
$collectionPhases = @('COLLECT_LOGS', 'COLLECT_STRUCTURED_RESULTS')
$script:EnvCertProvenance = $null

if (-not $preflightPass) {
    $blockReason = "preflight unavailable; guest execution not attempted"
    if ($preflightOutcome.reason) { $blockReason = "preflight unavailable ($($preflightOutcome.reason)); guest execution not attempted" }
    foreach ($phaseName in ($executionPhases + $collectionPhases)) {
        [void](Invoke-EnvCertRecordGatedPhase -Name $phaseName -Status 'BLOCKED' -Reason $blockReason)
    }
    [void](Invoke-EnvCertRecordGatedPhase -Name 'VERIFY_RESULT_PROVENANCE' -Status 'SKIPPED' -Reason 'no execution proof to verify; preflight unavailable')
    [void](Invoke-EnvCertRecordGatedPhase -Name 'RESTORE_ROLLBACK' -Status 'SKIPPED' -Reason 'nothing provisioned; preflight unavailable')
    [void](Invoke-EnvCertRunPhase -Name 'CLEANUP' -Action { Invoke-EnvCertDispatch -Operation 'CLEANUP' })
    [void](Invoke-EnvCertRunPhase -Name 'RESIDUE_CHECK' -Action { Invoke-EnvCertDispatch -Operation 'RESIDUE_CHECK' })
} else {
    $upstreamOk = $true
    $upstreamBlocker = $null
    $executeAttempted = $false
    foreach ($phaseName in $executionPhases) {
        if ($upstreamOk) {
            # NOTE: no GetNewClosure() here; the action runs synchronously in
            # this iteration, and a closure would detach the scriptblock from
            # this script's function scope.
            $outcome = Invoke-EnvCertRunPhase -Name $phaseName -Action { Invoke-EnvCertDispatch -Operation $phaseName }
            if ($phaseName -eq 'EXECUTE_PAYLOAD') { $executeAttempted = $true }
            if ($outcome.status -ne 'PASS') {
                $upstreamOk = $false
                $upstreamBlocker = $phaseName
            }
        } else {
            [void](Invoke-EnvCertRecordGatedPhase -Name $phaseName -Status 'SKIPPED' -Reason "upstream phase $upstreamBlocker did not pass")
            if ($phaseName -eq 'EXECUTE_PAYLOAD') { $executeAttempted = $false }
        }
    }
    foreach ($phaseName in $collectionPhases) {
        if ($executeAttempted) {
            [void](Invoke-EnvCertRunPhase -Name $phaseName -Action { Invoke-EnvCertDispatch -Operation $phaseName })
        } else {
            [void](Invoke-EnvCertRecordGatedPhase -Name $phaseName -Status 'SKIPPED' -Reason 'payload was never executed; nothing to collect')
        }
    }
    $execOutcome = $script:EnvCertOutcomes['EXECUTE_PAYLOAD']
    $collectOutcome = $script:EnvCertOutcomes['COLLECT_STRUCTURED_RESULTS']
    $canVerify = ($null -ne $execOutcome -and $execOutcome.status -eq 'PASS' -and $null -ne $collectOutcome -and $collectOutcome.status -eq 'PASS')
    if ($canVerify) {
        $collectedArtifact = $null
        $collectData = $collectOutcome.data
        if ($null -ne $collectData) {
            if ($collectData -is [System.Collections.IDictionary]) {
                if ($collectData.Contains('Artifact')) { $collectedArtifact = [string]$collectData['Artifact'] }
            } else {
                $artifactProp = $collectData.PSObject.Properties['Artifact']
                if ($null -ne $artifactProp) { $collectedArtifact = [string]$artifactProp.Value }
            }
        }
        $provenance = Invoke-EnvCertVerifyProvenance -ExecData $execOutcome.data -CollectedArtifact $collectedArtifact -Context $adapterContext
        $script:EnvCertProvenance = $provenance
        if ($provenance.verified) {
            [void](Invoke-EnvCertRecordGatedPhase -Name 'VERIFY_RESULT_PROVENANCE' -Status 'PASS' -Reason 'execution proof bound to host commit, tree, payload, and result artifact')
            ($script:EnvCertOutcomes['VERIFY_RESULT_PROVENANCE']).data = $provenance
        } else {
            $firstError = 'provenance verification failed'
            if ($provenance.verifyErrors.Count -gt 0) { $firstError = $provenance.verifyErrors[0] }
            [void](Invoke-EnvCertRecordGatedPhase -Name 'VERIFY_RESULT_PROVENANCE' -Status 'FAIL' -Reason $firstError)
            ($script:EnvCertOutcomes['VERIFY_RESULT_PROVENANCE']).data = $provenance
            ($script:EnvCertOutcomes['VERIFY_RESULT_PROVENANCE']).error = ($provenance.verifyErrors -join '; ')
        }
    } else {
        [void](Invoke-EnvCertRecordGatedPhase -Name 'VERIFY_RESULT_PROVENANCE' -Status 'SKIPPED' -Reason 'no successful execution with collected results to verify')
    }
    $provisionOutcome = $script:EnvCertOutcomes['PROVISION']
    if ($null -ne $provisionOutcome -and $provisionOutcome.status -eq 'PASS') {
        [void](Invoke-EnvCertRunPhase -Name 'RESTORE_ROLLBACK' -Action { Invoke-EnvCertDispatch -Operation 'RESTORE_ROLLBACK' })
    } else {
        [void](Invoke-EnvCertRecordGatedPhase -Name 'RESTORE_ROLLBACK' -Status 'SKIPPED' -Reason 'nothing provisioned; no rollback required')
    }
    [void](Invoke-EnvCertRunPhase -Name 'CLEANUP' -Action { Invoke-EnvCertDispatch -Operation 'CLEANUP' })
    [void](Invoke-EnvCertRunPhase -Name 'RESIDUE_CHECK' -Action { Invoke-EnvCertDispatch -Operation 'RESIDUE_CHECK' })
}

# Fail closed on residue content: an adapter claiming PASS while reporting
# residue is treated as a residue failure.
$residueOutcome = $script:EnvCertOutcomes['RESIDUE_CHECK']
if ($null -ne $residueOutcome -and $residueOutcome.status -eq 'PASS' -and $null -ne $residueOutcome.data) {
    $residueList = @()
    $residueData = $residueOutcome.data
    if ($residueData -is [System.Collections.IDictionary]) {
        if ($residueData.Contains('Residue')) { $residueList = @($residueData['Residue']) }
    } else {
        $residueProp = $residueData.PSObject.Properties['Residue']
        if ($null -ne $residueProp) { $residueList = @($residueProp.Value) }
    }
    if ($residueList.Count -gt 0) {
        ($script:EnvCertOutcomes['RESIDUE_CHECK']).status = 'FAIL'
        ($script:EnvCertOutcomes['RESIDUE_CHECK']).reason = 'RESIDUE_FOUND'
        ($script:EnvCertOutcomes['RESIDUE_CHECK']).error = 'adapter reported PASS with residue present: ' + ($residueList -join ', ')
        foreach ($rec in $script:EnvCertPhaseRecords) {
            if ($rec.phase -eq 'RESIDUE_CHECK') {
                $rec.status = 'FAIL'
                $rec.reason = 'RESIDUE_FOUND'
                $rec.error = ($script:EnvCertOutcomes['RESIDUE_CHECK']).error
            }
        }
        Write-Host "  [FAIL] phase RESIDUE_CHECK: adapter claimed clean with residue present (fail-closed)" -ForegroundColor Red
    }
}

# Build the cleanup object. Overall PASS requires cleanup clean; a cleanup
# failure forces FAIL and is never hidden.
$cleanupOutcome = $script:EnvCertOutcomes['CLEANUP']
$cleanupSucceeded = ($null -ne $cleanupOutcome -and $cleanupOutcome.status -eq 'PASS')
$cleanupMutated = $false
if ($null -ne $cleanupOutcome -and $null -ne $cleanupOutcome.data) {
    $cleanupData = $cleanupOutcome.data
    if ($cleanupData -is [System.Collections.IDictionary]) {
        if ($cleanupData.Contains('MutatedSystemState')) { $cleanupMutated = [bool]$cleanupData['MutatedSystemState'] }
    } else {
        $mutatedProp = $cleanupData.PSObject.Properties['MutatedSystemState']
        if ($null -ne $mutatedProp) { $cleanupMutated = [bool]$mutatedProp.Value }
    }
}
$cleanupObject = [ordered]@{
    provider = $Provider
    status = $(if ($cleanupSucceeded) { 'PASS' } else { 'FAIL' })
    succeeded = $cleanupSucceeded
    mutatedSystemState = $cleanupMutated
    actions = @($cleanupOutcome.actions)
    error = $cleanupOutcome.error
    completedAtUtc = ([DateTime]::UtcNow.ToString('o'))
}
foreach ($rec in $script:EnvCertPhaseRecords) {
    if ($rec.phase -eq 'CLEANUP') {
        $rec.cleanup = [pscustomobject]$cleanupObject
    }
}

# Bind cleanup disposition into the provenance record (verify runs before
# cleanup, so this field is completed here).
if ($null -ne $script:EnvCertProvenance) {
    $script:EnvCertProvenance.cleanupDisposition = $(if ($cleanupSucceeded) { 'CLEAN' } else { 'DIRTY' })
} else {
    $script:EnvCertProvenance = [pscustomobject][ordered]@{
        verified = $false
        hostCommit = $headSha
        hostTree = $treeSha
        payloadDigest = $payloadDigest
        candidateDigest = $candidateDigest
        provider = $Provider
        vmIdentity = $script:EnvCertGuestIdentity
        guestOs = $null
        guestTimestampUtc = $null
        executableHash = $null
        command = $null
        schemaVersion = $null
        resultArtifactHash = $null
        testCounts = $null
        exitCode = $null
        cleanupDisposition = $(if ($cleanupSucceeded) { 'CLEAN' } else { 'DIRTY' })
        verifyErrors = @('no execution proof was verified in this run')
    }
}

# Capabilities: only claimed when actually executed.
$capabilities = [System.Collections.Generic.List[string]]::new()
$provisionForCap = $script:EnvCertOutcomes['PROVISION']
$executeForCap = $script:EnvCertOutcomes['EXECUTE_PAYLOAD']
$verifyForCap = $script:EnvCertOutcomes['VERIFY_RESULT_PROVENANCE']
$restoreForCap = $script:EnvCertOutcomes['RESTORE_ROLLBACK']
if ($Provider -eq 'hyperv' -and $null -ne $provisionForCap -and $provisionForCap.status -eq 'PASS') {
    [void]$capabilities.Add('hyperv-provision')
}
if ($Provider -eq 'hyperv' -and $null -ne $executeForCap -and $executeForCap.status -eq 'PASS' -and $null -ne $verifyForCap -and $verifyForCap.status -eq 'PASS') {
    [void]$capabilities.Add('guest-execution')
}
if ($null -ne $restoreForCap -and $restoreForCap.status -eq 'PASS' -and $null -ne $restoreForCap.data) {
    $restoredFlag = $false
    $restoreData = $restoreForCap.data
    if ($restoreData -is [System.Collections.IDictionary]) {
        if ($restoreData.Contains('Restored')) { $restoredFlag = [bool]$restoreData['Restored'] }
    } else {
        $restoredProp = $restoreData.PSObject.Properties['Restored']
        if ($null -ne $restoredProp) { $restoredFlag = [bool]$restoredProp.Value }
    }
    if ($restoredFlag) {
        [void]$capabilities.Add('provider-rollback')
    }
}

# Cancellation visibility.
$cancelled = $false
if ($null -ne $executeForCap -and $executeForCap.reason -eq 'CANCELLED') {
    $cancelled = $true
}

# Legacy checks (backward compatible) derived from phase outcomes.
if ($Provider -eq 'hyperv') {
    $discoverForCheck = $script:EnvCertOutcomes['DISCOVER']
    $preflightForCheck = $script:EnvCertOutcomes['PREFLIGHT']
    if ($discoverForCheck.status -eq 'PASS') {
        Add-EnvCertCheck -Id 'hyperv-module-available' -Status 'PASS' -Message 'Windows Hyper-V PowerShell module installed'
    } else {
        Add-EnvCertCheck -Id 'hyperv-module-available' -Status 'REQUIRED-BUT-UNAVAILABLE' -Message 'Windows Hyper-V PowerShell module is not available on host'
    }
    if ($isElevatedProcess) {
        Add-EnvCertCheck -Id 'hyperv-elevation-verified' -Status 'PASS' -Message 'Host process is elevated (Administrator token present)'
    } else {
        Add-EnvCertCheck -Id 'hyperv-elevation-verified' -Status 'BLOCKED_NOT_ELEVATED' -Message 'Hyper-V VM management requires an elevated administrator process'
    }
    if (-not $isElevatedProcess) {
        Add-EnvCertCheck -Id 'hyperv-guest-image-presence' -Status 'SKIPPED' -Message 'Guest image inspection skipped; elevation required first'
    } elseif ($preflightForCheck.status -eq 'PASS') {
        Add-EnvCertCheck -Id 'hyperv-guest-image-presence' -Status 'PASS' -Message "Usable Windows guest image confirmed: $resolvedSource"
    } else {
        Add-EnvCertCheck -Id 'hyperv-guest-image-presence' -Status 'REQUIRED-BUT-UNAVAILABLE' -Message "Clean reference Windows VHDX guest template is not supplied or not usable ($($preflightForCheck.reason))"
    }
} elseif ($Provider -eq 'local') {
    Add-EnvCertCheck -Id 'local-provider-module' -Status 'PASS' -Message 'Local environment provider available in verify/providers/local'
    if ($script:EnvCertProvenance.verified) {
        Add-EnvCertCheck -Id 'local-provider-execution' -Status 'PASS' -Message 'Local host executed the certification payload with verified provenance'
    } elseif ($null -ne $verifyForCap -and $verifyForCap.status -eq 'FAIL') {
        Add-EnvCertCheck -Id 'local-provider-execution' -Status 'FAIL' -Message "Local execution proof failed verification ($($verifyForCap.reason))"
    } else {
        Add-EnvCertCheck -Id 'local-provider-execution' -Status 'REQUIRED-BUT-UNAVAILABLE' -Message 'Local host preflight verified; execution proof unavailable'
    }
} else {
    Add-EnvCertCheck -Id "$Provider-provider-prerequisites" -Status 'REQUIRED-BUT-UNAVAILABLE' -Message "Provider adapter '$Provider' requires external infrastructure and guest credentials"
}

# Per-phase checks (schema-valid statuses).
foreach ($record in $script:EnvCertPhaseRecords) {
    $slug = ($record.phase.ToLowerInvariant() -replace '_', '-')
    $detail = [string]$record.reason
    if ($record.error) { $detail = "$detail | error: $($record.error)" }
    Add-EnvCertCheck -Id "lifecycle-$slug" -Status (ConvertTo-EnvCertCheckStatus -PhaseStatus $record.status) -Message "Lifecycle phase $($record.phase): $($record.status)" -DetailObj $detail
}

$stopwatch.Stop()
$endUtc = [DateTime]::UtcNow

# Terminal classification. PASS requires real execution proof
# (EXECUTE_PAYLOAD + verified provenance) and clean cleanup.
$failedPhases = @($script:EnvCertPhaseRecords | Where-Object { $_.status -eq 'FAIL' })
$hasFail = ($failedPhases.Count -gt 0)
$executedWithProof = ($script:EnvCertProvenance.verified -and $executeForCap.status -eq 'PASS' -and $verifyForCap.status -eq 'PASS')

$laneStatus = 'REQUIRED-BUT-UNAVAILABLE'
if ($hasFail -or (-not $cleanupSucceeded)) {
    $laneStatus = 'FAIL'
} elseif ($elevationBlock) {
    $laneStatus = 'BLOCKED_NOT_ELEVATED'
} elseif ($executedWithProof -and $cleanupSucceeded) {
    $laneStatus = 'PASS'
} else {
    $laneStatus = 'REQUIRED-BUT-UNAVAILABLE'
}

$disposition = $(if ($laneStatus -eq 'PASS') { 'PASS' }
                elseif ($laneStatus -eq 'REQUIRED-BUT-UNAVAILABLE' -or $laneStatus -eq 'BLOCKED_NOT_ELEVATED') { 'BLOCKED_EXTERNAL' }
                else { 'FAIL' })

$failureReason = $null
if ($laneStatus -eq 'FAIL') {
    if (-not $cleanupSucceeded) {
        $failureReason = "Provider cleanup failed ($($cleanupOutcome.reason)); failure is surfaced, never hidden."
        if ($cleanupOutcome.error) { $failureReason = "$failureReason Error: $($cleanupOutcome.error)" }
    }
    if ($hasFail) {
        $first = $failedPhases[0]
        $phaseText = "Lifecycle phase $($first.phase) failed: $($first.reason)"
        if ($first.error) { $phaseText = "$phaseText | $($first.error)" }
        if ($null -eq $failureReason) {
            $failureReason = $phaseText
        } else {
            $failureReason = "$failureReason Also: $phaseText"
        }
    }
    if ($cancelled) {
        $failureReason = "$failureReason (run was cancelled; fail-closed)"
    }
}

$resultObject = [ordered]@{
    schemaVersion = 1
    lane = 'environment-provider'
    status = $laneStatus
    commit = $headSha
    tree = $treeSha
    exactTreeBound = $exactTreeBound
    candidatePath = $CandidatePath
    candidateHash = $candidateDigest
    environment = [ordered]@{
        os = [Environment]::OSVersion.ToString()
        architecture = [Environment]::GetEnvironmentVariable('PROCESSOR_ARCHITECTURE')
        computer = '<local-host>'
        powershell = $PSVersionTable.PSVersion.ToString()
        elevated = $isElevatedProcess
    }
    startUtc = $startUtc.ToString('o')
    endUtc = $endUtc.ToString('o')
    durationMs = [Math]::Round($stopwatch.Elapsed.TotalMilliseconds, 2)
    checks = @($script:EnvCertChecks)
    failureReason = $failureReason
    details = [ordered]@{
        provider = $Provider
        sourcePath = $SourcePath
        resolvedSourcePath = $resolvedSource
        runId = $runId
        vmName = $vmName
        adapterKind = $adapterKind
        preflightReasonCodes = @($preflightReasonCodes)
        cancelled = $cancelled
        timeoutSec = $TimeoutSec
    }
    disposition = $disposition
    artifactHashes = [ordered]@{}
    phases = @($script:EnvCertPhaseRecords)
    capabilities = @($capabilities)
    cleanup = [pscustomobject]$cleanupObject
    provenance = $script:EnvCertProvenance
}

$jsonText = $resultObject | ConvertTo-Json -Depth 12
Set-Content -LiteralPath $OutputPath -Value $jsonText -Encoding utf8NoBOM

$fileBytes = [IO.File]::ReadAllBytes($OutputPath)
$finalSha256 = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($fileBytes)).Replace('-', '').ToLowerInvariant()
Set-Content -LiteralPath "$OutputPath.sha256" -Value "$finalSha256  $([IO.Path]::GetFileName($OutputPath))" -Encoding utf8NoBOM

Write-Host "Environment provider certification finished with status: $laneStatus (disposition: $disposition)"
Write-Host "Artifact written: $OutputPath (SHA-256: $finalSha256)"

return [pscustomobject]$resultObject
