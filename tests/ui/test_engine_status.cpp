#include "EngineStatusPolicy.h"

#include <cstdlib>
#include <string>

#include "../TestSupport.h"

using namespace fftest;

namespace {

void TestStateText() {
    const auto starting = ffui::GetEngineStatusPresentation(ffui::EngineConnectionState::Starting, false);
    Check(std::wstring(starting.text) == L"Starting engine…" && !starting.privilegedPathActive,
          "Starting state uses the startup status text");

    const auto active = ffui::GetEngineStatusPresentation(ffui::EngineConnectionState::Connected, true);
    Check(std::wstring(active.text) == L"Instant search: enabled" && active.privilegedPathActive,
          "Connected privileged state uses the enabled status text");

    const auto degraded = ffui::GetEngineStatusPresentation(ffui::EngineConnectionState::Connected, false);
    Check(std::wstring(degraded.text) == L"Instant search: basic (degraded mode)" &&
              !degraded.privilegedPathActive,
          "Connected degraded state uses the degraded status text");

    const auto unavailable = ffui::GetEngineStatusPresentation(ffui::EngineConnectionState::Unavailable, true);
    Check(std::wstring(unavailable.text) == L"Engine unavailable — click to retry" &&
              !unavailable.privilegedPathActive,
          "Unavailable state uses the retry status text and cannot claim privilege");
}

} // namespace

int main() {
    TestStateText();
    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
