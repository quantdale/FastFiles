#pragma once

#include <windows.h>

namespace ffui {

// Debug-only WM_COPYDATA request/response identifiers for the narrow runtime
// selection-identity acceptance probe. The query is read-only and does not
// expose a control path into the UI.
inline constexpr ULONG_PTR kSelectionDiagnosticsQuery = 0x46465351u;
inline constexpr ULONG_PTR kSelectionDiagnosticsResponse = 0x46465352u;

} // namespace ffui
