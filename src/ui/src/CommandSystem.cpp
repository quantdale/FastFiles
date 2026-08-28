#include "CommandSystem.h"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <limits>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <windows.h>

namespace ffui {
namespace {

constexpr SelectionApplicability kAnySelection =
    SelectionKind::Empty | SelectionKind::SingleFile | SelectionKind::SingleFolder |
    SelectionKind::MultipleFiles | SelectionKind::MultipleFolders | SelectionKind::Mixed;
constexpr SelectionApplicability kAnyItem =
    SelectionKind::SingleFile | SelectionKind::SingleFolder | SelectionKind::MultipleFiles |
    SelectionKind::MultipleFolders | SelectionKind::Mixed;
constexpr SelectionApplicability kSingleItem = SelectionKind::SingleFile | SelectionKind::SingleFolder;
constexpr SelectionApplicability kMultipleItems =
    SelectionKind::MultipleFiles | SelectionKind::MultipleFolders | SelectionKind::Mixed;

CommandHandler Resolve(const BaselineHandlers& handlers, const wchar_t* id) {
    if (!handlers.resolve) return {};
    return handlers.resolve(id);
}

bool RegisterOne(CommandRegistry& registry, const BaselineHandlers& handlers, const wchar_t* id,
                 const wchar_t* name, const wchar_t* category, SelectionApplicability applicability,
                 ShortcutScope scope, std::optional<KeyChord> shortcut = std::nullopt,
                 EnabledPredicate predicate = {}, const wchar_t* disabledReason = nullptr) {
    return registry.Register({id, name, category, shortcut, applicability, scope,
                              std::move(predicate), Resolve(handlers, id),
                              disabledReason != nullptr ? disabledReason : L""});
}

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return value;
}

int FuzzyScore(const std::wstring& textValue, const std::wstring& queryValue) {
    const std::wstring text = Lower(textValue);
    const std::wstring query = Lower(queryValue);
    if (query.empty()) return 1;
    if (text == query) return 10000;
    if (text.starts_with(query)) return 8000 - static_cast<int>(text.size() - query.size());

    const size_t substring = text.find(query);
    if (substring != std::wstring::npos) {
        const bool boundary = substring == 0 || !std::iswalnum(text[substring - 1]);
        return (boundary ? 6500 : 5000) - static_cast<int>(substring);
    }

    size_t textIndex = 0;
    int score = 2000;
    int previous = -2;
    bool toleratedTypo = false;
    for (wchar_t queryChar : query) {
        const size_t found = text.find(queryChar, textIndex);
        if (found == std::wstring::npos) {
            if (toleratedTypo) return -1;
            toleratedTypo = true;
            score -= 500;
            continue;
        }
        if (found == 0 || !std::iswalnum(text[found - 1])) score += 120;
        if (static_cast<int>(found) == previous + 1) score += 40;
        score -= static_cast<int>(found - textIndex);
        previous = static_cast<int>(found);
        textIndex = found + 1;
    }
    return score;
}

std::wstring EscapeJson(const std::wstring& value) {
    std::wstring result;
    for (wchar_t ch : value) {
        if (ch == L'\\' || ch == L'"') result.push_back(L'\\');
        result.push_back(ch);
    }
    return result;
}

uint8_t ParseModifiers(const std::wstring& value) {
    uint8_t result = ModifierNone;
    if (value.find(L'C') != std::wstring::npos) result |= ModifierControl;
    if (value.find(L'S') != std::wstring::npos) result |= ModifierShift;
    if (value.find(L'A') != std::wstring::npos) result |= ModifierAlt;
    return result;
}

std::wstring FormatModifiers(uint8_t modifiers) {
    std::wstring result;
    if ((modifiers & ModifierControl) != 0) result += L'C';
    if ((modifiers & ModifierShift) != 0) result += L'S';
    if ((modifiers & ModifierAlt) != 0) result += L'A';
    return result;
}

std::wstring Trim(std::wstring value) {
    const auto whitespace = [](wchar_t ch) { return iswspace(ch) != 0; };
    const auto first = std::find_if_not(value.begin(), value.end(), whitespace);
    const auto last = std::find_if_not(value.rbegin(), value.rend(), whitespace).base();
    if (first >= last) return {};
    return {first, last};
}

bool EqualsInsensitive(const std::wstring& left, const std::wstring& right) {
    if (left.size() != right.size()) return false;
    return std::equal(left.begin(), left.end(), right.begin(),
                      [](wchar_t a, wchar_t b) { return towlower(a) == towlower(b); });
}

bool IsValidChord(KeyChord chord) {
    return chord.virtualKey != 0 && (chord.modifiers & ~(ModifierControl | ModifierShift | ModifierAlt)) == 0;
}

std::optional<std::wstring> UnescapeJson(const std::wstring& value) {
    std::wstring result;
    for (size_t index = 0; index < value.size(); ++index) {
        if (value[index] != L'\\') {
            result.push_back(value[index]);
            continue;
        }
        if (++index >= value.size()) return std::nullopt;
        if (value[index] != L'\\' && value[index] != L'"') return std::nullopt;
        result.push_back(value[index]);
    }
    return result;
}

bool IsShortcutDocument(const std::wstring& json) {
    static const std::wregex document(
        LR"json(^\s*\{\s*(?:"version"\s*:\s*[0-9]+\s*,\s*)?"bindings"\s*:\s*\[[\s\S]*\]\s*\}\s*$)json");
    return std::regex_match(json, document);
}

const CommandDescriptor* FindLegacyCommand(const std::wstring& text, const CommandRegistry& registry) {
    if (const auto* exact = registry.Find(text)) return exact;
    for (const auto& command : registry.Commands()) {
        if (EqualsInsensitive(command.displayName, text)) return &command;
    }
    return nullptr;
}

bool ScopesOverlap(ShortcutScope, ShortcutScope) {
    // A global binding is checked before active-view dispatch, so duplicate
    // chords in either scope overlap in the application input chain.
    return true;
}

} // namespace

