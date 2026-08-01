<#
    Reporting (task 3.3). Every report is a pure projection of manifest.json +
    index.json + capability result.json envelopes — never a source of new verdicts
    (task 3.5).
#>

function New-JsonRunReport {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [pscustomobject] $RunContext)

    $manifest = Get-Content $RunContext.ManifestPath -Raw | ConvertFrom-Json
    $index = Get-Content $RunContext.IndexPath -Raw | ConvertFrom-Json

    $report = [pscustomobject]@{
        manifest = $manifest
        index    = $index
    }
    $reportPath = Join-Path $RunContext.RunPath 'report.json'
    $report | ConvertTo-Json -Depth 20 | Set-Content -Path $reportPath -Encoding utf8
    return $reportPath
}

function New-MarkdownRunReport {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [pscustomobject] $RunContext)

    $manifest = Get-Content $RunContext.ManifestPath -Raw | ConvertFrom-Json
    $index = Get-Content $RunContext.IndexPath -Raw | ConvertFrom-Json

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
        $lines.Add("- Duration: $($cap.durationMs) ms")
        if ($cap.reason) { $lines.Add("- Reason: $($cap.reason)") }
        $lines.Add("- Result: ``$($cap.resultPath)``")

        $envelopePath = Join-Path $RunContext.RunPath $cap.resultPath
        if (Test-Path $envelopePath) {
            $envelope = Get-Content $envelopePath -Raw | ConvertFrom-Json
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

    $manifest = Get-Content $RunContext.ManifestPath -Raw | ConvertFrom-Json
    $index = Get-Content $RunContext.IndexPath -Raw | ConvertFrom-Json
    $rows = foreach ($cap in $index.capabilities) {
        "<tr data-capability-id=`"$(ConvertTo-HtmlText $cap.capabilityId)`" data-status=`"$(ConvertTo-HtmlText $cap.status)`" data-reason=`"$(ConvertTo-HtmlText $cap.reason)`"><td>$(ConvertTo-HtmlText $cap.capabilityId)</td><td>$(ConvertTo-HtmlText $cap.status)</td><td>$($cap.tier)</td><td>$($cap.durationMs)</td><td>$(ConvertTo-HtmlText $cap.reason)</td></tr>"
    }
    $html = @"
<!doctype html>
<html><head><meta charset="utf-8"><title>FastFiles Verification Report</title>
<style>body{font-family:Segoe UI,Arial,sans-serif;margin:2rem;color:#1f2937}table{border-collapse:collapse;width:100%}th,td{border:1px solid #d1d5db;padding:.5rem;text-align:left}th{background:#f3f4f6}.PASS{color:#166534}.FAIL{color:#b91c1c}.SKIPPED{color:#92400e}</style>
</head><body><h1>FastFiles Verification Report</h1>
<p><strong>Change:</strong> $(ConvertTo-HtmlText $manifest.Change)<br><strong>Run:</strong> $(ConvertTo-HtmlText $RunContext.Timestamp)<br><strong>Provider:</strong> $(ConvertTo-HtmlText $manifest.ProviderId)</p>
<h2>Summary</h2><p>Total: $($index.summary.total); Pass: $($index.summary.pass); Fail: $($index.summary.fail); Skipped: $($index.summary.skipped)</p>
<h2>Capabilities</h2><table><thead><tr><th>Capability</th><th>Status</th><th>Tier</th><th>Duration (ms)</th><th>Reason</th></tr></thead><tbody>$($rows -join [Environment]::NewLine)</tbody></table>
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

    $index = Get-Content $RunContext.IndexPath -Raw | ConvertFrom-Json
    $testCases = foreach ($cap in $index.capabilities) {
        $name = ConvertTo-XmlText $cap.capabilityId
        $seconds = ([double] $cap.durationMs / 1000.0).ToString([Globalization.CultureInfo]::InvariantCulture)
        switch ($cap.status) {
            'FAIL' { "<testcase classname=`"FastFiles.Verification`" name=`"$name`" time=`"$seconds`"><failure message=`"$(ConvertTo-XmlText $cap.reason)`" /></testcase>" }
            'SKIPPED' { "<testcase classname=`"FastFiles.Verification`" name=`"$name`" time=`"$seconds`"><skipped message=`"$(ConvertTo-XmlText $cap.reason)`" /></testcase>" }
            default { "<testcase classname=`"FastFiles.Verification`" name=`"$name`" time=`"$seconds`" />" }
        }
    }
    $xml = "<?xml version=`"1.0`" encoding=`"utf-8`"?><testsuite name=`"FastFiles Verification`" tests=`"$($index.summary.total)`" failures=`"$($index.summary.fail)`" skipped=`"$($index.summary.skipped)`">$($testCases -join '')</testsuite>"
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
    foreach ($id in $expected.Keys) {
        if (-not $actual.ContainsKey($id)) { $errors.Add("JSON report omitted '$id'") }
        elseif ($actual[$id].status -ne $expected[$id].status) { $errors.Add("JSON report changed '$id' from $($expected[$id].status) to $($actual[$id].status)") }
        elseif ([string]$actual[$id].reason -cne [string]$expected[$id].reason) { $errors.Add("JSON report changed the reason for '$id'") }
    }

    $markdown = Get-Content -Raw (Join-Path $RunContext.RunPath 'report.md')
    $markdownSections = [regex]::Matches($markdown, '(?m)^### ').Count
    if ($markdownSections -ne $expected.Count) { $errors.Add("Markdown report has $markdownSections capability sections; envelopes have $($expected.Count)") }
    foreach ($id in $expected.Keys) {
        $heading = "### $id — $($expected[$id].status)"
        if (-not $markdown.Contains($heading)) { $errors.Add("Markdown report omitted or changed '$id'") }
        if ($expected[$id].reason -and -not $markdown.Contains("- Reason: $($expected[$id].reason)")) { $errors.Add("Markdown report changed the reason for '$id'") }
    }

    $html = Get-Content -Raw (Join-Path $RunContext.RunPath 'report.html')
    $htmlRows = [regex]::Matches($html, '<tr data-capability-id=').Count
    if ($htmlRows -ne $expected.Count) { $errors.Add("HTML report has $htmlRows capability rows; envelopes have $($expected.Count)") }
    foreach ($id in $expected.Keys) {
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
    foreach ($id in $expected.Keys) {
        if (-not $seenTestCases.ContainsKey([string]$id)) { $errors.Add("JUnit report omitted '$id'") }
    }
    return [pscustomobject]@{ Valid = ($errors.Count -eq 0); Errors = $errors }
}

Export-ModuleMember -Function New-JsonRunReport, New-MarkdownRunReport, New-HtmlRunReport, New-JUnitRunReport, Test-RunReportFidelity
