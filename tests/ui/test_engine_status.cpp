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

    Check(std::wstring(degraded.compactText).find(L"degraded") != std::wstring::npos,
          "compact basic-search badge preserves explicit degraded meaning");

    const auto unavailable = ffui::GetEngineStatusPresentation(ffui::EngineConnectionState::Unavailable, true);
    Check(std::wstring(unavailable.text) == L"Engine unavailable — click to retry" &&
              !unavailable.privilegedPathActive,
          "Unavailable state uses the retry status text and cannot claim privilege");
    Check(std::wstring(unavailable.compactText).find(L"Retry") != std::wstring::npos,
          "compact unavailable badge retains its recovery action");
    for (const auto state : {ffui::EngineConnectionState::Starting, ffui::EngineConnectionState::Unavailable}) {
        const auto stale = ffui::GetEngineStatusPresentation(state, true);
        Check(!stale.privilegedPathActive && std::wstring(stale.compactText).find(L"Indexed") == std::wstring::npos,
              "stale privileged flag cannot advertise indexed search before connection");
    }
}

} // namespace

int main() {
    TestStateText();
    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
