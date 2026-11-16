# Test-fixture capture only. A launch path can be an execution alias and is not
# evidence of the running image. Production identity/ownership checks stay in
# CapabilityRunner.psm1; fixtures must supply the same actual image and start.
# Process.Path is module-derived and can report ntdll.dll during loader startup.
# Query the executable from the process handle instead, without trusting launch
# spelling or broadening the production verifier's ownership predicate.
if (-not ('FastFiles.Tests.FixtureProcessImage' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;
namespace FastFiles.Tests {
    public static class FixtureProcessImage {
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool QueryFullProcessImageNameW(
            SafeProcessHandle process, uint flags, StringBuilder name, ref uint size);

        public static string Read(SafeProcessHandle process) {
            var name = new StringBuilder(32768);
            uint size = (uint)name.Capacity;
            if (!QueryFullProcessImageNameW(process, 0, name, ref size)) {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            return name.ToString();
        }
    }
}
'@
}

function Wait-ProcessFixtureIdentity {
    param(
        [Parameter(Mandatory)] [System.Diagnostics.Process] $Process,
        [ValidateRange(1, 10000)] [int] $TimeoutMilliseconds = 5000
    )

    $expectedStart = $Process.StartTime.ToUniversalTime()
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    do {
        if ($Process.HasExited) { throw 'Fixture process exited before its image identity was captured.' }
        $observed = $null
        try {
            $observed = Get-Process -Id $Process.Id -ErrorAction Stop
            $path = [FastFiles.Tests.FixtureProcessImage]::Read($observed.SafeHandle)
            $start = $observed.StartTime.ToUniversalTime()
            if (-not [string]::IsNullOrWhiteSpace($path) -and $start -eq $expectedStart) {
                $path = [IO.Path]::GetFullPath($path)
                $startText = $start.ToString('o')
                return [pscustomobject]@{
                    pid = [int]$Process.Id
                    path = $path
                    startTimeUtc = $startText
                    identityKey = "$($Process.Id)|$($path.ToLowerInvariant())|$startText"
                }
            }
        } catch {
            # A newly launched process can briefly have no readable identity.
            # Retry only within the bound; never substitute its launch path.
        } finally {
            if ($null -ne $observed) { $observed.Dispose() }
        }
        Start-Sleep -Milliseconds 25
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Fixture process actual image/start identity was unavailable within the capture bound.'
}

Export-ModuleMember -Function Wait-ProcessFixtureIdentity
