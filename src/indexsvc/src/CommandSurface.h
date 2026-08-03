#pragma once

#include <cstdint>

#include "ffprotocol/Commands.h"

namespace ffindexsvc {

// Closed command protocol (resolve-raw-volume-privilege-insufficiency
// tasks 2.4/2.5; architecture foundation design.md D4): the only client
// request message types a connected, authenticated client may send on the
// ctrl pipe *after* a successful Handshake. The surface is deliberately
// limited to authenticated volume enumeration, raw-volume scan/journal
// open/close/stop, and structured heartbeat status.
//
// Everything else -- Handshake (already consumed by the handshake stage; a
// second one is a violation), every reply-only type (VolumeList,
// HandshakeAck, HeartbeatAck, ScanRecordBatch, ScanComplete, ...), and any
// unknown/out-of-range value -- is rejected by closing the connection
// before any work is performed. There is deliberately no generic
// "open path/handle", file-mutation, or unrestricted-handle primitive on
// this surface (spec: "Broker does not expose arbitrary privilege").
//
// `ToMessageType` bounds-checks the raw wire value before the membership
// switch, so no jump table is ever indexed with an untrusted integer.
inline bool IsAllowedClientRequest(uint16_t rawMessageType) {
    const auto type = ffprotocol::ToMessageType(rawMessageType);
    if (!type) {
        return false;
    }
    switch (*type) {
        case ffprotocol::MessageType::EnumerateVolumes:
        case ffprotocol::MessageType::StartVolumeScan:
        case ffprotocol::MessageType::StopVolumeScan:
        case ffprotocol::MessageType::OpenUsnJournal:
        case ffprotocol::MessageType::CloseUsnJournal:
        case ffprotocol::MessageType::Heartbeat:
            return true;
        default:
            return false;
    }
}

} // namespace ffindexsvc
