#pragma once
#include <cstdint>

namespace ffprotocol {

// Independent of the product build version (design.md D6) -- tracks only
// the shape of the wire protocol itself.
struct ProtocolVersion {
    uint16_t major;
    uint16_t minor;
};

// The version this build of the library implements.
constexpr ProtocolVersion kCurrentProtocolVersion{1, 0};

// Major must match, or be exactly one behind current, to absorb staged
// enterprise rollouts across one release cycle (design.md D6). Minor is
// purely additive and never gates compatibility.
bool IsVersionCompatible(ProtocolVersion local, ProtocolVersion peer) noexcept;

} // namespace ffprotocol
