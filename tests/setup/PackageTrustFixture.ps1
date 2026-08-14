[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('Prepare', 'Cleanup')]
    [string] $Mode,
    [Parameter(Mandatory)]
    [string] $PackageRoot,
    [Parameter(Mandatory)]
    [string] $StatePath
)

$ErrorActionPreference = 'Stop'

function Normalize-Thumbprint {
    param([Parameter(Mandatory)] [string] $Value)
    return ($Value -replace '[\s-]', '').ToUpperInvariant()
}

function Remove-CertificateThumbprint {
    param(
        [Parameter(Mandatory)] [ValidateSet('My', 'Root', 'CA')] [string] $StoreName,
        [Parameter(Mandatory)] [string] $Thumbprint
    )

    $normalized = Normalize-Thumbprint -Value $Thumbprint
    if ($StoreName -eq 'Root') {
        Remove-CurrentUserRootCertificate -Thumbprint $normalized
        return
    }
    $store = [Security.Cryptography.X509Certificates.X509Store]::new($StoreName, 'CurrentUser')
    try {
        $store.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
        foreach ($certificate in @($store.Certificates)) {
            if ((Normalize-Thumbprint -Value $certificate.Thumbprint) -ceq $normalized) {
                if ($StoreName -eq 'My') { Remove-PrivateKeyForCertificate -Certificate $certificate }
                $store.Remove($certificate)
            }
        }
    } finally {
        $store.Close()
        $store.Dispose()
    }
}

function Get-CertUtilPath {
    $path = Join-Path $env:SystemRoot 'System32\certutil.exe'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw 'PACKAGE_TRUST_FIXTURE_CERTUTIL_MISSING'
    }
    return $path
}

function Invoke-CertUtil {
    param([Parameter(Mandatory)] [string[]] $Arguments)

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = Get-CertUtilPath
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    foreach ($argument in $Arguments) { [void]$startInfo.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    try {
        if (-not $process.Start()) { throw 'PACKAGE_TRUST_FIXTURE_CERTUTIL_START_FAILED' }
        if (-not $process.WaitForExit(30000)) {
            if (-not $process.HasExited) {
                try { $process.Kill($true) } catch { if (-not $process.HasExited) { $process.Kill() } }
            }
            $process.WaitForExit()
            throw 'PACKAGE_TRUST_FIXTURE_CERTUTIL_TIMEOUT'
        }
        if ($process.ExitCode -ne 0) { throw 'PACKAGE_TRUST_FIXTURE_CERTUTIL_FAILED' }
    } finally {
        $process.Dispose()
    }
}

function Test-CurrentUserRootCertificate {
    param([Parameter(Mandatory)] [string] $Thumbprint)

    $normalized = Normalize-Thumbprint -Value $Thumbprint
    if ($normalized -notmatch '^[0-9A-F]{40}$') { throw 'PACKAGE_TRUST_FIXTURE_THUMBPRINT_INVALID' }
    $registryPath = 'Software\Microsoft\SystemCertificates\Root\Certificates\' + $normalized
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($registryPath)
    try { return $null -ne $key } finally { if ($key) { $key.Dispose() } }
}

function Remove-CurrentUserRootCertificate {
    param([Parameter(Mandatory)] [string] $Thumbprint)

    $normalized = Normalize-Thumbprint -Value $Thumbprint
    if ($normalized -notmatch '^[0-9A-F]{40}$') { throw 'PACKAGE_TRUST_FIXTURE_THUMBPRINT_INVALID' }
    if (-not (Test-CurrentUserRootCertificate -Thumbprint $normalized)) { return }
    try { Invoke-CertUtil -Arguments @('-user', '-delstore', 'Root', $normalized) } catch { }
    if (-not (Test-CurrentUserRootCertificate -Thumbprint $normalized)) { return }
    $parentPath = 'Software\Microsoft\SystemCertificates\Root\Certificates'
    $parent = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($parentPath, $true)
    if (-not $parent) { throw 'PACKAGE_TRUST_FIXTURE_ROOT_STORE_MISSING' }
    try { $parent.DeleteSubKey($normalized, $false) } finally { $parent.Dispose() }
    if (Test-CurrentUserRootCertificate -Thumbprint $normalized) {
        throw 'PACKAGE_TRUST_FIXTURE_ROOT_REMOVE_FAILED'
    }
}

function Get-RootCertificateTempPath {
    param([Parameter(Mandatory)] [string] $Thumbprint)

    $normalized = Normalize-Thumbprint -Value $Thumbprint
    if ($normalized -notmatch '^[0-9A-F]{40}$') { throw 'PACKAGE_TRUST_FIXTURE_THUMBPRINT_INVALID' }
    return Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-test-root-' + $normalized + '.cer')
}

function Remove-PrivateKeyForCertificate {
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
        throw 'PACKAGE_TRUST_FIXTURE_PRIVATE_KEY_PROVIDER_UNSUPPORTED'
    }
    try { Invoke-CertUtil -Arguments @('-user', '-csp', 'KSP', '-delkey', $keyName) }
    catch { throw 'PACKAGE_TRUST_FIXTURE_PRIVATE_KEY_DELETE_FAILED' }
}

