# Gallery A/B regression attribution — controlled experiment (campaign section 5-7)

Date: 2026-10-23. Host: interactive console session, 1920x1080 primary,
**live system DPI 96 (100%)** at experiment time (verified via
`GetDpiForMonitor` + `AppliedDPI`; the harness's coverage line claiming
"this host is 125% / 120 DPI" is a **hardcoded string** in
`verify/visual/Invoke-VisualCatalog.ps1`, and the manifests record DPI 96 —
the historical "125% DPI" failure attribution traces to that stale string,
not to a measured DPI).

## Arms and control variables

| Variable | Arm A (baseline) | Arm B (candidate) |
|---|---|---|
| Branch / commit | `feat/large-index-lifecycle-and-search-scalability-20261008` @ `d3884eec` (= PR #14 HEAD) | `feat/durable-cold-search-and-hybrid-residency` @ `b68767a` (= PR #15 tip) |
| Binaries | fresh `debug-diagnostic` build embedding `d3884eecd03805a91fe5d12d7a90ceb1b5c082f2` | fresh `debug-diagnostic` build embedding `b68767a903a4455f1c3409330d836c8ceb50e00c` |
| Tree state at build | clean | clean |
| Harness | `verify/visual/Invoke-VisualCatalog.ps1` (arm A tree) | identical file — `git diff d3884ee..b68767a -- verify/visual` is EMPTY (only `verify/Invoke-CiParity.ps1` differs; `src/ui` diff is empty) |
| Invocation | `pwsh verify/visual/Invoke-VisualCatalog.ps1 -Gallery` | identical |
| Desktop session / DPI / resolution / capture technique | identical (same host, same session, sequential runs) | identical |

## Matrix result (2 runs per arm; 20 scenario records per run)

| Scenario | Arm A run1 | Arm A run2 | Arm B run1 | Arm B run2 |
|---|---|---|---|---|
| 16 core product scenarios | PASS 16/16 | PASS 16/16 | PASS 16/16 | PASS 16/16 |
| gallery-light | PASS | PASS | PASS | PASS |
| gallery-dark | **FAIL** | **FAIL** | **FAIL** | **FAIL** |
| gallery-narrow | **FAIL** | **FAIL** | **FAIL** | **FAIL** |
| gallery-wide | **FAIL** | **FAIL** | **FAIL** | **FAIL** |

Failure detail is byte-identical across arms/runs: `gallery-dark`
(`lightMean=234.8 darkMean=234.8` — F9 shows no flip), `gallery-narrow`
(`at 800px; mean=32.4`), `gallery-wide` (`at 1600px; mean=28`). Artifacts:
`evidence/artifacts/gallery-ab/` (run summaries + representative captures;
full run trees under `build/campaign/gallery-ab/`).

**A/B verdict: both branches fail identically, reproducible 2/2 at 100% DPI.
PR #15 is not the cause. `SUCCESSOR_REGRESSION` is disproven.**

## Root-cause attribution (controlled probes, `build/gallery-probe/probe-gallery.ps1`)

Discriminating experiments on Arm A (defect is inherited; identical UI code
in both arms):

1. **Resize-only (no F9), fresh process:** 1280px mean 237.7 (light) -> 800px
   mean 232.0 (light) -> 1600px mean 238.5 (light) -> back 237.7. **Resize
   rendering is healthy at every width.** The `gallery-narrow`/`gallery-wide`
   "renders empty/black" symptom is NOT a resize defect.
2. **F9 sequence, fresh process:** launch 237.7 -> F9 #1 -> 237.7 at
   +0.6 s/+1.5 s/+3 s/+6 s (**no visible change**) -> F9 #2 -> 27.9 (dark) at
   +0.6 s. Three-F9 run: #1 inert, #2 flips to dark, #3 flips back to light.
3. **Forced-repaint discriminator:** after inert F9 #1, a resize-forced
   repaint at +2 s **stays light** (237.3). Falsifies "toggle applied but
   repaint deferred". The toggle genuinely produced the same rendered theme.
4. **Input-delivery discriminator:** F5 (inert key) before F9 does not absorb
   the loss (F9 after F5 still inert); a **directly posted `WM_KEYDOWN`
   `VK_F9` to the window procedure is also inert**. Falsifies SendInput/focus
   delivery theories.
5. **Persisted-state trace (decisive):** the sandbox
   `%LOCALAPPDATA%\FastFiles\settings.json` shows `"appearance":
   {"theme":"Follow-System"}` at launch; after F9 #1 it is rewritten to
   `"theme":"Light"` (rendered no-op on a light system); after F9 #2 to
   `"theme":"Dark"` (visible flip).

**Root cause (code):** `src/ui/src/WindowShell.cpp:2921-2926` (WM_KEYDOWN
`VK_F9` handler):

```cpp
settings_.theme = settings_.theme == ffprotocol::ThemePreference::Light
    ? ffprotocol::ThemePreference::Dark : ffprotocol::ThemePreference::Light;
```

The default is `ThemePreference::FollowSystem`
(`src/protocol/include/ffprotocol/Settings.h:28`). The toggle maps every
non-`Light` value to `Light`, so from the default state the **first F9 is a
visible no-op on a light system** (`FollowSystem -> Light` renders the same),
the second F9 performs the first visible flip, and `FollowSystem` is never
restored. On a dark system the first F9 would flip visibly but the cycle
still silently exits `FollowSystem`. This is a **product F9 theme-toggle
state-machine defect**, not a Gallery rendering or harness defect.

## How this explains the historical symptom set

- `gallery-dark` FAIL: harness sends exactly one F9 expecting a flip; the
  first F9 is the no-op; `lightMean == darkMean`.
- `gallery-narrow`/`gallery-wide` FAIL: the harness sends a second F9
  expecting to be back in Light before resizing; that second F9 is the one
  that actually flips to Dark, so the 800 px/1600 px captures are dark-theme
  frames (means ~30) asserted against a light expectation.
- "F9 dark toggle does not produce the expected repaint": precisely the
  no-op first press; repaint machinery is healthy (forced-repaint probe).
- DPI is not a factor: reproduced at 100% DPI; the 125% claim is a stale
  harness string.

## Classification and disposition

`INHERITED_UI_OR_HARNESS_DEFECT`, refined: **`INHERITED_PRODUCT_UI_DEFECT`**
(F9 toggle state machine vs `FollowSystem`), proven predating PR #15
(identical failure on PR #14 HEAD; `src/ui` byte-identical between arms).
Not `SUCCESSOR_REGRESSION`; not `BLOCKED_ENVIRONMENT`; not `UNQUALIFIED`.

Disposition per campaign section 7: a **separate, narrowly scoped UI-focused
fix** is prepared on its own isolated branch (`fix/f9-theme-toggle-follow-system`
based on `origin/main`), with red-first unit coverage of the toggle state
machine and a rerun of the 4 Gallery scenarios on the fixed build. PR #15
keeps the Gallery finding honestly qualified and references this attribution;
it does not absorb the UI change. The same defect affects the real product's
F9 toggle (first press from default state does nothing on light systems),
which is the argument for accepting the UI fix branch on its own merits.

## Artifact index

- `evidence/artifacts/gallery-ab/arm-a-run{1,2}-summary.md`,
  `arm-b-run{1,2}-summary.md`, `arm-a-run1-manifest-v2.json`,
  `arm-b-run1-manifest-v2.json` — per-run scenario matrices (both arms).
- `evidence/artifacts/gallery-ab/arm-{a,b}-gallery-light.png` — the passing
  1280 px light captures per arm (renders healthy at 1280).
- `evidence/artifacts/gallery-ab/probe-f9-{1-inert,2-dark,3-light}.png`,
  `probe-resize-800-light.png`, `probe-f9-then-resize-stays-light.png`,
  `probe-log-final.jsonl` — discriminating probe evidence.
- Full probe captures: `build/campaign/gallery-ab/probe-artifacts/`
  (untracked bulk), probe harness `build/gallery-probe/probe-gallery.ps1`.
