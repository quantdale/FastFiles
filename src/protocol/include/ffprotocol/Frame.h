#pragma once
#include <cstdint>

namespace ffprotocol {

#pragma pack(push, 1)
struct FrameHeader {
    uint32_t totalLength;   // total frame size in bytes, INCLUDING this 8-byte header
    uint16_t structVersion;
    uint16_t messageType;   // validated via ToMessageType() before use -- see Commands.h
};
#pragma pack(pop)
static_assert(sizeof(FrameHeader) == 8, "FrameHeader must be exactly 8 bytes on the wire");

// One protocol-wide maximum, enforced on every pipe in every direction
// (design.md D4). Chosen well above the largest expected batch (see
// Records.h kMaxBatchRecordCount) with headroom, while still bounding
// worst-case allocation from a single frame.
constexpr uint32_t kMaxFrameSize = 1u * 1024u * 1024u; // 1 MiB

// Must be checked immediately after reading the 8-byte header and before
// any buffer is allocated to hold the rest of the frame. Arithmetic is done
// in uint64_t, never uint32_t, so a near-UINT32_MAX totalLength can't wrap
// a subsequent size computation into a small allocation paired with a
// larger copy.
bool IsFrameLengthValid(uint32_t totalLength) noexcept;

} // namespace ffprotocol
