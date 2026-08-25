<#
    Per-run artifact tree (task 1.5) and Capability Artifact Contract (task 1.8 / D16):
    verify/runs/<change>/<timestamp>/{manifest.json, index.json, artifacts/<capability>/,
    repair-log.jsonl, report.*}. The core only ever reads a capability's result.json
    envelope — it never parses capability-specific payloads.
#>

Import-Module (Join-Path $PSScriptRoot 'Schema.psm1') -Force -Global
Import-Module (Join-Path $PSScriptRoot 'ResultModel.psm1') -Force -Global

function New-VerificationRun {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $VerifyRoot,
        [Parameter(Mandatory)] [string] $Change
    )

    $timestamp = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')
    $runPath = Join-Path $VerifyRoot "runs\$Change\$timestamp"

    # Guard against two runs racing onto the same second: if it exists, disambiguate.
    $suffix = 1
    $basePath = $runPath
    while (Test-Path $runPath) {
        $runPath = "$basePath-$suffix"
        $suffix++
    }

    New-Item -ItemType Directory -Path $runPath -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $runPath 'artifacts') -Force | Out-Null
    $timestamp = Split-Path -Leaf $runPath

    $runId = [guid]::NewGuid().ToString('n')
    $invocationId = [guid]::NewGuid().ToString('n')
    [pscustomobject]@{
        VerifyRoot    = $VerifyRoot
        Change        = $Change
        Timestamp     = $timestamp
        RunId         = $runId
        InvocationId  = $invocationId
        RunPath       = $runPath
        ManifestPath  = Join-Path $runPath 'manifest.json'
        IndexPath     = Join-Path $runPath 'index.json'
        RepairLogPath = Join-Path $runPath 'repair-log.jsonl'
        ArtifactsRoot = Join-Path $runPath 'artifacts'
        SchemasRoot   = Join-Path $VerifyRoot 'schemas'
    }
}

function Save-RunManifest {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [pscustomobject] $RunContext,
        [Parameter(Mandatory)] [pscustomobject] $Fingerprint
    )

    if ($Fingerprint.PSObject.Properties['runId']) { $Fingerprint.runId = [string]$RunContext.RunId }
    else { $Fingerprint | Add-Member -NotePropertyName runId -NotePropertyValue ([string]$RunContext.RunId) }
    if ($Fingerprint.PSObject.Properties['invocationId']) { $Fingerprint.invocationId = [string]$RunContext.InvocationId }
    else { $Fingerprint | Add-Member -NotePropertyName invocationId -NotePropertyValue ([string]$RunContext.InvocationId) }

    $schema = Get-Content (Join-Path $RunContext.SchemasRoot 'manifest.schema.json') -Raw | ConvertFrom-Json
    $validation = Test-JsonSchema -Data $Fingerprint -Schema $schema
    if (-not $validation.Valid) {
        throw "manifest.json failed schema validation:`n$($validation.Errors -join [Environment]::NewLine)"
    }

    $Fingerprint | ConvertTo-Json -Depth 10 | Set-Content -Path $RunContext.ManifestPath -Encoding utf8
}

function New-CapabilityArtifactsDir {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [pscustomobject] $RunContext,
        [Parameter(Mandatory)] [string] $CapabilityId
    )
    $dir = Join-Path $RunContext.ArtifactsRoot $CapabilityId
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    return $dir
}

