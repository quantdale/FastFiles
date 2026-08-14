[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('Prepare', 'Expire', 'Validate', 'Cleanup')]
    [string] $Action,
    [Parameter(Mandatory)]
    [string] $StatePath,
    [string] $SourceBinary,
    [string] $OutputPath,
    [ValidateSet('Valid', 'Expired', 'NotYetValid', 'Untrusted', 'SelfSigned')]
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
        if ($state.case -eq 'Expired') {
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

function Remove-FixtureState {
    param([Parameter(Mandatory)] [string] $Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return }
    $state = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    $cleanupDeadline = (Get-Date).AddSeconds(30)
    $certificatesClean = $false
    $lastError = $null
    do {
        try {
            if ($state.rootTrusted) { Remove-CertificateThumbprint -StoreName Root -Thumbprint $state.rootThumbprint }
            Remove-CertificateThumbprint -StoreName My -Thumbprint $state.leafThumbprint
            Remove-CertificateThumbprint -StoreName My -Thumbprint $state.rootThumbprint
            $certificatesClean =
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
$rootTrusted = $false
$stateFullPath = [IO.Path]::GetFullPath($StatePath)
$outputFullPath = [IO.Path]::GetFullPath($OutputPath)
$thumbprintPath = $outputFullPath + '.thumbprint'
try {
    if (-not (Test-Path -LiteralPath $SourceBinary -PathType Leaf)) { throw 'AUTHENTICODE_FIXTURE_SOURCE_MISSING' }
    $now = Get-Date
    if ($Case -ne 'SelfSigned') {
        $tag = [guid]::NewGuid().ToString('N')
        $root = New-SelfSignedCertificate -Subject "CN=FastFiles TEST CERTIFICATE NON-PRODUCTION runtime root $tag" `
            -Type Custom -TextExtension '2.5.29.19={text}CA=TRUE&pathlength=1' `
            -KeyUsage CertSign, CRLSign, DigitalSignature -CertStoreLocation 'Cert:\CurrentUser\My' `
            -KeyExportPolicy Exportable -NotBefore $now.AddDays(-2) -NotAfter $now.AddDays(30)
    }

    $leafNotBefore = $now.AddDays(-1)
    $leafNotAfter = $now.AddDays(7)
    if ($Case -eq 'Expired') {
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

    $parent = Split-Path -Parent $outputFullPath
    if ($parent) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    $state = [pscustomobject]@{
        case = $Case
        label = 'TEST CERTIFICATE / NON-PRODUCTION'
        rootTrusted = $rootTrusted
        rootThumbprint = if ($root) { $root.Thumbprint } else { $null }
        leafThumbprint = $leaf.Thumbprint
        notAfter = $leaf.NotAfter.ToString('o')
        outputPath = $outputFullPath
        thumbprintPath = $thumbprintPath
    }
    $stateParent = Split-Path -Parent $stateFullPath
    if ($stateParent) { New-Item -ItemType Directory -Path $stateParent -Force | Out-Null }
    [IO.File]::WriteAllText($stateFullPath, ($state | ConvertTo-Json -Depth 6), [Text.UTF8Encoding]::new($false))

    Copy-Item -LiteralPath ([IO.Path]::GetFullPath($SourceBinary)) -Destination $outputFullPath -Force
    $signtool = Find-SignTool
    & $signtool sign /fd SHA256 /sha1 $leaf.Thumbprint $outputFullPath *> $null
    if ($LASTEXITCODE -ne 0) { throw 'AUTHENTICODE_FIXTURE_SIGNING_FAILED' }
    [IO.File]::WriteAllText($thumbprintPath, $leaf.Thumbprint, [Text.UTF8Encoding]::new($false))
    exit 0
} catch {
    try {
        if (Test-Path -LiteralPath $stateFullPath -PathType Leaf) {
            Remove-FixtureState -Path $stateFullPath
        } else {
            if ($rootTrusted -and $root) { Remove-CertificateThumbprint -StoreName Root -Thumbprint $root.Thumbprint }
            if ($leaf) { Remove-CertificateThumbprint -StoreName My -Thumbprint $leaf.Thumbprint }
            if ($root) { Remove-CertificateThumbprint -StoreName My -Thumbprint $root.Thumbprint }
            if (Test-Path -LiteralPath $outputFullPath -PathType Leaf) { Remove-Item -LiteralPath $outputFullPath -Force }
            if (Test-Path -LiteralPath $thumbprintPath -PathType Leaf) { Remove-Item -LiteralPath $thumbprintPath -Force }
        }
    } catch { }
    Write-Error $_.Exception.Message
    exit 1
}
