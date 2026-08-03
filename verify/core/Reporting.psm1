<#
    Reporting (task 3.3). Every report is a pure projection of manifest.json +
    index.json + capability result.json envelopes — never a source of new verdicts
    (task 3.5).
#>

function Resolve-ReportArtifactPath {
    param([Parameter(Mandatory)] [pscustomobject] $RunContext, [Parameter(Mandatory)] [string] $DeclaredPath)

    $runRoot = [IO.Path]::GetFullPath($RunContext.RunPath).TrimEnd('\') + '\'
    $candidate = [IO.Path]::GetFullPath((Join-Path $RunContext.RunPath ($DeclaredPath -replace '/', '\')))
    if (-not $candidate.StartsWith($runRoot, [StringComparison]::OrdinalIgnoreCase)) { return $null }
    return $candidate
}

function Get-RunReportModel {
    param([Parameter(Mandatory)] [pscustomobject] $RunContext)

    $manifest = Get-Content $RunContext.ManifestPath -Raw | ConvertFrom-Json
    $index = Get-Content $RunContext.IndexPath -Raw | ConvertFrom-Json
    $capabilities = @()
    $failures = @()
    $skips = @()
    $performance = @()
    $crashArtifacts = @()
    $toolVersions = @()

    foreach ($cap in @($index.capabilities)) {
        $envelopePath = Join-Path $RunContext.RunPath $cap.resultPath
        if (-not (Test-Path -LiteralPath $envelopePath)) { continue }
        $envelope = Get-Content -LiteralPath $envelopePath -Raw | ConvertFrom-Json
        $capabilities += $envelope
        if ($envelope.status -eq 'FAIL') {
            $failures += [pscustomobject]@{ capabilityId = $envelope.capabilityId; checkId = $null; reason = $envelope.reason; summary = $envelope.summary }
        } elseif ($envelope.status -eq 'SKIPPED') {
            $skips += [pscustomobject]@{ capabilityId = $envelope.capabilityId; checkId = $null; reason = $envelope.reason; requiredContext = $envelope.requiredContext }
        }
        foreach ($sub in @($envelope.subResults)) {
            if ($sub.status -eq 'FAIL') {
                $failures += [pscustomobject]@{ capabilityId = $envelope.capabilityId; checkId = $sub.id; reason = $sub.reason; summary = $sub.detail }
            } elseif ($sub.status -eq 'SKIPPED') {
                $skips += [pscustomobject]@{ capabilityId = $envelope.capabilityId; checkId = $sub.id; reason = $sub.reason; requiredContext = $sub.requiredContext }
            }
        }
        foreach ($artifact in @($envelope.artifacts)) {
            $record = [pscustomobject]@{ capabilityId = $envelope.capabilityId; path = $artifact.path; type = $artifact.type }
            if ($artifact.type -match '^performance-(summary|comparison|metrics)$') {
                $payload = $null
                $resolved = Resolve-ReportArtifactPath -RunContext $RunContext -DeclaredPath $artifact.path
                if ($resolved -and (Test-Path -LiteralPath $resolved)) {
                    try { $payload = Get-Content -LiteralPath $resolved -Raw | ConvertFrom-Json } catch { $payload = [pscustomobject]@{ status = 'invalid-performance-payload'; detail = $_.Exception.Message } }
                }
                $performance += [pscustomobject]@{ capabilityId = $record.capabilityId; path = $record.path; type = $record.type; comparison = $payload }
            }
            if ($artifact.type -eq 'crash-analysis-verdict' -or $artifact.type -eq 'crash-dump') { $crashArtifacts += $record }
            if ($artifact.type -eq 'tool-version-metadata') {
                $resolved = Resolve-ReportArtifactPath -RunContext $RunContext -DeclaredPath $artifact.path
                if ($resolved -and (Test-Path -LiteralPath $resolved)) {
                    try {
                        $metadata = Get-Content -LiteralPath $resolved -Raw | ConvertFrom-Json
                        foreach ($tool in @($metadata.tools)) {
                            $toolVersions += [pscustomobject]@{ capabilityId = $envelope.capabilityId; id = $tool.id; version = $tool.version; path = $tool.path; status = $tool.status }
                        }
                    } catch {
                        $failures += [pscustomobject]@{ capabilityId = $envelope.capabilityId; checkId = 'report-tool-metadata'; reason = 'invalid-tool-version-metadata'; summary = $_.Exception.Message }
                    }
                }
            }
        }
    }

    $repairAttempts = @()
    $repairLogPath = Join-Path $RunContext.RunPath 'repair-log.jsonl'
    if (Test-Path -LiteralPath $repairLogPath) {
        foreach ($line in Get-Content -LiteralPath $repairLogPath) {
            if (-not [string]::IsNullOrWhiteSpace($line)) {
                try { $repairAttempts += $line | ConvertFrom-Json } catch { $repairAttempts += [pscustomobject]@{ status = 'invalid-record'; detail = $_.Exception.Message } }
            }
        }
    }

    [pscustomobject]@{
        manifest = $manifest
        index = $index
        capabilities = $capabilities
        environmentFingerprint = $manifest
        toolVersions = $toolVersions
        producedArtifacts = @($capabilities | ForEach-Object { $id = $_.capabilityId; @($_.artifacts) | ForEach-Object { [pscustomobject]@{ capabilityId = $id; path = $_.path; type = $_.type } } })
        performanceSummary = $performance
        failureSummary = [pscustomobject]@{ failures = $failures; crashArtifacts = $crashArtifacts }
        repairAttempts = $repairAttempts
        skipSummary = $skips
    }
}

function New-JsonRunReport {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [pscustomobject] $RunContext)

    $report = Get-RunReportModel -RunContext $RunContext
    $reportPath = Join-Path $RunContext.RunPath 'report.json'
    $report | ConvertTo-Json -Depth 20 | Set-Content -Path $reportPath -Encoding utf8
    return $reportPath
}

function New-MarkdownRunReport {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [pscustomobject] $RunContext)

    $model = Get-RunReportModel -RunContext $RunContext
    $manifest = $model.manifest
    $index = $model.index

    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add("# FastFiles Verification Report")
    $lines.Add("")
    $lines.Add("- **Change:** $($manifest.Change)")
    $lines.Add("- **Run:** $($RunContext.Timestamp)")
    $lines.Add("- **Started (UTC):** $($manifest.StartedAtUtc)")
    $lines.Add("- **OS:** $($manifest.OsBuild)")
    $lines.Add("- **Elevated:** $($manifest.IsElevated)")
    $lines.Add("- **Session:** $($manifest.SessionId) ($($manifest.SessionKind))")
    $lines.Add("- **Provider:** $($manifest.ProviderId)")
    if ($manifest.Toolchain) {
        $lines.Add("- **Toolchain:** $($manifest.Toolchain.DisplayName) $($manifest.Toolchain.InstallationVersion) (SDK $($manifest.Toolchain.WindowsSdkVersion))")
    } else {
        $lines.Add("- **Toolchain:** not found")
    }
    $lines.Add("")
    $lines.Add("## Summary")
    $lines.Add("")
    $lines.Add("| Total | Pass | Fail | Skipped |")
    $lines.Add("|---|---|---|---|")
    $lines.Add("| $($index.summary.total) | $($index.summary.pass) | $($index.summary.fail) | $($index.summary.skipped) |")
    $lines.Add("")

    if ($manifest.CapabilityLoadDiagnostics -and @($manifest.CapabilityLoadDiagnostics).Count -gt 0) {
        $lines.Add("## Capability Load Diagnostics")
        $lines.Add("")
        foreach ($diag in $manifest.CapabilityLoadDiagnostics) {
            $lines.Add("- **$($diag.capabilityId)**: $($diag.reason)")
        }
        $lines.Add("")
    }

    $lines.Add("## Capabilities")
    $lines.Add("")
    foreach ($cap in $index.capabilities) {
        $badge = switch ($cap.status) {
            'PASS'    { 'PASS' }
            'FAIL'    { 'FAIL' }
            'SKIPPED' { 'SKIPPED' }
            default   { $cap.status }
        }
        $lines.Add("### $($cap.capabilityId) — $badge")
        $lines.Add("")
        $lines.Add("- Tier: $($cap.tier)")
        $lines.Add("- Interface version: $($cap.interfaceVersion)")
        $lines.Add("- Duration: $($cap.durationMs) ms")
        if ($cap.reason) { $lines.Add("- Reason: $($cap.reason)") }
        $lines.Add("- Result: ``$($cap.resultPath)``")

        $envelopePath = Join-Path $RunContext.RunPath $cap.resultPath
        if (Test-Path $envelopePath) {
            $envelope = Get-Content $envelopePath -Raw | ConvertFrom-Json
            if ($envelope.artifacts -and @($envelope.artifacts).Count -gt 0) {
                $lines.Add("")
                $lines.Add("Produced artifacts:")
                foreach ($artifact in @($envelope.artifacts)) { $lines.Add("- [$($artifact.type)]($($artifact.path -replace '\\', '/'))") }
            }
            if ($envelope.subResults -and @($envelope.subResults).Count -gt 0) {
                $lines.Add("")
                $lines.Add("| Check | Status | Duration (ms) | Detail |")
                $lines.Add("|---|---|---|---|")
                foreach ($sub in $envelope.subResults) {
                    $detail = if ($sub.detail) { ($sub.detail -replace '\|', '\|') } else { $sub.reason }
                    $lines.Add("| $($sub.id) | $($sub.status) | $($sub.durationMs) | $detail |")
                }
            }
        }
        $lines.Add("")
    }

    $lines.Add("## Failure Summary")
    $lines.Add("")
    if (@($model.failureSummary.failures).Count -eq 0) { $lines.Add("No failures recorded.") }
    foreach ($failure in @($model.failureSummary.failures)) { $lines.Add("- **$($failure.capabilityId)$(if ($failure.checkId) { "/$($failure.checkId)" })**: $($failure.reason) — $($failure.summary)") }
    foreach ($artifact in @($model.failureSummary.crashArtifacts)) { $lines.Add("- Crash artifact: [$($artifact.type)]($($artifact.path))") }
    $lines.Add("")
    $lines.Add("## Performance Summary")
    $lines.Add("")
    if (@($model.performanceSummary).Count -eq 0) { $lines.Add("No performance comparison was recorded.") }
    foreach ($artifact in @($model.performanceSummary)) {
        $comparison = if ($artifact.comparison) { ' — ' + ($artifact.comparison | ConvertTo-Json -Compress -Depth 8) } else { '' }
        $lines.Add("- **$($artifact.capabilityId)**: [$($artifact.type)]($($artifact.path))$comparison")
    }
    $lines.Add("")
    $lines.Add("## Tool Versions")
    $lines.Add("")
    if (@($model.toolVersions).Count -eq 0) { $lines.Add("No capability-recorded tool versions.") }
    foreach ($tool in @($model.toolVersions)) {
        $versionText = if ($tool.version) { $tool.version } else { 'version unavailable' }
        $pathText = if ($tool.path) { ' — `{0}`' -f $tool.path } else { '' }
        $lines.Add("- **$($tool.capabilityId)/$($tool.id)**: $versionText$pathText")
    }
    $lines.Add("")
    $lines.Add("## Repair Attempts")
    $lines.Add("")
    if (@($model.repairAttempts).Count -eq 0) { $lines.Add("No repair attempts recorded.") }
    foreach ($attempt in @($model.repairAttempts)) { $lines.Add("- Iteration $($attempt.iteration): $($attempt.outcome) $($attempt.action)") }
    $lines.Add("")
    $lines.Add("## Skips And Unavailable Context")
    $lines.Add("")
    if (@($model.skipSummary).Count -eq 0) { $lines.Add("No skipped checks.") }
    foreach ($skip in @($model.skipSummary)) { $lines.Add("- **$($skip.capabilityId)$(if ($skip.checkId) { "/$($skip.checkId)" })**: $($skip.reason)") }

    $reportPath = Join-Path $RunContext.RunPath 'report.md'
    $lines -join [Environment]::NewLine | Set-Content -Path $reportPath -Encoding utf8
    return $reportPath
}

function ConvertTo-HtmlText {
    param([object] $Value)
    return [System.Net.WebUtility]::HtmlEncode([string] $Value)
}

function New-HtmlRunReport {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [pscustomobject] $RunContext)

    $model = Get-RunReportModel -RunContext $RunContext
    $manifest = $model.manifest
    $index = $model.index
    $rows = foreach ($cap in $index.capabilities) {
        "<tr data-capability-id=`"$(ConvertTo-HtmlText $cap.capabilityId)`" data-status=`"$(ConvertTo-HtmlText $cap.status)`" data-reason=`"$(ConvertTo-HtmlText $cap.reason)`"><td>$(ConvertTo-HtmlText $cap.capabilityId)</td><td>$(ConvertTo-HtmlText $cap.status)</td><td>$(ConvertTo-HtmlText $cap.interfaceVersion)</td><td>$($cap.tier)</td><td>$($cap.durationMs)</td><td>$(ConvertTo-HtmlText $cap.reason)</td></tr>"
    }
    $artifactItems = @($model.producedArtifacts | ForEach-Object { '<li><strong>{0}</strong>: <a href="{1}">{2}</a> ({3})</li>' -f (ConvertTo-HtmlText $_.capabilityId), (ConvertTo-HtmlText $_.path), (ConvertTo-HtmlText $_.path), (ConvertTo-HtmlText $_.type) })
    $failureItems = @($model.failureSummary.failures | ForEach-Object { '<li><strong>{0}</strong>: {1} — {2}</li>' -f (ConvertTo-HtmlText $_.capabilityId), (ConvertTo-HtmlText $_.reason), (ConvertTo-HtmlText $_.summary) })
    $crashItems = @($model.failureSummary.crashArtifacts | ForEach-Object { '<li><a href="{0}">{1}</a> ({2})</li>' -f (ConvertTo-HtmlText $_.path), (ConvertTo-HtmlText $_.path), (ConvertTo-HtmlText $_.type) })
    $toolItems = @($model.toolVersions | ForEach-Object { '<li><strong>{0}/{1}</strong>: {2} — {3}</li>' -f (ConvertTo-HtmlText $_.capabilityId), (ConvertTo-HtmlText $_.id), (ConvertTo-HtmlText $(if ($_.version) { $_.version } else { 'version unavailable' })), (ConvertTo-HtmlText $_.path) })
    $skipItems = @($model.skipSummary | ForEach-Object { '<li><strong>{0}{1}</strong>: {2}</li>' -f (ConvertTo-HtmlText $_.capabilityId), (ConvertTo-HtmlText $(if ($_.checkId) { "/$($_.checkId)" })), (ConvertTo-HtmlText $_.reason) })
    $repairItems = @($model.repairAttempts | ForEach-Object { '<li>Iteration {0}: {1} {2}</li>' -f (ConvertTo-HtmlText $_.iteration), (ConvertTo-HtmlText $_.outcome), (ConvertTo-HtmlText $_.action) })
    $performanceItems = @($model.performanceSummary | ForEach-Object { '<li><strong>{0}</strong>: <a href="{1}">{2}</a> ({3})</li>' -f (ConvertTo-HtmlText $_.capabilityId), (ConvertTo-HtmlText $_.path), (ConvertTo-HtmlText $_.path), (ConvertTo-HtmlText $_.type) })
    $html = @"
<!doctype html>
<html><head><meta charset="utf-8"><title>FastFiles Verification Report</title>
<style>body{font-family:Segoe UI,Arial,sans-serif;margin:2rem;color:#1f2937}table{border-collapse:collapse;width:100%}th,td{border:1px solid #d1d5db;padding:.5rem;text-align:left}th{background:#f3f4f6}.PASS{color:#166534}.FAIL{color:#b91c1c}.SKIPPED{color:#92400e}</style>
</head><body><h1>FastFiles Verification Report</h1>
<p><strong>Change:</strong> $(ConvertTo-HtmlText $manifest.Change)<br><strong>Run:</strong> $(ConvertTo-HtmlText $RunContext.Timestamp)<br><strong>Provider:</strong> $(ConvertTo-HtmlText $manifest.ProviderId)<br><strong>Target:</strong> $(ConvertTo-HtmlText $manifest.TargetIdentity)<br><strong>OS:</strong> $(ConvertTo-HtmlText $manifest.OsBuild)<br><strong>Elevated:</strong> $(ConvertTo-HtmlText $manifest.IsElevated)<br><strong>Session:</strong> $(ConvertTo-HtmlText $manifest.SessionId) ($(ConvertTo-HtmlText $manifest.SessionKind))</p>
<h2>Summary</h2><p>Total: $($index.summary.total); Pass: $($index.summary.pass); Fail: $($index.summary.fail); Skipped: $($index.summary.skipped)</p>
<h2>Capabilities</h2><table><thead><tr><th>Capability</th><th>Status</th><th>Interface</th><th>Tier</th><th>Duration (ms)</th><th>Reason</th></tr></thead><tbody>$($rows -join [Environment]::NewLine)</tbody></table>
<h2>Failure Summary</h2><ul>$(if ($failureItems.Count -eq 0) { '<li>No failures recorded.</li>' } else { $failureItems -join [Environment]::NewLine })$($crashItems -join [Environment]::NewLine)</ul>
<h2>Performance Summary</h2><ul>$(if ($performanceItems.Count -eq 0) { '<li>No performance comparison was recorded.</li>' } else { $performanceItems -join [Environment]::NewLine })</ul>
<h2>Produced Artifacts</h2><ul>$(if ($artifactItems.Count -eq 0) { '<li>No artifacts recorded.</li>' } else { $artifactItems -join [Environment]::NewLine })</ul>
<h2>Tool Versions</h2><ul>$(if ($toolItems.Count -eq 0) { '<li>No capability-recorded tool versions.</li>' } else { $toolItems -join [Environment]::NewLine })</ul>
<h2>Repair Attempts</h2><ul>$(if ($repairItems.Count -eq 0) { '<li>No repair attempts recorded.</li>' } else { $repairItems -join [Environment]::NewLine })</ul>
<h2>Skips And Unavailable Context</h2><ul>$(if ($skipItems.Count -eq 0) { '<li>No skipped checks.</li>' } else { $skipItems -join [Environment]::NewLine })</ul>
</body></html>
"@
    $reportPath = Join-Path $RunContext.RunPath 'report.html'
    $html | Set-Content -Path $reportPath -Encoding utf8
    return $reportPath
}

function ConvertTo-XmlText {
    param([object] $Value)
    return [Security.SecurityElement]::Escape([string] $Value)
}

function New-JUnitRunReport {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [pscustomobject] $RunContext)

    $model = Get-RunReportModel -RunContext $RunContext
    $index = $model.index
    $testCases = foreach ($cap in $index.capabilities) {
        $name = ConvertTo-XmlText $cap.capabilityId
        $seconds = ([double] $cap.durationMs / 1000.0).ToString([Globalization.CultureInfo]::InvariantCulture)
        switch ($cap.status) {
            'FAIL' { "<testcase classname=`"FastFiles.Verification`" name=`"$name`" time=`"$seconds`"><failure message=`"$(ConvertTo-XmlText $cap.reason)`" /><system-out>$(ConvertTo-XmlText ((@($model.producedArtifacts | Where-Object capabilityId -eq $cap.capabilityId) | ConvertTo-Json -Compress)))</system-out></testcase>" }
            'SKIPPED' { "<testcase classname=`"FastFiles.Verification`" name=`"$name`" time=`"$seconds`"><skipped message=`"$(ConvertTo-XmlText $cap.reason)`" /><system-out>$(ConvertTo-XmlText ((@($model.skipSummary | Where-Object capabilityId -eq $cap.capabilityId) | ConvertTo-Json -Compress)))</system-out></testcase>" }
            default { "<testcase classname=`"FastFiles.Verification`" name=`"$name`" time=`"$seconds`"><system-out>$(ConvertTo-XmlText ((@($model.producedArtifacts | Where-Object capabilityId -eq $cap.capabilityId) | ConvertTo-Json -Compress)))</system-out></testcase>" }
        }
    }
    $properties = "<properties><property name=`"provider`" value=`"$(ConvertTo-XmlText $model.manifest.ProviderId)`" /><property name=`"targetIdentity`" value=`"$(ConvertTo-XmlText $model.manifest.TargetIdentity)`" /><property name=`"osBuild`" value=`"$(ConvertTo-XmlText $model.manifest.OsBuild)`" /></properties>"
    $xml = "<?xml version=`"1.0`" encoding=`"utf-8`"?><testsuite name=`"FastFiles Verification`" tests=`"$($index.summary.total)`" failures=`"$($index.summary.fail)`" skipped=`"$($index.summary.skipped)`">$properties$($testCases -join '')</testsuite>"
    $reportPath = Join-Path $RunContext.RunPath 'report.xml'
    $xml | Set-Content -Path $reportPath -Encoding utf8
    return $reportPath
}

function Get-RunEnvelopeVerdicts {
    param([Parameter(Mandatory)] [pscustomobject] $RunContext)
    $verdicts = @{}
    Get-ChildItem -Path $RunContext.ArtifactsRoot -Directory | ForEach-Object {
        $resultPath = Join-Path $_.FullName 'result.json'
        if (Test-Path $resultPath) {
            $result = Get-Content -Raw $resultPath | ConvertFrom-Json
            $verdicts[$result.capabilityId] = [pscustomobject]@{ status = $result.status; reason = $result.reason }
        }
    }
    return $verdicts
}

function Test-RunReportFidelity {
    <# Task 3.5: reports may project verdicts, but can neither alter nor invent them. #>
    [CmdletBinding()]
    param([Parameter(Mandatory)] [pscustomobject] $RunContext)

    $expected = Get-RunEnvelopeVerdicts -RunContext $RunContext
    New-JsonRunReport -RunContext $RunContext | Out-Null
    New-MarkdownRunReport -RunContext $RunContext | Out-Null
    New-HtmlRunReport -RunContext $RunContext | Out-Null
    New-JUnitRunReport -RunContext $RunContext | Out-Null

    $reportNames = @('report.json', 'report.md', 'report.html', 'report.xml')
    $firstProjection = @{}
    foreach ($name in $reportNames) { $firstProjection[$name] = Get-Content -Raw (Join-Path $RunContext.RunPath $name) }
    New-JsonRunReport -RunContext $RunContext | Out-Null
    New-MarkdownRunReport -RunContext $RunContext | Out-Null
    New-HtmlRunReport -RunContext $RunContext | Out-Null
    New-JUnitRunReport -RunContext $RunContext | Out-Null

    $errors = [System.Collections.Generic.List[string]]::new()
    foreach ($name in $reportNames) {
        $secondProjection = Get-Content -Raw (Join-Path $RunContext.RunPath $name)
        if ($secondProjection -cne $firstProjection[$name]) { $errors.Add("$name is not deterministic when regenerated from the unchanged run tree") }
    }
    $json = Get-Content -Raw (Join-Path $RunContext.RunPath 'report.json') | ConvertFrom-Json
    $actual = @{}
    foreach ($cap in @($json.index.capabilities)) { $actual[$cap.capabilityId] = [pscustomobject]@{ status = $cap.status; reason = $cap.reason } }
    if ($actual.Count -ne $expected.Count) { $errors.Add("JSON report has $($actual.Count) capability verdicts; envelopes have $($expected.Count)") }
    foreach ($id in @($expected.Keys)) {
        if (-not $actual.ContainsKey($id)) { $errors.Add("JSON report omitted '$id'") }
        elseif ($actual[$id].status -ne $expected[$id].status) { $errors.Add("JSON report changed '$id' from $($expected[$id].status) to $($actual[$id].status)") }
        elseif ([string]$actual[$id].reason -cne [string]$expected[$id].reason) { $errors.Add("JSON report changed the reason for '$id'") }
    }

    $markdown = Get-Content -Raw (Join-Path $RunContext.RunPath 'report.md')
    $markdownSections = [regex]::Matches($markdown, '(?m)^### ').Count
    if ($markdownSections -ne $expected.Count) { $errors.Add("Markdown report has $markdownSections capability sections; envelopes have $($expected.Count)") }
    foreach ($id in @($expected.Keys)) {
        $heading = "### $id — $($expected[$id].status)"
        if (-not $markdown.Contains($heading)) { $errors.Add("Markdown report omitted or changed '$id'") }
        if ($expected[$id].reason -and -not $markdown.Contains("- Reason: $($expected[$id].reason)")) { $errors.Add("Markdown report changed the reason for '$id'") }
    }

    $html = Get-Content -Raw (Join-Path $RunContext.RunPath 'report.html')
    $htmlRows = [regex]::Matches($html, '<tr data-capability-id=').Count
    if ($htmlRows -ne $expected.Count) { $errors.Add("HTML report has $htmlRows capability rows; envelopes have $($expected.Count)") }
    foreach ($id in @($expected.Keys)) {
        $marker = 'data-capability-id="{0}" data-status="{1}" data-reason="{2}"' -f
            (ConvertTo-HtmlText $id), (ConvertTo-HtmlText $expected[$id].status), (ConvertTo-HtmlText $expected[$id].reason)
        if (-not $html.Contains($marker)) { $errors.Add("HTML report omitted or changed '$id'") }
    }

    [xml] $junit = Get-Content -Raw (Join-Path $RunContext.RunPath 'report.xml')
    $testCases = @($junit.testsuite.testcase)
    if ($testCases.Count -ne $expected.Count) { $errors.Add("JUnit report has $($testCases.Count) test cases; envelopes have $($expected.Count)") }
    $seenTestCases = @{}
    foreach ($case in $testCases) {
        $status = if ($case.failure) { 'FAIL' } elseif ($case.skipped) { 'SKIPPED' } else { 'PASS' }
        if (-not $expected.ContainsKey($case.name)) { $errors.Add("JUnit report invented '$($case.name)'") }
        elseif ($seenTestCases.ContainsKey([string]$case.name)) { $errors.Add("JUnit report duplicated '$($case.name)'") }
        elseif ($expected[$case.name].status -ne $status) { $errors.Add("JUnit report changed '$($case.name)' from $($expected[$case.name].status) to $status") }
        elseif ($status -eq 'FAIL' -and [string]$case.failure.message -cne [string]$expected[$case.name].reason) { $errors.Add("JUnit report changed the reason for '$($case.name)'") }
        elseif ($status -eq 'SKIPPED' -and [string]$case.skipped.message -cne [string]$expected[$case.name].reason) { $errors.Add("JUnit report changed the reason for '$($case.name)'") }
        $seenTestCases[[string]$case.name] = $true
    }
    foreach ($id in @($expected.Keys)) {
        if (-not $seenTestCases.ContainsKey([string]$id)) { $errors.Add("JUnit report omitted '$id'") }
    }
    return [pscustomobject]@{ Valid = ($errors.Count -eq 0); Errors = $errors }
}

Export-ModuleMember -Function New-JsonRunReport, New-MarkdownRunReport, New-HtmlRunReport, New-JUnitRunReport, Test-RunReportFidelity
