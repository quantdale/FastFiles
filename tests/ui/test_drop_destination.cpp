// stabilize-live-uia-e2e: regression for the drop-destination pane mapping.
// The product resolves a file drop's destination from the drop POINT's pane
// (not the active pane, whose activation the drag's own mouse-down changes),
// so a pane-to-pane drag lands in the dropped-on pane. This test pins the
// pure pane-split decision.

#include "OleDragDrop.h"

#include "../TestSupport.h"

using namespace fftest;

int main() {
    // Left half of the content width -> pane 0.
    Check(ffui::PaneIndexForDropPoint(10.0f, 800.0f) == 0, "left-half drop maps to pane 0");
    Check(ffui::PaneIndexForDropPoint(399.0f, 800.0f) == 0, "just-left-of-center drop maps to pane 0");
    // Right half -> pane 1.
    Check(ffui::PaneIndexForDropPoint(400.0f, 800.0f) == 1, "center drop maps to pane 1");
    Check(ffui::PaneIndexForDropPoint(799.0f, 800.0f) == 1, "right-half drop maps to pane 1");
    // Degenerate viewport width never indexes out of range.
    Check(ffui::PaneIndexForDropPoint(100.0f, 0.0f) == 0, "zero viewport width maps to pane 0");
    Check(ffui::PaneIndexForDropPoint(-5.0f, 800.0f) == 0, "negative x maps to pane 0");

    // stabilize-live-uia-e2e: the screen->client conversion inverts the UIA
    // screen mapping (screen = (dip + originDip) * scale + clientOrigin).
    // Window at client origin (400, 300) on screen, scale 1.0, content
    // origin at dip 200, viewport width 1000.
    const POINT clientOrigin{400, 300};
    // Pane.1 center: client dip 200 + 750 = 950 -> screen 400 + 950 = 1350.
    const POINTL pane1Center{1350, 700};
    Check(ffui::DropPointClientXDip(pane1Center, clientOrigin, 1.0f, 200.0f) == 750.0f,
          "pane-1 screen point converts to its viewport-relative dip x");
    Check(ffui::PaneIndexForDropPoint(ffui::DropPointClientXDip(pane1Center, clientOrigin, 1.0f, 200.0f), 1000.0f) == 1,
          "pane-1 screen point resolves to pane 1");
    // Pane.0 center: client dip 200 + 250 = 450 -> screen 400 + 450 = 850.
    // The conversion returns the viewport-RELATIVE dip (450 - 200 = 250),
    // which is the coordinate PaneIndexForDropPoint compares.
    const POINTL pane0Center{850, 700};
    Check(ffui::DropPointClientXDip(pane0Center, clientOrigin, 1.0f, 200.0f) == 250.0f,
          "pane-0 screen point converts to its viewport-relative dip x");
    Check(ffui::PaneIndexForDropPoint(ffui::DropPointClientXDip(pane0Center, clientOrigin, 1.0f, 200.0f), 1000.0f) == 0,
          "pane-0 screen point resolves to pane 0");
    // 150% scale: screen = (dip + 200) * 1.5 + 400.
    const POINTL scaledPane1{400 + static_cast<LONG>((200.0f + 750.0f) * 1.5f), 700};
    Check(ffui::DropPointClientXDip(scaledPane1, clientOrigin, 1.5f, 200.0f) == 750.0f,
          "scaled screen point converts back to its viewport-relative dip x");
    // The pre-fix formula (no client-origin term) mis-mapped this same point
    // past the viewport width, tripping the out-of-range fallback.
    const float oldFormulaX = static_cast<float>(pane1Center.x) / 1.0f - 200.0f;
    Check(oldFormulaX >= 1000.0f, "pre-fix conversion would have fallen back (regression premise)");

    if (FailureCount() != 0) {
        fprintf(stderr, "ffdrop_destination_tests: %d check(s) failed\n", FailureCount());
        return 1;
    }
    printf("ffdrop_destination_tests: all checks passed\n");
    return 0;
}