bool CommandRegistry::Register(CommandDescriptor descriptor) {
    if (descriptor.commandId.empty() || !descriptor.handler || indexById_.contains(descriptor.commandId)) {
        return false;
    }
    indexById_.emplace(descriptor.commandId, commands_.size());
    commands_.push_back(std::move(descriptor));
    return true;
}

bool CommandRegistry::RegisterRange(std::span<CommandDescriptor> descriptors) {
    bool result = true;
    for (auto& descriptor : descriptors) result = Register(std::move(descriptor)) && result;
    return result;
}

const CommandDescriptor* CommandRegistry::Find(const std::wstring& commandId) const {
    const auto found = indexById_.find(commandId);
    return found == indexById_.end() ? nullptr : &commands_[found->second];
}

std::vector<const CommandDescriptor*> CommandRegistry::Query(const CommandContext& context) const {
    std::vector<const CommandDescriptor*> result;
    const auto kind = AppliesTo(context.selectionKind);
    for (const auto& command : commands_) {
        if ((command.applicability & kind) != 0) result.push_back(&command);
    }
    return result;
}

bool CommandRegistry::Invoke(const std::wstring& commandId, const CommandContext& context,
                             const std::vector<std::wstring>& selection) const {
    const CommandDescriptor* descriptor = Find(commandId);
    if (descriptor == nullptr || (descriptor->applicability & AppliesTo(context.selectionKind)) == 0 ||
        (descriptor->enabledPredicate && !descriptor->enabledPredicate(context))) {
        return false;
    }
    descriptor->handler(selection);
    return true;
}

SelectionKind ClassifySelection(std::span<const SelectionItem> selection) {
    if (selection.empty()) return SelectionKind::Empty;
    if (selection.size() == 1) return selection.front().isDirectory ? SelectionKind::SingleFolder : SelectionKind::SingleFile;
    const bool firstDirectory = selection.front().isDirectory;
    if (std::all_of(selection.begin(), selection.end(), [firstDirectory](const SelectionItem& item) {
            return item.isDirectory == firstDirectory;
        })) {
        return firstDirectory ? SelectionKind::MultipleFolders : SelectionKind::MultipleFiles;
    }
    return SelectionKind::Mixed;
}

std::vector<std::wstring> SelectionPaths(std::span<const SelectionItem> selection) {
    std::vector<std::wstring> result;
    result.reserve(selection.size());
    for (const auto& item : selection) result.push_back(item.path);
    return result;
}

