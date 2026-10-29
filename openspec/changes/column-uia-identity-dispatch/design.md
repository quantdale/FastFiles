## Context
Baseline is verified product branch 981a9ba, isolated in column-uia-20261025. Item providers resolve identity and then call ordinal-only UiaSurface actions. Engine directory callbacks can update rows under columnsMutex_, so owner-thread dispatch alone is insufficient.

## Goals / Non-Goals
Preserve original identities through execution and reject retired/hidden fragments without side effects. Keep UIA projecting production state and canonical interaction helpers. Do not alter process boundaries, add accessibility shadow state, or claim unsupported physical display validation.

## Decisions
A closed request carries action kind, pane/column/row, column path hash, original item identity, and owner/fragment lifetime tokens. The native adapter synchronously dispatches to the owning HWND. Validation and model mutation share columnsMutex_; activation copies its target while holding that lock. No lock spans SendMessage, shell callback, navigation or UIA notification. Add/Remove evaluate current membership at mutation time. Column focus validates its original path under the model lock; pane focus retains its original fragment. Root, pane and column focus are handled on the owner thread, without an off-thread fallback.

## Risks / Trade-offs
Synchronous owner dispatch follows existing Home conventions; teardown and retired tokens fail closed. Requests must never be re-stamped from the current row. Directory callbacks remain independently synchronized. Regression tests force replacement after the provider's resolve to expose the original bug deterministically.

## Validation
First demonstrate old provider failures. Run focused provider tests, strict warning builds and ordinary native navigation/search/storage journeys. Preserve current 126-test certification and restricted reaper exclusion. New source needs its own qualification; old independent reviews do not cover it.

## Native visibility correction

Unmasked d34198a capture and a bounded five-second red probe expose focus=True with IsOffscreen=True: the child row begins exactly at the pane's right edge. Owner mutation bypassed the shell's normal post-input location, preview and horizontal reveal path. Every closed Browse action now synchronously enters the same owner-window message boundary, including same-thread callers. The shell shares `ReconcileBrowseInteraction` with mouse/keyboard input. ScrollItem reveals its requested pane without changing the active pane. Existing animation, DPI, High Contrast and reduced-motion policies are retained. A pure shared reveal calculation keeps the filename edge visible when a pane is narrower than a column. Native regressions require live physical row bounds inside the pane after root/row focus and child-folder Invoke, instead of accepting focus flags alone.

The first semantically passing narrow screenshot still failed visual review: Browse RenderPane lacked the per-pane clipping already used by Home, so pane1's partly scrolled parent column overpainted pane0. Browse now clips its complete paint scope to the pane viewport. Frame scroll synchronization precedes UIA geometry publication; rendering and geometry share one frozen pane1 offset. Actual before/after pixel review remains mandatory alongside bounds assertions. The new standalone native regression uses the existing strict provenance gate and marked synthetic fixtures.
