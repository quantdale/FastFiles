#include "VolumeSessionManager.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "VolumeIdentity.h"

#include "ffprotocol/Diagnostics.h"

namespace ffengine {

namespace {

// FILETIME ticks (100ns since 1601-01-01), matching the units
// $STANDARD_INFORMATION's own timestamps already use elsewhere in this
// codebase, so volume-metadata timestamps are directly comparable without
// a unit-conversion footgun.
uint64_t NowAsFileTime() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER value{};
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return value.QuadPart;
}

void AppendManagerDiagnostic(ffprotocol::DiagnosticCategory category,
                             ffprotocol::DiagnosticCode code,
                             std::wstring path,
                             std::wstring volumeId,
                             std::wstring state,
                             uint32_t errorCode,
                             uint64_t itemCount) noexcept {
    try {
        ffprotocol::DiagnosticEvent event{};
        event.category = category;
        event.code = code;
        event.path = std::move(path);
        event.volumeId = std::move(volumeId);
        event.state = std::move(state);
        event.errorCode = errorCode;
        event.itemCount = itemCount;
        ffprotocol::AppendDiagnostic(event);
    } catch (...) { }
}

// Default reconciliation cadence (design.md "Open Questions": cadence is
// left as an implementation/tuning detail, not fixed by the spec). Not
// yet user-configurable -- a reasonable fixed default for now.
constexpr uint64_t kReconciliationIntervalFileTime = 6ULL * 3600 * 10'000'000; // 6 hours
constexpr std::chrono::minutes kSchedulerPollInterval{10};
// residual-defect-closure-and-certification-completion D2: a delivered
// ScanFailed is retried by the existing scheduler, never by an unowned thread
// or an immediate tight loop.
constexpr std::chrono::seconds kScanFailureRetryDelay{5};

std::vector<uint8_t> BuildStartScanPayload(ffprotocol::ProtocolVersion version, ffprotocol::VolumeId volumeId,
                                           const std::vector<uint8_t>& cursor, uint16_t flags) {
    // A persisted cursor from a corrupt DB can exceed protocol limits; truncating
    // would scan from the wrong offset and cause silent index gaps. Treat it as a
    // non-recoverable cursor (fail-closed diagnostic + full rescan) instead.
    if (cursor.size() > ffprotocol::kMaxScanCursorLengthBytes) {
        AppendManagerDiagnostic(ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                ffprotocol::DiagnosticCode::FFDIAG_ENGINE_CONNECT_FAILED,
                                {}, {}, L"BuildStartScanPayload cursor oversize",
                                ERROR_INVALID_DATA, static_cast<uint64_t>(cursor.size()));
        return {};
    }
    const bool useV1 = version.major == 1;
    const size_t headerSize = useV1 ? sizeof(ffprotocol::StartVolumeScanRequestV1)
                                    : sizeof(ffprotocol::StartVolumeScanRequest);
    std::vector<uint8_t> payload(headerSize + cursor.size());
    if (useV1) {
        const ffprotocol::StartVolumeScanRequestV1 request{
            volumeId, static_cast<uint16_t>(cursor.size())};
        std::memcpy(payload.data(), &request, sizeof(request));
    } else {
        const ffprotocol::StartVolumeScanRequest request{
            volumeId, static_cast<uint16_t>(cursor.size()), flags};
        std::memcpy(payload.data(), &request, sizeof(request));
    }
    if (!cursor.empty()) {
        std::memcpy(payload.data() + headerSize, cursor.data(), cursor.size());
    }
    return payload;
}

} // namespace

VolumeSessionManager::VolumeSessionManager(IndexPipeline& pipeline, PrivilegedConnection& connection)
    : pipeline_(pipeline), connection_(connection) {}

VolumeSessionManager::~VolumeSessionManager() {
    Stop();
}

void VolumeSessionManager::SendSessionTeardown(ffprotocol::VolumeId ephemeralId,
                                                const VolumeSession& session) {
    std::lock_guard<std::mutex> requestLock(requestMutex_);
    if (!active_.load()) {
        return;
    }
    const ffprotocol::StopVolumeScanRequest stop{ephemeralId};
    connection_.SendRequest(static_cast<uint16_t>(ffprotocol::MessageType::StopVolumeScan), &stop, sizeof(stop));
    if (session.journalOpenRequested) {
        const ffprotocol::CloseUsnJournalRequest close{ephemeralId};
        connection_.SendRequest(static_cast<uint16_t>(ffprotocol::MessageType::CloseUsnJournal), &close, sizeof(close));
    }
}

void VolumeSessionManager::ReloadConfiguration(std::vector<ffprotocol::VolumeSetting> volumes) {
    std::vector<std::pair<ffprotocol::VolumeId, VolumeSession>> toStop;
    {
        std::scoped_lock lock(mutex_, wakeMutex_);
        configuredVolumes_ = std::move(volumes);
        // settings-and-appearance §9.2: a volume that was disabled (or
        // removed from the persisted selection) while a session was live
        // is torn down immediately -- no waiting for an engine restart.
        // The volume stays marked available (disabling is not
        // unavailability); only the session, its privileged-path work,
        // and its published directories go away. retry-ownership-and-focus-escape-closure D2: any retry intent the
        // session owned moves to the durable floor first, so re-enabling the
        // volume later cannot silently drop it.
        const auto disabledAt = std::chrono::steady_clock::now();
        for (auto it = sessionsByEphemeralId_.begin(); it != sessionsByEphemeralId_.end();) {
            const wchar_t letter = static_cast<wchar_t>(towupper(it->second.driveLetter));
            const auto configured = std::find_if(configuredVolumes_.begin(), configuredVolumes_.end(), [letter](const auto& volume) {
                return !volume.key.empty() && static_cast<wchar_t>(towupper(volume.key.front())) == letter;
            });
            if (configured != configuredVolumes_.end() && configured->enabled) {
                // zero-touch-autonomous-engineering (subtree gating): push the
                // (possibly updated) rules for this surviving session so the
                // ingestion pipeline picks up rule changes without waiting for
                // the next EnumerateVolumes poll (settings-and-appearance 2.5).
                pipeline_.SetVolumeRules(it->second.durableId, *configured);
                ++it;
                continue;
            }
            toStop.emplace_back(ffprotocol::VolumeId{it->first}, it->second);
            TransferSessionRetryToFloorLocked(it->first, it->second, disabledAt);
            it = sessionsByEphemeralId_.erase(it);
        }
    }
    for (const auto& [ephemeralId, session] : toStop) {
        SendSessionTeardown(ephemeralId, session);
        // The unavailable callback's contract is "withdraw this drive's
        // published directories" -- which is exactly what disabling needs.
        if (onVolumeUnavailable_) {
            onVolumeUnavailable_(session.durableId, session.driveLetter);
        }
    }
    RefreshProjectionScanState();
    // Re-enumeration makes newly enabled volumes enter the normal session
    // start path without waiting for an engine restart.
    if (active_.load()) {
        connection_.SendRequest(static_cast<uint16_t>(ffprotocol::MessageType::EnumerateVolumes));
    }
}

void VolumeSessionManager::SetIndexingPaused(uint8_t scope, bool paused) {
    std::vector<wchar_t> affectedLetters;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (scope == 0) {
            paused_ = paused;
            for (const auto& [ephemeralValue, session] : sessionsByEphemeralId_) {
                affectedLetters.push_back(static_cast<wchar_t>(towupper(session.driveLetter)));
            }
            if (!paused) {
                pausedVolumes_.clear(); // global resume clears any per-volume pauses too
            }
        } else {
            const wchar_t letter = static_cast<wchar_t>(towupper(static_cast<wchar_t>(scope)));
            if (letter < L'A' || letter > L'Z') {
                return;
            }
            if (paused) {
                pausedVolumes_.insert(letter);
            } else {
                pausedVolumes_.erase(letter);
            }
            affectedLetters.push_back(letter);
        }
    }
    // On resume, re-issue scan/journal work for every affected session
    // from its stored (last-applied) position -- the "continue from where
    // it left off" half of §9.1's resume scenario. While still paused the
    // re-issue is a no-op (StartOrResumeVolume gates on pause state).
    if (!paused) {
        ResumeAffectedSessions(affectedLetters);
    }
}

bool VolumeSessionManager::IsPausedLocked(wchar_t driveLetter) const {
    if (paused_.load()) {
        return true;
    }
    return pausedVolumes_.find(static_cast<wchar_t>(towupper(driveLetter))) != pausedVolumes_.end();
}

void VolumeSessionManager::ResumeAffectedSessions(const std::vector<wchar_t>& affectedLetters) {
    std::vector<std::pair<ffprotocol::VolumeId, VolumeSession>> toResume;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [ephemeralValue, session] : sessionsByEphemeralId_) {
            if (std::find(affectedLetters.begin(), affectedLetters.end(),
                          static_cast<wchar_t>(towupper(session.driveLetter))) == affectedLetters.end()) {
                continue;
            }
            if (IsPausedLocked(session.driveLetter)) {
                continue; // still paused (e.g. individually paused while global was resumed)
            }
            toResume.emplace_back(ffprotocol::VolumeId{ephemeralValue}, session);
        }
    }
    for (const auto& [ephemeralId, session] : toResume) {
        StartOrResumeVolume(ephemeralId, session);
    }
}

