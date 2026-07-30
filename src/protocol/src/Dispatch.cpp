#include "ffprotocol/Dispatch.h"

namespace ffprotocol {

FrameValidationResult ValidateFrame(const FrameHeader& header) noexcept {
    if (!IsFrameLengthValid(header.totalLength)) {
        return FrameValidationResult::RejectedOversizedFrame;
    }
    if (!ToMessageType(header.messageType).has_value()) {
        return FrameValidationResult::RejectedUnknownMessageType;
    }
    if (header.structVersion != kCurrentStructVersion) {
        return FrameValidationResult::RejectedUnsupportedStructVersion;
    }
    return FrameValidationResult::Valid;
}

} // namespace ffprotocol
