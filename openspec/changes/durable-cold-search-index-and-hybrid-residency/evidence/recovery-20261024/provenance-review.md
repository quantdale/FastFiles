## Review

**Reviewed HEAD:** `8c4c8481f312aa4651a5c2bab3cb61e0b0e50af0`  
**Implementation baseline:** `5d7e98ae412cd893e47e7ebc9a9bdea4c609d0ac`  
**Lane:** E — acceptance claims, provenance, executable inputs.

Read-only review. No tests, builds, repository mutations, or prohibited evidence-directory access occurred.

### Scope and inspection

Inspected actual source and the supplied saved diffs:
- `build/campaign/recovery-20261024/evidence-range.txt`: baseline-to-HEAD full diff and commit metadata.
- `build/campaign/recovery-20261024/pr15-executable.diff`: relevant benchmark/test/CMake changes against PR14.

Also inspected:
- Root `AGENTS.md`, `docs/agent/WORKSTATE.md`.
- Change `proposal.md`, `design.md`, `tasks.md`, `specs/index-engine/spec.md`; lifecycle entry.
- `final-acceptance-owner-report.md`, `exact-order-oracle.md`, historical certification record, downgrade policy, ingestion-cliff evidence.
- `Test-ExecutableTreeEquivalence.ps1`: blob mapping/digest, include/resource closure, build-input derivation, symmetric path checks.
- `cmake/WriteBuildIdentity.cmake`.
- Oracle `CopySnapshot`, `Search`, `Same`, `SequenceEqual`, `ComparatorSelfTest`, and premise checks.
- Benchmark allocator definitions and report generation; store-open identity/reporting.
- Engine/indexstore CMake inputs and production SQLite compile definitions.
- Preserved oracle, migration/recovery, ingestion, repeatability, parity, CTest, ASan, UIA and visual-result artifacts.
- Older-store probe source, CMake inputs and build logs.

No production-algorithm correctness verdict is supplied by this lane.

### Correct

- **Baseline evidence can carry forward as source-equivalent evidence.** The supplied `5d7e98a..8c4c848` full diff contains documentation/evidence changes, not executable-source, benchmark-source or build-input changes. This supports retaining the baseline’s *existing coverage*, not expanding it.

- **Oracle v2 genuinely separates the candidate route from the canonical scan.** `tests/engine/bench_identity_ordering_oracle.cpp:99–123` selects the canonical hook; `src/engine/src/IndexPipeline.cpp:1968–1971` bypasses the name-prefilter route. The oracle unloads projection residency (`:194–202`) and checks status, full scan counts, route admission and nonvacuity (`:239–255`). Its comparator covers every current candidate field (`:126–137`; protocol fields at `src/protocol/include/ffprotocol/UiProtocol.h:294–312`).

- **The preserved 5M result supports capped cold-versus-canonical equality.** `evidence/artifacts/final-acceptance/oracle-5m-final.json:3–22` records baseline identity, 5M canonical scans, selective-route admissions, equal sequences and cancellation. It does not establish fully-hot or uncapped equality.

- **Most final qualifications are appropriately explicit.** The owner report discloses missing final 2M repetitions, WAL operational cells, final independent-review failure, Gallery failures and UIA skips (`final-acceptance-owner-report.md:175–186, 207–243, 262–296`). Lifecycle remains `active` (`openspec/lifecycle.json:1722–1743`).

- **Reported ladder totals have matching preserved results.** CTest logs end in 129/129; ASan records four passing tests; parity records baseline SHA, clean state and 11 passing steps. UIA and visual artifacts retain the baseline identity and unsuccessful/skipped outcomes. These are inspected historical execution records—not tests rerun by this reviewer.

### Finding: P1 — Task 4.1 closes a narrower acceptance test than the governing requirement

**Location:** `openspec/changes/durable-cold-search-index-and-hybrid-residency/tasks.md:46`.

The diff changes 4.1 from open hot/cold equivalence acceptance to checked capped cold-versus-canonical acceptance. However:
- `specs/index-engine/spec.md:38–50` still requires exact **hot versus cold-with-index** identity/order at **200k, 1M, 2M and 5M**.
- `design.md:51–53` retains that mandatory equivalence dimension.
- The oracle deliberately unloads every volume and requires zero hot rows (`tests/engine/bench_identity_ordering_oracle.cpp:194–202, 243–247`).
- The owner report expressly does not claim fully-hot 5M residency (`final-acceptance-owner-report.md:73–79`).

**Minimal reproduction:** Compare task 4.1 with the unchanged specification, then inspect the oracle’s zero-hot-row premise. Its successful result cannot satisfy the written hot/cold scenario.