void VolumeSessionManager::Start() {
    connection_.SetVolumeListCallback([this](std::vector<ffprotocol::VolumeInfo> volumes) { OnVolumeList(std::move(volumes)); });
    connection_.SetScanBatchCallback([this](ffprotocol::VolumeId id, std::vector<uint8_t> cursor,
                                             std::vector<ffprotocol::MftRecordV1> records) {
        OnScanBatch(id, std::move(cursor), std::move(records));
    });
    connection_.SetScanBatchV2Callback([this](ffprotocol::VolumeId id, std::vector<uint8_t> cursor,
                                               std::vector<ffprotocol::MftRecordV2> records) {
        OnScanBatchV2(id, std::move(cursor), std::move(records));
    });
    connection_.SetScanCompleteCallback([this](ffprotocol::VolumeId id) { OnScanComplete(id); });
    connection_.SetScanFailedCallback([this](ffprotocol::VolumeId id) { OnScanFailed(id); });
    connection_.SetScanAttemptStartedCallback([this](ffprotocol::VolumeId id) { OnScanAttemptStarted(id); });
    connection_.SetJournalOpenedCallback(
        [this](ffprotocol::VolumeId id, uint64_t journalId, uint64_t currentUsn) { OnJournalOpened(id, journalId, currentUsn); });
    connection_.SetJournalBatchCallback([this](ffprotocol::VolumeId id, uint64_t latestUsn,
                                                std::vector<ffprotocol::UsnDeltaV2> records) {
        OnJournalBatch(id, latestUsn, std::move(records));
    });
    connection_.SetJournalBatchV3Callback([this](ffprotocol::VolumeId id, uint64_t latestUsn,
                                                  std::vector<ffprotocol::UsnDeltaV3> records) {
        OnJournalBatchV3(id, latestUsn, std::move(records));
    });
    connection_.SetJournalResumeInvalidCallback([this](ffprotocol::VolumeId id) { OnJournalResumeInvalid(id); });

    running_ = true;
    reconciliationThread_ = std::thread(&VolumeSessionManager::ReconciliationSchedulerLoop, this);
}

void VolumeSessionManager::Stop() {
    bool wasRunning = false;
    {
        // Publish the stop while holding the scheduler's wait mutex. A notify
        // sent outside it can land between the scheduler's predicate re-check
        // and its wait_for entry, losing the wakeup for a full 10-minute poll
        // interval -- an engine shutdown stall, not just a test hang. Shutdown
        // is the one transition that may discard owed retry work, so every
        // owner is cleared under the lock pair (D2).
        std::scoped_lock lock(mutex_, wakeMutex_);
        wasRunning = running_.exchange(false);
        failedScanRetries_.clear();
        schedulerDispatchRetries_.clear();
        scanReissueFloors_.clear();
    }
    if (!wasRunning) {
        return;
    }
    wakeCv_.notify_all();
    if (reconciliationThread_.joinable()) {
        reconciliationThread_.join();
    }

    std::vector<std::pair<ffprotocol::VolumeId, VolumeSession>> sessions;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions.reserve(sessionsByEphemeralId_.size());
        for (const auto& [ephemeralValue, session] : sessionsByEphemeralId_) {
            sessions.emplace_back(ffprotocol::VolumeId{ephemeralValue}, session);
        }
    }
    for (const auto& [ephemeralId, session] : sessions) {
        SendSessionTeardown(ephemeralId, session);
    }
    active_ = false;
    // The manager owns the callback registrations and must stop the source
    // before its destructor returns; otherwise the connection's reader could
    // dispatch into this object after the manager has been destroyed.
    connection_.Stop();
}

void VolumeSessionManager::OnConnectionStateChanged(ConnectionState state) {
    if (state != ConnectionState::Active) {
        active_ = false;
        // Ephemeral VolumeIds are only valid for the lifetime of one
        // connection -- clearing this on every non-Active transition
        // means the next Active period starts clean and re-derives
        // durable identity from a fresh EnumerateVolumes (task 7.1).
        std::vector<std::pair<ffindexstore::VolumeRowId, wchar_t>> abandonedPasses;
        {
            // retry-ownership-and-focus-escape-closure D2: every retry transition runs under the lock pair, so this
            // snapshot can never observe the intent unowned. Each session's
            // in-flight attempt, queued entry, or in-hand dispatch moves onto
            // the durable floor before the connection-scoped ids are erased.
            std::scoped_lock lock(mutex_, wakeMutex_);
            const auto disconnectedAt = std::chrono::steady_clock::now();
            for (const auto& [ephemeralValue, session] : sessionsByEphemeralId_) {
                abandonedPasses.emplace_back(session.durableId, session.driveLetter);
                TransferSessionRetryToFloorLocked(ephemeralValue, session, disconnectedAt);
            }
            // An in-hand dispatch whose session vanished before this snapshot
            // still owes its durable volume: keep it on the floor instead of
            // dropping it with the connection-scoped state.
            for (const auto& [durableId, retry] : schedulerDispatchRetries_) {
                MergeDurableFloorLocked(durableId, retry);
            }
            sessionsByEphemeralId_.clear();
            lastSnapshotPublish_.clear();
            failedScanRetries_.clear();
            schedulerDispatchRetries_.clear();
            ++scanRetryGeneration_;
        }
        wakeCv_.notify_one();
        // legacy-protocol-correctness-and-raw-volume-cancellation-closure D3:
        // a reconciliation pass is only ever finalized by the authoritative
        // completion of its own scan. Losing the connection loses that scan
        // (including the unreportable legacy failure the service ends by
        // tearing the connection down), so the pass is discarded here -- its
        // partial seen-set can never drive removals, and a later
        // reconciliation starts fresh through BeginReconciliationPass.
        for (const auto& [durableId, driveLetter] : abandonedPasses) {
            if (pipeline_.IsReconciliationPassActive(durableId)) {
                pipeline_.AbortReconciliationPass(durableId);
                AppendManagerDiagnostic(ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                        ffprotocol::DiagnosticCode::FFDIAG_STALE_SCAN_TERMINAL,
                                        {}, std::wstring(1, driveLetter),
                                        L"reconciliation-pass-aborted-connection-lost", 0, 0);
            }
        }
        pipeline_.SetBulkScanActive(false);
        return;
    }
    const bool wasActive = active_.exchange(true);
    if (!wasActive) {
        connection_.SendRequest(static_cast<uint16_t>(ffprotocol::MessageType::EnumerateVolumes));
    }
}

void VolumeSessionManager::OnVolumeList(std::vector<ffprotocol::VolumeInfo> volumes) {
    std::vector<std::pair<ffprotocol::VolumeId, VolumeSession>> toStart;
    std::vector<std::pair<ffprotocol::VolumeId, VolumeSession>> unavailable;

    {
        // retry-ownership-and-focus-escape-closure D2: disappearance transfers any retry intent to the durable floor
        // before the ephemeral id is erased, exactly like a disconnect.
        std::scoped_lock lock(mutex_, wakeMutex_);
        std::vector<uint32_t> seenEphemeral;
        seenEphemeral.reserve(volumes.size());

        // settings-and-appearance §9.3: every drive letter the engine
        // observes in the current poll, whether configured or not --
        // pending-decision is derived from this set against the persisted
        // selection, never stored as separate state.
        observedLetters_.clear();
        for (const auto& info : volumes) {
            observedLetters_.insert(static_cast<wchar_t>(towupper(info.driveLetter)));
        }

        for (const auto& info : volumes) {
            seenEphemeral.push_back(info.id.value);
            if (sessionsByEphemeralId_.find(info.id.value) != sessionsByEphemeralId_.end()) {
                continue; // already tracked this connection
            }
            const auto configured = std::find_if(configuredVolumes_.begin(), configuredVolumes_.end(), [&info](const auto& volume) {
                return !volume.key.empty() && towupper(volume.key.front()) == towupper(info.driveLetter);
            });
            if (configured == configuredVolumes_.end() || !configured->enabled) {
                continue; // absent means pending user decision; disabled volumes are never started
            }
            auto key = ResolveVolumeKeyForDriveLetter(info.driveLetter);
            if (!key) {
                continue; // couldn't resolve durable identity right now -- try again next poll
            }
            auto durableId = pipeline_.ResolveVolume(*key);
            if (durableId == 0) {
                continue;
            }
            pipeline_.SetVolumeAvailable(durableId, true, NowAsFileTime());
            // zero-touch-autonomous-engineering (subtree gating): push this
            // volume's configured include/exclude rules into the ingestion
            // pipeline so ApplyMftBatch/ApplyUsnBatch/reconciliation/rebuild
            // honor them (settings-and-appearance 2.5).
            pipeline_.SetVolumeRules(durableId, *configured);

            VolumeSession session;
            session.durableId = durableId;
            session.driveLetter = info.driveLetter;
            sessionsByEphemeralId_[info.id.value] = session;
            toStart.emplace_back(info.id, session);
        }

        // tasks.md 7.2: a previously-tracked volume absent from this
        // poll is marked unavailable, never deleted.
        for (auto it = sessionsByEphemeralId_.begin(); it != sessionsByEphemeralId_.end();) {
            if (std::find(seenEphemeral.begin(), seenEphemeral.end(), it->first) == seenEphemeral.end()) {
                pipeline_.SetVolumeAvailable(it->second.durableId, false, NowAsFileTime());
                unavailable.emplace_back(ffprotocol::VolumeId{it->first}, it->second);
                TransferSessionRetryToFloorLocked(it->first, it->second, std::chrono::steady_clock::now());
                it = sessionsByEphemeralId_.erase(it);
            } else {
                ++it;
            }
        }
    }

    RefreshProjectionScanState();

    for (const auto& [ephemeralId, session] : toStart) {
        StartOrResumeVolume(ephemeralId, session);
    }
    for (const auto& [ephemeralId, session] : unavailable) {
        SendSessionTeardown(ephemeralId, session);
        if (onVolumeUnavailable_) {
            onVolumeUnavailable_(session.durableId, session.driveLetter);
        }
    }
}

