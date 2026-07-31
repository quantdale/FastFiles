#include "ServiceConnection.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

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

// index-storage-and-scanning: a scan or journal stream runs on its own
// background thread (writes to the shared pipe handle serialized via
// `writeMutex`) so the ctrl loop below stays free to keep servicing
// Heartbeat/Stop/Close/second-scan requests concurrently, per tasks.md
// 4.4/5.4's "batched streaming ... for the duration of an open journal
// handle" requirement.
struct ActiveWorker {
    uint32_t volumeId = 0;
    std::shared_ptr<std::atomic<bool>> stopFlag;
    std::thread thread;
};

void StopAndJoin(std::vector<ActiveWorker>& workers, uint32_t volumeId) {
    for (auto it = workers.begin(); it != workers.end(); ++it) {
        if (it->volumeId == volumeId) {
            it->stopFlag->store(true);
            if (it->thread.joinable()) {
                it->thread.join();
            }
            workers.erase(it);
            return;
        }
    }
}

void StopAndJoinAll(std::vector<ActiveWorker>& workers) {
    for (auto& worker : workers) {
        worker.stopFlag->store(true);
    }
    for (auto& worker : workers) {
        if (worker.thread.joinable()) {
            worker.thread.join();
        }
    }
    workers.clear();
}

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

} // namespace

void RunCtrlConnection(HANDLE pipeHandle, const std::wstring& installDir, ConnectionRegistry& registry) {
    const ConnectionRegistry::ConnectionId connectionId = static_cast<ConnectionRegistry::ConnectionId>(reinterpret_cast<uintptr_t>(pipeHandle));

    bool shouldClose = false;
    auto identity = PerformHandshake(pipeHandle, installDir, shouldClose);
    if (!identity) {
        CloseHandle(pipeHandle);
        return;
    }

    // Guards every WriteFrame call on pipeHandle from any thread -- this
    // loop plus any active scan/journal worker threads (VolumeScanner.h /
    // UsnJournalReader.h) all write to the same handle, and named-pipe
    // writes from multiple threads are not implicitly serialized.
    std::mutex writeMutex;
    std::vector<ActiveWorker> activeScans;
    std::vector<ActiveWorker> activeJournals;

    bool disconnect = false;
    while (!disconnect) {
        auto frame = ffipc::ReadFrame(pipeHandle);
        if (!frame) {
            break; // I/O error or clean disconnect
        }

        switch (static_cast<MessageType>(frame->header.messageType)) {
            case MessageType::EnumerateVolumes: {
                std::lock_guard<std::mutex> lock(writeMutex);
                disconnect = !SendVolumeList(pipeHandle);
                break;
            }

            case MessageType::StartVolumeScan: {
                if (frame->payload.size() < sizeof(ffprotocol::StartVolumeScanRequest)) {
                    disconnect = true;
                    break;
                }
                ffprotocol::StartVolumeScanRequest request{};
                std::memcpy(&request, frame->payload.data(), sizeof(request));
                const size_t expectedCursorBytes = frame->payload.size() - sizeof(request);
                if (!ffprotocol::IsScanCursorLengthValid(request.resumeCursorLengthBytes)
                    || expectedCursorBytes != request.resumeCursorLengthBytes) {
                    disconnect = true;
                    break;
                }
                std::vector<uint8_t> resumeCursor(
                    frame->payload.begin() + sizeof(request), frame->payload.end());

                const wchar_t driveLetter = ResolveVolumeIdToDriveLetter(request.volumeId);
                if (driveLetter == L'\0') {
                    disconnect = true;
                    break;
                }

                registry.MarkVolumeScanStarted(connectionId, request.volumeId);
                StopAndJoin(activeScans, request.volumeId.value); // supersede any prior scan of the same volume

                ActiveWorker worker;
                worker.volumeId = request.volumeId.value;
                worker.stopFlag = std::make_shared<std::atomic<bool>>(false);
                const auto volumeId = request.volumeId;
                const auto stopFlag = worker.stopFlag;
                worker.thread = std::thread([pipeHandle, &writeMutex, volumeId, driveLetter, resumeCursor, stopFlag] {
                    RunVolumeScan(pipeHandle, writeMutex, volumeId, driveLetter, resumeCursor, *stopFlag);
                });
                activeScans.push_back(std::move(worker));
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
                StopAndJoin(activeScans, request.volumeId.value);
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

                registry.MarkUsnJournalOpened(connectionId, request.volumeId);
                StopAndJoin(activeJournals, request.volumeId.value);

                ActiveWorker worker;
                worker.volumeId = request.volumeId.value;
                worker.stopFlag = std::make_shared<std::atomic<bool>>(false);
                const auto volumeId = request.volumeId;
                const auto resumeUsn = request.resumeUsn;
                const auto stopFlag = worker.stopFlag;
                worker.thread = std::thread([pipeHandle, &writeMutex, volumeId, driveLetter, resumeUsn, stopFlag] {
                    const JournalStreamOutcome outcome =
                        RunUsnJournalStream(pipeHandle, writeMutex, volumeId, driveLetter, resumeUsn, *stopFlag);
                    if (outcome == JournalStreamOutcome::ResumePositionInvalid) {
                        // D6/task 7.6: tell the engine its ResumeUsn aged
                        // out of the journal's retained range before this
                        // worker tears down, so it falls back to a
                        // reconciliation sweep instead of a blind resume.
                        ffprotocol::JournalResumeInvalidPayload payload{volumeId};
                        std::lock_guard<std::mutex> lock(writeMutex);
                        ffipc::WriteFrame(pipeHandle, static_cast<uint16_t>(MessageType::JournalResumeInvalid),
                                           &payload, sizeof(payload));
                    }
                });
                activeJournals.push_back(std::move(worker));
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
                StopAndJoin(activeJournals, request.volumeId.value);
                break;
            }

            case MessageType::Heartbeat: {
                // Periodic re-validation piggybacks on the heartbeat
                // cadence (task 3.5) -- catches revoked group membership
                // or a replaced/unsigned binary on a long-lived connection,
                // not only at initial Handshake.
                if (!RevalidateClient(*identity, installDir)) {
                    disconnect = true;
                    break;
                }
                std::lock_guard<std::mutex> lock(writeMutex);
                disconnect = !ffipc::WriteFrame(pipeHandle, static_cast<uint16_t>(MessageType::HeartbeatAck));
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

    StopAndJoinAll(activeScans);
    StopAndJoinAll(activeJournals);
    registry.TeardownConnection(connectionId);
    CloseClientIdentity(*identity);
    CloseHandle(pipeHandle);
}

void RunDataConnection(HANDLE pipeHandle) {
    CloseHandle(pipeHandle);
}

} // namespace ffindexsvc
