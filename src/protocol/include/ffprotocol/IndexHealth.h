#pragma once

namespace ffprotocol {

enum class IndexHealth { FullyIndexed, CurrentlyIndexing, PartiallyIndexed, Unavailable, NeedsReconciliation };

struct VolumeIndexConditions {
    bool privilegedConnectionActive = false;
    bool reachable = false;
    bool scanning = false;
    bool needsReconciliation = false;
    bool partiallyIndexed = false;
};

IndexHealth DeriveIndexHealth(const VolumeIndexConditions& conditions) noexcept;

} // namespace ffprotocol
