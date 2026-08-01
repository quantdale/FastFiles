#include "ffprotocol/IndexHealth.h"

namespace ffprotocol {

IndexHealth DeriveIndexHealth(const VolumeIndexConditions& conditions) noexcept {
    if (!conditions.privilegedConnectionActive || !conditions.reachable) return IndexHealth::Unavailable;
    if (conditions.scanning) return IndexHealth::CurrentlyIndexing;
    if (conditions.needsReconciliation) return IndexHealth::NeedsReconciliation;
    if (conditions.partiallyIndexed) return IndexHealth::PartiallyIndexed;
    return IndexHealth::FullyIndexed;
}

} // namespace ffprotocol