function ConvertTo-RunRelativeArtifact {
    param(
        [Parameter(Mandatory)] [pscustomobject] $RunContext,
        [Parameter(Mandatory)] [object] $Artifact
    )

    $path = [string](Get-VerificationProperty -InputObject $Artifact -Name 'path')
    $type = [string](Get-VerificationProperty -InputObject $Artifact -Name 'type')
    if ([string]::IsNullOrWhiteSpace($path) -or [string]::IsNullOrWhiteSpace($type)) {
        throw 'Result artifact entries require non-empty path and type.'
    }
    if ([IO.Path]::IsPathRooted($path)) {
        $root = [IO.Path]::GetFullPath($RunContext.RunPath).TrimEnd('\') + '\'
        $full = [IO.Path]::GetFullPath($path)
        if (-not $full.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Result artifact '$path' escapes invocation run root '$($RunContext.RunPath)'."
        }
        $path = $full.Substring($root.Length).Replace('\', '/')
    } else {
        $path = $path.Replace('\', '/')
        if ($path -match '(^|/)\.\.?(/|$)') { throw "Result artifact path '$path' is not invocation-relative." }
    }
    [pscustomobject]@{ path = $path; type = $type }
}

function New-ResultEnvelope {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $CapabilityId,
        [Parameter(Mandatory)] [string] $InterfaceVersion,
        [Parameter(Mandatory)] [int] $Tier,
        [Parameter(Mandatory)] [ValidateSet('PASS', 'FAIL', 'SKIPPED')] [string] $Status,
        [Parameter(Mandatory)] [datetime] $StartedAtUtc,
        [Parameter(Mandatory)] [datetime] $FinishedAtUtc,
        [pscustomobject] $RunContext,
        [string] $Reason,
        [string] $Classification,
        [pscustomobject] $RequiredContext,
        [string] $Summary,
        [array] $Artifacts = @(),
        [array] $SubResults = @(),
        [object] $Lifecycle,
        [object] $ProviderIdentity,
        [object] $Metadata,
        [object] $PrimaryFailure,
        [array] $SecondaryFailures = @()
    )

    $runId = if ($RunContext -and $RunContext.RunId) { [string]$RunContext.RunId } else { 'unbound' }
    $invocationId = if ($RunContext -and $RunContext.InvocationId) { [string]$RunContext.InvocationId } else { 'unbound' }
    $normalizedArtifacts = @($Artifacts | ForEach-Object { if ($RunContext) { ConvertTo-RunRelativeArtifact -RunContext $RunContext -Artifact $_ } else { $_ } })
    $resolvedClassification = if ($Classification) { $Classification } else { Get-VerificationClassification -Status $Status -Reason $Reason -ExplicitClassification $null }
    $resolvedLifecycle = if ($Lifecycle) { $Lifecycle } else { [pscustomobject]@{ cleanup = $null } }

    [pscustomobject]@{
        runId             = $runId
        invocationId      = $invocationId
        capabilityId     = $CapabilityId
        interfaceVersion = $InterfaceVersion
        tier             = $Tier
        status           = $Status
        reason           = $Reason
        classification    = $resolvedClassification
        requiredContext  = $RequiredContext
        startedAtUtc     = $StartedAtUtc.ToUniversalTime().ToString('o')
        finishedAtUtc    = $FinishedAtUtc.ToUniversalTime().ToString('o')
        durationMs       = [math]::Round(($FinishedAtUtc - $StartedAtUtc).TotalMilliseconds, 0)
        summary          = Limit-VerificationText $Summary
        providerIdentity = $ProviderIdentity
        metadata         = $Metadata
        lifecycle        = $resolvedLifecycle
        primaryFailure   = $PrimaryFailure
        secondaryFailures = @($SecondaryFailures)
        artifacts        = $normalizedArtifacts
        subResults       = $SubResults
    }
}

function Save-CapabilityResult {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [pscustomobject] $RunContext,
        [Parameter(Mandatory)] [pscustomobject] $Result
    )

    if ([string]$Result.runId -ne [string]$RunContext.RunId -or [string]$Result.invocationId -ne [string]$RunContext.InvocationId) {
        throw "Result identity mismatch for capability '$($Result.capabilityId)': expected run '$($RunContext.RunId)' and invocation '$($RunContext.InvocationId)'."
    }
    $schema = Get-Content (Join-Path $RunContext.SchemasRoot 'result-envelope.schema.json') -Raw | ConvertFrom-Json
    $validation = Test-JsonSchema -Data $Result -Schema $schema
    if (-not $validation.Valid) {
        throw "result.json for capability '$($Result.capabilityId)' failed schema validation:`n$($validation.Errors -join [Environment]::NewLine)"
    }

    $dir = New-CapabilityArtifactsDir -RunContext $RunContext -CapabilityId $Result.capabilityId
    $resultPath = Join-Path $dir 'result.json'
    $temporaryPath = "$resultPath.$([guid]::NewGuid().ToString('n')).tmp"
    $Result | ConvertTo-Json -Depth 20 | Set-Content -Path $temporaryPath -Encoding utf8
    Move-Item -LiteralPath $temporaryPath -Destination $resultPath -Force
    return $resultPath
}

function Save-RunResult {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [pscustomobject] $RunContext,
        [Parameter(Mandatory)] [pscustomobject] $Result
    )

    if ([string]$Result.runId -ne [string]$RunContext.RunId -or [string]$Result.invocationId -ne [string]$RunContext.InvocationId) {
        throw "Top-level run result identity mismatch: expected run '$($RunContext.RunId)' and invocation '$($RunContext.InvocationId)'."
    }
    $schema = Get-Content (Join-Path $RunContext.SchemasRoot 'run-result.schema.json') -Raw | ConvertFrom-Json
    $validation = Test-JsonSchema -Data $Result -Schema $schema
    if (-not $validation.Valid) { throw "run-result.json failed schema validation:`n$($validation.Errors -join [Environment]::NewLine)" }
    $resultPath = Join-Path $RunContext.RunPath 'run-result.json'
    $temporaryPath = "$resultPath.$([guid]::NewGuid().ToString('n')).tmp"
    $Result | ConvertTo-Json -Depth 25 | Set-Content -LiteralPath $temporaryPath -Encoding utf8
    Move-Item -LiteralPath $temporaryPath -Destination $resultPath -Force
    return $resultPath
}

