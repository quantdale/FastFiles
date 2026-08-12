# Data-structure safety validation — 2026-08-11

## Scope

This slice closes tasks 3.1–3.4 and 8.2/8.5 of
`address-engineering-audit-findings`. It does not modify signing material,
authentication policy, IPC, or privileged-path behavior.

## Root cause and bounded fix

`FlatHashMap::operator[]` previously inserted without a growth preflight, and
its linear probe loop had no termination bound. The implementation now grows
before insertion at the 75% live-key threshold, limits each probe to 64 steps,
and reports a `std::runtime_error` when insertion cannot find a slot within
that bound. `EntryKeyHash` now mixes its result so normal sequential FRNs do
not reach the adversarial probe path under the bounded map. A constant-hash
test still proves that the abort path remains active.

## Evidence

- `ffindexstore_flat_hash_map_tests`: PASS. Repeated `operator[]` insertion
  retained 128 keys across rehashes, and the constant-hash sequence aborted at
  the 64-step bound while preserving earlier entries.
- `ffengine_index_pipeline_tests`: PASS. The 4,100-record projection cap
  fixture completes in the normal Debug path; this also guards the production
  `EntryKeyHash` distribution against the bounded-probe limit.
- Debug build: PASS (`cmake --build --preset debug`).
- Analyze build: PASS (`cmake --build --preset analyze`).
- Release build: PASS (`cmake --build --preset release`).
- Serial Debug CTest: PASS, 45/45.
- Serial Release CTest: PASS, 45/45.
- Windows build verifier: PASS (`pwsh ./verify/verify.ps1 build`).
- OpenSpec strict validation: PASS, 45/45 (`openspec validate --all --strict`).

## Changed files

- `src/indexstore/include/ffindexstore/FlatHashMap.h`
- `src/indexstore/include/ffindexstore/Identity.h`
- `tests/indexstore/test_flat_hash_map.cpp`
- `tests/indexstore/CMakeLists.txt`
