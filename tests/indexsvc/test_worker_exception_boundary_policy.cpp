#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

#include "../TestSupport.h"
#include "ScanAttemptBarrier.h"

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
    const std::string scanBoundary = ReadText(root / "src" / "indexsvc" / "src" / "ScanWorkerBoundary.h");
    const std::string workerLifecycle = ReadText(root / "src" / "indexsvc" / "src" / "ServiceWorkerLifecycle.h");
    const std::string scanner = ReadText(root / "src" / "indexsvc" / "src" / "VolumeScanner.cpp");

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

    fftest::Check(source.find("RunScanWorkerBoundary(") != std::string::npos,
                  "the real service scan worker delegates to the tested production boundary");
    fftest::Check(scanBoundary.find("transportFailed.store(true)") != std::string::npos,
                  "scan worker exceptions poison the connection generation");
    fftest::Check(scanBoundary.find("CancelIoEx(pipeHandle, nullptr)") != std::string::npos,
                  "scan worker failures cancel outstanding service pipe I/O");
    const std::regex standardWorkerCatch(R"(catch\s*\(\s*const\s+std::exception\s*&\s*\))");
    const std::regex unknownWorkerCatchWithFatalStore(
        R"(catch\s*\(\s*\.\.\.\s*\)\s*\{\s*transportFailed\.store\(true\)\s*;)");
    fftest::Check(std::regex_search(scanBoundary, standardWorkerCatch) &&
                      std::regex_search(scanBoundary, unknownWorkerCatchWithFatalStore),
                  "the shared scan boundary contains standard and unknown worker exceptions");

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
    const std::size_t startVolumeScan = source.find("case MessageType::StartVolumeScan");
    const std::size_t scanPush = source.find("activeScans.push_back", startVolumeScan);
    const std::size_t scanThread = source.find("std::thread", startVolumeScan);
    fftest::Check(scanPush != std::string::npos && scanThread != std::string::npos && scanPush < scanThread,
                  "scan worker is registered before its thread starts");

    // scan-generation-integrity-and-residual-certification-closure 3.4:
    // an old scan is joined before the service writes the ordering barrier,
    // and the replacement is not registered or started until after that
    // serialized reply. That is what makes VolumeId-only output on either
    // side of the marker attributable to the old or replacement worker.
    const std::size_t stopAndJoin = source.find("StopAndJoin(activeScans, request.volumeId.value)", startVolumeScan);
    const std::size_t barrierCapability = source.find("ProtocolSupportsScanAttemptStarted(negotiatedVersion)", stopAndJoin);
    const std::size_t barrierSend = source.find("SendScanAttemptStarted(", stopAndJoin);
    const std::size_t barrierHelper = source.find("bool SendScanAttemptStarted(");
    const std::size_t barrierLock = source.find("std::lock_guard<std::mutex> lock(writeMutex)", barrierHelper);
    const std::size_t barrierWrite = source.find("WriteFrameWithDeadline", barrierLock);
    fftest::Check(stopAndJoin != std::string::npos && barrierCapability != std::string::npos &&
                      barrierSend != std::string::npos && scanPush != std::string::npos &&
                      stopAndJoin < barrierCapability && barrierCapability <= barrierSend && barrierSend < scanPush,
                  "scan-attempt barrier is negotiated and emitted after join before replacement registration");
    fftest::Check(barrierHelper != std::string::npos && barrierLock != std::string::npos &&
                      barrierWrite != std::string::npos && barrierHelper < barrierLock && barrierLock < barrierWrite,
                  "scan-attempt barrier holds the shared service write mutex while framing its reply");
    const std::size_t journalPush = source.find("activeJournals.push_back");
    const std::size_t journalThread = source.find("std::thread", source.find("case MessageType::OpenUsnJournal"));
    fftest::Check(journalPush != std::string::npos && journalThread != std::string::npos && journalPush < journalThread,
                  "journal worker is registered before its thread starts");

    // Keep-true guards: existing ordered teardown call sites and joinable
    // checks must remain (regression against a careless boundary refactor).
    fftest::Check(source.find("StopAndJoinAll(activeJournals)") != std::string::npos,
                  "journal workers are drained on teardown");
    const std::regex selectedWorkerJoinGuard(
        R"(if\s*\(\s*it->thread\.joinable\(\)\s*\)\s*\{\s*it->thread\.join\(\);)");
    const std::regex allWorkersJoinGuard(
        R"(if\s*\(\s*worker\.thread\.joinable\(\)\s*\)\s*\{\s*worker\.thread\.join\(\);)");
    fftest::Check(std::regex_search(workerLifecycle, selectedWorkerJoinGuard) &&
                      std::regex_search(workerLifecycle, allWorkersJoinGuard),
                  "worker owner keeps both selected and all-worker joins joinable-guarded");
    fftest::Check(source.find("registry.TeardownConnection(connectionId)") != std::string::npos,
                  "connection-scoped handle registry is torn down");

    const std::size_t cancelSyncIo = workerLifecycle.find("CancelSynchronousIo(worker.thread.native_handle())");
    const std::size_t stopJoin = workerLifecycle.find("it->thread.join()", cancelSyncIo);
    fftest::Check(cancelSyncIo != std::string::npos && stopJoin != std::string::npos && cancelSyncIo < stopJoin,
                  "the shared service worker owner requests synchronous-I/O cancellation before joining");
    fftest::Check(workerLifecycle.find("only requests cancellation") != std::string::npos &&
                      workerLifecycle.find("does not provide a") != std::string::npos &&
                      workerLifecycle.find("completion deadline") != std::string::npos &&
                      workerLifecycle.find("joining still waits for the kernel request to return") != std::string::npos,
                  "worker teardown documents that kernel-I/O cancellation does not guarantee a deadline");

    const std::regex volumeGuardPattern(R"(ScopedVolumeHandle\s+volumeHandle\s*\(\s*CreateFileW\s*\()");
    fftest::Check(std::regex_search(scanner, volumeGuardPattern),
                  "production RunVolumeScan owns the raw volume handle with RAII");
    size_t explicitVolumeReleases = 0;
    size_t releaseSearch = 0;
    while ((releaseSearch = scanner.find("volumeHandle.reset();", releaseSearch)) != std::string::npos) {
        ++explicitVolumeReleases;
        releaseSearch += sizeof("volumeHandle.reset();") - 1;
    }
    fftest::Check(explicitVolumeReleases >= 3 && scanner.find("CloseHandle(volumeHandle)") == std::string::npos,
                  "intentional geometry-failure and enumeration cleanup remain explicit under the guard");
}

