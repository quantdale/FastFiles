[CmdletBinding()]
param(
    [string] $RepoRoot = (Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent),
    [string] $BuildRoot
)

$ErrorActionPreference = 'Stop'
if (-not $BuildRoot) { $BuildRoot = Join-Path $RepoRoot 'build\release' }
Import-Module (Join-Path (Split-Path $PSScriptRoot -Parent) 'ReleaseCommon.psm1') -Force -DisableNameChecking
$releaseCommonModule = Get-Module | Where-Object {
    $_.Path -eq (Resolve-Path (Join-Path (Split-Path $PSScriptRoot -Parent) 'ReleaseCommon.psm1')).Path
} | Select-Object -First 1

$failures = 0
function Check {
    param([bool] $Condition, [string] $Description)
    if ($Condition) { Write-Host "PASS: $Description" } else { Write-Host "FAIL: $Description"; $script:failures++ }
}

function Get-TestTechnicalCertificatePolicy {
    param(
        [Parameter(Mandatory)] [Security.Cryptography.X509Certificates.X509Certificate2] $Certificate,
        [Parameter(Mandatory)] [Security.Cryptography.X509Certificates.X509Certificate2] $CustomTrustRoot
    )

    if ($null -eq $script:releaseCommonModule) { throw 'RELEASE_COMMON_MODULE_NOT_FOUND_FOR_TEST_SEAM' }
    return & $script:releaseCommonModule {
        param($TestCertificate, $TestRoot)
        Get-ProductionCertificatePolicyForTest -Certificate $TestCertificate -CustomTrustRoot $TestRoot
    } $Certificate $CustomTrustRoot
}

function Add-TestCertificateToMyStore {
    param([Parameter(Mandatory)] [Security.Cryptography.X509Certificates.X509Certificate2] $Certificate)

    $store = [Security.Cryptography.X509Certificates.X509Store]::new('My', 'CurrentUser')
    try {
        $store.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
        $store.Add($Certificate)
    } finally {
        $store.Close()
        $store.Dispose()
    }
}

function Remove-TestCertificateThumbprint {
    param(
        [Parameter(Mandatory)] [ValidateSet('My', 'Root')] [string] $StoreName,
        [AllowNull()] [string] $Thumbprint
    )

    if ([string]::IsNullOrWhiteSpace($Thumbprint)) { return }
    $normalized = Normalize-Thumbprint -Value $Thumbprint
    $store = [Security.Cryptography.X509Certificates.X509Store]::new($StoreName, 'CurrentUser')
    try {
        $store.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
        foreach ($certificate in @($store.Certificates)) {
            if ((Normalize-Thumbprint -Value $certificate.Thumbprint) -ceq $normalized) {
                $store.Remove($certificate)
            }
        }
    } finally {
        $store.Close()
        $store.Dispose()
    }
}

function Test-TestCertificateAbsent {
    param(
        [Parameter(Mandatory)] [ValidateSet('My', 'Root')] [string] $StoreName,
        [AllowNull()] [string] $Thumbprint
    )

    if ([string]::IsNullOrWhiteSpace($Thumbprint)) { return $true }
    $normalized = Normalize-Thumbprint -Value $Thumbprint
    $store = [Security.Cryptography.X509Certificates.X509Store]::new($StoreName, 'CurrentUser')
    try {
        $store.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadOnly)
        return @($store.Certificates | Where-Object {
            (Normalize-Thumbprint -Value $_.Thumbprint) -ceq $normalized
        }).Count -eq 0
    } finally {
        $store.Close()
        $store.Dispose()
    }
}

