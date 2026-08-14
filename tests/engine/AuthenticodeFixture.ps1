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
        [Parameter(Mandatory)] [ValidateSet('My', 'Root')] [string] $StoreName,
        [AllowNull()] [string] $Thumbprint
    )

    if ([string]::IsNullOrWhiteSpace($Thumbprint)) { return }
    $store = [Security.Cryptography.X509Certificates.X509Store]::new($StoreName, 'CurrentUser')
    try {
        $store.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
        $normalized = ($Thumbprint -replace '[\s-]', '').ToUpperInvariant()
        foreach ($certificate in @($store.Certificates)) {
            $candidate = ([string]$certificate.Thumbprint -replace '[\s-]', '').ToUpperInvariant()
            if ($candidate -ceq $normalized) { $store.Remove($certificate) }
        }
    } finally {
        $store.Close()
        $store.Dispose()
    }
}

function Add-CertificateToCurrentUserRoot {
    param([Parameter(Mandatory)] [Security.Cryptography.X509Certificates.X509Certificate2] $Certificate)

    $thumbprint = ($Certificate.Thumbprint -replace '[\s-]', '').ToUpperInvariant()
    $publicCertificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new($Certificate.RawData)
    $store = [Security.Cryptography.X509Certificates.X509Store]::new('Root', 'CurrentUser')
    try {
        $store.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
        if (@($store.Certificates | Where-Object {
                (($_.Thumbprint -replace '[\s-]', '').ToUpperInvariant()) -ceq $thumbprint
            }).Count -ne 0) {
            throw 'AUTHENTICODE_FIXTURE_ROOT_ALREADY_TRUSTED'
        }
        [void]$store.Add($publicCertificate)
    } finally {
        $store.Close()
        $store.Dispose()
        $publicCertificate.Dispose()
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
        [Parameter(Mandatory)] [ValidateSet('My', 'Root')] [string] $StoreName,
        [AllowNull()] [string] $Thumbprint
    )

    if ([string]::IsNullOrWhiteSpace($Thumbprint)) { return $true }
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
            if (-not [string]::IsNullOrWhiteSpace($state.rootThumbprint) -and
                -not (Test-CertificateAbsent -StoreName Root -Thumbprint $state.rootThumbprint)) {
                Remove-CertificateThumbprint -StoreName Root -Thumbprint $state.rootThumbprint
            }
            Remove-CertificateThumbprint -StoreName My -Thumbprint $state.timestampThumbprint
            Remove-CertificateThumbprint -StoreName My -Thumbprint $state.leafThumbprint
            Remove-CertificateThumbprint -StoreName My -Thumbprint $state.rootThumbprint
            $certificatesClean =
                (Test-CertificateAbsent -StoreName My -Thumbprint $state.timestampThumbprint) -and
                (Test-CertificateAbsent -StoreName My -Thumbprint $state.leafThumbprint) -and
                (Test-CertificateAbsent -StoreName My -Thumbprint $state.rootThumbprint) -and
                (Test-CertificateAbsent -StoreName Root -Thumbprint $state.rootThumbprint)
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
        $existingRoot = Get-ChildItem -Path 'Cert:\CurrentUser\My', 'Cert:\CurrentUser\Root' -ErrorAction Stop |
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
            if ($root -and -not (Test-CertificateAbsent -StoreName Root -Thumbprint $root.Thumbprint)) {
                Remove-CertificateThumbprint -StoreName Root -Thumbprint $root.Thumbprint
            }
            if ($timestamp) { Remove-CertificateThumbprint -StoreName My -Thumbprint $timestamp.Thumbprint }
            if ($leaf) { Remove-CertificateThumbprint -StoreName My -Thumbprint $leaf.Thumbprint }
            if ($root) { Remove-CertificateThumbprint -StoreName My -Thumbprint $root.Thumbprint }
            if (Test-Path -LiteralPath $outputFullPath -PathType Leaf) { Remove-Item -LiteralPath $outputFullPath -Force }
            if (Test-Path -LiteralPath $thumbprintPath -PathType Leaf) { Remove-Item -LiteralPath $thumbprintPath -Force }
        }
    } catch { }
    Write-Error $_.Exception.Message
    exit 1
}
