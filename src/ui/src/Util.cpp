#include "Util.h"

#include <cmath>
#include <memory>

#include "ffprotocol/UiProtocol.h"

namespace ffui {

std::wstring FormatSize(uint64_t bytes) {
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        wchar_t buffer[64]{};
        swprintf_s(buffer, L"%.1f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
        return buffer;
    }
    if (bytes >= 1024ULL * 1024ULL) {
        wchar_t buffer[64]{};
        swprintf_s(buffer, L"%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
        return buffer;
    }
    if (bytes >= 1024ULL) {
        wchar_t buffer[64]{};
        swprintf_s(buffer, L"%.1f KB", static_cast<double>(bytes) / 1024.0);
        return buffer;
    }
    return std::to_wstring(bytes) + L" B";
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
