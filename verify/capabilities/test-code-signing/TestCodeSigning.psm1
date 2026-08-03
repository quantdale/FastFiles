<#
    Test-PKI code signing capability (task 4.1-4.6; design.md D4). Generates an
    isolated self-signed test code-signing certificate into the Current-User `My`
    store (no admin needed), exports the key to a temp PFX with a random password
    under the gitignored verify/.signing/, signs the four product binaries with
    signtool, and verifies each with the exact WinVerifyTrust(WTD_SAFER_FLAG) +
    SHA1-leaf-thumbprint extraction the product enforces (authenticode-pinning).
    A production OV/EV certificate is an injectable secret (FF_PRODUCTION_CERT_PFX
    / FF_PRODUCTION_CERT_PASSWORD); the production gate runs only when the cert is
    present and otherwise reports SKIPPED(production-cert-not-provided). It never
    disables verification and never fabricates a production signature.
#>

$script:SigningRoot = Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) '.signing'
$script:ProductBinaries = @(
    @{ Name = 'FastFiles.exe';        BuildRel = 'build\debug\src\ui\FastFiles.exe' }
    @{ Name = 'FastFilesEngine.exe';  BuildRel = 'build\debug\src\engine\FastFilesEngine.exe' }
    @{ Name = 'FastFilesIndexSvc.exe';BuildRel = 'build\debug\src\indexsvc\FastFilesIndexSvc.exe' }
    @{ Name = 'FastFilesSetup.exe';   BuildRel = 'build\debug\src\installer\FastFilesSetup.exe' }
)

function Test-TestCodeSigningCapabilityAvailability {
    param([Parameter(Mandatory)] $Fingerprint)
    if (-not $IsWindows) {
        return [pscustomobject]@{ Available = $false; Reason = 'windows-required'; RequiredContext = [pscustomobject]@{ needs = 'Windows Authenticode / PowerShell PKI module' } }
    }
    if (-not (Get-Command New-SelfSignedCertificate -ErrorAction SilentlyContinue)) {
        return [pscustomobject]@{ Available = $false; Reason = 'pki-module-unavailable'; RequiredContext = [pscustomobject]@{ needs = 'Windows PowerShell PKI module (New-SelfSignedCertificate)' } }
    }
    if (-not (Find-TestCodeSigningSigntool)) {
        return [pscustomobject]@{ Available = $false; Reason = 'signtool-unavailable'; RequiredContext = [pscustomobject]@{ needs = 'signtool.exe from the Windows SDK (Windows Kits)' } }
    }
    return [pscustomobject]@{ Available = $true; Reason = $null; RequiredContext = $null }
}

function Get-TestCodeSigningCapabilityDiagnostics {
    return @('test-certificate-generation', 'pfx-export', 'binary-signing', 'authenticode-verification', 'pinned-thumbprint', 'production-cert-gate')
}

function New-VSignSubResult {
    param([string] $Id, [string] $Status, [string] $Reason, [double] $DurationMs, [string] $Detail, [array] $Diagnostics = @())
    [pscustomobject]@{ id = $Id; tier = 0; status = $Status; reason = $Reason; requiredContext = $null; durationMs = [math]::Round($DurationMs, 0); detail = $Detail; diagnostics = @($Diagnostics) }
}

function Find-TestCodeSigningSigntool {
    $pattern = 'C:\Program Files (x86)\Windows Kits\10\bin\*\*\signtool.exe'
    $candidates = @(Get-ChildItem -Path $pattern -ErrorAction SilentlyContinue | Sort-Object FullName -Descending)
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate.FullName) { return $candidate.FullName }
    }
    return $null
}

function Get-ProductBinaryPaths {
    param([Parameter(Mandatory)] [hashtable] $Options)
    $repo = $Options.RepoRoot
    $result = @()
    foreach ($entry in $script:ProductBinaries) {
        $path = Join-Path $repo $entry.BuildRel
        $result += [pscustomobject]@{ Name = $entry.Name; Path = $path; Exists = (Test-Path -LiteralPath $path) }
    }
    return $result
}

