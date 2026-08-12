# FastFiles clean-host validation kit

This directory is a portable validation package for one exact Fast Files Release
candidate. It is intentionally not a production package and does not contain a
certificate, private key, password, or signing token.

The target host must be a supported Windows 10 1809-or-newer or Windows 11 x64
machine with no prior FastFiles service, process, scheduled task, `FastFilesUsers`
group, installation directory, FastFiles ProgramData/user state, or uninstall
registry entry. A VM is valid when it satisfies those conditions. A source clone,
Visual Studio, CMake, and build tools are not required by this kit. The kit does
not modify the trust store.

Run these commands from an Administrator PowerShell when the signed release is
available. The preflight also rejects unsigned artifacts, even on an otherwise
clean host:

```powershell
pwsh .\validate-clean-host.ps1
pwsh .\privileged-release.ps1
pwsh .\cleanup-clean-host.ps1 -RunUninstall
```

`validate-clean-host.ps1` is read-only and returns
`CLEAN_HOST_PRECONDITION_FAILED` without cleaning a contaminated host. The
privileged driver records only resources it created and verifies exact executable
paths before stopping any process. Evidence is written to the requested output
directory and can be validated/imported with `import-clean-host-result.ps1`.
