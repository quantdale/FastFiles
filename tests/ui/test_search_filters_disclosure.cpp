// completion-integrity-hardening-and-release-certification 4.5.2: headless
// coverage for the Search Filters disclosure identity seam. Pins the stable
// AutomationId, control-id mapping, and expand/collapse state so the live UIA
// harness can Expand without toggling a successful expansion.

#include "ffui/SearchFiltersDisclosure.h"

#include <string>

#include "../TestSupport.h"

using namespace fftest;

namespace {
int Runtime(int value) {
    volatile int opaque = value;
    return opaque;
}
bool Runtime(bool value) {
    volatile bool opaque = value;
    return opaque;
}
}  // namespace

namespace {

void TestAutomationIdAndControlId() {
    Check(Runtime(ffui::kSearchFiltersControlId) == 7110, "Filters control id is 7110");
    Check(std::wstring(ffui::kSearchFiltersAutomationId) == L"Search.Filters",
          "Filters AutomationId is Search.Filters");
    Check(ffui::SearchButtonAutomationId(Runtime(7110)) == ffui::kSearchFiltersAutomationId,
          "control id 7110 maps to Search.Filters");
    Check(ffui::SearchButtonAutomationId(Runtime(7105)) == nullptr,
          "Clear history keeps decimal HWND AutomationId");
    Check(ffui::SearchButtonAutomationId(Runtime(7107)) == nullptr,
          "sort direction keeps decimal HWND AutomationId");
}

void TestExpandCollapseMapping() {
    Check(ffui::ToSearchFiltersDisclosureState(Runtime(false)) ==
              ffui::SearchFiltersDisclosureState::Collapsed,
          "collapsed bool maps to Collapsed");
    Check(ffui::ToSearchFiltersDisclosureState(Runtime(true)) ==
              ffui::SearchFiltersDisclosureState::Expanded,
          "expanded bool maps to Expanded");
}

}  // namespace

int main() {
    TestAutomationIdAndControlId();
    TestExpandCollapseMapping();
    if (FailureCount() != 0) {
        fprintf(stderr, "ffsearch_filters_disclosure_tests: %d check(s) failed\n", FailureCount());
        return 1;
    }
    printf("ffsearch_filters_disclosure_tests: all checks passed\n");
    return 0;
}