bool RegisterFileOperationCommands(CommandRegistry& registry, const BaselineHandlers& handlers) {
    bool ok = true;
    // ui-v3 command-palette: the trailing strings are presentation-only
    // disabled reasons surfaced by the palette; semantics are unchanged.
    ok = RegisterOne(registry, handlers, L"file.new-folder", L"New Folder", L"File", AppliesTo(SelectionKind::Empty), ShortcutScope::ActiveView, KeyChord{'N', static_cast<uint8_t>(ModifierControl | ModifierShift)}, {}, L"Available when nothing is selected") && ok;
    ok = RegisterOne(registry, handlers, L"file.new-file", L"New File", L"File", AppliesTo(SelectionKind::Empty), ShortcutScope::ActiveView, std::nullopt, {}, L"Available when nothing is selected") && ok;
    ok = RegisterOne(registry, handlers, L"file.copy", L"Copy", L"File", kAnyItem, ShortcutScope::ActiveView, KeyChord{'C', ModifierControl}, {}, L"Requires a selection") && ok;
    ok = RegisterOne(registry, handlers, L"file.cut", L"Cut", L"File", kAnyItem, ShortcutScope::ActiveView, KeyChord{'X', ModifierControl}, {}, L"Requires a selection") && ok;
    ok = RegisterOne(registry, handlers, L"file.paste", L"Paste", L"File", AppliesTo(SelectionKind::Empty), ShortcutScope::ActiveView, KeyChord{'V', ModifierControl}, [](const CommandContext& c) { return c.clipboardCompatible; }, L"No compatible files on the Clipboard") && ok;
    ok = RegisterOne(registry, handlers, L"file.rename", L"Rename", L"File", kSingleItem, ShortcutScope::ActiveView, KeyChord{VK_F2, ModifierNone}, {}, L"Requires a single selected item") && ok;
    ok = RegisterOne(registry, handlers, L"file.delete", L"Delete", L"File", kAnyItem, ShortcutScope::ActiveView, KeyChord{VK_DELETE, ModifierNone}, {}, L"Requires a selection") && ok;
    ok = RegisterOne(registry, handlers, L"file.delete-permanently", L"Delete Permanently", L"File", kAnyItem, ShortcutScope::ActiveView, KeyChord{VK_DELETE, ModifierShift}, {}, L"Requires a selection") && ok;
    ok = RegisterOne(registry, handlers, L"file.undo", L"Undo Last File Operation", L"File", kAnySelection, ShortcutScope::ActiveView, KeyChord{'Z', ModifierControl}) && ok;
    return ok;
}

bool RegisterNavigationCommands(CommandRegistry& registry, const BaselineHandlers& handlers) {
    bool ok = true;
    ok = RegisterOne(registry, handlers, L"navigation.back", L"Back", L"Navigation", kAnySelection, ShortcutScope::ActiveView, KeyChord{VK_LEFT, ModifierAlt}) && ok;
    ok = RegisterOne(registry, handlers, L"navigation.forward", L"Forward", L"Navigation", kAnySelection, ShortcutScope::ActiveView, KeyChord{VK_RIGHT, ModifierAlt}) && ok;
    // quick-access-home 5.3: navigate the active context to Home (Alt+Home,
    // matching Explorer). Global scope so it works even while an edit owns focus.
    ok = RegisterOne(registry, handlers, L"navigation.home", L"Home", L"Navigation", kAnySelection, ShortcutScope::Global, KeyChord{VK_HOME, ModifierAlt}) && ok;
    ok = RegisterOne(registry, handlers, L"navigation.up", L"Up", L"Navigation", kAnySelection, ShortcutScope::ActiveView, KeyChord{VK_BACK, ModifierNone}) && ok;
    ok = RegisterOne(registry, handlers, L"navigation.refresh", L"Refresh", L"Navigation", kAnySelection, ShortcutScope::ActiveView, KeyChord{VK_F5, ModifierNone}) && ok;
    ok = RegisterOne(registry, handlers, L"navigation.focus-path", L"Focus Path Entry", L"Navigation", kAnySelection, ShortcutScope::Global, KeyChord{'L', ModifierControl}) && ok;
    ok = RegisterOne(registry, handlers, L"navigation.new-tab", L"New Tab", L"Navigation", kAnySelection, ShortcutScope::Global, KeyChord{'T', ModifierControl}) && ok;
    ok = RegisterOne(registry, handlers, L"navigation.close-tab", L"Close Tab", L"Navigation", kAnySelection, ShortcutScope::Global, KeyChord{'W', ModifierControl}) && ok;
    ok = RegisterOne(registry, handlers, L"navigation.next-tab", L"Next Tab", L"Navigation", kAnySelection, ShortcutScope::Global, KeyChord{VK_TAB, ModifierControl}) && ok;
    ok = RegisterOne(registry, handlers, L"navigation.previous-tab", L"Previous Tab", L"Navigation", kAnySelection, ShortcutScope::Global, KeyChord{VK_TAB, static_cast<uint8_t>(ModifierControl | ModifierShift)}) && ok;
    ok = RegisterOne(registry, handlers, L"navigation.reopen-tab", L"Reopen Closed Tab", L"Navigation", kAnySelection, ShortcutScope::Global, KeyChord{'T', static_cast<uint8_t>(ModifierControl | ModifierShift)}) && ok;
    ok = RegisterOne(registry, handlers, L"navigation.add-bookmark", L"Add to Bookmarks", L"Navigation", kAnySelection, ShortcutScope::Global, KeyChord{'D', ModifierControl}) && ok;
    ok = RegisterOne(registry, handlers, L"navigation.toggle-column-view", L"Toggle Column View", L"View", kAnySelection, ShortcutScope::Global) && ok;
    ok = RegisterOne(registry, handlers, L"navigation.toggle-dual-pane", L"Toggle Dual Pane", L"View", kAnySelection, ShortcutScope::Global) && ok;
    ok = RegisterOne(registry, handlers, L"view.toggle-details-pane", L"Toggle Details Pane", L"View", kAnySelection, ShortcutScope::Global) && ok;
    ok = RegisterOne(registry, handlers, L"selection.select-all", L"Select All", L"Selection", kAnySelection, ShortcutScope::ActiveView, KeyChord{'A', ModifierControl}) && ok;
    return ok;
}