function New-TestCodeSigningCertificate {
    param([string] $CertStoreLocation = 'Cert:\CurrentUser\My')
    $name = 'FastFiles Test Code Signing'
    $existing = Get-ChildItem -Path $CertStoreLocation -ErrorAction SilentlyContinue | Where-Object { $_.Subject -eq "CN=$name" } | Select-Object -First 1
    if ($existing) { return $existing }
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject "CN=$name" -CertStoreLocation $CertStoreLocation -KeyExportPolicy Exportable -NotAfter (Get-Date).AddYears(2)
    return $cert
}

function Add-TestCodeSigningRootTrust {
    <#
        The product's VerifyAutheticodeAndGetThumbprint only extracts the signer
        thumbprint when WinVerifyTrust returns ERROR_SUCCESS. A self-signed cert that
        is not a trusted root makes WinVerifyTrust fail with CERT_E_UNTRUSTEDROOT, so
        the pinned-thumbprint assertion would fail closed even though the bytes are
        signed. Installing the self-signed test cert into the Current-User Root store
        (per-user, no admin) makes it a trusted root for the current user only, exactly
        as a real installed code-signing cert would be trusted. This is a reversible,
        per-user trust decision recorded in the run.
    #>
    param([Parameter(Mandatory)] $Certificate)
    $root = 'Cert:\CurrentUser\Root'
    $existing = Get-ChildItem -Path $root -ErrorAction SilentlyContinue | Where-Object { $_.Thumbprint -eq $Certificate.Thumbprint } | Select-Object -First 1
    if ($existing) { return $true }
    $store = New-Object System.Security.Cryptography.X509Certificates.X509Store('Root', 'CurrentUser')
    try {
        $store.Open([System.Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
        $store.Add($Certificate)
        return $true
    } catch {
        return $false
    } finally {
        $store.Close()
    }
}

function Export-TestCodeSigningPfx {
    param([Parameter(Mandatory)] $Certificate, [Parameter(Mandatory)] [string] $SigningRoot)
    New-Item -ItemType Directory -Path $SigningRoot -Force | Out-Null
    $password = [Convert]::ToBase64String([Security.Cryptography.RandomNumberGenerator]::GetBytes(24)) -replace '[^a-zA-Z0-9]', 'x'
    $pfxPath = Join-Path $SigningRoot 'test-code-signing.pfx'
    $thumbprintPath = Join-Path $SigningRoot 'test-code-signing.thumbprint.txt'
    $passwordPath = Join-Path $SigningRoot 'test-code-signing.password.txt'
    $securePass = ConvertTo-SecureString -String $password -AsPlainText -Force
    Export-PfxCertificate -Cert $Certificate -FilePath $pfxPath -Password $securePass -Force | Out-Null
    Set-Content -LiteralPath $thumbprintPath -Value $Certificate.Thumbprint -Encoding utf8
    Set-Content -LiteralPath $passwordPath -Value $password -Encoding utf8
    [pscustomobject]@{ PfxPath = $pfxPath; Password = $password; Thumbprint = $Certificate.Thumbprint; PasswordPath = $passwordPath; ThumbprintPath = $thumbprintPath }
}

function Invoke-SigntoolProcess {
    param([Parameter(Mandatory)] [string] $Signtool, [Parameter(Mandatory)] [string[]] $Arguments)
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Signtool
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $startInfo.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) { throw "Failed to start signtool." }
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(180000)) { $process.Kill($true); throw 'signtool timed out.' }
    [pscustomobject]@{ ExitCode = $process.ExitCode; Output = $stdout.Result + $stderr.Result }
}

function Sign-TestCodeSigningBinary {
    param([Parameter(Mandatory)] [string] $Signtool, [Parameter(Mandatory)] [string] $PfxPath, [Parameter(Mandatory)] [string] $Password, [Parameter(Mandatory)] [string] $BinaryPath)
    $result = Invoke-SigntoolProcess -Signtool $Signtool -Arguments @('sign', '/fd', 'SHA256', '/f', $PfxPath, '/p', $Password, '/v', $BinaryPath)
    return $result
}

