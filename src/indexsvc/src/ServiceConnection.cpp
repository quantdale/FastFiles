#include "ServiceConnection.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include "ffipc/PipeFraming.h"
#include "ffprotocol/Version.h"

#include "ClientAuthentication.h"
#include "StalenessMonitor.h"
#include "UsnJournalReader.h"
#include "VolumeEnumeration.h"
#include "VolumeScanner.h"

namespace ffindexsvc {

namespace {

using ffprotocol::MessageType;

// Thread-safe wrapper around the one pipe handle a connection owns: the
// main read loop and any active scan/journal worker threads all write
// asynchronously onto the same pipe, and named-pipe writes from multiple
// threads must never interleave (a torn frame would violate the framing
// contract every reader on this pipe depends on).
class PipeWriter {
public:
    explicit PipeWriter(HANDLE pipe) : pipe_(pipe) {}

    bool Write(uint16_t messageType, const void* payload, uint32_t payloadSize) {
        std::lock_guard<std::mutex> lock(mutex_);
        return ffipc::WriteFrame(pipe_, messageType, payload, payloadSize);
    }
    bool Write(uint16_t messageType) {
        std::lock_guard<std::mutex> lock(mutex_);
        return ffipc::WriteFrame(pipe_, messageType);
    }

private:
    HANDLE pipe_;
    std::mutex mutex_;
};

bool SendIncompatibleVersion(HANDLE pipeHandle) {
    ffprotocol::IncompatibleVersionPayload payload{ffprotocol::kCurrentProtocolVersion};
    return ffipc::WriteFrame(pipeHandle, static_cast<uint16_t>(MessageType::IncompatibleVersion), &payload, sizeof(payload));
}

bool SendHandshakeReject(HANDLE pipeHandle, ffprotocol::HandshakeRejectReason reason) {
    ffprotocol::HandshakeRejectPayload payload{reason};
    return ffipc::WriteFrame(pipeHandle, static_cast<uint16_t>(MessageType::HandshakeReject), &payload, sizeof(payload));
}

bool SendVolumeList(HANDLE pipeHandle) {
    auto volumes = EnumerateFixedNtfsVolumes();

    std::vector<uint8_t> payload(sizeof(ffprotocol::VolumeListHeader) + volumes.size() * sizeof(ffprotocol::VolumeInfo));
    ffprotocol::VolumeListHeader header{static_cast<uint32_t>(volumes.size())};
    std::memcpy(payload.data(), &header, sizeof(header));
    if (!volumes.empty()) {
        std::memcpy(payload.data() + sizeof(header), volumes.data(), volumes.size() * sizeof(ffprotocol::VolumeInfo));
    }

    return ffipc::WriteFrame(pipeHandle, static_cast<uint16_t>(MessageType::VolumeList), payload.data(), static_cast<uint32_t>(payload.size()));
}

// Serializes and sends one ScanBatch frame: header, resume cursor bytes,
// then the MFT record batch (Records.h layout) -- see Commands.h's
// ScanBatchHeader comment for the exact wire order.
bool SendScanBatch(PipeWriter& writer, ffprotocol::VolumeId volumeId, const std::vector<ffprotocol::MftRecordV1>& batch,
                    const std::vector<uint8_t>& resumeCursor) {
    ffprotocol::ScanBatchHeader header{volumeId, static_cast<uint32_t>(batch.size()), static_cast<uint16_t>(resumeCursor.size())};
    std::vector<uint8_t> recordsBlob = ffprotocol::SerializeMftBatch(batch);

    std::vector<uint8_t> payload;
    payload.reserve(sizeof(header) + resumeCursor.size() + recordsBlob.size());
    payload.insert(payload.end(), reinterpret_cast<uint8_t*>(&header), reinterpret_cast<uint8_t*>(&header) + sizeof(header));
    payload.insert(payload.end(), resumeCursor.begin(), resumeCursor.end());
    payload.insert(payload.end(), recordsBlob.begin(), recordsBlob.end());

    return writer.Write(static_cast<uint16_t>(MessageType::ScanBatch), payload.data(), static_cast<uint32_t>(payload.size()));
}

bool SendUsnBatch(PipeWriter& writer, ffprotocol::VolumeId volumeId, const std::vector<ffprotocol::UsnDeltaV1>& batch) {
    ffprotocol::UsnBatchHeader header{volumeId, static_cast<uint32_t>(batch.size())};
    std::vector<uint8_t> recordsBlob = ffprotocol::SerializeUsnDeltaBatch(batch);

    std::vector<uint8_t> payload;
    payload.reserve(sizeof(header) + recordsBlob.size());
    payload.insert(payload.end(), reinterpret_cast<uint8_t*>(&header), reinterpret_cast<uint8_t*>(&header) + sizeof(header));
    payload.insert(payload.end(), recordsBlob.begin(), recordsBlob.end());

    return writer.Write(static_cast<uint16_t>(MessageType::UsnBatch), payload.data(), static_cast<uint32_t>(payload.size()));
}

// Returns std::nullopt (and does not reply -- the connection is closed by
// the caller) if the Handshake payload itself is malformed, or if version
// negotiation/authentication fails after having already sent the
// appropriate rejection reply.
std::optional<ClientIdentity> PerformHandshake(HANDLE pipeHandle, const std::wstring& installDir, bool& outShouldClose) {
    auto frame = ffipc::ReadFrame(pipeHandle);
    outShouldClose = true;
    if (!frame || frame->header.messageType != static_cast<uint16_t>(MessageType::Handshake)) {
        return std::nullopt; // first message must be Handshake
    }
    if (frame->payload.size() != sizeof(ffprotocol::HandshakeRequest)) {
        return std::nullopt;
    }

    ffprotocol::HandshakeRequest request{};
    std::memcpy(&request, frame->payload.data(), sizeof(request));

    if (!ffprotocol::IsVersionCompatible(ffprotocol::kCurrentProtocolVersion, request.clientVersion)) {
        SendIncompatibleVersion(pipeHandle);
        return std::nullopt;
    }

    // Opportunistic staleness check alongside every new connection (spec:
    // "checked ... opportunistically at handshake").
    CheckStalenessNow();

    ffprotocol::HandshakeRejectReason rejectReason{};
    auto identity = VerifyClientAtHandshake(pipeHandle, installDir, rejectReason);
    if (!identity) {
        SendHandshakeReject(pipeHandle, rejectReason);
        return std::nullopt;
    }

    ffprotocol::HandshakeAckPayload ack{ffprotocol::kCurrentProtocolVersion};
    if (!ffipc::WriteFrame(pipeHandle, static_cast<uint16_t>(MessageType::HandshakeAck), &ack, sizeof(ack))) {
        CloseClientIdentity(*identity);
        return std::nullopt;
    }

    outShouldClose = false;
    return identity;
}

// Per-connection bookkeeping for active scan/journal worker threads,
// separate from ConnectionRegistry (which only tracks cross-connection
// ownership, not thread lifetimes). Only ever touched from the main
// per-connection loop thread -- worker threads communicate exclusively via
// their VolumeScanner/UsnJournalReader's RequestStop()/atomic state and the
// shared PipeWriter, never by reaching back into these maps themselves, so
// there is no self-join hazard and no need for its own mutex.
struct ScanWorker {
    std::shared_ptr<VolumeScanner> scanner;
    std::thread thread;
};
struct JournalWorker {
    std::shared_ptr<UsnJournalReader> reader;
    std::thread thread;
};

void StopAndJoin(ScanWorker& worker) {
    worker.scanner->RequestStop();
    if (worker.thread.joinable()) {
        worker.thread.join();
    }
}
void StopAndJoin(JournalWorker& worker) {
    worker.reader->RequestStop();
    if (worker.thread.joinable()) {
        worker.thread.join();
    }
}

} // namespace

void RunCtrlConnection(HANDLE pipeHandle, const std::wstring& installDir, ConnectionRegistry& registry) {
    const ConnectionRegistry::ConnectionId connectionId = static_cast<ConnectionRegistry::ConnectionId>(reinterpret_cast<uintptr_t>(pipeHandle));

    bool shouldClose = false;
    auto identity = PerformHandshake(pipeHandle, installDir, shouldClose);
    if (!identity) {
        CloseHandle(pipeHandle);
        return;
    }

    auto writer = std::make_shared<PipeWriter>(pipeHandle);
    std::map<uint32_t, ScanWorker> scanWorkers;
    std::map<uint32_t, JournalWorker> journalWorkers;

    bool disconnect = false;
    while (!disconnect) {
        auto frame = ffipc::ReadFrame(pipeHandle);
        if (!frame) {
            break; // I/O error or clean disconnect
        }

        switch (static_cast<MessageType>(frame->header.messageType)) {
            case MessageType::EnumerateVolumes:
                disconnect = !SendVolumeList(pipeHandle);
                break;

            case MessageType::StartVolumeScan: {
                if (frame->payload.size() < sizeof(ffprotocol::StartVolumeScanRequestHeader)) {
                    disconnect = true;
                    break;
                }
                ffprotocol::StartVolumeScanRequestHeader header{};
                std::memcpy(&header, frame->payload.data(), sizeof(header));
                if (header.resumeCursorLengthBytes > ffprotocol::kMaxScanCursorLengthBytes ||
                    frame->payload.size() != sizeof(header) + header.resumeCursorLengthBytes) {
                    disconnect = true;
                    break;
                }
                std::vector<uint8_t> cursorBytes(
                    frame->payload.begin() + sizeof(header), frame->payload.begin() + sizeof(header) + header.resumeCursorLengthBytes);

                const wchar_t driveLetter = ResolveVolumeIdToDriveLetter(header.volumeId);
                if (driveLetter == L'\0') {
                    disconnect = true; // unknown VolumeId -- never issued, or the volume vanished since
                    break;
                }

                registry.MarkVolumeScanStarted(connectionId, header.volumeId);

                // A prior scan on this exact VolumeId that hasn't finished
                // yet is replaced, not run concurrently with the new one.
                if (auto it = scanWorkers.find(header.volumeId.value); it != scanWorkers.end()) {
                    StopAndJoin(it->second);
                    scanWorkers.erase(it);
                }

                auto scanner = std::make_shared<VolumeScanner>();
                uint64_t resumeFrom = 0;
                if (!cursorBytes.empty()) {
                    if (auto parsedCursor = DeserializeScanCursor(cursorBytes)) {
                        resumeFrom = *parsedCursor;
                    }
                }

                const ffprotocol::VolumeId volumeId = header.volumeId;
                std::thread worker([scanner, writer, volumeId, driveLetter, resumeFrom]() {
                    scanner->Run(driveLetter, resumeFrom, [&](const std::vector<ffprotocol::MftRecordV1>& batch, std::vector<uint8_t> cursor) {
                        return SendScanBatch(*writer, volumeId, batch, cursor);
                    });
                    if (!scanner->WasStopped()) {
                        ffprotocol::ScanCompletePayload complete{volumeId};
                        writer->Write(static_cast<uint16_t>(MessageType::ScanComplete), &complete, sizeof(complete));
                    }
                });
                scanWorkers[header.volumeId.value] = ScanWorker{scanner, std::move(worker)};
                break;
            }

            case MessageType::StopVolumeScan: {
                if (frame->payload.size() != sizeof(ffprotocol::StopVolumeScanRequest)) {
                    disconnect = true;
                    break;
                }
                ffprotocol::StopVolumeScanRequest request{};
                std::memcpy(&request, frame->payload.data(), sizeof(request));
                if (!registry.TryStopVolumeScan(connectionId, request.volumeId)) {
                    // Spec "Stop request from a different connection is
                    // rejected" -- the closed protocol has no dedicated
                    // rejection reply, so reject by closing the connection,
                    // consistent with how an unrecognized message type is
                    // handled.
                    disconnect = true;
                    break;
                }
                if (auto it = scanWorkers.find(request.volumeId.value); it != scanWorkers.end()) {
                    StopAndJoin(it->second);
                    scanWorkers.erase(it);
                }
                break;
            }

            case MessageType::OpenUsnJournal: {
                if (frame->payload.size() != sizeof(ffprotocol::OpenUsnJournalRequest)) {
                    disconnect = true;
                    break;
                }
                ffprotocol::OpenUsnJournalRequest request{};
                std::memcpy(&request, frame->payload.data(), sizeof(request));

                const wchar_t driveLetter = ResolveVolumeIdToDriveLetter(request.volumeId);
                if (driveLetter == L'\0') {
                    disconnect = true;
                    break;
                }

                auto identity2 = QueryOrCreateUsnJournal(driveLetter);
                if (!identity2) {
                    // Spec "respond within normal response time ... never
                    // blocking indefinitely": still reply, just report an
                    // id of 0 the engine will never match against a
                    // persisted journal id, rather than hanging the caller.
                    ffprotocol::JournalOpenedPayload failedReply{request.volumeId, 0};
                    disconnect = !ffipc::WriteFrame(pipeHandle, static_cast<uint16_t>(MessageType::JournalOpened), &failedReply, sizeof(failedReply));
                    break;
                }

                registry.MarkUsnJournalOpened(connectionId, request.volumeId);

                ffprotocol::JournalOpenedPayload reply{request.volumeId, identity2->journalId};
                if (!ffipc::WriteFrame(pipeHandle, static_cast<uint16_t>(MessageType::JournalOpened), &reply, sizeof(reply))) {
                    disconnect = true;
                    break;
                }

                if (auto it = journalWorkers.find(request.volumeId.value); it != journalWorkers.end()) {
                    StopAndJoin(it->second);
                    journalWorkers.erase(it);
                }

                auto reader = std::make_shared<UsnJournalReader>();
                const ffprotocol::VolumeId volumeId = request.volumeId;
                const uint64_t journalId = identity2->journalId;
                const uint64_t resumeUsn = request.resumeUsn;
                std::thread worker([reader, writer, volumeId, driveLetter, journalId, resumeUsn]() {
                    const JournalRunOutcome outcome = reader->Run(driveLetter, journalId, resumeUsn,
                        [&](const std::vector<ffprotocol::UsnDeltaV1>& batch, uint64_t /*resumeUsnAfterBatch*/) {
                            return SendUsnBatch(*writer, volumeId, batch);
                        });
                    if (outcome == JournalRunOutcome::ResumePositionInvalid) {
                        ffprotocol::JournalResumeInvalidPayload payload{volumeId};
                        writer->Write(static_cast<uint16_t>(MessageType::JournalResumeInvalid), &payload, sizeof(payload));
                    }
                });
                journalWorkers[request.volumeId.value] = JournalWorker{reader, std::move(worker)};
                break;
            }

            case MessageType::CloseUsnJournal: {
                if (frame->payload.size() != sizeof(ffprotocol::CloseUsnJournalRequest)) {
                    disconnect = true;
                    break;
                }
                ffprotocol::CloseUsnJournalRequest request{};
                std::memcpy(&request, frame->payload.data(), sizeof(request));
                if (!registry.TryCloseUsnJournal(connectionId, request.volumeId)) {
                    disconnect = true;
                    break;
                }
                if (auto it = journalWorkers.find(request.volumeId.value); it != journalWorkers.end()) {
                    StopAndJoin(it->second);
                    journalWorkers.erase(it);
                }
                break;
            }

            case MessageType::Heartbeat: {
                // Periodic re-validation piggybacks on the heartbeat
                // cadence (task 3.5) -- catches revoked group membership
                // or a replaced/unsigned binary on a long-lived connection,
                // not only at initial Handshake. The read loop stays
                // responsive to this even while scan/journal workers
                // stream batches on their own threads, since those never
                // block this loop (task 5.5's "no indefinite blocking").
                if (!RevalidateClient(*identity, installDir)) {
                    disconnect = true;
                    break;
                }
                disconnect = !writer->Write(static_cast<uint16_t>(MessageType::HeartbeatAck));
                break;
            }

            default:
                // Any other known-but-unexpected message type on an
                // established connection (a reply-only type sent by a
                // client, or a second Handshake) is a protocol violation.
                disconnect = true;
                break;
        }
    }

    for (auto& [id, worker] : scanWorkers) {
        (void)id;
        StopAndJoin(worker);
    }
    for (auto& [id, worker] : journalWorkers) {
        (void)id;
        StopAndJoin(worker);
    }

    registry.TeardownConnection(connectionId);
    CloseClientIdentity(*identity);
    CloseHandle(pipeHandle);
}

void RunDataConnection(HANDLE pipeHandle) {
    CloseHandle(pipeHandle);
}

} // namespace ffindexsvc
