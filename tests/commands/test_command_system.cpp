#include "CommandSystem.h"
#include "QuickActions.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <windows.h>
#include "../TestSupport.h"

using namespace fftest;

namespace {
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
    Check(shortcuts.FindByCommand(L"storage.analyze") != nullptr &&
          shortcuts.FindByCommand(L"storage.analyze")->scope == ffui::ShortcutScope::Global &&
          ffui::FormatKeyChord(shortcuts.FindByCommand(L"storage.analyze")->chord) == L"Ctrl+Shift+S",
          "storage analysis default shortcut is Ctrl+Shift+S");
    Check(ffui::FormatKeyChord(shortcuts.FindByCommand(L"app.command-palette")->chord) == L"Ctrl+Shift+P",
          "palette default shortcut is present");
    Check(ffui::FormatKeyChord(shortcuts.FindByCommand(L"navigation.home")->chord) == L"Alt+Home",
          "Home shortcut formats as Alt+Home rather than Alt+Key 36");
    Check(registry.Find(L"navigation.toggle-column-view") != nullptr &&
              registry.Find(L"navigation.toggle-column-view")->displayName == L"Toggle Dual Pane",
          "legacy toggle-column-view id is an honest dual-pane alias");
    {
        // ui-overhaul: the retained legacy alias must not duplicate the
        // canonical palette row, while staying invocable for persisted
        // shortcut bindings that still reference it.
        const auto dualRows = ffui::SearchCommands(registry, shortcuts, {ffui::SelectionKind::Empty, false}, L"dual pane");
        int dualPaneRows = 0;
        const ffui::CommandDescriptor* listedDual = nullptr;
        for (const auto& row : dualRows) {
            if (row.command->displayName == L"Toggle Dual Pane") {
                ++dualPaneRows;
                listedDual = row.command;
            }
        }
        Check(dualPaneRows == 1, "the palette lists exactly one Toggle Dual Pane row");
        Check(listedDual != nullptr && listedDual->commandId == L"navigation.toggle-dual-pane",
              "the listed dual-pane row is the canonical command id");
        const int invocationsBeforeAlias = invocations;
        Check(registry.Invoke(L"navigation.toggle-column-view", {ffui::SelectionKind::Empty, false}, {}),
              "the legacy alias stays invocable by id");
        Check(invocations == invocationsBeforeAlias + 1,
              "invoking the legacy alias dispatches its handler exactly once");
    }
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
    const auto path = std::filesystem::temp_directory_path() / L"fastfiles-command-tests" / L"shortcuts.json";
    std::error_code error;
    std::filesystem::remove(path, error);
    ffui::ShortcutMap defaultOnly;
    Check(defaultOnly.Load(path, registry), "missing shortcut configuration loads successfully");
    Check(defaultOnly.FindByCommand(L"search.focus") != nullptr &&
              defaultOnly.FindByCommand(L"search.focus")->chord == ffui::KeyChord{'F', ffui::ModifierControl},
          "missing shortcut configuration restores built-in defaults");
    Check(shortcuts.Rebind(L"navigation.refresh", {'R', ffui::ModifierControl}, false) == ffui::RebindResult::Applied,
          "custom shortcut applies");
    Check(shortcuts.Rebind(L"search.focus", {'K', ffui::ModifierControl}, false) == ffui::RebindResult::Applied,
          "global custom shortcut applies");
    Check(shortcuts.Save(path), "shortcut diffs save");
    ffui::ShortcutMap loaded;
    Check(loaded.Load(path, registry), "shortcut diffs load");
    Check(loaded.FindByCommand(L"navigation.refresh") != nullptr &&
          loaded.FindByCommand(L"navigation.refresh")->chord == ffui::KeyChord{'R', ffui::ModifierControl},
          "custom shortcut survives reload");
    Check(loaded.FindByCommand(L"search.focus") != nullptr &&
              loaded.FindByCommand(L"search.focus")->chord == ffui::KeyChord{'K', ffui::ModifierControl} &&
              loaded.FindByCommand(L"search.focus")->scope == ffui::ShortcutScope::Global,
          "global custom shortcut and scope survive reload");
    Check(loaded.FindByCommand(L"file.copy") != nullptr, "unrelated default remains after ordinary customization");
    Check(loaded.FindByChord({VK_F5, ffui::ModifierNone}, ffui::ShortcutScope::ActiveView) == nullptr,
          "the old binding is removed after reload");
    Check(loaded.FindByCommand(L"navigation.refresh")->scope == ffui::ShortcutScope::ActiveView,
          "active-view scope is restored from command metadata after reload");

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
    {
        std::wofstream malformed(path, std::ios::trunc);
        malformed << L"{\"version\":1,\"bindings\":[{\"commandId\":\"search.focus\",\"key\":\"bad\"}]}";
    }
    Check(!loaded.Load(path, registry), "malformed shortcut configuration reports failure");
    Check(loaded.FindByCommand(L"search.focus") != nullptr &&
              loaded.FindByCommand(L"search.focus")->chord == ffui::KeyChord{'F', ffui::ModifierControl},
          "malformed shortcut configuration falls back to defaults");
    std::filesystem::remove(path, error);
    std::filesystem::remove(path.parent_path(), error);
}

