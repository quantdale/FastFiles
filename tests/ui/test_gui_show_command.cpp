// completion-integrity-hardening-and-release-certification 4.5.3: a GUI
// file manager launched from a hidden parent (UIA worker) must still show.

#include "ffui/GuiShowCommand.h"

#include "../TestSupport.h"

using namespace fftest;

namespace {
int Runtime(int value) {
    volatile int opaque = value;
    return opaque;
}
}  // namespace

int main() {
    Check(ffui::NormalizeGuiShowCommand(Runtime(SW_HIDE)) == SW_SHOWNORMAL,
          "SW_HIDE inherited from a hidden parent becomes SW_SHOWNORMAL");
    Check(ffui::NormalizeGuiShowCommand(Runtime(SW_SHOWNORMAL)) == SW_SHOWNORMAL,
          "SW_SHOWNORMAL is preserved");
    Check(ffui::NormalizeGuiShowCommand(Runtime(SW_SHOWMAXIMIZED)) == SW_SHOWMAXIMIZED,
          "SW_SHOWMAXIMIZED is preserved");
    Check(ffui::NormalizeGuiShowCommand(Runtime(SW_SHOWMINNOACTIVE)) == SW_SHOWMINNOACTIVE,
          "explicit minimize-on-start is preserved");
    if (FailureCount() != 0) {
        fprintf(stderr, "ffgui_show_command_tests: %d check(s) failed\n", FailureCount());
        return 1;
    }
    printf("ffgui_show_command_tests: all checks passed\n");
    return 0;
}