bool VolumeSessionManager::StartOrResumeVolume(ffprotocol::VolumeId ephemeralId, const VolumeSession& session) {
    {
        std::scoped_lock lock(mutex_, wakeMutex_);
        const std::wstring key(1, session.driveLetter);
        const auto configured = std::find_if(configuredVolumes_.begin(), configuredVolumes_.end(), [&key](const auto& volume) {
            return !volume.key.empty() && towupper(volume.key.front()) == key.front();
        });
        if (configured != configuredVolumes_.end() && !configured->enabled) return false;
        // settings-and-appearance §9.1: paused volumes are never (re)started.
        if (IsPausedLocked(session.driveLetter)) return false;
    }
    auto meta = pipeline_.GetVolumeMetadata(session.durableId);
    const bool scanComplete = meta && meta->scanComplete;
    std::optional<FailedScanRetry> queuedRetry;
    std::optional<FailedScanRetry> reissueFloor;
    bool startInitialScan = false;
    bool awaitingScanAttemptStarted = false;
    bool resetConnectionForAuthority = false;
    bool retryOwnerChanged = false;
    const bool supportsScanAttemptStarted =
        ffprotocol::ProtocolSupportsScanAttemptStarted(connection_.NegotiatedVersion());

    // tasks.md 6.3/D8: only (re)start the initial scan if it never
    // finished; once complete, the journal plus periodic reconciliation
    // is what keeps the volume current -- reconnecting never triggers an
    // unconditional full rescan (spec "Volume Reconnection Triggers
    // Resume-or-Reconcile, Not Blind Rescan").
    {
        // retry-ownership-and-focus-escape-closure D2: inspecting the queue/floor, taking the floor, and claiming the
        // attempt are one transition under the lock pair, so a disconnect can
        // never land between "no owner visible" and "owner installed".
        std::scoped_lock lock(mutex_, wakeMutex_);
        auto it = sessionsByEphemeralId_.find(ephemeralId.value);
        if (it == sessionsByEphemeralId_.end() || it->second.durableId != session.durableId) {
            return false;
        }
        const auto queued = failedScanRetries_.find(ephemeralId.value);
        if (queued != failedScanRetries_.end()) {
            queuedRetry = queued->second;
        }
        if (it->second.awaitingScanAttemptStarted) {
            // A second local resume cannot be allowed to overtake the
            // outstanding barrier: ScanAttemptStarted carries only the
            // connection-scoped volume id, so one provisional replacement at
            // a time is what makes its stream ordering meaningful.
            return false;
        }
        reissueFloor = TakeScanReissueFloorLocked(session.durableId);
        if (reissueFloor) {
            // retry-ownership-and-focus-escape-closure D2: the durable floor's single retry intent is transferred onto
            // this replacement connection's own retry slot in the same
            // critical section that consumes the floor. The stored kind
            // travels with it, so an interrupted reconciliation of an
            // already-indexed volume is reattached as a reconciliation (D4)
            // instead of waiting for the ordinary poll.
            failedScanRetries_[ephemeralId.value] = {reissueFloor->due, reissueFloor->reconciliation};
            ++scanRetryGeneration_;
            retryOwnerChanged = true;
        } else if (!queuedRetry && !scanComplete) {
            // scan-generation-integrity-and-residual-certification-closure
            // 3.4: a stopped worker can emit no terminal. A local counter
            // cannot then tell its replacement's only terminal apart from an
            // old one. A {2,3}+ service supplies the ordered barrier; a
            // staged peer without it gets a fresh connection-scoped id. A
            // retry-ownership-and-focus-escape-closure 3.3: a replacement that supersedes an attempt still in flight (a)
            // pause/resume cycle) is exactly as ambiguous as one that
            // superseded a coverage hole: the retired worker can still emit
            // its only terminal into the shared stream.
            const bool supersedesInFlight = it->second.scanInProgress;
            if (it->second.scanBatchFailed || supersedesInFlight) {
                if (!supportsScanAttemptStarted) {
                    resetConnectionForAuthority = true;
                } else {
                    it->second.awaitingScanAttemptStarted = true;
                    awaitingScanAttemptStarted = true;
                    AppendManagerDiagnostic(ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                            ffprotocol::DiagnosticCode::FFDIAG_STALE_SCAN_TERMINAL,
                                            {}, std::wstring(1, it->second.driveLetter),
                                            L"scan-attempt-started-barrier-pending", 0, 0);
                }
            }
            if (!resetConnectionForAuthority) {
                // The coverage hole, or the superseded attempt, remains
                // non-authoritative until the ordered service barrier proves
                // that subsequent frames belong to the replacement rather
                // than an older worker still draining the shared pipe.
                ClaimActiveAttemptLocked(ephemeralId.value, session.durableId, /*reconciliation=*/false);
                if (!awaitingScanAttemptStarted) {
                    it->second.scanBatchFailed = false;
                }
                startInitialScan = true;
            }
        }
    }
    if (retryOwnerChanged) {
        wakeCv_.notify_one();
    }
    if (resetConnectionForAuthority) {
        // No pre-{2,3} stream marker can prove that the service joined the old
        // worker. Discarding the connection makes the old VolumeId unusable
        // and lets the normal durable reissue-floor path retry safely.
        connection_.ResetForScanAuthority();
        return false;
    }
    if (reissueFloor && !scanComplete) {
        // A pause/resume is not permission to bypass the five-second failure
        // cadence. The initial scan is deferred through the transferred slot
        // so the reissue honors that floor.
        return false;
    }
    if (startInitialScan) {
        RefreshProjectionScanState();
        std::vector<uint8_t> cursor = meta ? meta->scanCursor : std::vector<uint8_t>{};
        auto payload = BuildStartScanPayload(connection_.NegotiatedVersion(), ephemeralId, cursor, 0);
        if (payload.empty() && !cursor.empty()) {
            // Oversize corrupt cursor — already logged inside BuildStartScanPayload.
            // Force a clean rescan from offset 0 rather than silently truncating.
            payload = BuildStartScanPayload(connection_.NegotiatedVersion(), ephemeralId, {}, 0);
        }
        if (!connection_.SendRequest(static_cast<uint16_t>(ffprotocol::MessageType::StartVolumeScan), payload.data(),
                                     static_cast<uint32_t>(payload.size()))) {
            // WriteFrameOverlapped can fail after emitting a prefix of a
            // frame. Do not roll provisional state back into a connection
            // whose service may still process that prefix; clear this whole
            // connection generation instead. The in-flight claim travels to
            // the durable floor if that reset ends the connection.
            connection_.ResetForScanAuthority();
            return false;
        }
    }

    // tasks.md 7.5/7.6: always (re)request the journal from the persisted
    // position -- OnJournalOpened reconciles the JournalId mismatch case
    // once the service's ack reports the volume's *current* journal
    // identity, since that isn't known until the service replies.
    return RequestJournalOpen(ephemeralId, meta ? meta->resumeUsn : 0, session.durableId);
}

