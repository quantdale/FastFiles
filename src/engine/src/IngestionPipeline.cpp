#include "IngestionPipeline.h"

#include <windows.h>
#include <winioctl.h> // USN_REASON_* constants

#include <cstring>

#include "ffprotocol/Commands.h"
#include "ffprotocol/Records.h"

#include "VolumeIdentityLookup.h"

namespace ffengine {

namespace {

using ffprotocol::MessageType;
using ffindexstore::DurableVolumeId;
using ffindexstore::EntryKey;
using ffindexstore::FileId128;
using ffindexstore::IngestEntry;
using ffindexstore::IngestOp;

// Reconciliation sweep cadence -- an explicit open question in design.md
// ("Exact reconciliation sweep cadence/pacing parameters ... are left as
// an implementation/tuning detail"); this is a conservative default that
// self-corrects missed events without meaningfully adding to steady-state
// I/O load.
constexpr std::chrono::milliseconds kReconciliationCheckInterval{30 * 60 * 1000};
constexpr std::chrono::milliseconds kReconciliationInterval{6ll * 60 * 60 * 1000};

uint64_t NowFileTime() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER value{};
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return value.QuadPart;
}

IngestEntry ToIngestEntry(DurableVolumeId volumeId, const ffprotocol::MftRecordV1& record) {
    IngestEntry entry;
    entry.op = IngestOp::Upsert;
    entry.key = EntryKey{volumeId, FileId128::FromNtfs(record.fixed.fileReferenceNumber)};
    entry.parentFileReferenceNumber = FileId128::FromNtfs(record.fixed.parentFileReferenceNumber);
    // Filename canonicalization (task 6.2): the raw parser already hands
    // back a clean UTF-16 name with no path separators; nothing further
    // to normalize, and identity below is FileReferenceNumber-keyed, never
    // re-derived from this name or any path.
    entry.name.assign(record.fileName.begin(), record.fileName.end());
    entry.sizeBytes = record.fixed.sizeBytes;
    entry.lastWriteTime = record.fixed.lastModifiedTime;
    entry.attributes = record.fixed.fileAttributes;
    return entry;
}

IngestEntry ToIngestEntry(DurableVolumeId volumeId, const ffprotocol::UsnDeltaV1& record) {
    IngestEntry entry;
    entry.key = EntryKey{volumeId, FileId128::FromNtfs(record.fixed.fileReferenceNumber)};
    entry.op = (record.fixed.reason & USN_REASON_FILE_DELETE) ? IngestOp::Remove : IngestOp::Upsert;
    entry.parentFileReferenceNumber = FileId128::FromNtfs(record.fixed.parentFileReferenceNumber);
    entry.name.assign(record.fileName.begin(), record.fileName.end());
    entry.sizeBytes = record.fixed.sizeBytes;
    entry.lastWriteTime = record.fixed.timestamp;
    entry.attributes = record.fixed.fileAttributes;
    return entry;
}

} // namespace

IngestionPipeline::~IngestionPipeline() {
    Stop();
}

void IngestionPipeline::Start(PrivilegedConnection& connection, ffindexstore::IndexStore& indexStore, std::function<void()> onIndexUpdated) {
    connection_ = &connection;
    indexStore_ = &indexStore;
    onIndexUpdated_ = std::move(onIndexUpdated);

    connection_->SetFrameHandler([this](uint16_t messageType, const std::vector<uint8_t>& payload) { HandleFrame(messageType, payload); });

    running_ = true;
    reconciliationThread_ = std::thread(&IngestionPipeline::ReconciliationLoop, this);
}

void IngestionPipeline::Stop() {
    if (!running_) {
        return;
    }
    running_ = false;
    reconciliationCv_.notify_all();
    if (reconciliationThread_.joinable()) {
        reconciliationThread_.join();
    }
}

void IngestionPipeline::OnConnectionStateChanged(ConnectionState state) {
    const bool nowActive = (state == ConnectionState::Active);
    const bool wasActive = privilegedActive_.exchange(nowActive);
    if (nowActive && !wasActive) {
        // Task 6.1 kickoff: learn which volumes exist before deciding
        // resume-vs-scan for each (task 7.1).
        connection_->SendCommand(static_cast<uint16_t>(MessageType::EnumerateVolumes));
    }
    // Task 8.5: no further action needed on the "became inactive" edge --
    // ReconciliationLoop itself checks privilegedActive_ before scheduling
    // any sweep, so nothing privileged-path-dependent is scheduled while
    // degraded, and normal scheduling simply resumes once Active again.
}

