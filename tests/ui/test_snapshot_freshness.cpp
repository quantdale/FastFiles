#include <cstdint>

#include "ffui/SnapshotFreshness.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {

void TestInitialSuccessAndEmptySuccess() {
    ffui::SnapshotFreshness empty;
    empty.BeginRequest();
    Check(empty.AcceptGeneration(1), "initial snapshot generation is accepted");
    Check(empty.state == ffui::SnapshotViewState::Current && empty.HasLastKnownGood(),
          "initial success is current even when the valid directory has zero items");

    ffui::SnapshotFreshness failed;
    failed.BeginRequest();
    Check(failed.Fail(ffui::SnapshotFailureReason::EnumerationFailed),
          "initial request failure is recorded");
    Check(failed.state == ffui::SnapshotViewState::Unavailable,
          "no prior snapshot becomes unavailable instead of fake empty current data");
}

void TestSameContextStaleAndRecovery() {
    ffui::SnapshotFreshness state;
    state.BeginRequest();
    Check(state.AcceptGeneration(1), "same-context baseline generation loads");
    state.BeginRequest();
    Check(state.Fail(ffui::SnapshotFailureReason::PublishFailed),
          "same-context publish failure is recorded");
    Check(state.state == ffui::SnapshotViewState::StaleWithError && state.HasLastKnownGood() &&
              state.lastSuccessfulGeneration == 1,
          "same-context failure retains the last-known-good generation as stale");
    Check(state.AcceptGeneration(2), "later same-context generation is accepted");
    Check(state.state == ffui::SnapshotViewState::Current &&
              state.failure == ffui::SnapshotFailureReason::None,
          "successful recovery clears the stale/error state");
}

void TestContextIsolation() {
    ffui::SnapshotFreshness folderA;
    folderA.AcceptGeneration(1);

    ffui::SnapshotFreshness folderB;
    folderB.BeginRequest();
    folderB.Fail(ffui::SnapshotFailureReason::NoLongerAvailable);

    Check(folderA.state == ffui::SnapshotViewState::Current,
          "a previous location remains current in its own context");
    Check(folderB.state == ffui::SnapshotViewState::Unavailable && !folderB.HasLastKnownGood(),
          "a failed new location cannot inherit the previous location's data");
}

void TestMappingFailureAndGenerationOrdering() {
    ffui::SnapshotFreshness state;
    state.AcceptGeneration(4);
    state.AnnounceGeneration(5);
    Check(state.Fail(ffui::SnapshotFailureReason::SnapshotLoadFailed, 5),
          "announced mapping/load failure is recorded");
    Check(state.state == ffui::SnapshotViewState::StaleWithError && state.lastSuccessfulGeneration == 4,
          "mapping/load failure keeps same-context data stale");
    Check(!state.AcceptGeneration(3), "an older generation cannot replace newer accepted state");
    Check(state.state == ffui::SnapshotViewState::StaleWithError,
          "older out-of-order data cannot clear a newer stale state");
    Check(state.AcceptGeneration(5), "the later valid generation is accepted");
    Check(state.state == ffui::SnapshotViewState::Current,
          "accepted newer generation restores current state");
}

void TestRepeatedFailureDoesNotAccumulateState() {
    ffui::SnapshotFreshness state;
    state.AcceptGeneration(1);
    state.Fail(ffui::SnapshotFailureReason::EnumerationFailed);
    state.Fail(ffui::SnapshotFailureReason::EnumerationFailed);
    Check(state.state == ffui::SnapshotViewState::StaleWithError &&
              state.failure == ffui::SnapshotFailureReason::EnumerationFailed &&
              state.lastSuccessfulGeneration == 1,
          "repeated identical failure is one stable current-state result");
}

} // namespace

int main() {
    TestInitialSuccessAndEmptySuccess();
    TestSameContextStaleAndRecovery();
    TestContextIsolation();
    TestMappingFailureAndGenerationOrdering();
    TestRepeatedFailureDoesNotAccumulateState();
    return FailureCount() == 0 ? 0 : 1;
}
