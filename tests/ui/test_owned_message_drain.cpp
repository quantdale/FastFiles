#include "OwnedMessageDrain.h"
#include "../TestSupport.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {
constexpr UINT kOwned = WM_APP + 41;
constexpr UINT kOther = WM_APP + 42;

void Verify(HWND target, HWND other, int exitCode, bool legacy, bool unrelated = true) {
    fftest::Check(PostMessageW(target, kOwned, 0, 101) != FALSE, "queue first owned payload");
    if (unrelated) {
        fftest::Check(PostMessageW(other, kOwned, 0, 303) != FALSE, "queue unrelated window payload");
        fftest::Check(PostMessageW(target, kOther, 0, 404) != FALSE, "queue unrelated message");
    }
    PostQuitMessage(exitCode);
    fftest::Check(PostMessageW(target, kOwned, 0, 202) != FALSE, "queue second owned payload");
    int disposed = 0;
    auto dispose = [&](const MSG& message) {
        fftest::Check(message.message == kOwned && message.hwnd == target,
                      "only expected window message reaches payload disposal");
        fftest::Check(message.lParam == 101 || message.lParam == 202,
                      "quit or unrelated data never reaches payload disposal");
        ++disposed;
    };
    if (legacy) {
        // Exact old filtered drain, with safe observer instead of deleting
        // an untrusted WM_QUIT lParam, makes the regression fail deterministically.
        MSG pending{};
        while (PeekMessageW(&pending, target, kOwned, kOwned, PM_REMOVE)) dispose(pending);
    } else {
        ffui::DrainOwnedMessages(target, kOwned, dispose);
    }
    fftest::Check(disposed == 2, "both owned payloads disposed exactly once");
    MSG pending{};
    if (unrelated) {
        fftest::Check(PeekMessageW(&pending, other, kOwned, kOwned, PM_REMOVE) != FALSE && pending.lParam == 303,
                      "unrelated window payload remains queued");
        fftest::Check(PeekMessageW(&pending, target, kOther, kOther, PM_REMOVE) != FALSE && pending.lParam == 404,
                      "unrelated message remains queued");
    }
    const bool quit = PeekMessageW(&pending, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE) != FALSE;
    fftest::Check(quit && pending.message == WM_QUIT && static_cast<int>(pending.wParam) == exitCode,
                  "outer loop retains original quit and exit code");
    fftest::Check(!PeekMessageW(&pending, nullptr, 0, 0, PM_REMOVE), "fixture leaves message queue empty");
}
} // namespace

int main(int argc, char** argv) {
    const bool legacy = argc == 2 && std::string_view(argv[1]) == "--legacy";
    HWND target = CreateWindowExW(0, L"STATIC", L"owned drain", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    HWND other = CreateWindowExW(0, L"STATIC", L"other drain", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    fftest::Check(target != nullptr && other != nullptr, "own message-only windows created");
    if (target && other) {
        Verify(target, other, 0, legacy);
        Verify(target, other, 37, legacy);
        Verify(target, other, 0, legacy, false);
        Verify(target, other, 37, legacy, false);
        // Repeated destructor/child cleanup must preserve an already pending quit.
        PostQuitMessage(19);
        ffui::DrainOwnedMessages(target, kOwned, [](const MSG&) { fftest::Check(false, "empty drain has no payload"); });
        ffui::DrainOwnedMessages(target, kOwned, [](const MSG&) { fftest::Check(false, "repeated drain has no payload"); });
        MSG quit{};
        fftest::Check(GetMessageW(&quit, nullptr, 0, 0) == 0 && quit.wParam == 19, "real outer GetMessage receives preserved quit");
    }
    if (target) fftest::Check(DestroyWindow(target) != FALSE, "target normal destruction");
    if (other) fftest::Check(DestroyWindow(other) != FALSE, "other normal destruction");
    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
