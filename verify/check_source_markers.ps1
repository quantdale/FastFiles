<#
.SYNOPSIS
    Rejects temporary debug-stall markers from non-test product source.

.DESCRIPTION
    CI invokes this guard before configuring the build. It intentionally scans
    only first-party files under src/; tests and audit/history documents may
    describe the marker without making it part of a production binary.
#>

[CmdletBinding()]
param(
    [string] $RepoRoot = (Split-Path $PSScriptRoot -Parent)
)

$ErrorActionPreference = 'Stop'
$sourceRoot = Join-Path $RepoRoot 'src'
$extensions = @('.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.hxx')
$patterns = @('TEMP-DEBUG', 'DEBUG_STALL')

if (-not (Test-Path -LiteralPath $sourceRoot -PathType Container)) {
    Write-Error "Source root is missing: $sourceRoot"
    exit 1
}

$files = @(Get-ChildItem -LiteralPath $sourceRoot -Recurse -File |
    Where-Object { $extensions -contains $_.Extension.ToLowerInvariant() })
$matches = @()
foreach ($file in $files) {
    $matches += @(Select-String -LiteralPath $file.FullName -Pattern $patterns -SimpleMatch)
}

if ($matches.Count -gt 0) {
    foreach ($match in $matches) {
        Write-Host ("FAIL: production debug marker at {0}:{1}: {2}" -f
            $match.Path, $match.LineNumber, $match.Line.Trim()) -ForegroundColor Red
    }
    exit 1
}

Write-Host ("PASS: no TEMP-DEBUG or DEBUG_STALL markers in {0} production source files" -f $files.Count)
exit 0