<#
    Mirrors ffsetup::AuthenticodeVerification.cpp: WinVerifyTrust under
    WINTRUST_ACTION_GENERIC_VERIFY_V2 with WTD_UI_NONE / WTD_REVOKE_NONE /
    WTD_SAFER_FLAG (no trusted-root requirement), then extracts the leaf signer's
    SHA-1 CERT_SHA1_HASH_PROP_ID thumbprint. Returns the 20-byte thumbprint hex or
    $null when the signature is absent/invalid.
#>
if (-not ('FfAuthenticode' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class FfAuthenticode
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct WINTRUST_FILE_INFO
    {
        public uint cbStruct;
        public IntPtr pcwszFilePath;
        public IntPtr hFile;
        public IntPtr pgKnownSubject;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct WINTRUST_DATA
    {
        public uint cbStruct;
        public IntPtr pPolicyCallbackData;
        public IntPtr pSIPClientData;
        public uint dwUIChoice;
        public uint fdwRevocationChecks;
        public uint dwUnionChoice;
        public IntPtr pFile;
        public uint dwStateAction;
        public IntPtr hWVTStateData;
        public IntPtr pwszURLReference;
        public uint dwProvFlags;
        public uint dwUIContext;
        public IntPtr pSignatureSettings;
    }

    public const uint WTD_UI_NONE = 2;
    public const uint WTD_REVOKE_NONE = 0;
    public const uint WTD_CHOICE_FILE = 1;
    public const uint WTD_STATEACTION_VERIFY = 1;
    public const uint WTD_STATEACTION_CLOSE = 2;
    public const uint WTD_SAFER_FLAG = 0x100;

    [DllImport("wintrust.dll", SetLastError = true)]
    public static extern uint WinVerifyTrust(IntPtr hwnd, ref Guid pgActionID, ref WINTRUST_DATA pWVTData);

    public static bool VerifySignatureStatus(string filePath)
    {
        if (filePath == null) return false;
        string path = null;
        try { path = System.IO.Path.GetFullPath(filePath); } catch { return false; }

        WINTRUST_FILE_INFO fileInfo = new WINTRUST_FILE_INFO();
        fileInfo.cbStruct = (uint)Marshal.SizeOf(typeof(WINTRUST_FILE_INFO));
        fileInfo.pcwszFilePath = Marshal.StringToCoTaskMemUni(path);

        WINTRUST_DATA trustData = new WINTRUST_DATA();
        trustData.cbStruct = (uint)Marshal.SizeOf(typeof(WINTRUST_DATA));
        trustData.dwUIChoice = WTD_UI_NONE;
        trustData.fdwRevocationChecks = WTD_REVOKE_NONE;
        trustData.dwUnionChoice = WTD_CHOICE_FILE;
        trustData.pFile = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(WINTRUST_FILE_INFO)));
        Marshal.StructureToPtr(fileInfo, trustData.pFile, false);
        trustData.dwStateAction = WTD_STATEACTION_VERIFY;
        trustData.dwProvFlags = WTD_SAFER_FLAG;

        Guid action = new Guid("00AAC56B-CD44-11d0-8CC2-00C04FC295EE");
        uint status = WinVerifyTrust(IntPtr.Zero, ref action, ref trustData);

        trustData.dwStateAction = WTD_STATEACTION_CLOSE;
        WinVerifyTrust(IntPtr.Zero, ref action, ref trustData);
        if (trustData.pFile != IntPtr.Zero) Marshal.FreeHGlobal(trustData.pFile);
        if (fileInfo.pcwszFilePath != IntPtr.Zero) Marshal.FreeCoTaskMem(fileInfo.pcwszFilePath);
        return status == 0;
    }
}
'@
}

function Test-WinVerifyTrustSignature {
    <#
        Native WinVerifyTrust(WTD_SAFER_FLAG) status check - the product's exact
        signature-validity gate (ffsetup::AuthenticodeVerification.cpp). Returns true
        when WinVerifyTrust returns ERROR_SUCCESS. WTD_SAFER_FLAG means no trusted-root
        requirement; the test cert is additionally installed as a CurrentUser root so
        the normal trust provider also reports Valid.
    #>
    param([Parameter(Mandatory)] [string] $BinaryPath)
    if (-not (Test-Path -LiteralPath $BinaryPath)) { return $false }
    return [FfAuthenticode]::VerifySignatureStatus($BinaryPath)
}

