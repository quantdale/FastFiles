<#
    Capability registry (tasks 1.2/1.3): scans verify/capabilities/*/capability.json,
    validates each manifest against the capability-interface schema, loads only
    manifests whose interfaceVersion major is within the core's supported range, and
    records an incompatible/invalid manifest as a load diagnostic rather than a
    silent drop (D11). Adding a capability is dropping in a conforming module — the
    core never special-cases a capability id.
#>

Import-Module (Join-Path $PSScriptRoot 'Schema.psm1') -Force -Global

# Supported capability interfaceVersion major versions. Bump when the core makes a
# breaking change to the capability contract; capabilities declaring a major outside
# this set are refused (see design.md D11, open question "interface version policy").
$script:SupportedInterfaceMajors = @(1)

function Test-InterfaceVersionSupported {
    param([string] $InterfaceVersion)
    if ($InterfaceVersion -notmatch '^(\d+)\.\d+\.\d+$') { return $false }
    $major = [int]$Matches[1]
    return $script:SupportedInterfaceMajors -contains $major
}

function Find-Capabilities {
    <#
        Returns @{ Capabilities = [...]; LoadDiagnostics = [...] }. Capabilities is an
        array of objects exposing the resolved functions (Availability/Run/Diagnostics
        /Baseline/RepairHints) so the caller never touches module internals directly.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $CapabilitiesRoot,
        [string] $ManifestSchemaPath
    )

    $capabilities = @()
    $loadDiagnostics = @()

    if (-not (Test-Path $CapabilitiesRoot)) {
        return [pscustomobject]@{ Capabilities = $capabilities; LoadDiagnostics = $loadDiagnostics }
    }

    $schema = $null
    if ($ManifestSchemaPath -and (Test-Path $ManifestSchemaPath)) {
        $schema = Get-Content $ManifestSchemaPath -Raw | ConvertFrom-Json
    }

    $manifestFiles = Get-ChildItem -Path $CapabilitiesRoot -Filter 'capability.json' -Recurse
    foreach ($manifestFile in $manifestFiles) {
        $capDir = $manifestFile.Directory.FullName
        $raw = Get-Content $manifestFile.FullName -Raw
        try {
            $manifest = $raw | ConvertFrom-Json
        } catch {
            $loadDiagnostics += [pscustomobject]@{
                capabilityPath = $manifestFile.FullName
                reason         = "invalid-json: $($_.Exception.Message)"
            }
            continue
        }

        if ($schema) {
            $validation = Test-JsonSchema -Data $manifest -Schema $schema
            if (-not $validation.Valid) {
                $loadDiagnostics += [pscustomobject]@{
                    capabilityId   = $manifest.id
                    capabilityPath = $manifestFile.FullName
                    reason         = "manifest-schema-invalid: $($validation.Errors -join '; ')"
                }
                continue
            }
        }

        if (-not (Test-InterfaceVersionSupported -InterfaceVersion $manifest.interfaceVersion)) {
            $loadDiagnostics += [pscustomobject]@{
                capabilityId   = $manifest.id
                capabilityPath = $manifestFile.FullName
                reason         = "unsupported-interface-version: capability declares '$($manifest.interfaceVersion)', core supports majors [$($script:SupportedInterfaceMajors -join ',')]"
            }
            continue
        }

        $modulePath = Join-Path $capDir $manifest.entryModule
        if (-not (Test-Path $modulePath)) {
            $loadDiagnostics += [pscustomobject]@{
                capabilityId   = $manifest.id
                capabilityPath = $manifestFile.FullName
                reason         = "entry-module-missing: '$modulePath' does not exist"
            }
            continue
        }

        try {
            Import-Module $modulePath -Force -Global -ErrorAction Stop
        } catch {
            $loadDiagnostics += [pscustomobject]@{
                capabilityId   = $manifest.id
                capabilityPath = $manifestFile.FullName
                reason         = "module-import-failed: $($_.Exception.Message)"
            }
            continue
        }

        $entryPoints = $manifest.entryPoints
        $missingFns = @()
        foreach ($required in @('availability', 'run', 'diagnostics')) {
            $fnName = $entryPoints.$required
            if (-not $fnName -or -not (Get-Command $fnName -ErrorAction SilentlyContinue)) {
                $missingFns += "$required -> '$fnName'"
            }
        }
        if ($missingFns.Count -gt 0) {
            $loadDiagnostics += [pscustomobject]@{
                capabilityId   = $manifest.id
                capabilityPath = $manifestFile.FullName
                reason         = "entry-point-functions-missing: $($missingFns -join '; ')"
            }
            continue
        }

        $capabilities += [pscustomobject]@{
            Id               = $manifest.id
            InterfaceVersion = $manifest.interfaceVersion
            Tier             = $manifest.tier
            DisplayName      = $manifest.displayName
            Description      = $manifest.description
            ModulePath       = $modulePath
            CapabilityDir    = $capDir
            Manifest         = $manifest
            Fn               = [pscustomobject]@{
                Availability = $entryPoints.availability
                Run          = $entryPoints.run
                Diagnostics  = $entryPoints.diagnostics
                Baseline     = $entryPoints.baseline
                RepairHints  = $entryPoints.repairHints
            }
        }
    }

    [pscustomobject]@{
        Capabilities    = $capabilities
        LoadDiagnostics = $loadDiagnostics
    }
}

Export-ModuleMember -Function Find-Capabilities, Test-InterfaceVersionSupported
