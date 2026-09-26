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

function Remove-TestPrivateKeyForCertificate {
    param([Parameter(Mandatory)] [Security.Cryptography.X509Certificates.X509Certificate2] $Certificate)

    if (-not $Certificate.HasPrivateKey) { return }
    $keyName = $null
    $privateKey = $null
    try {
        $privateKey = [Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPrivateKey($Certificate)
        if ($privateKey -is [Security.Cryptography.RSACng]) {
            $keyName = $privateKey.Key.KeyName
        }
    } finally {
        if ($privateKey) { $privateKey.Dispose() }
    }
    if (-not $keyName) {
        throw 'RELEASE_TOOLING_PRIVATE_KEY_PROVIDER_UNSUPPORTED'
    }
    $certutil = Join-Path $env:SystemRoot 'System32\certutil.exe'
    if (-not (Test-Path -LiteralPath $certutil -PathType Leaf)) { throw 'RELEASE_TOOLING_CERTUTIL_MISSING' }
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $certutil
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    foreach ($argument in @('-user', '-csp', 'KSP', '-delkey', $keyName)) {
        [void]$startInfo.ArgumentList.Add($argument)
    }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    try {
        if (-not $process.Start()) { throw 'RELEASE_TOOLING_CERTUTIL_START_FAILED' }
        if (-not $process.WaitForExit(30000)) {
            if (-not $process.HasExited) {
                try { $process.Kill($true) } catch { if (-not $process.HasExited) { $process.Kill() } }
            }
            $process.WaitForExit()
            throw 'RELEASE_TOOLING_CERTUTIL_TIMEOUT'
        }
        if ($process.ExitCode -ne 0) { throw 'RELEASE_TOOLING_CERTUTIL_FAILED' }
    } catch {
        throw 'RELEASE_TOOLING_PRIVATE_KEY_DELETE_FAILED'
    } finally {
        $process.Dispose()
    }
}

function Remove-TestCertificateThumbprint {
    param(
        [Parameter(Mandatory)] [ValidateSet('My', 'Root', 'CA')] [string] $StoreName,
        [AllowNull()] [string] $Thumbprint
    )

    if ([string]::IsNullOrWhiteSpace($Thumbprint)) { return }
    $normalized = Normalize-Thumbprint -Value $Thumbprint
    $store = [Security.Cryptography.X509Certificates.X509Store]::new($StoreName, 'CurrentUser')
    try {
        $store.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
        foreach ($certificate in @($store.Certificates)) {
            if ((Normalize-Thumbprint -Value $certificate.Thumbprint) -ceq $normalized) {
                if ($StoreName -eq 'My') { Remove-TestPrivateKeyForCertificate -Certificate $certificate }
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
        [Parameter(Mandatory)] [ValidateSet('My', 'Root', 'CA')] [string] $StoreName,
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
        if ($leaf) {
            Remove-TestCertificateThumbprint -StoreName CA -Thumbprint $leaf.Thumbprint
            Remove-TestCertificateThumbprint -StoreName My -Thumbprint $leaf.Thumbprint
        }
        if ($root) {
            Remove-TestCertificateThumbprint -StoreName CA -Thumbprint $root.Thumbprint
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
    Remove-TestCertificateThumbprint -StoreName CA -Thumbprint $rootThumbprint
    Remove-TestCertificateThumbprint -StoreName CA -Thumbprint $leafThumbprint
    Remove-TestCertificateThumbprint -StoreName My -Thumbprint $leafThumbprint
    Remove-TestCertificateThumbprint -StoreName My -Thumbprint $rootThumbprint
    return (Test-TestCertificateAbsent -StoreName My -Thumbprint $leafThumbprint) -and
        (Test-TestCertificateAbsent -StoreName My -Thumbprint $rootThumbprint) -and
        (Test-TestCertificateAbsent -StoreName Root -Thumbprint $rootThumbprint) -and
        (Test-TestCertificateAbsent -StoreName CA -Thumbprint $leafThumbprint) -and
        (Test-TestCertificateAbsent -StoreName CA -Thumbprint $rootThumbprint)
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

function Find-TestSignTool {
    $command = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($command) { return [IO.Path]::GetFullPath($command.Source) }
    $programFilesX86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
    if ($programFilesX86) {
        $kitRoot = Join-Path $programFilesX86 'Windows Kits\10\bin'
        if (Test-Path -LiteralPath $kitRoot -PathType Container) {
            foreach ($versionDirectory in @(Get-ChildItem -LiteralPath $kitRoot -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending)) {
                $candidate = Join-Path $versionDirectory.FullName 'x64\signtool.exe'
                if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
            }
        }
    }
    throw 'TEST_SIGNTOOL_MISSING'
}

function Read-Rfc3161Request {
    param([Parameter(Mandatory)] [byte[]] $RequestBytes)

    Add-Type -AssemblyName System.Formats.Asn1 -ErrorAction Stop
    $reader = [System.Formats.Asn1.AsnReader]::new(
        $RequestBytes, [System.Formats.Asn1.AsnEncodingRules]::DER)
    $request = $reader.ReadSequence()
    [void]$request.ReadInteger()
    $messageImprint = $request.ReadSequence()
    $algorithm = $messageImprint.ReadSequence()
    $algorithmOid = $algorithm.ReadObjectIdentifier()
    while ($algorithm.HasData) { [void]$algorithm.ReadEncodedValue() }
    $hashedMessage = [byte[]]$messageImprint.ReadOctetString()
    $nonce = $null
    if ($request.HasData) {
        $tag = $request.PeekTag()
        if ([int]$tag.TagClass -eq 0 -and $tag.TagValue -eq 6) {
            [void]$request.ReadObjectIdentifier()
            $tag = if ($request.HasData) { $request.PeekTag() } else { $null }
        }
        if ($null -ne $tag -and [int]$tag.TagClass -eq 0 -and $tag.TagValue -eq 2) {
            $nonce = [long]$request.ReadInteger()
        }
    }
    while ($request.HasData) { [void]$request.ReadEncodedValue() }
    if ($reader.HasData) { throw 'TEST_TIMESTAMP_REQUEST_INVALID' }
    if ($algorithmOid -ne '2.16.840.1.101.3.4.2.1' -or $hashedMessage.Length -ne 32) {
        throw 'TEST_TIMESTAMP_REQUEST_NOT_SHA256'
    }
    [pscustomobject]@{ hashedMessage = $hashedMessage; nonce = $nonce }
}

function New-Rfc3161TimestampResponse {
    param(
        [Parameter(Mandatory)] [byte[]] $RequestBytes,
        [Parameter(Mandatory)] [Security.Cryptography.X509Certificates.X509Certificate2] $TimestampCertificate,
        [Parameter(Mandatory)] [DateTimeOffset] $TimestampTime
    )

    $request = Read-Rfc3161Request -RequestBytes $RequestBytes
    Add-Type -AssemblyName System.Security.Cryptography.Pkcs -ErrorAction Stop
    Add-Type -AssemblyName System.Formats.Asn1 -ErrorAction Stop
    $tstWriter = [System.Formats.Asn1.AsnWriter]::new([System.Formats.Asn1.AsnEncodingRules]::DER)
    $tstWriter.PushSequence() | Out-Null
    $tstWriter.WriteInteger([long]1)
    $tstWriter.WriteObjectIdentifier('1.3.6.1.4.1.311.10.3.12')
    $tstWriter.PushSequence() | Out-Null
    $tstWriter.PushSequence() | Out-Null
    $tstWriter.WriteObjectIdentifier('2.16.840.1.101.3.4.2.1')
    $tstWriter.WriteEncodedValue([byte[]](0x05, 0x00))
    $tstWriter.PopSequence() | Out-Null
    $tstWriter.WriteOctetString($request.hashedMessage)
    $tstWriter.PopSequence() | Out-Null
    $tstWriter.WriteInteger([long]([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()))
    $tstWriter.WriteGeneralizedTime($TimestampTime.ToUniversalTime(), $true)
    if ($null -ne $request.nonce) { $tstWriter.WriteInteger([long]$request.nonce) }
    $tstWriter.PopSequence() | Out-Null
    $tstInfo = $tstWriter.Encode()

    $tokenContentInfo = [Security.Cryptography.Pkcs.ContentInfo]::new(
        [Security.Cryptography.Oid]::new('1.2.840.113549.1.9.16.1.4'), $tstInfo)
    $tokenCms = [Security.Cryptography.Pkcs.SignedCms]::new($tokenContentInfo, $false)
    $tokenSigner = [Security.Cryptography.Pkcs.CmsSigner]::new(
        [Security.Cryptography.Pkcs.SubjectIdentifierType]::IssuerAndSerialNumber, $TimestampCertificate)
    $tokenSigner.IncludeOption = [Security.Cryptography.X509Certificates.X509IncludeOption]::EndCertOnly
    $essWriter = [System.Formats.Asn1.AsnWriter]::new([System.Formats.Asn1.AsnEncodingRules]::DER)
    $essWriter.PushSequence() | Out-Null
    $essWriter.PushSequence() | Out-Null
    $essWriter.PushSequence() | Out-Null
    $essWriter.WriteOctetString([Security.Cryptography.SHA256]::Create().ComputeHash($TimestampCertificate.RawData))
    $essWriter.PopSequence() | Out-Null
    $essWriter.PopSequence() | Out-Null
    $essWriter.PopSequence() | Out-Null
    $essValues = [Security.Cryptography.AsnEncodedDataCollection]::new()
    [void]$essValues.Add([Security.Cryptography.AsnEncodedData]::new(
            '1.2.840.113549.1.9.16.2.47', $essWriter.Encode()))
    [void]$tokenSigner.SignedAttributes.Add([Security.Cryptography.CryptographicAttributeObject]::new(
            [Security.Cryptography.Oid]::new('1.2.840.113549.1.9.16.2.47'), $essValues))
    $signingTimeBytes = [Text.Encoding]::ASCII.GetBytes(
        $TimestampTime.ToUniversalTime().ToString("yyMMddHHmmss'Z'", [Globalization.CultureInfo]::InvariantCulture))
    $signingTimeEncoded = [byte[]](@(0x17, [byte]$signingTimeBytes.Length) + $signingTimeBytes)
    $signingTimeValues = [Security.Cryptography.AsnEncodedDataCollection]::new()
    [void]$signingTimeValues.Add([Security.Cryptography.AsnEncodedData]::new(
            '1.2.840.113549.1.9.5', $signingTimeEncoded))
    [void]$tokenSigner.SignedAttributes.Add([Security.Cryptography.CryptographicAttributeObject]::new(
            [Security.Cryptography.Oid]::new('1.2.840.113549.1.9.5'), $signingTimeValues))
    $tokenCms.ComputeSignature($tokenSigner)
    $timestampToken = $tokenCms.Encode()

    $responseWriter = [System.Formats.Asn1.AsnWriter]::new([System.Formats.Asn1.AsnEncodingRules]::DER)
    $responseWriter.PushSequence() | Out-Null
    $responseWriter.PushSequence() | Out-Null
    $responseWriter.WriteInteger([long]0)
    $responseWriter.PopSequence() | Out-Null
    $responseWriter.WriteEncodedValue($timestampToken)
    $responseWriter.PopSequence() | Out-Null
    return $responseWriter.Encode()
}

function Read-TimestampHttpBody {
    param([Parameter(Mandatory)] [Net.Sockets.NetworkStream] $Stream)

    $headerBytes = [Collections.Generic.List[byte]]::new()
    while ($true) {
        $value = $Stream.ReadByte()
        if ($value -lt 0) { throw 'TEST_TIMESTAMP_HTTP_EOF' }
        [void]$headerBytes.Add([byte]$value)
        $count = $headerBytes.Count
        if ($count -ge 4 -and $headerBytes[$count - 4] -eq 13 -and
            $headerBytes[$count - 3] -eq 10 -and $headerBytes[$count - 2] -eq 13 -and
            $headerBytes[$count - 1] -eq 10) { break }
    }
    $headers = [Text.Encoding]::ASCII.GetString($headerBytes.ToArray())
    $match = [Text.RegularExpressions.Regex]::Match($headers, '(?im)^Content-Length:\s*(\d+)\s*$')
    if (-not $match.Success) { throw 'TEST_TIMESTAMP_CONTENT_LENGTH_MISSING' }
    $length = [int]$match.Groups[1].Value
    $body = [byte[]]::new($length)
    $read = 0
    while ($read -lt $length) {
        $received = $Stream.Read($body, $read, $length - $read)
        if ($received -le 0) { throw 'TEST_TIMESTAMP_HTTP_BODY_TRUNCATED' }
        $read += $received
    }
    return $body
}

function Invoke-LocalRfc3161P7Timestamp {
    param(
        [Parameter(Mandatory)] [string] $OutputSignaturePath,
        [Parameter(Mandatory)] [Security.Cryptography.X509Certificates.X509Certificate2] $TimestampCertificate,
        [Parameter(Mandatory)] [DateTimeOffset] $TimestampTime
    )

    $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $listener.Start()
    $client = $null
    $process = $null
    try {
        $port = ([Net.IPEndPoint]$listener.LocalEndpoint).Port
        $acceptTask = $listener.AcceptTcpClientAsync()
        $startInfo = [Diagnostics.ProcessStartInfo]::new()
        $startInfo.FileName = Find-TestSignTool
        $startInfo.UseShellExecute = $false
        $startInfo.CreateNoWindow = $true
        foreach ($argument in @('timestamp', '/p7', '/tr', "http://127.0.0.1:$port/", '/td', 'SHA256',
                                [IO.Path]::GetFullPath($OutputSignaturePath))) {
            [void]$startInfo.ArgumentList.Add($argument)
        }
        $process = [Diagnostics.Process]::new()
        $process.StartInfo = $startInfo
        if (-not $process.Start()) { throw 'TEST_TIMESTAMP_SIGNTOOL_START_FAILED' }
        if (-not $acceptTask.Wait(30000)) { throw 'TEST_TIMESTAMP_REQUEST_TIMEOUT' }
        $client = $acceptTask.GetAwaiter().GetResult()
        $stream = $client.GetStream()
        $requestBytes = Read-TimestampHttpBody -Stream $stream
        $responseBytes = New-Rfc3161TimestampResponse -RequestBytes $requestBytes `
            -TimestampCertificate $TimestampCertificate -TimestampTime $TimestampTime
        $header = [Text.Encoding]::ASCII.GetBytes(
            "HTTP/1.1 200 OK`r`nContent-Type: application/timestamp-reply`r`nContent-Length: $($responseBytes.Length)`r`nConnection: close`r`n`r`n")
        $stream.Write($header, 0, $header.Length)
        $stream.Write($responseBytes, 0, $responseBytes.Length)
        $stream.Flush()
        $client.Close()
        $client = $null
        if (-not $process.WaitForExit(30000)) { throw 'TEST_TIMESTAMP_SIGNTOOL_TIMEOUT' }
        if ($process.ExitCode -ne 0) { throw 'TEST_TIMESTAMP_SIGNTOOL_FAILED' }
    } finally {
        if ($client) { $client.Close() }
        if ($process) {
            if (-not $process.HasExited) { $process.Kill() }
            $process.Dispose()
        }
        $listener.Stop()
    }
}

function New-LocalRfc3161DetachedSignature {
    param(
        [Parameter(Mandatory)] [string] $InputSignaturePath,
        [Parameter(Mandatory)] [string] $OutputSignaturePath,
        [Parameter(Mandatory)] [Security.Cryptography.X509Certificates.X509Certificate2] $TimestampCertificate,
        [DateTimeOffset] $TimestampTime = [DateTimeOffset]::UtcNow
    )

    Add-Type -AssemblyName System.Security.Cryptography.Pkcs -ErrorAction Stop
    $cms = [Security.Cryptography.Pkcs.SignedCms]::new()
    $cms.Decode([IO.File]::ReadAllBytes([IO.Path]::GetFullPath($InputSignaturePath)))
    if ($cms.SignerInfos.Count -ne 1) { throw 'TEST_CMS_SIGNER_COUNT_INVALID' }
    Copy-Item -LiteralPath $InputSignaturePath -Destination $OutputSignaturePath -Force
    Invoke-LocalRfc3161P7Timestamp -OutputSignaturePath $OutputSignaturePath `
        -TimestampCertificate $TimestampCertificate -TimestampTime $TimestampTime
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
Check ((Get-ReleaseBuildConfigurationLabel -ArtifactRoot (Join-Path $RepoRoot 'build\release-test')) -eq 'release-test') 'release-test artifact root is labeled release-test'
Check ((Get-ReleaseBuildConfigurationLabel -ArtifactRoot (Join-Path $RepoRoot 'build\release')) -eq 'Release') 'release artifact root is labeled Release'
Check ($manifest.configuration -eq (Get-ReleaseBuildConfigurationLabel -ArtifactRoot $BuildRoot)) "manifest configuration matches artifact-root label ($($manifest.configuration))"
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

    $artifactLongevityFixture = $null
    try {
        $artifactLongevityFixture = New-TestCertificateFixture -Case TrustedExpired
        $artifactPolicy = & $script:releaseCommonModule {
            param($certificate, $timestampCertificate)
            Get-SignedArtifactVerificationPolicy -Certificate $certificate -AuthenticodeStatus 'Valid' `
                -TimestampCertificate $timestampCertificate
        } $artifactLongevityFixture.leaf $signerPolicyFixture.leaf
        $artifactExpiredNow = [DateTime]::UtcNow -gt ([DateTime]$artifactLongevityFixture.leaf.NotAfter).ToUniversalTime()
        Check ($artifactExpiredNow -and $artifactPolicy.valid -and -not $artifactPolicy.currentlyValid -and
            -not $artifactPolicy.currentValidityEnforced -and $artifactPolicy.timestampSigned -and
            $artifactPolicy.timestampValid -and
            $artifactPolicy.timestampPolicy -eq 'RFC3161_AUTHENTICATED_BY_WINTRUST') `
            'timestamp-aware signed-artifact policy preserves a WinTrust-valid artifact after leaf expiry'
    } finally {
        if ($artifactLongevityFixture) {
            Check (Remove-TestCertificateFixture -Fixture $artifactLongevityFixture) `
                'signed-artifact longevity policy fixture certificates are cleaned'
        }
    }

    $manifestPolicyRoot = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-manifest-policy-test-' + [guid]::NewGuid().ToString('N'))
    $manifestPolicyPath = Join-Path $manifestPolicyRoot 'FastFiles.package.manifest'
    $manifestPolicySignaturePath = Join-Path $manifestPolicyRoot 'FastFiles.package.manifest.p7s'
    $manifestTimestampCertificate = $null
    try {
        New-Item -ItemType Directory -Path $manifestPolicyRoot -Force | Out-Null
        [IO.File]::WriteAllText($manifestPolicyPath, 'manifest-policy-probe', [Text.UTF8Encoding]::new($false))
        Add-Type -AssemblyName System.Security.Cryptography.Pkcs -ErrorAction Stop
        $contentInfo = [Security.Cryptography.Pkcs.ContentInfo]::new(
            [IO.File]::ReadAllBytes($manifestPolicyPath))
        $cms = [Security.Cryptography.Pkcs.SignedCms]::new($contentInfo, $true)
        $cmsSigner = [Security.Cryptography.Pkcs.CmsSigner]::new($signerPolicyFixture.leaf)
        $cms.ComputeSignature($cmsSigner)
        $unsignedManifestSignaturePath = Join-Path $manifestPolicyRoot 'unsigned.manifest.p7s'
        [IO.File]::WriteAllBytes($unsignedManifestSignaturePath, $cms.Encode())
        $manifestTimestampCertificate = New-SelfSignedCertificate `
            -Subject ('CN=FastFiles TEST CERTIFICATE NON-PRODUCTION timestamp ' + [guid]::NewGuid().ToString('N')) `
            -Type Custom -Signer $signerPolicyFixture.root -KeyUsage DigitalSignature `
            -TextExtension '2.5.29.37={critical}{text}1.3.6.1.5.5.7.3.8' -CertStoreLocation 'Cert:\CurrentUser\My' `
            -KeyExportPolicy Exportable -NotBefore (Get-Date).AddDays(-2) -NotAfter (Get-Date).AddDays(7)
        New-LocalRfc3161DetachedSignature -InputSignaturePath $unsignedManifestSignaturePath `
            -OutputSignaturePath $manifestPolicySignaturePath -TimestampCertificate $manifestTimestampCertificate
        $manifestPolicyResult = & $script:releaseCommonModule {
            param($manifest, $signature, $thumbprint, $root)
            Test-PackageManifestSignatureForTest -ManifestPath $manifest -SignaturePath $signature `
                -AllowedSignerThumbprints @($thumbprint) -CustomTrustRoot $root
        } $manifestPolicyPath $manifestPolicySignaturePath $signerPolicyFixture.leaf.Thumbprint $signerPolicyFixture.root
        Check ($manifestPolicyResult.valid -and
            $manifestPolicyResult.signerThumbprint -ceq $signerPolicyFixture.leaf.Thumbprint -and
            $manifestPolicyResult.timestampValid -and $manifestPolicyResult.timestampAuthentic) `
            'timestamped detached CMS and the custom-root technical verifier require the exact Code Signing leaf'

        $manifestChangedPath = Join-Path $manifestPolicyRoot 'changed.manifest'
        [IO.File]::WriteAllText($manifestChangedPath, 'manifest-policy-probe-mutated', [Text.UTF8Encoding]::new($false))
        $manifestChangedResult = & $script:releaseCommonModule {
            param($manifest, $signature, $thumbprint, $root)
            Test-PackageManifestSignatureForTest -ManifestPath $manifest -SignaturePath $signature `
                -AllowedSignerThumbprints @($thumbprint) -CustomTrustRoot $root
        } $manifestChangedPath $manifestPolicySignaturePath $signerPolicyFixture.leaf.Thumbprint $signerPolicyFixture.root
        Check (-not $manifestChangedResult.valid) 'changing detached manifest bytes invalidates the timestamped CMS binding'

        $badTimestampPath = Join-Path $manifestPolicyRoot 'bad-timestamp.manifest.p7s'
        $badTimestampBytes = [IO.File]::ReadAllBytes($manifestPolicySignaturePath)
        $badTimestampBytes[$badTimestampBytes.Length - 1] = $badTimestampBytes[$badTimestampBytes.Length - 1] -bxor 0x01
        [IO.File]::WriteAllBytes($badTimestampPath, $badTimestampBytes)
        $badTimestampResult = & $script:releaseCommonModule {
            param($manifest, $signature, $thumbprint, $root)
            Test-PackageManifestSignatureForTest -ManifestPath $manifest -SignaturePath $signature `
                -AllowedSignerThumbprints @($thumbprint) -CustomTrustRoot $root
        } $manifestPolicyPath $badTimestampPath $signerPolicyFixture.leaf.Thumbprint $signerPolicyFixture.root
        Check (-not $badTimestampResult.valid) 'changing the detached CMS timestamp/signature invalidates manifest authentication'

        $wrongSignerResult = & $script:releaseCommonModule {
            param($manifest, $signature, $root)
            Test-PackageManifestSignatureForTest -ManifestPath $manifest -SignaturePath $signature `
                -AllowedSignerThumbprints @('B' * 40) -CustomTrustRoot $root
        } $manifestPolicyPath $manifestPolicySignaturePath $signerPolicyFixture.root
        Check (-not $wrongSignerResult.valid -and $wrongSignerResult.reason -eq 'PACKAGE_MANIFEST_SIGNATURE_WRONG_SIGNER') `
            'a timestamped manifest signed by the wrong leaf is rejected'

        $expiredManifestFixture = New-TestCertificateFixture -Case TrustedExpired
        $expiredTimestampCertificate = $null
        try {
            $expiredTimestampCertificate = New-SelfSignedCertificate `
                -Subject ('CN=FastFiles TEST CERTIFICATE NON-PRODUCTION expired-test timestamp ' + [guid]::NewGuid().ToString('N')) `
                -Type Custom -Signer $expiredManifestFixture.root -KeyUsage DigitalSignature `
                -TextExtension '2.5.29.37={critical}{text}1.3.6.1.5.5.7.3.8' -CertStoreLocation 'Cert:\CurrentUser\My' `
                -KeyExportPolicy Exportable -NotBefore (Get-Date).AddDays(-3) -NotAfter (Get-Date).AddDays(3)
            $expiredUnsignedPath = Join-Path $manifestPolicyRoot 'expired-unsigned.manifest.p7s'
            $expiredSignaturePath = Join-Path $manifestPolicyRoot 'expired.manifest.p7s'
            $expiredCms = [Security.Cryptography.Pkcs.SignedCms]::new(
                [Security.Cryptography.Pkcs.ContentInfo]::new([IO.File]::ReadAllBytes($manifestPolicyPath)), $true)
            $expiredCmsSigner = [Security.Cryptography.Pkcs.CmsSigner]::new($expiredManifestFixture.leaf)
            $expiredCms.ComputeSignature($expiredCmsSigner)
            [IO.File]::WriteAllBytes($expiredUnsignedPath, $expiredCms.Encode())
            $timestampBeforeExpiry = ([DateTime]$expiredManifestFixture.leaf.NotAfter).ToUniversalTime().AddHours(-1)
            New-LocalRfc3161DetachedSignature -InputSignaturePath $expiredUnsignedPath `
                -OutputSignaturePath $expiredSignaturePath -TimestampCertificate $expiredTimestampCertificate `
                -TimestampTime ([DateTimeOffset]$timestampBeforeExpiry)
            $expiredResult = & $script:releaseCommonModule {
                param($manifest, $signature, $thumbprint, $root)
                Test-PackageManifestSignatureForTest -ManifestPath $manifest -SignaturePath $signature `
                    -AllowedSignerThumbprints @($thumbprint) -CustomTrustRoot $root
            } $manifestPolicyPath $expiredSignaturePath $expiredManifestFixture.leaf.Thumbprint $expiredManifestFixture.root
            $expiredNow = [DateTime]::UtcNow -gt ([DateTime]$expiredManifestFixture.leaf.NotAfter).ToUniversalTime()
            Check ($expiredNow -and $expiredResult.valid -and $expiredResult.timestampValid) `
                'a manifest signer that is expired now remains valid when its trusted timestamp predates expiry'
        } finally {
            if ($expiredTimestampCertificate) {
                Remove-TestCertificateThumbprint -StoreName CA -Thumbprint $expiredTimestampCertificate.Thumbprint
                Remove-TestCertificateThumbprint -StoreName My -Thumbprint $expiredTimestampCertificate.Thumbprint
            }
            if ($expiredManifestFixture) { Check (Remove-TestCertificateFixture -Fixture $expiredManifestFixture) 'expired manifest timestamp fixture certificates are cleaned' }
        }

        $noTimestampRoot = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-no-timestamp-test-' + [guid]::NewGuid().ToString('N'))
        $noTimestampArtifact = Join-Path $noTimestampRoot 'probe.exe'
        try {
            New-Item -ItemType Directory -Path $noTimestampRoot -Force | Out-Null
            Copy-Item -LiteralPath (Join-Path $BuildRoot 'src\engine\FastFilesEngine.exe') -Destination $noTimestampArtifact -Force
            $beforeNoTimestampHash = (Get-FileHash -LiteralPath $noTimestampArtifact -Algorithm SHA256).Hash
            $noTimestampOutput = & (Get-Command pwsh).Source -NoProfile -NonInteractive -File (Join-Path $RepoRoot 'verify\release\sign-release.ps1') `
                -Artifacts $noTimestampArtifact -Provider CertificateStore -CertificateThumbprint $signerPolicyFixture.leaf.Thumbprint `
                -ExpectedSignerThumbprint $signerPolicyFixture.leaf.Thumbprint -SigntoolPath $env:ComSpec `
                -OutputFormat json 2>&1
            $noTimestampExit = $LASTEXITCODE
            $noTimestampResult = $null
            try { $noTimestampResult = ($noTimestampOutput -join "`n") | ConvertFrom-Json } catch { }
            $afterNoTimestampHash = (Get-FileHash -LiteralPath $noTimestampArtifact -Algorithm SHA256).Hash
            Check ($noTimestampExit -eq 1 -and $noTimestampResult -and
                $noTimestampResult.error -like 'PRODUCTION_TIMESTAMP_REQUIRED*' -and
                -not $noTimestampResult.signingPerformed -and $beforeNoTimestampHash -ceq $afterNoTimestampHash) `
                'a real PE signing operation fails closed before mutation when no production RFC3161 timestamp is configured'
        } finally {
            if (Test-Path -LiteralPath $noTimestampRoot -PathType Container) {
                Remove-Item -LiteralPath $noTimestampRoot -Recurse -Force -ErrorAction SilentlyContinue
            }
        }

    } finally {
        if ($manifestTimestampCertificate) {
            Remove-TestCertificateThumbprint -StoreName CA -Thumbprint $manifestTimestampCertificate.Thumbprint
            Remove-TestCertificateThumbprint -StoreName My -Thumbprint $manifestTimestampCertificate.Thumbprint
        }
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
    if ($pfxProbeRootCertificate) { Remove-TestCertificateThumbprint -StoreName CA -Thumbprint $pfxProbeRootCertificate.Thumbprint }
    if ($pfxProbeLeafCertificate) { Remove-TestCertificateThumbprint -StoreName CA -Thumbprint $pfxProbeLeafCertificate.Thumbprint }
    if ($pfxProbeLeafCertificate) { Remove-TestCertificateThumbprint -StoreName My -Thumbprint $pfxProbeLeafCertificate.Thumbprint }
    if ($pfxProbeRootCertificate) { Remove-TestCertificateThumbprint -StoreName My -Thumbprint $pfxProbeRootCertificate.Thumbprint }
    if ($pfxProbeLeafCertificate) { Check (Test-TestCertificateAbsent -StoreName My -Thumbprint $pfxProbeLeafCertificate.Thumbprint) 'encrypted PFX temporary leaf is absent from CurrentUser personal store after cleanup' }
    if ($pfxProbeRootCertificate) { Check (Test-TestCertificateAbsent -StoreName My -Thumbprint $pfxProbeRootCertificate.Thumbprint) 'encrypted PFX temporary root is absent from CurrentUser personal store after cleanup' }
    if ($pfxProbeRootCertificate) { Check (Test-TestCertificateAbsent -StoreName CA -Thumbprint $pfxProbeRootCertificate.Thumbprint) 'encrypted PFX temporary root is absent from CurrentUser CA store after cleanup' }
    if (Test-Path -LiteralPath $pfxProbePath -PathType Leaf) { Remove-Item -LiteralPath $pfxProbePath -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath $pfxProbeRoot -PathType Container) { Remove-Item -LiteralPath $pfxProbeRoot -Recurse -Force -ErrorAction SilentlyContinue }
}

$pins = Get-ConfiguredPinStates -RepoRoot $RepoRoot -BuildRoot $BuildRoot
$matrix = @(Get-ReleaseSignatureMatrix -Manifest $manifest -PinStates $pins)
Check (@($matrix | Where-Object result -eq 'UNSIGNED — PRODUCTION SIGNING REQUIRED').Count -eq 4) 'unsigned Release binaries produce explicit production-signing blockers'

$matrixThumbprint = 'A' * 40
$matrixPins = [pscustomobject]@{
    engine = Get-PinState -Name 'engine' -Value $matrixThumbprint
    service = Get-PinState -Name 'service' -Value ('B' * 40)
}
$matrixArtifact = [pscustomobject]@{
    filename = 'FastFilesEngine.exe'
    signature = [pscustomobject]@{
        signed = $true
        valid = $true
        signerSubject = 'TEST CERTIFICATE / NON-PRODUCTION'
        leafThumbprint = $matrixThumbprint
        timestampSigned = $false
        timestampValid = $null
    }
}
$matrixManifest = [pscustomobject]@{ artifacts = @($matrixArtifact) }
$missingTimestampMatrix = @(Get-ReleaseSignatureMatrix -Manifest $matrixManifest -PinStates $matrixPins -ExpectedSignerThumbprint $matrixThumbprint)
Check ($missingTimestampMatrix[0].result -eq 'MISSING_TIMESTAMP') 'release readiness rejects a currently valid PE artifact without a timestamp'
$matrixArtifact.signature.timestampSigned = $true
$matrixArtifact.signature.timestampValid = $true
$validTimestampMatrix = @(Get-ReleaseSignatureMatrix -Manifest $matrixManifest -PinStates $matrixPins -ExpectedSignerThumbprint $matrixThumbprint)
Check ($validTimestampMatrix[0].result -eq 'PASS') 'release readiness accepts a valid timestamp and exact signer pin'
$matrixArtifact.signature.timestampValid = $false
$invalidTimestampMatrix = @(Get-ReleaseSignatureMatrix -Manifest $matrixManifest -PinStates $matrixPins -ExpectedSignerThumbprint $matrixThumbprint)
Check ($invalidTimestampMatrix[0].result -eq 'TIMESTAMP_INVALID') 'release readiness rejects an invalid timestamp'
$matrixArtifact.signature.timestampValid = $true
$wrongSignerMatrix = @(Get-ReleaseSignatureMatrix -Manifest $matrixManifest -PinStates $matrixPins -ExpectedSignerThumbprint ('C' * 40))
Check ($wrongSignerMatrix[0].result -eq 'WRONG_SIGNER') 'release readiness rejects a timestamped artifact from the wrong signer'
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

$artifactRaceRoot = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-artifact-manifest-race-' + [guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $artifactRaceRoot -Force | Out-Null
    foreach ($artifactPath in @(Get-ReleaseArtifactPaths -ArtifactRoot $BuildRoot)) {
        Copy-Item -LiteralPath $artifactPath.sourcePath -Destination (Join-Path $artifactRaceRoot $artifactPath.name) -Force
    }
    $artifactRaceBaseline = Get-ReleaseArtifactManifest -ArtifactRoot $artifactRaceRoot -Configuration 'test-fixture'
    $raceTarget = Join-Path $artifactRaceRoot 'FastFilesEngine.exe'
    $raceReplacement = Join-Path $artifactRaceRoot 'replacement.exe'
    $raceOriginalBytes = [IO.File]::ReadAllBytes($raceTarget)
    $raceReplacementBytes = [byte[]]::new($raceOriginalBytes.Length)
    [Array]::Copy($raceOriginalBytes, $raceReplacementBytes, $raceOriginalBytes.Length)
    $raceReplacementBytes[$raceReplacementBytes.Length - 1] = $raceReplacementBytes[$raceReplacementBytes.Length - 1] -bxor 0xff
    [IO.File]::WriteAllBytes($raceReplacement, $raceReplacementBytes)
    $baselineEngine = @($artifactRaceBaseline.artifacts | Where-Object filename -eq 'FastFilesEngine.exe')[0]

    & $releaseCommonModule {
        $script:ReleaseArtifactOriginalSignatureForTest = (Get-Command Get-SignatureRecord).ScriptBlock
        $script:ReleaseArtifactSnapshotTargetForTest = $raceTarget
        $script:ReleaseArtifactSnapshotReplacementForTest = $raceReplacement
        $script:ReleaseArtifactSnapshotCapturedForTest = {
            param($sourcePath, $snapshotPath)
            if ([IO.Path]::GetFullPath($sourcePath) -ieq [IO.Path]::GetFullPath($script:ReleaseArtifactSnapshotTargetForTest)) {
                Copy-Item -LiteralPath $script:ReleaseArtifactSnapshotReplacementForTest -Destination $sourcePath -Force
                $script:ReleaseArtifactSnapshotCapturedForTest = $null
            }
        }
        Set-Item -Path Function:\Get-SignatureRecord -Value {
            param([Parameter(Mandatory)] [string] $Path)
            $record = & $script:ReleaseArtifactOriginalSignatureForTest -Path $Path
            Add-Member -InputObject $record -MemberType NoteProperty -Name testSnapshotSha256 `
                -Value ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()) -Force
            return $record
        }
    }
    try {
        $artifactRaceResult = Get-ReleaseArtifactManifest -ArtifactRoot $artifactRaceRoot -Configuration 'test-fixture'
    } finally {
        & $releaseCommonModule {
            $script:ReleaseArtifactSnapshotCapturedForTest = $null
            if ($script:ReleaseArtifactOriginalSignatureForTest) {
                Set-Item -Path Function:\Get-SignatureRecord -Value $script:ReleaseArtifactOriginalSignatureForTest
                $script:ReleaseArtifactOriginalSignatureForTest = $null
            }
            $script:ReleaseArtifactSnapshotTargetForTest = $null
            $script:ReleaseArtifactSnapshotReplacementForTest = $null
        }
    }
    $racedEngine = @($artifactRaceResult.artifacts | Where-Object filename -eq 'FastFilesEngine.exe')[0]
    $mutatedEngineHash = (Get-FileHash -LiteralPath $raceTarget -Algorithm SHA256).Hash.ToUpperInvariant()
    Check ($racedEngine.sha256 -ceq $baselineEngine.sha256 -and [long]$racedEngine.size -eq [long]$baselineEngine.size -and
        [string]$racedEngine.configuration -ceq [string]$baselineEngine.configuration -and
        [string]$racedEngine.peMachine -ceq [string]$baselineEngine.peMachine -and
        [string]$racedEngine.signature.testSnapshotSha256 -ceq [string]$baselineEngine.sha256 -and
        $mutatedEngineHash -cne [string]$baselineEngine.sha256) `
        'artifact manifest derives hash, size, PE, VERSIONINFO, and Authenticode from one snapshot after source replacement'
} catch {
    Check $false "artifact manifest replacement fixture completed without exception ($($_.Exception.Message))"
} finally {
    $artifactRaceFull = [IO.Path]::GetFullPath($artifactRaceRoot).TrimEnd('\')
    $artifactRaceTempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
    if ($artifactRaceFull.StartsWith($artifactRaceTempRoot + '\', [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $artifactRaceFull)) {
        Remove-Item -LiteralPath $artifactRaceFull -Recurse -Force
    }
}

$archiveProbeRoot = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-archive-probe-' + [guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $archiveProbeRoot -Force | Out-Null
    $archiveProbePayload = Join-Path $archiveProbeRoot 'payload.bin'
    $archiveProbeBytes = [byte[]](1, 2, 3, 4, 5)
    $archiveProbeReplacement = [byte[]](9, 8, 7, 6, 5)
    [IO.File]::WriteAllBytes($archiveProbePayload, $archiveProbeBytes)
    $expectedArchiveEntries = @(Get-ReleaseArchiveInventory -Root $archiveProbeRoot)
    $archiveProbePath = Join-Path $archiveProbeRoot 'injected.zip'
    Add-Type -AssemblyName System.IO.Compression -ErrorAction SilentlyContinue
    $zipStream = [IO.File]::Open($archiveProbePath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    $zip = [IO.Compression.ZipArchive]::new($zipStream, [IO.Compression.ZipArchiveMode]::Create, $false)
    try {
        $zipEntry = $zip.CreateEntry('payload.bin', [IO.Compression.CompressionLevel]::Optimal)
        $entryStream = $zipEntry.Open()
        try {
            # Deterministically inject the path replacement in the packaging
            # window, then restore the source as the original race could do.
            [IO.File]::WriteAllBytes($archiveProbePayload, $archiveProbeReplacement)
            $entryStream.Write($archiveProbeReplacement, 0, $archiveProbeReplacement.Length)
        } finally { $entryStream.Dispose() }
    } finally {
        $zip.Dispose()
        $zipStream.Dispose()
        [IO.File]::WriteAllBytes($archiveProbePayload, $archiveProbeBytes)
    }
    $mutatedArchiveCheck = Test-ReleaseArchiveContents -ArchivePath $archiveProbePath -ExpectedEntries $expectedArchiveEntries
    $restoredSourceHash = (Get-FileHash -LiteralPath $archiveProbePayload -Algorithm SHA256).Hash
    Check (-not $mutatedArchiveCheck.valid -and @($mutatedArchiveCheck.issues | Where-Object { $_ -eq 'ARCHIVE_ENTRY_HASH_MISMATCH:payload.bin' }).Count -eq 1 -and
        $restoredSourceHash -ieq [string]$expectedArchiveEntries[0].sha256) `
        'archive inventory rejects a payload replaced during ZIP creation even after the source path is restored'

    $validArchivePath = Join-Path $archiveProbeRoot 'valid.zip'
    $validZipStream = [IO.File]::Open($validArchivePath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    $validZip = [IO.Compression.ZipArchive]::new($validZipStream, [IO.Compression.ZipArchiveMode]::Create, $false)
    try {
        $validEntry = $validZip.CreateEntry('payload.bin', [IO.Compression.CompressionLevel]::Optimal)
        $validEntryStream = $validEntry.Open()
        try { $validEntryStream.Write($archiveProbeBytes, 0, $archiveProbeBytes.Length) }
        finally { $validEntryStream.Dispose() }
    } finally { $validZip.Dispose(); $validZipStream.Dispose() }
    $validArchiveCheck = Test-ReleaseArchiveContents -ArchivePath $validArchivePath -ExpectedEntries $expectedArchiveEntries
    Check ($validArchiveCheck.valid -and $validArchiveCheck.entryCount -eq 1) 'archive inventory accepts the exact immutable payload snapshot'
} catch {
    Check $false "archive inventory mutation fixture completed without exception ($($_.Exception.Message))"
} finally {
    $archiveProbeFull = [IO.Path]::GetFullPath($archiveProbeRoot).TrimEnd('\')
    $archiveProbeTempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
    if ($archiveProbeFull.StartsWith($archiveProbeTempRoot + '\', [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $archiveProbeFull)) {
        Remove-Item -LiteralPath $archiveProbeFull -Recurse -Force
    }
}

$candidateSnapshotRoot = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-candidate-snapshot-check-' + [guid]::NewGuid().ToString('N'))
try {
    $candidateOutputRoot = Join-Path $candidateSnapshotRoot 'output'
    $candidateStagingRoot = Join-Path $candidateSnapshotRoot 'staging'
    New-Item -ItemType Directory -Path (Join-Path $candidateOutputRoot 'validation') -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $candidateStagingRoot 'validation') -Force | Out-Null
    $snapshotCommit = 'A' * 40
    $snapshotSqliteVersion = '3.49.1'
    $expectedCandidatePaths = @('artifact-manifest.json', 'dependency-inventory.json', 'kit-manifest.json', 'privileged-release.ps1', 'validation/fftest.exe')
    $candidateOutputFftest = Join-Path (Join-Path $candidateOutputRoot 'validation') 'fftest.exe'
    [IO.File]::WriteAllText((Join-Path $candidateOutputRoot 'artifact-manifest.json'), '{}', [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($candidateOutputFftest, 'probe', [Text.UTF8Encoding]::new($false))
    $candidateOutputHelper = Join-Path $candidateOutputRoot 'privileged-release.ps1'
    [IO.File]::WriteAllText($candidateOutputHelper, 'committed-helper-bytes', [Text.UTF8Encoding]::new($false))
    $expectedKitProjection = [ordered]@{
        schemaVersion = 1
        kind = 'fastfiles-clean-host-kit'
        commit = $snapshotCommit
        architecture = 'x64'
        pinProvisioning = [ordered]@{
            engine = [ordered]@{ name = 'engine'; state = 'MISSING'; source = 'not-exported-until-matched-to-signed-artifact'; length = 0; thumbprint = $null }
            service = [ordered]@{ name = 'service'; state = 'MISSING'; source = 'not-exported-until-matched-to-signed-artifact'; length = 0; thumbprint = $null }
            provenanceRequired = $true
        }
        productionSigning = [ordered]@{ required = $true; currentState = 'BLOCKED_EXTERNAL' }
        sourceCloneRequired = $false
        buildToolsRequired = $false
    }
    $kitOutputPath = Join-Path $candidateOutputRoot 'kit-manifest.json'
    [IO.File]::WriteAllText($kitOutputPath, (ConvertTo-Json -InputObject $expectedKitProjection -Depth 12), [Text.UTF8Encoding]::new($false))
    $dependencyInventory = [ordered]@{
        schemaVersion = 1
        kind = 'fastfiles-dependency-inventory'
        format = 'internal-structured-inventory'
        commit = $snapshotCommit
        components = @([ordered]@{ name = 'SQLite'; version = $snapshotSqliteVersion; scope = 'statically linked build/runtime dependency'; source = 'third_party/sqlite' })
        note = 'This is a lightweight dependency inventory, not a claim of a complete SPDX or CycloneDX SBOM.'
    }
    [IO.File]::WriteAllText((Join-Path $candidateOutputRoot 'dependency-inventory.json'), (ConvertTo-Json -InputObject $dependencyInventory -Depth 8), [Text.UTF8Encoding]::new($false))
    Copy-Item -LiteralPath (Join-Path $candidateOutputRoot 'artifact-manifest.json') -Destination $candidateStagingRoot
    Copy-Item -LiteralPath $kitOutputPath -Destination $candidateStagingRoot
    Copy-Item -LiteralPath (Join-Path $candidateOutputRoot 'dependency-inventory.json') -Destination $candidateStagingRoot
    Copy-Item -LiteralPath $candidateOutputHelper -Destination $candidateStagingRoot
    Copy-Item -LiteralPath $candidateOutputFftest -Destination (Join-Path $candidateStagingRoot 'validation')
    $validCandidateSnapshot = Test-ReleaseCandidateSnapshotContents -Root $candidateStagingRoot -ExpectedPaths $expectedCandidatePaths `
        -ExpectedCommit $snapshotCommit -ExpectedSqliteVersion $snapshotSqliteVersion
    Check ($validCandidateSnapshot.valid -and $validCandidateSnapshot.entryCount -eq 5) 'candidate snapshot accepts the exact declared file set and committed SQLite inventory'
    $validKitProjection = Test-ReleaseKitManifestProjection -Path (Join-Path $candidateStagingRoot 'kit-manifest.json') -ExpectedManifest $expectedKitProjection
    Check $validKitProjection.valid 'candidate kit manifest projection accepts the complete expected record'

    $originalKitBytes = [IO.File]::ReadAllBytes($kitOutputPath)
    $originalKitHash = (Get-FileHash -LiteralPath $kitOutputPath -Algorithm SHA256).Hash
    $originalKitText = [Text.UTF8Encoding]::new($false, $true).GetString($originalKitBytes)
    $tamperedKitText = $originalKitText.Replace($snapshotCommit, ('B' * 40))
    [IO.File]::WriteAllText($kitOutputPath, $tamperedKitText, [Text.UTF8Encoding]::new($false))
    Copy-Item -LiteralPath $kitOutputPath -Destination (Join-Path $candidateStagingRoot 'kit-manifest.json') -Force
    [IO.File]::WriteAllBytes($kitOutputPath, $originalKitBytes)
    $restoredKitHash = (Get-FileHash -LiteralPath $kitOutputPath -Algorithm SHA256).Hash
    $tamperedKitProjection = Test-ReleaseKitManifestProjection -Path (Join-Path $candidateStagingRoot 'kit-manifest.json') -ExpectedManifest $expectedKitProjection
    Check (-not $tamperedKitProjection.valid -and $restoredKitHash -ieq $originalKitHash) `
        'candidate snapshot rejects a changed kit commit after its output path is restored'

    $tamperedKitPin = Get-Content -LiteralPath (Join-Path $candidateStagingRoot 'kit-manifest.json') -Raw | ConvertFrom-Json
    $tamperedKitPin.pinProvisioning.engine.state = 'POPULATED'
    [IO.File]::WriteAllText((Join-Path $candidateStagingRoot 'kit-manifest.json'),
        (ConvertTo-Json -InputObject $tamperedKitPin -Depth 12), [Text.UTF8Encoding]::new($false))
    $tamperedKitPinProjection = Test-ReleaseKitManifestProjection -Path (Join-Path $candidateStagingRoot 'kit-manifest.json') -ExpectedManifest $expectedKitProjection
    Check (-not $tamperedKitPinProjection.valid -and
        @($tamperedKitPinProjection.issues | Where-Object { $_ -eq 'CANDIDATE_KIT_MANIFEST_PROJECTION_MISMATCH' }).Count -eq 1) `
        'candidate snapshot rejects a forged pin-provisioning state'
    Copy-Item -LiteralPath $kitOutputPath -Destination (Join-Path $candidateStagingRoot 'kit-manifest.json') -Force

    $unexpectedRootPath = Join-Path $candidateStagingRoot 'unexpected.exe'
    [IO.File]::WriteAllText($unexpectedRootPath, 'unexpected', [Text.UTF8Encoding]::new($false))
    $unexpectedRootCheck = Test-ReleaseCandidateSnapshotContents -Root $candidateStagingRoot -ExpectedPaths $expectedCandidatePaths `
        -ExpectedCommit $snapshotCommit -ExpectedSqliteVersion $snapshotSqliteVersion
    Check (-not $unexpectedRootCheck.valid -and @($unexpectedRootCheck.issues | Where-Object { $_ -eq 'CANDIDATE_SNAPSHOT_UNEXPECTED_PATH:unexpected.exe' }).Count -eq 1) `
        'candidate snapshot rejects an undeclared root executable'
    Remove-Item -LiteralPath $unexpectedRootPath -Force

    $unexpectedValidationPath = Join-Path $candidateStagingRoot 'validation\unexpected.exe'
    [IO.File]::WriteAllText($unexpectedValidationPath, 'unexpected', [Text.UTF8Encoding]::new($false))
    $unexpectedValidationCheck = Test-ReleaseCandidateSnapshotContents -Root $candidateStagingRoot -ExpectedPaths $expectedCandidatePaths `
        -ExpectedCommit $snapshotCommit -ExpectedSqliteVersion $snapshotSqliteVersion
    Check (-not $unexpectedValidationCheck.valid -and @($unexpectedValidationCheck.issues | Where-Object { $_ -eq 'CANDIDATE_SNAPSHOT_UNEXPECTED_PATH:validation/unexpected.exe' }).Count -eq 1) `
        'candidate snapshot rejects an undeclared validation member'
    Remove-Item -LiteralPath $unexpectedValidationPath -Force

    $dependencyOutputPath = Join-Path $candidateOutputRoot 'dependency-inventory.json'
    $dependencyStagingPath = Join-Path $candidateStagingRoot 'dependency-inventory.json'
    $originalDependencyBytes = [IO.File]::ReadAllBytes($dependencyOutputPath)
    $originalDependencyHash = (Get-FileHash -LiteralPath $dependencyOutputPath -Algorithm SHA256).Hash
    $originalDependencyText = [Text.UTF8Encoding]::new($false, $true).GetString($originalDependencyBytes)
    $tamperedDependencyText = $originalDependencyText.Replace($snapshotCommit, ('B' * 40)).Replace($snapshotSqliteVersion, '0.0.0')
    [IO.File]::WriteAllText($dependencyOutputPath, $tamperedDependencyText, [Text.UTF8Encoding]::new($false))
    Copy-Item -LiteralPath $dependencyOutputPath -Destination $dependencyStagingPath -Force
    [IO.File]::WriteAllBytes($dependencyOutputPath, $originalDependencyBytes)
    $restoredOutputHash = (Get-FileHash -LiteralPath $dependencyOutputPath -Algorithm SHA256).Hash
    $tamperedDependencyCheck = Test-ReleaseCandidateSnapshotContents -Root $candidateStagingRoot -ExpectedPaths $expectedCandidatePaths `
        -ExpectedCommit $snapshotCommit -ExpectedSqliteVersion $snapshotSqliteVersion
    Check (-not $tamperedDependencyCheck.valid -and
        @($tamperedDependencyCheck.issues | Where-Object { $_ -eq 'CANDIDATE_SNAPSHOT_DEPENDENCY_INVENTORY_MISMATCH' }).Count -eq 1 -and
        $restoredOutputHash -ieq $originalDependencyHash) `
        'candidate snapshot rejects a replaced dependency inventory after the output path is restored'

    $wrongVersionText = $originalDependencyText.Replace($snapshotSqliteVersion, '0.0.0')
    [IO.File]::WriteAllText($dependencyStagingPath, $wrongVersionText, [Text.UTF8Encoding]::new($false))
    $wrongVersionCheck = Test-ReleaseCandidateSnapshotContents -Root $candidateStagingRoot -ExpectedPaths $expectedCandidatePaths `
        -ExpectedCommit $snapshotCommit -ExpectedSqliteVersion $snapshotSqliteVersion
    Check (-not $wrongVersionCheck.valid -and
        @($wrongVersionCheck.issues | Where-Object { $_ -eq 'CANDIDATE_SNAPSHOT_DEPENDENCY_INVENTORY_MISMATCH' }).Count -eq 1) `
        'candidate snapshot rejects a SQLite version that differs from committed dependency metadata'

    Copy-Item -LiteralPath $dependencyOutputPath -Destination $dependencyStagingPath -Force
    $expectedCandidateOutputEntries = @(Get-ReleaseArchiveInventory -Root $candidateStagingRoot)
    $candidateArchivePath = Join-Path $candidateOutputRoot 'FastFiles-test.zip'
    [byte[]]$candidateArchiveBytes = @(1, 2, 3, 4, 5)
    [IO.File]::WriteAllBytes($candidateArchivePath, $candidateArchiveBytes)
    $expectedCandidateOutputEntries += [pscustomobject]@{
        path = 'FastFiles-test.zip'
        length = $candidateArchiveBytes.Length
        sha256 = (Get-FileHash -LiteralPath $candidateArchivePath -Algorithm SHA256).Hash
    }
    $candidateAdditionalOutputs = @('candidate-manifest.json', 'provenance.json', 'SHA256SUMS.txt', '.fastfiles-release-candidate-v1')
    foreach ($name in $candidateAdditionalOutputs) {
        [IO.File]::WriteAllText((Join-Path $candidateOutputRoot $name), "generated:$name", [Text.UTF8Encoding]::new($false))
    }
    $outputSnapshotCheck = Test-ReleaseCandidateOutputContents -Root $candidateOutputRoot `
        -ExpectedEntries $expectedCandidateOutputEntries -AdditionalPaths $candidateAdditionalOutputs
    Check ($outputSnapshotCheck.valid -and $outputSnapshotCheck.guards.Count -eq 10) 'candidate output files match the guarded archive generation'
    foreach ($guard in @($outputSnapshotCheck.guards)) { if ($guard) { $guard.Dispose() } }

    $helperOutputBytes = [IO.File]::ReadAllBytes($candidateOutputHelper)
    [IO.File]::WriteAllText($candidateOutputHelper, 'alternate-helper-bytes', [Text.UTF8Encoding]::new($false))
    $mutatedOutputCheck = Test-ReleaseCandidateOutputContents -Root $candidateOutputRoot `
        -ExpectedEntries $expectedCandidateOutputEntries -AdditionalPaths $candidateAdditionalOutputs
    Check (-not $mutatedOutputCheck.valid -and
        @($mutatedOutputCheck.issues | Where-Object { $_ -eq 'CANDIDATE_OUTPUT_HASH_MISMATCH:privileged-release.ps1' }).Count -eq 1) `
        'candidate refuses a returned helper tree changed after archive snapshot capture'
    foreach ($guard in @($mutatedOutputCheck.guards)) { if ($guard) { $guard.Dispose() } }
    [IO.File]::WriteAllBytes($candidateOutputHelper, $helperOutputBytes)

    $checksumsPath = Join-Path $candidateOutputRoot 'SHA256SUMS.txt'
    $markerPath = Join-Path $candidateOutputRoot '.fastfiles-release-candidate-v1'
    $checksumLines = @(Get-ChildItem -LiteralPath $candidateOutputRoot -Recurse -File -Force | Where-Object {
            [IO.Path]::GetFullPath($_.FullName) -ine [IO.Path]::GetFullPath($checksumsPath) -and
            [IO.Path]::GetFullPath($_.FullName) -ine [IO.Path]::GetFullPath($markerPath)
        } | ForEach-Object {
            $relative = $_.FullName.Substring([IO.Path]::GetFullPath($candidateOutputRoot).TrimEnd('\').Length).TrimStart('\').Replace('\', '/')
            "$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToUpperInvariant())  $relative"
        } | Sort-Object)
    [IO.File]::WriteAllText($checksumsPath, ($checksumLines -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
    $checksumsCheck = Test-ReleaseCandidateChecksums -Root $candidateOutputRoot -ChecksumsPath $checksumsPath -MarkerPath $markerPath
    Check $checksumsCheck.valid 'candidate checksums match the complete output generation'
    $lateCandidateFile = Join-Path $candidateOutputRoot 'package\late-insertion.dll'
    New-Item -ItemType Directory -Path (Split-Path -Parent $lateCandidateFile) -Force | Out-Null
    [IO.File]::WriteAllText($lateCandidateFile, 'late insertion after checksum enumeration', [Text.UTF8Encoding]::new($false))
    $lateOutputCheck = Test-ReleaseCandidateOutputContents -Root $candidateOutputRoot `
        -ExpectedEntries $expectedCandidateOutputEntries -AdditionalPaths $candidateAdditionalOutputs
    Check (-not $lateOutputCheck.valid -and
        @($lateOutputCheck.issues | Where-Object { $_ -eq 'CANDIDATE_OUTPUT_UNEXPECTED_PATH:package/late-insertion.dll' }).Count -eq 1) `
        'final output inventory rejects a file inserted after checksum enumeration'
    foreach ($guard in @($lateOutputCheck.guards)) { if ($guard) { $guard.Dispose() } }
    Remove-Item -LiteralPath $lateCandidateFile -Force
} catch {
    Check $false "candidate snapshot inventory fixtures completed without exception ($($_.Exception.Message))"
} finally {
    $candidateSnapshotFull = [IO.Path]::GetFullPath($candidateSnapshotRoot).TrimEnd('\')
    $candidateSnapshotTempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
    if ($candidateSnapshotFull.StartsWith($candidateSnapshotTempRoot + '\', [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $candidateSnapshotFull)) {
        Remove-Item -LiteralPath $candidateSnapshotFull -Recurse -Force
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
