#include "ffprotocol/UiProtocol.h"

#include "ffprotocol/Dispatch.h"

namespace ffprotocol {

std::optional<UiMessageType> ToUiMessageType(uint16_t raw) noexcept {
    switch (static_cast<UiMessageType>(raw)) {
        case UiMessageType::Subscribe:
        case UiMessageType::SubscribeAck:
        case UiMessageType::RequestDirectory:
        case UiMessageType::DirectoryError:
        case UiMessageType::NewGeneration:
        case UiMessageType::EngineStatus:
        case UiMessageType::ReloadIndexingConfig:
            return static_cast<UiMessageType>(raw);
        default:
            return std::nullopt;
    }
}

bool IsUiPathLengthValid(uint16_t lengthChars) noexcept {
    return lengthChars > 0 && lengthChars <= kMaxPathLengthChars;
}

UiFrameValidationResult ValidateUiFrame(const FrameHeader& header) noexcept {
    if (!IsFrameLengthValid(header.totalLength)) {
        return UiFrameValidationResult::RejectedOversizedFrame;
    }
    if (!ToUiMessageType(header.messageType).has_value()) {
        return UiFrameValidationResult::RejectedUnknownMessageType;
    }
    if (header.structVersion != kCurrentStructVersion) {
        return UiFrameValidationResult::RejectedUnsupportedStructVersion;
    }
    return UiFrameValidationResult::Valid;
}

} // namespace ffprotocol
