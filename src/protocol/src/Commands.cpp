#include "ffprotocol/Commands.h"

namespace ffprotocol {

std::optional<MessageType> ToMessageType(uint16_t raw) noexcept {
    switch (static_cast<MessageType>(raw)) {
        case MessageType::Handshake:
        case MessageType::HandshakeAck:
        case MessageType::HandshakeReject:
        case MessageType::IncompatibleVersion:
        case MessageType::EnumerateVolumes:
        case MessageType::VolumeList:
        case MessageType::StartVolumeScan:
        case MessageType::StopVolumeScan:
        case MessageType::OpenUsnJournal:
        case MessageType::CloseUsnJournal:
        case MessageType::NotYetImplemented:
        case MessageType::Heartbeat:
        case MessageType::HeartbeatAck:
        case MessageType::ScanRecordBatch:
        case MessageType::ScanComplete:
        case MessageType::UsnJournalOpened:
        case MessageType::JournalRecordBatch:
        case MessageType::JournalResumeInvalid:
            return static_cast<MessageType>(raw);
        default:
            return std::nullopt;
    }
}

bool AreStartVolumeScanFlagsValid(uint16_t flags) noexcept {
    return (flags & ~kKnownStartVolumeScanFlags) == 0;
}

bool IsScanCursorLengthValid(uint16_t lengthBytes) noexcept {
    return lengthBytes <= kMaxScanCursorLengthBytes;
}

} // namespace ffprotocol