bool RegisterSearchCommands(CommandRegistry& registry, const BaselineHandlers& handlers) {
    return RegisterOne(registry, handlers, L"search.focus", L"Search", L"Search", kAnySelection,
                       ShortcutScope::Global, KeyChord{'F', ModifierControl});
}

bool RegisterStorageCommands(CommandRegistry& registry, const BaselineHandlers& handlers) {
    return RegisterOne(registry, handlers, L"storage.analyze", L"Analyze Storage", L"Storage", kAnySelection,
                       ShortcutScope::Global,
                       KeyChord{'S', static_cast<uint8_t>(ModifierControl | ModifierShift)});
}

bool RegisterPropertiesCommands(CommandRegistry& registry, const BaselineHandlers& handlers) {
    return RegisterOne(registry, handlers, L"item.properties", L"Properties", L"Item", kAnyItem,
                       ShortcutScope::ActiveView, std::nullopt, {}, L"Requires a selection");
}

bool RegisterShellCommands(CommandRegistry& registry, const BaselineHandlers& handlers) {
    bool ok = true;
    ok = RegisterOne(registry, handlers, L"item.open", L"Open", L"Item", kSingleItem, ShortcutScope::ActiveView, std::nullopt, {}, L"Requires a single selected item") && ok;
    ok = RegisterOne(registry, handlers, L"item.open-with", L"Open With", L"Item", AppliesTo(SelectionKind::SingleFile), ShortcutScope::ActiveView, std::nullopt, {}, L"Requires a single selected file") && ok;
    ok = RegisterOne(registry, handlers, L"item.copy-path", L"Copy Path", L"Item", kAnyItem, ShortcutScope::ActiveView, std::nullopt, {}, L"Requires a selection") && ok;
    ok = RegisterOne(registry, handlers, L"item.copy-relative-path", L"Copy Relative Path", L"Item", kAnyItem, ShortcutScope::ActiveView, std::nullopt, {}, L"Requires a selection") && ok;
    ok = RegisterOne(registry, handlers, L"item.open-containing-folder", L"Open Containing Folder", L"Item", AppliesTo(SelectionKind::SingleFile), ShortcutScope::ActiveView, std::nullopt, {}, L"Requires a single selected file") && ok;
    ok = RegisterOne(registry, handlers, L"item.open-terminal", L"Open Terminal Here", L"Item", SelectionKind::Empty | SelectionKind::SingleFolder, ShortcutScope::ActiveView, std::nullopt, {}, L"Requires a folder or an empty location") && ok;
    ok = RegisterOne(registry, handlers, L"app.settings", L"Settings", L"Application", kAnySelection, ShortcutScope::Global) && ok;
    ok = RegisterOne(registry, handlers, L"app.command-palette", L"Open Command Palette", L"Application", kAnySelection, ShortcutScope::Global, KeyChord{'P', static_cast<uint8_t>(ModifierControl | ModifierShift)}) && ok;
    return ok;
}

