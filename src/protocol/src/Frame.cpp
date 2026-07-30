#include "ffprotocol/Frame.h"

namespace ffprotocol {

bool IsFrameLengthValid(uint32_t totalLength) noexcept {
    const uint64_t length64 = totalLength;
    return length64 >= sizeof(FrameHeader) && length64 <= kMaxFrameSize;
}

} // namespace ffprotocol
