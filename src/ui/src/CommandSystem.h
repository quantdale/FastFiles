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
};

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

enum class RebindResult { Applied, Conflict, UnknownCommand };

class ShortcutMap {
public:
    void ResetDefaults(const CommandRegistry& registry);
    const ShortcutBinding* FindByCommand(const std::wstring& commandId) const;
    const ShortcutBinding* FindByChord(KeyChord chord, ShortcutScope scope) const;
    std::vector<ShortcutBinding> EffectiveBindings() const;
    RebindResult Rebind(const std::wstring& commandId, KeyChord chord, bool reassignConflict);
    bool Remove(const std::wstring& commandId);
    bool Load(const std::filesystem::path& path, const CommandRegistry& registry,
              const std::function<void(const std::wstring&)>& log = {});
    bool Save(const std::filesystem::path& path) const;
    static bool IsWindowsReserved(KeyChord chord);

private:
    std::unordered_map<std::wstring, ShortcutBinding> defaults_;
    std::unordered_map<std::wstring, std::optional<ShortcutBinding>> customizations_;
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

} // namespace ffui