**Smallest correction:** Preserve the successful capped canonical-oracle subtask, but reopen the remaining equivalence acceptance. Alternatively, obtain an explicit scope decision and amend the governing specification/design consistently. No expensive rerun is necessary merely to correct the ledger.

This is an acceptance-closure defect, **not proof of incorrect search results**.

### Finding: P2 — Reopened resource acceptance conflicts with checked task 5.9

**Location:** `tasks.md:37, 47`; `evidence/5m-staged-acceptance.md:1–5`.

Task 5.9 still says resource-matrix completion is DONE and points to the historical staged record. This diff explicitly reopens 4.2 and documents unexecuted large-reader, SQLITE_FULL and large-removal crash cells.

**Minimal reproduction:** Read the two checkboxes together: the parent completion statement remains checked while the referenced acceptance is reopened.

**Smallest correction:** Relabel 5.9 as historical staged execution/recording, explicitly excluding current 4.2 completion, or reopen its resource-completion portion. Preserve NOT_EXECUTED dispositions.

### Finding: P2 — The 132.37-second result needs an allocator-instrumentation qualification

**Location:** `evidence/final-acceptance-owner-report.md:100–108, 190–201`; `tasks.md:52`.

The final report calls 132.37 seconds the “ordinary benchmark” result without surfacing that this executable is allocator-instrumented:
- `tests/engine/bench_large_index_scalability.cpp:35–38` unconditionally defines the global allocator replacements.
- Its own limitation explicitly rejects using this binary as uninstrumented latency evidence (`:2103`).
- `bench_allocator_probe.h:18–25` documents instrumentation overhead and its measurement boundary.
- `ingestion-5m-final.json:8, 14, 19–21` records `--memory-report`, `allocator_probe_enabled=true`, and that limitation.

Task 5.13 still says an **uninstrumented** clean remeasurement is pending. The recorded 132.37-second measurement cannot discharge it.

**Minimal reproduction:** Inspect the preserved ingestion artifact’s instrumentation fields and the unconditional allocator definition; no execution required.

**Smallest correction:** Describe this as “stage-timer-free, allocator-instrumented benchmark ingestion”; explicitly retain the uninstrumented production-latency gap in the final summary. The measured value and stage-isolation evidence need not be withdrawn.

### Provenance and coverage gaps

These are limitations, not proven wrong-binary allegations:

1. **Exact-tip machine equivalence verdict not found.**  
   `verify/status/generated/executable-tree-equivalence.json:5–6` concerns `80a6f119..b68767a`, not the reviewed pair. Do not reuse its PASS. The inspected full diff independently supports source equivalence, but I did not execute the gate.

2. **Source identity is not universal executable attestation.**  
   Build identity refresh dependencies exist for the principal benchmarks/oracle. However, the oracle records only `kBuildIdentityAscii` (`bench_identity_ordering_oracle.cpp:229–231`), whereas store-open artifacts additionally record a runtime executable hash. Caller-supplied clean/source labels remain assertions. No evidence of an actual mismatched executable was established.

3. **Older-store inputs lack immutable binding in preserved output.**  
   The probe CMake file points to mutable absolute `OLD_ROOT` source/header paths and an existing SQLite library (`build/campaign/final-acceptance/v2-probe/CMakeLists.txt:3–8`). The preserved refusal/preservation logs do not record those input hashes or probe executable identity. Preserve that binding before treating this as independently reproducible proof of the exact PR14 build. The observed refusal/rollback output itself is present.

4. **Carry-forward does not fill missing cells.**  
   Targeted/uncapped 5M cases, final-source 2M repetitions, operational WAL cases, physical display qualification and uninstrumented latency remain unproven. Source equivalence cannot manufacture those results.

5. **Hosted execution was not independently inspected here.**  
   Local parity is not hosted certification. No previous review report was used as certification.

### Minimum authoritative current ladder

For this evidence-only tip, the bounded next verification is:
- Exact-pair executable-tree equivalence:
  `Test-ExecutableTreeEquivalence.ps1 -ImplementationSha 5d7e98ae412cd893e47e7ebc9a9bdea4c609d0ac -EvidenceSha 8c4c8481f312aa4651a5c2bab3cb61e0b0e50af0`.
- Current-tip strict OpenSpec, lifecycle/doc-drift checks and range whitespace validation. Baseline policy results cannot validate newly edited documents.
- Retain compiled-test/oracle outcomes as **baseline execution, source-equivalent carry-forward**, not fresh HEAD executions.
- Record actual hosted head/result separately; retain Gallery failures and UIA skips.
- Complete or explicitly disposition remaining acceptance before unqualified readiness claims.

**Fixed:** None; read-only review.

**Merge verdict: OK with notes for preserving this qualified evidence-only handoff. BLOCK unqualified acceptance closure until the P1 contract/ledger mismatch is corrected.** No production-algorithm approval is implied.