function New-TestCertificateFixture {
    param(
        [Parameter(Mandatory)] [ValidateSet('SelfSigned', 'UntrustedCodeSigning', 'TrustedNoEku', 'TrustedExpired', 'TrustedNotYetValid', 'TrustedValid')] [string] $Case
    )

    $tag = [guid]::NewGuid().ToString('N')
    $now = Get-Date
    $root = $null
    $leaf = $null
    # The test root stays only in CurrentUser\My. The valid technical case
    # uses the private custom-root chain seam; no host trust store is changed.
    $trusted = $false
    try {
        if ($Case -ne 'SelfSigned') {
            $root = New-SelfSignedCertificate -Subject "CN=FastFiles TEST CERTIFICATE NON-PRODUCTION root $tag" -Type Custom `
                -TextExtension '2.5.29.19={text}CA=TRUE&pathlength=1' `
                -KeyUsage CertSign, CRLSign, DigitalSignature -CertStoreLocation 'Cert:\CurrentUser\My' `
                -KeyExportPolicy Exportable -NotBefore $now.AddDays(-2) -NotAfter $now.AddDays(30)
        }

        $leafNotBefore = $now.AddDays(-1)
        $leafNotAfter = $now.AddDays(7)
        if ($Case -eq 'TrustedExpired') {
            $leafNotBefore = $now.AddDays(-3)
            $leafNotAfter = $now.AddDays(-1)
        } elseif ($Case -eq 'TrustedNotYetValid') {
            $leafNotBefore = $now.AddDays(1)
            $leafNotAfter = $now.AddDays(2)
        }

        if ($Case -eq 'TrustedNoEku') {
            $leaf = New-SelfSignedCertificate -Subject "CN=FastFiles TEST CERTIFICATE NON-PRODUCTION no-eku $tag" `
                -Type Custom -Signer $root -KeyUsage DigitalSignature -CertStoreLocation 'Cert:\CurrentUser\My' `
                -KeyExportPolicy Exportable -NotBefore $leafNotBefore -NotAfter $leafNotAfter
        } elseif ($Case -eq 'SelfSigned') {
            $leaf = New-SelfSignedCertificate -Subject "CN=FastFiles TEST CERTIFICATE NON-PRODUCTION self-signed $tag" `
                -Type CodeSigningCert -CertStoreLocation 'Cert:\CurrentUser\My' -KeyExportPolicy Exportable `
                -NotBefore $leafNotBefore -NotAfter $leafNotAfter
        } else {
            $leaf = New-SelfSignedCertificate -Subject "CN=FastFiles TEST CERTIFICATE NON-PRODUCTION code-signing $tag" `
                -Type CodeSigningCert -Signer $root -CertStoreLocation 'Cert:\CurrentUser\My' `
                -KeyExportPolicy Exportable -NotBefore $leafNotBefore -NotAfter $leafNotAfter
        }

        [pscustomobject]@{
            case = $Case
            label = 'TEST CERTIFICATE / NON-PRODUCTION'
            root = $root
            leaf = $leaf
            rootTrusted = $trusted
        }
    } catch {
        if ($leaf) { Remove-TestCertificateThumbprint -StoreName My -Thumbprint $leaf.Thumbprint }
        if ($root) {
            Remove-TestCertificateThumbprint -StoreName My -Thumbprint $root.Thumbprint
        }
        throw
    }
}

function Remove-TestCertificateFixture {
    param([AllowNull()] $Fixture)

    if ($null -eq $Fixture) { return $true }
    $rootThumbprint = if ($Fixture.root) { $Fixture.root.Thumbprint } else { $null }
    $leafThumbprint = if ($Fixture.leaf) { $Fixture.leaf.Thumbprint } else { $null }
    if ($Fixture.rootTrusted) { Remove-TestCertificateThumbprint -StoreName Root -Thumbprint $rootThumbprint }
    Remove-TestCertificateThumbprint -StoreName My -Thumbprint $leafThumbprint
    Remove-TestCertificateThumbprint -StoreName My -Thumbprint $rootThumbprint
    return (Test-TestCertificateAbsent -StoreName My -Thumbprint $leafThumbprint) -and
        (Test-TestCertificateAbsent -StoreName My -Thumbprint $rootThumbprint) -and
        (Test-TestCertificateAbsent -StoreName Root -Thumbprint $rootThumbprint)
}

function Invoke-SignReleaseDryRun {
    param(
        [Parameter(Mandatory)] $Fixture,
        [Parameter(Mandatory)] [string] $ExpectedSignerThumbprint
    )

    $signScript = Join-Path $RepoRoot 'verify\release\sign-release.ps1'
    $output = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $signScript `
        -ArtifactRoot $BuildRoot -Provider CertificateStore -CertificateThumbprint $Fixture.leaf.Thumbprint `
        -ExpectedSignerThumbprint $ExpectedSignerThumbprint -SigntoolPath $env:ComSpec `
        -DryRun -OutputFormat json 2>&1
    $exitCode = $LASTEXITCODE
    $result = $null
    try { $result = ($output -join "`n") | ConvertFrom-Json } catch { }
    [pscustomobject]@{ exitCode = $exitCode; result = $result; output = ($output -join "`n") }
}

$manifest = Get-ReleaseArtifactManifest -ArtifactRoot $BuildRoot -RepoRoot $RepoRoot
$manifestAgain = Get-ReleaseArtifactManifest -ArtifactRoot $BuildRoot -RepoRoot $RepoRoot
Check (@($manifest.artifacts).Count -eq 4) 'manifest enumerates exactly four release artifacts'
Check (@($manifest.artifacts | Where-Object architecture -ne 'x64').Count -eq 0) 'all release artifacts are x64'
Check ((@($manifest.artifacts | ForEach-Object sha256 | Where-Object { $_ -notmatch '^[0-9A-F]{64}$' }).Count) -eq 0) 'all artifact hashes are uppercase SHA-256'
Check (($manifest.commit -match '^[0-9a-f]{40}$')) 'manifest records the current Git commit'
Check (@($manifest.artifacts | Where-Object { $_.buildIdentity -notmatch '^[0-9a-f]{40}$' }).Count -eq 0) 'all artifacts carry a canonical build identity'
Check (@($manifest.artifacts | Where-Object { $_.componentRole -notin @('ui', 'engine', 'service', 'installer') }).Count -eq 0) 'all artifacts carry a distinct component role'
Check ((@($manifest.artifacts | ForEach-Object { $_ | ConvertTo-Json -Depth 8 }) -join '|') -eq (@($manifestAgain.artifacts | ForEach-Object { $_ | ConvertTo-Json -Depth 8 }) -join '|')) 'manifest artifact records are deterministic'
$expectedProductVersion = Get-RepositoryProductVersion -RepoRoot $RepoRoot
$expectedFileVersion = "$expectedProductVersion.0"
$versionCheck = Test-ReleaseVersionMetadata -ArtifactRoot $BuildRoot -RepoRoot $RepoRoot
Check ($versionCheck.valid) 'all release executables contain canonical VERSIONINFO metadata'
Check (@($manifest.artifacts | Where-Object { $_.productVersion -cne $expectedProductVersion -or $_.fileVersion -cne $expectedFileVersion -or $_.productName -cne 'FastFiles' -or $_.companyName -cne 'FastFiles' -or $_.originalFilename -ine $_.filename -or $_.internalName -ine $_.filename -or $_.architecture -cne 'x64' }).Count -eq 0) 'artifact manifest records canonical version and identity fields'
Check (-not (Test-StrictProductVersion -Value '01.2.3')) 'leading-zero semantic-version components are rejected'
Check (Test-StrictProductVersion -Value '0.2.3') 'zero is accepted as the canonical zero semantic-version component'

Check ((Get-PinState -Name 'zero' -Value ('0' * 40)).state -eq 'PLACEHOLDER') 'all-zero pin is classified as PLACEHOLDER'
Check ((Get-PinState -Name 'bad' -Value '1234').state -eq 'MALFORMED') 'short pin is classified as MALFORMED'
Check ((Get-PinState -Name 'good' -Value ('A' * 40)).state -eq 'POPULATED') '40-hex pin is classified as POPULATED'
Check ((Test-ThumbprintFormat -Value 'aa-aa-aa-aa-aa-aa-aa-aa-aa-aa-aa-aa-aa-aa-aa-aa-aa-aa-aa-aa')) 'separator normalization accepts a 20-byte thumbprint'
Check (-not (Test-ThumbprintFormat -Value 'not-a-thumbprint')) 'invalid thumbprint is rejected'
Check (-not (Test-ProductionCertificate -Certificate $null)) 'missing production certificate is rejected'

$certificatePolicyFixtures = @(
    [pscustomobject]@{ case = 'SelfSigned'; expectedReason = 'CERTIFICATE_SELF_SIGNED'; expectedValid = $false; customRoot = $false },
    [pscustomobject]@{ case = 'UntrustedCodeSigning'; expectedReason = 'CERTIFICATE_CHAIN_UNTRUSTED_OR_INVALID'; expectedValid = $false; customRoot = $false },
    [pscustomobject]@{ case = 'TrustedNoEku'; expectedReason = 'CERTIFICATE_CODE_SIGNING_EKU_MISSING'; expectedValid = $false; customRoot = $true },
    [pscustomobject]@{ case = 'TrustedExpired'; expectedReason = 'CERTIFICATE_OUTSIDE_CURRENT_VALIDITY'; expectedValid = $false; customRoot = $true },
    [pscustomobject]@{ case = 'TrustedNotYetValid'; expectedReason = 'CERTIFICATE_OUTSIDE_CURRENT_VALIDITY'; expectedValid = $false; customRoot = $true },
    [pscustomobject]@{ case = 'TrustedValid'; expectedReason = $null; expectedValid = $true; customRoot = $true }
)
foreach ($fixtureDefinition in $certificatePolicyFixtures) {
    $fixture = $null
    try {
        $fixture = New-TestCertificateFixture -Case $fixtureDefinition.case
        $policy = if ($fixtureDefinition.customRoot) {
            Get-TestTechnicalCertificatePolicy -Certificate $fixture.leaf -CustomTrustRoot $fixture.root
        } else {
            Get-ProductionCertificatePolicy -Certificate $fixture.leaf
        }
        $reasonPresent = $fixtureDefinition.expectedReason -and $policy.reasons -contains $fixtureDefinition.expectedReason
        $technicalResult = $policy.valid -eq $fixtureDefinition.expectedValid
        if ($fixtureDefinition.case -eq 'TrustedValid') {
            Check ($technicalResult -and $policy.chainValid -and $policy.codeSigningEku -and
                $policy.currentlyValid -and $policy.chainTrustMode -eq 'CUSTOM_ROOT_TEST_ONLY' -and
                $policy.revocationPolicy -eq 'NOT_CHECKED_OFFLINE' -and
                $fixture.label -eq 'TEST CERTIFICATE / NON-PRODUCTION') `
                'trusted test root with a currently valid Code Signing leaf passes only the technical TEST CERTIFICATE / NON-PRODUCTION policy seam'
        } else {
            Check ($technicalResult -and $reasonPresent) `
                "production certificate policy rejects $($fixtureDefinition.case)"
        }
    } catch {
        Check $false "production certificate policy fixture $($fixtureDefinition.case) completed without exception ($($_.Exception.Message))"
    } finally {
        if ($fixture) {
            $cleaned = Remove-TestCertificateFixture -Fixture $fixture
            Check $cleaned "temporary $($fixtureDefinition.case) certificate fixture and CurrentUser trust entry are cleaned"
        }
    }
}

$signerPolicyFixture = $null
try {
    $signerPolicyFixture = New-TestCertificateFixture -Case TrustedValid
    $wrongExpected = if ($signerPolicyFixture.leaf.Thumbprint[0] -ceq 'A') {
        'B' + $signerPolicyFixture.leaf.Thumbprint.Substring(1)
    } else {
        'A' + $signerPolicyFixture.leaf.Thumbprint.Substring(1)
    }
    $wrongExpectedResult = Invoke-SignReleaseDryRun -Fixture $signerPolicyFixture -ExpectedSignerThumbprint $wrongExpected
    Check ($wrongExpectedResult.exitCode -eq 1 -and $wrongExpectedResult.result -and
        $wrongExpectedResult.result.error -like 'EXPECTED_SIGNER_MISMATCH*') `
        'wrong expected signer thumbprint fails before any signing operation'
    $placeholderExpectedResult = Invoke-SignReleaseDryRun -Fixture $signerPolicyFixture -ExpectedSignerThumbprint ('0' * 40)
    Check ($placeholderExpectedResult.exitCode -eq 1 -and $placeholderExpectedResult.result -and
        $placeholderExpectedResult.result.error -like 'MALFORMED_EXPECTED_SIGNER*') `
        'placeholder expected signer thumbprint fails closed'
    $malformedExpectedResult = Invoke-SignReleaseDryRun -Fixture $signerPolicyFixture -ExpectedSignerThumbprint 'not-a-thumbprint'
    Check ($malformedExpectedResult.exitCode -eq 1 -and $malformedExpectedResult.result -and
        $malformedExpectedResult.result.error -like 'MALFORMED_EXPECTED_SIGNER*') `
        'malformed expected signer thumbprint fails closed'

    $manifestPolicyRoot = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-manifest-policy-test-' + [guid]::NewGuid().ToString('N'))
    $manifestPolicyPath = Join-Path $manifestPolicyRoot 'FastFiles.package.manifest'
    $manifestPolicySignaturePath = Join-Path $manifestPolicyRoot 'FastFiles.package.manifest.p7s'
    try {
        New-Item -ItemType Directory -Path $manifestPolicyRoot -Force | Out-Null
        [IO.File]::WriteAllText($manifestPolicyPath, 'manifest-policy-probe', [Text.UTF8Encoding]::new($false))
        Add-Type -AssemblyName System.Security.Cryptography.Pkcs -ErrorAction Stop
        $contentInfo = [Security.Cryptography.Pkcs.ContentInfo]::new(
            [IO.File]::ReadAllBytes($manifestPolicyPath))
        $cms = [Security.Cryptography.Pkcs.SignedCms]::new($contentInfo, $true)
        $cmsSigner = [Security.Cryptography.Pkcs.CmsSigner]::new($signerPolicyFixture.leaf)
        $cms.ComputeSignature($cmsSigner)
        [IO.File]::WriteAllBytes($manifestPolicySignaturePath, $cms.Encode())
        $manifestPolicyResult = & $script:releaseCommonModule {
            param($manifest, $signature, $thumbprint, $root)
            Test-PackageManifestSignatureForTest -ManifestPath $manifest -SignaturePath $signature `
                -AllowedSignerThumbprints @($thumbprint) -CustomTrustRoot $root
        } $manifestPolicyPath $manifestPolicySignaturePath $signerPolicyFixture.leaf.Thumbprint $signerPolicyFixture.root
        Check ($manifestPolicyResult.valid -and
            $manifestPolicyResult.signerThumbprint -ceq $signerPolicyFixture.leaf.Thumbprint) `
            'detached CMS and the custom-root technical verifier require the exact Code Signing leaf'
    } finally {
        if (Test-Path -LiteralPath $manifestPolicyRoot -PathType Container) {
            Remove-Item -LiteralPath $manifestPolicyRoot -Recurse -Force -ErrorAction SilentlyContinue
        }
    }

    $publicOnly = [Security.Cryptography.X509Certificates.X509Certificate2]::new($signerPolicyFixture.leaf.RawData)
    Remove-TestCertificateThumbprint -StoreName My -Thumbprint $signerPolicyFixture.leaf.Thumbprint
    Add-TestCertificateToMyStore -Certificate $publicOnly
    $noPrivateKeyResult = Invoke-SignReleaseDryRun -Fixture $signerPolicyFixture -ExpectedSignerThumbprint $signerPolicyFixture.leaf.Thumbprint
    Check (-not $publicOnly.HasPrivateKey -and $noPrivateKeyResult.exitCode -eq 1 -and $noPrivateKeyResult.result -and
        $noPrivateKeyResult.result.error -like '*private key*') `
        'certificate without a private key fails the actual signing-provider preflight'
} catch {
    Check $false "signer thumbprint/private-key policy probes completed without exception ($($_.Exception.Message))"
} finally {
    if ($signerPolicyFixture) {
        $cleaned = Remove-TestCertificateFixture -Fixture $signerPolicyFixture
        Check $cleaned 'signer policy certificate fixture and trust entry are cleaned'
    }
}

$pfxProbeRoot = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-pfx-provider-test-' + [guid]::NewGuid().ToString('N'))
$pfxProbePath = Join-Path $pfxProbeRoot 'provider-test.pfx'
$pfxProbeRootCertificate = $null
$pfxProbeLeafCertificate = $null
$pfxProbeEnvironmentName = 'FASTFILES_TEST_PFX_PASSWORD_' + [guid]::NewGuid().ToString('N').ToUpperInvariant()
try {
    New-Item -ItemType Directory -Path $pfxProbeRoot -Force | Out-Null
    $pfxProbePasswordText = [guid]::NewGuid().ToString('N') + '!Pv'
    $pfxProbePassword = ConvertTo-SecureString -String $pfxProbePasswordText -AsPlainText -Force
    $pfxProbeRootCertificate = New-SelfSignedCertificate -Subject 'CN=FastFiles release-tooling test root' -Type Custom `
        -TextExtension '2.5.29.19={text}CA=TRUE&pathlength=1' `
        -KeyUsage CertSign, CRLSign, DigitalSignature -CertStoreLocation 'Cert:\CurrentUser\My' `
        -KeyExportPolicy Exportable -NotAfter (Get-Date).AddDays(1)
    $pfxProbeLeafCertificate = New-SelfSignedCertificate -Subject 'CN=FastFiles release-tooling test leaf' -Type CodeSigningCert `
        -Signer $pfxProbeRootCertificate -CertStoreLocation 'Cert:\CurrentUser\My' `
        -KeyExportPolicy Exportable -NotAfter (Get-Date).AddDays(1)
    Export-PfxCertificate -Cert $pfxProbeLeafCertificate -FilePath $pfxProbePath -Password $pfxProbePassword -ChainOption BuildChain | Out-Null
    [Environment]::SetEnvironmentVariable($pfxProbeEnvironmentName, $pfxProbePasswordText, 'Process')
    $signScript = Join-Path $RepoRoot 'verify\release\sign-release.ps1'
    $pfxProbeOutput = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $signScript `
        -ArtifactRoot $BuildRoot -Provider Pfx -PfxPath $pfxProbePath `
        -PfxPasswordEnvironmentVariable $pfxProbeEnvironmentName -SigntoolPath $env:ComSpec `
        -DryRun -OutputFormat json 2>&1
    $pfxProbeExit = $LASTEXITCODE
    $pfxProbeResult = $null
    try { $pfxProbeResult = ($pfxProbeOutput -join "`n") | ConvertFrom-Json } catch { }
    Check ($pfxProbeExit -eq 1 -and $pfxProbeResult -and $pfxProbeResult.status -eq 'FAIL' -and
        $pfxProbeResult.error -like 'DEVELOPMENT_CERTIFICATE_NOT_ALLOWED*CERTIFICATE_CHAIN_UNTRUSTED_OR_INVALID*') `
        'encrypted external PFX password is consumed before the non-production development chain is rejected by signer policy'

    $manifestProbePath = Join-Path $pfxProbeRoot 'FastFiles.package.manifest'
    $manifestProbeSignaturePath = Join-Path $pfxProbeRoot 'FastFiles.package.manifest.p7s'
    [IO.File]::WriteAllText($manifestProbePath, 'manifest-probe', [Text.UTF8Encoding]::new($false))
    $manifestProbeOutput = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $signScript `
        -ManifestOnly -Provider Pfx -PfxPath $pfxProbePath `
        -PfxPasswordEnvironmentVariable $pfxProbeEnvironmentName `
        -PackageManifestPath $manifestProbePath -ManifestSignatureOutputPath $manifestProbeSignaturePath `
        -DryRun -OutputFormat json 2>&1
    $manifestProbeExit = $LASTEXITCODE
    $manifestProbeResult = $null
    try { $manifestProbeResult = ($manifestProbeOutput -join "`n") | ConvertFrom-Json } catch { }
    Check ($manifestProbeExit -eq 1 -and $manifestProbeResult -and
        $manifestProbeResult.error -like 'DEVELOPMENT_CERTIFICATE_NOT_ALLOWED*CERTIFICATE_CHAIN_UNTRUSTED_OR_INVALID*' -and
        -not (Test-Path -LiteralPath $manifestProbeSignaturePath -PathType Leaf)) `
        'manifest-only signing preflight rejects the untrusted non-production leaf before any manifest bytes change'

    $unsafeManifestOutput = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $signScript `
        -ArtifactRoot $BuildRoot -Provider Pfx -PfxPath $pfxProbePath `
        -PfxPasswordEnvironmentVariable $pfxProbeEnvironmentName -SigntoolPath $env:ComSpec `
        -PackageManifestPath $manifestProbePath -ManifestSignatureOutputPath $manifestProbeSignaturePath `
        -DryRun -OutputFormat json 2>&1
    $unsafeManifestExit = $LASTEXITCODE
    $unsafeManifestResult = $null
    try { $unsafeManifestResult = ($unsafeManifestOutput -join "`n") | ConvertFrom-Json } catch { }
    Check ($unsafeManifestExit -eq 1 -and $unsafeManifestResult -and
        $unsafeManifestResult.error -like 'PACKAGE_MANIFEST_SIGNATURE_REQUIRES_MANIFEST_ONLY*') `
        'combined PE and manifest signing is rejected to prevent stale manifest hashes'
} catch {
    Check $false "encrypted external PFX provider dry-run completed without exception ($($_.Exception.Message))"
} finally {
    [Environment]::SetEnvironmentVariable($pfxProbeEnvironmentName, $null, 'Process')
    if ($pfxProbeLeafCertificate) { Remove-Item -LiteralPath ('Cert:\CurrentUser\My\' + $pfxProbeLeafCertificate.Thumbprint) -Force -ErrorAction SilentlyContinue }
    if ($pfxProbeRootCertificate) { Remove-Item -LiteralPath ('Cert:\CurrentUser\My\' + $pfxProbeRootCertificate.Thumbprint) -Force -ErrorAction SilentlyContinue }
    if ($pfxProbeLeafCertificate) { Remove-TestCertificateThumbprint -StoreName My -Thumbprint $pfxProbeLeafCertificate.Thumbprint }
    if ($pfxProbeRootCertificate) { Remove-TestCertificateThumbprint -StoreName My -Thumbprint $pfxProbeRootCertificate.Thumbprint }
    if ($pfxProbeLeafCertificate) { Check (Test-TestCertificateAbsent -StoreName My -Thumbprint $pfxProbeLeafCertificate.Thumbprint) 'encrypted PFX temporary leaf is absent from CurrentUser personal store after cleanup' }
    if ($pfxProbeRootCertificate) { Check (Test-TestCertificateAbsent -StoreName My -Thumbprint $pfxProbeRootCertificate.Thumbprint) 'encrypted PFX temporary root is absent from CurrentUser personal store after cleanup' }
    if (Test-Path -LiteralPath $pfxProbePath -PathType Leaf) { Remove-Item -LiteralPath $pfxProbePath -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath $pfxProbeRoot -PathType Container) { Remove-Item -LiteralPath $pfxProbeRoot -Recurse -Force -ErrorAction SilentlyContinue }
}

$pins = Get-ConfiguredPinStates -RepoRoot $RepoRoot -BuildRoot $BuildRoot
$matrix = @(Get-ReleaseSignatureMatrix -Manifest $manifest -PinStates $pins)
Check (@($matrix | Where-Object result -eq 'UNSIGNED — PRODUCTION SIGNING REQUIRED').Count -eq 4) 'unsigned Release binaries produce explicit production-signing blockers'
Check ($pins.engine.state -in @('PLACEHOLDER', 'POPULATED', 'MALFORMED', 'MISSING')) 'engine pin validator emits a known state'
Check ($pins.service.state -in @('PLACEHOLDER', 'POPULATED', 'MALFORMED', 'MISSING')) 'service pin validator emits a known state'
Check ((Get-ReleaseReadinessStatus -ArtifactStatus 'FAIL' -SigningStatus 'PASS' -PinsStatus 'PASS' -CleanHost $true -ArchitectureCompatible $true -Elevated $true) -eq 'FAIL') 'readiness status preserves local artifact failures'
Check ((Get-ReleaseReadinessStatus -ArtifactStatus 'PASS' -SigningStatus 'PASS' -PinsStatus 'PASS' -CleanHost $true -ArchitectureCompatible $true -Elevated $true) -eq 'PASS') 'readiness status can reach PASS when every production prerequisite passes'
Check ((Get-ReleaseReadinessStatus -ArtifactStatus 'PASS' -SigningStatus 'BLOCKED_EXTERNAL' -PinsStatus 'PASS' -CleanHost $true -ArchitectureCompatible $true -Elevated $true) -eq 'BLOCKED_EXTERNAL') 'readiness status preserves external signing blockers'

$readinessProbeRoot = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-readiness-root-test-' + [guid]::NewGuid().ToString('N'))
try {
    $readinessProbeOutput = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File (Join-Path $RepoRoot 'verify\release\ReleaseReadiness.ps1') `
        -Mode report -ArtifactRoot $BuildRoot -OutputDirectory $readinessProbeRoot -OutputFormat json 2>&1
    $readinessProbeExit = $LASTEXITCODE
    $readinessProbeResult = $null
    try { $readinessProbeResult = ($readinessProbeOutput -join "`n") | ConvertFrom-Json } catch { }
    Check ($readinessProbeExit -eq 2 -and $readinessProbeResult -and
        $readinessProbeResult.artifactIntegrity.status -eq 'PASS' -and
        $readinessProbeResult.pins.engine.state -eq 'MISSING' -and
        $readinessProbeResult.pins.service.state -eq 'MISSING') `
        'readiness report derives pin provenance from the inspected artifact root'
} catch {
    Check $false "readiness artifact-root pin provenance validation completed without exception ($($_.Exception.Message))"
} finally {
    $readinessProbeFull = [IO.Path]::GetFullPath($readinessProbeRoot).TrimEnd('\')
    $readinessProbeTempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
    if ($readinessProbeFull.StartsWith($readinessProbeTempRoot + '\', [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $readinessProbeFull)) {
        Remove-Item -LiteralPath $readinessProbeFull -Recurse -Force
    }
}

$temp = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-release-tooling-test-' + [guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $temp -Force | Out-Null
    $missingRejected = $false
    try { Get-ReleaseArtifactPaths -ArtifactRoot $temp | Out-Null } catch { $missingRejected = $true }
    Check $missingRejected 'incomplete artifact root is rejected before hashing'
    $x86Path = Join-Path $temp 'fixture-x86.exe'
    $x86Bytes = [byte[]]::new(70)
    $x86Bytes[0] = 0x4d; $x86Bytes[1] = 0x5a; $x86Bytes[60] = 64
    $x86Bytes[64] = 0x50; $x86Bytes[65] = 0x45; $x86Bytes[68] = 0x4c; $x86Bytes[69] = 0x01
    [IO.File]::WriteAllBytes($x86Path, $x86Bytes)
    Check ((Get-PeArchitecture -Path $x86Path).architecture -eq 'x86') 'PE architecture parser rejects a non-x64 image for release signing'
    $malformedPath = Join-Path $temp 'malformed.exe'
    [IO.File]::WriteAllBytes($malformedPath, [byte[]]::new(3))
    $malformedRejected = $false
    try { Get-PeArchitecture -Path $malformedPath | Out-Null } catch { $malformedRejected = $true }
    Check $malformedRejected 'malformed PE input is rejected before signing or hashing'
    $metadataCheck = Test-ExecutableVersionMetadata -Path $malformedPath -ExpectedProductVersion $expectedProductVersion -ExpectedFileVersion $expectedFileVersion -ExpectedOriginalFilename 'malformed.exe'
    Check (-not $metadataCheck.valid -and @($metadataCheck.issues).Count -gt 0) 'missing VERSIONINFO metadata is rejected by final-artifact validation'
} finally {
    $tempFull = [IO.Path]::GetFullPath($temp).TrimEnd('\')
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
    if ($tempFull.StartsWith($tempRoot + '\', [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $tempFull)) {
        Remove-Item -LiteralPath $tempFull -Recurse -Force
    }
}

$hostPreflight = Get-CleanHostPreflight
Check ($null -ne $hostPreflight.resources.service -and $null -ne $hostPreflight.resources.installDirectory) 'clean-host preflight reports exact FastFiles resources'
Check ($null -ne $hostPreflight.trustStore -and -not $hostPreflight.trustStore.checked) 'clean-host preflight does not mutate or claim trust-store provenance'

$packageTemp = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-package-manifest-test-' + [guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $packageTemp -Force | Out-Null
    foreach ($artifactPath in @(Get-ReleaseArtifactPaths -ArtifactRoot $BuildRoot)) {
        Copy-Item -LiteralPath $artifactPath.sourcePath -Destination (Join-Path $packageTemp $artifactPath.name) -Force
    }
    $packageManifestPath = Join-Path $packageTemp 'FastFiles.package.manifest'
    Write-PackageManifest -Manifest $manifest -Path $packageManifestPath
    $packageCheck = Test-PackageManifest -Path $packageManifestPath -ArtifactManifest $manifest -PackageRoot $packageTemp -AllowUnsignedPreparation
    Check ($packageCheck.valid -and @($packageCheck.entries).Count -eq 4) 'package manifest round-trips the complete artifact inventory'
    Check ((Get-Content -LiteralPath $packageManifestPath -Raw) -match 'FASTFILES_PACKAGE_MANIFEST_V2') 'package manifest has an authenticated-schema versioned header'
    Check ($packageCheck.productVersion -ceq $expectedProductVersion) 'package manifest carries the canonical product version'
    Check ($packageCheck.buildIdentity -ceq ([string]$manifest.artifacts[0].buildIdentity).ToLowerInvariant()) 'package manifest carries the canonical build identity'
    $tamperedVersionManifest = Join-Path $packageTemp 'tampered-version.package.manifest'
    $tamperedVersionText = (Get-Content -LiteralPath $packageManifestPath -Raw) -replace "productVersion=$([regex]::Escape($expectedProductVersion))", 'productVersion=9.9.9'
    [IO.File]::WriteAllText($tamperedVersionManifest, $tamperedVersionText, [Text.UTF8Encoding]::new($false))
    $tamperedVersionCheck = Test-PackageManifest -Path $tamperedVersionManifest -ArtifactManifest $manifest -AllowUnsignedPreparation
    Check (-not $tamperedVersionCheck.valid -and $tamperedVersionCheck.reason -eq 'PACKAGE_MANIFEST_VERSION_MISMATCH') 'package manifest version substitution is rejected'
    $tamperedPackageManifest = Join-Path $packageTemp 'tampered.package.manifest'
    $tamperedText = Get-Content -LiteralPath $packageManifestPath -Raw
    $hashLine = ($tamperedText -split "`n" | Where-Object { $_ -like 'file=FastFiles.exe*' } | Select-Object -First 1)
    $replacementHashLine = $hashLine.Substring(0, $hashLine.LastIndexOf('|') + 1) + ('0' * 64)
    $tamperedText = $tamperedText.Replace($hashLine, $replacementHashLine)
    [IO.File]::WriteAllText($tamperedPackageManifest, $tamperedText, [Text.UTF8Encoding]::new($false))
    $tamperedCheck = Test-PackageManifest -Path $tamperedPackageManifest -ArtifactManifest $manifest -AllowUnsignedPreparation
    Check (-not $tamperedCheck.valid -and $tamperedCheck.reason -eq 'PACKAGE_MANIFEST_HASH_MISMATCH') 'package manifest hash substitution is rejected'
    $tamperedArtifactPath = Join-Path $packageTemp 'FastFiles.exe'
    $originalBytes = [IO.File]::ReadAllBytes($tamperedArtifactPath)
    try {
        [IO.File]::WriteAllBytes($tamperedArtifactPath, [byte[]]::new(1))
        $tamperedArtifactCheck = Test-PackageManifest -Path $packageManifestPath -ArtifactManifest $manifest -PackageRoot $packageTemp -AllowUnsignedPreparation
        Check (-not $tamperedArtifactCheck.valid -and $tamperedArtifactCheck.reason -eq 'PACKAGE_ARTIFACT_HASH_MISMATCH') 'package artifact substitution is rejected even when the manifest is unchanged'
    } finally {
        [IO.File]::WriteAllBytes($tamperedArtifactPath, $originalBytes)
    }
    $missingPackageManifest = Join-Path $packageTemp 'missing.package.manifest'
    $missingCheck = Test-PackageManifest -Path $missingPackageManifest -ArtifactManifest $manifest -AllowUnsignedPreparation
    Check (-not $missingCheck.valid -and $missingCheck.reason -eq 'PACKAGE_MANIFEST_MISSING') 'missing package manifest is rejected'

    $productionUnsignedCheck = Test-PackageManifest -Path $packageManifestPath -ArtifactManifest $manifest
    Check (-not $productionUnsignedCheck.valid -and $productionUnsignedCheck.reason -eq 'PACKAGE_MANIFEST_SIGNATURE_MISSING') 'unsigned package manifest cannot authorize production release'
    $badSignaturePath = Join-Path $packageTemp 'FastFiles.package.manifest.p7s'
    [IO.File]::WriteAllBytes($badSignaturePath, [byte[]](0x01, 0x02, 0x03))
    $badSignatureCheck = Test-PackageManifestSignature -ManifestPath $packageManifestPath -SignaturePath $badSignaturePath -AllowedSignerThumbprints @('A' * 40)
    Check (-not $badSignatureCheck.valid -and $badSignatureCheck.reason -eq 'PACKAGE_MANIFEST_SIGNATURE_INVALID') 'malformed or wrong-signer package signature fails closed'

    $antiMixCases = @(
        [pscustomobject]@{ name = 'same-signer-different-version'; property = 'productVersion'; value = '9.9.9' },
        [pscustomobject]@{ name = 'old-engine-build'; property = 'buildIdentity'; value = '1111111111111111111111111111111111111111' },
        [pscustomobject]@{ name = 'wrong-component-role'; property = 'componentRole'; value = 'service' },
        [pscustomobject]@{ name = 'wrong-original-name'; property = 'originalFilename'; value = 'FastFiles.exe.renamed' },
        [pscustomobject]@{ name = 'wrong-internal-name'; property = 'internalName'; value = 'FastFiles.exe.renamed' },
        [pscustomobject]@{ name = 'wrong-architecture'; property = 'architecture'; value = 'arm64' }
    )
    foreach ($case in $antiMixCases) {
        $mutated = $manifest | ConvertTo-Json -Depth 20 | ConvertFrom-Json
        $target = @($mutated.artifacts | Where-Object filename -eq 'FastFilesEngine.exe')[0]
        $target.($case.property) = $case.value
        $casePath = Join-Path $packageTemp ($case.name + '.manifest')
        $writeFailed = $false
        try { Write-PackageManifest -Manifest $mutated -Path $casePath } catch { $writeFailed = $true }
        $caseCheck = if ($writeFailed) { $null } else { Test-PackageManifest -Path $casePath -ArtifactManifest $manifest -AllowUnsignedPreparation }
        Check ($writeFailed -or ($caseCheck -and -not $caseCheck.valid)) "anti-mix case rejects $($case.name)"
    }

    $duplicateText = (Get-Content -LiteralPath $packageManifestPath -Raw) + ((Get-Content -LiteralPath $packageManifestPath -Raw) -split "`n" | Where-Object { $_ -like 'file=FastFiles.exe*' } | Select-Object -First 1) + "`n"
    $duplicatePath = Join-Path $packageTemp 'duplicate.manifest'
    [IO.File]::WriteAllText($duplicatePath, $duplicateText, [Text.UTF8Encoding]::new($false))
    $duplicateCheck = Test-PackageManifest -Path $duplicatePath -ArtifactManifest $manifest -AllowUnsignedPreparation
    Check (-not $duplicateCheck.valid -and $duplicateCheck.reason -eq 'PACKAGE_MANIFEST_DUPLICATE_FILE') 'duplicate package member is rejected'
} finally {
    $packageTempFull = [IO.Path]::GetFullPath($packageTemp).TrimEnd('\')
    $packageTempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
    if ($packageTempFull.StartsWith($packageTempRoot + '\', [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $packageTempFull)) {
        Remove-Item -LiteralPath $packageTempFull -Recurse -Force
    }
}

$kitTemp = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-generated-kit-test-' + [guid]::NewGuid().ToString('N'))
try {
    $kitScript = Join-Path $RepoRoot 'verify\release\New-ReleaseKit.ps1'
    $kitOutput = & (Get-Command pwsh).Source -NoProfile -File $kitScript -BuildRoot $BuildRoot -OutputDirectory $kitTemp -OutputFormat json 2>&1
    $kitExit = $LASTEXITCODE
    Check ($kitExit -eq 0) 'generated release kit is produced from the release artifact root'
    if ($kitExit -eq 0) {
        $generatedPackageRoot = Resolve-ReleaseKitPackageRoot -Root $kitTemp
        $expectedPackageMembers = @('FastFiles.exe', 'FastFilesEngine.exe', 'FastFilesIndexSvc.exe', 'FastFilesSetup.exe', 'FastFiles.package.manifest')
        $actualPackageMembers = @(Get-ChildItem -LiteralPath $generatedPackageRoot -Force | ForEach-Object Name | Sort-Object)
        Check (($actualPackageMembers -join '|') -eq (($expectedPackageMembers | Sort-Object) -join '|')) 'generated kit package subtree has the exact installable member inventory'
        Check (-not (Test-Path -LiteralPath (Join-Path $kitTemp 'FastFiles.exe') -PathType Leaf)) 'generated kit keeps product payload out of the tooling root'
        $generatedKitManifest = Get-Content -LiteralPath (Join-Path $kitTemp 'kit-manifest.json') -Raw | ConvertFrom-Json
        Check ([string]$generatedKitManifest.packageRoot -ceq 'package') 'kit manifest declares the authoritative package root'
        Check (@($generatedKitManifest.package | Where-Object { $_ -notlike 'package/*' }).Count -eq 0) 'kit manifest package paths are rooted under package/'
        $generatedPackageCheck = Test-PackageManifest -Path (Join-Path $generatedPackageRoot 'FastFiles.package.manifest') -ArtifactManifest $manifest -PackageRoot $generatedPackageRoot -AllowUnsignedPreparation
        Check ($generatedPackageCheck.valid) 'generated kit package passes the exact PowerShell inventory validator'
        $unexpectedPackageMember = Join-Path $generatedPackageRoot 'unexpected.txt'
        [IO.File]::WriteAllText($unexpectedPackageMember, 'unexpected', [Text.UTF8Encoding]::new($false))
        $unexpectedPackageCheck = Test-PackageManifest -Path (Join-Path $generatedPackageRoot 'FastFiles.package.manifest') -ArtifactManifest $manifest -PackageRoot $generatedPackageRoot -AllowUnsignedPreparation
        Check (-not $unexpectedPackageCheck.valid -and $unexpectedPackageCheck.reason -eq 'PACKAGE_UNEXPECTED_MEMBER') 'generated package rejects a non-payload member'
        Remove-Item -LiteralPath $unexpectedPackageMember -Force
        $packageRootItem = Get-Item -LiteralPath $generatedPackageRoot
        Check (($packageRootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) 'generated package root is not a reparse point'
        $malformedKitRoot = Join-Path $kitTemp 'malformed-kit'
        New-Item -ItemType Directory -Path $malformedKitRoot -Force | Out-Null
        [IO.File]::WriteAllText((Join-Path $malformedKitRoot 'kit-manifest.json'), '{"schemaVersion":1,"kind":"fastfiles-clean-host-kit"}', [Text.UTF8Encoding]::new($false))
        $malformedKitRejected = $false
        try { Resolve-ReleaseKitPackageRoot -Root $malformedKitRoot | Out-Null } catch { $malformedKitRejected = $true }
        Check $malformedKitRejected 'malformed kit package-root metadata fails closed'
    }
} catch {
    Check $false "generated kit layout validation completed without exception ($($_.Exception.Message))"
} finally {
    $kitTempFull = [IO.Path]::GetFullPath($kitTemp).TrimEnd('\')
    $kitTempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
    if ($kitTempFull.StartsWith($kitTempRoot + '\', [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $kitTempFull)) {
        Remove-Item -LiteralPath $kitTempFull -Recurse -Force
    }
}

$presets = Get-Content -LiteralPath (Join-Path $RepoRoot 'CMakePresets.json') -Raw | ConvertFrom-Json
$releaseTestPreset = @($presets.configurePresets | Where-Object name -eq 'release-test') | Select-Object -First 1
$releasePreset = @($presets.configurePresets | Where-Object name -eq 'release') | Select-Object -First 1
Check ($releaseTestPreset -and $releaseTestPreset.cacheVariables.FASTFILES_RELEASE_TEST_BUILD -eq 'ON' -and $releaseTestPreset.cacheVariables.CMAKE_BUILD_TYPE -eq 'RelWithDebInfo') 'optimized test-release preset is explicit and non-production labeled'
Check ($releasePreset -and $null -eq $releasePreset.cacheVariables.FASTFILES_RELEASE_TEST_BUILD) 'production release preset does not opt out of pin enforcement'
$diagnosticPolicyScript = Join-Path $RepoRoot 'verify\check_diagnostic_policy.ps1'
$diagnosticPolicyOutput = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File $diagnosticPolicyScript -RepoRoot $RepoRoot 2>&1
$diagnosticPolicyExit = $LASTEXITCODE
Check ($diagnosticPolicyExit -eq 0) 'source/configuration policy rejects the unsigned-authentication bypass in shipped configurations'

Write-Host "Failures: $failures"
exit $(if ($failures -eq 0) { 0 } else { 1 })
