#include "UiaHostQuery.h"

#include <algorithm>
#include <mutex>
#include <vector>

namespace ffui {
namespace {

std::mutex gHostQueryMutex;
std::vector<HWND> gHostQueryHwnds;

void PushHostQuery(HWND hwnd) {
    std::lock_guard<std::mutex> lock(gHostQueryMutex);
    gHostQueryHwnds.push_back(hwnd);
}

void PopHostQuery(HWND hwnd) {
    std::lock_guard<std::mutex> lock(gHostQueryMutex);
    const auto found = std::find(gHostQueryHwnds.rbegin(), gHostQueryHwnds.rend(), hwnd);
    if (found != gHostQueryHwnds.rend()) {
        gHostQueryHwnds.erase(std::next(found).base());
    }
}

}  // namespace

UiaHostQueryGuard::UiaHostQueryGuard() noexcept : hwnd_(nullptr) { PushHostQuery(nullptr); }

UiaHostQueryGuard::UiaHostQueryGuard(HWND hwnd) noexcept : hwnd_(hwnd) { PushHostQuery(hwnd); }

UiaHostQueryGuard::~UiaHostQueryGuard() noexcept { PopHostQuery(hwnd_); }

bool UiaHostQueryGuard::Active() noexcept {
    std::lock_guard<std::mutex> lock(gHostQueryMutex);
    return !gHostQueryHwnds.empty();
}

bool UiaHostQueryGuard::Active(HWND hwnd) noexcept {
    if (hwnd == nullptr) return Active();
    std::lock_guard<std::mutex> lock(gHostQueryMutex);
    for (HWND active : gHostQueryHwnds) {
        if (active == nullptr || active == hwnd) return true;
    }
    return false;
}

}  // namespace ffui
