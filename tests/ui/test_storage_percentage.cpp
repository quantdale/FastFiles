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

void TestParentCompositionAndRounding() {
    const auto fileA = ffui::ComputeStoragePercentage(10, 100);
    const auto fileB = ffui::ComputeStoragePercentage(20, 100);
    const auto folder = ffui::ComputeStoragePercentage(70, 100);
    Check(fileA.display == L"10.0%" && fileB.display == L"20.0%" && folder.display == L"70.0%",
          "mixed file and directory children use the complete parent total");

    const auto onlyFilesA = ffui::ComputeStoragePercentage(30, 100);
    const auto onlyFilesB = ffui::ComputeStoragePercentage(70, 100);
    Check(onlyFilesA.display == L"30.0%" && onlyFilesB.display == L"70.0%",
          "file-only parents produce parent-relative percentages");

    const auto onlyFoldersA = ffui::ComputeStoragePercentage(40, 100);
    const auto onlyFoldersB = ffui::ComputeStoragePercentage(60, 100);
    Check(onlyFoldersA.display == L"40.0%" && onlyFoldersB.display == L"60.0%",
          "directory-only parents produce parent-relative percentages");

    const auto roundedA = ffui::ComputeStoragePercentage(1, 7);
    const auto roundedB = ffui::ComputeStoragePercentage(2, 7);
    const auto roundedC = ffui::ComputeStoragePercentage(4, 7);
    Check(roundedA.display == L"14.3%" && roundedB.display == L"28.6%" && roundedC.display == L"57.1%",
          "one-decimal rounding stays within the documented cumulative tolerance");
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
    Check(!ffui::ComputeStoragePercentage(100, 100, false).known &&
              ffui::ComputeStoragePercentage(100, 100, false).display == L"—",
          "a root or no-parent row displays an em dash");
}

void TestLargeValuesAndOrdering() {
    constexpr uint64_t kTerabyte = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
    const auto first = ffui::ComputeStoragePercentage(10ULL * kTerabyte, 50ULL * kTerabyte);
    const auto second = ffui::ComputeStoragePercentage(40ULL * kTerabyte, 50ULL * kTerabyte);
    Check(first.known && second.known && first.display == L"20.0%" && second.display == L"80.0%",
          "multi-terabyte values do not overflow percentage computation");
    Check(!ffui::StoragePercentageNearlyEqual(first.sortKey, second.sortKey),
          "distinct percentage sort keys remain distinct");
    Check(ffui::StoragePercentageNearlyEqual(first.sortKey, first.sortKey),
          "equal percentage sort keys compare equal");
    Check(ffui::StoragePercentageNearlyEqual(1.0 / 3.0, 1.0 / 3.0 + 1e-13),
          "near-equal percentages use an explicit comparison tolerance");

    const auto hugeLow = ffui::ComputeStoragePercentage(UINT64_MAX / 3, UINT64_MAX);
    const auto hugeHigh = ffui::ComputeStoragePercentage(UINT64_MAX / 2, UINT64_MAX);
    Check(hugeLow.known && hugeHigh.known && hugeLow.sortKey < hugeHigh.sortKey,
          "overflow-scale percentage sorting remains ordered without integer multiplication");
}

} // namespace

int main() {
    TestKnownValues();
    TestParentCompositionAndRounding();
    TestUnknownAndZeroValues();
    TestLargeValuesAndOrdering();
    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
