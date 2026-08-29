#pragma once

#include <windows.h>

namespace ffui {

// GUI apps inherit STARTUPINFO.wShowWindow from a hidden parent (UIA worker,
// scheduled task, agent shell). SW_HIDE then keeps the main window invisible
// to both the user and Control-view UIA. File-manager launch must still
// present a window; explicit minimize/maximize nCmdShow values are preserved.
constexpr int NormalizeGuiShowCommand(int showCommand) {
    return showCommand == SW_HIDE ? SW_SHOWNORMAL : showCommand;
}

}  // namespace ffui
