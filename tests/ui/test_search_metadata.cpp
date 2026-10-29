// indexed-search-allocated-size D7: headless tests for the shared Size-on-Disk
// wording helper. Pure header-only include (no windows.h / COM / renderer), so it
// compiles standalone like fftreemap_metadata_tests.

#include "ffui/SearchResultMetadata.h"
#include "ffui/SearchRowLayout.h"

#include <cstdint>
#include <string>

#include "../TestSupport.h"

using namespace fftest;

namespace {

// A plain byte formatter local to the test so the helper's semantics can be
// verified without pulling in the UI's own FormatSize wording.
std::wstring PlainBytes(uint64_t bytes) {
    return std::to_wstring(bytes) + L" B";
}

void TestSearchAllocatedSizeText() {
    for (const int dpi : {96, 120, 192}) {
        for (const int widthDip : {120, 320, 800}) {
            const auto scale = [dpi](int dip) { return dip * dpi / 96; };
            const auto layout = ffui::ComputeSearchContextLayout(scale(32), scale(widthDip),
                scale(8), scale(8), scale(460));
            Check(layout.locationLeft <= layout.locationRight &&
                  layout.locationRight <= layout.metadataLeft &&
                  layout.metadataLeft <= layout.metadataRight &&
                  layout.metadataRight <= scale(widthDip),
                  "location and long metadata never overlap at narrow/normal widths and real DPI");
            Check(layout.metadataRight - layout.metadataLeft <=
                  (layout.metadataRight - layout.locationLeft) * 45 / 100,
                  "metadata cannot displace the primary location at high DPI");
        }
    }
    const auto empty = ffui::ComputeSearchContextLayout(40, 20, 8, 8, 900);
    Check(empty.locationLeft == empty.locationRight && empty.metadataLeft == empty.metadataRight,
          "fully clipped row has empty valid bounds");
    // Unknown: explicit wording, never echoes a fabricated byte count.
    Check(ffui::SearchAllocatedSizeText(0, false, PlainBytes) == L"Size on disk unknown",
          "unknown (even when bytes are 0) reads as explicit unknown");
    Check(ffui::SearchAllocatedSizeText(123456, false, PlainBytes) == L"Size on disk unknown",
          "unknown with stray bytes still reads as explicit unknown");
    Check(ffui::SearchAllocatedSizeText(0, false, PlainBytes) != L"0 B on disk",
          "unknown is never rendered as zero on disk");

    // Known zero: distinct from unknown.
    Check(ffui::SearchAllocatedSizeText(0, true, PlainBytes) == L"0 B on disk",
          "known zero reads as '0 B on disk'");

    // Known nonzero: "<bytes> on disk".
    Check(ffui::SearchAllocatedSizeText(80, true, PlainBytes) == L"80 B on disk",
          "known nonzero reads as '<bytes> on disk'");

    // A default-constructed candidate (0 / false) is unknown, never 0 on disk.
    // Verified by the 0-byte unknown case above; kept explicit for the regression.
}

} // namespace

int main() {
    TestSearchAllocatedSizeText();
    if (fftest::FailureCount() > 0) {
        std::fprintf(stderr, "ffsearch_metadata_tests: %d check(s) failed\n", fftest::FailureCount());
        return 1;
    }
    std::printf("ffsearch_metadata_tests: all checks passed\n");
    return 0;
}
