## Context

The current `main` tip is `69e894f` at campaign start and already contains the
recent active-path and interactive UI runtime-determinism work. The current
architecture is authoritative. In particular, `ffui::NormalizeDisplayPath` is
already used across most user-facing path surfaces, Storage Analysis already
has an owner-drawn radio presentation plus a UIA fixup provider, and the shared
theme palette already has executable contrast coverage.

The stale PR #6 branch is locally available at
`origin/stabilization/ship-ready-2026-08-23` with head
`9ddf1a3189be32db11c97e7c79bb7404caac1d8f` and merge base
`90fa560a99217cabcf456b1198c83351c46513ed`. GitHub write authentication is not
available in this environment, so PR administration is an external handoff.

## Goals / Non-Goals

**Goals:**

- Reconcile every PR #6 commit and changed file against current main.
- Close only reproducible ship-readiness defects in path presentation, Storage
  Analysis interaction/state, theme invalidation, and touched accessibility
  seams.
- Keep user-facing labels separate from canonical operational identity and avoid
  network filesystem work from render/layout/UIA paths.
- Preserve current provider-generation, stale-element, pane-local, cleanup, and
  truthful foreground-skip contracts.
- Produce deterministic headless evidence and complete the applicable local
  validation ladder without elevation, signing, or trust-store mutation.

**Non-Goals:**

- No merge, cherry-pick, reset, or wholesale port of PR #6.
- No process-boundary, IPC, index, privileged-service, Authenticode, trust-store,
  raw MFT/USN, or release-signing changes.
- No palette redesign where current main already satisfies the requirement.
- No fake live UIA pass, retry-as-proof, pixel-only accessibility assertion, or
  deletion of user settings.

## Decisions

### D1: Current main and executable evidence outrank the stale PR

The final source, tests, current OpenSpec tasks, and fresh validation are the
source of truth. The PR audit is retained as a disposition record, not as an
implementation baseline.

**Why:** PR #6 predates later UI-v3, theme, Home, storage, provider-lifetime,
active-path, and runtime-determinism work. Applying its files wholesale would
discard newer behavior and reintroduce its known `/analyze` failure.

**Alternative rejected:** Rebasing or cherry-picking the PR and repairing the
result. The patch mixes useful intent with obsolete seams and unnecessary
documentation/churn.

### D2: PR #6 commit/file disposition

The complete audit is:

| Commit | File(s) | Classification | Current disposition |
| --- | --- | --- | --- |
| `73fc9fc` `fix(ui): hide local 8.3 names in navigation labels` | `src/ui/src/NavigationWorkspace.h` | Incomplete historical fix; superseded | Current `DisplayPath` is the shared path-presentation seam. Its local-only gate will be hardened; the PR inline Win32 helper is not ported. |
| `1525e91` `fix(ui): expand 8.3 names in tab labels` | `src/ui/src/NavigationChrome.h` | Superseded | Current tabs already consume the broader shared display seam; add only edge-case coverage. |
| `153d970` `refactor(ui): unify production theme palette` | `src/ui/src/UITheme.h` | Still-valid intent, independently implemented | Current main’s palette from the later convergence work is retained. No PR palette copy is imported. |
| `a16aab3` `fix(ui): theme Storage Analysis native controls` | `src/ui/src/StorageAnalysis.h` | Incompatible/insufficient | The current `StorageAnalysis.cpp` owner-drawn radio/subclass and list-header path are newer and preserve UIA semantics. The PR’s `ThemedStorageButton` wrapper is not ported; Back/Up receive a current-layer themed subclass. |
| `0f83b12` `test(ui): cover display-only 8.3 expansion` | `tests/navigation/test_home_location.cpp` | Incorrect for current warning policy | Its large stack fixture contributes to the historical MSVC C6262 `/analyze` failure. Coverage is rehomed to heap-backed display-path tests. |
| `9bd408b` `fix(a11y): make Storage chrome keyboard reachable` | `src/ui/src/StorageAnalysis.h` | Still-valid requirement, obsolete implementation | Current native controls remain the semantic authority. Explicit Back/Up tab stops and one radio-group entry are added at the current creation/subclass seam. |
| `a15b9e7` `test(ui): make short-name test includes explicit` | `tests/navigation/test_home_location.cpp` | Superseded/incorrect | Same historical stack-heavy fixture; replaced with dynamic storage and deterministic resolver cases. |
| `bf599d4` `docs: record ship-ready stabilization audit` | `docs/agent/SHIP_READY_STABILIZATION_2026-08-23.md` | Historical documentation only | Not copied. This campaign’s proposal, design, tasks, evidence, and final audit are authoritative for this run. |
| `814889f` `fix(a11y): restore accessible dark accent contrast` | `src/ui/src/UITheme.h` | Still-valid intent, independently ported | Current `textOnAccent` and palette values already contain the accessible dark selected-text correction. |
| `430c18a` `test(a11y): pin theme contrast floors` | `tests/ui/test_ui_style.cpp` | Superseded | Current tests cover a broader set of pairings, alpha compositing, focus, disabled, and status floors. |
| `5554432` `refactor(ui): preserve theme design-contract documentation` | `src/ui/src/UITheme.h` | Unnecessary churn | Current comments and token contract are retained without this historical edit. |
| `9ddf1a3` `test(a11y): cover tertiary theme text contrast` | `tests/ui/test_ui_style.cpp` | Superseded | Tertiary coverage already exists in current main’s stronger contrast matrix. |

