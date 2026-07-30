#pragma once
#include "ffprotocol/Commands.h"
#include "ffprotocol/Frame.h"

namespace ffprotocol {

// The only StructVersion this build understands. There is exactly one
// wire layout per message type today; D6's additive-minor-version scheme
// (older readers skip trailing fields they don't recognize) is a forward
// extension point, not yet exercised by any message in this change.
constexpr uint16_t kCurrentStructVersion = 1;

enum class FrameValidationResult {
    Valid,
    RejectedOversizedFrame,
    RejectedUnknownMessageType,
    RejectedUnsupportedStructVersion,
};

// Validates a decoded frame header before any payload parsing or handler
// dispatch. Uses bounds-checked lookups (ToMessageType) rather than
// indexing a jump table by the raw, untrusted wire integer (spec
// "Frame and Input Validation" / task 2.5).
FrameValidationResult ValidateFrame(const FrameHeader& header) noexcept;

} // namespace ffprotocol
