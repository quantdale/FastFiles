#pragma once

#include <windows.h>

namespace ffui {

// preserve-ui-quit-through-message-drains: PeekMessage returns WM_QUIT even
// outside the requested range. Never interpret its exit code as a payload,
// and repost only after draining so the same quit cannot be consumed forever.
template<class Dispose>
void DrainOwnedMessages(HWND window, UINT messageId, Dispose dispose) {
    bool quitSeen = false;
    int exitCode = 0;
    MSG pending{};
    while (PeekMessageW(&pending, window, messageId, messageId, PM_REMOVE)) {
        if (pending.message == WM_QUIT) {
            quitSeen = true;
            exitCode = static_cast<int>(pending.wParam);
        } else if (pending.message == messageId) {
            dispose(pending);
        }
    }
    if (quitSeen) PostQuitMessage(exitCode);
}

} // namespace ffui