function Build-RunIndex {
    <#
        The core's indexer: reads only each capability's result.json envelope and
        never the capability-specific payloads alongside it (D16).
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [pscustomobject] $RunContext
    )

    $capabilityEntries = @()
    $seenCapabilityIds = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    if (Test-Path $RunContext.ArtifactsRoot) {
        Get-ChildItem -Path $RunContext.ArtifactsRoot -Directory | ForEach-Object {
            $resultPath = Join-Path $_.FullName 'result.json'
            if (Test-Path $resultPath) {
                $envelope = Get-Content $resultPath -Raw | ConvertFrom-Json
                $schema = Get-Content (Join-Path $RunContext.SchemasRoot 'result-envelope.schema.json') -Raw | ConvertFrom-Json
                $validation = Test-JsonSchema -Data $envelope -Schema $schema
                if (-not $validation.Valid) { throw "Invalid persisted result '$resultPath': $($validation.Errors -join '; ')" }
                if ([string]$envelope.runId -ne [string]$RunContext.RunId -or [string]$envelope.invocationId -ne [string]$RunContext.InvocationId) {
                    throw "Cross-run result '$resultPath' does not belong to the selected invocation."
                }
                if (-not $seenCapabilityIds.Add([string]$envelope.capabilityId)) {
                    throw "Duplicate capability result '$($envelope.capabilityId)' in run '$($RunContext.RunId)'."
                }
                $capabilityEntries += [pscustomobject]@{
                    runId            = $envelope.runId
                    invocationId     = $envelope.invocationId
                    capabilityId     = $envelope.capabilityId
                    interfaceVersion = $envelope.interfaceVersion
                    tier             = $envelope.tier
                    status           = $envelope.status
                    reason           = $envelope.reason
                    classification   = $envelope.classification
                    durationMs       = $envelope.durationMs
                    artifactCount    = @($envelope.artifacts).Count
                    resultPath       = "artifacts/$($_.Name)/result.json"
                }
            }
        }
    }

    $runResultPath = Join-Path $RunContext.RunPath 'run-result.json'
    $runResult = $null
    if (Test-Path -LiteralPath $runResultPath) {
        $runResult = Get-Content -LiteralPath $runResultPath -Raw | ConvertFrom-Json
        $runResultSchema = Get-Content (Join-Path $RunContext.SchemasRoot 'run-result.schema.json') -Raw | ConvertFrom-Json
        $runResultValidation = Test-JsonSchema -Data $runResult -Schema $runResultSchema
        if (-not $runResultValidation.Valid) { throw "Invalid top-level run result '$runResultPath': $($runResultValidation.Errors -join '; ')" }
        if ([string]$runResult.runId -ne [string]$RunContext.RunId -or [string]$runResult.invocationId -ne [string]$RunContext.InvocationId) { throw "Top-level run result '$runResultPath' belongs to another invocation." }
    }
    $index = [pscustomobject]@{
        runId          = $RunContext.RunId
        invocationId   = $RunContext.InvocationId
        timestamp      = $RunContext.Timestamp
        change         = $RunContext.Change
        generatedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
        runResultPath  = if ($runResult) { 'run-result.json' } else { $null }
        runResult      = $runResult
        capabilities   = $capabilityEntries
        summary        = [pscustomobject]@{
            total    = $capabilityEntries.Count
            pass     = @($capabilityEntries | Where-Object { $_.status -eq 'PASS' }).Count
            fail     = @($capabilityEntries | Where-Object { $_.status -eq 'FAIL' }).Count
            skipped  = @($capabilityEntries | Where-Object { $_.status -eq 'SKIPPED' }).Count
        }
    }

    $index | ConvertTo-Json -Depth 10 | Set-Content -Path $RunContext.IndexPath -Encoding utf8
    return $index
}

Export-ModuleMember -Function New-VerificationRun, Save-RunManifest, New-CapabilityArtifactsDir, New-ResultEnvelope, Save-CapabilityResult, Save-RunResult, Build-RunIndex