bool VolumeSessionManager::RequestJournalOpen(ffprotocol::VolumeId ephemeralId, uint64_t resumeUsn,
                                              ffindexstore::VolumeRowId expectedDurableId) {
    if (!running_.load()) {
        return false;
    }
    std::lock_guard<std::mutex> requestLock(requestMutex_);
    if (!running_.load()) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessionsByEphemeralId_.find(ephemeralId.value);
        if (it == sessionsByEphemeralId_.end() ||
            (expectedDurableId != 0 && it->second.durableId != expectedDurableId)) {
            return false;
        }
        it->second.journalOpenRequested = true;
    }

    const ffprotocol::OpenUsnJournalRequest request{ephemeralId, resumeUsn};
    if (connection_.SendRequest(static_cast<uint16_t>(ffprotocol::MessageType::OpenUsnJournal), &request, sizeof(request))) {
        return true;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessionsByEphemeralId_.find(ephemeralId.value);
    if (it != sessionsByEphemeralId_.end() &&
        (expectedDurableId == 0 || it->second.durableId == expectedDurableId)) {
        it->second.journalOpenRequested = false;
    }
    return false;
}

// legacy-protocol-correctness-and-raw-volume-cancellation-closure D3/8.6:
// the unreportable legacy failure ends its connection without writing any
// terminal, so the delivered-ScanFailed retry queue is never armed and the
// connection-scoped session (with its ephemeral id) is cleared. A durable,
// per-volume floor keeps the documented five-second failed-scan cadence across
// that teardown instead of letting the replacement connection restart a
// persistently failing volume's scan immediately.
std::optional<VolumeSessionManager::FailedScanRetry> VolumeSessionManager::TakeScanReissueFloorLocked(
    ffindexstore::VolumeRowId durableId) {
    auto floor = scanReissueFloors_.find(durableId);
    if (floor == scanReissueFloors_.end()) {
        return std::nullopt;
    }
    // Consumed exactly once: its single retry intent is transferred onto the
    // replacement connection's own retry slot (or issued directly) by the
    // caller that takes it. An unexpired due time keeps the five-second floor;
    // an already-expired one still enters the slot so the work is retried
    // promptly instead of waiting for the ordinary poll or six-hour interval.
    const FailedScanRetry pending = floor->second;
    scanReissueFloors_.erase(floor);
    return pending;
}

VolumeSessionManager::RetryOwnership VolumeSessionManager::RetryOwnerStateLocked(
    ffindexstore::VolumeRowId durableId) const {
    if (schedulerDispatchRetries_.find(durableId) != schedulerDispatchRetries_.end()) {
        return RetryOwnership::SchedulerDispatching;
    }
    if (scanReissueFloors_.find(durableId) != scanReissueFloors_.end()) {
        return RetryOwnership::DurableFloor;
    }
    for (const auto& [ephemeralValue, session] : sessionsByEphemeralId_) {
        if (session.durableId != durableId) {
            continue;
        }
        if (session.scanInProgress) {
            return RetryOwnership::ActiveAttempt;
        }
        if (failedScanRetries_.find(ephemeralValue) != failedScanRetries_.end()) {
            return RetryOwnership::EphemeralQueued;
        }
    }
    return RetryOwnership::None;
}

void VolumeSessionManager::MergeDurableFloorLocked(ffindexstore::VolumeRowId durableId,
                                                   const FailedScanRetry& retry) {
    auto [floor, inserted] = scanReissueFloors_.try_emplace(durableId, retry);
    if (!inserted) {
        floor->second.due = (std::max)(floor->second.due, retry.due);
        floor->second.reconciliation = floor->second.reconciliation || retry.reconciliation;
    }
}

void VolumeSessionManager::TransferSessionRetryToFloorLocked(uint32_t ephemeralValue,
                                                             const VolumeSession& session,
                                                             std::chrono::steady_clock::time_point at) {
    std::optional<FailedScanRetry> pending;
    const auto queued = failedScanRetries_.find(ephemeralValue);
    if (queued != failedScanRetries_.end()) {
        pending = queued->second;
    }
    if (session.scanInProgress) {
        // The active attempt also became unreportable at this transition, so
        // retain both five-second bounds (the later one) and the attempt's own
        // owed kind.
        FailedScanRetry active{at + kScanFailureRetryDelay, session.reconciliationScan};
        if (pending) {
            active.due = (std::max)(pending->due, active.due);
        }
        pending = active;
    }
    const auto dispatch = schedulerDispatchRetries_.find(session.durableId);
    if (dispatch != schedulerDispatchRetries_.end()) {
        if (pending) {
            pending->due = (std::max)(pending->due, dispatch->second.due);
        } else {
            pending = dispatch->second;
        }
        schedulerDispatchRetries_.erase(dispatch);
    }
    if (pending) {
        MergeDurableFloorLocked(session.durableId, *pending);
    }
    failedScanRetries_.erase(ephemeralValue);
}

void VolumeSessionManager::InstallNonAuthoritativeRetryLocked(uint32_t ephemeralValue,
                                                              ffindexstore::VolumeRowId durableId,
                                                              std::chrono::steady_clock::time_point due,
                                                              bool reconciliation) {
    // The single next owner is chosen inside the same critical section that
    // closes the attempt, so a disconnect can never observe neither an
    // in-flight scan nor a retry owner. A dead connection is never given an
    // ephemeral entry -- its durable identity carries the floor instead.
    schedulerDispatchRetries_.erase(durableId);
    if (active_.load()) {
        failedScanRetries_[ephemeralValue] = {due, reconciliation};
        ++scanRetryGeneration_;
        return;
    }
    MergeDurableFloorLocked(durableId, FailedScanRetry{due, reconciliation});
}

void VolumeSessionManager::ClaimActiveAttemptLocked(uint32_t ephemeralValue,
                                                    ffindexstore::VolumeRowId durableId,
                                                    bool reconciliation) {
    // Consume whichever other owner was installed for this volume: the
    // in-flight attempt becomes the sole owner.
    schedulerDispatchRetries_.erase(durableId);
    failedScanRetries_.erase(ephemeralValue);
    auto it = sessionsByEphemeralId_.find(ephemeralValue);
    if (it == sessionsByEphemeralId_.end() || it->second.durableId != durableId) {
        return;
    }
    it->second.scanInProgress = true;
    it->second.reconciliationScan = reconciliation;
}

void VolumeSessionManager::ClearRetryOwnerLocked(ffindexstore::VolumeRowId durableId, uint32_t ephemeralValue) {
    failedScanRetries_.erase(ephemeralValue);
    schedulerDispatchRetries_.erase(durableId);
    scanReissueFloors_.erase(durableId);
}

void VolumeSessionManager::RearmFailedAdmissionLocked(uint32_t ephemeralValue,
                                                      ffindexstore::VolumeRowId durableId,
                                                      bool reconciliation) {
    auto it = sessionsByEphemeralId_.find(ephemeralValue);
    if (it == sessionsByEphemeralId_.end() || it->second.durableId != durableId) {
        // The connection generation was already discarded, so the disconnect
        // transfer owns the intent on the durable floor.
        return;
    }
    it->second.scanInProgress = false;
    it->second.reconciliationScan = false;
    InstallNonAuthoritativeRetryLocked(ephemeralValue, durableId,
                                       std::chrono::steady_clock::now() + kScanFailureRetryDelay,
                                       reconciliation);
}

