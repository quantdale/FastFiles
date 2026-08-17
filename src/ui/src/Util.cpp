#include "Util.h"

#include <cmath>
#include <memory>

#include "ffprotocol/UiProtocol.h"
#include "ffui/TreemapSemantics.h"

namespace ffui {

std::wstring FormatSize(uint64_t bytes) {
    // semantic-treemap-uia 7.x: one canonical size formatter lives in the
    // pure TreemapSemantics header so hover text, the storage ListView, and
    // UIA metadata can never drift.
    return TreemapFormatSize(bytes);
}

std::wstring JoinPath(const std::wstring& base, const std::wstring& name) {
    if (base.empty()) return name;
    if (!base.empty() && base.back() == L'\\') {
        return base + name;
    }
    return base + L'\\' + name;
}

void PostFolderAggregateResult(HWND target, UINT msg, uint64_t requestId,
                               ffprotocol::FolderAggregateStatus status,
                               uint64_t itemCount, uint64_t totalSizeBytes,
                               uint64_t totalAllocatedSizeBytes,
                               uint64_t allocatedBytesKnownCount,
                               uint64_t allocatedBytesTotalCount) {
    auto payload = std::make_unique<ffprotocol::FolderAggregateResultV2Payload>(
        ffprotocol::FolderAggregateResultV2Payload{requestId, status, itemCount, totalSizeBytes,
                                                   totalAllocatedSizeBytes, allocatedBytesKnownCount,
                                                   allocatedBytesTotalCount});
    if (target != nullptr
        && PostMessageW(target, msg, 0,
                        reinterpret_cast<LPARAM>(payload.get()))) {
        payload.release();
    }
}

} // namespace ffui
