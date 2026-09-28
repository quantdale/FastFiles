// legacy-protocol-correctness-and-raw-volume-cancellation-closure 2.2/2.3:
// deterministic mixed-version failed-scan reproducer.
//
// The campaign invariant is that a scan which did not complete must never be
// represented as successfully completed merely because the negotiated peer
// cannot understand ScanFailed. This test binds the two halves that make the
// suspicion a defect rather than a theory:
//
//   * the REAL service terminal decision -- RunVolumeScan (setup failure) and
//     RunSyntheticVolumeScanForTesting (mid-enumeration failure / genuine
//     success) driven through the production RunScanWorkerBoundary, with the
//     production writer and the real negotiated version; and
//   * the REAL engine frame dispatch -- PrivilegedConnection::
//     DispatchReceivedFrame -> VolumeSessionManager callbacks ->
//     IndexPipeline durable store, over an in-process named pipe pair.
//
// For pre-{2,2} negotiations the uncorrected service emits the legacy
// ScanComplete for a scan that did not complete; the engine then marks the
// volume indexed and finalizes an in-flight reconciliation pass over a partial
// seen-set (deleting live rows). Every failure case below asserts the
// invariant, so this test MUST fail against the uncorrected implementation and
// pass only once an incomplete scan can no longer masquerade as a successful
// one for any supported negotiation.
#include <atomic>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include <windows.h>

#include "ffipc/PipeFraming.h"
#include "ffprotocol/Commands.h"
#include "ffprotocol/Records.h"
#include "ffprotocol/Version.h"
#include "IndexPipeline.h"
#include "PrivilegedConnection.h"
#include "ScanWorkerBoundary.h"
#include "../TestSupport.h"
#include "VolumeScanner.h"

// The engine's production dispatch path and the session manager's callback
// wiring are internal state. Like test_volume_session_manager.cpp, this test
// translation unit opens the access gate only for its own compilation.
// PrivilegedConnection.h is deliberately included above so its public
// test-only dispatcher remains independent of this translation unit's
// private-access gate for the session manager.
#define private public
#include "VolumeSessionManager.h"
#undef private

using fftest::Check;

namespace {

constexpr ffprotocol::VolumeId kEphemeralVolume{7};
constexpr uint32_t kEphemeralKey = 7;
constexpr uint32_t kVolumeSerial = 0x5EED5EEDu;
constexpr wchar_t kFixtureEntryName[] = L"legacy-fixture.txt";

struct PipePair {
    HANDLE server = INVALID_HANDLE_VALUE; // service writes (overlapped: production shape)
    HANDLE client = INVALID_HANDLE_VALUE; // engine reads
    ~PipePair() {
        if (client != INVALID_HANDLE_VALUE) CloseHandle(client);
        if (server != INVALID_HANDLE_VALUE) CloseHandle(server);
    }
};

std::unique_ptr<PipePair> CreatePipePair() {
    static std::atomic<uint32_t> pipeInstance{0};
    const std::wstring name = L"\\\\.\\pipe\\FastFilesLegacyScanBinding-" +
                              std::to_wstring(GetCurrentProcessId()) + L"-" +
                              std::to_wstring(GetTickCount64()) + L"-" +
                              std::to_wstring(pipeInstance.fetch_add(1, std::memory_order_relaxed));
    auto pair = std::make_unique<PipePair>();
    pair->server = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
                                    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 65536, 65536, 0, nullptr);
    if (pair->server == INVALID_HANDLE_VALUE) return nullptr;
    pair->client = CreateFileW(name.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pair->client == INVALID_HANDLE_VALUE) return nullptr;
    if (ConnectNamedPipe(pair->server, nullptr) == FALSE && GetLastError() != ERROR_PIPE_CONNECTED) {
        return nullptr;
    }
    return pair;
}

bool HasBufferedBytes(HANDLE pipe) {
    DWORD available = 0;
    const bool peeked = PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) != FALSE;
    return peeked && available != 0;
}

ffprotocol::MftRecordV2 MakeFixtureRecord() {
    ffprotocol::MftRecordV2 record{};
    record.fixed.fileReferenceNumber = 100;
    record.fixed.parentFileReferenceNumber = 5;
    record.fixed.sizeBytes = 23;
    record.fixed.allocatedSizeBytes = 4096;
    record.fixed.fileAttributes = 0;
    record.fileName = u"legacy-fixture.txt";
    record.fixed.fileNameLengthChars = static_cast<uint16_t>(record.fileName.size());
    return record;
}

ffprotocol::MftRecordV2 MakeRootRecord() {
    ffprotocol::MftRecordV2 record{};
    record.fixed.fileReferenceNumber = 5;
    record.fixed.parentFileReferenceNumber = 5;
    record.fixed.fileAttributes = 0x10; // FILE_ATTRIBUTE_DIRECTORY
    record.fileName = u"";
    return record;
}

