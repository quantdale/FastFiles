# TreemapLayout catch-all stress probe (pre-fix defect + post-fix validation)

Probe: `tools/treemap_stress_probe.cpp` (temporary, not committed).

## Pre-fix defect (old per-rect `std::max(kMinRectSize, ...)` clamp)

Measured on 100x100 container, 1 large + 10 medium + 100 tiny (sizes 1e6/1e5/100):

- 111 nodes -> 111 rects, **4950 overlapping pairs**
- **100 rects escaped the parent bounds**
- covered area 49950% of the container (runaway coverage)
- hit-test ambiguity: the last-inserted rect owns every overlapped point

Root cause: clamping every generated rectangle to a 4px minimum dimension
corrupts the squarified tiling invariant (shares no longer sum to 1).

## Post-fix validation (new partition + exact band math, no clamping)

| Fixture | nodes | rects | overlaps | outOfBounds | covered | catch-all |
|---|---|---|---|---|---|---|
| 1 large + 10 medium + 100 tiny | 111 | 12 | 0 | 0 | 100% | 1/100 members |
| 300 equally tiny | 300 | 1 | 0 | 0 | 100% | 1/300 members |
| 1000 equally tiny | 1000 | 1 | 0 | 0 | 100% | 1/1000 members |
| 8:4:2 baseline | 3 | 3 | 0 | 0 | 100% | none |
| 1 large + 500 tiny | 501 | 2 | 0 | 0 | 100% | 1/500 members |

Catch-all participates by merged size (max tile 95.24% for 1e6/(1e6+500*100)
= 95.24% correct; 8:4:2 max tile 57.14% = 8/14 correct). Unknown-size
(calculating) nodes are never merged into the catch-all and use a minimum-share
placeholder so they stay visible/clickable.

Run: `cl /std:c++17 /EHsc /I src\ui\include tools\treemap_stress_probe.cpp`
then execute. MSVC 19.x, Debug host.
