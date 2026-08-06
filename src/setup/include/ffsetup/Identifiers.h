#pragma once

// Names and paths shared between FastFilesIndexSvc, FastFilesEngine, and the
// installer (FastFilesSetup). Centralized here so the three targets and the
// installer can never disagree on a pipe/service/task name (design.md D3-D6).

namespace ffsetup {

// SCM service name for the privileged service.
constexpr wchar_t kServiceName[] = L"FastFilesIndexSvc";
constexpr wchar_t kServiceDisplayName[] = L"FastFiles Index Service";

// Virtual service account name retained for the non-production registration
// path (task 3.1). The production model runs FastFilesIndexSvc under
// LocalSystem as a constrained privileged broker (see InstallSteps.cpp and
// resolve-raw-volume-privilege-insufficiency). Windows resolves
// "NT SERVICE\<ServiceName>" to a per-service virtual account with no
// password to manage; LsaAddAccountRights grants it privileges directly.
constexpr wchar_t kServiceVirtualAccountName[] = L"NT SERVICE\\FastFilesIndexSvc";

// Local group added at install time; membership is the coarse knob for
// which unprivileged users may reach the privileged pipes (design.md D4,
// Risks: "Cross-user visibility").
constexpr wchar_t kAuthorizedClientGroupName[] = L"FastFilesUsers";

// Privileged IPC seam (FastFilesEngine <-> FastFilesIndexSvc). Single
// service instance, not per-session -- the service is a machine-wide
// singleton (design.md D2).
constexpr wchar_t kCtrlPipeName[] = L"\\\\.\\pipe\\FastFiles.Svc.Ctrl";

// Same-privilege control seam (FastFiles UI <-> FastFilesEngine). Per
// logon-session, since each session runs its own FastFilesEngine (task 4.8).
// %u is replaced with the numeric Terminal Services session ID.
constexpr wchar_t kUiCtrlPipeNameFormat[] = L"\\\\.\\pipe\\FastFiles.Ui.Ctrl.%u";

// Read-only snapshot section name (design.md D3). Session-local ("Local\")
// namespace, suffixed by session ID (task 4.8).
constexpr wchar_t kSnapshotSectionNameFormat[] = L"Local\\FastFiles.IndexSnapshot.%u";

// Per-user Scheduled Task that starts FastFilesEngine at logon (task 4.1).
constexpr wchar_t kEngineTaskFolder[] = L"\\FastFiles\\";
constexpr wchar_t kEngineTaskName[] = L"FastFilesEngine";

// Executable file names, used to validate a peer's image path against the
// ACL-locked install directory (design.md D4 "Symmetric Mutual
// Authentication").
constexpr wchar_t kEngineExeName[] = L"FastFilesEngine.exe";
constexpr wchar_t kIndexSvcExeName[] = L"FastFilesIndexSvc.exe";

} // namespace ffsetup
