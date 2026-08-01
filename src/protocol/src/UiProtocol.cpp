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
        case UiMessageType::RequestUnavailableVolumes:
        case UiMessageType::UnavailableVolumes:
        case UiMessageType::ForgetUnavailableVolume:
        case UiMessageType::ForgetUnavailableVolumeResult:
            return static_cast<UiMessageType>(raw);
        default:
            return std::nullopt;
    }
}

bool IsUiPathLengthValid(uint16_t lengthChars) noexcept {
    return lengthChars > 0 && lengthChars <= kMaxPathLengthChars;
}

bool IsForgetUnavailableVolumeStatusValid(ForgetUnavailableVolumeStatus status) noexcept {
    switch (status) {
        case ForgetUnavailableVolumeStatus::Removed:
        case ForgetUnavailableVolumeStatus::NotFound:
        case ForgetUnavailableVolumeStatus::VolumeAvailable:
        case ForgetUnavailableVolumeStatus::Failed:
            return true;
        default:
            return false;
    }
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
