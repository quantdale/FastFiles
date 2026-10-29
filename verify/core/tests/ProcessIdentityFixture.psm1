# Test-fixture capture only. A launch path can be an execution alias and is not
# evidence of the running image. Production identity/ownership checks stay in
# CapabilityRunner.psm1; fixtures must supply the same actual image and start.
function Wait-ProcessFixtureIdentity {
    param(
        [Parameter(Mandatory)] [System.Diagnostics.Process] $Process,
        [ValidateRange(1, 10000)] [int] $TimeoutMilliseconds = 5000
    )

    $expectedStart = $Process.StartTime.ToUniversalTime()
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    do {
        if ($Process.HasExited) { throw 'Fixture process exited before its image identity was captured.' }
        $observed = $null
        try {
            $observed = Get-Process -Id $Process.Id -ErrorAction Stop
            $path = [string]$observed.Path
            $start = $observed.StartTime.ToUniversalTime()
            if (-not [string]::IsNullOrWhiteSpace($path) -and $start -eq $expectedStart) {
                $path = [IO.Path]::GetFullPath($path)
                $startText = $start.ToString('o')
                return [pscustomobject]@{
                    pid = [int]$Process.Id
                    path = $path
                    startTimeUtc = $startText
                    identityKey = "$($Process.Id)|$($path.ToLowerInvariant())|$startText"
                }
            }
        } catch {
            # A newly launched process can briefly have no readable image.
            # Retry only within the bound; never substitute its launch path.
        } finally {
            if ($null -ne $observed) { $observed.Dispose() }
        }
        Start-Sleep -Milliseconds 25
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Fixture process actual image/start identity was unavailable within the capture bound.'
}

Export-ModuleMember -Function Wait-ProcessFixtureIdentity
