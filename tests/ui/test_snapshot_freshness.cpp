#include <cstdint>

#include "ffui/SnapshotFreshness.h"
#include "ffprotocol/SnapshotFormat.h"
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

void TestIncompatibleFormatStartsFreshMappingSource() {
    ffui::SnapshotFreshness state;
    state.AcceptGeneration(7);
    Check(!ffprotocol::IsSnapshotFormatCompatible(ffprotocol::kSnapshotFormatVersion + 1),
          "an incompatible snapshot format is detected before parsing");

    // EngineClient clears its mapped section and waits for a new generation;
    // this is the UI freshness state that accompanies that re-map request.
    state.ResetForNewSource();
    Check(state.state == ffui::SnapshotViewState::Loading && state.requestedGeneration == 0 &&
              state.lastSuccessfulGeneration == 0,
          "an incompatible snapshot re-map starts a fresh source state");
}

// deterministic-missing-path-state D6: confirmed terminal path states
// (NotFound, AccessDenied) must force Unavailable even when a
// last-known-good generation exists, so the column clears its rows and
// shows the unavailable wording instead of stale-retained content.
void TestForceUnavailableClearsWithLKG() {
    ffui::SnapshotFreshness state;
    state.AcceptGeneration(1);
    Check(state.HasLastKnownGood(), "baseline has last-known-good");
    Check(state.state == ffui::SnapshotViewState::Current, "baseline is current");

    // ForceUnavailable for NotFound overrides StaleWithError behavior
    Check(state.ForceUnavailable(ffui::SnapshotFailureReason::NoLongerAvailable, 2),
          "ForceUnavailable accepts generation 2");
    Check(state.state == ffui::SnapshotViewState::Unavailable,
          "ForceUnavailable sets Unavailable even with LKG");
    Check(state.HasLastKnownGood(), "LKG is preserved so recovery can later succeed");
    Check(state.failure == ffui::SnapshotFailureReason::NoLongerAvailable,
          "failure reason is recorded");

    // Recovery: successful generation clears the Unavailable state
    Check(state.AcceptGeneration(3), "later generation accepted after ForceUnavailable");
    Check(state.state == ffui::SnapshotViewState::Current,
          "successful recovery clears ForceUnavailable state");
    Check(state.failure == ffui::SnapshotFailureReason::None,
          "recovery clears failure reason");
}

void TestForceUnavailableRejectsOlderGeneration() {
    ffui::SnapshotFreshness state;
    state.AcceptGeneration(5);
    Check(!state.ForceUnavailable(ffui::SnapshotFailureReason::AccessDenied, 3),
          "ForceUnavailable rejects generation older than requested");
    Check(state.state == ffui::SnapshotViewState::Current,
          "old generation does not change state");
}

// deterministic-missing-path-state D6: regular Fail with LKG still
// produces StaleWithError for transient failures (EnumerationFailed,
// PublishFailed, etc.) -- only terminal states use ForceUnavailable.
void TestRegularFailStillStaleWithError() {
    ffui::SnapshotFreshness state;
    state.AcceptGeneration(1);
    Check(state.Fail(ffui::SnapshotFailureReason::EnumerationFailed),
          "transient failure is recorded");
    Check(state.state == ffui::SnapshotViewState::StaleWithError,
          "transient failure with LKG still produces StaleWithError");
}

} // namespace

int main() {
    TestInitialSuccessAndEmptySuccess();
    TestSameContextStaleAndRecovery();
    TestContextIsolation();
    TestMappingFailureAndGenerationOrdering();
    TestRepeatedFailureDoesNotAccumulateState();
    TestIncompatibleFormatStartsFreshMappingSource();
    TestForceUnavailableClearsWithLKG();
    TestForceUnavailableRejectsOlderGeneration();
    TestRegularFailStillStaleWithError();
    return FailureCount() == 0 ? 0 : 1;
}
