[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $BinaryPath,
    [ValidateSet('UpperNoSeparator', 'ColonSeparated', 'SpaceSeparated')]
    [string] $Format = 'UpperNoSeparator',
    [ValidateSet('human', 'json')]
    [string] $OutputFormat = 'human'
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'ReleaseCommon.psm1') -Force -DisableNameChecking

try {
    if (-not (Test-Path -LiteralPath $BinaryPath -PathType Leaf)) {
        throw "SIGNED_BINARY_REQUIRED: executable not found: $BinaryPath"
    }
    $architecture = Get-PeArchitecture -Path $BinaryPath
    if ($architecture.architecture -ne 'x64') {
        throw "WRONG_ARCHITECTURE: expected x64, found $($architecture.architecture) ($($architecture.machine))"
    }
    $signature = Get-SignatureRecord -Path $BinaryPath
    if (-not $signature.signed -or -not $signature.valid -or -not $signature.leafThumbprint -or
        -not $signature.timestampSigned -or -not $signature.timestampValid) {
        throw "SIGNED_BINARY_REQUIRED: Authenticode status is '$($signature.status)'"
    }
    if (-not (Test-ThumbprintFormat -Value $signature.leafThumbprint)) {
        throw 'MALFORMED_LEAF_THUMBPRINT: the signer certificate thumbprint is not 20-byte SHA-1 hexadecimal.'
    }

    $pin = $signature.leafThumbprint.ToUpperInvariant()
    $formatted = switch ($Format) {
        'UpperNoSeparator' { $pin }
        'ColonSeparated' { ($pin -replace '(.{2})(?=.)', '$1:').TrimEnd(':') }
        'SpaceSeparated' { ($pin -replace '(.{2})(?=.)', '$1 ').TrimEnd(' ') }
    }
    $result = [pscustomobject]@{
        status = 'PASS'
        binary = [IO.Path]::GetFullPath($BinaryPath)
        architecture = $architecture.architecture
        signatureStatus = $signature.status
        signerSubject = $signature.signerSubject
        leafThumbprint = $pin
        candidatePin = $formatted
        format = $Format
        productionPinWritten = $false
        note = 'Candidate only. Review and provision through the release configuration process; this command never edits source or build configuration.'
    }
    if ($OutputFormat -eq 'json') { $result | ConvertTo-Json -Depth 8 } else {
        Write-Host "Signer: $($result.signerSubject)"
        Write-Host "Leaf thumbprint: $($result.leafThumbprint)"
        Write-Host "Candidate pin ($Format): $($result.candidatePin)"
        Write-Host 'Production pin written: NO'
    }
    exit 0
} catch {
    $errorRecord = [pscustomobject]@{ status = 'BLOCKED_OR_INVALID'; error = $_.Exception.Message; productionPinWritten = $false }
    if ($OutputFormat -eq 'json') { $errorRecord | ConvertTo-Json -Depth 6 } else { Write-Error $_.Exception.Message }
    exit 1
}
