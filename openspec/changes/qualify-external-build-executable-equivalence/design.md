## Context

Current evidence: `closure-equivalence-fullsha.log` and `equivalence/fixtures-red.log` under the 2026-11-17 production-readiness session. The guard hard-codes `^build/` for generated translation units and includes; external generated RC/header files fail the Git-input proof. A relative TU additionally remains `../source tree/src/main.cpp` instead of resolving against its compilation directory.

## Decisions

### D1 — Provenance-bound generated root, not arbitrary external-source exclusion

Use the directory containing the actual compile database as a candidate binary root. If its CMake cache exists, require one unambiguous CMAKE_HOME_DIRECTORY matching RepoRoot and one CMAKE_CACHEFILE_DIR matching the binary root (Windows case-insensitive normalized paths). Reject a binary root equal to or containing the source tree. A missing cache grants no generated-file exemption. Cache metadata establishes the conventional configured build location, not an adversarially trusted or signed artifact.

Only existing files beneath this proven binary-root component boundary may be treated as generated. Tracked paths from either compared commit take precedence, even if located beneath the binary root. Missing files, prefix siblings, parent traversal and files outside this root cannot hide behind the exemption. Generated recipes/templates remain included in the digest. The claim remains Git-controlled source equivalence, not equality of mutable generated outputs.

### D2 — Recorded compilation directory owns relative paths

Resolve relative directory entries against the compile-database directory, then file/include paths against that compilation directory, as required by the JSON compilation-database specification. Keep normalized repository-relative tracked paths and content-sensitive blob hashes. Existing recursive header/RC manifest/template and symmetric added-input checks remain enforced.

### D3 — Real-child fixtures before acceptance

A disposable Git fixture with source/build paths containing spaces runs the actual guard. Positive arms cover conventional/internal and external roots, relative paths and location-independent digest equality. Negative arms cover missing generated files, missing/foreign/moved/duplicate cache provenance, unrelated external sources, nested-header/manifest/template content changes and new product inputs. Add explicit tracked-under-build-root controls. The real unmodified isolated Debug/release-test compile databases qualify an implementation-to-documentation pair; do not filter or fabricate those records.

## Non-goals

No build/debug rebuild, trust policy changes, privileged runtime qualification, general CMake interpreter, SDK hashing or reproducible-binary claim.
