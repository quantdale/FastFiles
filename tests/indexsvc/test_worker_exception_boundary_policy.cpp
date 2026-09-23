#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

#include "../TestSupport.h"

namespace {

std::filesystem::path FindRepositoryRoot() {
    std::filesystem::path current = std::filesystem::current_path();
    for (int depth = 0; depth < 8; ++depth) {
        if (std::filesystem::exists(current / "src" / "indexsvc" / "src" / "ServiceConnection.cpp")) {
            return current;
        }
        if (current == current.root_path()) break;
        current = current.parent_path();
    }
    return {};
}

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

// hardening-campaign Phase 7 H2: lifecycle-transition policy for escaping
// exceptions. RED against the boundary-less implementation; GREEN once an
// exception can only produce diagnostic + ordered teardown + engine-side
// reconnect on a NEW transport -- never std::terminate on a LocalSystem
// service, never a resumed poisoned connection.
void TestWorkerExceptionBoundaryPolicy() {
    const auto root = FindRepositoryRoot();
    fftest::Check(!root.empty(), "exception boundary policy test resolves the repository root");
    if (root.empty()) return;

    const std::string source = ReadText(root / "src" / "indexsvc" / "src" / "ServiceConnection.cpp");

    const std::size_t stdException = source.find("catch (const std::exception");
    const std::size_t catchAll = source.find("catch (...)");
    fftest::Check(stdException != std::string::npos,
                  "service catches std::exception at its connection/worker boundaries");
    fftest::Check(catchAll != std::string::npos,
                  "service has a catch-all so non-standard throws cannot terminate LocalSystem");

    // Transition: exception -> diagnostics recorded, then ordered teardown.
    fftest::Check(source.find("WriteServiceLifecycleLog") != std::string::npos,
                  "an escaping exception is recorded in the admin-only service log");
    fftest::Check(source.find("AppendDiagnostic") != std::string::npos,
                  "an escaping exception emits a metadata-only diagnostic event");

    // Teardown must strictly FOLLOW the boundary: the catch wraps dispatch,
    // and StopAndJoinAll/TeardownConnection/CloseClientIdentity/CloseHandle
    // run unconditionally afterwards (handle-staleness invariant).
    const std::size_t lastCatch = source.rfind("catch");
    const std::size_t teardown = source.find("StopAndJoinAll(activeScans)");
    fftest::Check(lastCatch != std::string::npos && teardown != std::string::npos && teardown > lastCatch,
                  "ordered teardown runs after every catch, unconditionally");

    // Transition: worker spawn -> register-before-start, so an exception
    // between thread creation and registry insertion cannot orphan a writer
    // holding the pipe handle (joinable-guard survives via StopAndJoinAll).
    const std::size_t scanPush = source.find("activeScans.push_back");
    const std::size_t scanThread = source.find("std::thread", source.find("case MessageType::StartVolumeScan"));
    fftest::Check(scanPush != std::string::npos && scanThread != std::string::npos && scanPush < scanThread,
                  "scan worker is registered before its thread starts");
    const std::size_t journalPush = source.find("activeJournals.push_back");
    const std::size_t journalThread = source.find("std::thread", source.find("case MessageType::OpenUsnJournal"));
    fftest::Check(journalPush != std::string::npos && journalThread != std::string::npos && journalPush < journalThread,
                  "journal worker is registered before its thread starts");

    // Keep-true guards: existing ordered teardown call sites and joinable
    // checks must remain (regression against a careless boundary refactor).
    fftest::Check(source.find("StopAndJoinAll(activeJournals)") != std::string::npos,
                  "journal workers are drained on teardown");
    fftest::Check(source.find("joinable()") != std::string::npos,
                  "worker joins stay joinable-guarded (empty-slot safe)");
    fftest::Check(source.find("registry.TeardownConnection(connectionId)") != std::string::npos,
                  "connection-scoped handle registry is torn down");
}

} // namespace

int main() {
    TestWorkerExceptionBoundaryPolicy();
    if (fftest::FailureCount() > 0) {
        std::fprintf(stderr, "%d test(s) failed\n", fftest::FailureCount());
        return 1;
    }
    std::printf("All worker exception boundary policy tests passed.\n");
    return 0;
}
