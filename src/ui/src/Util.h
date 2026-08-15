#pragma once

#include <string>
#include <windows.h>

#include "ffprotocol/UiProtocol.h"

namespace ffui {

std::wstring FormatSize(uint64_t bytes);
std::wstring JoinPath(const std::wstring& base, const std::wstring& name);
// allocated-size-storage-accuracy 6.3/7.1: posts the additive aggregate
// payload (logical total + allocated subtotal + completeness pair) to the
// owner window; the receiver owns and deletes the posted object.
void PostFolderAggregateResult(HWND target, UINT msg, uint64_t requestId,
                               ffprotocol::FolderAggregateStatus status,
                               uint64_t itemCount, uint64_t totalSizeBytes,
                               uint64_t totalAllocatedSizeBytes,
                               uint64_t allocatedBytesKnownCount,
                               uint64_t allocatedBytesTotalCount);

} // namespace ffui