bool VolumeSessionManager::TriggerReconciliation(ffprotocol::VolumeId ephemeralId,
                                                 ffindexstore::VolumeRowId durableId) {
    // index-durability-false-success-closure D4: a reconciliation pass is
    // finalized by the authoritative completion of the scan started here,
    // so starting one must respect the scan authority that already exists
    // for the volume rather than racing it.
    std::optional<FailedScanRetry> reissueFloor;
    bool resetConnectionForAuthority = false;
    bool retryOwnerChanged = false;
    {
        // retry-ownership-and-focus-escape-closure D2: the busy/deferral check, the floor transfer, and the claim are
        // one transition under the lock pair.
        std::scoped_lock lock(mutex_, wakeMutex_);
        auto it = sessionsByEphemeralId_.find(ephemeralId.value);
        if (it == sessionsByEphemeralId_.end() || it->second.durableId != durableId) {
            // Session vanished (volume list changed / teardown raced the
            // caller). An orphan StartVolumeScan for a stale VolumeId is
            // rejected by the service as a connection-violating handle and
            // would tear the connection down for every *other* volume.
            return false;
        }
        if (IsPausedLocked(it->second.driveLetter)) {
            // settings-and-appearance §9.1: paused volumes get no new scan
            // authority. Previously only the scheduler honored this; the
            // journal-gap triggers did not.
            return false;
        }
        if (it->second.scanInProgress) {
            // A scan is in flight: its completion -- not a second
            // BeginReconciliationPass clearing the first pass's seen-set
            // mid-scan -- is what must decide. Re-arm after it lands.
            if (!it->second.deferredReconciliation) {
                it->second.deferredReconciliation = true;
                AppendManagerDiagnostic(ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                        ffprotocol::DiagnosticCode::FFDIAG_RECONCILIATION_DEFERRED,
                                        {}, std::wstring(1, it->second.driveLetter),
                                        L"reconciliation-deferred-scan-in-flight", 0, 0);
            }
            return false;
        }
        reissueFloor = TakeScanReissueFloorLocked(durableId);
        if (reissueFloor) {
            // retry-ownership-and-focus-escape-closure D2: the floor's single retry intent moves onto this connection's
            // own slot under the same critical section that consumed it. The
            // owed kind travels with it -- an interrupted initial scan is not
            // silently upgraded to a reconciliation, and the due time is kept.
            failedScanRetries_[ephemeralId.value] = {reissueFloor->due, reissueFloor->reconciliation};
            ++scanRetryGeneration_;
            retryOwnerChanged = true;
        } else if (it->second.scanBatchFailed) {
            // Defensive only: the normal batch-failure terminal path clears
            // this flag before a deferred reconciliation can reach here. If a
            // caller ever observes a hole with no owning scan, a volume-only
            // terminal is no longer orderable, so discard the connection
            // instead of silently granting a reconciliation clean slate.
            resetConnectionForAuthority = true;
        } else if (failedScanRetries_.find(ephemeralId.value) != failedScanRetries_.end()) {
            // No caller may preempt an owned retry slot: the slot issues at its
            // bounded due time and carries exactly one intent. Consuming it
            // here would issue before the five-second floor and could change
            // the owed work's kind.
            return false;
        } else {
            ClaimActiveAttemptLocked(ephemeralId.value, durableId, /*reconciliation=*/true);
        }
    }
    if (retryOwnerChanged) {
        wakeCv_.notify_one();
    }
    if (resetConnectionForAuthority) {
        connection_.ResetForScanAuthority();
        return false;
    }
    if (reissueFloor) {
        // A previous scan for this durable volume ended in connection loss
        // without a terminal: the reissue was deferred through the bounded
        // retry scheduler so it honors the same five-second cadence a
        // delivered ScanFailed would have used.
        return false;
    }
    RefreshProjectionScanState();
    pipeline_.BeginReconciliationPass(durableId);
    // Reconciliation is deliberately lower priority than an initial scan:
    // it is a correctness backstop and must not compete with foreground
    // indexing or interactive filesystem work (task 8.2).
    const auto payload = BuildStartScanPayload(connection_.NegotiatedVersion(), ephemeralId, {},
                                                ffprotocol::kStartVolumeScanLowPriority);
    if (connection_.SendRequest(static_cast<uint16_t>(ffprotocol::MessageType::StartVolumeScan), payload.data(),
                                static_cast<uint32_t>(payload.size()))) {
        return true;
    }
    // A failed frame write is not proof that the service saw no request. The
    // connection reset drops the provisional reconciliation owner together
    // with its connection-scoped volume id; restore the single bounded retry
    // owner on the still-live session so the intent is not lost. The pass's
    // own terminal handling (or the scheduler's abort) discards the partial
    // seen-set.
    {
        std::scoped_lock lock(mutex_, wakeMutex_);
        RearmFailedAdmissionLocked(ephemeralId.value, durableId, /*reconciliation=*/true);
    }
    wakeCv_.notify_one();
    connection_.ResetForScanAuthority();
    return false;
}

void VolumeSessionManager::OnScanBatch(
    ffprotocol::VolumeId volumeId, std::vector<uint8_t> cursor, std::vector<ffprotocol::MftRecordV1> records) {
    ApplyScanBatch(volumeId, std::move(cursor), [&](ffindexstore::VolumeRowId durableId) {
        return pipeline_.ApplyMftBatch(durableId, records);
    });
}

void VolumeSessionManager::OnScanBatchV2(
    ffprotocol::VolumeId volumeId, std::vector<uint8_t> cursor, std::vector<ffprotocol::MftRecordV2> records) {
    // allocated-size-storage-accuracy 5.2: identical lifecycle to the V1
    // path; only the record type (and thus allocated-size availability)
    // differs.
    ApplyScanBatch(volumeId, std::move(cursor), [&](ffindexstore::VolumeRowId durableId) {
        return pipeline_.ApplyMftBatch(durableId, records);
    });
}

void VolumeSessionManager::ApplyScanBatch(ffprotocol::VolumeId volumeId, std::vector<uint8_t> cursor,
                                          const std::function<bool(ffindexstore::VolumeRowId)>& apply) {
    ffindexstore::VolumeRowId durableId = 0;
    wchar_t driveLetter = L'\0';
    bool droppedWhilePaused = false;
    bool droppedBeforeScanAttemptStarted = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessionsByEphemeralId_.find(volumeId.value);
        if (it == sessionsByEphemeralId_.end()) {
            return;
        }
        durableId = it->second.durableId;
        driveLetter = it->second.driveLetter;
        if (it->second.awaitingScanAttemptStarted) {
            // scan-generation-integrity-and-residual-certification-closure
            // 3.4: this frame was ordered before the service's replacement
            // barrier, so it may be from the superseded worker. Keep the
            // frozen cursor and the coverage hole until the barrier proves a
            // new worker has started.
            droppedBeforeScanAttemptStarted = true;
        }
        // settings-and-appearance §9.1: while paused, batches are dropped
        // without applying and without advancing the stored cursor, so
        // resume re-issues the scan from the last-applied position.
        // legacy-protocol-correctness-and-raw-volume-cancellation-closure
        // 8.6 (reviewer finding): a dropped batch is a coverage hole exactly
        // like a failed apply (PA-003). If the volume resumes before the
        // terminal arrives, the terminal-time pause check no longer sees it,
        // so a reconciliation pass would otherwise finalize a seen-set
        // missing this whole window and delete the still-valid rows it never
        // observed. Mark the in-flight scan non-authoritative instead.
        if (!droppedBeforeScanAttemptStarted && IsPausedLocked(driveLetter)) {
            it->second.scanBatchFailed = true;
            droppedWhilePaused = true;
        }
    }
    if (droppedBeforeScanAttemptStarted || droppedWhilePaused) {
        AppendManagerDiagnostic(ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                ffprotocol::DiagnosticCode::FFDIAG_STALE_SCAN_TERMINAL,
                                {}, std::wstring(1, driveLetter),
                                droppedBeforeScanAttemptStarted
                                    ? L"scan-batch-before-scan-attempt-started"
                                    : L"scan-batch-dropped-paused-non-authoritative",
                                0, 0);
        return;
    }

    if (!apply(durableId)) {
        // task 3.4: leave the cursor untouched so the next batch attempt (or a full restart) can retry.
        // index-durability-false-success-closure D2 (PA-003): the service
        // keeps streaming later batches past a batch the store rejected,
        // and it will still report ScanComplete at the end. Mark this
        // scan's completion non-authoritative so that terminal can never
        // mark the volume indexed over the gap left at the frozen cursor.
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessionsByEphemeralId_.find(volumeId.value);
        if (it != sessionsByEphemeralId_.end() && it->second.durableId == durableId) {
            it->second.scanBatchFailed = true;
        }
        AppendManagerDiagnostic(ffprotocol::DiagnosticCategory::IndexingError,
                                ffprotocol::DiagnosticCode::FFDIAG_SCAN_BATCH_APPLY_FAILED,
                                {}, std::wstring(1, driveLetter),
                                L"scan-batch-apply-failed-cursor-frozen", 0,
                                static_cast<uint64_t>(cursor.size()));
        return;
    }
    pipeline_.SetScanCursor(durableId, cursor);
    RepublishSnapshot(durableId, driveLetter);
}

