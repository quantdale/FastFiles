# Targeted-build identity proof (tasks 7.1–7.3)

Status: the isolated A→B proof passed on executable candidate
`bac5d616070ec6accade403273501c43d44747e9`. Each identity-bearing target had a
separate clean worktree and build directory. The prior report in
`targeted-build-identity.json` used one shared build tree and is superseded: an
earlier target could refresh the generated identity before a later target was
tested.

## Task 7.1 — source and command map

`fastfiles_build_identity` (`cmake/FastFilesVersion.cmake`) is an always-run
custom target. `cmake/WriteBuildIdentity.cmake` reads the explicit
`FASTFILES_BUILD_ID` override or the worktree's `git rev-parse HEAD`, validates
exactly 40 hexadecimal characters, and writes byte-stable outputs only when
their contents change:

- `generated/ffsetup/BuildIdentity.h` — `ffsetup::kBuildIdentity` and
  `kBuildIdentityAscii`, plus tree/configuration/architecture/commit-time data;
- `generated/ffsetup/BuildIdentity.rc.h` — version-resource identity macros.

`fastfiles_add_version_resource` adds both the identity-target dependency and
the generated resource dependency. The package-trust targets include
`ffsetup/FastFilesVersion.h` from their fixture code, so they also need a
direct dependency on `fastfiles_build_identity`.

| Target / artifact | Preset and targeted command | Identity carrier |
| --- | --- | --- |
| `FastFiles` / `FastFiles.exe` | `release-test`; `debug-diagnostic` | VERSIONINFO `PrivateBuild` |
| `FastFilesEngine` / `FastFilesEngine.exe` | `release-test` | VERSIONINFO `PrivateBuild` |
| `FastFilesIndexSvc` / `FastFilesIndexSvc.exe` | `release-test` | VERSIONINFO `PrivateBuild` |
| `FastFilesSetup` / `FastFilesSetup.exe` | `release-test` | VERSIONINFO `PrivateBuild`; installer code also includes `FastFilesVersion.h` |
| `fftest` / `fftest.exe` | `release-test` | VERSIONINFO `PrivateBuild` |
| `ffsetup_package_trust_tests.exe` | `release-test` | Embedded UTF-16 build identity in the signed-package fixture |
| `ffsetup_package_trust_wintrust_integration_tests.exe` | `release-test` with `FASTFILES_ENABLE_PRIVILEGED_TRUST_TESTS=ON` | Embedded UTF-16 build identity; build only, no trust-store test execution |
| ASan protocol, IPC, scanner, and engine tests | `asan`, listed below | No `FastFilesVersion.h` include or source-revision carrier |

The version-resource targets above are the four release executables plus
`fftest`. The focused diagnostic executable is the `debug-diagnostic`
`FastFiles` target. Searching `src/` and `tests/` for `kBuildIdentity` and
`FastFilesVersion.h` found the package-trust fixture and its opt-in WinTrust
counterpart as the other compiled identity consumers.

The hosted targeted ASan command builds:

```text
cmake --build --preset asan --target ffprotocol_fuzz_tests ffipc_framing_tests ffipc_listener_lifecycle_tests ffvolumescanner_terminal_tests
```

The local campaign additionally targets
`ffengine_volume_session_manager_tests` and
`ffengine_legacy_scan_failure_binding_tests`. All six were inspected in the
isolated ASan build and contain neither revision A nor revision B; none includes
the generated identity header. Hosted `debug`, `release-test`, and `analyze`
use full preset builds; their targeted reproduction commands are recorded in
the result table below. The unsigned release-candidate gate also uses the full
`release-test` preset build.

## Task 7.2 — isolated revision A → revision B experiment

Reproducible probe: `targeted-build-identity-isolated.ps1`. It created one
short-path worktree per identity-bearing target, configured that target's
preset, built only the named target at A, copied the A artifact as a stale
control, made an empty commit B in that worktree, then built only the same
target and read the resulting binary. No other target could update that
worktree's generated identity between A and B. The short fixture root was
`D:\ffp\<run-id>` to keep the repository's long historical evidence paths
within Windows' path limit. All ten owned worktrees and the unique fixture
directory were removed; the pre-existing `.kilo/worktrees/scythe-cloud` was
untouched.

Revision A was the clean executable candidate above. Each B is the empty commit
created in that target's isolated worktree.

| Isolated target | Preset | Revision B | B refresh | Copied A artifact stayed A |
| --- | --- | --- | --- | --- |
| `FastFiles` | `release-test` | `1dac4883ea77bebe3363e88af5901c7a9228bbe8` | PASS | PASS |
| `FastFilesEngine` | `release-test` | `60076b59914f06f4e6beb54b015c6fe5ebdd02aa` | PASS | PASS |
| `FastFilesIndexSvc` | `release-test` | `470a22511d57ed53643d2ab8634b246e6004db74` | PASS | PASS |
| `FastFilesSetup` | `release-test` | `ea8c40b6c57a55c4f692cf2c2e3878b0b0b3e9a6` | PASS | PASS |
| `fftest` | `release-test` | `cb3266fadbe53c99819a83a1028d903cdc13f2ab` | PASS | PASS |
| `ffsetup_package_trust_tests` | `release-test` | `0896d4d8c0c5288efb421009e463e3d56cc539b2` | PASS | PASS |
| `ffsetup_package_trust_wintrust_integration_tests` | `release-test` | `b1459f8eac2ade9e991a66016de3636716dcd74f` | PASS | PASS |
| diagnostic `FastFiles` | `debug-diagnostic` | `4a81edcac7eac80b9646bef8258f869a5d8a8045` | PASS | PASS |

The ASan target command was run in its own worktree at A and again after empty
commit B `6de833f662f7841dbf9531b469eb81b58c95a533`. All six test executables
were present at both revisions and contained neither full identity. This is a
no-carrier classification, not a claim that these test binaries embed A or B.

The probe separately built `FastFilesEngine` with
`FASTFILES_BUILD_ID=0123456789abcdef0123456789abcdef01234567`; its VERSIONINFO
`PrivateBuild` matched the override exactly and the build warned that it differs
from Git HEAD. Explicit-empty and malformed override rejection are covered by
`ffbuild_identity_freshness_tests`; the candidate run is recorded with the
exact-candidate validation results in
`certification-residuals-and-handoff.md`.

Machine-readable per-target identities, exit codes, stale controls, ASan
classification, override result, and cleanup status are in
`targeted-build-identity-isolated.json`.

## Task 7.3 — dependency-edge correction

Review found the two package-trust executables compiled code that embeds the
generated identity but had no direct target dependency. Added
`add_dependencies(... fastfiles_build_identity)` to
`ffsetup_package_trust_tests` and the opt-in
`ffsetup_package_trust_wintrust_integration_tests`. The isolated experiment
proves both now transition from A to B under their own targeted build. The
existing version-resource dependency edges also refresh all five versioned
executables. No global rebuild dependency or unrelated build edge was added.

The identity freshness tests also require explicitly empty and malformed
`FASTFILES_BUILD_ID` values to fail closed, and preserve a well-formed override
as given. Those checks are part of the focused candidate gate; the standalone
probe above records the successful well-formed executable override.