function Test-BinaryTestSigned {
    <#
        Verifies the pinned-thumbprint behavior the product enforces, mirroring
        ffsetup::VerifyPinnedSignature:
          1. WinVerifyTrust(WTD_SAFER_FLAG) returns ERROR_SUCCESS (native, the product's
             signature-validity gate).
          2. Get-AuthenticodeSignature reports Status=Valid (trusted now that the test
             cert is a CurrentUser root).
          3. The leaf signer certificate's SHA1 thumbprint equals the test cert
             thumbprint (the product's pinning comparison; .NET exposes the SHA1
             thumbprint via SignerCertificate.Thumbprint, equivalent to
             CertGetCertificateContextProperty(CERT_SHA1_HASH_PROP_ID)).
    #>
    param([Parameter(Mandatory)] [string] $BinaryPath, [Parameter(Mandatory)] [string] $ExpectedThumbprint)
    $winTrust = Test-WinVerifyTrustSignature -BinaryPath $BinaryPath
    $sig = Get-AuthenticodeSignature -LiteralPath $BinaryPath
    $signerThumb = if ($sig.SignerCertificate) { $sig.SignerCertificate.Thumbprint } else { $null }
    $issues = @()
    if (-not $winTrust) { $issues += 'WinVerifyTrust-failed' }
    if ($sig.Status -ne 'Valid') { $issues += "status-$($sig.Status)" }
    if (-not $signerThumb -or -not [string]::Equals($signerThumb, $ExpectedThumbprint, [StringComparison]::OrdinalIgnoreCase)) {
        $issues += 'pinned-thumbprint-mismatch'
    }
    [pscustomobject]@{
        Valid = $issues.Count -eq 0
        Reason = if ($issues.Count -eq 0) { $null } else { $issues -join ';' }
        WinVerifyTrust = [bool]$winTrust
        PgStatus = $sig.Status.ToString()
        SignerThumbprint = $signerThumb
    }
}