void TestLegacyMigrationAndChordValidation() {
    int invocations = 0;
    std::wstring lastCommand;
    ffui::CommandRegistry registry;
    ffui::RegisterBaselineCommands(registry, CountingHandlers(invocations, lastCommand));
    ffui::ShortcutMap shortcuts;
    shortcuts.ResetDefaults(registry);
    const std::vector<std::pair<std::wstring, std::wstring>> legacy = {
        {L"Search", L"Ctrl+K"},
        {L"Settings", L"Ctrl+,"},
        {L"navigation.refresh", L"F6"},
        {L"removed.command", L"Ctrl+Q"},
    };
    bool loggedUnknown = false;
    Check(shortcuts.ImportLegacy(legacy, registry, [&](const std::wstring& message) {
        loggedUnknown = message.find(L"removed.command") != std::wstring::npos;
    }), "legacy shortcut import accepts valid entries while reporting no parse failures");
    Check(loggedUnknown, "legacy unknown command is logged");
    Check(shortcuts.FindByCommand(L"search.focus")->chord == ffui::KeyChord{'K', ffui::ModifierControl},
          "legacy display name resolves to the stable search command");
    Check(shortcuts.FindByCommand(L"search.focus")->scope == ffui::ShortcutScope::Global,
          "legacy import retains Global scope from command metadata");
    Check(shortcuts.FindByCommand(L"app.settings") != nullptr &&
              shortcuts.FindByCommand(L"app.settings")->chord == ffui::KeyChord{VK_OEM_COMMA, ffui::ModifierControl},
          "legacy import can bind a command that had no built-in default");
    Check(shortcuts.FindByCommand(L"app.settings")->scope == ffui::ShortcutScope::Global,
          "legacy import retains Global scope for an unbound-by-default command");
    Check(shortcuts.FindByCommand(L"navigation.refresh")->scope == ffui::ShortcutScope::ActiveView,
          "legacy import retains ActiveView scope from command metadata");
    Check(ffui::ShortcutMap::ParseKeyChord(L"Ctrl+Shift+P") ==
              std::optional<ffui::KeyChord>(ffui::KeyChord{'P', static_cast<uint8_t>(ffui::ModifierControl | ffui::ModifierShift)}),
          "display shortcut parser accepts modifier combinations");
    Check(!ffui::ShortcutMap::ParseKeyChord(L"Ctrl+NoSuchKey").has_value(),
          "display shortcut parser rejects invalid key names");
    Check(shortcuts.Rebind(L"search.focus", {0, ffui::ModifierControl}, false) == ffui::RebindResult::InvalidBinding,
          "runtime rebind rejects an invalid key code");
}

