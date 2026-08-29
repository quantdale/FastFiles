// Regression coverage for the Search panel's snapshot-refresh coalescing rule.
//
// A snapshot generation published inside the rate-limit interval used to set a
// pending flag and nothing else. That flag was only consumed by ShowAndFocus,
// so while the panel stayed open the refresh was never serviced and the panel
// kept showing results from before the generation -- for a query the user had
// already typed. These cases pin that a deferred refresh always reports a
// non-zero delay for the caller to arm a timer with.

#include "ffui/SearchSnapshotRefresh.h"

#include "../TestSupport.h"

using namespace fftest;

namespace {
// Defeats constant folding so the decision is exercised at runtime rather than
// collapsed by the optimizer into the expected answer.
bool Runtime(bool value) {
    volatile bool opaque = value;
    return opaque;
}
std::uint64_t Runtime(std::uint64_t value) {
    volatile std::uint64_t opaque = value;
    return opaque;
}

using Action = ffui::SearchSnapshotRefreshAction;

ffui::SearchSnapshotRefreshDecision Decide(bool visible, bool hasQuery, std::uint64_t sinceMs) {
    return ffui::DecideSearchSnapshotRefresh(Runtime(visible), Runtime(hasQuery), Runtime(sinceMs));
}

void TestNothingToRefresh() {
    Check(Decide(false, true, 0).action == Action::None, "hidden panel does not refresh");
    Check(Decide(false, true, 60000).action == Action::None,
          "hidden panel does not refresh even long after the last dispatch");
    Check(Decide(true, false, 60000).action == Action::None, "no query text means nothing to re-run");
    Check(Decide(false, false, 0).action == Action::None, "hidden and empty does not refresh");
}

void TestOutsideIntervalDispatchesImmediately() {
    Check(Decide(true, true, ffui::kSearchSnapshotRefreshIntervalMs).action == Action::DispatchNow,
          "a generation exactly at the interval dispatches now");
    Check(Decide(true, true, ffui::kSearchSnapshotRefreshIntervalMs + 1).action == Action::DispatchNow,
          "a generation past the interval dispatches now");
    Check(Decide(true, true, 60000).deferMs == 0, "an immediate dispatch carries no defer delay");
}

// The defect: inside the interval the decision must still schedule work.
void TestInsideIntervalDefersWithNonZeroDelay() {
    for (std::uint64_t since = 0; since < ffui::kSearchSnapshotRefreshIntervalMs; ++since) {
        const auto decision = Decide(true, true, since);
        if (decision.action != Action::Defer) {
            Check(false, "a generation inside the interval must defer, not drop");
            return;
        }
        if (decision.deferMs == 0) {
            Check(false, "a deferred refresh must carry a non-zero timer delay");
            return;
        }
        if (decision.deferMs > ffui::kSearchSnapshotRefreshIntervalMs) {
            Check(false, "a deferred refresh must not wait longer than the interval");
            return;
        }
    }
    Check(true, "every generation inside the interval defers with a bounded non-zero delay");
}

void TestDeferDelayCoversRemainderOfInterval() {
    Check(Decide(true, true, 0).deferMs == ffui::kSearchSnapshotRefreshIntervalMs,
          "a generation at the instant of dispatch waits the whole interval");
    Check(Decide(true, true, 500).deferMs == ffui::kSearchSnapshotRefreshIntervalMs - 500,
          "the defer delay is the remainder of the interval");
    Check(Decide(true, true, ffui::kSearchSnapshotRefreshIntervalMs - 1).deferMs == 1,
          "the last millisecond of the interval still defers by 1ms, never 0");
}

}  // namespace

int main() {
    TestNothingToRefresh();
    TestOutsideIntervalDispatchesImmediately();
    TestInsideIntervalDefersWithNonZeroDelay();
    TestDeferDelayCoversRemainderOfInterval();
    if (FailureCount() != 0) {
        fprintf(stderr, "ffsearch_snapshot_refresh_tests: %d check(s) failed\n", FailureCount());
        return 1;
    }
    printf("ffsearch_snapshot_refresh_tests: all checks passed\n");
    return 0;
}
