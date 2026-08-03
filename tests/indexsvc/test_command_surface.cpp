#include <cstdio>
#include <cstdlib>

#include "CommandSurface.h"
#include "ffprotocol/Commands.h"

namespace {
int failures = 0;
void Check(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
}

int main() {
    using namespace ffindexsvc;
    using ffprotocol::MessageType;

    // The closed command protocol (resolve-raw-volume-privilege-insufficiency
    // tasks 2.4/2.5): exactly the documented volume/journal/heartbeat surface
    // is allowed on an established, authenticated ctrl connection.
    Check(IsAllowedClientRequest(static_cast<uint16_t>(MessageType::EnumerateVolumes)),
          "allowlist: EnumerateVolumes is an allowed client request");
    Check(IsAllowedClientRequest(static_cast<uint16_t>(MessageType::StartVolumeScan)),
          "allowlist: StartVolumeScan is an allowed client request");
    Check(IsAllowedClientRequest(static_cast<uint16_t>(MessageType::StopVolumeScan)),
          "allowlist: StopVolumeScan is an allowed client request");
    Check(IsAllowedClientRequest(static_cast<uint16_t>(MessageType::OpenUsnJournal)),
          "allowlist: OpenUsnJournal is an allowed client request");
    Check(IsAllowedClientRequest(static_cast<uint16_t>(MessageType::CloseUsnJournal)),
          "allowlist: CloseUsnJournal is an allowed client request");
    Check(IsAllowedClientRequest(static_cast<uint16_t>(MessageType::Heartbeat)),
          "allowlist: Heartbeat is an allowed client request");

    // Handshake is consumed by the dedicated handshake stage before the
    // dispatch loop; a second Handshake on an established connection is a
    // protocol violation (spec: "Broker does not expose arbitrary privilege").
    Check(!IsAllowedClientRequest(static_cast<uint16_t>(MessageType::Handshake)),
          "allowlist: a second Handshake is rejected");

    // Every reply-only (service -> client) type is rejected if a client
    // sends it: there is no client-usable operation behind any of these.
    Check(!IsAllowedClientRequest(static_cast<uint16_t>(MessageType::HandshakeAck)),
          "allowlist: reply-only HandshakeAck is rejected");
    Check(!IsAllowedClientRequest(static_cast<uint16_t>(MessageType::HandshakeReject)),
          "allowlist: reply-only HandshakeReject is rejected");
    Check(!IsAllowedClientRequest(static_cast<uint16_t>(MessageType::IncompatibleVersion)),
          "allowlist: reply-only IncompatibleVersion is rejected");
    Check(!IsAllowedClientRequest(static_cast<uint16_t>(MessageType::VolumeList)),
          "allowlist: reply-only VolumeList is rejected");
    Check(!IsAllowedClientRequest(static_cast<uint16_t>(MessageType::NotYetImplemented)),
          "allowlist: reply-only NotYetImplemented is rejected");
    Check(!IsAllowedClientRequest(static_cast<uint16_t>(MessageType::HeartbeatAck)),
          "allowlist: reply-only HeartbeatAck is rejected");
    Check(!IsAllowedClientRequest(static_cast<uint16_t>(MessageType::ScanRecordBatch)),
          "allowlist: reply-only ScanRecordBatch is rejected");
    Check(!IsAllowedClientRequest(static_cast<uint16_t>(MessageType::ScanComplete)),
          "allowlist: reply-only ScanComplete is rejected");
    Check(!IsAllowedClientRequest(static_cast<uint16_t>(MessageType::UsnJournalOpened)),
          "allowlist: reply-only UsnJournalOpened is rejected");
    Check(!IsAllowedClientRequest(static_cast<uint16_t>(MessageType::JournalRecordBatch)),
          "allowlist: reply-only JournalRecordBatch is rejected");
    Check(!IsAllowedClientRequest(static_cast<uint16_t>(MessageType::JournalResumeInvalid)),
          "allowlist: reply-only JournalResumeInvalid is rejected");

    // No generic escape hatches exist on the surface: unknown and
    // out-of-range wire values are rejected before any dispatch table is
    // consulted (ToMessageType bounds-checks first).
    Check(!IsAllowedClientRequest(0),
          "allowlist: zero (invalid) message type is rejected");
    Check(!IsAllowedClientRequest(0xFFFF),
          "allowlist: out-of-range message type is rejected");
    Check(!IsAllowedClientRequest(19),
          "allowlist: value above the largest known type is rejected");

    // Exhaustive sweep over the entire representable range: the allowlist
    // must accept exactly the six documented client requests.
    size_t allowedCount = 0;
    for (uint32_t value = 0; value <= 0xFFFF; ++value) {
        if (IsAllowedClientRequest(static_cast<uint16_t>(value))) {
            ++allowedCount;
            const bool isDocumented =
                value == static_cast<uint16_t>(MessageType::EnumerateVolumes) ||
                value == static_cast<uint16_t>(MessageType::StartVolumeScan) ||
                value == static_cast<uint16_t>(MessageType::StopVolumeScan) ||
                value == static_cast<uint16_t>(MessageType::OpenUsnJournal) ||
                value == static_cast<uint16_t>(MessageType::CloseUsnJournal) ||
                value == static_cast<uint16_t>(MessageType::Heartbeat);
            Check(isDocumented, "allowlist: every accepted value is in the documented surface");
        }
    }
    Check(allowedCount == 6, "allowlist: exactly six client request types are accepted");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