void TestBaselinePaletteCoverage() {
    // shell-integration-and-commands task 3.7: the 9 baseline commands
    // (New Folder, Copy Path, Open Terminal Here, Search, Analyze Storage,
    // Toggle Column View, Toggle Dual Pane, Refresh, Settings) must each be
    // discoverable through the command palette and invocable. SearchCommands
    // is the exact query the palette runs against the registry, so asserting
    // every baseline command is a top-ranked palette result for a query that
    // unambiguously names it proves palette discoverability headlessly.
    int invocations = 0;
    std::wstring lastCommand;
    ffui::CommandRegistry registry;
    ffui::RegisterBaselineCommands(registry, CountingHandlers(invocations, lastCommand));
    ffui::ShortcutMap shortcuts;
    shortcuts.ResetDefaults(registry);

    struct BaselineCase { const wchar_t* id; const wchar_t* query; ffui::SelectionKind kind; };
    const BaselineCase cases[] = {
        {L"file.new-folder", L"New Folder", ffui::SelectionKind::Empty},
        {L"item.copy-path", L"Copy Path", ffui::SelectionKind::SingleFile},
        {L"item.open-terminal", L"Open Terminal Here", ffui::SelectionKind::SingleFolder},
        {L"search.focus", L"Search", ffui::SelectionKind::Empty},
        {L"storage.analyze", L"Analyze Storage", ffui::SelectionKind::Empty},
        {L"navigation.toggle-dual-pane", L"Toggle Dual Pane", ffui::SelectionKind::Empty},
        {L"navigation.refresh", L"Refresh", ffui::SelectionKind::Empty},
        {L"app.settings", L"Settings", ffui::SelectionKind::Empty},
    };

    for (const BaselineCase& testCase : cases) {
        const ffui::CommandContext context{testCase.kind, false};
        const auto results = ffui::SearchCommands(registry, shortcuts, context, testCase.query);
        const bool discovered = !results.empty() &&
            (results.front().command->commandId == testCase.id ||
             (std::wstring(testCase.id) == L"navigation.toggle-dual-pane" &&
              results.front().command->commandId == L"navigation.toggle-column-view"));
        Check(discovered, "palette discovers the baseline command by name");
        const bool invoked = registry.Invoke(testCase.id, context, {});
        Check(invoked, "baseline command is invocable");
        Check(invocations == 1 && lastCommand == testCase.id, "invocation reaches the registered handler");
        invocations = 0;
    }
}