No PR #6 commit is cherry-picked. The only reimplementation is the still-valid
behavioral subset identified in D3–D6 below.

### D3: Normalize only local display candidates

`StripDisplayPathPrefix` remains a pure operation. `NormalizeDisplayPath` and
its resolver-injected test seam first classify the path: only a DOS local drive
(`X:\...`) or an extended local drive (`\\?\X:\...`) may call the long-name
resolver, and only when a short-name marker requires it. Normal UNC and extended
UNC paths are returned in a display-safe prefix form without resolution. The
resolver uses heap-backed bounded storage and falls back to the original display
text on every failure.

**Why:** The existing early path check treats every extended path and every UNC
path containing `~` as eligible for `GetLongPathNameW`, which can perform
network I/O from a paint/UIA path. The PR’s inline helper avoided UNC but was
too narrow, duplicated the seam, and introduced a stack-heavy test fixture.

**Alternative rejected:** Resolve every path and cache it synchronously. A cache
would add lifetime/invalidation complexity and still require a safe network
policy. The contract only needs local best-effort normalization.

### D4: Preserve raw canonical text in editable mode

Breadcrumbs, tabs, Home, sidebar, Search locations, details, and UIA display
properties use the presentation seam. Entering editable address-bar mode uses
the context’s exact canonical `currentPath`; copy-path and file-operation
commands continue to receive their existing canonical strings. Default bookmark
labels may be generated from a display projection, but the stored bookmark path
is never changed.

**Why:** An editable address field is an explicit path exposure/editing surface,
not a decorative label. Normalizing it can silently change the text a user
copies or commits and can remove an extended prefix needed by Win32 operations.

### D5: Extend the current Storage Analysis presentation seam

The existing radio subclass/UIA provider remains in place. Add a small themed
push-button subclass for Back/Up that defers entirely to native behavior under
High Contrast, paints token-derived rest/hover/pressed/disabled/focus states
otherwise, and removes itself on `WM_NCDESTROY`. Use a pure style/state seam to
make Back/Up explicit tab stops and make only the first mode radio the group’s
Tab entry; native arrow navigation and mutual exclusivity remain responsible for
radio behavior.

`Up` is disabled when the current scope has no parent. `SetEngineActive` refreshes
the visible panel so degraded/unavailable wording changes immediately. Existing
list/header/treemap invalidation remains on the same `SetDarkTheme` path.

**Why:** The current main has the right custom radio layer but leaves Back/Up as
system-light, non-explicitly-tabbed buttons, gives every radio a Tab stop, and
does not refresh status text on an engine-state transition. These are current
defects, while the PR wrapper is an older and riskier way to address them.

### D6: Treat theme and UIA coverage as contracts, not redesigns

Keep the current palette and its executable contrast matrix. Extend only the
headless state/style tests needed for the new control geometry/state decisions.
The Storage controls remain real HWND controls and the existing UIA fixup
provider remains the accessibility source. No provider cache, pane fragment,
RuntimeId, AutomationId, or foreground behavior is changed.

**Why:** The newer UIA/runtime-determinism campaign explicitly made fragment
replacement and stale-provider failure safe. A presentation patch must not
reintroduce cached-fragment assumptions or accessibility-only state.

### D7: Validation is staged and bounded

Run display/storage/theme focused tests after each implementation slice, then
Debug/release-test, Analyze, ASAN protocol/IPC, verifier, policy, OpenSpec, and
live UIA/visual capabilities where available. The historical 20-run interactive
selection gate remains external when foreground ownership is unavailable and
cannot be relabeled as a product pass.

**Why:** Focused evidence localizes defects, and the repository’s warning-as-error
policy makes the old C6262 failure a required explicit regression check.

## Risks / Trade-offs

- [Risk] A local `GetLongPathNameW` call can still add latency for a genuinely
  short-name label. → Only paths containing `~` enter it, ordinary paths are a
  pure fast path, and failures are bounded/fallback-safe; no network path enters.
- [Risk] Only the first radio is a Tab stop, which is less discoverable if native
  arrow navigation is unavailable. → Real radio HWNDs retain keyboard focus and
  SelectionItem UIA semantics; focused/checked states are visibly distinct and
  the group’s first stop is deterministic.
- [Risk] An engine-state refresh can discard in-flight Storage aggregate rows.
  → Refresh already invalidates request IDs and reissues from current snapshot;
  stale callbacks are rejected by the existing monotonic request gate.
- [Risk] UIA clients may hold Storage elements across hide/show. → The existing
  owner-state lifetime and native HWND validity checks remain authoritative; no
  stale element is retargeted.
- [Risk] Live visual/UIA evidence may be blocked by foreground or display setup.
  → Record the established `interactive-foreground-unavailable` or environment
  category exactly; do not weaken the acceptance gate.

## Migration Plan

No persisted schema or operational migration is needed. Apply code and tests in
small focused slices, validate each slice, then run the full current ladder. A
rollback is a normal Git revert of this campaign’s commit(s); it does not touch
workspace state, indexes, services, trust stores, or signing material.

## Open Questions

None that change the selected contracts or implementation seams. Production
signing, elevated privileged-service validation, clean-host MFT/USN evidence,
and a foreground-controllable 20-run UIA selection gate remain external
prerequisites rather than implementation questions.