bool RegisterBaselineCommands(CommandRegistry& registry, const BaselineHandlers& handlers) {
    return RegisterFileOperationCommands(registry, handlers) && RegisterNavigationCommands(registry, handlers) &&
           RegisterSearchCommands(registry, handlers) && RegisterStorageCommands(registry, handlers) &&
           RegisterPropertiesCommands(registry, handlers) && RegisterShellCommands(registry, handlers);
}

void ShortcutMap::ResetDefaults(const CommandRegistry& registry) {
    defaults_.clear();
    customizations_.clear();
    commandScopes_.clear();
    for (const auto& command : registry.Commands()) {
        commandScopes_.emplace(command.commandId, command.scope);
        if (command.defaultShortcut) defaults_.emplace(command.commandId, ShortcutBinding{command.commandId, *command.defaultShortcut, command.scope});
    }
}

const ShortcutBinding* ShortcutMap::FindByCommand(const std::wstring& commandId) const {
    const auto customized = customizations_.find(commandId);
    if (customized != customizations_.end()) return customized->second ? &*customized->second : nullptr;
    const auto fallback = defaults_.find(commandId);
    return fallback == defaults_.end() ? nullptr : &fallback->second;
}

const ShortcutBinding* ShortcutMap::FindByChord(KeyChord chord, ShortcutScope scope) const {
    for (const auto& binding : EffectiveBindings()) {
        if (binding.chord == chord && binding.scope == scope) return FindByCommand(binding.commandId);
    }
    return nullptr;
}

std::vector<ShortcutBinding> ShortcutMap::EffectiveBindings() const {
    std::vector<ShortcutBinding> result;
    for (const auto& [id, binding] : defaults_) {
        const auto custom = customizations_.find(id);
        if (custom == customizations_.end()) result.push_back(binding);
        else if (custom->second) result.push_back(*custom->second);
    }
    for (const auto& [id, custom] : customizations_) {
        if (custom && !defaults_.contains(id)) result.push_back(*custom);
    }
    return result;
}

RebindResult ShortcutMap::Rebind(const std::wstring& commandId, KeyChord chord, bool reassignConflict) {
    if (!IsValidChord(chord)) return RebindResult::InvalidBinding;
    const ShortcutBinding* existing = FindByCommand(commandId);
    if (existing == nullptr && !commandScopes_.contains(commandId)) return RebindResult::UnknownCommand;
    const ShortcutScope scope = existing ? existing->scope : commandScopes_.at(commandId);
    for (const auto& binding : EffectiveBindings()) {
        if (binding.commandId != commandId && binding.chord == chord && ScopesOverlap(binding.scope, scope)) {
            if (!reassignConflict) return RebindResult::Conflict;
            customizations_[binding.commandId] = std::nullopt;
        }
    }
    customizations_[commandId] = ShortcutBinding{commandId, chord, scope};
    const auto defaultBinding = defaults_.find(commandId);
    if (defaultBinding != defaults_.end() && defaultBinding->second.chord == chord) customizations_.erase(commandId);
    return RebindResult::Applied;
}

bool ShortcutMap::Remove(const std::wstring& commandId) {
    if (!commandScopes_.contains(commandId) && !defaults_.contains(commandId) && !customizations_.contains(commandId)) return false;
    customizations_[commandId] = std::nullopt;
    return true;
}

