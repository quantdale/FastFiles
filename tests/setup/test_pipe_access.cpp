#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <exception>
#include <optional>
#include <string>
#include <vector>

#include "ffsetup/SecurityDescriptors.h"
#include "../TestSupport.h"

namespace {

class Handle {
public:
    explicit Handle(HANDLE value = nullptr) : value_(value) {}
    ~Handle() { if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE Get() const { return value_; }
private:
    HANDLE value_;
};

constexpr DWORD kClientAccess = (FILE_GENERIC_READ | FILE_GENERIC_WRITE) & ~FILE_CREATE_PIPE_INSTANCE;

std::optional<bool> Allowed(PSECURITY_DESCRIPTOR descriptor, HANDLE token, DWORD desired) {
    GENERIC_MAPPING mapping{FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
    PRIVILEGE_SET privileges{};
    DWORD privilegeBytes = sizeof(privileges), granted = 0;
    BOOL allowed = FALSE;
    if (!AccessCheck(descriptor, token, desired, &mapping, &privileges, &privilegeBytes, &granted, &allowed)) {
        std::printf("AccessCheck error=%lu desired=0x%lx\n", GetLastError(), desired);
        return std::nullopt;
    }
    return allowed != FALSE;
}

void TestServiceClientCannotCreateServerInstance() {
    using fftest::Check;
    HANDLE rawProcessToken = nullptr;
    Check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &rawProcessToken) != FALSE,
          "pipe access: current token opens without elevation");
    Handle processToken(rawProcessToken);
    if (!rawProcessToken) return;
    DWORD needed = 0;
    GetTokenInformation(rawProcessToken, TokenUser, nullptr, 0, &needed);
    std::vector<uint8_t> userBytes(needed);
    const bool userKnown = needed != 0 && GetTokenInformation(rawProcessToken, TokenUser, userBytes.data(), needed, &needed);
    Check(userKnown, "pipe access: current-user SID is available");
    if (!userKnown) return;
    PSID user = reinterpret_cast<TOKEN_USER*>(userBytes.data())->User.Sid;
    const auto admins = ffsetup::GetBuiltinAdministratorsSid();
    const auto system = ffsetup::GetLocalSystemSid();
    Check(admins.has_value() && system.has_value(), "pipe access: broker principal SIDs resolve");
    if (!admins || !system) return;

    // Remove the admin allow-ACE from the fixture's effective token even on
    // hosted runners with an elevated default token. The restricting SID also
    // forces the access check through the production client ACE. No account,
    // group, trust store, privilege, or installed service is changed.
    SID_AND_ATTRIBUTES disabled{admins->Get(), 0};
    SID_AND_ATTRIBUTES restricting{user, 0};
    HANDLE rawRestricted = nullptr;
    Check(CreateRestrictedToken(rawProcessToken, DISABLE_MAX_PRIVILEGE, 1, &disabled, 0, nullptr,
                                1, &restricting, &rawRestricted) != FALSE,
          "pipe access: a restricted client token is created");
    Handle restricted(rawRestricted);
    if (!rawRestricted) return;
    HANDLE rawImpersonation = nullptr;
    Check(DuplicateToken(rawRestricted, SecurityImpersonation, &rawImpersonation) != FALSE,
          "pipe access: restricted impersonation token is created");
    Handle token(rawImpersonation);
    if (!rawImpersonation) return;

    auto descriptor = ffsetup::BuildPipeSecurityDescriptor(user);
    Check(descriptor.has_value(), "pipe access: production service descriptor accepts the client SID fixture");
    if (!descriptor) return;
    // AccessCheck requires owner/group even though this assertion concerns
    // only DACL grants. The kernel supplies these on real object creation.
    auto accessDescriptor = ffsetup::BuildPipeSecurityDescriptor(user);
    if (!accessDescriptor) { Check(false, "pipe access: access-check descriptor exists"); return; }
    const bool complete = SetSecurityDescriptorOwner(accessDescriptor->attributes.lpSecurityDescriptor, system->Get(), FALSE)
        && SetSecurityDescriptorGroup(accessDescriptor->attributes.lpSecurityDescriptor, system->Get(), FALSE);
    Check(complete, "pipe access: standalone AccessCheck descriptor is complete");
    if (!complete) return;
    const auto clientAllowed = Allowed(accessDescriptor->attributes.lpSecurityDescriptor, rawImpersonation, kClientAccess);
    const auto instanceAllowed = Allowed(accessDescriptor->attributes.lpSecurityDescriptor, rawImpersonation, FILE_CREATE_PIPE_INSTANCE);
    Check(clientAllowed && *clientAllowed, "pipe access: effective client token can exchange framed data");
    Check(instanceAllowed && !*instanceAllowed, "pipe access: effective client token cannot create a broker server instance");
    const auto changeAllowed = Allowed(accessDescriptor->attributes.lpSecurityDescriptor, rawImpersonation, WRITE_DAC);
    Check(changeAllowed && !*changeAllowed, "pipe access: effective client token cannot rewrite broker security");

