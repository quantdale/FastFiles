#include <cstdlib>
#include <string>

#include "../TestSupport.h"

using namespace fftest;

namespace {

void TestWideLiteralEncoding() {
    const std::wstring ellipsis = L"…";
    const std::wstring bullet = L"•";

    Check(ellipsis.size() == 1 && ellipsis[0] == L'\u2026',
          "UTF-8 source ellipsis remains U+2026 in a wide literal");
    Check(bullet.size() == 1 && bullet[0] == L'\u2022',
          "UTF-8 source bullet remains U+2022 in a wide literal");
}

} // namespace

int main() {
    TestWideLiteralEncoding();
    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
