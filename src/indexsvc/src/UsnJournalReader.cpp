#include "UsnJournalReader.h"

#include <windows.h>
#include <winioctl.h>

#include <cstring>

#include "NtfsRawAccess.h"

namespace ffindexsvc {

namespace {

constexpr DWORD kReadBufferBytes = 64 * 1024;

} // namespace

std::optional<JournalIdentity> QueryOrCreateUsnJournal(wchar_t driveLetter) {
    auto volume = RawVolumeHandle::Open(driveLetter);
    if (!volume) {
        return std::nullopt;
    }

    USN_JOURNAL_DATA_V0 data{};
    DWORD bytesReturned = 0;
    BOOL ok = DeviceIoControl(volume->Get(), FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &data, sizeof(data), &bytesReturned, nullptr);

    if (!ok && GetLastError() == ERROR_JOURNAL_NOT_ACTIVE) {
        // First time this volume has been indexed: bring a journal into
        // existence so future changes are tracked at all (task 5.1's
        // "real USN journal reads" presumes one exists).
        CREATE_USN_JOURNAL_DATA createData{};
        createData.MaximumSize = 32ull * 1024 * 1024;
        createData.AllocationDelta = 4ull * 1024 * 1024;
        if (DeviceIoControl(volume->Get(), FSCTL_CREATE_USN_JOURNAL, &createData, sizeof(createData), nullptr, 0, &bytesReturned, nullptr)) {
            ok = DeviceIoControl(volume->Get(), FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &data, sizeof(data), &bytesReturned, nullptr);
        }
    }
    if (!ok) {
        return std::nullopt;
    }

    JournalIdentity identity;
    identity.journalId = data.UsnJournalID;
    identity.firstUsn = static_cast<uint64_t>(data.FirstUsn);
    identity.nextUsn = static_cast<uint64_t>(data.NextUsn);
    return identity;
}

JournalRunOutcome UsnJournalReader::Run(wchar_t driveLetter, uint64_t journalId, uint64_t startUsn, const BatchCallback& onBatch) {
    auto volume = RawVolumeHandle::Open(driveLetter);
    if (!volume) {
        return JournalRunOutcome::VolumeOpenFailed;
    }

    uint64_t currentUsn = startUsn;
    std::vector<uint8_t> outputBuffer(kReadBufferBytes);

    while (!stopRequested_.load(std::memory_order_relaxed)) {
        READ_USN_JOURNAL_DATA_V0 readData{};
        readData.StartUsn = static_cast<USN>(currentUsn);
        readData.ReasonMask = 0xFFFFFFFFu; // filtering by relevance is the engine ingestion pipeline's job, not the service's
        readData.ReturnOnlyOnClose = FALSE;
        readData.Timeout = 0;       // never block indefinitely (spec "no indefinite blocking")
        readData.BytesToWaitFor = 0;
        readData.UsnJournalID = journalId;

        DWORD bytesReturned = 0;
        const BOOL ok = DeviceIoControl(
            volume->Get(), FSCTL_READ_USN_JOURNAL, &readData, sizeof(readData), outputBuffer.data(),
            static_cast<DWORD>(outputBuffer.size()), &bytesReturned, nullptr);
        if (!ok) {
            const DWORD error = GetLastError();
            if (error == ERROR_JOURNAL_ENTRY_DELETED || error == ERROR_INVALID_PARAMETER) {
                // D6/task 7.6: startUsn is no longer within the journal's
                // retained range (wrap, or the journal was deleted and
                // recreated) -- the caller falls back to reconciliation
                // rather than a blind resume or silently missing changes.
                return JournalRunOutcome::ResumePositionInvalid;
            }
            return JournalRunOutcome::VolumeOpenFailed;
        }
        if (bytesReturned < sizeof(USN)) {
            return JournalRunOutcome::StoppedOrDisconnected;
        }

        USN nextUsn = 0;
        std::memcpy(&nextUsn, outputBuffer.data(), sizeof(nextUsn));

        std::vector<ffprotocol::UsnDeltaV1> batch;
        size_t offset = sizeof(USN);
        bool sawAnyRecord = false;
        while (offset + sizeof(USN_RECORD_V2) <= bytesReturned) {
            const auto* record = reinterpret_cast<const USN_RECORD_V2*>(outputBuffer.data() + offset);
            if (record->RecordLength == 0 || offset + record->RecordLength > bytesReturned) {
                break; // malformed trailing record -- stop consuming this buffer, not the whole stream
            }
            sawAnyRecord = true;

            ffprotocol::UsnDeltaV1 delta;
            delta.fixed.usn = static_cast<uint64_t>(record->Usn);
            delta.fixed.fileReferenceNumber = record->FileReferenceNumber;
            delta.fixed.parentFileReferenceNumber = record->ParentFileReferenceNumber;
            delta.fixed.reason = record->Reason;
            delta.fixed.timestamp = static_cast<uint64_t>(record->TimeStamp.QuadPart);

            bool forward = true;
            if (record->Reason & USN_REASON_FILE_DELETE) {
                // The file is gone -- nothing to fetch; use the journal
                // record's own (final) fields directly rather than a raw
                // MFT read that would now fail.
                delta.fixed.sizeBytes = 0;
                delta.fixed.fileAttributes = record->FileAttributes;
                if (record->FileNameLength > 0 &&
                    static_cast<uint32_t>(record->FileNameOffset) + record->FileNameLength <= record->RecordLength) {
                    const auto* namePtr = reinterpret_cast<const uint8_t*>(record) + record->FileNameOffset;
                    delta.fileName.assign(reinterpret_cast<const char16_t*>(namePtr), record->FileNameLength / sizeof(WCHAR));
                }
                delta.fixed.fileNameLengthChars = static_cast<uint16_t>(delta.fileName.size());
            } else {
                // Task 4.3/5.3 applied uniformly to journal deltas: a
                // record for a file that vanished again before this fetch
                // (a benign race, e.g. create-then-immediately-delete) is
                // skipped, not fatal.
                auto parsed = FetchAndParseMftRecord(volume->Get(), volume->BytesPerSector(), record->FileReferenceNumber);
                if (!parsed) {
                    forward = false;
                } else {
                    delta.fixed.parentFileReferenceNumber = parsed->parentFileReferenceNumber;
                    delta.fixed.sizeBytes = parsed->realSizeBytes;
                    delta.fixed.fileAttributes = parsed->dosAttributes | (parsed->isDirectory ? FILE_ATTRIBUTE_DIRECTORY : 0);
                    delta.fileName = parsed->name;
                    delta.fixed.fileNameLengthChars = static_cast<uint16_t>(parsed->name.size());
                }
            }

            if (forward) {
                batch.push_back(std::move(delta));
            }
            offset += record->RecordLength;
        }

        currentUsn = static_cast<uint64_t>(nextUsn);

        if (!batch.empty() || sawAnyRecord) {
            // Report (and let the caller persist) progress even for a
            // buffer that yielded zero forwardable records, so a resumed
            // read never re-fetches the same already-rejected records.
            if (!onBatch(batch, currentUsn)) {
                return JournalRunOutcome::StoppedOrDisconnected;
            }
        }

        if (!sawAnyRecord) {
            // Truly caught up: nothing new since currentUsn. Return
            // rather than spin -- the caller re-invokes on its own cadence
            // (spec "never blocking the connection indefinitely").
            return JournalRunOutcome::StoppedOrDisconnected;
        }
    }

    return JournalRunOutcome::StoppedOrDisconnected;
}

} // namespace ffindexsvc
