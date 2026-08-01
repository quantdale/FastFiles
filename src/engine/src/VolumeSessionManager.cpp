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
    {
        std::lock_guard<std::mutex> lock(mutex_);
        configuredVolumes_ = std::move(volumes);
    }
    // Re-enumeration makes newly enabled volumes enter the normal session
    // start path without waiting for an engine restart.
    if (active_.load()) {
        connection_.SendRequest(static_cast<uint16_t>(ffprotocol::MessageType::EnumerateVolumes));
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

    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<uint32_t> seenEphemeral;
        seenEphemeral.reserve(volumes.size());

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
                it = sessionsByEphemeralId_.erase(it);
            } else {
                ++it;
            }
        }
    }

    for (const auto& [ephemeralId, session] : toStart) {
        StartOrResumeVolume(ephemeralId, session);
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
