#pragma once

#include <vector>

namespace ffprotocol {

enum class IndexHealth { FullyIndexed, CurrentlyIndexing, PartiallyIndexed, Unavailable, NeedsReconciliation };

struct VolumeIndexConditions {
    bool privilegedConnectionActive = false;
    bool reachable = false;
    bool scanning = false;
    bool needsReconciliation = false;
    bool partiallyIndexed = false;
};

// The single headline status, resolved by the fixed precedence order
// (Unavailable > Currently Indexing > Needs Reconciliation > Partially
// Indexed > Fully Indexed). See design.md D7 / spec "Status Precedence When
// Multiple Conditions Apply".
IndexHealth DeriveIndexHealth(const VolumeIndexConditions& conditions) noexcept;
const wchar_t* IndexHealthName(IndexHealth health) noexcept;

// The discrete conditions that can apply to a volume, in the same precedence
// order used by DeriveIndexHealth. The per-volume detail view surfaces every
// applicable condition (not just the headline), so a volume that is both
// mid-scan and whose connection just dropped still shows the in-progress scan
// condition beneath the Unavailable headline (spec scenario "Unavailable
// outranks an in-progress scan"; design D7 "the other conditions remain
// visible in the per-volume detail view, not lost").
enum class IndexCondition {
    Unavailable,           // no active privileged connection, or volume unreachable
    CurrentlyIndexing,     // initial scan or post-reconnection catch-up in progress
    NeedsReconciliation,   // index store detected a mismatch requiring a sweep
    PartiallyIndexed,      // some but not all configured subtrees have completed
    FullyIndexed,          // scan complete, no pending reconciliation, connection active
};

// Returns every condition applicable to `conditions`, in precedence order
// (highest first). Always non-empty: a fully-healthy volume yields a single
// FullyIndexed entry; an unavailable volume yields Unavailable (plus any other
// simultaneously-true conditions such as an in-progress scan, so the detail
// view can explain why a search result may be missing or stale). Pure and
// allocation-free aside from the returned vector.
std::vector<IndexCondition> ApplicableIndexConditions(const VolumeIndexConditions& conditions);
const wchar_t* IndexConditionName(IndexCondition condition) noexcept;

} // namespace ffprotocol
