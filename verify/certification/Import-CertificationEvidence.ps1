[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $EvidenceBundlePath,

    [string] $TargetChange = 'external-certification-readiness-and-evidence-convergence',

    [switch] $CheckStalenessOnly,

    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)

$ErrorActionPreference = 'Stop'

Write-Host "Starting Certification Evidence Import & Staleness Verification:"
Write-Host "  Source: $EvidenceBundlePath"
Write-Host "  Target Change: $TargetChange"

if (-not (Test-Path -LiteralPath $EvidenceBundlePath)) {
    throw "Evidence bundle path does not exist: $EvidenceBundlePath"
}

# Resolve target evidence files (.json)
$jsonFiles = if (Test-Path -LiteralPath $EvidenceBundlePath -PathType Leaf) {
    @([IO.Path]::GetFullPath($EvidenceBundlePath))
} else {
    @(Get-ChildItem -LiteralPath $EvidenceBundlePath -Filter "*.json" -File | ForEach-Object { $_.FullName })
}

if ($jsonFiles.Count -eq 0) {
    throw "No JSON certification evidence artifacts found at $EvidenceBundlePath"
}

$schemasDir = Join-Path $RepoRoot 'verify\certification\schemas'
$currentHead = (& git -C $RepoRoot rev-parse HEAD 2>&1).ToString().Trim()

$importResults = [System.Collections.Generic.List[object]]::new()

# Staleness mapping paths
$stalenessPaths = @{
    'production-signing' = @('src/engine/src/Authenticode*.cpp', 'verify/.signing/', 'verify/qualification/', 'CMakeLists.txt')
    'privileged-ntfs'    = @('src/indexsvc/', 'tests/indexsvc/', 'src/engine/src/Mft*.cpp')
    'clean-host'         = @('setup/', 'verify/release/', 'src/ui/')
    'external-provider'  = @('verify/visual/', 'verify/providers/')
    'long-run'           = @('src/', 'tests/')
    'resource-soak'      = @('src/', 'tests/')
}

foreach ($jsonFile in $jsonFiles) {
    Write-Host "`nProcessing: $([IO.Path]::GetFileName($jsonFile))" -ForegroundColor Cyan
    $content = Get-Content -LiteralPath $jsonFile -Raw -Encoding utf8
    $artifactName = [IO.Path]::GetFileName($jsonFile)

    # 1. Secret Scanning
    $secretPatterns = @(
        '-----BEGIN [A-Z0-9 ]*PRIVATE KEY-----',
        'password\s*[:=]\s*["''][^"'']+["'']',
        'FASTFILES_VISION_PROVIDER_KEY\s*[:=]\s*["''][^"'']+["'']'
    )
    foreach ($pat in $secretPatterns) {
        if ($content -match $pat) {
            throw "Security rejection: evidence artifact $artifactName contains sensitive credential pattern ($pat)"
        }
    }

    # 2. Parse JSON
    $doc = $null
    try {
        $doc = $content | ConvertFrom-Json
    } catch {
        throw "Malformed JSON in ${artifactName}: $($_.Exception.Message)"
    }

    # 3. Validate zero-SHA rejection
    $commit = [string]$doc.commit
    $tree = [string]$doc.tree
    if ([string]::IsNullOrWhiteSpace($commit) -or $commit -match '^0{40}$') {
        throw "Validation failure: artifact $artifactName has all-zero or missing commit SHA"
    }
    if ([string]::IsNullOrWhiteSpace($tree) -or $tree -match '^0{40}$') {
        throw "Validation failure: artifact $artifactName has all-zero or missing tree SHA"
    }

    # 4. Validate Git reachability
    $catExit = 0
    & git -C $RepoRoot cat-file -e "$commit^{commit}" 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  Warning: Commit $commit is not an ancestor/object in current repository clone" -ForegroundColor Yellow
    }

    # 5. Schema Validation
    $laneOrType = if ($doc.lane) { $doc.lane } elseif ($doc.testId) { "long-run" } elseif ($doc.metrics) { "resource-soak" } else { "unknown" }
    $schemaFile = switch ($laneOrType) {
        'long-run' { Join-Path $schemasDir 'long-run-result.schema.json' }
        'resource-soak' { Join-Path $schemasDir 'resource-soak-result.schema.json' }
        default { Join-Path $schemasDir 'certification-result.schema.json' }
    }

    if (Test-Path -LiteralPath $schemaFile) {
        $pyCmd = "import json, jsonschema; s = json.load(open(r'$schemaFile')); d = json.load(open(r'$jsonFile')); jsonschema.validate(instance=d, schema=s); print('OK')"
        $val = & (Get-Command python).Source -c $pyCmd 2>&1
        if ($val -notmatch 'OK') {
            throw "Schema validation failed for $artifactName against $([IO.Path]::GetFileName($schemaFile)): $val"
        }
        Write-Host "  Schema check: PASS ($([IO.Path]::GetFileName($schemaFile)))" -ForegroundColor Green
    }

    # 6. Staleness Check
    $isStale = $false
    $staleReason = $null
    $changedFiles = @()
    if ($commit -ne $currentHead) {
        $pathsToCheck = if ($stalenessPaths.ContainsKey($laneOrType)) { $stalenessPaths[$laneOrType] } else { @('src/') }
        $diffCmd = @('diff', '--name-only', $commit, 'HEAD', '--') + $pathsToCheck
        $changedFiles = @(& git -C $RepoRoot @diffCmd 2>&1 | ForEach-Object { $_.ToString().Trim() } | Where-Object { $_ -ne '' })
        if ($changedFiles.Count -gt 0) {
            $isStale = $true
            $staleReason = "Evidence commit ($commit) is stale: $($changedFiles.Count) relevant file(s) modified between commit and HEAD ($currentHead)"
            Write-Host "  Staleness: STALE ($staleReason)" -ForegroundColor Yellow
        } else {
            Write-Host "  Staleness: CURRENT (No relevant code changes between $commit and $currentHead)" -ForegroundColor Green
        }
    } else {
        Write-Host "  Staleness: CURRENT (Matches HEAD exact commit $commit)" -ForegroundColor Green
    }

    # 7. Safe Import Copy (if not check-only)
    $destinationPath = $null
    if (-not $CheckStalenessOnly) {
        $destDir = Join-Path $RepoRoot "openspec\changes\$TargetChange\evidence\imported"
        if (-not (Test-Path -LiteralPath $destDir)) { New-Item -ItemType Directory -Path $destDir -Force | Out-Null }
        $destinationPath = Join-Path $destDir $artifactName
        Copy-Item -LiteralPath $jsonFile -Destination $destinationPath -Force
        Write-Host "  Imported: $destinationPath" -ForegroundColor Green
    }

    $importResults.Add([pscustomobject]@{
        artifact = $artifactName
        lane = $laneOrType
        commit = $commit
        exactTreeBound = [bool]$doc.exactTreeBound
        schemaValid = $true
        isStale = $isStale
        staleReason = $staleReason
        changedFiles = $changedFiles
        importedTo = $destinationPath
    })
}

Write-Host "`nImport and staleness audit complete for $($importResults.Count) artifact(s)." -ForegroundColor Green
return @($importResults)
