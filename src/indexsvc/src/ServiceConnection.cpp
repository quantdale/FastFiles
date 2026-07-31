#include "ServiceConnection.h"

#include <cstdio>
#include <cstring>

#include "ffipc/PipeFraming.h"
#include "ffprotocol/Version.h"

#include "ClientAuthentication.h"
#include "StalenessMonitor.h"
#include "VolumeEnumeration.h"

namespace ffindexsvc {

namespace {

using ffprotocol::MessageType;

bool SendIncompatibleVersion(HANDLE pipeHandle) {
    ffprotocol::IncompatibleVersionPayload payload{ffprotocol::kCurrentProtocolVersion};
    return ffipc::WriteFrame(pipeHandle, static_cast<uint16_t>(MessageType::IncompatibleVersion), &payload, sizeof(payload));
}

bool SendHandshakeReject(HANDLE pipeHandle, ffprotocol::HandshakeRejectReason reason) {
    ffprotocol::HandshakeRejectPayload payload{reason};
    return ffipc::WriteFrame(pipeHandle, static_cast<uint16_t>(MessageType::HandshakeReject), &payload, sizeof(payload));
}

bool SendNotYetImplemented(HANDLE pipeHandle, MessageType requestType) {
    ffprotocol::NotYetImplementedPayload payload{static_cast<uint16_t>(requestType)};
    return ffipc::WriteFrame(pipeHandle, static_cast<uint16_t>(MessageType::NotYetImplemented), &payload, sizeof(payload));
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
                if (frame->payload.size() != sizeof(ffprotocol::StartVolumeScanRequest)) {
                    disconnect = true;
                    break;
                }
                ffprotocol::StartVolumeScanRequest request{};
                std::memcpy(&request, frame->payload.data(), sizeof(request));
                registry.MarkVolumeScanStarted(connectionId, request.volumeId);
                disconnect = !SendNotYetImplemented(pipeHandle, MessageType::StartVolumeScan);
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
                disconnect = !SendNotYetImplemented(pipeHandle, MessageType::StopVolumeScan);
                break;
            }

            case MessageType::OpenUsnJournal: {
                if (frame->payload.size() != sizeof(ffprotocol::OpenUsnJournalRequest)) {
                    disconnect = true;
                    break;
                }
                ffprotocol::OpenUsnJournalRequest request{};
                std::memcpy(&request, frame->payload.data(), sizeof(request));
                registry.MarkUsnJournalOpened(connectionId, request.volumeId);
                disconnect = !SendNotYetImplemented(pipeHandle, MessageType::OpenUsnJournal);
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
                disconnect = !SendNotYetImplemented(pipeHandle, MessageType::CloseUsnJournal);
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

    registry.TeardownConnection(connectionId);
    CloseClientIdentity(*identity);
    CloseHandle(pipeHandle);
}

void RunDataConnection(HANDLE pipeHandle) {
    CloseHandle(pipeHandle);
}

} // namespace ffindexsvc
