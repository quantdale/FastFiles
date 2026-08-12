#include "ffui/DetailsAggregateState.h"

#include <cstdlib>

#include "../TestSupport.h"

using namespace fftest;

namespace {

ffui::FolderAggregateKey FirstFolder() {
    return {7, 100, 200};
}

ffui::FolderAggregateKey SecondFolder() {
    return {7, 101, 201};
}

void TestInitialAndCompletedRequest() {
    ffui::FolderAggregateRequestState state;
    const auto folder = FirstFolder();

    Check(state.NeedsRequest(folder), "an uninitialized aggregate state requests the selected folder");
    state.Begin(folder);
    state.SetRequestId(41);
    Check(!state.NeedsRequest(folder), "a completed request key is reused for the same folder");
    Check(state.Accepts(41), "the current aggregate request accepts its response");
    Check(!state.Accepts(40), "an unrelated aggregate response is rejected");
}

void TestInvalidationForcesRefreshAndRejectsLateResult() {
    ffui::FolderAggregateRequestState state;
    const auto folder = FirstFolder();
    state.Begin(folder);
    state.SetRequestId(41);

    state.Invalidate();
    Check(state.NeedsRequest(folder), "snapshot or selection invalidation forces a fresh aggregate request");
    Check(!state.Accepts(41), "a response posted before invalidation is rejected");
}

void TestChangingFolderRejectsPreviousRequest() {
    ffui::FolderAggregateRequestState state;
    state.Begin(FirstFolder());
    state.SetRequestId(41);
    state.Begin(SecondFolder());
    state.SetRequestId(42);

    Check(state.NeedsRequest(FirstFolder()), "returning to a previous folder requires its current aggregate");
    Check(!state.Accepts(41), "a previous folder response cannot satisfy the new selection");
    Check(state.Accepts(42), "the new folder response remains accepted");
}

} // namespace

int main() {
    TestInitialAndCompletedRequest();
    TestInvalidationForcesRefreshAndRejectsLateResult();
    TestChangingFolderRejectsPreviousRequest();
    return FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
