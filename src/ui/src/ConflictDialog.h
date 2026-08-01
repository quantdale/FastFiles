#pragma once

#include "FileOperationPolicy.h"

#include <windows.h>

namespace ffui {

ConflictDecision ShowConflictDialog(HWND owner, const std::wstring& source, const std::wstring& destination);

} // namespace ffui