// The engine half: a real IndexPipeline over a fresh store, a real
// PrivilegedConnection whose callbacks are wired by the real manager Start(),
// and one connection-scoped session for the scanned volume.
class EngineHarness {
public:
    EngineHarness(const char* dbName, uint32_t serialNumber) : dbPath_(fftest::FreshDbPath(dbName)) {
        openFailed_ = !pipeline_.Open(dbPath_);
        if (openFailed_) return;
        ffindexstore::VolumeKey key;
        key.serialNumber = serialNumber;
        durableId_ = pipeline_.ResolveVolume(key);
        manager_ = std::make_unique<ffengine::VolumeSessionManager>(pipeline_, connection_);
        manager_->Start();
        manager_->sessionsByEphemeralId_.emplace(
            kEphemeralKey,
            ffengine::VolumeSessionManager::VolumeSession{durableId_, L'C', 0, false});
    }

    ~EngineHarness() {
        if (manager_) manager_->Stop();
    }

    bool openFailed() const noexcept { return openFailed_; }
    ffengine::IndexPipeline& pipeline() noexcept { return pipeline_; }
    ffengine::PrivilegedConnection& connection() noexcept { return connection_; }
    ffengine::VolumeSessionManager& manager() noexcept { return *manager_; }
    ffindexstore::VolumeRowId durableId() const noexcept { return durableId_; }

    void MarkInFlightScan(bool reconciliationScan) {
        auto& session = manager_->sessionsByEphemeralId_.at(kEphemeralKey);
        session.scanInProgress = true;
        session.reconciliationScan = reconciliationScan;
    }

    bool DispatchFrame(const ffipc::ReceivedFrame& frame) {
        return connection_.DispatchFrameForTesting(frame);
    }

    void SeedDurableEntry() {
        Check(pipeline_.ApplyMftBatch(durableId(), {MakeRootRecord(), MakeFixtureRecord()}),
              "the reproducer seeds a durable root + file entry");
    }

    bool FixtureEntryPresent() {
        const auto snapshot = pipeline_.ExportDirectorySnapshot(durableId(), L"C:");
        const auto root = snapshot.find(L"C:");
        if (root == snapshot.end()) return false;
        for (const auto& entry : root->second.entries) {
            if (entry.name == kFixtureEntryName) return true;
        }
        return false;
    }

private:
    std::string dbPath_;
    bool openFailed_ = false;
    ffindexstore::VolumeRowId durableId_ = 0;
    ffengine::IndexPipeline pipeline_;
    ffengine::PrivilegedConnection connection_;
    std::unique_ptr<ffengine::VolumeSessionManager> manager_;
};

enum class ScanScenario {
    SetupFailure,          // real RunVolumeScan: backup privilege / raw-volume open / geometry failure
    MidEnumerationFailure, // real ScanOutput::Finish(enumerationFailed=true) after a delivered batch
    GenuineSuccess,        // real ScanOutput::Finish(enumerationFailed=false)
};

struct ServiceScanOutcome {
    ffindexsvc::ScanRunResult result = ffindexsvc::ScanRunResult::Completed;
    bool connectionAbortRequested = false;
    std::vector<ffipc::ReceivedFrame> frames;
};

// Runs the real service scanner on the calling thread through the production
// worker boundary, with the negotiated version the service would have derived
// from a peer handshake at that version.
ServiceScanOutcome RunServiceScan(PipePair& pair, ffprotocol::ProtocolVersion negotiated, ScanScenario scenario) {
    ServiceScanOutcome outcome;
    std::mutex writeMutex;
    std::atomic<bool> shouldStop{false};
    std::atomic<bool> transportFailed{false};
    ffindexsvc::ScanRunResult result = ffindexsvc::ScanRunResult::Completed;
    ffindexsvc::RunScanWorkerBoundary(
        pair.server, transportFailed,
        [&]() -> ffindexsvc::ScanRunResult {
            if (scenario == ScanScenario::SetupFailure) {
                result = ffindexsvc::RunVolumeScan(pair.server, writeMutex, kEphemeralVolume, L'Q', {}, false,
                                                   negotiated, shouldStop, &transportFailed);
                return result;
            }
            const bool enumerationFailed = scenario == ScanScenario::MidEnumerationFailure;
            result = ffindexsvc::RunSyntheticVolumeScanForTesting(
                pair.server, writeMutex, kEphemeralVolume, negotiated,
                [](std::vector<ffprotocol::MftRecordV2>& out) {
                    out.push_back(MakeFixtureRecord());
                    return true;
                },
                shouldStop, enumerationFailed, {}, &transportFailed);
            return result;
        },
        [](ffindexsvc::ScanRunResult) {},
        [](ffindexsvc::ScanWorkerExceptionKind) {});
    outcome.result = result;
    outcome.connectionAbortRequested = transportFailed.load();

    // Drain every complete frame the scan produced (batches precede a terminal).
    while (HasBufferedBytes(pair.client)) {
        auto frame = ffipc::ReadFrame(pair.client);
        if (!frame) break;
        outcome.frames.push_back(std::move(*frame));
    }
    return outcome;
}

