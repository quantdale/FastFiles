## Review

**Reviewed PR16 SHA:** `e75a230db9ac89f6a638754cb6ad672735e4d644`  
**Base:** `a26fe609439a18c128b2a2e557bc7eb2534fa128`

Read applicable `AGENTS.md`, inspected the supplied full committed-range diff (`pr16-full.diff`), and independently traced checked-out source. The diff contains exactly four files.

No issues found.

### Correct

- **First F9 visibly reverses FollowSystem:** `ThemeToggle.h:15–30` resolves the system state before choosing an explicit opposite preference:

  | Starting preference | Windows light | Windows dark |
  |---|---|---|
  | FollowSystem | Dark | Light |
  | Light | Dark | Dark |
  | Dark | Light | Light |

  This matches the actual rendering predicate in `src/ui/src/WindowShell.cpp:1309–1313`. The previous light-system reproduction—FollowSystem renders light, first F9 selects Light again—is eliminated.

- **Real product handler uses the fix:** `src/ui/src/WindowShell.cpp:2921–2929` invokes the helper, persists settings, and calls `ApplyTheme()`. Theme publication, resource invalidation, and repaint remain on the existing path (`:1309–1363`).

- **Settings restoration and persistence remain intact:** Settings refreshes its draft on reopening (`src/ui/src/SettingsDialog.cpp:160–170`), offers Follow System (`:489–491`), reads it back (`:1248–1252`), and commits only on OK (`:300–316`). The shell callback saves and reapplies it (`src/ui/src/WindowShell.cpp:1136–1143`). JSON serialization/loading preserves Light, Dark, and Follow-System (`src/protocol/src/Settings.cpp:178–180, 275–280, 345–382`). FollowSystem resumes OS-change handling at `WindowShell.cpp:2296–2301`.

- **Gallery exercises actual application rendering:** Gallery is gated to `_DEBUG` (`src/ui/src/Gallery.cpp:40–45`); non-debug startup rejects `--gallery` (`src/ui/src/Main.cpp:17–21`). It uses the same shell and published theme (`WindowShell.cpp:927–928, 1536–1558`). The visual catalog sends real F9 input and checks observed brightness, including dark/narrow/wide variants (`verify/visual/Invoke-VisualCatalog.ps1:724–803`).

- **Keyboard scope is unchanged:** F9 remains shell `WM_KEYDOWN` handling, not a newly global shortcut. Registered shortcuts and dialog routing retain precedence (`WindowShell.cpp:1258–1289`). Child-control focus or a customized F9 binding can therefore prevent reaching this handler; that behavior predates PR16.

- **Focused regression coverage is correctly registered:** `tests/ui/test_theme_toggle.cpp:20–49` covers both FollowSystem resolutions, all explicit-state/system combinations, and rendered-state inversion. `tests/ui/CMakeLists.txt:310–314` registers the header-only test with the required include paths.

### PR15 compatibility

Compared against exact PR15 SHA **`8c4c8481f312aa4651a5c2bab3cb61e0b0e50af0`**, using supplied `pr15-executable.diff` from `d3884eecd03805a91fe5d12d7a90ceb1b5c082f2`.

No conflicting executable changes found: PR15 changes engine/indexstore/SQLite and their tests, not PR16’s four paths or theme/settings contracts. Its inherited `ffclipboard_classifier_tests` CMake entry must remain intact during any later integration. No integration was performed.

### Validation limits and focused commands

**No builds, tests, application launches, edits, or ref changes were performed.** Working tree remained unchanged. Rendering and persistence conclusions above are source-verified, not runtime certification.

For an authorized later validation, using binaries built from the exact reviewed SHA:

```powershell
ctest --test-dir build/debug -R '^(fftheme_toggle_tests|ffgallery_tests|ffprotocol_tests)$' --output-on-failure
```

On an isolated interactive profile, verify the six transitions above, reopening Settings after F9, restoring Follow System with OK, Cancel behavior, and restart persistence. The existing visual check is:

```powershell
pwsh ./verify/visual/Invoke-VisualCatalog.ps1 -Gallery -OutputRoot build/pr16-gallery-review
```

Do not initiate rebuilding under the current disk constraint merely for this review.

**Merge verdict: OK with notes** — scoped source review approves PR16; exact-SHA runtime validation remains unperformed, and this is not approval of PR15 or a combined tree.