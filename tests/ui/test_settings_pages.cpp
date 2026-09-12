// Headless regression for Settings tab membership and native overlay
// z-order layout flags. The Indexing page previously hid the rule list and
// exclude checkbox and never showed them again.

#include "ffui/NativeOverlayLayout.h"
#include "ffui/SettingsPages.h"

#include <cstdlib>
#include "../TestSupport.h"

using namespace fftest;
using namespace ffui;

namespace {

void TestIndexingPageIncludesRuleChrome() {
    Check(SettingsControlOnPage(kSettingsRuleList, 3),
          "Indexing page includes the include/exclude rule list");
    Check(SettingsControlOnPage(kSettingsExcludeRule, 3),
          "Indexing page includes the Exclude rule checkbox");
    Check(SettingsControlOnPage(kSettingsVolumeList, 3),
          "Indexing page includes the volume list");
    Check(!SettingsControlOnPage(kSettingsRuleList, 0),
          "General page does not include the rule list");
    Check(!SettingsControlOnPage(kSettingsExcludeRule, 1),
          "Search page does not include the Exclude rule checkbox");
}

void TestGeneralPageKeepsDensityAndTheme() {
    Check(SettingsControlOnPage(kSettingsThemeCombo, 0), "General page includes Theme");
    Check(SettingsControlOnPage(kSettingsDensityCombo, 0), "General page includes Density");
    Check(SettingsControlOnPage(kSettingsPreviewCheck, 0), "General page includes preview");
    Check(!SettingsControlOnPage(kSettingsThemeCombo, 3),
          "Indexing page does not include Theme");
}

void TestGeneralPageCarriesRowGrammar() {
    // ui-overhaul phase 10: the General page owns its section headers and
    // helper text, and no other page shows that chrome.
    Check(SettingsControlOnPage(kSettingsAppearanceHeader, 0) &&
              SettingsControlOnPage(kSettingsAppearanceHint, 0),
          "General page includes the Appearance header and helper text");
    Check(SettingsControlOnPage(kSettingsPreviewHeader, 0) &&
              SettingsControlOnPage(kSettingsPreviewSizeHint, 0),
          "General page includes the Preview header and size helper text");
    Check(!SettingsControlOnPage(kSettingsAppearanceHeader, 1) &&
              !SettingsControlOnPage(kSettingsPreviewSizeHint, 3),
          "section headers and helper text stay off other pages");
}

void TestUnknownPageShowsNothing() {
    Check(SettingsPageVisibleControls(-1) == 0, "unknown negative page shows no controls");
    Check(SettingsPageVisibleControls(6) == 0, "page index past Shortcuts shows no controls");
}

void TestNativeOverlayLayoutKeepsZOrder() {
    // Load through locals so /analyze C6326 does not treat the flag contract
    // as a constant-vs-constant comparison.
    UINT workspaceFlags = 0;
    UINT paletteFlags = 0;
    workspaceFlags |= kNativeWorkspaceLayoutFlags;
    paletteFlags |= kPaletteOverlayLayoutFlags;
    Check((workspaceFlags & SWP_NOZORDER) != 0,
          "Search/Storage layout must not raise z-order over the palette");
    Check((workspaceFlags & SWP_NOACTIVATE) != 0,
          "Search/Storage layout does not activate on reposition");
    Check((paletteFlags & SWP_SHOWWINDOW) != 0,
          "palette overlay layout shows the dimmer/edit/list");
    Check((paletteFlags & SWP_NOZORDER) == 0,
          "palette overlay is allowed to raise above workspace lists");
}

}  // namespace

int main() {
    TestIndexingPageIncludesRuleChrome();
    TestGeneralPageKeepsDensityAndTheme();
    TestGeneralPageCarriesRowGrammar();
    TestUnknownPageShowsNothing();
    TestNativeOverlayLayoutKeepsZOrder();
    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
