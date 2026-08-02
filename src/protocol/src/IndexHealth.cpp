#include "ffprotocol/IndexHealth.h"

namespace ffprotocol {

IndexHealth DeriveIndexHealth(const VolumeIndexConditions& conditions) noexcept {
    if (!conditions.privilegedConnectionActive || !conditions.reachable) return IndexHealth::Unavailable;
    if (conditions.scanning) return IndexHealth::CurrentlyIndexing;
    if (conditions.needsReconciliation) return IndexHealth::NeedsReconciliation;
    if (conditions.partiallyIndexed) return IndexHealth::PartiallyIndexed;
    return IndexHealth::FullyIndexed;
}

const wchar_t* IndexHealthName(IndexHealth health) noexcept {
    switch (health) {
        case IndexHealth::FullyIndexed: return L"Fully Indexed";
        case IndexHealth::CurrentlyIndexing: return L"Currently Indexing";
        case IndexHealth::PartiallyIndexed: return L"Partially Indexed";
        case IndexHealth::Unavailable: return L"Unavailable";
        case IndexHealth::NeedsReconciliation: return L"Needs Reconciliation";
    }
    return L"Unknown";
}

std::vector<IndexCondition> ApplicableIndexConditions(const VolumeIndexConditions& conditions) {
    std::vector<IndexCondition> result;
    // Unavailable is itself a condition, not a suppression of the others:
    // a volume can be both unreachable and mid-scan, and the detail view must
    // show both so the user understands a search result may be missing/stale
    // for more than one reason (spec "Status Precedence When Multiple
    // Conditions Apply" / scenario "Unavailable outranks an in-progress scan").
    if (!conditions.privilegedConnectionActive || !conditions.reachable) {
        result.push_back(IndexCondition::Unavailable);
    }
    if (conditions.scanning) {
        result.push_back(IndexCondition::CurrentlyIndexing);
    }
    if (conditions.needsReconciliation) {
        result.push_back(IndexCondition::NeedsReconciliation);
    }
    if (conditions.partiallyIndexed) {
        result.push_back(IndexCondition::PartiallyIndexed);
    }
    // A healthy volume has no specific adverse condition; surface FullyIndexed
    // so the detail view is never empty (the function's contract guarantees a
    // non-empty result). When any adverse condition applies, FullyIndexed is
    // intentionally omitted -- it is the absence-of-issues condition, not a
    // co-existing one.
    if (result.empty()) {
        result.push_back(IndexCondition::FullyIndexed);
    }
    return result;
}

const wchar_t* IndexConditionName(IndexCondition condition) noexcept {
    switch (condition) {
        case IndexCondition::Unavailable: return L"Unavailable";
        case IndexCondition::CurrentlyIndexing: return L"Currently Indexing";
        case IndexCondition::NeedsReconciliation: return L"Needs Reconciliation";
        case IndexCondition::PartiallyIndexed: return L"Partially Indexed";
        case IndexCondition::FullyIndexed: return L"Fully Indexed";
    }
    return L"Unknown";
}

} // namespace ffprotocol
