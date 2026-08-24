#include <cstdint>

#include "ffui/AsyncResultGate.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {

void TestSupersessionAcrossQueryAndSourceTransitions() {
    ffui::AsyncResultGate gate;
    uint64_t source = 1;
    for (uint64_t transition = 0; transition < 150; ++transition) {
        const auto first = gate.Begin(source);
        Check(gate.Accept(first), "the newest request is accepted before supersession");

        const auto second = gate.Begin(source);
        Check(!gate.Accept(first), "a superseded query result is rejected");
        Check(gate.Accept(second), "the replacement query result is accepted");

        ++source;
        const auto reconnect = gate.Begin(source);
        Check(!gate.Accept(second), "a result from the previous engine source is rejected");
        Check(gate.Accept(reconnect), "a result from the replacement engine source is accepted");

        const auto cancelled = gate.Invalidate(source);
        Check(!gate.Accept(reconnect), "a cancelled request cannot become visible later");
        Check(gate.Accept(cancelled), "the cancellation boundary becomes the only current token");
    }
}

void TestZeroTokenCannotBeAccepted() {
    ffui::AsyncResultGate gate;
    Check(!gate.Accept({0, 0}), "an uninitialized async result token is rejected");
    const auto token = gate.Begin(42);
    Check(token.request != 0 && token.source == 42, "a token carries a nonzero request and source identity");
}

} // namespace

int main() {
    TestSupersessionAcrossQueryAndSourceTransitions();
    TestZeroTokenCannotBeAccepted();
    return FailureCount() == 0 ? 0 : 1;
}
