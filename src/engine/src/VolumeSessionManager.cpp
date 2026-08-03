#include "VolumeSessionManager.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "VolumeIdentity.h"

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

// Default reconciliation cadence (design.md "Open Questions": cadence is
// left as an implementation/tuning detail, not fixed by the spec). Not
// yet user-configurable -- a reasonable fixed default for now.
constexpr uint64_t kReconciliationIntervalFileTime = 6ULL * 3600 * 10'000'000; // 6 hours
constexpr std::chrono::minutes kSchedulerPollInterval{10};

std::vector<uint8_t> BuildStartScanPayload(ffprotocol::ProtocolVersion version, ffprotocol::VolumeId volumeId,
                                           const std::vector<uint8_t>& cursor, uint16_t flags) {
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

void VolumeSessionManager::ReloadConfiguration(std::vector<ffprotocol::VolumeSetting> volumes) {
    std::vector<std::pair<ffprotocol::VolumeId, VolumeSession>> toStop;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        configuredVolumes_ = std::move(volumes);
        // settings-and-appearance §9.2: a volume that was disabled (or
        // removed from the persisted selection) while a session was live
        // is torn down immediately -- no waiting for an engine restart.
        // The volume stays marked available (disabling is not
        // unavailability); only the session, its privileged-path work,
        // and its published directories go away.
        for (auto it = sessionsByEphemeralId_.begin(); it != sessionsByEphemeralId_.end();) {
            const wchar_t letter = static_cast<wchar_t>(towupper(it->second.driveLetter));
            const auto configured = std::find_if(configuredVolumes_.begin(), configuredVolumes_.end(), [letter](const auto& volume) {
                return !volume.key.empty() && static_cast<wchar_t>(towupper(volume.key.front())) == letter;
            });
            if (configured != configuredVolumes_.end() && configured->enabled) {
                ++it;
                continue;
            }
            toStop.emplace_back(ffprotocol::VolumeId{it->first}, it->second);
            it = sessionsByEphemeralId_.erase(it);
        }
    }
    for (const auto& [ephemeralId, session] : toStop) {
        if (active_.load()) {
            const ffprotocol::StopVolumeScanRequest stop{ephemeralId};
            connection_.SendRequest(static_cast<uint16_t>(ffprotocol::MessageType::StopVolumeScan), &stop, sizeof(stop));
            const ffprotocol::CloseUsnJournalRequest close{ephemeralId};
            connection_.SendRequest(static_cast<uint16_t>(ffprotocol::MessageType::CloseUsnJournal), &close, sizeof(close));
        }
        // The unavailable callback's contract is "withdraw this drive's
        // published directories" -- which is exactly what disabling needs.
        if (onVolumeUnavailable_) {
            onVolumeUnavailable_(session.durableId, session.driveLetter);
        }
    }
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
    connection_.SetScanCompleteCallback([this](ffprotocol::VolumeId id) { OnScanComplete(id); });
    connection_.SetJournalOpenedCallback(
        [this](ffprotocol::VolumeId id, uint64_t journalId, uint64_t currentUsn) { OnJournalOpened(id, journalId, currentUsn); });
    connection_.SetJournalBatchCallback([this](ffprotocol::VolumeId id, uint64_t latestUsn,
                                                std::vector<ffprotocol::UsnDeltaV1> records) {
        OnJournalBatch(id, latestUsn, std::move(records));
    });
    connection_.SetJournalResumeInvalidCallback([this](ffprotocol::VolumeId id) { OnJournalResumeInvalid(id); });

    running_ = true;
    reconciliationThread_ = std::thread(&VolumeSessionManager::ReconciliationSchedulerLoop, this);
}

void VolumeSessionManager::Stop() {
    if (!running_.exchange(false)) {
        return;
    }
    wakeCv_.notify_all();
    if (reconciliationThread_.joinable()) {
        reconciliationThread_.join();
    }
}

void VolumeSessionManager::OnConnectionStateChanged(ConnectionState state) {
    if (state != ConnectionState::Active) {
        active_ = false;
        // Ephemeral VolumeIds are only valid for the lifetime of one
        // connection -- clearing this on every non-Active transition
        // means the next Active period starts clean and re-derives
        // durable identity from a fresh EnumerateVolumes (task 7.1).
        std::lock_guard<std::mutex> lock(mutex_);
        sessionsByEphemeralId_.clear();
        return;
    }
    const bool wasActive = active_.exchange(true);
    if (!wasActive) {
        connection_.SendRequest(static_cast<uint16_t>(ffprotocol::MessageType::EnumerateVolumes));
    }
}

void VolumeSessionManager::OnVolumeList(std::vector<ffprotocol::VolumeInfo> volumes) {
    std::vector<std::pair<ffprotocol::VolumeId, VolumeSession>> toStart;
    std::vector<VolumeSession> unavailable;

    {
        std::lock_guard<std::mutex> lock(mutex_);
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
                unavailable.push_back(it->second);
                it = sessionsByEphemeralId_.erase(it);
            } else {
                ++it;
            }
        }
    }

    for (const auto& [ephemeralId, session] : toStart) {
        StartOrResumeVolume(ephemeralId, session);
    }
    for (const auto& session : unavailable) {
        if (onVolumeUnavailable_) {
            onVolumeUnavailable_(session.durableId, session.driveLetter);
        }
    }
}