struct ReproOutcome {
    ServiceScanOutcome service;
    bool engineProtocolViolation = false;
    bool scanMarkedComplete = false;
    bool reconciliationPassActive = false;
    bool reconciliationClockUntouched = false;
    bool fixtureEntryPresent = false;
    bool sessionsCleared = false;
    bool scanStillInFlight = false;
    std::string terminalDescription = "no terminal";
};

int CountTerminalFrames(const ServiceScanOutcome& outcome, uint16_t messageType) {
    int count = 0;
    for (const auto& frame : outcome.frames) {
        if (frame.header.messageType == messageType) ++count;
    }
    return count;
}

ReproOutcome RunMixedVersionCase(const char* dbName, ffprotocol::ProtocolVersion negotiated, ScanScenario scenario,
                                 bool reconciliationScan) {
    ReproOutcome outcome;

    auto pair = CreatePipePair();
    Check(pair != nullptr, "the reproducer creates an in-process pipe pair");
    if (!pair) return outcome;

    EngineHarness harness(dbName, kVolumeSerial);
    Check(!harness.openFailed(), "the engine harness opens a fresh durable store");
    if (harness.openFailed()) return outcome;

    harness.MarkInFlightScan(reconciliationScan);
    if (reconciliationScan) {
        // The pass exists before its scan's records arrive, so a later
        // finalization would treat the seeded entry as unseen and delete it.
        harness.SeedDurableEntry();
        harness.pipeline().BeginReconciliationPass(harness.durableId());
    }

    outcome.service = RunServiceScan(*pair, negotiated, scenario);

    if (CountTerminalFrames(outcome.service, static_cast<uint16_t>(ffprotocol::MessageType::ScanComplete)) > 0) {
        outcome.terminalDescription = "ScanComplete";
    } else if (CountTerminalFrames(outcome.service, static_cast<uint16_t>(ffprotocol::MessageType::ScanFailed)) > 0) {
        outcome.terminalDescription = "ScanFailed";
    }

    // Feed the bytes the service actually wrote through the engine's real
    // frame dispatch, exactly as the Active reader thread would.
    for (const auto& frame : outcome.service.frames) {
        if (!harness.DispatchFrame(frame)) outcome.engineProtocolViolation = true;
    }

    // A service-side connection abort is what the engine observes as a
    // non-Active transition; the engine must then start clean.
    if (outcome.service.connectionAbortRequested) {
        harness.manager().OnConnectionStateChanged(ffengine::ConnectionState::Disconnected);
    }

    const auto meta = harness.pipeline().GetVolumeMetadata(harness.durableId());
    outcome.scanMarkedComplete = meta && meta->scanComplete;
    outcome.reconciliationClockUntouched = meta && meta->lastReconciliationTime == 0;
    outcome.reconciliationPassActive = harness.pipeline().IsReconciliationPassActive(harness.durableId());
    outcome.fixtureEntryPresent = harness.FixtureEntryPresent();
    outcome.sessionsCleared = harness.manager().sessionsByEphemeralId_.empty();
    outcome.scanStillInFlight = !outcome.sessionsCleared &&
                                harness.manager().sessionsByEphemeralId_.at(kEphemeralKey).scanInProgress;
    return outcome;
}

const char* LevelName(ffprotocol::ProtocolVersion version) {
    static char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "{%u,%u}", version.major, version.minor);
    return buffer;
}

