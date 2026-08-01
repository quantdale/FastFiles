#pragma once

#include <functional>
#include <string>
#include <vector>
#include <windows.h>

#include "CommandSystem.h"

namespace ffui {

class CommandPalette {
public:
    bool Initialize(HWND owner, const CommandRegistry* registry, const ShortcutMap* shortcuts,
                    std::function<void(const std::wstring&)> execute);
    void Show(CommandContext context);
    void Hide();
    bool Visible() const { return visible_; }
    bool HandleMessage(const MSG& message);
    bool HandleOwnerCommand(WPARAM wParam, LPARAM lParam);
    void Reposition();

private:
    void RefreshResults();
    void ExecuteSelected();

    HWND owner_ = nullptr;
    HWND edit_ = nullptr;
    HWND list_ = nullptr;
    const CommandRegistry* registry_ = nullptr;
    const ShortcutMap* shortcuts_ = nullptr;
    std::function<void(const std::wstring&)> execute_;
    CommandContext context_;
    std::vector<PaletteResult> results_;
    bool visible_ = false;
};

} // namespace ffui
