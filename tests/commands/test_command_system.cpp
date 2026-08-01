#include "CommandSystem.h"
#include "QuickActions.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <windows.h>

namespace {
int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

ffui::BaselineHandlers CountingHandlers(int& invocations, std::wstring& lastCommand) {
    return {[&](const std::wstring& id) {
        return [&invocations, &lastCommand, id](const std::vector<std::wstring>&) {
            ++invocations;
            lastCommand = id;
        };
    }};
}

void TestRegistryAndSelection() {
    int invocations = 0;
    std::wstring lastCommand;
    ffui::CommandRegistry registry;
    Check(ffui::RegisterBaselineCommands(registry, CountingHandlers(invocations, lastCommand)),
          "baseline registration succeeds");
    Check(registry.Commands().size() >= 20, "baseline command set is complete");
    Check(!registry.Register({L"file.copy", L"Duplicate", L"Test", std::nullopt,
                              ffui::AppliesTo(ffui::SelectionKind::SingleFile),
                              ffui::ShortcutScope::ActiveView, {}, [](const auto&) {}}),
          "duplicate command ids are rejected");

    Check(ffui::ClassifySelection({}) == ffui::SelectionKind::Empty, "empty selection classified");
    const std::vector<ffui::SelectionItem> file{{L"C:\\a.txt", false}};
    const std::vector<ffui::SelectionItem> folder{{L"C:\\folder", true}};
    const std::vector<ffui::SelectionItem> files{{L"C:\\a", false}, {L"C:\\b", false}};
    const std::vector<ffui::SelectionItem> folders{{L"C:\\a", true}, {L"C:\\b", true}};
    const std::vector<ffui::SelectionItem> mixed{{L"C:\\a", false}, {L"C:\\b", true}};
    Check(ffui::ClassifySelection(file) == ffui::SelectionKind::SingleFile, "single file classified");
    Check(ffui::ClassifySelection(folder) == ffui::SelectionKind::SingleFolder, "single folder classified");
    Check(ffui::ClassifySelection(files) == ffui::SelectionKind::MultipleFiles, "multiple files classified");
    Check(ffui::ClassifySelection(folders) == ffui::SelectionKind::MultipleFolders, "multiple folders classified");
    Check(ffui::ClassifySelection(mixed) == ffui::SelectionKind::Mixed, "mixed selection classified");

    const auto singleFile = registry.Query({ffui::SelectionKind::SingleFile, false});
    const auto has = [&](const wchar_t* id) {
        return std::ranges::any_of(singleFile, [id](const auto* command) { return command->commandId == id; });
    };
    Check(has(L"item.open-with") && !has(L"item.open-terminal"), "file menu applicability is correct");
    Check(registry.Invoke(L"file.copy", {ffui::SelectionKind::SingleFile, false}, {L"C:\\a.txt"}),
          "applicable command invokes");
    Check(invocations == 1 && lastCommand == L"file.copy", "registry invokes the one registered handler");
}

void TestShortcutsAndPalette() {
    int invocations = 0;
    std::wstring lastCommand;
    ffui::CommandRegistry registry;
    ffui::RegisterBaselineCommands(registry, CountingHandlers(invocations, lastCommand));
    ffui::ShortcutMap shortcuts;
    shortcuts.ResetDefaults(registry);
    Check(shortcuts.FindByCommand(L"search.focus") != nullptr &&
          shortcuts.FindByCommand(L"search.focus")->scope == ffui::ShortcutScope::Global,
          "search binding is global");
    Check(ffui::FormatKeyChord(shortcuts.FindByCommand(L"app.command-palette")->chord) == L"Ctrl+Shift+P",
          "palette default shortcut is present");
    Check(shortcuts.Rebind(L"navigation.refresh", {'C', ffui::ModifierControl}, false) == ffui::RebindResult::Conflict,
          "conflicting rebind is rejected");
    Check(shortcuts.Rebind(L"navigation.refresh", {'C', ffui::ModifierControl}, true) == ffui::RebindResult::Applied,
          "conflicting rebind can explicitly reassign");
    Check(shortcuts.FindByCommand(L"file.copy") == nullptr, "reassignment unbinds the old command");
    Check(ffui::ShortcutMap::IsWindowsReserved({VK_F4, ffui::ModifierAlt}), "Alt+F4 warns as reserved");

    auto results = ffui::SearchCommands(registry, shortcuts, {ffui::SelectionKind::Empty, false}, L"antp");
    Check(!results.empty() && results.front().command->commandId == L"storage.analyze",
          "out-of-order fuzzy query finds Analyze Storage");
    results = ffui::SearchCommands(registry, shortcuts, {ffui::SelectionKind::SingleFile, false}, L"copy");
    Check(!results.empty() && results.front().command->commandId == L"file.copy",
          "exact name ranks above longer copy commands");
    Check(shortcuts.Rebind(L"search.focus", {'K', ffui::ModifierControl}, false) == ffui::RebindResult::Applied,
          "palette-hint rebind applies");
    results = ffui::SearchCommands(registry, shortcuts, {ffui::SelectionKind::Empty, false}, L"search");
    Check(!results.empty() && results.front().shortcutText == L"Ctrl+K",
          "palette hint immediately reflects the live shortcut map");
}

void TestPersistence() {
    int invocations = 0;
    std::wstring lastCommand;
    ffui::CommandRegistry registry;
    ffui::RegisterBaselineCommands(registry, CountingHandlers(invocations, lastCommand));
    ffui::ShortcutMap shortcuts;
    shortcuts.ResetDefaults(registry);
    Check(shortcuts.Rebind(L"navigation.refresh", {'R', ffui::ModifierControl}, false) == ffui::RebindResult::Applied,
          "custom shortcut applies");
    const auto path = std::filesystem::temp_directory_path() / L"fastfiles-command-tests" / L"shortcuts.json";
    Check(shortcuts.Save(path), "shortcut diffs save");
    ffui::ShortcutMap loaded;
    Check(loaded.Load(path, registry), "shortcut diffs load");
    Check(loaded.FindByCommand(L"navigation.refresh") != nullptr &&
          loaded.FindByCommand(L"navigation.refresh")->chord == ffui::KeyChord{'R', ffui::ModifierControl},
          "custom shortcut survives reload");
    Check(loaded.FindByCommand(L"search.focus") != nullptr, "unrelated new defaults remain active");
    Check(loaded.FindByCommand(L"file.copy") != nullptr, "unrelated default remains after ordinary customization");

    Check(shortcuts.Rebind(L"navigation.refresh", {'C', ffui::ModifierControl}, true) == ffui::RebindResult::Applied,
          "reassignment applies before persistence test");
    Check(shortcuts.Save(path), "reassignment diffs save");
    Check(loaded.Load(path, registry), "reassignment diffs reload");
    Check(loaded.FindByCommand(L"file.copy") == nullptr, "explicitly unbound conflicting command stays unbound");
    {
        std::wofstream unknown(path, std::ios::trunc);
        unknown << L"{\"bindings\":[{\"commandId\":\"removed.command\",\"key\":65,\"modifiers\":\"C\"}]}";
    }
    bool loggedUnknown = false;
    Check(loaded.Load(path, registry, [&](const std::wstring& message) {
        loggedUnknown = message.find(L"removed.command") != std::wstring::npos;
    }), "unknown shortcut ids do not fail loading");
    Check(loggedUnknown, "unknown shortcut ids are logged and ignored");
    std::error_code error;
    std::filesystem::remove(path, error);
    std::filesystem::remove(path.parent_path(), error);
}

void TestQuickActions() {
    const auto launch = ffui::BuildTerminalLaunchSpec(L"C:\\folder & name", L"powershell.exe");
    Check(launch.currentDirectory == L"C:\\folder & name", "terminal target is passed as current directory");
    Check(launch.commandLine == L"powershell.exe" && launch.commandLine.find(L"folder") == std::wstring::npos,
          "terminal path is never interpolated into the command line");
    bool fallback = false;
    const auto relative = ffui::PathsRelativeTo({L"C:\\base\\folder\\file.txt"}, L"C:\\base", fallback);
    Check(!fallback && relative.size() == 1 && relative.front() == L"folder\\file.txt",
          "relative paths use the configured base");
}
} // namespace

int main() {
    TestRegistryAndSelection();
    TestShortcutsAndPalette();
    TestPersistence();
    TestQuickActions();
    if (failures == 0) std::cout << "All command-system tests passed\n";
    return failures == 0 ? 0 : 1;
}
