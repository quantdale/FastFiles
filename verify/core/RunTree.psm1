<#
    Per-run artifact tree (task 1.5) and Capability Artifact Contract (task 1.8 / D16):
    verify/runs/<change>/<timestamp>/{manifest.json, index.json, artifacts/<capability>/,
    repair-log.jsonl, report.*}. The core only ever reads a capability's result.json
    envelope — it never parses capability-specific payloads.
#>

Import-Module (Join-Path $PSScriptRoot 'Schema.psm1') -Force -Global

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

    [pscustomobject]@{
        VerifyRoot   = $VerifyRoot
        Change       = $Change
        Timestamp    = $timestamp
        RunPath      = $runPath
        ManifestPath = Join-Path $runPath 'manifest.json'
        IndexPath    = Join-Path $runPath 'index.json'
        RepairLogPath = Join-Path $runPath 'repair-log.jsonl'
        ArtifactsRoot = Join-Path $runPath 'artifacts'
        SchemasRoot  = Join-Path $VerifyRoot 'schemas'
    }
}

function Save-RunManifest {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [pscustomobject] $RunContext,
        [Parameter(Mandatory)] [pscustomobject] $Fingerprint
    )

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

function New-ResultEnvelope {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $CapabilityId,
        [Parameter(Mandatory)] [string] $InterfaceVersion,
        [Parameter(Mandatory)] [int] $Tier,
        [Parameter(Mandatory)] [ValidateSet('PASS', 'FAIL', 'SKIPPED')] [string] $Status,
        [Parameter(Mandatory)] [datetime] $StartedAtUtc,
        [Parameter(Mandatory)] [datetime] $FinishedAtUtc,
        [string] $Reason,
        [pscustomobject] $RequiredContext,
        [string] $Summary,
        [array] $Artifacts = @(),
        [array] $SubResults = @()
    )

    [pscustomobject]@{
        capabilityId     = $CapabilityId
        interfaceVersion = $InterfaceVersion
        tier             = $Tier
        status           = $Status
        reason           = $Reason
        requiredContext  = $RequiredContext
        startedAtUtc     = $StartedAtUtc.ToUniversalTime().ToString('o')
        finishedAtUtc    = $FinishedAtUtc.ToUniversalTime().ToString('o')
        durationMs       = [math]::Round(($FinishedAtUtc - $StartedAtUtc).TotalMilliseconds, 0)
        summary          = $Summary
        artifacts        = $Artifacts
        subResults       = $SubResults
    }
}

function Save-CapabilityResult {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [pscustomobject] $RunContext,
        [Parameter(Mandatory)] [pscustomobject] $Result
    )

    $schema = Get-Content (Join-Path $RunContext.SchemasRoot 'result-envelope.schema.json') -Raw | ConvertFrom-Json
    $validation = Test-JsonSchema -Data $Result -Schema $schema
    if (-not $validation.Valid) {
        throw "result.json for capability '$($Result.capabilityId)' failed schema validation:`n$($validation.Errors -join [Environment]::NewLine)"
    }

    $dir = New-CapabilityArtifactsDir -RunContext $RunContext -CapabilityId $Result.capabilityId
    $resultPath = Join-Path $dir 'result.json'
    $Result | ConvertTo-Json -Depth 15 | Set-Content -Path $resultPath -Encoding utf8
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
    if (Test-Path $RunContext.ArtifactsRoot) {
        Get-ChildItem -Path $RunContext.ArtifactsRoot -Directory | ForEach-Object {
            $resultPath = Join-Path $_.FullName 'result.json'
            if (Test-Path $resultPath) {
                $envelope = Get-Content $resultPath -Raw | ConvertFrom-Json
                $capabilityEntries += [pscustomobject]@{
                    capabilityId     = $envelope.capabilityId
                    interfaceVersion = $envelope.interfaceVersion
                    tier             = $envelope.tier
                    status           = $envelope.status
                    reason           = $envelope.reason
                    durationMs       = $envelope.durationMs
                    artifactCount    = @($envelope.artifacts).Count
                    resultPath       = "artifacts/$($_.Name)/result.json"
                }
            }
        }
    }

    $index = [pscustomobject]@{
        runId          = $RunContext.Timestamp
        change         = $RunContext.Change
        generatedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
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

Export-ModuleMember -Function New-VerificationRun, Save-RunManifest, New-CapabilityArtifactsDir, New-ResultEnvelope, Save-CapabilityResult, Build-RunIndex
