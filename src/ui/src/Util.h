#pragma once

#include <string>
#include <windows.h>

#include "ffprotocol/UiProtocol.h"

namespace ffui {

std::wstring FormatSize(uint64_t bytes);
std::wstring JoinPath(const std::wstring& base, const std::wstring& name);
void PostFolderAggregateResult(HWND target, UINT msg, uint64_t requestId,
                               ffprotocol::FolderAggregateStatus status,
                               uint64_t itemCount, uint64_t totalSizeBytes);

} // namespace ffui