// retry-ownership-and-focus-escape-closure 4.3: execute
// both barrier outcomes. A negotiated peer whose bounded barrier write fails
// must tear the connection down; a peer that did not negotiate the barrier
// never has the write attempted. The production loop must act on TearDown
// before it registers or starts the replacement worker, so deleting that
// branch cannot pass the source binding.
void TestScanAttemptBarrierDecisionIsExecutable() {
    using ffindexsvc::ApplyScanAttemptBarrier;
    using ffindexsvc::ScanAttemptBarrierDecision;

    bool writeInvoked = false;
    const auto notNegotiated = ApplyScanAttemptBarrier(false, [&] {
        writeInvoked = true;
        return true;
    });
    fftest::Check(notNegotiated == ScanAttemptBarrierDecision::Proceed,
                  "a pre-2.3 peer proceeds without a barrier write");
    fftest::Check(!writeInvoked, "the barrier writer is never invoked for a pre-2.3 peer");

    const auto written = ApplyScanAttemptBarrier(true, [] { return true; });
    fftest::Check(written == ScanAttemptBarrierDecision::Proceed,
                  "a successful negotiated barrier write proceeds to the replacement worker");

    const auto failed = ApplyScanAttemptBarrier(true, [] { return false; });
    fftest::Check(failed == ScanAttemptBarrierDecision::TearDown,
                  "a failed negotiated barrier write tears the connection down instead of starting a worker");

    const auto root = FindRepositoryRoot();
    fftest::Check(!root.empty(), "the barrier-order binding resolves the repository root");
    if (!root.empty()) {
        const std::string source = ReadText(root / "src" / "indexsvc" / "src" / "ServiceConnection.cpp");
        const std::size_t gate = source.find("ApplyScanAttemptBarrier(");
        const std::size_t tearDown = source.find("ScanAttemptBarrierDecision::TearDown", gate);
        const std::size_t disconnect = source.find("disconnect = true;", tearDown);
        const std::size_t breakStatement = source.find("break;", disconnect);
        const std::size_t workerRegistration = source.find("activeScans.push_back", gate);
        fftest::Check(gate != std::string::npos && tearDown != std::string::npos &&
                          disconnect != std::string::npos && breakStatement != std::string::npos &&
                          workerRegistration != std::string::npos && tearDown < disconnect &&
                          disconnect < breakStatement && breakStatement < workerRegistration,
                      "TearDown disconnects before the replacement worker is registered");
    }
}

} // namespace

int main() {
    TestWorkerExceptionBoundaryPolicy();
    TestScanAttemptBarrierDecisionIsExecutable();
    if (fftest::FailureCount() > 0) {
        std::fprintf(stderr, "%d test(s) failed\n", fftest::FailureCount());
        return 1;
    }
    std::printf("All worker exception boundary policy tests passed.\n");
    return 0;
}
