# Executable-tree equivalence gate — independent validation + adversarial matrix

Closure-campaign validation of `verify/qualification/Test-ExecutableTreeEquivalence.ps1`
(PHASE B of the PR #14 closure mission), 2026-10-21, campaign host (checkout
root `C:\Users\Michael Roy\Documents\FastFiles` — itself a space-containing
path). Two stages are recorded honestly: the gate **as it stood at the
certified tip** (which found one real derivation gap) and the gate **after the
gap was fixed**, with the full adversarial matrix re-run.

## 1. What the gate claims (and what it does not)

- **Claim (narrow, exact):** the modeled set of **Git-controlled
  executable-impacting inputs** — compile-database translation units, their
  recursively resolved `#include` closure, RC-referenced resource files, and
  CMake/version build inputs — is **unchanged in content** between the two
  compared commits. Digests hash `path + git blob id` pairs, so content
  identity is proven, not filename identity.
- **Not claimed:** bit-for-bit reproducible binaries. Compiler version, SDK
  version, linker, environment-derived build inputs (e.g. the generated
  `BuildIdentity.h` embeds git SHA/config/arch), and build timestamps are
  outside the claim. A binary certified at `implementation_sha` is not
  invalidated by a later evidence-only commit; it is not proven identical to
  a binary rebuilt later.
- **Fail-closed rule:** any unproven derivation coverage (unresolved quoted
  project include, unresolved RC resource, derived path missing from either
  commit) forces a non-PASS verdict even when digests match.

## 2. Identity model

- `implementation_sha` = `43fc3c175d4a940a094e1261b1c96aa8bf0668d1` (last
  executable-impacting commit: six-lane review remediation).
- `certified_binary_sha`: executables built at `43fc3c1` era (identity
  binding verified by `ffbuild_identity_binding_tests`); **not** claimed
  reproducible.
