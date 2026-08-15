#pragma once
#include <cstdint>

namespace ffprotocol {

// Independent of the product build version (design.md D6) -- tracks only
// the shape of the wire protocol itself.
struct ProtocolVersion {
    uint16_t major;
    uint16_t minor;
};

// The version this build of the library implements. Minor moved 2.0 -> 2.1
// for allocated-size-storage-accuracy: a {2,1} peer understands the V2 MFT
// scan records (ScanRecordBatchV2) and V3 USN records (JournalRecordBatchV3)
// that carry allocated size. Because major is the only compatibility gate
// and negotiation takes the per-component min, a 2.1 peer talking to a 2.0
// peer simply falls back to the legacy V1/V2 record layouts -- exactly as
// the service's SendJournalBatch major-1 branch works today.
constexpr ProtocolVersion kCurrentProtocolVersion{2, 1};

// Major must match, or be exactly one behind current, to absorb staged
// enterprise rollouts across one release cycle (design.md D6). Minor is
// purely additive and never gates compatibility.
bool IsVersionCompatible(ProtocolVersion local, ProtocolVersion peer) noexcept;

// allocated-size-storage-accuracy D3: true when a peer that negotiated
// `v` understands the additive allocated-size wire records (V2 MFT scan
// records and V3 USN records). The single gate the service checks when
// choosing which record layouts to emit.
constexpr bool ProtocolSupportsAllocatedSize(ProtocolVersion v) noexcept {
    return v.major > 2 || (v.major == 2 && v.minor >= 1);
}

} // namespace ffprotocol
