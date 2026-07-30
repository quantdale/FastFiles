#include "ffprotocol/Version.h"

namespace ffprotocol {

bool IsVersionCompatible(ProtocolVersion local, ProtocolVersion peer) noexcept {
    if (peer.major == local.major) {
        return true;
    }
    // One-release-cycle overlap window: a peer exactly one major version
    // behind is still accepted so staged rollouts don't hard-fail.
    return peer.major + 1 == local.major;
}

} // namespace ffprotocol
