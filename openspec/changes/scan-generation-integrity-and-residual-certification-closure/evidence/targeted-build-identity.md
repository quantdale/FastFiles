# Targeted-build provenance (tasks 7.1–7.3)

## Task 7.1 — map

`fastfiles_build_identity` (`cmake/FastFilesVersion.cmake`) is an `ALL` custom
target that runs `cmake/WriteBuildIdentity.cmake` on every build. That script
derives the identity from `FASTFILES_BUILD_ID` (override) or
`git -C <source> rev-parse HEAD`, validates it fail-closed as exactly 40 hex
characters, and writes two byte-stable outputs only when the bytes change:

- `generated/ffsetup/BuildIdentity.h` — `ffsetup::kBuildIdentity` (wide) and
  `kBuildIdentityAscii`, plus tree/config/arch/commit-time constants;
- `generated/ffsetup/BuildIdentity.rc.h` — the same values as
  `FF_BUILD_IDENTITY` / `FF_BUILD_SPECIAL` for the version resource.

`fastfiles_add_version_resource(target, original, description)` sets
`OBJECT_DEPENDS` on the generated `.rc` defines and
`add_dependencies(<target> fastfiles_build_identity)`. Consumers:

| Target | Executable | Source | Identity carrier |
| --- | --- | --- | --- |
| `FastFiles` | `FastFiles.exe` | `src/ui/CMakeLists.txt:188` | version resource (`PrivateBuild`) |
| `FastFilesEngine` | `FastFilesEngine.exe` | `src/engine/CMakeLists.txt:34` | version resource |
| `FastFilesIndexSvc` | `FastFilesIndexSvc.exe` | `src/indexsvc/CMakeLists.txt:73` | version resource |
| `FastFilesSetup` | `FastFilesSetup.exe` | `src/installer/CMakeLists.txt:15` | version resource |
| `fftest` | `fftest.exe` | `src/fftest/CMakeLists.txt:21` | version resource |

Other binaries that embed a source revision — through
`ffsetup/FastFilesVersion.h` → `BuildIdentity.h`, with **no** version
resource — are the installer and package-trust TUs:

- `src/installer/src/InstallSteps.cpp` (compiled into `FastFilesSetup.exe`);
- `tests/setup/PackageTrustFixtureSupport.h` +
  `tests/setup/test_package_trust.cpp` → **`ffsetup_package_trust_tests.exe`**;
- `tests/setup/test_package_trust_wintrust_integration.cpp` → the opt-in
  `ffsetup_package_trust_wintrust_integration_tests.exe` (only registered when
  `FASTFILES_ENABLE_PRIVILEGED_TRUST_TESTS=ON`).

Commands local and hosted CI use:

- hosted `debug` / `release-test` / `analyze`: `cmake --preset <preset>` then
  `cmake --build --preset <preset>` (full build), then CTest;
- hosted `asan-protocol-ipc-lifecycle`: `cmake --preset asan` then
  `cmake --build --preset asan --target ffprotocol_fuzz_tests
  ffipc_framing_tests ffipc_listener_lifecycle_tests
  ffvolumescanner_terminal_tests`;
- hosted `unsigned-release-candidate-qualification`: `cmake --preset
  release-test`, `cmake --build --preset release-test`, then
  `verify/release/tests/Test-ReleaseCandidate.ps1`;
- local: `pwsh ./dev.ps1 build -Preset <preset> [-Target <target>]`,
  `pwsh ./verify/verify.ps1 build`, `pwsh ./verify/Invoke-CiParity.ps1`.

## Task 7.2 — isolated revision A → revision B experiment

`build/_probe_identity.ps1` (evidence copy:
`evidence/targeted-build-identity.json`) created a detached git worktree at
revision A, configured `--preset debug`, and built the certification targets
there. It then advanced the source identity with an empty commit in that
worktree (revision B), ran each **targeted** build command separately, and read
each binary's embedded identity from the binary itself:

- version-resource executables: `(Get-Item exe).VersionInfo.PrivateBuild`;
- header-only test binary: a byte search for the ASCII and UTF-16LE identity
  literals inside the file.

```text
revisionA = 1172c1a1e6aa9c1a726d52abdade0975b6979e4b
revisionB = 66bb9cb39730d677fed396cd87a0360660627ec0   (isolated worktree commit)
```

| Binary | At A | After targeted build at B | Still A? |
| --- | --- | --- | --- |
| `FastFiles.exe` | A | **B** | no |
| `FastFilesEngine.exe` | A | **B** | no |
| `FastFilesIndexSvc.exe` | A | **B** | no |
| `FastFilesSetup.exe` | A | **B** | no |
| `fftest.exe` | A | **B** | no |
| `ffsetup_package_trust_tests.exe` | A | **B** | no |

Stale-artifact control: a copy of `FastFiles.exe` taken before the advance
still reports A and does **not** report B, so the check reads the artifact it
names and cannot pass because another target refreshed a shared header.

Override behaviour was validated separately:

- `FASTFILES_BUILD_ID=0123456789abcdef0123456789abcdef01234567` +
  `cmake --build --preset debug --target FastFilesEngine` → exit 0, the
  embedded `PrivateBuild` is exactly that override, and the build warns that it
  differs from `git HEAD`;
- `FASTFILES_BUILD_ID=not-a-40-hex-identity` → the targeted build exits 1 and
  the configure exits 1, both with
  `FASTFILES_BUILD_ID must be a 40-character Git identity` (fail-closed, no
  fallback identity written);
- a targeted build of the existing `debug-diagnostic` tree with the override
  moved `FastFiles.exe` to the override and a plain rebuild restored
  `1172c1a1e6aa9c1a726d52abadde...` (`matchesHead: true`), so the diagnostic
  tree is left at its normal identity.

The isolated worktree was removed after the run; the only residue is the
report itself (`git worktree list` shows only the pre-existing worktrees).

## Task 7.3 — no-code closure

Every certification-relevant binary that embeds a source revision refreshed to
revision B under the targeted commands above, with no global rebuild. The
existing identity target and its dependency edges are therefore sufficient, so
**no build file was edited** and the identity target's dependency edges are
unchanged. The fail-closed rejection of a malformed override and the
as-given embedding of a well-formed one are both preserved and proven.
