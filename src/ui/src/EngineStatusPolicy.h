#pragma once

#include "EngineClient.h"

namespace ffui {

struct EngineStatusPresentation {
    const wchar_t* text;
    bool privilegedPathActive;
};

// recovery-degraded-browsing 3.1: keep the status chip's state-to-text
// contract independent from Direct2D so every connection state is testable.
constexpr EngineStatusPresentation GetEngineStatusPresentation(
    EngineConnectionState state, bool privilegedPathActive) noexcept {
    switch (state) {
        case EngineConnectionState::Connected:
            return privilegedPathActive
                       ? EngineStatusPresentation{L"Instant search: enabled", true}
                       : EngineStatusPresentation{L"Instant search: basic (degraded mode)", false};
        case EngineConnectionState::Starting:
            return EngineStatusPresentation{L"Starting engine…", false};
        case EngineConnectionState::Unavailable:
            return EngineStatusPresentation{L"Engine unavailable — click to retry", false};
    }

    return EngineStatusPresentation{L"Engine unavailable — click to retry", false};
}

} // namespace ffui
