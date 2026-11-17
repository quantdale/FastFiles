# External-build executable equivalence qualification

## Root cause and correction

The previous guard discarded generated translation units/includes only when their repository-relative paths began with `build/`. The untouched isolated Debug compilation database instead lists five existing generated version RCs outside the checkout. The guard rejected these as missing Git inputs. Relative TU/include paths were also not resolved against the recorded compilation directory.

`Get-VerifiedGeneratedBuildRoot` now reads adjacent CMake cache metadata, requires exactly one matching source and cache-directory entry, and rejects a binary root containing the source tree. Only existing untracked files under that root's path-component boundary receive the generated-output exemption. A missing cache grants no exemption. Tracked paths from either compared commit retain precedence and content hashing, even beneath a configured binary root. Relative directory entries are resolved from the compilation database; relative source/include paths use each compilation's working directory.

No application, IPC, service, signer or trust policy changed. The cache is ordinary local build metadata, **not** a trusted or signed provenance authority. This gate proves Git-controlled source-input equivalence, not mutable generated-output equality, compiler/SDK identity or binary reproducibility. Arbitrary external build recipes and reparse-point attestation are not newly certified.

## Real-child regression matrix

New CTest `ffverify_executable_equivalence_ps_tests` invokes the actual guard in an owned disposable Git fixture with spaces in source/build paths. Initial red evidence reproduced external-build and relative-path positive failures while the conventional internal arm passed. The final matrix passes **21 assertions**, including:

- Internal/external absolute builds, relative file/include paths, relative compilation directory, identical location-independent digest.
- Missing generated header and generated TU, absent/foreign/moved/duplicate cache metadata, a binary root containing the source tree, prefix-sibling external TU, and normalized quoted-header traversal to a sibling all reject.
- Nested project-header, embedded RC manifest and generator-template content changes explicitly appear in `pathsWithChangedContent`.
- Added product inputs reject even when digests otherwise agree.
- Tracked TU/header files beneath a configured binary root remain content-sensitive; the tracked TU is not counted as generated.

Negative tests inspect their intended failure reason or changed path, rather than accepting any unrelated nonzero exit. Fixture cleanup is restricted to the unique directory created by the test. Original red and final output: `equivalence/fixtures-red.log`, `fixtures-expanded.log`, `fixtures-final.log` in the session folder below.

## Actual build-database proof

Compared immutable implementation `da4111e7905b1b5f7094f584f65e84a006303177` against documentation checkpoint `d6fe665f026cdb2bc3e19e5e0cd8a4f2ff7e548f`, using both **untouched actual** isolated Debug and release-test `compile_commands.json` files. No filtering, rewriting or substitute compilation database was used.

Both comparisons PASS with complete coverage: **425 executable-impacting paths, 355 translation-unit records, five generated version RCs**, no changed input paths, equal digest on both sides and configurations:

`6c0a85383f2cca66ca5c6748a4bcaa8abbab53441ae3032a3506e128d91802ed`

Machine records: `equivalence/actual-debug-bounded.json` and `actual-release-test.json`. The first full-tree attempt exceeded a 300-second caller budget and produced no verdict; a subsequent finite 1,800-second invocation completed. That timeout is neither a failed equivalence assertion nor an earlier pass.

## Local validation

- All-target isolated Debug build + full CTest: **147/147**, 362.85 seconds.
- All-target isolated release-test build + full CTest: **147/147**, 334.75 seconds.
- Final fixture after approved-verb rename: **21/21**; active PowerShell LSP: **two files clean**.
- Source-marker, diagnostic, strict OpenSpec and lifecycle validation: PASS. Full suites include doc-drift/status/provenance guards; final documentation drift is rechecked before publication.
- Source review preserved recursive include/RC/template/content/symmetric-addition checks. The new generated-root rule replaces, rather than broadens, the old blanket `repo/build` rule. No warning/security policy was relaxed.

Raw logs/scripts/build records are under `D:/Documents/tryPython/_AgentArtifacts/FastFiles/sessions/20261117-pi-production-readiness/` (the reused isolated builds are in the 20261114 audit session). The protected scheduled-task `build/debug` was not rebuilt. Goal mode is inactive; this is completion of the bounded resumed repair only.

## Publication and bounded completion

**COMPLETE:** implementation `c1dba201beba162870c0af677bc75feec364adfe` was committed and pushed without force. Both post-commit identity/package-trust/reaper/identity-fixture/model/engine/new-equivalence gates passed **7/7** (`equivalence/debug-postcommit-test.log`, `release-test-postcommit-test.log`). Local/remote equality and clean source state were verified at publication.

Exact implementation-SHA hosted [run 44000162097](https://github.com/quantdale/FastFiles/actions/runs/44000162097) completed **SUCCESS, all six jobs**: Debug, release-test, MSVC analysis, ASan protocol/IPC/lifecycle, repository hygiene/contracts, and unsigned release-candidate qualification. A bounded `gh run watch --interval 60 --exit-status` returned 0; final headSha/status/job conclusions were independently queried. Task 3.3 is satisfied.

The implementation SHA is an immutable validated baseline, not a self-reference to this evidence document. Subsequent documentation-only closure must preserve its executable inputs; derive latest Git and hosted status externally on resume. No whole-repository clean bill of health or production-release authorization is claimed.