void VolumeSessionManager::OnScanComplete(ffprotocol::VolumeId volumeId) {
    ffindexstore::VolumeRowId durableId = 0;
    wchar_t driveLetter = L'\0';
    bool anyScanInProgress = false;
    bool wasReconciliationScan = false;
    bool batchFailed = false;
    bool deferred = false;
    bool paused = false;
    bool waitingForScanAttemptStarted = false;
    bool retryOwnerChanged = false;
    {
        // retry-ownership-and-focus-escape-closure D2: closing the attempt and installing its next retry owner are one
        // transition under the lock pair.
        std::scoped_lock lock(mutex_, wakeMutex_);
        auto it = sessionsByEphemeralId_.find(volumeId.value);
        if (it == sessionsByEphemeralId_.end()) {
            return;
        }
        durableId = it->second.durableId;
        driveLetter = it->second.driveLetter;
        if (it->second.awaitingScanAttemptStarted) {
            // scan-generation-integrity-and-residual-certification-closure
            // 3.4: all frames before ScanAttemptStarted are still attributable
            // to the superseded worker. The barrier is the first frame that
            // may grant the replacement's volume-only terminal authority.
            waitingForScanAttemptStarted = true;
        } else if (!it->second.scanInProgress) {
            // index-durability-false-success-closure D2: a completion for
            // a volume with no scan in flight (duplicate, or one that
            // raced pause/teardown bookkeeping) has no authority over the
            // index at all.
            AppendManagerDiagnostic(ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                    ffprotocol::DiagnosticCode::FFDIAG_STALE_SCAN_TERMINAL,
                                    {}, std::wstring(1, it->second.driveLetter),
                                    L"scan-complete-without-scan-in-progress", 0, 0);
            return;
        } else {
            wasReconciliationScan = it->second.reconciliationScan;
            batchFailed = it->second.scanBatchFailed;
            deferred = it->second.deferredReconciliation;
            paused = IsPausedLocked(driveLetter);
            it->second.scanInProgress = false;
            it->second.reconciliationScan = false;
            it->second.scanBatchFailed = false;
            it->second.deferredReconciliation = false;
            anyScanInProgress = std::any_of(
                sessionsByEphemeralId_.begin(), sessionsByEphemeralId_.end(),
                [](const auto& entry) { return entry.second.scanInProgress; });
            if (batchFailed) {
                // scan-generation-integrity-and-residual-certification-closure
                // D2/D4: coverage that could not be authorized is retried from
                // the last durably applied position (or as its own
                // reconciliation) under the same critical section that closed
                // the attempt, so a disconnect re-entering from the publication
                // below cannot observe the intent unowned.
                InstallNonAuthoritativeRetryLocked(volumeId.value, durableId,
                                                   std::chrono::steady_clock::now() + kScanFailureRetryDelay,
                                                   wasReconciliationScan);
                retryOwnerChanged = true;
            }
        }
    }
    if (retryOwnerChanged) {
        wakeCv_.notify_one();
    }

    if (waitingForScanAttemptStarted) {
        AppendManagerDiagnostic(ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                ffprotocol::DiagnosticCode::FFDIAG_STALE_SCAN_TERMINAL,
                                {}, std::wstring(1, driveLetter),
                                L"scan-complete-before-scan-attempt-started", 0, 0);
        return;
    }
    pipeline_.SetBulkScanActive(anyScanInProgress);

    if (paused || batchFailed) {
        // index-durability-false-success-closure D2/D5 (PA-001/PA-005):
        // ScanComplete is only authority when the scan actually ran to the
        // end of the MFT with every batch applied. A completion received
        // while paused (batches were dropped) or after any batch failed to
        // apply must not mark the volume indexed or finalize a reconciliation
        // pass on a seen-set with guaranteed holes -- that finalization is exactly
        // what deletes live rows.
        if (pipeline_.IsReconciliationPassActive(durableId)) {
            pipeline_.AbortReconciliationPass(durableId);
        }
        if (deferred) {
            // The pending reconciliation request is still owed -- the scan
            // that was supposed to satisfy it never became authoritative.
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = sessionsByEphemeralId_.find(volumeId.value);
            if (it != sessionsByEphemeralId_.end() && it->second.durableId == durableId) {
                it->second.deferredReconciliation = true;
            }
        }
        AppendManagerDiagnostic(ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                ffprotocol::DiagnosticCode::FFDIAG_STALE_SCAN_TERMINAL,
                                {}, std::wstring(1, driveLetter),
                                paused ? L"scan-complete-non-authoritative-paused"
                                       : L"scan-complete-non-authoritative-batch-failed",
                                0, 0);
        RepublishSnapshot(durableId, driveLetter, true);
        return;
    }

    pipeline_.MarkScanComplete(durableId);
    // D2/D4: this attempt's coverage is authoritative, so every retry owner
    // that was armed to repair an earlier hole or retirement is no longer
    // owed (the intent is satisfied).
    {
        std::scoped_lock lock(mutex_, wakeMutex_);
        ClearRetryOwnerLocked(durableId, volumeId.value);
    }
    if (wasReconciliationScan) {
        if (pipeline_.IsReconciliationPassActive(durableId)) {
            pipeline_.FinishReconciliationPass(durableId);
            pipeline_.SetLastReconciliationTime(durableId, NowAsFileTime());
        }
    } else if (pipeline_.IsReconciliationPassActive(durableId)) {
        // D4's invariant (a pass exists only while its own scan runs,
        // every terminal clears it) makes this unreachable today; defend
        // anyway -- a partial scan must never finalize a pass it does not
        // own. Abort, and let the next scheduler sweep redo it.
        AppendManagerDiagnostic(ffprotocol::DiagnosticCategory::IndexingError,
                                ffprotocol::DiagnosticCode::FFDIAG_STALE_SCAN_TERMINAL,
                                {}, std::wstring(1, driveLetter),
                                L"reconciliation-pass-not-owned-by-completing-scan", 0, 0);
        pipeline_.AbortReconciliationPass(durableId);
    }
    if (deferred) {
        // Reconciliation requested while this scan ran (e.g. the journal
        // resume position proved invalid mid-scan -- PA-004). Now the
        // volume's scan authority is free and the initial scan is
        // complete, so it runs cleanly instead of having raced the scan.
        TriggerReconciliation(volumeId, durableId);
    }
    RepublishSnapshot(durableId, driveLetter, true);
}

void VolumeSessionManager::OnScanFailed(ffprotocol::VolumeId volumeId) {
    // index-durability-false-success-closure D1/D2: the service's explicit
    // terminal failure. Nothing this scan covered is authoritative: the
    // volume keeps its previous scanComplete state, an in-flight
    // reconciliation pass is discarded rather than finalized (a partial
    // seen-set must never drive deletions), and the reconciliation cadence
    // clock is untouched so the failure is retried, not papered over.
    ffindexstore::VolumeRowId durableId = 0;
    wchar_t driveLetter = L'\0';
    bool anyScanInProgress = false;
    bool wasReconciliationScan = false;
    bool waitingForScanAttemptStarted = false;
    bool retryOwnerChanged = false;
    {
        // retry-ownership-and-focus-escape-closure D2: closing the attempt and installing its next owner are one
        // transition under the lock pair. RepublishSnapshot below re-enters the
        // manager through the publication callback, so the owner must already
        // exist before the locks are released.
        std::scoped_lock lock(mutex_, wakeMutex_);
        auto it = sessionsByEphemeralId_.find(volumeId.value);
        if (it == sessionsByEphemeralId_.end()) {
            return;
        }
        durableId = it->second.durableId;
        driveLetter = it->second.driveLetter;
        if (it->second.awaitingScanAttemptStarted) {
            // scan-generation-integrity-and-residual-certification-closure
            // 3.4: this failure can still belong to the worker that was
            // superseded. Preserve the provisional replacement until the
            // service's ordered start barrier arrives.
            waitingForScanAttemptStarted = true;
        } else if (!it->second.scanInProgress) {
            AppendManagerDiagnostic(ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                    ffprotocol::DiagnosticCode::FFDIAG_STALE_SCAN_TERMINAL,
                                    {}, std::wstring(1, it->second.driveLetter),
                                    L"scan-failed-without-scan-in-progress", 0, 0);
            return;
        } else {
            wasReconciliationScan = it->second.reconciliationScan;
            it->second.scanInProgress = false;
            it->second.reconciliationScan = false;
            it->second.scanBatchFailed = false;
            anyScanInProgress = std::any_of(
                sessionsByEphemeralId_.begin(), sessionsByEphemeralId_.end(),
                [](const auto& entry) { return entry.second.scanInProgress; });
            InstallNonAuthoritativeRetryLocked(volumeId.value, durableId,
                                               std::chrono::steady_clock::now() + kScanFailureRetryDelay,
                                               wasReconciliationScan);
            retryOwnerChanged = true;
        }
    }
    if (retryOwnerChanged) {
        wakeCv_.notify_one();
    }

    if (waitingForScanAttemptStarted) {
        AppendManagerDiagnostic(ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                ffprotocol::DiagnosticCode::FFDIAG_STALE_SCAN_TERMINAL,
                                {}, std::wstring(1, driveLetter),
                                L"scan-failed-before-scan-attempt-started", 0, 0);
        return;
    }
    pipeline_.SetBulkScanActive(anyScanInProgress);
    if (pipeline_.IsReconciliationPassActive(durableId)) {
        pipeline_.AbortReconciliationPass(durableId);
    }
    AppendManagerDiagnostic(ffprotocol::DiagnosticCategory::IndexingError,
                            ffprotocol::DiagnosticCode::FFDIAG_SCAN_FAILED,
                            {}, std::wstring(1, driveLetter),
                            L"service-reported-scan-failure", 0, 0);
    RepublishSnapshot(durableId, driveLetter, true);
}

void VolumeSessionManager::OnScanAttemptStarted(ffprotocol::VolumeId volumeId) {
    wchar_t driveLetter = L'\0';
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessionsByEphemeralId_.find(volumeId.value);
        if (it == sessionsByEphemeralId_.end()
            || !it->second.scanInProgress
            || !it->second.awaitingScanAttemptStarted) {
            // Initial scans also receive the marker at {2,3}; only a
            // coverage-hole replacement has provisional authority to release.
            return;
        }
        driveLetter = it->second.driveLetter;
        it->second.awaitingScanAttemptStarted = false;
        // The replacement starts from the frozen durable cursor, so the old
        // attempt's coverage hole no longer poisons frames ordered after this
        // service-side stop-and-join barrier.
        it->second.scanBatchFailed = false;
    }
    AppendManagerDiagnostic(ffprotocol::DiagnosticCategory::VolumeStateTransition,
                            ffprotocol::DiagnosticCode::FFDIAG_STALE_SCAN_TERMINAL,
                            {}, std::wstring(1, driveLetter),
                            L"scan-attempt-started-barrier-accepted", 0, 0);
}

