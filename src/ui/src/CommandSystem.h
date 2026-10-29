#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace ffui {

enum class SelectionKind : uint32_t {
    Empty = 1u << 0,
    SingleFile = 1u << 1,
    SingleFolder = 1u << 2,
    MultipleFiles = 1u << 3,
    MultipleFolders = 1u << 4,
    Mixed = 1u << 5,
};

using SelectionApplicability = uint32_t;

constexpr SelectionApplicability AppliesTo(SelectionKind kind) {
    return static_cast<SelectionApplicability>(kind);
}

constexpr SelectionApplicability operator|(SelectionKind left, SelectionKind right) {
    return AppliesTo(left) | AppliesTo(right);
}

constexpr SelectionApplicability operator|(SelectionApplicability left, SelectionKind right) {
    return left | AppliesTo(right);
}

enum class ShortcutScope { Global, ActiveView };

// Global commands remain available while a native edit owns focus. ActiveView
// commands yield to the edit so Windows can perform ordinary text editing.
constexpr bool IsShortcutDispatchAllowed(ShortcutScope scope, bool nativeEditFocused) noexcept {
    return !nativeEditFocused || scope == ShortcutScope::Global;
}

enum KeyModifier : uint8_t {
    ModifierNone = 0,
    ModifierControl = 1u << 0,
    ModifierShift = 1u << 1,
    ModifierAlt = 1u << 2,
};

struct KeyChord {
    uint16_t virtualKey = 0;
    uint8_t modifiers = ModifierNone;
    auto operator<=>(const KeyChord&) const = default;
};

struct ShortcutBinding {
    std::wstring commandId;
    KeyChord chord;
    ShortcutScope scope = ShortcutScope::ActiveView;
};

struct SelectionItem {
    std::wstring path;
    bool isDirectory = false;
};

struct CommandContext {
    SelectionKind selectionKind = SelectionKind::Empty;
    bool clipboardCompatible = false;
    bool browseWorkspace = true;
};

// Handlers deliberately receive only filesystem paths. This caller-agnostic
// seam can later be reused by an IExplorerCommand shell extension without
// exposing FastFiles HWNDs, views, or internal command-target objects.
using CommandHandler = std::function<void(const std::vector<std::wstring>&)>;
using EnabledPredicate = std::function<bool(const CommandContext&)>;

struct CommandDescriptor {
    std::wstring commandId;
    std::wstring displayName;
    std::wstring category;
    std::optional<KeyChord> defaultShortcut;
    SelectionApplicability applicability = 0;
    ShortcutScope scope = ShortcutScope::ActiveView;
    EnabledPredicate enabledPredicate;
    CommandHandler handler;
    // ui-v3 command-palette: presentation-only reason shown when the command is
    // unavailable in the current context (palette rows, disabled feedback).
    // Never consulted by Invoke(); the applicability mask and enabledPredicate
    // remain the sole semantic gates.
    std::wstring disabledReason;
    // ui-overhaul: legacy ids retained for persisted shortcut bindings stay
    // invocable and resolvable by id, but must not surface as a second palette
    // row under a display name a newer id already owns.
    bool paletteHidden = false;
};

// original-vision-journey-recovery 2.1: one availability policy for the
// palette and execution. Alternate workspaces never borrow hidden browse
// destinations, editors or selection mutations.
bool CommandEnabled(const CommandDescriptor& command, const CommandContext& context);
bool CommandUsesSelection(const CommandDescriptor& command);
std::optional<std::vector<SelectionItem>> ResolveExistingSelection(std::span<const std::wstring> paths);

class CommandRegistry {
public:
    bool Register(CommandDescriptor descriptor);
    bool RegisterRange(std::span<CommandDescriptor> descriptors);
    const CommandDescriptor* Find(const std::wstring& commandId) const;
    std::vector<const CommandDescriptor*> Query(const CommandContext& context) const;
    const std::vector<CommandDescriptor>& Commands() const { return commands_; }
    bool Invoke(const std::wstring& commandId, const CommandContext& context,
                const std::vector<std::wstring>& selection) const;

private:
    std::vector<CommandDescriptor> commands_;
    std::unordered_map<std::wstring, size_t> indexById_;
};

SelectionKind ClassifySelection(std::span<const SelectionItem> selection);
std::vector<std::wstring> SelectionPaths(std::span<const SelectionItem> selection);

struct BaselineHandlers {
    std::function<CommandHandler(const std::wstring& commandId)> resolve;
};

bool RegisterFileOperationCommands(CommandRegistry& registry, const BaselineHandlers& handlers);
bool RegisterNavigationCommands(CommandRegistry& registry, const BaselineHandlers& handlers);
bool RegisterSearchCommands(CommandRegistry& registry, const BaselineHandlers& handlers);
bool RegisterStorageCommands(CommandRegistry& registry, const BaselineHandlers& handlers);
bool RegisterPropertiesCommands(CommandRegistry& registry, const BaselineHandlers& handlers);
bool RegisterShellCommands(CommandRegistry& registry, const BaselineHandlers& handlers);
bool RegisterBaselineCommands(CommandRegistry& registry, const BaselineHandlers& handlers);

enum class RebindResult { Applied, Conflict, UnknownCommand, InvalidBinding };

class ShortcutMap {
public:
    void ResetDefaults(const CommandRegistry& registry);
    const ShortcutBinding* FindByCommand(const std::wstring& commandId) const;
    const ShortcutBinding* FindByChord(KeyChord chord, ShortcutScope scope) const;
    std::vector<ShortcutBinding> EffectiveBindings() const;
    RebindResult Rebind(const std::wstring& commandId, KeyChord chord, bool reassignConflict);
    bool Remove(const std::wstring& commandId);
    bool ImportLegacy(const std::vector<std::pair<std::wstring, std::wstring>>& entries,
                      const CommandRegistry& registry,
                      const std::function<void(const std::wstring&)>& log = {});
    bool Load(const std::filesystem::path& path, const CommandRegistry& registry,
              const std::function<void(const std::wstring&)>& log = {});
    bool Save(const std::filesystem::path& path) const;
    static bool IsWindowsReserved(KeyChord chord);
    static std::optional<KeyChord> ParseKeyChord(const std::wstring& text);

private:
    std::unordered_map<std::wstring, ShortcutBinding> defaults_;
    std::unordered_map<std::wstring, std::optional<ShortcutBinding>> customizations_;
    std::unordered_map<std::wstring, ShortcutScope> commandScopes_;
};

struct PaletteResult {
    const CommandDescriptor* command = nullptr;
    int score = 0;
    bool enabled = false;
    std::wstring shortcutText;
};

std::vector<PaletteResult> SearchCommands(const CommandRegistry& registry,
                                          const ShortcutMap& shortcuts,
                                          const CommandContext& context,
                                          const std::wstring& query);
std::wstring FormatKeyChord(KeyChord chord);

// ui-v3 command-palette: canonical Segoe MDL2 glyph for a command id, so the
// palette (and future command surfaces) render the same glyph for the same
// command everywhere. Presentation metadata only — returns L"" when no glyph
// is defined and never influences registration or execution.
const wchar_t* CommandDisplayGlyph(const std::wstring& commandId);

} // namespace ffui
