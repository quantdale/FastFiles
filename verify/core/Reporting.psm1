<#
    Reporting (subset of task 3.3, scoped to Markdown + JSON for this phase; HTML and
    JUnit-aggregate projections are follow-up work). Every report is a pure projection
    of manifest.json + index.json + capability result.json envelopes — never a
    source of new verdicts (task 3.5).
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

Export-ModuleMember -Function New-JsonRunReport, New-MarkdownRunReport