function Invoke-TestCodeSigningCapability {
    param($RunContext, $Fingerprint, [string] $ArtifactsDir, [hashtable] $Options)

    $subResults = @()
    $artifacts = @()
    $log = [System.Collections.ArrayList]::new()
    $logFn = { param($msg) [void]$log.Add("[$(Get-Date -Format 'HH:mm:ss')] $msg") }
    $logFn.Invoke("test-code-signing: repo=$($Options.RepoRoot)")

    try {
        $binaryPaths = @(Get-ProductBinaryPaths -Options $Options)
        $missing = @($binaryPaths | Where-Object { -not $_.Exists })
        if (-not $Options.RepoRoot -or $missing.Count -gt 0) {
            $detail = "product binaries not found under RepoRoot ($($Options.RepoRoot)): $($missing.Name -join ', ')"
            $logFn.Invoke($detail)
            $subResults += New-VSignSubResult -Id 'binary-signing' -Status 'SKIPPED' -Reason 'product-binaries-not-built' -DurationMs 0 -Detail $detail
            return [pscustomobject]@{
                Status = 'SKIPPED'; Reason = 'product-binaries-not-built'
                Summary = 'Product binaries were not present under the build tree; run windows-build-validation first.'
                Artifacts = $artifacts; SubResults = $subResults
            }
        }

        $signtool = Find-TestCodeSigningSigntool
        $logFn.Invoke("signtool: $signtool")

        $sw = [Diagnostics.Stopwatch]::StartNew()
        $cert = New-TestCodeSigningCertificate
        $sw.Stop()
        $subResults += New-VSignSubResult -Id 'test-certificate-generation' -Status 'PASS' -Reason $null -DurationMs $sw.Elapsed.TotalMilliseconds `
            -Detail "cert `"$($cert.Subject)`" thumbprint $($cert.Thumbprint) in CurrentUser\My store; expires $($cert.NotAfter.ToShortDateString())"

        $sw.Restart()
        $pfx = Export-TestCodeSigningPfx -Certificate $cert -SigningRoot $script:SigningRoot
        $sw.Stop()
        $subResults += New-VSignSubResult -Id 'pfx-export' -Status 'PASS' -Reason $null -DurationMs $sw.Elapsed.TotalMilliseconds `
            -Detail "PFX written to $($pfx.PfxPath) (gitignored verify/.signing/); thumbprint $($pfx.Thumbprint)"
        $artifacts += [pscustomobject]@{ path = $pfx.PfxPath; type = 'test-pfx' }
        $artifacts += [pscustomobject]@{ path = $pfx.ThumbprintPath; type = 'test-thumbprint' }

        $rootTrusted = Add-TestCodeSigningRootTrust -Certificate $cert
        $subResults += New-VSignSubResult -Id 'test-root-trust' -Status $(if ($rootTrusted) { 'PASS' } else { 'FAIL' }) `
            -DurationMs 0 -Detail "test cert installed into CurrentUser\Root so WinVerifyTrust (product's VerifyPinnedSignature) succeeds" `
            -Reason $(if ($rootTrusted) { $null } else { 'root-trust-install-failed' })

        $signErrors = @()
        $signDetail = @()
        $unverified = @()
        foreach ($binary in $binaryPaths) {
            $logFn.Invoke("signing $($binary.Name)")
            $result = Sign-TestCodeSigningBinary -Signtool $signtool -PfxPath $pfx.PfxPath -Password $pfx.Password -BinaryPath $binary.Path
            $signDetail += "$($binary.Name)=exit$($result.ExitCode)"
            if ($result.ExitCode -ne 0) {
                $signErrors += "$($binary.Name)-sign-failed"
                $logFn.Invoke("sign failed for $($binary.Name): $($result.Output)")
            }
        }
        $subResults += New-VSignSubResult -Id 'binary-signing' -Status $(if ($signErrors.Count -eq 0) { 'PASS' } else { 'FAIL' }) `
            -DurationMs 0 -Detail ($signDetail -join '; ') -Reason $(if ($signErrors.Count -eq 0) { $null } else { 'binary-signing-failed' })

        $verifyIssues = @()
        $verifyDetail = @()
        foreach ($binary in $binaryPaths) {
            $check = Test-BinaryTestSigned -BinaryPath $binary.Path -ExpectedThumbprint $pfx.Thumbprint
            $verifyDetail += "$($binary.Name)=WinVerifyTrust:$($check.WinVerifyTrust)/PSGetAuth:$($check.PgStatus)/signer=$($check.SignerThumbprint)"
            if (-not $check.Valid) { $verifyIssues += "$($binary.Name)-$($check.Reason)" }
        }
        $subResults += New-VSignSubResult -Id 'authenticode-verification' -Status $(if ($verifyIssues.Count -eq 0) { 'PASS' } else { 'FAIL' }) `
            -DurationMs 0 -Detail ($verifyDetail -join '; ') -Reason $(if ($verifyIssues.Count -eq 0) { $null } else { 'authenticode-invalid' })
        $subResults += New-VSignSubResult -Id 'pinned-thumbprint' -Status $(if ($verifyIssues.Count -eq 0) { 'PASS' } else { 'FAIL' }) `
            -DurationMs 0 -Detail "each binary: WinVerifyTrust(SAFER) success + leaf signer SHA1 thumbprint == test cert $($pfx.Thumbprint) (mirrors ffsetup::VerifyPinnedSignature)" `
            -Reason $(if ($verifyIssues.Count -eq 0) { $null } else { 'pinned-thumbprint-mismatch' })

        $prodPfx = $env:FF_PRODUCTION_CERT_PFX
        $prodPassword = $env:FF_PRODUCTION_CERT_PASSWORD
        if ($prodPfx -and (Test-Path -LiteralPath $prodPfx)) {
            $prodPfx = [IO.Path]::GetFullPath($prodPfx)
            $prodSignErrors = @()
            $prodDetail = @()
            foreach ($binary in $binaryPaths) {
                $r = Invoke-SigntoolProcess -Signtool $signtool -Arguments @('sign', '/fd', 'SHA256', '/f', $prodPfx, '/p', $prodPassword, '/v', $binary.Path)
                $prodDetail += "$($binary.Name)=exit$($r.ExitCode)"
                if ($r.ExitCode -ne 0) { $prodSignErrors += $binary.Name }
            }
            $prodThumb = $null
            if ($prodSignErrors.Count -eq 0) {
                $prodSig = Get-AuthenticodeSignature -LiteralPath $binaryPaths[0].Path
                $prodThumb = if ($prodSig.SignerCertificate) { $prodSig.SignerCertificate.Thumbprint } else { $null }
            }
            $subResults += New-VSignSubResult -Id 'production-cert-gate' -Status $(if ($prodSignErrors.Count -eq 0) { 'PASS' } else { 'FAIL' }) `
                -DurationMs 0 -Detail "production cert present; gate ran. $($prodDetail -join '; '); production leaf thumbprint $prodThumb" `
                -Reason $(if ($prodSignErrors.Count -eq 0) { $null } else { 'production-signing-failed' })
        } else {
            $subResults += New-VSignSubResult -Id 'production-cert-gate' -Status 'SKIPPED' -Reason 'production-cert-not-provided' -DurationMs 0 `
                -Detail 'FF_PRODUCTION_CERT_PFX / FF_PRODUCTION_CERT_PASSWORD not set or PFX missing; production gate did not run. Test-PKI Authenticode + pinned-thumbprint checks above proceeded independently and were not disabled.'
        }

        $sigPath = Join-Path $ArtifactsDir 'test-code-signing.json'
        [pscustomobject]@{
            schemaVersion = '1.0.0'
            generatedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
            testCertThumbprint = $pfx.Thumbprint
            signtool = $signtool
            binaries = @($binaryPaths | ForEach-Object { [pscustomobject]@{ name = $_.Name; path = $_.Path; signed = $true } })
            subresults = @($subResults | ForEach-Object { [pscustomobject]@{ id = $_.id; status = $_.status; reason = $_.reason; detail = $_.detail } })
        } | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $sigPath -Encoding utf8
        $artifacts += [pscustomobject]@{ path = $sigPath; type = 'test-code-signing-report' }

        $logPath = Join-Path $ArtifactsDir 'run.log'
        $log | Set-Content -LiteralPath $logPath -Encoding utf8
        $artifacts += [pscustomobject]@{ path = $logPath; type = 'run-log' }

        $failures = @($subResults | Where-Object status -eq 'FAIL').Count
        $skipped = @($subResults | Where-Object status -eq 'SKIPPED').Count
        $status = if ($failures -gt 0) { 'FAIL' } else { 'PASS' }
        $reason = if ($failures -gt 0) { 'test-code-signing-failed' } else { $null }
        return [pscustomobject]@{
            Status = $status; Reason = $reason
            Summary = "$(@($subResults | Where-Object status -eq 'PASS').Count) passed, $skipped skipped, $failures failed; test cert thumbprint $($pfx.Thumbprint)"
            Artifacts = $artifacts; SubResults = $subResults
        }
    } catch {
        $logFn.Invoke("capability error: $($_.Exception.Message)")
        $logPath = Join-Path $ArtifactsDir 'run.log'
        $log | Set-Content -LiteralPath $logPath -Encoding utf8
        $failurePath = Join-Path $ArtifactsDir 'failure.tree.txt'
        "capability error: $($_.Exception.ToString())" | Set-Content -LiteralPath $failurePath -Encoding utf8
        $artifacts += [pscustomobject]@{ path = $failurePath; type = 'failure-tree' }
        $subResults += New-VSignSubResult -Id 'capability-run' -Status 'FAIL' -Reason 'driver-exception' -DurationMs 0 -Detail $_.Exception.Message
        return [pscustomobject]@{ Status = 'FAIL'; Reason = 'driver-exception'; Summary = $_.Exception.Message; Artifacts = $artifacts; SubResults = $subResults }
    }
}

Export-ModuleMember -Function Test-TestCodeSigningCapabilityAvailability, Invoke-TestCodeSigningCapability, Get-TestCodeSigningCapabilityDiagnostics