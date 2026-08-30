#include "QuickActions.h"

#include "../TestSupport.h"

using namespace fftest;

int main() {
    const std::vector<std::wstring> published{L"C:\\Fixture\\Moved.txt", L"D:\\Fixture\\Other.txt"};
    const std::vector<std::wstring> shellCaseVariant{L"c:\\fixture\\moved.txt", L"d:\\fixture\\other.txt"};
    const std::vector<std::wstring> changed{L"C:\\Fixture\\Changed.txt", L"D:\\Fixture\\Other.txt"};

    Check(ffui::CanUseLocalCutClipboardFallback(shellCaseVariant, published, false, true, 42, 42),
          "same-sequence case-insensitive paths preserve the app's cut intent");
    Check(!ffui::CanUseLocalCutClipboardFallback(published, published, false, true, 43, 42),
          "a later clipboard publication cannot inherit the previous cut intent");
    Check(!ffui::CanUseLocalCutClipboardFallback(changed, published, false, true, 42, 42),
          "a changed path list cannot inherit the previous cut intent");
    Check(!ffui::CanUseLocalCutClipboardFallback(published, published, true, true, 42, 42),
          "an explicit external cut does not use the app-local fallback");
    Check(!ffui::CanUseLocalCutClipboardFallback(published, published, false, false, 42, 42),
          "a copy publication never becomes a cut through the fallback");
    Check(!ffui::CanUseLocalCutClipboardFallback(published, published, false, true, 0, 0),
          "an unavailable clipboard sequence cannot prove ownership");
    return FailureCount();
}