void VolumeSessionManager::StartOrResumeVolume(ffprotocol::VolumeId ephemeralId, const VolumeSession& session) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::wstring key(1, session.driveLetter);
        const auto configured = std::find_if(configuredVolumes_.begin(), configuredVolumes_.end(), [&key](const auto& volume) {
            return !volume.key.empty() && towupper(volume.key.front()) == key.front();
        });
        if (configured != configuredVolumes_.end() && !configured->enabled) return;
        // settings-and-appearance §9.1: paused volumes are never (re)started.
        if (IsPausedLocked(session.driveLetter)) return;
    }
    auto meta = pipeline_.GetVolumeMetadata(session.durableId);

    // tasks.md 6.3/D8: only (re)start the initial scan if it never
    // finished; once complete, the journal plus periodic reconciliation
    // is what keeps the volume current -- reconnecting never triggers an
    // unconditional full rescan (spec "Volume Reconnection Triggers
    // Resume-or-Reconcile, Not Blind Rescan").
    if (!meta || !meta->scanComplete) {
        std::vector<uint8_t> cursor = meta ? meta->scanCursor : std::vector<uint8_t>{};
        auto payload = BuildStartScanPayload(connection_.NegotiatedVersion(), ephemeralId, cursor, 0);
        connection_.SendRequest(static_cast<uint16_t>(ffprotocol::MessageType::StartVolumeScan), payload.data(),
                                 static_cast<uint32_t>(payload.size()));
    }

    // tasks.md 7.5/7.6: always (re)request the journal from the persisted
    // position -- OnJournalOpened reconciles the JournalId mismatch case
    // once the service's ack reports the volume's *current* journal
    // identity, since that isn't known until the service replies.
    ffprotocol::OpenUsnJournalRequest request{ephemeralId, meta ? meta->resumeUsn : 0};
    connection_.SendRequest(static_cast<uint16_t>(ffprotocol::MessageType::OpenUsnJournal), &request, sizeof(request));
}

void VolumeSessionManager::TriggerReconciliation(ffprotocol::VolumeId ephemeralId, ffindexstore::VolumeRowId durableId) {
    pipeline_.BeginReconciliationPass(durableId);
    // Reconciliation is deliberately lower priority than an initial scan:
    // it is a correctness backstop and must not compete with foreground
    // indexing or interactive filesystem work (task 8.2).
    const auto payload = BuildStartScanPayload(connection_.NegotiatedVersion(), ephemeralId, {},
                                                ffprotocol::kStartVolumeScanLowPriority);
    connection_.SendRequest(static_cast<uint16_t>(ffprotocol::MessageType::StartVolumeScan), payload.data(),
                            static_cast<uint32_t>(payload.size()));
}

void VolumeSessionManager::OnScanBatch(
    ffprotocol::VolumeId volumeId, std::vector<uint8_t> cursor, std::vector<ffprotocol::MftRecordV1> records) {
    ffindexstore::VolumeRowId durableId = 0;
    wchar_t driveLetter = L'\0';
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessionsByEphemeralId_.find(volumeId.value);
        if (it == sessionsByEphemeralId_.end()) {
            return;
        }
        durableId = it->second.durableId;
        driveLetter = it->second.driveLetter;
        // settings-and-appearance §9.1: while paused, batches are dropped
        // without applying and without advancing the stored cursor, so
        // resume re-issues the scan from the last-applied position.
        if (IsPausedLocked(driveLetter)) {
            return;
        }
    }

    if (!pipeline_.ApplyMftBatch(durableId, records)) {
        return; // task 3.4: leave the cursor untouched so the next batch attempt (or a full restart) can retry
    }
    pipeline_.SetScanCursor(durableId, cursor);
    RepublishSnapshot(durableId, driveLetter);
}

void VolumeSessionManager::OnScanComplete(ffprotocol::VolumeId volumeId) {
    ffindexstore::VolumeRowId durableId = 0;
    wchar_t driveLetter = L'\0';
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessionsByEphemeralId_.find(volumeId.value);
        if (it == sessionsByEphemeralId_.end()) {
            return;
        }
        durableId = it->second.durableId;
        driveLetter = it->second.driveLetter;
    }

    pipeline_.MarkScanComplete(durableId);
    if (pipeline_.IsReconciliationPassActive(durableId)) {
        pipeline_.FinishReconciliationPass(durableId);
        pipeline_.SetLastReconciliationTime(durableId, NowAsFileTime());
    }
    RepublishSnapshot(durableId, driveLetter);
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
    ffprotocol::VolumeId volumeId, uint64_t latestUsn, std::vector<ffprotocol::UsnDeltaV1> records) {
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

    if (!pipeline_.ApplyUsnBatch(durableId, records)) {
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
    ffprotocol::OpenUsnJournalRequest request{volumeId, 0};
    connection_.SendRequest(static_cast<uint16_t>(ffprotocol::MessageType::OpenUsnJournal), &request, sizeof(request));
}

void VolumeSessionManager::RepublishSnapshot(ffindexstore::VolumeRowId durableId, wchar_t driveLetter) {
    if (!onSnapshotReady_) {
        return;
    }
    std::wstring prefix;
    prefix.push_back(driveLetter);
    prefix.push_back(L':');
    onSnapshotReady_(durableId, pipeline_.ExportDirectorySnapshot(durableId, prefix));
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
            wakeCv_.wait_for(lock, kSchedulerPollInterval, [this] { return !running_.load(); });
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
        // were.
        if (!active_.load()) {
            continue;
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
