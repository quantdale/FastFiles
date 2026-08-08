#include "ffui/StoragePercentage.h"

#include <cmath>
#include <cstdlib>

#include "../TestSupport.h"

using namespace fftest;

namespace {

void TestKnownValues() {
    const auto file = ffui::ComputeStoragePercentage(25, 100);
    const auto folder = ffui::ComputeStoragePercentage(75, 100);
    Check(file.known && file.display == L"25.0%", "file percentage formats from complete parent total");
    Check(folder.known && folder.display == L"75.0%", "directory percentage formats from complete parent total");
    Check(std::fabs(file.sortKey + folder.sortKey - 1.0) < 1e-12,
          "file and directory ratios sum to one");
}

void TestUnknownAndZeroValues() {
    Check(!ffui::ComputeStoragePercentage(0, 100).known &&
              ffui::ComputeStoragePercentage(0, 100).display == L"—",
          "zero-size child displays an em dash");
    Check(!ffui::ComputeStoragePercentage(10, 0).known &&
              ffui::ComputeStoragePercentage(10, 0).display == L"—",
          "zero-size parent displays an em dash");
    Check(!ffui::ComputeStoragePercentage(10, 100, false).known,
          "pending child aggregate stays unknown");
}

void TestLargeValuesAndOrdering() {
    constexpr uint64_t kTerabyte = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
    const auto first = ffui::ComputeStoragePercentage(10ULL * kTerabyte, 50ULL * kTerabyte);
    const auto second = ffui::ComputeStoragePercentage(40ULL * kTerabyte, 50ULL * kTerabyte);
    Check(first.known && second.known && first.display == L"20.0%" && second.display == L"80.0%",
          "multi-terabyte values do not overflow percentage computation");
    Check(!ffui::StoragePercentageNearlyEqual(first.sortKey, second.sortKey),
          "distinct percentage sort keys remain distinct");
    Check(ffui::StoragePercentageNearlyEqual(1.0 / 3.0, 1.0 / 3.0 + 1e-13),
          "near-equal percentages use an explicit comparison tolerance");
}

} // namespace

int main() {
    TestKnownValues();
    TestUnknownAndZeroValues();
    TestLargeValuesAndOrdering();
    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