void VolumeSessionManager::OnJournalOpened(ffprotocol::VolumeId volumeId, uint64_t journalId, uint64_t currentUsn) {
    ffindexstore::VolumeRowId durableId = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessionsByEphemeralId_.find(volumeId.value);
        if (it == sessionsByEphemeralId_.end()) {
            return;
        }
        it->second.journalId = journalId;
        it->second.journalIdKnown = true;
        it->second.journalOpenRequested = true;
        durableId = it->second.durableId;
    }

    auto meta = pipeline_.GetVolumeMetadata(durableId);
    if (meta && meta->journalId.has_value() && *meta->journalId != journalId) {
        // design.md D6: the journal was deleted/recreated since we last
        // held a position for it -- the ResumeUsn we just requested is
        // almost certainly invalid for this JournalId, so reset to the
        // journal's current position and reconcile instead of silently
        // missing whatever changed in the gap.
        pipeline_.SetJournalPosition(durableId, journalId, currentUsn);
        TriggerReconciliation(volumeId, durableId);
    } else {
        pipeline_.SetJournalPosition(durableId, journalId, meta ? meta->resumeUsn : currentUsn);
    }
}

void VolumeSessionManager::OnJournalBatch(
    ffprotocol::VolumeId volumeId, uint64_t latestUsn, std::vector<ffprotocol::UsnDeltaV2> records) {
    ApplyJournalBatch(volumeId, latestUsn, [&](ffindexstore::VolumeRowId durableId) {
        return pipeline_.ApplyUsnBatch(durableId, records);
    });
}

void VolumeSessionManager::OnJournalBatchV3(
    ffprotocol::VolumeId volumeId, uint64_t latestUsn, std::vector<ffprotocol::UsnDeltaV3> records) {
    // allocated-size-storage-accuracy 5.2: identical lifecycle to the V2
    // path; only the record type (and thus allocated-size presence) differs.
    ApplyJournalBatch(volumeId, latestUsn, [&](ffindexstore::VolumeRowId durableId) {
        return pipeline_.ApplyUsnBatch(durableId, records);
    });
}

void VolumeSessionManager::ApplyJournalBatch(ffprotocol::VolumeId volumeId, uint64_t latestUsn,
                                             const std::function<bool(ffindexstore::VolumeRowId)>& apply) {
    ffindexstore::VolumeRowId durableId = 0;
    wchar_t driveLetter = L'\0';
    uint64_t journalId = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessionsByEphemeralId_.find(volumeId.value);
        if (it == sessionsByEphemeralId_.end() || !it->second.journalIdKnown) {
            return;
        }
        durableId = it->second.durableId;
        driveLetter = it->second.driveLetter;
        journalId = it->second.journalId;
        // settings-and-appearance §9.1: same drop-without-advancing rule
        // as scan batches; the stored ResumeUsn is the resume point.
        if (IsPausedLocked(driveLetter)) {
            return;
        }
    }

    if (!apply(durableId)) {
        // index-durability-false-success-closure D3/4.4 (PA-002 gap
        // closure): an un-applied journal batch is a coverage hole the
        // position advance must not paper over. The stored ResumeUsn stays
        // behind (monotonic replay is safe -- re-applied events upsert),
        // and a reconciliation sweep is requested to close the gap; with
        // D4's deferral this is idempotent while any scan is in flight.
        AppendManagerDiagnostic(ffprotocol::DiagnosticCategory::IndexingError,
                                ffprotocol::DiagnosticCode::FFDIAG_SCAN_BATCH_APPLY_FAILED,
                                {}, std::wstring(1, driveLetter),
                                L"journal-batch-apply-failed-reconciliation-requested", 0, 0);
        TriggerReconciliation(volumeId, durableId);
        return;
    }
    pipeline_.SetJournalPosition(durableId, journalId, latestUsn);
    RepublishSnapshot(durableId, driveLetter);
}

void VolumeSessionManager::OnJournalResumeInvalid(ffprotocol::VolumeId volumeId) {
    ffindexstore::VolumeRowId durableId = 0;
    uint64_t journalId = 0;
    bool journalIdKnown = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessionsByEphemeralId_.find(volumeId.value);
        if (it == sessionsByEphemeralId_.end()) {
            return;
        }
        durableId = it->second.durableId;
        journalId = it->second.journalId;
        journalIdKnown = it->second.journalIdKnown;
    }

    // design.md D6, tasks.md 7.6: the persisted ResumeUsn falls outside
    // the journal's retained range (the journal wrapped during a long
    // disconnection). Same policy as OnJournalOpened's JournalId-mismatch
    // branch: stop trusting the persisted position and reconcile instead
    // of silently missing the changes in the gap. JournalResumeInvalid
    // carries no currentUsn, so the position resets to the journal's
    // start and the journal is reopened from there using the existing
    // OpenUsnJournal request -- the batches from that reopened journal
    // plus the reconciliation pass's full scan cover the gap.
    if (journalIdKnown) {
        pipeline_.SetJournalPosition(durableId, journalId, 0);
    }
    TriggerReconciliation(volumeId, durableId);
    RequestJournalOpen(volumeId, 0, durableId);
}

void VolumeSessionManager::RepublishSnapshot(ffindexstore::VolumeRowId durableId, wchar_t driveLetter, bool force) {
    if (!onSnapshotReady_) {
        return;
    }
    constexpr auto kSnapshotPublishInterval = std::chrono::milliseconds(500);
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool scanInProgress = std::any_of(
            sessionsByEphemeralId_.begin(), sessionsByEphemeralId_.end(),
            [durableId](const auto& entry) {
                return entry.second.durableId == durableId && entry.second.scanInProgress;
            });
        if (!force && scanInProgress) {
            return;
        }
        const auto previous = lastSnapshotPublish_.find(durableId);
        if (!force && previous != lastSnapshotPublish_.end()
            && now - previous->second < kSnapshotPublishInterval) {
            return;
        }
        lastSnapshotPublish_[durableId] = now;
    }
    std::wstring prefix;
    prefix.push_back(driveLetter);
    prefix.push_back(L':');
    // memory-bounded-indexed-scale 5.1: bulk scan-complete publication exports
    // the bounded top-of-tree working set, not the complete volume map --
    // published bytes scale with active browse contexts; deeper directories
    // re-materialize through the normal request path.
    onSnapshotReady_(durableId, pipeline_.ExportDirectorySnapshotBounded(
                                    durableId, prefix, IndexPipeline::kInitialIndexedExportDirectories));
}

void VolumeSessionManager::RefreshProjectionScanState() {
    bool anyScanInProgress = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        anyScanInProgress = std::any_of(
            sessionsByEphemeralId_.begin(), sessionsByEphemeralId_.end(),
            [](const auto& entry) { return entry.second.scanInProgress; });
    }
    pipeline_.SetBulkScanActive(anyScanInProgress);
}

std::vector<ffprotocol::VolumeStatusRecord> VolumeSessionManager::CollectVolumeStatus() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ffprotocol::VolumeStatusRecord> records;
    records.reserve(configuredVolumes_.size());
    for (const auto& volume : configuredVolumes_) {
        if (volume.key.empty() || !volume.enabled) {
            continue; // disabled volumes are not reported; the UI derives the disabled annotation itself
        }
        const wchar_t letter = static_cast<wchar_t>(towupper(volume.key.front()));
        if (letter < L'A' || letter > L'Z') {
            continue;
        }

        ffprotocol::VolumeStatusRecord record{};
        record.driveLetter = static_cast<uint8_t>(letter);

        const auto sessionIt = std::find_if(sessionsByEphemeralId_.begin(), sessionsByEphemeralId_.end(),
            [letter](const auto& entry) { return static_cast<wchar_t>(towupper(entry.second.driveLetter)) == letter; });
        if (sessionIt == sessionsByEphemeralId_.end()) {
            // Configured but absent from the current volume list: either
            // the volume is unreachable right now or the engine is not
            // connected to the privileged path. Both read as
            // "Unavailable" once the UI folds in the connection state.
            records.push_back(record);
            continue;
        }

        record.flags |= ffprotocol::VolumeStatusReachable;
        const auto meta = pipeline_.GetVolumeMetadata(sessionIt->second.durableId);
        const bool reconciliationActive = pipeline_.IsReconciliationPassActive(sessionIt->second.durableId);
        if ((meta && !meta->scanComplete) || reconciliationActive) {
            record.flags |= ffprotocol::VolumeStatusScanning;
        }
        if (reconciliationActive) {
            record.flags |= ffprotocol::VolumeStatusNeedsReconciliation;
        }
        // settings-and-appearance §9.1: the pause state is read back
        // through this same report (D9: status converges through D7's
        // derivation, no parallel outcome state).
        if (IsPausedLocked(letter)) {
            record.flags |= ffprotocol::VolumeStatusPaused;
        }
        records.push_back(record);
    }

    // settings-and-appearance §9.3: observed-but-unselected volumes are
    // pending a user decision -- derived from the last EnumerateVolumes
    // poll against the persisted selection, never a tracked list.
    for (const wchar_t letter : observedLetters_) {
        if (letter < L'A' || letter > L'Z') {
            continue;
        }
        const auto configured = std::find_if(configuredVolumes_.begin(), configuredVolumes_.end(), [letter](const auto& volume) {
            return !volume.key.empty() && static_cast<wchar_t>(towupper(volume.key.front())) == letter;
        });
        if (configured != configuredVolumes_.end()) {
            continue; // selected (enabled or disabled) -- not pending
        }
        ffprotocol::VolumeStatusRecord record{};
        record.driveLetter = static_cast<uint8_t>(letter);
        record.flags = ffprotocol::VolumeStatusReachable | ffprotocol::VolumeStatusPendingDecision;
        records.push_back(record);
    }
    return records;
}

