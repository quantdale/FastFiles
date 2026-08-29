[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('Prepare', 'Expire', 'Validate', 'Cleanup')]
    [string] $Action,
    [Parameter(Mandatory)]
    [string] $StatePath,
    [string] $SourceBinary,
    [string] $OutputPath,
    [ValidateSet('Valid', 'Expired', 'TimestampedExpired', 'NotYetValid', 'Untrusted', 'SelfSigned')]
    [string] $Case
)

$ErrorActionPreference = 'Stop'

function Remove-CertificateThumbprint {
    param(
        [Parameter(Mandatory)] [ValidateSet('My', 'Root', 'CA')] [string] $StoreName,
        [AllowNull()] [string] $Thumbprint
    )

    if ([string]::IsNullOrWhiteSpace($Thumbprint)) { return }
    if ($StoreName -eq 'Root') {
        Remove-CurrentUserRootCertificate -Thumbprint $Thumbprint
        return
    }
    $store = [Security.Cryptography.X509Certificates.X509Store]::new($StoreName, 'CurrentUser')
    try {
        $store.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
        $normalized = ($Thumbprint -replace '[\s-]', '').ToUpperInvariant()
        foreach ($certificate in @($store.Certificates)) {
            $candidate = ([string]$certificate.Thumbprint -replace '[\s-]', '').ToUpperInvariant()
            if ($candidate -ceq $normalized) {
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
        throw 'AUTHENTICODE_FIXTURE_CERTUTIL_MISSING'
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
        if (-not $process.Start()) { throw 'AUTHENTICODE_FIXTURE_CERTUTIL_START_FAILED' }
        if (-not $process.WaitForExit(30000)) {
            if (-not $process.HasExited) {
                try { $process.Kill($true) } catch { if (-not $process.HasExited) { $process.Kill() } }
            }
            $process.WaitForExit()
            throw 'AUTHENTICODE_FIXTURE_CERTUTIL_TIMEOUT'
        }
        if ($process.ExitCode -ne 0) { throw 'AUTHENTICODE_FIXTURE_CERTUTIL_FAILED' }
    } finally {
        $process.Dispose()
    }
}

function Test-CurrentUserRootCertificate {
    param([Parameter(Mandatory)] [string] $Thumbprint)

    $normalized = ($Thumbprint -replace '[\s-]', '').ToUpperInvariant()
    if ($normalized -notmatch '^[0-9A-F]{40}$') { throw 'AUTHENTICODE_FIXTURE_THUMBPRINT_INVALID' }
    $registryPath = 'Software\Microsoft\SystemCertificates\Root\Certificates\' + $normalized
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($registryPath)
    try { return $null -ne $key } finally { if ($key) { $key.Dispose() } }
}

function Remove-CurrentUserRootCertificate {
    param([Parameter(Mandatory)] [string] $Thumbprint)

    $normalized = ($Thumbprint -replace '[\s-]', '').ToUpperInvariant()
    if ($normalized -notmatch '^[0-9A-F]{40}$') { throw 'AUTHENTICODE_FIXTURE_THUMBPRINT_INVALID' }
    if (-not (Test-CurrentUserRootCertificate -Thumbprint $normalized)) { return }
    try { Invoke-CertUtil -Arguments @('-user', '-delstore', 'Root', $normalized) } catch { }
    if (-not (Test-CurrentUserRootCertificate -Thumbprint $normalized)) { return }
    $parentPath = 'Software\Microsoft\SystemCertificates\Root\Certificates'
    $parent = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($parentPath, $true)
    if (-not $parent) { throw 'AUTHENTICODE_FIXTURE_ROOT_STORE_MISSING' }
    try { $parent.DeleteSubKey($normalized, $false) } finally { $parent.Dispose() }
    if (Test-CurrentUserRootCertificate -Thumbprint $normalized) {
        throw 'AUTHENTICODE_FIXTURE_ROOT_REMOVE_FAILED'
    }
}

function Get-RootCertificateTempPath {
    param([Parameter(Mandatory)] [string] $Thumbprint)

    $normalized = ($Thumbprint -replace '[\s-]', '').ToUpperInvariant()
    if ($normalized -notmatch '^[0-9A-F]{40}$') { throw 'AUTHENTICODE_FIXTURE_THUMBPRINT_INVALID' }
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
        throw 'AUTHENTICODE_FIXTURE_PRIVATE_KEY_PROVIDER_UNSUPPORTED'
    }
    try { Invoke-CertUtil -Arguments @('-user', '-csp', 'KSP', '-delkey', $keyName) }
    catch { throw 'AUTHENTICODE_FIXTURE_PRIVATE_KEY_DELETE_FAILED' }
}

if (-not ('FfSilentUserRoot' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

public static class FfSilentUserRoot
{
    // CERT_STORE_PROV_REG on the HKCU Root key writes the encoded certificate
    // without Crypt32's "install this CA?" UI. certutil -addstore and
    // X509Store.Add hang on that dialog when stdout is redirected.
    const int CERT_STORE_PROV_REG = 4;
    const uint X509_ASN_ENCODING = 0x00000001;
    const uint PKCS_7_ASN_ENCODING = 0x00010000;
    const uint CERT_STORE_ADD_REPLACE_EXISTING = 3;
    const uint KEY_ALL_ACCESS = 0xF003F;
    static readonly IntPtr HKEY_CURRENT_USER = new IntPtr(unchecked((int)0x80000001));

    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern int RegOpenKeyEx(IntPtr hKey, string lpSubKey, uint ulOptions, uint samDesired, out IntPtr phkResult);
    [DllImport("advapi32.dll")]
    static extern int RegCloseKey(IntPtr hKey);
    [DllImport("crypt32.dll", SetLastError = true)]
    static extern IntPtr CertOpenStore(IntPtr lpszStoreProvider, uint dwMsgAndCertEncodingType,
        IntPtr hCryptProv, uint dwFlags, IntPtr pvPara);
    [DllImport("crypt32.dll", SetLastError = true)]
    static extern bool CertCloseStore(IntPtr hCertStore, uint dwFlags);
    [DllImport("crypt32.dll", SetLastError = true)]
    static extern bool CertAddEncodedCertificateToStore(IntPtr hCertStore, uint dwCertEncodingType,
        byte[] pbCertEncoded, uint cbCertEncoded, uint dwAddDisposition, IntPtr ppCertContext);

    public static void AddEncoded(byte[] encoded)
    {
        if (encoded == null || encoded.Length == 0) throw new ArgumentException("encoded");
        IntPtr hKey;
        int rc = RegOpenKeyEx(HKEY_CURRENT_USER, "Software\\Microsoft\\SystemCertificates\\Root", 0, KEY_ALL_ACCESS, out hKey);
        if (rc != 0) throw new Win32Exception(rc, "RegOpenKeyEx CurrentUser Root");
        try
        {
            IntPtr store = CertOpenStore(new IntPtr(CERT_STORE_PROV_REG), 0, IntPtr.Zero, 0, hKey);
            if (store == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error(), "CertOpenStore REG Root");
            try
            {
                if (!CertAddEncodedCertificateToStore(store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                    encoded, (uint)encoded.Length, CERT_STORE_ADD_REPLACE_EXISTING, IntPtr.Zero))
                {
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "CertAddEncodedCertificateToStore");
                }
            }
            finally { CertCloseStore(store, 0); }
        }
        finally { RegCloseKey(hKey); }
    }
}
'@
}

function Test-ProtectedRootMembership {
    param([Parameter(Mandatory)] [string] $Thumbprint)

    $normalized = ($Thumbprint -replace '[\s-]', '').ToUpperInvariant()
    if ($normalized -notmatch '^[0-9A-F]{40}$') { return $false }
    $path = 'HKCU:\Software\Microsoft\SystemCertificates\Root\ProtectedRoots'
    if (-not (Test-Path -LiteralPath $path)) { return $false }
    $blob = (Get-ItemProperty -LiteralPath $path).Certificates
    if ($null -eq $blob -or $blob.Length -lt 20) { return $false }
    $hay = [BitConverter]::ToString([byte[]]$blob) -replace '-', ''
    return $hay.Contains($normalized)
}

function Add-CertificateToCurrentUserRoot {
    param([Parameter(Mandatory)] [Security.Cryptography.X509Certificates.X509Certificate2] $Certificate)

    $thumbprint = ($Certificate.Thumbprint -replace '[\s-]', '').ToUpperInvariant()
    if (Test-CurrentUserRootCertificate -Thumbprint $thumbprint) {
        throw 'AUTHENTICODE_FIXTURE_ROOT_ALREADY_TRUSTED'
    }
    try {
        [FfSilentUserRoot]::AddEncoded($Certificate.RawData)
    } catch {
        throw ('AUTHENTICODE_FIXTURE_ROOT_INSTALL_FAILED: ' + $_.Exception.Message)
    }
    $rootKey = 'HKCU:\Software\Microsoft\SystemCertificates\Root\Certificates\' + $thumbprint
    $caKey = 'HKCU:\Software\Microsoft\SystemCertificates\CA\Certificates\' + $thumbprint
    if (-not (Test-Path -LiteralPath $rootKey) -and (Test-Path -LiteralPath $caKey)) {
        Copy-Item -LiteralPath $caKey -Destination $rootKey -Recurse -Force
    }
    if (-not (Test-CurrentUserRootCertificate -Thumbprint $thumbprint)) {
        throw 'AUTHENTICODE_FIXTURE_ROOT_INSTALL_FAILED'
    }
    # Crypt32 only trusts CurrentUser roots listed in ProtectedRoots. That
    # list is ACL-protected; certutil/X509Store.Add update it through a GUI
    # confirmation that hangs when the fixture has no desktop. Fail closed
    # with a stable reason instead of a 30s certutil timeout then WinTrust
    # CERT_E_UNTRUSTEDROOT.
    if (-not (Test-ProtectedRootMembership -Thumbprint $thumbprint)) {
        throw 'AUTHENTICODE_FIXTURE_PROTECTEDROOTS_UI_REQUIRED'
    }
}

function Test-CustomRootChain {
    param([Parameter(Mandatory)] [string] $Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw 'AUTHENTICODE_FIXTURE_STATE_MISSING' }
    $state = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    $root = Get-Item -LiteralPath ('Cert:\CurrentUser\My\' + $state.rootThumbprint)
    $leaf = Get-Item -LiteralPath ('Cert:\CurrentUser\My\' + $state.leafThumbprint)
    $chain = [Security.Cryptography.X509Certificates.X509Chain]::new()
    try {
        $chain.ChainPolicy.TrustMode =
            [Security.Cryptography.X509Certificates.X509ChainTrustMode]::CustomRootTrust
        [void]$chain.ChainPolicy.CustomTrustStore.Add($root)
        $chain.ChainPolicy.RevocationMode =
            [Security.Cryptography.X509Certificates.X509RevocationMode]::NoCheck
        $chain.ChainPolicy.VerificationFlags =
            [Security.Cryptography.X509Certificates.X509VerificationFlags]::NoFlag
        [void]$chain.ChainPolicy.ApplicationPolicy.Add(
            [Security.Cryptography.Oid]::new('1.3.6.1.5.5.7.3.3'))
        $built = [bool]$chain.Build($leaf)
        $statuses = @($chain.ChainStatus | ForEach-Object { $_.Status })
        $expired = [DateTime]::UtcNow -gt ([DateTime]$leaf.NotAfter).ToUniversalTime()
        $timeInvalid = @($chain.ChainStatus | Where-Object {
            ($_.Status -band [Security.Cryptography.X509Certificates.X509ChainStatusFlags]::NotTimeValid) -ne 0
        }).Count -gt 0
        if ($state.case -in @('Expired', 'TimestampedExpired')) {
            if ($expired -and ($built -or -not $timeInvalid)) {
                throw 'AUTHENTICODE_FIXTURE_EXPIRED_CHAIN_STATUS_MISSING'
            }
            if (-not $expired -and -not $built) {
                throw 'AUTHENTICODE_FIXTURE_CURRENT_CHAIN_INVALID'
            }
        } elseif (-not $built) {
            throw ('AUTHENTICODE_FIXTURE_CUSTOM_ROOT_CHAIN_INVALID: ' + ($statuses -join ','))
        }
    } finally {
        $chain.Dispose()
    }
}

function Test-CertificateAbsent {
    param(
        [Parameter(Mandatory)] [ValidateSet('My', 'Root', 'CA')] [string] $StoreName,
        [AllowNull()] [string] $Thumbprint
    )

    if ([string]::IsNullOrWhiteSpace($Thumbprint)) { return $true }
    if ($StoreName -eq 'Root') {
        return -not (Test-CurrentUserRootCertificate -Thumbprint $Thumbprint)
    }
    $store = [Security.Cryptography.X509Certificates.X509Store]::new($StoreName, 'CurrentUser')
    try {
        $store.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadOnly)
        $normalized = ($Thumbprint -replace '[\s-]', '').ToUpperInvariant()
        return @($store.Certificates | Where-Object {
            (([string]$_.Thumbprint -replace '[\s-]', '').ToUpperInvariant()) -ceq $normalized
        }).Count -eq 0
    } finally {
        $store.Close()
        $store.Dispose()
    }
}

function Find-SignTool {
    $candidates = [System.Collections.Generic.List[string]]::new()
    $command = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($command) { $candidates.Add([IO.Path]::GetFullPath($command.Source)) }
    $programFilesX86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
    if ($programFilesX86) {
        $windowsKits = Join-Path $programFilesX86 'Windows Kits\10\bin'
        if (Test-Path -LiteralPath $windowsKits -PathType Container) {
            foreach ($candidate in @(Get-ChildItem -LiteralPath $windowsKits -Recurse -Filter 'signtool.exe' -File -ErrorAction SilentlyContinue |
                Where-Object { $_.FullName -match '\\x64\\signtool\.exe$' })) {
                $candidates.Add($candidate.FullName)
            }
        }
    }
    $selected = @($candidates | Select-Object -Unique | Select-Object -First 1)
    if ($selected.Count -eq 0) { throw 'AUTHENTICODE_FIXTURE_SIGNTOOL_MISSING' }
    return $selected[0]
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
    if ($reader.HasData) { throw 'AUTHENTICODE_FIXTURE_TIMESTAMP_REQUEST_INVALID' }
    if ($algorithmOid -ne '2.16.840.1.101.3.4.2.1' -or $hashedMessage.Length -ne 32) {
        throw 'AUTHENTICODE_FIXTURE_TIMESTAMP_REQUEST_NOT_SHA256'
    }
    [pscustomobject]@{ hashedMessage = $hashedMessage; nonce = $nonce }
}

function New-Rfc3161Response {
    param(
        [Parameter(Mandatory)] [byte[]] $RequestBytes,
        [Parameter(Mandatory)] [Security.Cryptography.X509Certificates.X509Certificate2] $TimestampCertificate
    )

    $request = Read-Rfc3161Request -RequestBytes $RequestBytes
    $timestampTime = [DateTimeOffset]::UtcNow
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
    $tstWriter.WriteInteger([long]$timestampTime.ToUnixTimeMilliseconds())
    $tstWriter.WriteGeneralizedTime($timestampTime, $true)
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
        $timestampTime.ToUniversalTime().ToString("yyMMddHHmmss'Z'", [Globalization.CultureInfo]::InvariantCulture))
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
    $responseWriter.Encode()
}

function Read-TcpHttpRequestBody {
    param([Parameter(Mandatory)] [Net.Sockets.NetworkStream] $Stream)

    $headerBytes = [Collections.Generic.List[byte]]::new()
    while ($true) {
        $value = $Stream.ReadByte()
        if ($value -lt 0) { throw 'AUTHENTICODE_FIXTURE_TIMESTAMP_HTTP_EOF' }
        [void]$headerBytes.Add([byte]$value)
        $count = $headerBytes.Count
        if ($count -ge 4 -and $headerBytes[$count - 4] -eq 13 -and
            $headerBytes[$count - 3] -eq 10 -and $headerBytes[$count - 2] -eq 13 -and
            $headerBytes[$count - 1] -eq 10) { break }
    }
    $headers = [Text.Encoding]::ASCII.GetString($headerBytes.ToArray())
    $match = [Text.RegularExpressions.Regex]::Match($headers, '(?im)^Content-Length:\s*(\d+)\s*$')
    if (-not $match.Success) { throw 'AUTHENTICODE_FIXTURE_TIMESTAMP_CONTENT_LENGTH_MISSING' }
    $length = [int]$match.Groups[1].Value
    $body = [byte[]]::new($length)
    $read = 0
    while ($read -lt $length) {
        $received = $Stream.Read($body, $read, $length - $read)
        if ($received -le 0) { throw 'AUTHENTICODE_FIXTURE_TIMESTAMP_HTTP_BODY_TRUNCATED' }
        $read += $received
    }
    $body
}

function Invoke-LocalRfc3161Timestamp {
    param(
        [Parameter(Mandatory)] [string] $SignToolPath,
        [Parameter(Mandatory)] [string] $OutputPath,
        [Parameter(Mandatory)] [Security.Cryptography.X509Certificates.X509Certificate2] $TimestampCertificate
    )

    $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $listener.Start()
    $client = $null
    $process = $null
    try {
        $port = ([Net.IPEndPoint]$listener.LocalEndpoint).Port
        $acceptTask = $listener.AcceptTcpClientAsync()
        $startInfo = [Diagnostics.ProcessStartInfo]::new()
        $startInfo.FileName = $SignToolPath
        $startInfo.UseShellExecute = $false
        $startInfo.CreateNoWindow = $true
        foreach ($argument in @('timestamp', '/tr', "http://127.0.0.1:$port/", '/td', 'SHA256', $OutputPath)) {
            [void]$startInfo.ArgumentList.Add($argument)
        }
        $process = [Diagnostics.Process]::new()
        $process.StartInfo = $startInfo
        if (-not $process.Start()) { throw 'AUTHENTICODE_FIXTURE_TIMESTAMP_SIGNTOOL_START_FAILED' }
        if (-not $acceptTask.Wait(30000)) { throw 'AUTHENTICODE_FIXTURE_TIMESTAMP_REQUEST_TIMEOUT' }
        $client = $acceptTask.GetAwaiter().GetResult()
        $stream = $client.GetStream()
        $requestBytes = Read-TcpHttpRequestBody -Stream $stream
        $responseBytes = New-Rfc3161Response -RequestBytes $requestBytes -TimestampCertificate $TimestampCertificate
        $header = [Text.Encoding]::ASCII.GetBytes(
            "HTTP/1.1 200 OK`r`nContent-Type: application/timestamp-reply`r`nContent-Length: $($responseBytes.Length)`r`nConnection: close`r`n`r`n")
        $stream.Write($header, 0, $header.Length)
        $stream.Write($responseBytes, 0, $responseBytes.Length)
        $stream.Flush()
        $client.Close()
        $client = $null
        if (-not $process.WaitForExit(30000)) { throw 'AUTHENTICODE_FIXTURE_TIMESTAMP_SIGNTOOL_TIMEOUT' }
        if ($process.ExitCode -ne 0) { throw 'AUTHENTICODE_FIXTURE_TIMESTAMP_SIGNTOOL_FAILED' }
    } finally {
        if ($client) { $client.Close() }
        if ($process) {
            if (-not $process.HasExited) { $process.Kill() }
            $process.Dispose()
        }
        $listener.Stop()
    }
}

function Remove-FixtureState {
    param([Parameter(Mandatory)] [string] $Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return }
    $state = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    $cleanupDeadline = (Get-Date).AddSeconds(30)
    $certificatesClean = $false
    $lastError = $null
    do {
        try {
            if (-not [string]::IsNullOrWhiteSpace($state.rootThumbprint)) {
                Remove-CertificateThumbprint -StoreName Root -Thumbprint $state.rootThumbprint
                Remove-CertificateThumbprint -StoreName CA -Thumbprint $state.rootThumbprint
            }
            Remove-CertificateThumbprint -StoreName My -Thumbprint $state.timestampThumbprint
            Remove-CertificateThumbprint -StoreName My -Thumbprint $state.leafThumbprint
            Remove-CertificateThumbprint -StoreName My -Thumbprint $state.rootThumbprint
            Remove-CertificateThumbprint -StoreName CA -Thumbprint $state.timestampThumbprint
            Remove-CertificateThumbprint -StoreName CA -Thumbprint $state.leafThumbprint
            Remove-CertificateThumbprint -StoreName CA -Thumbprint $state.rootThumbprint
            $certificatesClean =
                (Test-CertificateAbsent -StoreName My -Thumbprint $state.timestampThumbprint) -and
                (Test-CertificateAbsent -StoreName My -Thumbprint $state.leafThumbprint) -and
                (Test-CertificateAbsent -StoreName My -Thumbprint $state.rootThumbprint) -and
                (Test-CertificateAbsent -StoreName Root -Thumbprint $state.rootThumbprint) -and
                (Test-CertificateAbsent -StoreName CA -Thumbprint $state.timestampThumbprint) -and
                (Test-CertificateAbsent -StoreName CA -Thumbprint $state.leafThumbprint) -and
                (Test-CertificateAbsent -StoreName CA -Thumbprint $state.rootThumbprint)
        } catch {
            $lastError = $_.Exception.Message
        }
        if (-not $certificatesClean -and (Get-Date) -lt $cleanupDeadline) {
            Start-Sleep -Milliseconds 500
        }
    } while (-not $certificatesClean -and (Get-Date) -lt $cleanupDeadline)

    if (Test-Path -LiteralPath $state.outputPath -PathType Leaf) {
        Remove-Item -LiteralPath $state.outputPath -Force
    }
    if (Test-Path -LiteralPath $state.thumbprintPath -PathType Leaf) {
        Remove-Item -LiteralPath $state.thumbprintPath -Force
    }
    if (-not [string]::IsNullOrWhiteSpace($state.rootThumbprint)) {
        $rootCertificatePath = Get-RootCertificateTempPath -Thumbprint $state.rootThumbprint
        if (Test-Path -LiteralPath $rootCertificatePath -PathType Leaf) {
            Remove-Item -LiteralPath $rootCertificatePath -Force
        }
    }

    if (-not $certificatesClean) {
        if ($lastError) { throw "AUTHENTICODE_FIXTURE_CERTIFICATE_CLEANUP_FAILED: $lastError" }
        throw 'AUTHENTICODE_FIXTURE_CERTIFICATE_CLEANUP_FAILED'
    }
    Remove-Item -LiteralPath $Path -Force
}

function Wait-ForFixtureExpiry {
    param([Parameter(Mandatory)] [string] $Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw 'AUTHENTICODE_FIXTURE_STATE_MISSING' }
    $state = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    if ([string]::IsNullOrWhiteSpace($state.notAfter)) { throw 'AUTHENTICODE_FIXTURE_EXPIRY_MISSING' }
    $notAfter = if ($state.notAfter -is [DateTime]) {
        [DateTime]$state.notAfter
    } else {
        [DateTime]::Parse(
            [string]$state.notAfter,
            [Globalization.CultureInfo]::InvariantCulture,
            [Globalization.DateTimeStyles]::RoundtripKind)
    }
    $waitSeconds = [int][Math]::Ceiling(($notAfter - (Get-Date)).TotalSeconds) + 2
    if ($waitSeconds -gt 0) { Start-Sleep -Seconds $waitSeconds }
}

if ($Action -eq 'Expire') {
    try {
        Wait-ForFixtureExpiry -Path ([IO.Path]::GetFullPath($StatePath))
        exit 0
    } catch {
        Write-Error $_.Exception.Message
        exit 1
    }
}

if ($Action -eq 'Validate') {
    try {
        Test-CustomRootChain -Path ([IO.Path]::GetFullPath($StatePath))
        exit 0
    } catch {
        Write-Error $_.Exception.Message
        exit 1
    }
}

if ($Action -eq 'Cleanup') {
    try {
        Remove-FixtureState -Path ([IO.Path]::GetFullPath($StatePath))
        exit 0
    } catch {
        Write-Error $_.Exception.Message
        exit 1
    }
}

$root = $null
$leaf = $null
$timestamp = $null
$rootTrusted = $false
$stateFullPath = [IO.Path]::GetFullPath($StatePath)
$outputFullPath = [IO.Path]::GetFullPath($OutputPath)
$thumbprintPath = $outputFullPath + '.thumbprint'
try {
    if (-not (Test-Path -LiteralPath $SourceBinary -PathType Leaf)) { throw 'AUTHENTICODE_FIXTURE_SOURCE_MISSING' }
    $now = Get-Date
    $stateParent = Split-Path -Parent $stateFullPath
    if ($stateParent) { New-Item -ItemType Directory -Path $stateParent -Force | Out-Null }
    $state = [pscustomobject]@{
        case = $Case
        label = 'TEST CERTIFICATE / NON-PRODUCTION'
        rootTrusted = $false
        rootThumbprint = $null
        leafThumbprint = $null
        timestampThumbprint = $null
        notAfter = $null
        outputPath = $outputFullPath
        thumbprintPath = $thumbprintPath
    }
    [IO.File]::WriteAllText($stateFullPath, ($state | ConvertTo-Json -Depth 6), [Text.UTF8Encoding]::new($false))
    if ($Case -ne 'SelfSigned') {
        $tag = [guid]::NewGuid().ToString('N')
        $rootSubject = "CN=FastFiles TEST CERTIFICATE NON-PRODUCTION runtime root $tag"
        # The generated root is created in My first. The exact CurrentUser Root
        # registry key is checked before the helper installs only this public
        # certificate; do not enumerate the default protected-root PowerShell
        # view because it can stall on hosted Windows images.
        $existingRoot = Get-ChildItem -Path 'Cert:\CurrentUser\My' -ErrorAction Stop |
            Where-Object { $_.Subject -ceq $rootSubject }
        if (@($existingRoot).Count -ne 0) { throw 'AUTHENTICODE_FIXTURE_ROOT_COLLISION' }
        $root = New-SelfSignedCertificate -Subject "CN=FastFiles TEST CERTIFICATE NON-PRODUCTION runtime root $tag" `
            -Type Custom -TextExtension '2.5.29.19={text}CA=TRUE&pathlength=1' `
            -KeyUsage CertSign, CRLSign, DigitalSignature -CertStoreLocation 'Cert:\CurrentUser\My' `
            -KeyExportPolicy Exportable -NotBefore $now.AddDays(-2) -NotAfter $now.AddDays(30)
        $state.rootThumbprint = $root.Thumbprint
        [IO.File]::WriteAllText($stateFullPath, ($state | ConvertTo-Json -Depth 6), [Text.UTF8Encoding]::new($false))
    }

    $leafNotBefore = $now.AddDays(-1)
    $leafNotAfter = $now.AddDays(7)
    if ($Case -in @('Expired', 'TimestampedExpired')) {
        # SignTool will not create a new signature with an already-expired
        # certificate. Create a short-lived, currently usable leaf, sign the
        # PE, then wait past its exact NotAfter so production WinTrust sees an
        # actually expired signer at verification time.
        $leafNotBefore = (Get-Date).AddMinutes(-2)
        $leafNotAfter = (Get-Date).AddSeconds(120)
    } elseif ($Case -eq 'NotYetValid') {
        $leafNotBefore = $now.AddDays(1)
        $leafNotAfter = $now.AddDays(2)
    }

    $tag = [guid]::NewGuid().ToString('N')
    if ($Case -eq 'SelfSigned') {
        $leaf = New-SelfSignedCertificate -Subject "CN=FastFiles TEST CERTIFICATE NON-PRODUCTION runtime self-signed $tag" `
            -Type CodeSigningCert -CertStoreLocation 'Cert:\CurrentUser\My' -KeyExportPolicy Exportable `
            -NotBefore $leafNotBefore -NotAfter $leafNotAfter
    } else {
        $leaf = New-SelfSignedCertificate -Subject "CN=FastFiles TEST CERTIFICATE NON-PRODUCTION runtime leaf $tag" `
            -Type CodeSigningCert -Signer $root -CertStoreLocation 'Cert:\CurrentUser\My' `
            -KeyExportPolicy Exportable -NotBefore $leafNotBefore -NotAfter $leafNotAfter
    }

    $state.leafThumbprint = $leaf.Thumbprint
    $state.notAfter = $leaf.NotAfter.ToString('o')
    [IO.File]::WriteAllText($stateFullPath, ($state | ConvertTo-Json -Depth 6), [Text.UTF8Encoding]::new($false))
    $parent = Split-Path -Parent $outputFullPath
    if ($parent) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }

    if ($Case -eq 'TimestampedExpired') {
        $timestamp = New-SelfSignedCertificate -Subject "CN=FastFiles TEST CERTIFICATE NON-PRODUCTION runtime timestamp $tag" `
            -Type Custom -Signer $root -KeyUsage DigitalSignature -CertStoreLocation 'Cert:\CurrentUser\My' `
            -TextExtension '2.5.29.37={critical}{text}1.3.6.1.5.5.7.3.8' `
            -KeyExportPolicy Exportable -NotBefore $now.AddDays(-2) -NotAfter $now.AddDays(30)
        $state.timestampThumbprint = $timestamp.Thumbprint
        [IO.File]::WriteAllText($stateFullPath, ($state | ConvertTo-Json -Depth 6), [Text.UTF8Encoding]::new($false))
    }

    if ($Case -in @('Expired', 'TimestampedExpired')) {
        $rootTrusted = $true
        Add-CertificateToCurrentUserRoot -Certificate $root
        $state.rootTrusted = $true
        [IO.File]::WriteAllText($stateFullPath, ($state | ConvertTo-Json -Depth 6), [Text.UTF8Encoding]::new($false))
    }

    Copy-Item -LiteralPath ([IO.Path]::GetFullPath($SourceBinary)) -Destination $outputFullPath -Force
    $signtool = Find-SignTool
    & $signtool sign /fd SHA256 /sha1 $leaf.Thumbprint $outputFullPath *> $null
    if ($LASTEXITCODE -ne 0) {
        throw 'AUTHENTICODE_FIXTURE_SIGNING_FAILED'
    }
    if ($Case -eq 'TimestampedExpired') {
        Invoke-LocalRfc3161Timestamp -SignToolPath $signtool -OutputPath $outputFullPath `
            -TimestampCertificate $timestamp
    }
    [IO.File]::WriteAllText($thumbprintPath, $leaf.Thumbprint, [Text.UTF8Encoding]::new($false))
    exit 0
} catch {
    try {
        if (Test-Path -LiteralPath $stateFullPath -PathType Leaf) {
            Remove-FixtureState -Path $stateFullPath
        } else {
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
            if (Test-Path -LiteralPath $outputFullPath -PathType Leaf) { Remove-Item -LiteralPath $outputFullPath -Force }
            if (Test-Path -LiteralPath $thumbprintPath -PathType Leaf) { Remove-Item -LiteralPath $thumbprintPath -Force }
        }
    } catch { }
    Write-Error $_.Exception.Message
    exit 1
}
