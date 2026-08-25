<#
    Worker for one capability invocation.

    runtime-certification-and-verifier-convergence 4.1: the parent owns the
    process and timeout; this child owns only capability resolution/execution
    and writes one candidate object to the invocation-provided path.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $RequestPath
)

$ErrorActionPreference = 'Stop'
$request = $null
try {
    $request = Get-Content -LiteralPath $RequestPath -Raw | ConvertFrom-Json
    if ([int]$request.protocolVersion -ne 1) { throw "Unsupported capability worker protocol '$($request.protocolVersion)'." }

    $verifyRoot = Split-Path -Parent $PSScriptRoot
    Import-Module (Join-Path $verifyRoot 'core\Registry.psm1') -Force -Global
    Import-Module (Join-Path $verifyRoot 'core\CapabilityRunner.psm1') -Force -Global

    $discovery = Find-Capabilities -CapabilitiesRoot ([string]$request.capabilitiesRoot) -ManifestSchemaPath ([string]$request.manifestSchemaPath)
    $matches = @($discovery.Capabilities | Where-Object { $_.Id -eq [string]$request.capabilityId })
    if ($matches.Count -ne 1) { throw "Capability '$($request.capabilityId)' resolved to $($matches.Count) registered entries." }

    $options = @{}
    if ($request.options) {
        foreach ($property in $request.options.PSObject.Properties) {
            if ($property.Value -is [System.Array]) { $options[$property.Name] = @($property.Value) }
            else { $options[$property.Name] = $property.Value }
        }
    }
    $rawResults = @(Invoke-CapabilityDirect -Capability $matches[0] -RunContext $request.runContext -Fingerprint $request.fingerprint -Options $options)
    if ($rawResults.Count -ne 1) { throw "Capability worker received $($rawResults.Count) authoritative result objects." }

    $candidate = [pscustomobject]@{
        protocolVersion = 1
        capabilityId = [string]$request.capabilityId
        runId = [string]$request.runContext.RunId
        invocationId = [string]$request.runContext.InvocationId
        result = $rawResults[0]
    }
    $candidateTemp = "$($request.candidatePath).$([guid]::NewGuid().ToString('n')).tmp"
    $candidate | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $candidateTemp -Encoding utf8
    Move-Item -LiteralPath $candidateTemp -Destination ([string]$request.candidatePath) -Force
    exit 0
} catch {
    if ($request -and $request.candidatePath) {
        try {
            [pscustomobject]@{
                protocolVersion = 1
                capabilityId = [string]$request.capabilityId
                runId = [string]$request.runContext.RunId
                invocationId = [string]$request.runContext.InvocationId
                error = [string]$_.Exception.Message
            } | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath ([string]$request.candidatePath) -Encoding utf8
        } catch { }
    }
    Write-Error "Capability worker failed: $($_.Exception.Message)"
    exit 3
}
