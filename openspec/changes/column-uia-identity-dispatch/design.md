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
