#pragma once

// Decides what a visible Search panel does when the engine publishes a new
// snapshot generation. Kept off the window procedure so the coalescing rule is
// covered headlessly. Mirrors SearchFiltersDisclosure.h / SearchTabOrder.h.
//
// Generations can publish every few seconds during active indexing, so
// re-dispatches are rate limited. A generation that arrives inside the refresh
// interval must still be serviced: marking a pending flag alone stranded the
// panel on stale results, because the flag was only consumed when the panel
// was next shown -- which never happens while it stays open.

#include <cstdint>

namespace ffui {

inline constexpr unsigned int kSearchSnapshotRefreshIntervalMs = 2000;

enum class SearchSnapshotRefreshAction {
    // Nothing to refresh: panel hidden, or no query to re-run.
    None = 0,
    // Outside the rate limit -- re-dispatch immediately.
    DispatchNow = 1,
    // Inside the rate limit -- mark pending AND arm a timer for the remainder.
    Defer = 2,
};

struct SearchSnapshotRefreshDecision {
    SearchSnapshotRefreshAction action = SearchSnapshotRefreshAction::None;
    // Only meaningful for Defer. Never zero: a zero SetTimer elapse would be
    // clamped by USER to the system minimum rather than firing promptly.
    unsigned int deferMs = 0;

    friend constexpr bool operator==(const SearchSnapshotRefreshDecision& lhs,
                                     const SearchSnapshotRefreshDecision& rhs) {
        return lhs.action == rhs.action && lhs.deferMs == rhs.deferMs;
    }
};

// `msSinceLastDispatch` is measured from the last dispatch that actually ran a
// query, which is what stamps the rate-limit clock.
constexpr SearchSnapshotRefreshDecision DecideSearchSnapshotRefresh(bool panelVisible,
                                                                    bool hasQueryText,
                                                                    std::uint64_t msSinceLastDispatch) {
    if (!panelVisible || !hasQueryText) return {SearchSnapshotRefreshAction::None, 0};
    if (msSinceLastDispatch >= kSearchSnapshotRefreshIntervalMs) {
        return {SearchSnapshotRefreshAction::DispatchNow, 0};
    }
    const auto remaining =
        static_cast<unsigned int>(kSearchSnapshotRefreshIntervalMs - msSinceLastDispatch);
    return {SearchSnapshotRefreshAction::Defer, remaining > 0 ? remaining : 1};
}

}  // namespace ffui
