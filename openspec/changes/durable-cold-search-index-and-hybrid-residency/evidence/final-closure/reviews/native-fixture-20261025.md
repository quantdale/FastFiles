# Independent fixture-only source review

- Reviewer: fresh-context native `/root/independent_acceptance_review`.
- Date: 2026-10-25.
- Source: acceptance successor working tree based on
  `8bffb329779ee0a431aaed37fa92925a4b373d9b`; canonical Git blobs recorded below.
- Activity: read-only; no builds, measurements, UI or process termination.

| File | Git blob |
|---|---|
| `verify/core/tests/ProcessIdentityFixture.psm1` | `b51ec252035c3f8903ead6da702796dc990b8953` |
| `verify/core/tests/Test-ProcessIdentityFixture.ps1` | `8317a2f439bdff88579954ad101fa55994f0b1d6` |
| `verify/core/tests/Test-ProcessReaper.ps1` | `24d89d33c20406e7893a5359e36853376d0eb7bc` |
| `tests/verification/CMakeLists.txt` | `4987c444c00305d0793a59aa31ee4934ae931cff` |

**Verdict: APPROVE; no material P1/P2 findings.**

The helper reads actual observed `Process.Path`, binds the exact launched PID
and start, retries absent/lazy path reads within the timeout and never substitutes
a launch alias. The original live fixture's identity precheck strengthens
discrimination. Production `CapabilityRunner.psm1` remains unchanged.

The new qualification child has a bounded natural lifetime and no kill/reaper
calls. Alias and wrong-start negatives exercise the unchanged production
verifier; the old identity is rejected after natural exit. CMake registers a
distinct 30-second test and retains the original reaper test and 60-second
timeout.

Nonblocking observations: the initial `Process.StartTime` read precedes the
retry loop and `identityKey` is assembled before path normalization. Neither
weakens identity verification or this fixture's empty-before ownership semantics;
no source change is required for this repair.

Reported 8/8, negative-control 7/8 with exit 1 and CTest 1/1 are implementer-run
evidence, not independently rerun by this reviewer. Approval covers fixture
source logic only and does not claim the original live reaper or full suite
passed. No cross-provider independence is claimed.