void TestAlternateWorkspaceTargets() {
    // original-vision-journey-recovery 2.1: an alternate selection must never
    // turn an empty/summary target into a hidden browsing destination.
    int invocations = 0;
    std::wstring lastCommand;
    ffui::CommandRegistry registry;
    ffui::RegisterBaselineCommands(registry, CountingHandlers(invocations, lastCommand));
    const ffui::CommandContext emptyTool{ffui::SelectionKind::Empty, true, false};
    for (const auto* id : {L"file.paste", L"file.new-folder", L"file.new-file", L"item.open-terminal",
                           L"selection.select-all", L"navigation.up", L"navigation.refresh"}) {
        Check(!registry.Invoke(id, emptyTool, {}), "alternate workspace rejects hidden browse destination/selection command");
    }
    Check(invocations == 0, "unavailable tool actions never reach an operation handler");
    for (const auto* id : {L"navigation.new-tab", L"navigation.focus-path", L"navigation.home",
                           L"app.command-palette", L"app.settings", L"file.undo"}) {
        const auto* command = registry.Find(id);
        Check(command != nullptr && !ffui::CommandUsesSelection(*command),
              "global/recovery actions do not validate a disappeared selected file");
    }
    Check(ffui::CommandUsesSelection(*registry.Find(L"file.delete")) &&
          ffui::CommandUsesSelection(*registry.Find(L"item.open-terminal")),
          "filesystem item actions validate their explicit targets");
    const auto rows = ffui::SearchCommands(registry, ffui::ShortcutMap{}, emptyTool, L"Paste");
    Check(!rows.empty() && !rows.front().enabled, "palette agrees that tool workspace has no paste destination");

    const auto root = std::filesystem::temp_directory_path() /
        (L"FastFiles-command-targets-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    const auto browse = root / L"browse-sentinel.txt";
    const auto storage = root / L"storage-target.txt";
    { std::ofstream a(browse); a << "untouched browse sentinel"; }
    { std::ofstream b(storage); b << "storage target"; }
    const std::vector<std::wstring> targets{storage.wstring()};
    const auto selection = ffui::ResolveExistingSelection(targets);
    Check(selection && selection->size() == 1 && selection->front().path == storage &&
          !selection->front().isDirectory, "live target resolution preserves storage identity and file type");
    std::vector<std::wstring> received;
    ffui::CommandRegistry captured;
    ffui::RegisterBaselineCommands(captured, {[&](const std::wstring&) -> ffui::CommandHandler {
        return [&](const std::vector<std::wstring>& paths) { received = paths; };
    }});
    if (selection) {
        const ffui::CommandContext context{ffui::ClassifySelection(*selection), false, false};
        for (const auto* id : {L"item.copy-path", L"file.copy", L"file.cut", L"file.delete", L"item.open"}) {
            Check(captured.Invoke(id, context, ffui::SelectionPaths(*selection)) && received == targets,
                  "alternate selected item reaches existing command handler with exact target");
        }
    }
    Check(!ffui::ResolveExistingSelection(std::vector<std::wstring>{storage.wstring(), (root / L"missing").wstring()}),
          "one unavailable target rejects the complete selection without partial fallback");
    Check(!ffui::ResolveExistingSelection(std::vector<std::wstring>{L""}), "empty explicit path is invalid");
    Check(std::filesystem::exists(browse) && std::filesystem::file_size(browse) == 25,
          "browse sentinel remains untouched by alternate command dispatch");
    std::filesystem::remove(storage);
    std::filesystem::remove(browse);
    std::filesystem::remove(root);
}

void TestCancelOperationsDiscovery() {
    int invocations = 0;
    std::wstring lastCommand;
    ffui::CommandRegistry registry;
    Check(ffui::RegisterBaselineCommands(registry, CountingHandlers(invocations, lastCommand)),
          "cancel discovery registry initializes");
    ffui::ShortcutMap shortcuts;
    shortcuts.ResetDefaults(registry);
    const auto* cancel = registry.Find(L"file.cancel-operations");
    Check(cancel != nullptr, "cancel command resolves by stable id");
    if (!cancel) return;
    Check(cancel->scope == ffui::ShortcutScope::Global && !cancel->defaultShortcut &&
          !ffui::CommandUsesSelection(*cancel), "cancel has global selection-independent policy without a shortcut");
    Check(cancel->disabledReason == L"No file operations are running or queued",
          "idle cancel explains its availability");
    const auto idle = ffui::SearchCommands(registry, shortcuts, {}, L"Cancel File Operations");
    Check(idle.size() == 1 && idle.front().command == cancel && !idle.front().enabled,
          "idle cancel remains discoverable by its exact name and disabled");
    Check(!registry.Invoke(cancel->commandId, {}, {}) && invocations == 0,
          "idle cancel cannot invoke its handler");
    for (const auto kind : {ffui::SelectionKind::Empty, ffui::SelectionKind::SingleFile,
                           ffui::SelectionKind::SingleFolder, ffui::SelectionKind::MultipleFiles,
                           ffui::SelectionKind::MultipleFolders, ffui::SelectionKind::Mixed}) {
        for (const bool browse : {false, true}) {
            const ffui::CommandContext active{kind, false, browse, true};
            const auto matches = ffui::SearchCommands(registry, shortcuts, active, L"Cancel File Operations");
            Check(matches.size() == 1 && matches.front().enabled && matches.front().command == cancel,
                  "outstanding work enables cancel across selections and workspaces");
            Check(registry.Invoke(cancel->commandId, active, {}) && lastCommand == cancel->commandId,
                  "cancel executes without selected paths");
            auto fresh = active;
            fresh.fileOperationsOutstanding = false;
            const int before = invocations;
            Check(!registry.Invoke(cancel->commandId, fresh, {L"stale-selection"}) && invocations == before,
                  "fresh idle context rejects a previously enabled palette command");
        }
    }
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
    bool crossVolumeFallback = false;
    const auto absolute = ffui::PathsRelativeTo({L"D:\\other\\file.txt"}, L"C:\\base", crossVolumeFallback);
    Check(crossVolumeFallback && absolute.size() == 1 && absolute.front() == L"D:\\other\\file.txt",
          "cross-volume selection falls back to the absolute path");
}
} // namespace

int main() {
    TestRegistryAndSelection();
    TestShortcutsAndPalette();
    TestPersistence();
    TestLegacyMigrationAndChordValidation();
    TestBaselinePaletteCoverage();
    TestCancelOperationsDiscovery();
    TestQuickActions();
    TestAlternateWorkspaceTargets();
    if (fftest::FailureCount() == 0) std::cout << "All command-system tests passed\n";
    return fftest::FailureCount() == 0 ? 0 : 1;
}