function Test-CertificateAbsent {
    param(
        [Parameter(Mandatory)] [ValidateSet('My', 'Root', 'CA')] [string] $StoreName,
        [Parameter(Mandatory)] [string] $Thumbprint
    )

    $normalized = Normalize-Thumbprint -Value $Thumbprint
    if ($StoreName -eq 'Root') {
        return -not (Test-CurrentUserRootCertificate -Thumbprint $normalized)
    }
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

function Add-CertificateToCurrentUserRoot {
    param([Parameter(Mandatory)] [Security.Cryptography.X509Certificates.X509Certificate2] $Certificate)

    $normalized = Normalize-Thumbprint -Value $Certificate.Thumbprint
    if (Test-CurrentUserRootCertificate -Thumbprint $normalized) {
        throw 'PACKAGE_TRUST_FIXTURE_ROOT_ALREADY_PRESENT'
    }
    $certificatePath = Get-RootCertificateTempPath -Thumbprint $normalized
    try {
        [IO.File]::WriteAllBytes($certificatePath, $Certificate.RawData)
        Invoke-CertUtil -Arguments @('-user', '-f', '-addstore', 'Root', $certificatePath)
    } catch {
        throw ('PACKAGE_TRUST_FIXTURE_ROOT_INSTALL_FAILED: ' + $_.Exception.Message)
    } finally {
        if ([IO.File]::Exists($certificatePath)) { [IO.File]::Delete($certificatePath) }
    }
    if (-not (Test-CurrentUserRootCertificate -Thumbprint $normalized)) {
        throw 'PACKAGE_TRUST_FIXTURE_ROOT_INSTALL_FAILED'
    }
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
    throw 'PACKAGE_TRUST_FIXTURE_SIGNTOOL_MISSING'
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
    if ($reader.HasData) { throw 'PACKAGE_TRUST_FIXTURE_TIMESTAMP_REQUEST_INVALID' }
    if ($algorithmOid -ne '2.16.840.1.101.3.4.2.1' -or $hashedMessage.Length -ne 32) {
        throw 'PACKAGE_TRUST_FIXTURE_TIMESTAMP_REQUEST_NOT_SHA256'
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
        if ($value -lt 0) { throw 'PACKAGE_TRUST_FIXTURE_TIMESTAMP_HTTP_EOF' }
        [void]$headerBytes.Add([byte]$value)
        $count = $headerBytes.Count
        if ($count -ge 4 -and $headerBytes[$count - 4] -eq 13 -and
            $headerBytes[$count - 3] -eq 10 -and $headerBytes[$count - 2] -eq 13 -and
            $headerBytes[$count - 1] -eq 10) { break }
    }
    $headers = [Text.Encoding]::ASCII.GetString($headerBytes.ToArray())
    $match = [Text.RegularExpressions.Regex]::Match($headers, '(?im)^Content-Length:\s*(\d+)\s*$')
    if (-not $match.Success) { throw 'PACKAGE_TRUST_FIXTURE_TIMESTAMP_CONTENT_LENGTH_MISSING' }
    $length = [int]$match.Groups[1].Value
    $body = [byte[]]::new($length)
    $read = 0
    while ($read -lt $length) {
        $received = $Stream.Read($body, $read, $length - $read)
        if ($received -le 0) { throw 'PACKAGE_TRUST_FIXTURE_TIMESTAMP_HTTP_BODY_TRUNCATED' }
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
        if (-not $process.Start()) { throw 'PACKAGE_TRUST_FIXTURE_TIMESTAMP_SIGNTOOL_START_FAILED' }
        if (-not $acceptTask.Wait(30000)) { throw 'PACKAGE_TRUST_FIXTURE_TIMESTAMP_REQUEST_TIMEOUT' }
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
        if (-not $process.WaitForExit(30000)) { throw 'PACKAGE_TRUST_FIXTURE_TIMESTAMP_SIGNTOOL_TIMEOUT' }
        if ($process.ExitCode -ne 0) { throw 'PACKAGE_TRUST_FIXTURE_TIMESTAMP_SIGNTOOL_FAILED' }
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
    if ($cms.SignerInfos.Count -ne 1) { throw 'PACKAGE_TRUST_FIXTURE_SIGNER_COUNT_INVALID' }
    Copy-Item -LiteralPath $InputSignaturePath -Destination $OutputSignaturePath -Force
    Invoke-LocalRfc3161P7Timestamp -OutputSignaturePath $OutputSignaturePath `
        -TimestampCertificate $TimestampCertificate -TimestampTime $TimestampTime
}

function Remove-FixtureCertificates {
    param([Parameter(Mandatory)] $State)

    if (-not [string]::IsNullOrWhiteSpace($State.rootThumbprint)) {
        $rootCertificatePath = Get-RootCertificateTempPath -Thumbprint $State.rootThumbprint
        if (Test-Path -LiteralPath $rootCertificatePath -PathType Leaf) {
            Remove-Item -LiteralPath $rootCertificatePath -Force
        }
    }
    Remove-CertificateThumbprint -StoreName Root -Thumbprint $State.rootThumbprint
    Remove-CertificateThumbprint -StoreName CA -Thumbprint $State.rootThumbprint
    Remove-CertificateThumbprint -StoreName My -Thumbprint $State.timestampThumbprint
    Remove-CertificateThumbprint -StoreName My -Thumbprint $State.leafThumbprint
    Remove-CertificateThumbprint -StoreName My -Thumbprint $State.rootThumbprint
    Remove-CertificateThumbprint -StoreName CA -Thumbprint $State.timestampThumbprint
    Remove-CertificateThumbprint -StoreName CA -Thumbprint $State.leafThumbprint
    Remove-CertificateThumbprint -StoreName CA -Thumbprint $State.rootThumbprint
    foreach ($storeName in @('Root', 'CA', 'My')) {
        if (-not (Test-CertificateAbsent -StoreName $storeName -Thumbprint $State.rootThumbprint)) {
            throw "PACKAGE_TRUST_FIXTURE_CERTIFICATE_REMAINS: $storeName root"
        }
    }
    foreach ($thumbprint in @($State.leafThumbprint, $State.timestampThumbprint)) {
        foreach ($storeName in @('My', 'CA')) {
            if (-not (Test-CertificateAbsent -StoreName $storeName -Thumbprint $thumbprint)) {
                throw "PACKAGE_TRUST_FIXTURE_CERTIFICATE_REMAINS: $storeName $thumbprint"
            }
        }
    }
}

function Prepare-Fixture {
    New-Item -ItemType Directory -Path $PackageRoot -Force | Out-Null
    if (-not (Test-Path -LiteralPath (Join-Path $PackageRoot 'FastFiles.package.manifest') -PathType Leaf)) {
        throw 'PACKAGE_TRUST_FIXTURE_MANIFEST_MISSING'
    }
    $tag = [guid]::NewGuid().ToString('N')
    $root = $null
    $leaf = $null
    $timestamp = $null
    $rootTrusted = $false
    try {
        $now = Get-Date
        $root = New-SelfSignedCertificate -Subject "CN=FastFiles TEST CERTIFICATE NON-PRODUCTION package root $tag" -Type Custom `
            -TextExtension '2.5.29.19={text}CA=TRUE&pathlength=1' `
            -KeyUsage CertSign, CRLSign, DigitalSignature -CertStoreLocation 'Cert:\CurrentUser\My' `
            -KeyExportPolicy Exportable -NotBefore $now.AddDays(-2) -NotAfter $now.AddDays(30)
        $leaf = New-SelfSignedCertificate -Subject "CN=FastFiles TEST CERTIFICATE NON-PRODUCTION package code-signing $tag" `
            -Type CodeSigningCert -Signer $root -KeyUsage DigitalSignature -CertStoreLocation 'Cert:\CurrentUser\My' `
            -KeyExportPolicy Exportable -NotBefore $now.AddDays(-1) -NotAfter $now.AddDays(7)
        $timestamp = New-SelfSignedCertificate -Subject "CN=FastFiles TEST CERTIFICATE NON-PRODUCTION package timestamp $tag" `
            -Type Custom -Signer $root -KeyUsage DigitalSignature `
            -TextExtension '2.5.29.37={critical}{text}1.3.6.1.5.5.7.3.8' -CertStoreLocation 'Cert:\CurrentUser\My' `
            -KeyExportPolicy Exportable -NotBefore $now.AddDays(-2) -NotAfter $now.AddDays(7)

        # Persist the exact certificate identities before the trust-store
        # mutation so a process-level timeout can still run Cleanup.
        $state = [pscustomobject]@{
            rootThumbprint = Normalize-Thumbprint -Value $root.Thumbprint
            leafThumbprint = Normalize-Thumbprint -Value $leaf.Thumbprint
            timestampThumbprint = Normalize-Thumbprint -Value $timestamp.Thumbprint
            rootTrusted = $false
        }
        $state | ConvertTo-Json -Compress | Set-Content -LiteralPath $StatePath -Encoding UTF8

        Add-CertificateToCurrentUserRoot -Certificate $root
        $rootTrusted = $true
        $state.rootTrusted = $true
        $state | ConvertTo-Json -Compress | Set-Content -LiteralPath $StatePath -Encoding UTF8
        Add-Type -AssemblyName System.Security.Cryptography.Pkcs -ErrorAction Stop
        $manifestPath = Join-Path $PackageRoot 'FastFiles.package.manifest'
        $contentInfo = [Security.Cryptography.Pkcs.ContentInfo]::new([IO.File]::ReadAllBytes($manifestPath))
        $cms = [Security.Cryptography.Pkcs.SignedCms]::new($contentInfo, $true)
        $cmsSigner = [Security.Cryptography.Pkcs.CmsSigner]::new($leaf)
        $cms.ComputeSignature($cmsSigner)
        $unsignedPath = Join-Path $PackageRoot 'unsigned.manifest.p7s'
        $signaturePath = Join-Path $PackageRoot 'FastFiles.package.manifest.p7s'
        [IO.File]::WriteAllBytes($unsignedPath, $cms.Encode())
        New-LocalRfc3161DetachedSignature -InputSignaturePath $unsignedPath -OutputSignaturePath $signaturePath `
            -TimestampCertificate $timestamp
        [IO.File]::Delete($unsignedPath)
        $state | ConvertTo-Json -Compress | Set-Content -LiteralPath $StatePath -Encoding UTF8
    } catch {
        if ($root) {
            Remove-CertificateThumbprint -StoreName Root -Thumbprint $root.Thumbprint
            Remove-CertificateThumbprint -StoreName CA -Thumbprint $root.Thumbprint
        }
        if ($timestamp) {
            Remove-CertificateThumbprint -StoreName CA -Thumbprint $timestamp.Thumbprint
            Remove-CertificateThumbprint -StoreName My -Thumbprint $timestamp.Thumbprint
        }
        if ($leaf) {
            Remove-CertificateThumbprint -StoreName CA -Thumbprint $leaf.Thumbprint
            Remove-CertificateThumbprint -StoreName My -Thumbprint $leaf.Thumbprint
        }
        if ($root) {
            Remove-CertificateThumbprint -StoreName CA -Thumbprint $root.Thumbprint
            Remove-CertificateThumbprint -StoreName My -Thumbprint $root.Thumbprint
        }
        throw
    }
}

function Cleanup-Fixture {
    if (-not (Test-Path -LiteralPath $StatePath -PathType Leaf)) { throw 'PACKAGE_TRUST_FIXTURE_STATE_MISSING' }
    $state = Get-Content -LiteralPath $StatePath -Raw | ConvertFrom-Json
    Remove-FixtureCertificates -State $state
    if (Test-Path -LiteralPath $PackageRoot -PathType Container) {
        Remove-Item -LiteralPath $PackageRoot -Recurse -Force -ErrorAction Stop
    }
    if (Test-Path -LiteralPath $StatePath -PathType Leaf) {
        Remove-Item -LiteralPath $StatePath -Force -ErrorAction Stop
    }
    if ((Test-Path -LiteralPath $PackageRoot -PathType Container) -or
        (Test-Path -LiteralPath $StatePath -PathType Leaf)) {
        throw 'PACKAGE_TRUST_FIXTURE_FILES_REMAIN'
    }
}

try {
    if ($Mode -eq 'Prepare') { Prepare-Fixture } else { Cleanup-Fixture }
    exit 0
} catch {
    Write-Error $_.Exception.Message
    exit 1
}
