#include "CommandSystem.h"

#include <array>
#include <iostream>
#include <string>
#include <vector>
#include <windows.h>

#include "../TestSupport.h"

namespace {

ffui::ShortcutMap DefaultShortcuts() {
    ffui::CommandRegistry registry;
    const ffui::BaselineHandlers handlers{[](const std::wstring&) {
        return [](const std::vector<std::wstring>&) {};
    }};
    ffui::RegisterBaselineCommands(registry, handlers);
    ffui::ShortcutMap shortcuts;
    shortcuts.ResetDefaults(registry);
    return shortcuts;
}

int EligibleRouteCount(const ffui::ShortcutMap& shortcuts, ffui::KeyChord chord, bool nativeEditFocused) {
    int count = 0;
    for (const ffui::ShortcutScope scope : {ffui::ShortcutScope::Global, ffui::ShortcutScope::ActiveView}) {
        const auto* binding = shortcuts.FindByChord(chord, scope);
        if (binding != nullptr && ffui::IsShortcutDispatchAllowed(binding->scope, nativeEditFocused)) ++count;
    }
    return count;
}

void TestGlobalBindingsRemainAvailableFromNativeEdit() {
    const ffui::ShortcutMap shortcuts = DefaultShortcuts();
    struct Case { const wchar_t* commandId; ffui::KeyChord chord; };
    const std::array<Case, 3> cases{{
        {L"search.focus", {'F', ffui::ModifierControl}},
        {L"navigation.focus-path", {'L', ffui::ModifierControl}},
        {L"app.command-palette", {'P', static_cast<uint8_t>(ffui::ModifierControl | ffui::ModifierShift)}},
    }};

    for (const Case& testCase : cases) {
        const auto* binding = shortcuts.FindByChord(testCase.chord, ffui::ShortcutScope::Global);
        fftest::Check(binding != nullptr && binding->commandId == testCase.commandId,
                      "actual global binding is registered in the Global scope");
        fftest::Check(binding != nullptr && ffui::IsShortcutDispatchAllowed(binding->scope, true),
                      "global binding remains dispatchable from native edit focus");
        fftest::Check(EligibleRouteCount(shortcuts, testCase.chord, true) == 1,
                      "global binding has exactly one eligible route from native edit focus");
    }
}

void TestNativeEditBindingsRemainWithTheEdit() {
    const ffui::ShortcutMap shortcuts = DefaultShortcuts();
    struct Case { const wchar_t* commandId; ffui::KeyChord chord; };
    const std::array<Case, 5> cases{{
        {L"file.copy", {'C', ffui::ModifierControl}},
        {L"file.paste", {'V', ffui::ModifierControl}},
        {L"file.cut", {'X', ffui::ModifierControl}},
        {L"selection.select-all", {'A', ffui::ModifierControl}},
        {L"file.undo", {'Z', ffui::ModifierControl}},
    }};

    for (const Case& testCase : cases) {
        const auto* binding = shortcuts.FindByChord(testCase.chord, ffui::ShortcutScope::ActiveView);
        fftest::Check(binding != nullptr && binding->commandId == testCase.commandId,
                      "native-edit chord is an actual ActiveView binding");
        fftest::Check(binding != nullptr && !ffui::IsShortcutDispatchAllowed(binding->scope, true),
                      "native-edit chord is not dispatched by Fast Files while editing");
        fftest::Check(EligibleRouteCount(shortcuts, testCase.chord, true) == 0,
                      "native-edit chord has no Fast Files route while editing");
    }
}

void TestViewLocalAndNonEditRouting() {
    const ffui::ShortcutMap shortcuts = DefaultShortcuts();
    const ffui::KeyChord refresh{'F', ffui::ModifierControl};
    const auto* refreshBinding = shortcuts.FindByChord({'F', ffui::ModifierControl}, ffui::ShortcutScope::Global);
    fftest::Check(refreshBinding != nullptr && refreshBinding->commandId == L"search.focus",
                  "Ctrl+F remains the global search binding");
    fftest::Check(ffui::IsShortcutDispatchAllowed(ffui::ShortcutScope::ActiveView, true) == false,
                  "ActiveView policy suppresses view/file commands during text editing");

    const auto* viewBinding = shortcuts.FindByChord({VK_F5, ffui::ModifierNone}, ffui::ShortcutScope::ActiveView);
    fftest::Check(viewBinding != nullptr && viewBinding->commandId == L"navigation.refresh",
                  "F5 is an actual view-local binding");
    fftest::Check(EligibleRouteCount(shortcuts, {VK_F5, ffui::ModifierNone}, true) == 0,
                  "view-local F5 is suppressed while a native edit owns focus");
    fftest::Check(EligibleRouteCount(shortcuts, refresh, false) == 1,
                  "global search remains routed outside native edit focus");
    fftest::Check(EligibleRouteCount(shortcuts, {VK_F5, ffui::ModifierNone}, false) == 1,
                  "view-local routing remains unchanged outside native edit focus");

    const ffui::KeyChord unknown{'Q', ffui::ModifierControl};
    fftest::Check(shortcuts.FindByChord(unknown, ffui::ShortcutScope::Global) == nullptr &&
                      shortcuts.FindByChord(unknown, ffui::ShortcutScope::ActiveView) == nullptr,
                  "unknown or unbound chord has no command");
}

void TestNoDoubleDispatch() {
    const ffui::ShortcutMap shortcuts = DefaultShortcuts();
    const ffui::KeyChord palette{'P', static_cast<uint8_t>(ffui::ModifierControl | ffui::ModifierShift)};
    fftest::Check(EligibleRouteCount(shortcuts, palette, true) == 1,
                  "native-edit global routing produces one semantic dispatch");
    fftest::Check(EligibleRouteCount(shortcuts, palette, false) == 1,
                  "non-edit global routing produces one semantic dispatch");
}

} // namespace

int main() {
    TestGlobalBindingsRemainAvailableFromNativeEdit();
    TestNativeEditBindingsRemainWithTheEdit();
    TestViewLocalAndNonEditRouting();
    TestNoDoubleDispatch();
    if (fftest::FailureCount() == 0) std::cout << "All shortcut routing tests passed\n";
    return fftest::FailureCount() == 0 ? 0 : 1;
}