void IngestionPipeline::HandleFrame(uint16_t messageType, const std::vector<uint8_t>& payload) {
    switch (static_cast<MessageType>(messageType)) {
        case MessageType::VolumeList: HandleVolumeList(payload); break;
        case MessageType::ScanBatch: HandleScanBatch(payload); break;
        case MessageType::ScanComplete: HandleScanComplete(payload); break;
        case MessageType::JournalOpened: HandleJournalOpened(payload); break;
        case MessageType::UsnBatch: HandleUsnBatch(payload); break;
        case MessageType::JournalResumeInvalid: HandleJournalResumeInvalid(payload); break;
        default: break; // anything else (Handshake/Heartbeat replies, etc.) isn't this pipeline's concern
    }
}

void IngestionPipeline::HandleVolumeList(const std::vector<uint8_t>& payload) {
    if (payload.size() < sizeof(ffprotocol::VolumeListHeader)) {
        return;
    }
    ffprotocol::VolumeListHeader header{};
    std::memcpy(&header, payload.data(), sizeof(header));
    const size_t expectedSize = sizeof(header) + static_cast<size_t>(header.count) * sizeof(ffprotocol::VolumeInfo);
    if (payload.size() != expectedSize) {
        return;
    }

    std::vector<ffprotocol::VolumeInfo> volumes(header.count);
    if (header.count > 0) {
        std::memcpy(volumes.data(), payload.data() + sizeof(header), header.count * sizeof(ffprotocol::VolumeInfo));
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const uint64_t now = NowFileTime();

    std::vector<DurableVolumeId> presentDurableIds;
    presentDurableIds.reserve(volumes.size());

    for (const auto& volume : volumes) {
        // Task 7.1: map the ephemeral, connection-scoped VolumeId to a
        // durable (guid, serial) identity (design.md D6) -- resolved
        // directly by the engine via ordinary unprivileged Win32 calls,
        // not by the privileged service (VolumeEnumeration.cpp only
        // enumerates by drive letter; the durable identity itself needs no
        // elevated access to read).
        auto identity = ResolveDurableVolumeIdentity(volume.driveLetter);
        if (!identity) {
            continue; // the drive vanished between EnumerateVolumes and this lookup -- next cycle will reflect it
        }
        const DurableVolumeId durableId =
            indexStore_->ResolveVolume(ffindexstore::VolumeIdentity{identity->volumeGuid, identity->serialNumber}, volume.driveLetter, now);

        ephemeralToDurable_[volume.id.value] = durableId;
        driveLetterByDurable_[durableId] = volume.driveLetter;
        presentDurableIds.push_back(durableId);

        StartOrResumeVolumeLocked(volume.id, durableId, volume.driveLetter);
    }

    // Task 7.2: anything previously known but absent from this
    // enumeration is unavailable now -- entries retained, never deleted.
    indexStore_->MarkAbsentVolumesUnavailable(presentDurableIds, now);
}

void IngestionPipeline::StartOrResumeVolumeLocked(ffprotocol::VolumeId ephemeralId, DurableVolumeId durableId, wchar_t /*driveLetter*/) {
    auto record = indexStore_->Store().GetVolume(durableId);
    if (!record) {
        return;
    }

    // Task 6.3: resume an incomplete initial scan from its persisted
    // cursor rather than restarting; a volume whose scan already
    // completed in a prior session is not re-scanned on every restart.
    if (!record->scanComplete) {
        ffprotocol::StartVolumeScanRequestHeader header{
            ephemeralId, static_cast<uint16_t>(record->scanCursor.size())};
        std::vector<uint8_t> payload(sizeof(header) + record->scanCursor.size());
        std::memcpy(payload.data(), &header, sizeof(header));
        if (!record->scanCursor.empty()) {
            std::memcpy(payload.data() + sizeof(header), record->scanCursor.data(), record->scanCursor.size());
        }
        connection_->SendCommand(static_cast<uint16_t>(MessageType::StartVolumeScan), payload.data(), static_cast<uint32_t>(payload.size()));
    }

    // D6/task 7.5-7.6: resume the journal from its persisted position only
    // if the journal identity still matches; otherwise open from USN 0.
    // This is always safe (never silently drops changes) because every
    // ingestion write is an idempotent upsert keyed by (volume,
    // FileReferenceNumber) (D7/D8) -- re-processing already-seen journal
    // history is wasted work, never a correctness problem. A mismatch
    // additionally flags the volume for reconciliation (handled inside
    // TryResumeJournal).
    uint64_t resumeUsn = 0;
    if (record->journalId) {
        indexStore_->TryResumeJournal(durableId, *record->journalId, resumeUsn);
    }
    ffprotocol::OpenUsnJournalRequest journalRequest{ephemeralId, resumeUsn};
    connection_->SendCommand(static_cast<uint16_t>(MessageType::OpenUsnJournal), &journalRequest, sizeof(journalRequest));
}

void IngestionPipeline::HandleScanBatch(const std::vector<uint8_t>& payload) {
    if (payload.size() < sizeof(ffprotocol::ScanBatchHeader)) {
        return;
    }
    ffprotocol::ScanBatchHeader header{};
    std::memcpy(&header, payload.data(), sizeof(header));
    if (header.resumeCursorLengthBytes > ffprotocol::kMaxScanCursorLengthBytes ||
        payload.size() < sizeof(header) + header.resumeCursorLengthBytes) {
        return;
    }
    std::vector<uint8_t> cursor(
        payload.begin() + sizeof(header), payload.begin() + sizeof(header) + header.resumeCursorLengthBytes);

    const uint8_t* recordsStart = payload.data() + sizeof(header) + header.resumeCursorLengthBytes;
    const size_t recordsSize = payload.size() - sizeof(header) - header.resumeCursorLengthBytes;
    auto records = ffprotocol::ParseMftBatch(recordsStart, recordsSize, header.recordCount);
    if (!records) {
        return; // malformed batch from our own service is unexpected, but never trusted blindly
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = ephemeralToDurable_.find(header.volumeId.value);
    if (it == ephemeralToDurable_.end()) {
        return;
    }
    const DurableVolumeId durableId = it->second;

    // A reconciliation sweep's scan reuses the same wire message, but its
    // batches are buffered and diffed on completion rather than ingested
    // incrementally (see StartReconciliationSweepLocked).
    if (auto bufferIt = reconciliationBuffers_.find(durableId); bufferIt != reconciliationBuffers_.end()) {
        for (const auto& record : *records) {
            bufferIt->second.push_back(ToIngestEntry(durableId, record));
        }
        return;
    }

    std::vector<IngestEntry> entries;
    entries.reserve(records->size());
    for (const auto& record : *records) {
        entries.push_back(ToIngestEntry(durableId, record));
    }

    if (indexStore_->IngestBatch(durableId, entries, onIndexUpdated_)) {
        // Task 6.4: persist the scan-progress cursor only after the batch
        // it corresponds to has committed.
        indexStore_->Store().SetScanCursor(durableId, cursor);
    }
}

void IngestionPipeline::HandleScanComplete(const std::vector<uint8_t>& payload) {
    if (payload.size() != sizeof(ffprotocol::ScanCompletePayload)) {
        return;
    }
    ffprotocol::ScanCompletePayload complete{};
    std::memcpy(&complete, payload.data(), sizeof(complete));

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = ephemeralToDurable_.find(complete.volumeId.value);
    if (it == ephemeralToDurable_.end()) {
        return;
    }
    const DurableVolumeId durableId = it->second;

    if (auto bufferIt = reconciliationBuffers_.find(durableId); bufferIt != reconciliationBuffers_.end()) {
        // Task 8.1/8.4: diff the freshly-scanned ground truth against the
        // durable store, reusing/updating existing rows.
        indexStore_->ReconcileVolume(durableId, bufferIt->second, NowFileTime(), onIndexUpdated_);
        reconciliationBuffers_.erase(bufferIt);
        return;
    }

    indexStore_->Store().SetScanComplete(durableId, true);
    indexStore_->Store().ClearScanCursor(durableId);
}

void IngestionPipeline::HandleJournalOpened(const std::vector<uint8_t>& payload) {
    if (payload.size() != sizeof(ffprotocol::JournalOpenedPayload)) {
        return;
    }
    ffprotocol::JournalOpenedPayload opened{};
    std::memcpy(&opened, payload.data(), sizeof(opened));
    if (opened.journalId == 0) {
        return; // service could not query/create a journal for this volume
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = ephemeralToDurable_.find(opened.volumeId.value);
    if (it == ephemeralToDurable_.end()) {
        return;
    }
    indexStore_->Store().SetJournalId(it->second, opened.journalId);
}

void IngestionPipeline::HandleUsnBatch(const std::vector<uint8_t>& payload) {
    if (payload.size() < sizeof(ffprotocol::UsnBatchHeader)) {
        return;
    }
    ffprotocol::UsnBatchHeader header{};
    std::memcpy(&header, payload.data(), sizeof(header));

    auto records = ffprotocol::ParseUsnDeltaBatch(payload.data() + sizeof(header), payload.size() - sizeof(header), header.recordCount);
    if (!records) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = ephemeralToDurable_.find(header.volumeId.value);
    if (it == ephemeralToDurable_.end()) {
        return;
    }
    const DurableVolumeId durableId = it->second;

    std::vector<IngestEntry> entries;
    entries.reserve(records->size());
    for (const auto& record : *records) {
        entries.push_back(ToIngestEntry(durableId, record));
    }

    if (indexStore_->IngestBatch(durableId, entries, onIndexUpdated_)) {
        indexStore_->Store().SetResumeUsn(durableId, header.resumeUsnAfterBatch);
    }
}

void IngestionPipeline::HandleJournalResumeInvalid(const std::vector<uint8_t>& payload) {
    if (payload.size() != sizeof(ffprotocol::JournalResumeInvalidPayload)) {
        return;
    }
    ffprotocol::JournalResumeInvalidPayload invalid{};
    std::memcpy(&invalid, payload.data(), sizeof(invalid));

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = ephemeralToDurable_.find(invalid.volumeId.value);
    if (it == ephemeralToDurable_.end()) {
        return;
    }
    // D6/task 7.6: the resume position turned out to be outside the
    // journal's retained range once actually attempted -- fall back to
    // reconciliation rather than silently missing changes.
    indexStore_->MarkNeedsReconciliation(it->second);
}

void IngestionPipeline::StartReconciliationSweepLocked(DurableVolumeId durableId) {
    auto driveLetterIt = driveLetterByDurable_.find(durableId);
    if (driveLetterIt == driveLetterByDurable_.end()) {
        return;
    }
    uint32_t ephemeralId = 0;
    for (const auto& [ephemeral, durable] : ephemeralToDurable_) {
        if (durable == durableId) {
            ephemeralId = ephemeral;
            break;
        }
    }
    if (ephemeralId == 0) {
        return; // volume not currently enumerable on this connection
    }

    reconciliationBuffers_[durableId].clear();

    ffprotocol::StartVolumeScanRequestHeader header{ffprotocol::VolumeId{ephemeralId}, 0};
    connection_->SendCommand(static_cast<uint16_t>(MessageType::StartVolumeScan), &header, sizeof(header));
}

void IngestionPipeline::ReconciliationLoop() {
    // Reuses mutex_ for the wait itself (released while actually
    // sleeping, exactly like std::condition_variable always does) so this
    // loop serializes against HandleFrame's IndexStore access only for the
    // brief window it's actually iterating/sending commands below, never
    // for the whole sleep interval.
    std::unique_lock<std::mutex> lock(mutex_);

    while (running_) {
        reconciliationCv_.wait_for(lock, kReconciliationCheckInterval, [this] { return !running_.load(); });
        if (!running_) {
            break;
        }

        // Task 1.6: scheduled WAL checkpointing, piggybacked on this
        // same low-frequency timer rather than a dedicated one.
        indexStore_->MaintenanceTick();

        if (!privilegedActive_.load()) {
            continue; // task 8.5: no privileged-path reconciliation while degraded
        }

        const uint64_t now = NowFileTime();
        for (const auto& record : indexStore_->Store().AllVolumes()) {
            if (!record.available || !record.scanComplete) {
                continue; // don't race an in-progress initial scan with a reconciliation sweep
            }
            if (reconciliationBuffers_.count(record.id) != 0) {
                continue; // a sweep for this volume is already in flight
            }
            const uint64_t elapsedMs = (now - record.lastReconciliationTime) / 10000; // FILETIME 100ns ticks -> ms
            const bool due = record.needsReconciliation ||
                (record.lastReconciliationTime == 0) ||
                (elapsedMs >= static_cast<uint64_t>(kReconciliationInterval.count()));
            if (due) {
                StartReconciliationSweepLocked(record.id);
            }
        }
    }
}

} // namespace ffengine