// The campaign invariant for a failed scan at one negotiated version.
void ExpectFailedScanCannotBeSuccessful(ffprotocol::ProtocolVersion negotiated, ScanScenario scenario,
                                        const char* scenarioName, const char* dbName) {
    const ReproOutcome outcome = RunMixedVersionCase(dbName, negotiated, scenario, /*reconciliationScan=*/true);
    const char* level = LevelName(negotiated);
    char description[256];

    std::snprintf(description, sizeof(description),
                  "%s %s failure writes no success terminal",
                  level, scenarioName);
    Check(CountTerminalFrames(outcome.service, static_cast<uint16_t>(ffprotocol::MessageType::ScanComplete)) == 0,
          description);

    std::snprintf(description, sizeof(description),
                  "%s %s failure never marks the volume scan complete",
                  level, scenarioName);
    Check(!outcome.scanMarkedComplete, description);

    std::snprintf(description, sizeof(description),
                  "%s %s failure discards the reconciliation pass instead of finalizing it",
                  level, scenarioName);
    Check(!outcome.reconciliationPassActive, description);

    std::snprintf(description, sizeof(description),
                  "%s %s failure deletes no durable records from incomplete coverage",
                  level, scenarioName);
    Check(outcome.fixtureEntryPresent, description);

    std::snprintf(description, sizeof(description),
                  "%s %s failure leaves the reconciliation cadence clock untouched",
                  level, scenarioName);
    Check(outcome.reconciliationClockUntouched, description);

    std::snprintf(description, sizeof(description),
                  "%s %s failure keeps the engine's frame dispatch a valid protocol stream",
                  level, scenarioName);
    Check(!outcome.engineProtocolViolation, description);

    if (ffprotocol::ProtocolSupportsScanFailure(negotiated)) {
        std::snprintf(description, sizeof(description),
                      "%s %s failure still delivers ScanFailed to a capable peer",
                      level, scenarioName);
        Check(CountTerminalFrames(outcome.service, static_cast<uint16_t>(ffprotocol::MessageType::ScanFailed)) == 1,
              description);
        std::snprintf(description, sizeof(description),
                      "%s %s failure keeps the connection alive for a capable peer",
                      level, scenarioName);
        Check(!outcome.service.connectionAbortRequested, description);
        std::snprintf(description, sizeof(description),
                      "%s %s failure clears the in-flight scan state without dropping the connection",
                      level, scenarioName);
        Check(!outcome.sessionsCleared && !outcome.scanStillInFlight, description);
    } else {
        std::snprintf(description, sizeof(description),
                      "%s %s failure never sends a terminal the peer cannot parse",
                      level, scenarioName);
        Check(CountTerminalFrames(outcome.service, static_cast<uint16_t>(ffprotocol::MessageType::ScanFailed)) == 0,
              description);
        std::snprintf(description, sizeof(description),
                      "%s %s failure ends the attempt by aborting the connection",
                      level, scenarioName);
        Check(outcome.service.connectionAbortRequested, description);
        std::snprintf(description, sizeof(description),
                      "%s %s failure clears connection-scoped engine state",
                      level, scenarioName);
        Check(outcome.sessionsCleared, description);
    }
}

// A genuine legacy success must remain a success: ScanComplete, marked
// complete, and no reconciliation deletion for a pass it authoritatively owns.
void ExpectLegacySuccessStaysAuthoritative(ffprotocol::ProtocolVersion negotiated, const char* dbName) {
    const ReproOutcome outcome = RunMixedVersionCase(dbName, negotiated, ScanScenario::GenuineSuccess,
                                                      /*reconciliationScan=*/false);
    const char* level = LevelName(negotiated);
    char description[256];

    std::snprintf(description, sizeof(description),
                  "%s genuine success sends exactly one ScanComplete", level);
    Check(CountTerminalFrames(outcome.service, static_cast<uint16_t>(ffprotocol::MessageType::ScanComplete)) == 1,
          description);

    std::snprintf(description, sizeof(description),
                  "%s genuine success marks the volume indexed", level);
    Check(outcome.scanMarkedComplete, description);

    std::snprintf(description, sizeof(description),
                  "%s genuine success leaves the engine protocol stream valid", level);
    Check(!outcome.engineProtocolViolation, description);

    std::snprintf(description, sizeof(description),
                  "%s genuine success does not abort the connection", level);
    Check(!outcome.service.connectionAbortRequested, description);
}

} // namespace

int main() {
    // Every supported negotiation exercises the invariant separately for a
    // setup failure (the real unmounted-volume path) and a mid-enumeration
    // failure (the real ScanOutput::Finish(enumerationFailed=true) path).
    const ffprotocol::ProtocolVersion legacyVersions[] = {
        {1, 5},
        {2, 0},
        {2, 1},
        {2, 2},
    };
    int caseIndex = 0;
    for (const auto negotiated : legacyVersions) {
        char dbName[96];
        std::snprintf(dbName, sizeof(dbName), "engine_test_legacy_setup_failure_%d.db", caseIndex);
        ExpectFailedScanCannotBeSuccessful(negotiated, ScanScenario::SetupFailure, "setup", dbName);
        std::snprintf(dbName, sizeof(dbName), "engine_test_legacy_mid_enumeration_failure_%d.db", caseIndex);
        ExpectFailedScanCannotBeSuccessful(negotiated, ScanScenario::MidEnumerationFailure, "mid-enumeration", dbName);
        std::snprintf(dbName, sizeof(dbName), "engine_test_legacy_success_%d.db", caseIndex);
        ExpectLegacySuccessStaysAuthoritative(negotiated, dbName);
        ++caseIndex;
    }
    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
