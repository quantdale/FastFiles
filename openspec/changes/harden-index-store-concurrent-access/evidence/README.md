# Evidence plan — harden-index-store-concurrent-access

Create `implementation-evidence.md` during execution. Do not pre-mark evidence as PASS.

## Required evidence

### 1. Baseline gap

Record:
- current `Store::Open` WAL/synchronous configuration;
- current canonical `filesystem-index-store` reader/writer requirement;
- exact pre-change `tests/indexstore` coverage showing whether any same-file dual-connection held-read/write case exists;
- focused baseline test result before modifications.

### 2. Two-connection fixture

Document the exact fixture:
- database path generated under test temp space;
- seed volume/entry counts;
- two independent `Store` instances opening the same existing database;
- which thread owns each Store while operations are active;
- barrier/condition-variable protocol used to prove the read statement is actually live before the writer starts;
- watchdog bounds and failure diagnostics.

Do not use fixed sleep timing as proof of overlap.

### 3. Snapshot truth table

Record executable observations for at least:

| Phase | Held reader should see | Fresh reader should see |
|---|---|---|
| Before writer commit | seed rows | seed rows |
| Writer committed while held read remains active | same coherent pre-commit snapshot | post-commit rows once a fresh statement is issued |
| After release | N/A | committed inserts/updates/removes |

Include counts/identities sufficient to prove no torn/duplicate state.

### 4. Writer and checkpoint boundedness

Record:
- writer `ApplyBatch` completion result and duration while reader is held;
- passive checkpoint result/duration while reader is held;
- whether frames remain pinned (if observable) without treating that as failure;
- subsequent checkpoint/read result after reader release.

If a Store implementation change is needed, identify the precise failure it fixes and why the change is bounded/fail-closed.

### 5. Repetition / teardown

Record consecutive deterministic iterations (target >=20 when runtime is reasonable):
- PASS count;
- zero retry-until-pass;
- min/median/max or representative writer/checkpoint durations;
- no stale lock preventing the next iteration;
- final close/reopen + `RunIntegrityCheck()` result and durable row verification.

### 6. Scope proof

Confirm:
- no schema/user_version change;
- no protocol or snapshot wire change;
- no production signing/trust-store changes;
- no `FastFilesIndexSvc`/raw MFT/USN change;
- no multi-writer product promise;
- no generated DB/WAL/SHM/build/run artifacts committed.

### 7. Full validation matrix

Record exact results for:
- focused index-store tests;
- Debug all CTest;
- release-test all CTest;
- analyze `/W4 /WX`;
- `verify.ps1 build`;
- `verify.ps1 doctor`;
- source-marker policy;
- diagnostic policy;
- doc drift;
- strict OpenSpec validation;
- `git diff --check`;
- final diff/scope review;
- final pushed SHA / `HEAD == origin/main` / tracked-worktree cleanliness;
- hosted Actions run/job status for the exact final SHA when queried.

Preserve failures/transient environmental observations truthfully; do not rewrite history to make the run look cleaner.