- `evidence_sha` (stage-1 inspection) = `2f365a52748511420499b5d3459e5560739ea958`.
- `repository_tip_sha` at final gate run is recorded **outside the checked
  tree** (PR #14 description + final owner report) per the non-self-reference
  rule; raw JSON verdicts live in ignored `build/equiv-matrix/`.

## 3. Stage 1 — validation of the gate as it stood (pre-fix)

Digests: `implementation == evidence ==
cc97f4708d6eefef49ceec9d8589c73ce002e3505bc7731f600bbc775e0eb7a3` at 393
paths (313 compile-DB translation units + closure headers + 33 build inputs;
header entries overlap the TU count via `target_sources` listings).

| Control | Expected | Observed (pre-fix) | Evidence |
|---|---|---|---|
| A: implementation vs evidence-only descendant (`43fc3c1` vs `2f365a5`) | PASS | **PASS**, identical digests | `control-a-tip.json` |
| B: compiled `.cpp` content change | FAIL | **FAIL**, `pathsWithChangedContent=[src/indexstore/src/Projection.cpp]` | `control-B.json` |
| C: reachable product header change | FAIL | **FAIL**, changed `src/indexstore/include/ffindexstore/Projection.h` | `control-C.json` |
| D: nested header change (only via `columnview.h`) | FAIL | **FAIL**, changed `src/ui/src/HomeData.h` — recursive include traversal confirmed | `control-D.json` |
| E: build-affecting CMake input (`target_compile_definitions`) | FAIL | **FAIL**, changed `src/indexstore/CMakeLists.txt` | `control-E.json` |
| F: unresolved quoted project include, evidence side | FAIL | **FAIL** (content change detected) | `control-F.json` |
| F′: unresolved quoted include on the **derivation** side (self-comparison) | FAIL_CLOSED | **FAIL_CLOSED**: digests equal (`38fb4512…`) but coverage UNPROVEN — `unresolved-quoted-include: src/indexstore/src/Projection.cpp -> equiv_matrix_unresolvable_probe.h`; "certification MUST NOT be carried forward" | `control-F-failclosed.json` |
| G: space-containing checkout root | PASS | see §3.1 | `control-G*.json` |
| H1: delete a compiled input | FAIL | **FAIL**, fail-fast `Derived translation units are absent from one of the commits: src/indexstore/src/NamePool.cpp` | `control-H1.log` (no JSON: throw precedes write) |
| H2: add + register a compiled input | FAIL | **FAIL**, changed `src/indexstore/CMakeLists.txt` (the added TU is outside the fixed compile DB; its owning manifest change is the derived signal — documented nuance) | `control-H2.json` |
| I: documentation-only change | PASS | **PASS**, identical digests | `control-I.json` |
| J: RC-embedded manifest content change | FAIL | **PASS (WRONG) — gap demonstrated** | `control-J-prefix.json` |

Synthetic commits were created in disposable worktrees
(`build/equiv-matrix/wt`, ignored), never on `main` or real product data.

### 3.1 Control G (path quoting) — three escalating runs

The repairs under test: `Start-Process -ArgumentList` losing the `git -C`
path boundary, and the `-I` include-root parser truncating quoted paths at
whitespace.

- **G-1** (substituted compile DB, never-configured checkout): full
  derivation succeeded from the space-path root — 313 TUs, 33 build inputs,
  digests equal — and the only failures were 7 generated-header existence
  checks (`ffsetup/FastFilesVersion.h`, `ffsetup/BuildIdentity.h`,
  `ffsetup/PinnedSignaturesConfig.h`) whose build outputs do not exist in a
  never-configured tree. That is the documented **fail-closed** generated
  boundary, not a parse failure.
- **G-2** (real `cmake --preset release-test` configure in
  `build/equiv-matrix/space path/wt-g`): genuine compile DB with
  space-containing `-I"…"` roots parsed correctly; 4 of 7 generated headers
  resolved (configure-time codegen); 3 remained (build-time
  `fastfiles_build_identity` codegen) → still fail-closed.
- **G-3** (configure + `fastfiles_build_identity` target): **PASS**, 393
  paths, identical digests, coverage verified — from a checkout root
  containing spaces.

Conclusion: the quoting repairs work under space-containing paths at every
level (`git -C`, quoted `-I` roots, `Get-BlobText` rev:path probes). The
residual generated-header condition is a **derivation-model boundary**: the
gate requires a configured/built tree to resolve build-output includes, and
fails closed (never passes) without one.

### 3.2 Closure completeness (independent ground truth)

An independent Python traversal (own resolver, no verifier code) over the
same compile DB and implementation tree found: 204 unique translation units,
**all 156 tracked headers reachable**, 33 build inputs = the verifier's 393
paths exactly; **zero** tracked headers outside the closure; the 6 vendored
`sqlite3.c` quoted toolchain includes are the only unresolved-quoted notes.
The 313 translation-unit entries vs 204 unique files reflects sources
compiled in more than one configuration (e.g. `Projection.cpp` with
`FF_INDEXSTORE_TESTING`).

## 4. The gap found and fixed (in-scope defect)

`src/ui/src/FastFiles.rc` embeds the application manifest through the
resource compiler: `1 RT_MANIFEST "FastFiles.manifest"`. This is **not** an
`#include`, so the closure never derived `src/ui/src/FastFiles.manifest` — a
manifest-only change would move the shipped binary without moving the gate
digest. Control J (append-only manifest change) proved it red-first:
**pre-fix verdict PASS** (`control-J-prefix.json`).

Fix (`verify/qualification/Test-ExecutableTreeEquivalence.ps1`): RC files in
the closure are scanned for quoted resource references with known
resource-file extensions (`manifest|ico|cur|bmp|jpe?g|png|gif|rc2|rct|tlb|wmf|emf|rtf`);
each resolves like a quoted include relative to the `.rc`, enters the derived
set, and an unresolved one is a coverage failure (fail-closed). Ordinary RC
string literals cannot match the extension whitelist. The derivation set
grows 393 → **394** paths and the digest value changes (expected: the
derivation model is now stronger); the equivalence conclusion for
`43fc3c1 ↔ 2f365a5` is unchanged.

## 5. Stage 2 — full adversarial matrix at the fixed gate

Digests: `implementation == evidence ==
6e7642640a5a00783e0acd1761c1eafebe2bd612812b2ea9583775c50946a1a6` where
equivalent; 394 paths (313 TU entries + 47 closure-discovered inputs + 33
build inputs + manifest, with header/TU listings overlapping as above).

| Control | Implementation SHA | Evidence SHA | Expected | Observed | Inputs | Coverage | Failure reason / diff |
|---|---|---|---|---|---|---|---|
| A | `43fc3c175d4a…` | `2f365a527485…` | PASS | **VERIFIED_PASS** | 394 | verified | — (digests equal `6e764264…`) |
| B | `43fc3c175d4a…` | `913341d25d58…` | FAIL | **VERIFIED_FAIL** | 394 | verified | content-changed `src/indexstore/src/Projection.cpp` |
| C | `43fc3c175d4a…` | `33b192011d12…` | FAIL | **VERIFIED_FAIL** | 394 | verified | content-changed `src/indexstore/include/ffindexstore/Projection.h` |
| D | `43fc3c175d4a…` | `09e90be9ae20…` | FAIL | **VERIFIED_FAIL** | 394 | verified | content-changed `src/ui/src/HomeData.h` (nested) |
| E | `43fc3c175d4a…` | `b1a39248f281…` | FAIL | **VERIFIED_FAIL** | 394 | verified | content-changed `src/indexstore/CMakeLists.txt` |
| F′ | `7b7660f19def…` (self) | `7b7660f19def…` | FAIL_CLOSED | **VERIFIED_FAIL (fail-closed)** | 394 | **UNPROVEN** | `unresolved-quoted-include: src/indexstore/src/Projection.cpp -> equiv_matrix_unresolvable_probe.h`; equal digests refused |
| G | `43fc3c175d4a…` | `2f365a527485…` | PASS | **VERIFIED_PASS** (space-path root `…\space path\wt-g`, genuine compile DB) | 394 | verified | — |
| H1 | `43fc3c175d4a…` | `5567a92c3483…` | FAIL | **VERIFIED_FAIL** (fail-fast) | n/a | n/a | `Derived translation units are absent from one of the commits: src/indexstore/src/NamePool.cpp` |
| H2 | `43fc3c175d4a…` | `91a6cffb0d0f…` | FAIL | **VERIFIED_FAIL** | 394 | verified | content-changed `src/indexstore/CMakeLists.txt` (added `src/EquivMatrixAdded.cpp` registered there) |
| I | `43fc3c175d4a…` | `400cd40f56bb…` | PASS | **VERIFIED_PASS** | 394 | verified | — (docs-only) |
| J | `43fc3c175d4a…` | `0fb8b03f60a1…` | FAIL | **VERIFIED_FAIL** | 394 | verified | content-changed `src/ui/src/FastFiles.manifest` (pre-fix: wrongly PASS) |

## 6. Mechanism properties proven

1. **Content identity, not filenames** — controls B/C/D/E/J change content at
   unchanged paths with identical path sets and still flip the verdict
   (`pathsWithChangedContent` names each file).
2. **Real include closure** — control D catches a header reachable only
   through another header; independent traversal proves all 156 tracked
   headers are inside the closure (empty/incomplete closure impossible
   silently: `no-translation-units-derived` and unresolved-quoted checks fail
   closed).
3. **Fail-closed on derivation errors** — F′, H1, G-1/G-2 refuse a verdict
   (including equal-digest cases) instead of passing with incomplete
   coverage.
4. **Path-quoting repairs hold** — G-1..G-3 from a checkout root containing
   spaces, both substituted and genuine MSVC compile databases.

## 7. Residual boundaries (recorded, not fixed)

- The resource-reference whitelist is extension-based; a future RC-referenced
  resource type outside the list would need the list extended (the `.rc`
  itself is derived as a TU, so the reference line changing is caught; only a
  change confined to a new resource file type would be missed).
- Unresolved **angle** includes are assumed to be SDK/STL and skipped; a
  project angle include that stopped resolving would be missed (project
  includes in this repo use quoted or recorded-root forms; the `-I` roots are
  derived from the compile DB).
- `Get-TrackedPaths` splits `ls-tree -z` output on newlines as a fallback;
  a tracked path containing a newline could confuse it (none exist; Git
  quoting also escapes such names in this configuration).
- Generated (build-output) includes are out of the Git-controlled claim and
  resolved only for existence; their content is covered by the CMake
  generator inputs (`cmake/`), not by digest.