void VolumeSessionManager::ReconciliationSchedulerLoop() {
    while (running_.load()) {
        {
            std::unique_lock<std::mutex> lock(wakeMutex_);
            const auto now = std::chrono::steady_clock::now();
            auto nextWake = now + kSchedulerPollInterval;
            for (const auto& [ephemeralValue, retry] : failedScanRetries_) {
                (void)ephemeralValue;
                if (retry.due < nextWake) nextWake = retry.due;
            }
            const uint64_t generation = scanRetryGeneration_;
            wakeCv_.wait_until(lock, nextWake, [this, generation] {
                if (!running_.load() || scanRetryGeneration_ != generation) return true;
                const auto current = std::chrono::steady_clock::now();
                for (const auto& [ephemeralValue, retry] : failedScanRetries_) {
                    (void)ephemeralValue;
                    if (retry.due <= current) return true;
                }
                return false;
            });
        }
        if (!running_.load()) {
            break;
        }

        // tasks.md 1.6: scheduled WAL checkpointing rides this periodic
        // poll -- a cheap passive checkpoint every pass, escalating to a
        // forced checkpoint once the WAL grows past the threshold
        // (design.md "Risks": unbounded WAL growth). Runs regardless of
        // the privileged connection's state: it's purely local
        // maintenance of the engine's own database file.
        pipeline_.RunStoreMaintenance();

        // tasks.md 8.5: never schedule privileged-path reconciliation
        // while degraded -- there is no connection to send it over, and
        // no ephemeral VolumeIds to address volumes with even if there
        // were. Keep failed-scan retries queued without spinning while the
        // connection is unavailable; reconnect will re-derive the session.
        if (!active_.load()) {
            std::lock_guard<std::mutex> wakeLock(wakeMutex_);
            const auto retryAt = std::chrono::steady_clock::now() + kScanFailureRetryDelay;
            for (auto& [ephemeralValue, retry] : failedScanRetries_) {
                (void)ephemeralValue;
                if (retry.due <= retryAt) retry.due = retryAt;
            }
            continue;
        }

        struct DueRetry {
            ffprotocol::VolumeId ephemeralId;
            ffindexstore::VolumeRowId durableId = 0;
            bool reconciliation = false;
        };
        std::vector<DueRetry> failedRetries;
        {
            // retry-ownership-and-focus-escape-closure D2: moving a due retry out of the queue and into the visible
            // in-hand dispatch owner is one transition under the lock pair, so
            // a disconnect in the D3 seam below can transfer it.
            std::scoped_lock lock(mutex_, wakeMutex_);
            const auto now = std::chrono::steady_clock::now();
            for (auto it = failedScanRetries_.begin(); it != failedScanRetries_.end();) {
                if (it->second.due > now) {
                    ++it;
                    continue;
                }
                const auto session = sessionsByEphemeralId_.find(it->first);
                if (session == sessionsByEphemeralId_.end()) {
                    // Every transition that erases a session transfers its
                    // retry first; leave an unkeyable entry queued rather than
                    // dispatching it against a dead ephemeral id.
                    ++it;
                    continue;
                }
                schedulerDispatchRetries_[session->second.durableId] = it->second;
                failedRetries.push_back(DueRetry{ffprotocol::VolumeId{it->first}, session->second.durableId,
                                                 it->second.reconciliation});
                it = failedScanRetries_.erase(it);
            }
        }
        // retry-ownership-and-focus-escape-closure D3: a due retry has left its queue; the dispatch seam runs with no
        // manager lock held, before the retry is adopted.
        if (scanRetryDispatchSeam_) {
            scanRetryDispatchSeam_();
        }
        for (const auto& retry : failedRetries) {
            VolumeSession session;
            bool sessionAlive = false;
            bool retryReady = false;
            {
                std::scoped_lock lock(mutex_, wakeMutex_);
                const auto dispatch = schedulerDispatchRetries_.find(retry.durableId);
                if (dispatch == schedulerDispatchRetries_.end()) {
                    continue; // disconnect/disappearance already transferred it
                }
                const auto it = sessionsByEphemeralId_.find(retry.ephemeralId.value);
                if (it == sessionsByEphemeralId_.end() || it->second.durableId != retry.durableId) {
                    // The session vanished without a transfer (defensive):
                    // keep the intent on the durable floor instead of dropping
                    // it with the dead ephemeral id.
                    MergeDurableFloorLocked(retry.durableId, dispatch->second);
                    schedulerDispatchRetries_.erase(dispatch);
                    continue;
                }
                sessionAlive = true;
                if (!it->second.scanInProgress && !IsPausedLocked(it->second.driveLetter)) {
                    session = it->second;
                    retryReady = true;
                }
            }
            if (!sessionAlive) {
                continue;
            }
            if (!retryReady) {
                // Keep one non-issuing owner at the five-second cadence so a
                // paused or busy volume's intent survives.
                bool requeued = false;
                {
                    std::scoped_lock lock(mutex_, wakeMutex_);
                    const auto dispatch = schedulerDispatchRetries_.find(retry.durableId);
                    if (dispatch != schedulerDispatchRetries_.end()) {
                        failedScanRetries_[retry.ephemeralId.value] =
                            {std::chrono::steady_clock::now() + kScanFailureRetryDelay,
                             dispatch->second.reconciliation};
                        ++scanRetryGeneration_;
                        schedulerDispatchRetries_.erase(dispatch);
                        requeued = true;
                    }
                }
                if (requeued) {
                    wakeCv_.notify_one();
                }
                continue;
            }
            const bool issued = retry.reconciliation
                ? TriggerReconciliation(retry.ephemeralId, session.durableId)
                : StartOrResumeVolume(retry.ephemeralId, session);
            bool keptOwner = false;
            {
                std::scoped_lock lock(mutex_, wakeMutex_);
                const auto dispatch = schedulerDispatchRetries_.find(retry.durableId);
                if (dispatch != schedulerDispatchRetries_.end()) {
                    const auto it = sessionsByEphemeralId_.find(retry.ephemeralId.value);
                    const bool sameSession =
                        it != sessionsByEphemeralId_.end() && it->second.durableId == retry.durableId;
                    if (sameSession && it->second.scanInProgress) {
                        // The admission claimed the in-flight attempt; it is
                        // the owner now (defensive cleanup only).
                        schedulerDispatchRetries_.erase(dispatch);
                    } else if (sameSession) {
                        failedScanRetries_[retry.ephemeralId.value] =
                            {std::chrono::steady_clock::now() + kScanFailureRetryDelay,
                             dispatch->second.reconciliation};
                        ++scanRetryGeneration_;
                        schedulerDispatchRetries_.erase(dispatch);
                        keptOwner = true;
                    } else {
                        MergeDurableFloorLocked(retry.durableId, dispatch->second);
                        schedulerDispatchRetries_.erase(dispatch);
                    }
                }
            }
            if (keptOwner) {
                wakeCv_.notify_one();
            }
            if (!issued && !keptOwner && retry.reconciliation) {
                pipeline_.AbortReconciliationPass(retry.durableId);
            }
        }

        std::vector<std::pair<ffprotocol::VolumeId, ffindexstore::VolumeRowId>> due;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const uint64_t now = NowAsFileTime();
            for (const auto& [ephemeralValue, session] : sessionsByEphemeralId_) {
                auto meta = pipeline_.GetVolumeMetadata(session.durableId);
                if (!meta || !meta->available || !meta->scanComplete) {
                    continue; // an in-progress initial scan already covers the same ground
                }
                if (IsPausedLocked(session.driveLetter)) {
                    continue; // settings-and-appearance §9.1: no reconciliation while paused
                }
                if (now - meta->lastReconciliationTime >= kReconciliationIntervalFileTime) {
                    due.emplace_back(ffprotocol::VolumeId{ephemeralValue}, session.durableId);
                }
            }
        }
        for (const auto& [ephemeralId, durableId] : due) {
            TriggerReconciliation(ephemeralId, durableId);
        }
    }
}

} // namespace ffengine
