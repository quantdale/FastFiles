<#
    hardening-campaign Phase 4: Convert-UiaForegroundFailureToSkip evidence
    contract. A skip requires PROVEN foreign foreground ownership (the
    foreground='...' capture or the JSON foreign markers) or a proven blocked
    input point; missing evidence must surface as a real failure, never an
    environment skip. Headless: imports the capability module and drives the
    pure converter; launches nothing and consumes no run artifacts.
#>

$ErrorActionPreference = 'Stop'
$failures = 0
function Check {
    param([bool] $Condition, [string] $Description)
    if ($Condition) { Write-Host "PASS: $Description" } else { Write-Host "FAIL: $Description"; $script:failures++ }
}

$modulePath = Join-Path (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')) 'verify\capabilities\windows-ui-automation-validation\UiAutomationValidation.psm1'
Import-Module $modulePath -Force

# Proven foreign foreground -> evidence-backed skip.
$foreign = Convert-UiaForegroundFailureToSkip -Id 'scenario-x' -DurationMs 5 -Detail "dual-pane setup failed: foreground precondition failed (foreground='Some Other App')"
Check ($null -ne $foreign -and $foreign.status -eq 'SKIPPED' -and $foreign.reason -eq 'interactive-foreground-unavailable') 'proven foreign foreground converts to an evidence-backed skip'

# FastFiles owns the foreground -> real failure, never a skip.
$owning = Convert-UiaForegroundFailureToSkip -Id 'scenario-x' -DurationMs 5 -Detail "foreground precondition failed (foreground='FastFiles')"
Check ($null -eq $owning) 'FastFiles-owned foreground failure stays a hard FAIL'

# P1 regression guard: precondition text WITHOUT a foreground capture is
# missing evidence, not an environment block.
$noCapture = Convert-UiaForegroundFailureToSkip -Id 'scenario-x' -DurationMs 5 -Detail 'address bar edit did not appear after Ctrl+L'
Check ($null -eq $noCapture) 'precondition without a foreground capture cannot skip (missing evidence)'

# The JSON foreign marker proves ownership without the foreground='...' form.
$jsonForeign = Convert-UiaForegroundFailureToSkip -Id 'scenario-x' -DurationMs 5 -Detail 'palette edit (6301) did not appear; observed "foregroundTitle":"<foreign>"'
Check ($null -ne $jsonForeign -and $jsonForeign.status -eq 'SKIPPED') 'JSON foreign marker converts to a skip'

# Ownerless desktop is an environment block only when the observation itself
# succeeded. An unavailable query must remain a hard failure.
$ownerless = Convert-UiaForegroundFailureToSkip -Id 'scenario-x' -DurationMs 5 -Detail "foreground could not be established (foreground=''; foregroundEvidenceAvailable=true)"
Check ($null -ne $ownerless -and $ownerless.status -eq 'SKIPPED') 'proven ownerless foreground converts to a skip'
$unavailable = Convert-UiaForegroundFailureToSkip -Id 'scenario-x' -DurationMs 5 -Detail "foreground could not be established (foreground='<unavailable>'; foregroundEvidenceAvailable=false)"
Check ($null -eq $unavailable) 'unavailable foreground evidence cannot become a skip'
$none = Convert-UiaForegroundFailureToSkip -Id 'scenario-x' -DurationMs 5 -Detail "foreground could not be established (foreground='<none>'; foregroundEvidenceAvailable=true)"
Check ($null -ne $none -and $none.status -eq 'SKIPPED') 'observed ownerless foreground converts to a skip'
$topmostUnknown = Convert-UiaForegroundFailureToSkip -Id 'scenario-x' -DurationMs 5 -Detail 'point 10,20 is not inside FastFiles; topmost=unavailable'
Check ($null -eq $topmostUnknown) 'unavailable topmost observation cannot become a skip'
$thrownNavigation = Invoke-UiaAddressBarNavigateResult -Driver $null -Main $null -ProcessId 0 -Path 'C:\fixture' -ExpectedLeaf 'fixture' -NavigationInvoker { throw 'synthetic navigation failure' }
Check (-not $thrownNavigation.Pass -and $thrownNavigation.Detail -match 'address navigation helper failed') 'navigation-helper exceptions are converted to the result contract'

# A blocked input point is its own evidence path, independent of precondition text.
$pointBlocked = Convert-UiaForegroundFailureToSkip -Id 'scenario-x' -DurationMs 5 -Detail 'click point 10,20 is not inside FastFiles; topmost=OtherWnd'
Check ($null -ne $pointBlocked -and $pointBlocked.status -eq 'SKIPPED') 'blocked input point converts to a skip'

# A product failure never converts, even when a foreign-looking capture is present.
$product = Convert-UiaForegroundFailureToSkip -Id 'scenario-x' -DurationMs 5 -Detail "expected row missing (foreground='OtherApp')"
Check ($null -eq $product) 'non-precondition product failure never converts'

# Invariant (hardening-campaign Phase 4): interactive-foreground failure must
# never silently become PASS. The converter's only constructible result is a
# SKIPPED subresult; everything else is $null and the caller records a hard
# FAIL. Assert the whole evidence set contains no PASS anywhere.
$noPassByForeground = $true
foreach ($outcome in @($foreign, $owning, $noCapture, $jsonForeign, $ownerless, $unavailable, $pointBlocked, $product)) {
    if ($null -ne $outcome -and $outcome.status -eq 'PASS') { $noPassByForeground = $false }
}
Check $noPassByForeground 'foreground failure never converts into PASS (skip-or-fail only)'

# --- Machine-readable skip reason -> primary class registry (hardening-campaign Phase 4) ---
# No free-form-only skip explanations: every SKIPPED reason code the capability
# can emit must resolve to exactly one primary class in the tracked registry,
# bound by schema. Coverage is extracted from the capability source so a new
# emitter fails this test until it is classified.
$repoRootPath = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
$registryPath = Join-Path $repoRootPath 'verify\capabilities\windows-ui-automation-validation\skip-reason-classes.json'
$schemaPath = Join-Path $repoRootPath 'verify\schemas\skip-reason-classes.schema.json'
Check (Test-Path -LiteralPath $schemaPath) 'skip reason class schema contract exists'
Check (Test-Path -LiteralPath $registryPath) 'skip reason class registry exists'
$registry = Get-Content -LiteralPath $registryPath -Raw | ConvertFrom-Json
$expectedClasses = @('AUTOMATION_LIMITATION', 'CAPABILITY_UNAVAILABLE', 'ENVIRONMENT_UNSUPPORTED', 'FLAKY', 'HARNESS_DEFECT', 'PRODUCT_DEFECT', 'UNKNOWN')
Check ($registry.schemaVersion -eq 1) 'registry carries schemaVersion 1'
Check ((@($registry.classes | Sort-Object) -join '|') -eq ($expectedClasses -join '|')) 'classes enum is exactly the closed 7-class taxonomy'
$reasons = $registry.reasons
$registryKeys = @($reasons.PSObject.Properties.Name)
Check ($registryKeys.Count -gt 0) 'registry binds at least one reason code'
$structureOk = $true
foreach ($key in $registryKeys) {
    if ($key -notmatch '^[a-z][a-z0-9]*(-[a-z0-9]+)*$') {
        $structureOk = $false
        Write-Host "FAIL: registry key '$key' is not a kebab-case reason code"
    }
    $entry = $reasons.$key
    if ($expectedClasses -notcontains $entry.primaryClass) {
        $structureOk = $false
        Write-Host "FAIL: registry key '$key' maps to non-enum class '$($entry.primaryClass)'"
    }
    if ([string]::IsNullOrWhiteSpace($entry.notes)) {
        $structureOk = $false
        Write-Host "FAIL: registry key '$key' has no notes"
    }
}
Check $structureOk 'every registry entry is kebab-case, enum-classed, and annotated'

# Coverage direction 1: every inline SKIPPED reason literal in the capability
# source must be classified. Handles both the -Status/-Reason parameter form
# (possibly split across a backtick continuation) and the Status = ... form.
$moduleText = Get-Content -LiteralPath $modulePath -Raw
$inlineReasons = @{}
foreach ($m in [regex]::Matches($moduleText, "-Status 'SKIPPED'[\s\S]{0,64}?-Reason '([a-z0-9-]+)'")) {
    $inlineReasons[$m.Groups[1].Value] = $true
}
foreach ($m in [regex]::Matches($moduleText, "Status = 'SKIPPED';\s*Reason = '([a-z0-9-]+)'")) {
    $inlineReasons[$m.Groups[1].Value] = $true
}
Check ($inlineReasons.Count -gt 0) 'inline SKIPPED reason extraction found emitters'
foreach ($r in ($inlineReasons.Keys | Sort-Object)) {
    Check ($registryKeys -contains $r) "inline SKIPPED reason '$r' is classified in the registry"
}

# Coverage direction 2: var-based SKIPPED reasons (status and reason assigned
# through locals before composition). Each is bound to its source assignment so
# the hardcoded watch list cannot silently drift from the capability.
$varReasons = @{
    'connected-state-observed' = '\$badgeReason = if \('
    'engine-connected-host'    = '\$earlyErrorReason = ''engine-connected-host'''
}
foreach ($r in ($varReasons.Keys | Sort-Object)) {
    Check ($registryKeys -contains $r) "var-based SKIPPED reason '$r' is classified in the registry"
    Check ($moduleText -match $varReasons[$r]) "var-based reason '$r' assignment still present in capability source"
}

# Coverage direction 3 (staleness): every registry key must be emittable.
$emittable = @{}
foreach ($k in $inlineReasons.Keys) { $emittable[$k] = $true }
foreach ($k in $varReasons.Keys) { $emittable[$k] = $true }
foreach ($r in $registryKeys) {
    Check ($emittable.ContainsKey($r)) "registry reason '$r' is emittable by the capability source (no stale entries)"
}

# --- 4.2 deterministic privileged-scan eligibility (hardening-campaign Phase 4) ---
$elig = Resolve-UiaPrivilegedScanEligibility
Check ($elig.PSObject.Properties['Absences'] -ne $null) 'privileged eligibility probe returns absence codes'
$absenceEnum = @($registry.reasons.'privileged-scan-unavailable'.absenceCodes)
Check ($absenceEnum.Count -eq 6) 'registry declares the six privileged absence codes'
foreach ($code in @($elig.Absences)) {
    Check ($absenceEnum -contains $code) "observed absence '$code' is registry-classified"
}
Check (($elig.Absences -contains 'clean-host-requirement') -and ($elig.Absences -contains 'host-contamination')) 'policy absences always hold while the privileged campaign is open'
Check ($elig.Eligible -eq $false) 'this host is deterministically ineligible for privileged scan'
Check ($registry.reasons.'privileged-scan-unavailable'.canonicalCode -eq 'PRIVILEGED_SCAN_CAPABILITY_UNAVAILABLE') 'registry binds the canonical reason code'
Check ((Get-Content -LiteralPath $schemaPath -Raw) -match 'absenceCodes') 'schema contract covers absence codes'
$detail42 = Get-UiaPrivilegedSkipDetail -Eligibility $elig
Check ($detail42 -match '(?m)^SKIP$') 'expected form line 1 is SKIP'
Check ($detail42 -match '(?m)^reason=PRIVILEGED_SCAN_CAPABILITY_UNAVAILABLE$') 'expected form line 2 carries the canonical reason'
Check ($detail42 -match '(?m)^evidence=absence=[a-z0-9,-]+;service=') 'expected form line 3 carries machine evidence facts'
$moduleText42 = Get-Content -LiteralPath $modulePath -Raw
Check ($moduleText42 -match 'reason=PRIVILEGED_SCAN_CAPABILITY_UNAVAILABLE') 'capability source emits the canonical expected form'
Check ($moduleText42 -match 'privileged-eligibility-inconsistent') 'eligible-inversion guard fails loud instead of skipping'
Check (($moduleText42 -notmatch 'toggling service availability while search is open cannot be meaningfully exercised') -and ($moduleText42 -notmatch 'the mid-scan "Calculating..." path cannot be exercised')) 'legacy free-form privileged skip details are gone'

# --- Reviewer-C integrity guards (adversarial holes H-SKIP/H-FAIL/H-COORD) ---
$capText = Get-Content -LiteralPath $modulePath -Raw
Check ($capText -notmatch 'GetSystemMetrics\(0\)|GetSystemMetrics\(1\)') 'capability placement/input never reads primary-monitor metrics (virtual screen only)'
Check ($capText -match 'return & \$fail "return to root failed"') 'return-to-root failure returns its result (no swallowed fall-through)'
$mtGuard = [regex]::Match($capText, 'button input \(foreground=')
Check $mtGuard.Success 'mouse-tab foreground skip records the foreground owner'
$diGuard = [regex]::Match($capText, 'attempt=\$dragAttempts Explorer foreground could not be established')
Check $diGuard.Success 'drag-in foreground skip records the per-attempt foreground blocker'
Check ($capText -match 'acceptedInputAttempts -eq 3') 'drag-in automation skip requires three accepted-input attempts'
Check ($capText -match 'Connection badge observed: degradedVisible=') 'badge skip records the observed degraded fact'
Check ($capText -match 'engineConnected=') 'badge skip records the engine-connected fact'

# --- Residual-campaign Phase E: shared-session compose sites consult the converter ---
foreach ($sharedId in @(
    'keyboard-address-bar-navigation',
    'selection-and-scroll-patterns',
    'search-results-discoverable',
    'search-structured-filter-query',
    'search-panel-open',
    'scroll-pattern-real-viewport',
    'settings-dialog-keyboard-navigation'
)) {
    Check ($capText -match ("Convert-UiaForegroundFailureToSkip -Id '" + $sharedId + "'")) "shared-session site consults the converter for '$sharedId'"
}

# --- Residual-campaign Phase D (R4 ranked fixes) ---
Check ($capText.Contains("ForegroundTitle -like 'FastFiles*'")) 'Safe-state maps ownership by FastFiles* prefix (product retitle cannot become foreign)'
Check ($capText.Contains("-notlike 'FastFiles*'")) 'converter uses prefix ownership on raw captures (false-skip hole closed)'
$ensureIdx = $capText.IndexOf('function Ensure-UiaInputForeground')
$ensureForIdx = if ($ensureIdx -ge 0) { $capText.IndexOf('for ($attempt', $ensureIdx) } else { -1 }
$ensureTopIdx = if ($ensureForIdx -ge 0) { $capText.IndexOf('Keep-UiaWindowTopmost', $ensureForIdx) } else { -1 }
Check ($ensureIdx -ge 0 -and $ensureForIdx -gt $ensureIdx -and $ensureTopIdx -gt $ensureForIdx -and ($ensureTopIdx - $ensureIdx) -lt 800) 'Ensure re-pulses TOPMOST inside its attempt loop'
Check (([regex]::Matches($capText, [regex]::Escape('Ensure-UiaInputForeground -Driver $driver -Main $main -Attempts 3'))).Count -ge 2) 'context verifies foreground before AND after the engine-connect poll'
Check ($capText.Contains('residual-campaign Phase D (R4 rank1): scroll the terminal')) 'Activate-UiaPane scrolls the terminal column before the focus click'
Check ($capText.Contains("did not establish HasKeyboardFocus; foreground='")) 'Activate failure detail keeps the converter-compatible capture'
Check ($capText.Contains('$($safe.focusedAutomationId)')) 'Activate failure detail splits focus evidence (Safe-state facts)'
Check ($capText.Contains('Invoke-UiaAddressBarNavigateResult')) 'shared Search navigation uses a total-result wrapper'
Check ($capText.Contains('$searchFixtureNavClassified = $true')) 'failed Search fixture navigation classifies dependent scenarios before further input'
Check ($capText.Contains('-not $searchFixtureNavClassified')) 'dependent Search compose paths are suppressed after navigation failure'

if ($failures -gt 0) { exit 1 }
Write-Host 'All foreground skip-conversion and skip-reason-class checks passed.'
exit 0
