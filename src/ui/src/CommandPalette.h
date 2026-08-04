#pragma once

#include <functional>
#include <string>
#include <vector>
#include <windows.h>

#include "CommandSystem.h"

namespace ffui {

class CommandPalette {
public:
    ~CommandPalette();
    bool Initialize(HWND owner, const CommandRegistry* registry, const ShortcutMap* shortcuts,
                    std::function<void(const std::wstring&)> execute);
    void Show(CommandContext context);
    void Hide();
    bool Visible() const { return visible_; }
    bool HandleMessage(const MSG& message);
    bool HandleOwnerCommand(WPARAM wParam, LPARAM lParam);
    void SetDarkTheme(bool dark);
    // WM_DRAWITEM from the owner window for the palette's owner-drawn list box.
    bool HandleDrawItem(LPARAM lParam);
    // WM_CTLCOLOREDIT / WM_CTLCOLORLISTBOX from the owner window; returns the
    // themed background brush (never 0 on success), or 0 when the message
    // targets a control the palette does not own.
    LRESULT HandleCtlColor(UINT message, WPARAM wParam, LPARAM lParam);
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
    bool darkTheme_ = false;
    // Themed chrome: Segoe UI 14 DIP font (created in Initialize) and cached
    // GDI brushes (created on first paint, deleted on theme change/teardown).
    HFONT paletteFont_ = nullptr;
    HBRUSH ctlEditBrush_ = nullptr;
    HBRUSH ctlListBrush_ = nullptr;
    HBRUSH ctlAccentBrush_ = nullptr;
};

} // namespace ffui