std::optional<KeyChord> ShortcutMap::ParseKeyChord(const std::wstring& text) {
    const std::wstring value = Trim(text);
    if (value.empty()) return std::nullopt;

    std::vector<std::wstring> tokens;
    size_t start = 0;
    while (start <= value.size()) {
        const size_t separator = value.find(L'+', start);
        const size_t end = separator == std::wstring::npos ? value.size() : separator;
        const std::wstring token = Trim(value.substr(start, end - start));
        if (token.empty()) return std::nullopt;
        tokens.push_back(token);
        if (separator == std::wstring::npos) break;
        start = separator + 1;
    }

    uint8_t modifiers = ModifierNone;
    for (size_t index = 0; index + 1 < tokens.size(); ++index) {
        const std::wstring token = Lower(tokens[index]);
        if (token == L"ctrl" || token == L"control") modifiers |= ModifierControl;
        else if (token == L"shift") modifiers |= ModifierShift;
        else if (token == L"alt") modifiers |= ModifierAlt;
        else return std::nullopt;
    }

    const std::wstring key = Lower(tokens.back());
    uint16_t virtualKey = 0;
    if (key.size() == 1 && ((key.front() >= L'a' && key.front() <= L'z') ||
                            (key.front() >= L'0' && key.front() <= L'9'))) {
        virtualKey = static_cast<uint16_t>(towupper(key.front()));
    } else if (key == L"delete") {
        virtualKey = VK_DELETE;
    } else if (key == L"left") {
        virtualKey = VK_LEFT;
    } else if (key == L"right") {
        virtualKey = VK_RIGHT;
    } else if (key == L"up") {
        virtualKey = VK_UP;
    } else if (key == L"down") {
        virtualKey = VK_DOWN;
    } else if (key == L"backspace") {
        virtualKey = VK_BACK;
    } else if (key == L"tab") {
        virtualKey = VK_TAB;
    } else if (key == L"escape" || key == L"esc") {
        virtualKey = VK_ESCAPE;
    } else if (key == L",") {
        virtualKey = VK_OEM_COMMA;
    } else if (key.size() >= 2 && key.front() == L'f') {
        try {
            const unsigned long functionKey = std::stoul(key.substr(1));
            if (functionKey >= 1 && functionKey <= 24) {
                virtualKey = static_cast<uint16_t>(VK_F1 + functionKey - 1);
            }
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    if (virtualKey == 0) return std::nullopt;
    return KeyChord{virtualKey, modifiers};
}

bool ShortcutMap::ImportLegacy(const std::vector<std::pair<std::wstring, std::wstring>>& entries,
                               const CommandRegistry& registry,
                               const std::function<void(const std::wstring&)>& log) {
    bool valid = true;
    for (const auto& [commandText, bindingText] : entries) {
        const auto* descriptor = FindLegacyCommand(commandText, registry);
        if (descriptor == nullptr) {
            if (log) log(L"Ignoring unresolved legacy shortcut command: " + commandText);
            continue;
        }
        if (Trim(bindingText).empty()) {
            Remove(descriptor->commandId);
            continue;
        }
        const auto chord = ParseKeyChord(bindingText);
        if (!chord) {
            if (log) log(L"Ignoring malformed legacy shortcut binding for: " + descriptor->commandId);
            valid = false;
            continue;
        }
        const RebindResult rebound = Rebind(descriptor->commandId, *chord, false);
        if (rebound != RebindResult::Applied) {
            if (log) log(L"Ignoring conflicting legacy shortcut binding for: " + descriptor->commandId);
            valid = false;
        }
    }
    return valid;
}

bool ShortcutMap::Load(const std::filesystem::path& path, const CommandRegistry& registry,
                       const std::function<void(const std::wstring&)>& log) {
    ResetDefaults(registry);
    std::wifstream input(path);
    if (!input) return true;
    std::wstringstream buffer;
    buffer << input.rdbuf();
    const std::wstring json = buffer.str();
    try {
        if (!IsShortcutDocument(json)) throw std::runtime_error("malformed shortcut document");
        const std::wregex entry(LR"json(\{\s*"commandId"\s*:\s*"((?:\\.|[^"])*)"\s*,\s*"key"\s*:\s*([0-9]+)\s*,\s*"modifiers"\s*:\s*"([CSA]*)"\s*\})json");
        const std::wregex unbound(LR"json(\{\s*"commandId"\s*:\s*"((?:\\.|[^"])*)"\s*,\s*"unbound"\s*:\s*true\s*\})json");
        const std::wregex commandToken(LR"json("commandId"\s*:)json");
        size_t parsedEntries = 0;
        std::vector<std::pair<std::wstring, ShortcutBinding>> bindings;
        std::vector<std::wstring> unboundCommands;
        std::vector<std::wstring> seenCommands;
        for (std::wsregex_iterator current(json.begin(), json.end(), entry), end; current != end; ++current) {
            const auto decodedId = UnescapeJson((*current)[1]);
            if (!decodedId) throw std::runtime_error("malformed shortcut command id");
            const std::wstring id = *decodedId;
            if (std::ranges::find(seenCommands, id) != seenCommands.end()) {
                throw std::runtime_error("duplicate shortcut command id");
            }
            seenCommands.push_back(id);
            ++parsedEntries;
            const auto* descriptor = registry.Find(id);
            if (descriptor == nullptr) {
                if (log) log(L"Ignoring unresolved shortcut command: " + id);
                continue;
            }
            const unsigned long key = std::stoul((*current)[2].str());
            if (key > std::numeric_limits<uint16_t>::max()) throw std::runtime_error("shortcut key out of range");
            const uint8_t modifiers = ParseModifiers((*current)[3]);
            if (!IsValidChord({static_cast<uint16_t>(key), modifiers})) {
                throw std::runtime_error("invalid shortcut chord");
            }
            bindings.emplace_back(id, ShortcutBinding{id, {static_cast<uint16_t>(key), modifiers}, descriptor->scope});
        }
        for (std::wsregex_iterator current(json.begin(), json.end(), unbound), end; current != end; ++current) {
            const auto decodedId = UnescapeJson((*current)[1]);
            if (!decodedId) throw std::runtime_error("malformed shortcut command id");
            const std::wstring id = *decodedId;
            if (std::ranges::find(seenCommands, id) != seenCommands.end()) {
                throw std::runtime_error("duplicate shortcut command id");
            }
            seenCommands.push_back(id);
            ++parsedEntries;
            if (registry.Find(id) == nullptr) {
                if (log) log(L"Ignoring unresolved shortcut command: " + id);
                continue;
            }
            unboundCommands.push_back(id);
        }
        size_t declaredEntries = 0;
        for (std::wsregex_iterator current(json.begin(), json.end(), commandToken), end; current != end; ++current) {
            ++declaredEntries;
        }
        if (parsedEntries != declaredEntries) throw std::runtime_error("unrecognized shortcut binding entry");

        for (const auto& id : unboundCommands) customizations_[id] = std::nullopt;
        for (const auto& [id, binding] : bindings) customizations_[id] = binding;

        const auto effective = EffectiveBindings();
        for (size_t left = 0; left < effective.size(); ++left) {
            for (size_t right = left + 1; right < effective.size(); ++right) {
                if (effective[left].chord == effective[right].chord &&
                    ScopesOverlap(effective[left].scope, effective[right].scope)) {
                    throw std::runtime_error("conflicting shortcut bindings");
                }
            }
        }
    } catch (const std::exception&) {
        if (log) log(L"Shortcut customization file is malformed; defaults restored.");
        customizations_.clear();
        return false;
    }
    return true;
}

bool ShortcutMap::Save(const std::filesystem::path& path) const {
    std::error_code error;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    const std::filesystem::path temporary = path.wstring() + L".tmp";
    std::wofstream output(temporary, std::ios::trunc);
    if (!output) return false;
    output << L"{\n  \"version\": 1,\n  \"bindings\": [";
    bool first = true;
    for (const auto& [id, custom] : customizations_) {
        output << (first ? L"" : L",") << L"\n    {\"commandId\": \"" << EscapeJson(id) << L"\", ";
        if (custom) {
            output << L"\"key\": " << custom->chord.virtualKey << L", \"modifiers\": \""
                   << FormatModifiers(custom->chord.modifiers) << L"\"}";
        } else {
            output << L"\"unbound\": true}";
        }
        first = false;
    }
    output << L"\n  ]\n}\n";
    output.close();
    if (!output) return false;
    return MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

bool ShortcutMap::IsWindowsReserved(KeyChord chord) {
    return chord.virtualKey == VK_F4 && (chord.modifiers & ModifierAlt) != 0;
}

std::vector<PaletteResult> SearchCommands(const CommandRegistry& registry, const ShortcutMap& shortcuts,
                                          const CommandContext& context, const std::wstring& query) {
    std::vector<PaletteResult> result;
    for (const auto& command : registry.Commands()) {
        const int score = FuzzyScore(command.displayName, query);
        if (score < 0) continue;
        const bool applicable = (command.applicability & AppliesTo(context.selectionKind)) != 0;
        const bool enabled = applicable && (!command.enabledPredicate || command.enabledPredicate(context));
        const ShortcutBinding* binding = shortcuts.FindByCommand(command.commandId);
        result.push_back({&command, score, enabled, binding ? FormatKeyChord(binding->chord) : L""});
    }
    std::stable_sort(result.begin(), result.end(), [](const PaletteResult& left, const PaletteResult& right) {
        if (left.score != right.score) return left.score > right.score;
        // ui-v3 command-palette: among equally relevant commands, available
        // ones rank above unavailable ones; ordering only — no filtering.
        if (left.enabled != right.enabled) return left.enabled;
        return left.command->displayName < right.command->displayName;
    });
    return result;
}

// ui-v3 command-palette: one canonical glyph per command id so every command
// surface renders the same icon for the same command. Only well-established
// Segoe MDL2 Assets codepoints are used; commands without a confident glyph
// intentionally map to L"" (the palette reserves an empty gutter instead of
// guessing a look-alike symbol). Mirrors the glyphs already shipped on the
// Home quick-actions strip (new tab / search / open path / settings).
const wchar_t* CommandDisplayGlyph(const std::wstring& commandId) {
    struct GlyphEntry {
        const wchar_t* id;
        const wchar_t* glyph;
    };
    static constexpr GlyphEntry kGlyphs[] = {
        {L"navigation.back", L"\uE76B"},             // ChevronLeft
        {L"navigation.forward", L"\uE76C"},          // ChevronRight
        {L"navigation.home", L"\uE80F"},             // Home
        {L"navigation.up", L"\uE70E"},               // ChevronUp
        {L"navigation.refresh", L"\uE72C"},          // Refresh
        {L"navigation.new-tab", L"\uE8A7"},          // Add
        {L"navigation.close-tab", L"\uE8BB"},        // ChromeClose
        {L"navigation.add-bookmark", L"\uE734"},     // FavoriteStar
        {L"selection.select-all", L"\uE73E"},        // CheckMark
        {L"search.focus", L"\uE721"},                // Search
        {L"storage.analyze", L"\uE9D2"},             // Diagnostic / storage
        {L"app.settings", L"\uE713"},                // Setting
        {L"file.copy", L"\uE8C8"},                   // Copy
        {L"file.cut", L"\uE8C6"},                    // Cut
        {L"file.paste", L"\uE77F"},                  // Paste
        {L"file.rename", L"\uE70F"},                 // Edit
        {L"file.delete", L"\uE74D"},                 // Delete
        {L"file.delete-permanently", L"\uE74D"},     // Delete
        {L"file.undo", L"\uE7A7"},                   // Undo
        {L"item.open", L"\uE8E5"},                   // OpenFile
        {L"item.open-containing-folder", L"\uE8B7"}, // FolderOpen
        {L"item.open-terminal", L"\uE756"},          // CommandPrompt
        {L"item.properties", L"\uE946"},             // Info
    };
    for (const auto& entry : kGlyphs) {
        if (commandId == entry.id) return entry.glyph;
    }
    return L"";
}

std::wstring FormatKeyChord(KeyChord chord) {
    std::wstring result;
    if ((chord.modifiers & ModifierControl) != 0) result += L"Ctrl+";
    if ((chord.modifiers & ModifierShift) != 0) result += L"Shift+";
    if ((chord.modifiers & ModifierAlt) != 0) result += L"Alt+";
    if ((chord.virtualKey >= 'A' && chord.virtualKey <= 'Z') ||
        (chord.virtualKey >= '0' && chord.virtualKey <= '9')) {
        result.push_back(static_cast<wchar_t>(chord.virtualKey));
    }
    else if (chord.virtualKey == VK_DELETE) result += L"Delete";
    else if (chord.virtualKey >= VK_F1 && chord.virtualKey <= VK_F24) result += L"F" + std::to_wstring(chord.virtualKey - VK_F1 + 1);
    else if (chord.virtualKey == VK_LEFT) result += L"Left";
    else if (chord.virtualKey == VK_RIGHT) result += L"Right";
    else if (chord.virtualKey == VK_UP) result += L"Up";
    else if (chord.virtualKey == VK_DOWN) result += L"Down";
    else if (chord.virtualKey == VK_BACK) result += L"Backspace";
    else if (chord.virtualKey == VK_TAB) result += L"Tab";
    else if (chord.virtualKey == VK_ESCAPE) result += L"Esc";
    else if (chord.virtualKey == VK_OEM_COMMA) result += L",";
    else result += L"Key " + std::to_wstring(chord.virtualKey);
    return result;
}

} // namespace ffui