    // Use a disposable pipe, never the installed service endpoint. This
    // verifies the kernel's actual instance-creation access check, not merely
    // that an SDDL string lacks a particular letter pair.
    const std::wstring name = L"\\\\.\\pipe\\FastFiles.PipeAccessTest." + std::to_wstring(GetCurrentProcessId())
        + L"." + std::to_wstring(GetTickCount64());
    Handle server(CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                  PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                  2, 4096, 4096, 0, &descriptor->attributes));
    Check(server.Get() != INVALID_HANDLE_VALUE, "pipe access: disposable first server is created");
    if (server.Get() == INVALID_HANDLE_VALUE) return;
    const bool impersonating = ImpersonateLoggedOnUser(rawImpersonation) != FALSE;
    Check(impersonating, "pipe access: restricted client is impersonated for kernel access checks");
    if (!impersonating) return;
    const HANDLE additional = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX,
                                               PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                               2, 4096, 4096, 0, nullptr);
    const DWORD serverError = GetLastError();
    // On a regression, retire the unexpected server before opening a client
    // so the red control cannot accidentally connect to its own impostor.
    if (additional != INVALID_HANDLE_VALUE) CloseHandle(additional);
    const HANDLE rawClient = CreateFileW(name.c_str(), kClientAccess, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    const DWORD clientError = GetLastError();
    const bool reverted = RevertToSelf() != FALSE;
    Handle client(rawClient);
    Check(reverted, "pipe access: fixture reverts impersonation");
    if (!reverted) std::terminate(); // never run remaining tests under another token
    std::printf("kernel additional-server=%s error=%lu; client=%s error=%lu\n",
                additional == INVALID_HANDLE_VALUE ? "denied" : "ALLOWED", serverError,
                rawClient == INVALID_HANDLE_VALUE ? "denied" : "allowed", clientError);
    Check(additional == INVALID_HANDLE_VALUE && serverError == ERROR_ACCESS_DENIED,
          "pipe access: kernel rejects an additional server from the authorized client token");
    Check(rawClient != INVALID_HANDLE_VALUE, "pipe access: kernel still permits the authorized data client");
    if (rawClient == INVALID_HANDLE_VALUE) return;
    const bool connected = ConnectNamedPipe(server.Get(), nullptr) != FALSE || GetLastError() == ERROR_PIPE_CONNECTED;
    Check(connected, "pipe access: the legitimate server accepts the data client");
    if (!connected) return;
    const uint8_t sent = 0x5a;
    uint8_t received = 0;
    DWORD count = 0;
    const bool wrote = WriteFile(rawClient, &sent, sizeof(sent), &count, nullptr) != FALSE && count == sizeof(sent);
    Check(wrote, "pipe access: restricted client writes data with specific access rights");
    if (!wrote) return;
    Check(ReadFile(server.Get(), &received, sizeof(received), &count, nullptr) != FALSE
              && count == sizeof(received) && received == sent,
          "pipe access: legitimate server receives exactly the client's data");
    const bool replied = WriteFile(server.Get(), &sent, sizeof(sent), &count, nullptr) != FALSE && count == sizeof(sent);
    Check(replied, "pipe access: legitimate server writes a reply");
    if (!replied) return;
    received = 0;
    Check(ReadFile(rawClient, &received, sizeof(received), &count, nullptr) != FALSE
              && count == sizeof(received) && received == sent,
          "pipe access: restricted client reads the exact reply");
}

} // namespace

int main() {
    TestServiceClientCannotCreateServerInstance();
    return fftest::FailureCount() == 0 ? 0 : 1;
}
