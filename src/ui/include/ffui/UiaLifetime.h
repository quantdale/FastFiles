#pragma once

#include <atomic>
#include <memory>

namespace ffui {

// Shared owner/fragment lifetime. Existing providers retire these tokens before
// destroying their surface or replacing a fragment; queued actions must obey
// the same boundary as client-held elements.
struct UiaOwnerState {
    std::atomic<bool> alive{true};
    UiaOwnerState() = default;
    UiaOwnerState(const UiaOwnerState&) = delete;
    UiaOwnerState& operator=(const UiaOwnerState&) = delete;
};
using UiaOwnerStatePtr = std::shared_ptr<UiaOwnerState>;

} // namespace ffui
