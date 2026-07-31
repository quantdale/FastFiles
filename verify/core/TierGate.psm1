<#
    Tier gating (task 1.7 / D5, D1): compares a capability's or descriptor's declared
    tier requirement against the run's fingerprint and returns an availability verdict.
    A capability/descriptor whose required context is absent is SKIPPED with a
    machine-readable reason — it is never silently treated as passed.
#>

function Test-TierAvailability {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [int] $RequiredTier,
        [Parameter(Mandatory)] [pscustomobject] $Fingerprint
    )

    switch ($RequiredTier) {
        0 {
            return [pscustomobject]@{ Available = $true; Reason = $null; RequiredContext = $null }
        }
        1 {
            if (-not $Fingerprint.IsElevated) {
                return [pscustomobject]@{
                    Available       = $false
                    Reason          = 'not-elevated'
                    RequiredContext = [pscustomobject]@{ tier = 1; needs = 'admin-token'; sessionKind = $Fingerprint.SessionKind }
                }
            }
            return [pscustomobject]@{ Available = $true; Reason = $null; RequiredContext = $null }
        }
        2 {
            return [pscustomobject]@{
                Available       = $false
                Reason          = 'requires-tier-2-context'
                RequiredContext = [pscustomobject]@{ tier = 2; needs = 'interactive-multi-session-uia-context' }
            }
        }
        3 {
            return [pscustomobject]@{
                Available       = $false
                Reason          = 'requires-tier-3-context'
                RequiredContext = [pscustomobject]@{ tier = 3; needs = 'disposable-stress-host' }
            }
        }
        default {
            return [pscustomobject]@{
                Available       = $false
                Reason          = "unknown-tier-$RequiredTier"
                RequiredContext = [pscustomobject]@{ tier = $RequiredTier }
            }
        }
    }
}

Export-ModuleMember -Function Test-TierAvailability
