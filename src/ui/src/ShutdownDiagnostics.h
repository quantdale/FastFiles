#pragma once

#include <windows.h>
#include "ffprotocol/Diagnostics.h"

namespace ffui {

// original-vision-journey-recovery 4.8: opt-in metadata only; no paths/content.
inline void AppendShutdownPhase(const wchar_t* phase, const wchar_t* outcome) {
    wchar_t value[8]{};
    const DWORD length = GetEnvironmentVariableW(L"FASTFILES_UIA_DIAGNOSTICS", value, 8);
    if (length != 1 || value[0] != L'1') return;
    ffprotocol::DiagnosticEvent event{};
    event.category = ffprotocol::DiagnosticCategory::UiInteraction;
    event.code = ffprotocol::DiagnosticCode::FFDIAG_UI_SHUTDOWN_PHASE;
    event.state = phase;
    event.outcome = outcome;
    ffprotocol::AppendDiagnostic(event, ffprotocol::DiagnosticFormat::Json);
}

} // namespace ffui
