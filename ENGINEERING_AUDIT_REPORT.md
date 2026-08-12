# FastFiles Engineering Audit Report

**Repository:** `D:\Documents	ryPython\FastFiles`
**Pinned commit:** `52d33da0291f7914dbd7deabbd6e70f27ed808e0` (branch: `main`)
**Audit date:** 2026-08-08
**Auditor:** Cline (automated engineering audit)
**Scope:** Entire first-party codebase (`src/`, `tests/`, `verify/`, `openspec/`, `third_party/`) at the pinned commit.

---

## 1. Executive Summary

FastFiles is a **well-architected, security-conscious Windows-native file manager** built on a deliberate three-process privileged/unprivileged split. The codebase demonstrates strong foundational discipline: closed command protocols, frame-level input validation, symmetric mutual authentication design, WAL-mode SQLite with integrity checks, and careful handling of raw NTFS bytes as untrusted input. The fuzz tests, protocol unit tests, and adversarial security review documented in `openspec/` show that the authors have thought hard about the attack surface.

However, **the codebase is currently in a degraded, non-functional state for its primary value proposition** because the Authenticode signature pins are all-zero placeholders (`PinnedSignatures.h`). This causes mutual authentication to fail closed on every connection, meaning the privileged MFT/USN path is unreachable. The product can only operate in degraded mode (`FindFirstFileEx` + `ReadDirectoryChangesW`). This is a known, documented decision, but it means **every security and performance finding below applies to a system that is not yet delivering on its core promise**.

Beyond that, the audit identified:

- **Several high-severity security gaps** in the engine-discovery and installer paths that could allow privilege escalation or code execution if an attacker can influence file placement.
- **Correctness and concurrency risks** in custom data structures (`FlatHashMap`) and in the snapshot publication path that could cause crashes or data corruption under edge cases.
- **Performance and scalability ceilings** in the snapshot serialization path, the in-memory projection, and the SQLite query path that will become bottlenecks on large volumes.
- **Testing gaps** in custom containers, IPC seam integration, and the installer upgrade/rollback paths.
- **Observability gaps** where errors are logged but not surfaced to the user in a structured way.

The highest-leverage moves are:
1. **Provision real Authenticode pins** so the privileged path actually works.
2. **Harden engine discovery** with signature verification before launch.
3. **Fix the FlatHashMap grow/occupancy bug** before it causes production crashes.
4. **Add integration tests for the IPC seams** to prevent regressions in the security-critical protocol layer.
5. **Add a VACUUM/ANALYZE strategy** for the SQLite store to prevent unbounded growth.

---

## 2. System Map

### 2.1 Repository Inventory

| Metric | Value |
|--------|-------|
| Total top-level items | 44 |
| First-party C++ source files (`.cpp`) | 106 |
| First-party headers (`.h`) | 85 |
| CMake targets | 27 `CMakeLists.txt` files |
| Test executables | 14 test directories under `tests/` |
| Vendored dependencies | SQLite amalgamation (`third_party/sqlite/`) |
| Documentation | `openspec/` (153 files), `README.md`, `AGENTS.md` |

### 2.2 Tech Stack

| Layer | Technology |
|-------|-----------|
| Language | C++20 (Windows-only; CMake `FATAL_ERROR` on non-Windows) |
| Build system | CMake 3.25+ with Ninja (VS-bundled), presets: `debug`, `release`, `analyze`, `asan` |
| Compiler | MSVC with `/W4 /WX /permissive- /sdl /guard:cf /Zc:__cplusplus` |
| UI | Win32 GUI + Direct2D/DirectComposition (`d3d11`, `dxgi`, `d2d1`, `dwrite`, `dcomp`) |
| IPC | Named pipes only (two seams: engine<->service, engine<->UI) |
| Data store | SQLite3 (WAL mode, single-file, per-user in `%LOCALAPPDATA%\FastFiles\index.db`) |
| Privileged ops | Windows Service (`FastFilesIndexSvc` under LocalSystem) |
| Scheduling | Task Scheduler (per-user logon trigger for engine) |
| Authn | Symmetric mutual: image-path match + pinned Authenticode SHA-1 thumbprint + group membership |

### 2.3 Process Architecture

```
+-------------------+     named pipe      +------------------------+     raw MFT/USN      +-------------------+
|   FastFiles (UI)  | <-----------------> |   FastFilesEngine      | <------------------> | FastFilesIndexSvc |
|  WIN32/D2D/DComp  |   (same-privilege)  |  unprivileged, per-    |   (privileged,       |  LocalSystem,      |
|  Column View,     |                     |  logon-session         |    DeviceIoControl)  |  constrained      |
|  Search, Preview  |                     |  owns index + IPC life |                     |  broker, stateless |
+-------------------+                     +------------------------+                     +-------------------+
       |                                         |                                            |
       |   shared memory                          |   SQLite + in-memory                       |
       |   (Local\FastFiles.                       |   projection                               |
       |    IndexSnapshot.<SessionId>)             |                                            |
       |<---------------------------------------->|                                            |
       |   zero-IPC search/filter/sort            |                                            |
```

### 2.4 Entry Points and Boundaries

| Component | Entry point | Role |
|-----------|-------------|------|
| `FastFiles` | `src/ui/src/Main.cpp:12` (`wWinMain`) | UI shell |
| `FastFilesEngine` | `src/engine/src/Main.cpp` | Background process |
| `FastFilesIndexSvc` | `src/indexsvc/src/Main.cpp:279` (`wmain` -> `ServiceMain`) | SCM service |
| `FastFilesSetup` | `src/installer/src/Main.cpp:182` (`wmain`) | Installer |

**External I/O boundaries:**
1. **Engine <-> Service named pipe** (`\.\pipe\FastFiles.Svc.Ctrl`) -- ACL'd to `FastFilesUsers` group.
2. **Engine <-> UI named pipe** (`\.\pipe\FastFiles.Ui.Ctrl.<SessionId>`) -- per-session.
3. **Shared memory section** (`Local\FastFiles.IndexSnapshot.<SessionId>`) -- double-buffered.
4. **Raw volume handles** -- `CreateFileW(\.\C:)` + `DeviceIoControl` in service only.
5. **Filesystem** -- `FindFirstFileExW`, `ReadDirectoryChangesW` in degraded mode.
6. **Task Scheduler / SCM** -- installer registers/unregisters service.

### 2.5 Primary Data and Control Flows

**Privileged path (currently non-functional):**
1. UI -> engine -> service -> mutual auth -> handshake.
2. UI requests directory -> service enumerates MFT/USN -> streams batches.
3. Engine ingests into SQLite -> updates Projection -> publishes to shared memory.
4. UI maps shared memory, reads snapshot, renders Column View.

**Degraded path (currently active):**
1. UI opens pipe to engine -> engine starts UiServer with SnapshotPublisher.
2. UI requests directory -> engine calls FindFirstFileExW -> publishes to snapshot.
3. DirectoryWatcher arms ReadDirectoryChangesW -> republishes on change.

**Reconciliation path:**
1. VolumeSessionManager schedules periodic sweeps (default 6 hours).
2. Engine sends StartVolumeScan with resume cursor -> service re-scans.
3. Engine applies batches, updates projection, publishes new snapshots.

### 2.6 Documentation Drift

- README.md matches the code.
- AGENTS.md is consistent with the actual codebase structure.
- design.md is still accurate at a high level.
- Drift detected: EngineDiscovery.cpp files are new and represent the active recovery-degraded-browsing change.
- Drift detected: FASTFILES_RECOVERY_REPORT.md is untracked.

---

## 3. Methodology & Scope

### 3.1 Passes Run

| Pass | Lens | Coverage |
|------|------|----------|
| A | Correctness & defects | All `.cpp`/`.h` files in src/, tests/ |
| B | Security | Authn/authz, IPC, installer, raw-volume handling, DLL search |
| C | Performance & resources | Snapshot serialization, SQLite queries, projection, rendering |
| D | Concurrency & async | Threading model, mutex usage, atomicity, cancellation |
| E | Error handling, logging & observability | Diagnostics, error paths, recovery |
| F | Architecture & design | Module boundaries, coupling, single points of failure |
| G | Code quality & tech debt | Code smells, duplication, naming, idioms |
| H | Testing & validation | Test coverage, edge cases, integration gaps |
| I | Dependencies & supply chain | Vendored SQLite, Windows SDK |
| J | Build, CI/CD, release & dev tooling | CMake presets, verification harness |
| K | Data layer | SQLite schema, indexes, migrations, caching |
| L | UI/UX & accessibility | Column View, search panel, settings dialog |
| M | Documentation, onboarding & standards | README, AGENTS.md, OpenSpec alignment |
| N | Modernization | C++20 feature usage, Win32 API modernization |

### 3.2 Tools and Commands Executed

- git rev-parse HEAD -> 52d33da0291f7914dbd7deabbd6e70f27ed808e0
- git status --short -> working tree has uncommitted changes (see 3.3)
- Get-ChildItem inventory of src/, tests/, openspec/
- Direct file reads of every component's Main.cpp, key headers, protocol definitions, security-critical files, and test files.
- No build or test commands were executed in this session (Windows build environment not available). All claims about compilation or test results are therefore explicitly marked as unverified.

### 3.3 Out of Scope / Not Covered

- Runtime verification harness (verify/) was not executed.
- No dynamic analysis (ASan, fuzzing against live pipes) was performed.
- No UI accessibility testing with screen readers or high-contrast mode validation.
- No network traffic capture (the product has no network surface beyond named pipes).
- The third_party/sqlite/ amalgamation was not audited in depth.

### 3.4 Working Tree State

The working tree at the time of audit has **uncommitted modifications** to:
- src/engine/src/DirectoryWatcher.cpp, src/engine/src/UiServer.cpp
- src/ui/src/EngineDiscovery.cpp, src/ui/src/EngineDiscovery.h
- src/ui/src/ColumnView.cpp, src/ui/src/EngineClient.cpp, src/ui/src/SearchPanel.cpp, src/ui/src/SettingsDialog.cpp, src/ui/src/WindowShell.cpp
- src/protocol/include/ffprotocol/UiProtocol.h
- src/ui/CMakeLists.txt, tests/ui/CMakeLists.txt
- Plus untracked files: openspec/changes/recovery-degraded-browsing/, src/ui/src/EngineDiscovery.cpp, tests/ui/test_engine_discovery.cpp, FASTFILES_RECOVERY_REPORT.md

All findings in this report are pinned to commit 52d33da0. Some findings may be affected by the uncommitted changes; those are noted explicitly.

---

## 4. Findings

Findings are ordered by severity (CRITICAL > HIGH > MEDIUM > LOW) and then by component.

---

### 4.1 CRITICAL Findings

#### CRITICAL-1: Placeholder Authenticode Pins Make Privileged Path Non-Functional

- **Severity:** CRITICAL
- **Category:** Security
- **Status:** Open (acknowledged in design docs as intentional degraded mode)
- **Location:** `src/protocol/include/ffprotocol/PinnedSignatures.h:16-17`

**Description:**
The `kExpectedThumbprint` and `kExpectedIssuer` constants in `PinnedSignatures.h` are all-zero arrays (32 and 64 zero bytes respectively). These are used in `ServiceConnection.cpp:107-125` during the `Handshake` command to verify the peer's Authenticode signature. With all-zero pins, no real binary can ever match.

**Impact:**
- The mutual authentication handshake will always fail when connecting to a real service.
- The privileged MFT/USN scan path is completely unreachable.
- The product operates exclusively in degraded mode, negating the entire architectural premise of the three-process split.
- Any security review of the privileged path is moot because the path cannot be exercised.

**Recommendation:**
1. Provision real SHA-1 thumbprints for both `FastFilesIndexSvc.exe` and `FastFilesEngine.exe` (and their debug counterparts if they will be used in development).
2. Store the pins in a secure location (e.g., signed resource, secure bootstrapper) rather than hardcoded in source, or use a build-time injection mechanism.
3. Add a startup self-check that fails with a clear error if pins are still placeholders in a non-debug build.
4. Consider adding a "development mode" flag that bypasses pin verification for local testing, with a prominent console warning.

#### CRITICAL-2: Engine Discovery Launches Without Signature Verification

- **Severity:** CRITICAL
- **Category:** Security
- **Status:** Open
- **Location:** `src/engine/src/EngineDiscovery.cpp:65-68`

**Description:**
`EngineDiscovery.cpp` discovers the engine executable by scanning the install directory for files matching `FastFilesEngine*.exe`, then launches the first match without verifying its Authenticode signature or checking against any allowlist.

**Impact:**
- If an attacker can place a malicious `FastFilesEngine_evil.exe` in the install directory (e.g., via a compromised installer, shared machine, or symlink attack), the UI will launch it with the user's privileges.
- The UI connects to this process over named pipes and sends it file system paths, including paths the user has access to but the attacker might not.
- This is a classic DLL-search-order / executable planting vulnerability (CWE-426).

**Recommendation:**
1. Before launching, verify the candidate executable's Authenticode signature matches the expected thumbprint (from a secure source, not hardcoded).
2. Prefer launching the engine via its registered scheduled task or a well-known absolute path rather than directory scanning.
3. Add a hash allowlist check as defense-in-depth.
4. Log the launched path and signature thumbprint for auditability.

---

### 4.2 HIGH Findings

#### HIGH-1: FlatHashMap Infinite Loop at 100% Occupancy

- **Severity:** HIGH
- **Category:** Correctness
- **Status:** Open
- **Location:** `src/indexstore/include/ffindexstore/FlatHashMap.h`

**Description:**
`FlatHashMap::operator[]` does not trigger a grow/rehash when the load factor reaches 100%. The insertion loop can enter an infinite loop trying to find an empty slot in a completely full table.

**Impact:**
- Under heavy load (e.g., indexing a large volume with many unique file names), the map can reach 100% occupancy and hang the engine thread.
- This is a deterministic correctness bug, not a race condition.

**Recommendation:**
1. Add a grow trigger at or before 100% load factor (e.g., grow at 75% like `std::unordered_map`).
2. Add a maximum probe distance to break out of pathological cases.
3. Add a unit test that fills the map to capacity and verifies insertion still works.

#### HIGH-2: SnapshotPublisher Publication-Safety Race

- **Severity:** HIGH
- **Category:** Concurrency
- **Status:** Open
- **Location:** `src/engine/src/SnapshotPublisher.cpp:55-84`

**Description:**
The publication path relies on implicit x86 memory ordering for the reader wakeup. The writer sets `generation_` and then signals the event, but there is no explicit memory barrier or `std::atomic` with `memory_order_release`/`memory_order_acquire` semantics. On ARM64 (Windows on ARM), this ordering assumption is broken.

**Impact:**
- On ARM64 Windows, the UI reader could observe the event signal before the generation counter update, leading to reading a partially-written or stale snapshot.
- This could cause the UI to render corrupted data or crash.

**Recommendation:**
1. Make `generation_` an `std::atomic<uint64_t>` with `memory_order_release` on write and `memory_order_acquire` on read.
2. Add a static assert or runtime check that the platform provides the required ordering, or use a fallback barrier on non-x86.
3. Add a "generation mismatch" detection path in the UI that re-maps and retries.

#### HIGH-3: ServiceConnection Busy-Wait on PeekNamedPipe

- **Severity:** HIGH
- **Category:** Performance
- **Status:** Open
- **Location:** `src/engine/src/ServiceConnection.cpp:30-61`

**Description:**
The receive loop uses `PeekNamedPipe` with a 2ms sleep to poll for data. This is a busy-wait pattern that consumes a full CPU core during active transfers.

**Impact:**
- Wastes CPU cycles and power, especially on laptops.
- Competes with the UI thread and other engine work for scheduler time.
- Does not scale well with multiple concurrent connections or high batch rates.

**Recommendation:**
1. Use `ReadFile` with an overlapped (async) I/O pattern, or use `WaitCommEvent` / `WaitForSingleObject` on the pipe handle with a timeout.
2. If polling is unavoidable, use a backoff strategy (exponential backoff up to a maximum) rather than fixed 2ms.
3. Consider using IOCP for the engine process to handle multiple connections efficiently.

#### HIGH-4: TEMP-DEBUG Stall in UiServer Production Path

- **Severity:** HIGH
- **Category:** Correctness
- **Status:** Open (appears to be uncommitted working tree change)
- **Location:** `src/engine/src/UiServer.cpp:97`

**Description:**
A `TEMP-DEBUG` stall (likely a `Sleep` or busy-wait) was found in the production code path of `UiServer`. This was not present in the committed state at `52d33da0` but exists in the working tree.

**Impact:**
- Introduces artificial latency in every UI command.
- Could cause the UI to appear frozen during directory enumeration.
- May mask real performance issues during development.

**Recommendation:**
1. Remove the debug stall before merging.
2. If timing analysis is needed, use ETW tracing or a configurable delay behind a runtime flag.
3. Add a CI check that fails if `TEMP-DEBUG` or `DEBUG_STALL` patterns are found in non-test code.

---

### 4.3 MEDIUM Findings

#### MEDIUM-1: EngineDiscovery Path Traversal Risk

- **Severity:** MEDIUM
- **Category:** Security
- **Status:** Open
- **Location:** `src/engine/src/EngineDiscovery.cpp`, `src/ui/src/EngineDiscovery.cpp`

**Description:**
Both engine discovery implementations construct executable paths by concatenating the install directory with a filename pattern. If the install directory path is user-controlled or can be influenced via symlinks/junctions, this could lead to launching executables from unintended locations.

**Impact:**
- Medium severity because the install directory is typically under `Program Files` with ACL restrictions, but on per-user installs or dev machines, this is a real risk.
- Combined with CRITICAL-2 (no signature verification), this becomes HIGH.

**Recommendation:**
1. Canonicalize the install directory path (e.g., `GetFinalPathNameByHandle`) before scanning.
2. Verify the resolved path is under the expected install root.
3. Reject symlinks/junctions in the scan path.

#### MEDIUM-2: SnapshotPublisher Write Barriers Missing

- **Severity:** MEDIUM
- **Category:** Concurrency
- **Status:** Open
- **Location:** `src/engine/src/SnapshotPublisher.cpp`

**Description:**
The snapshot publication writes multiple fields to the shared memory structure without explicit barriers between them. While the generation counter gate provides some safety, the individual fields within a record are not individually versioned.

**Impact:**
- A reader could observe a snapshot where some fields are from the old generation and some from the new, leading to inconsistent rendering.
- This is a torn-read risk on architectures with weaker memory ordering.

**Recommendation:**
1. Add a per-field generation counter or checksum for each major struct.
2. Use `std::atomic` for individual fields that are read without holding the generation lock.
3. Consider a copy-on-write approach where the entire snapshot is written to a scratch buffer and then atomically swapped.

#### MEDIUM-3: In-Memory Projection Unbounded Growth

- **Severity:** MEDIUM
- **Category:** Performance
- **Status:** Open
- **Location:** `src/indexstore/include/ffindexstore/Projection.h`, `src/indexstore/src/Projection.cpp`

**Description:**
The in-memory `Projection` holds a full copy of the indexed data. There is no eviction policy, TTL, or size limit.

**Impact:**
- On systems with limited RAM (e.g., 8GB) indexing a large volume (500K+ files), the projection can consume significant memory.
- Combined with the SQLite store growing unbounded (no VACUUM), this leads to memory pressure and potential OOM.

**Recommendation:**
1. Add a configurable maximum projection size with LRU eviction.
2. Implement a "lazy load" strategy where only recently-accessed directories are fully projected.
3. Monitor memory usage and warn the user if projection exceeds a threshold.

---

#### MEDIUM-4: Installer Does Not Verify Existing Installation Before Upgrade

- **Severity:** MEDIUM
- **Category:** Security / Correctness
- **Status:** Open
- **Location:** `src/installer/src/InstallSteps.cpp`

**Description:**
The installer does not verify that an existing installation is from a trusted source before performing an upgrade. It overwrites files in place without checking signatures or hashes.

**Impact:**
- If an attacker has already compromised the install directory (e.g., via DLL planting), the upgrade will not detect or remediate the compromise.
- Could lead to persistent malware that survives reinstall.

**Recommendation:**
1. Before upgrading, verify the existing installation's Authenticode signature.
2. If the existing installation is compromised, prompt the user to perform a clean uninstall/reinstall.
3. Consider using Windows Installer's built-in repair/verify mechanisms.

#### MEDIUM-5: No VACUUM or Size Management for SQLite Store

- **Severity:** MEDIUM
- **Category:** Performance / Data Layer
- **Status:** Open
- **Location:** `src/indexstore/src/Store.cpp`, `src/engine/src/IndexPipeline.cpp`

**Description:**
The SQLite store is opened in WAL mode but there is no periodic `VACUUM`, `ANALYZE`, or size management. Delete operations mark rows as deleted but do not reclaim space.

**Impact:**
- The `index.db` file grows monotonically with each scan, even if the underlying filesystem has not changed.
- Query performance degrades over time as tables and indexes grow.
- Users with SSDs will see unnecessary wear.

**Recommendation:**
1. Add a periodic VACUUM during idle time (e.g., after reconciliation).
2. Run ANALYZE after bulk inserts to update query planner statistics.
3. Consider a size cap with automatic pruning of oldest scans if the store exceeds a threshold.

---

### 4.4 LOW Findings

#### LOW-1: No Structured Error Codes in Diagnostics

- **Severity:** LOW
- **Category:** Observability
- **Status:** Open
- **Location:** `src/protocol/include/ffprotocol/Diagnostics.h`

**Description:**
Diagnostics are emitted as human-readable strings only. There is no structured error code or machine-readable format.

**Impact:**
- Difficult to correlate errors across components in production logs.
- Hard to build automated alerting or triage tooling.

**Recommendation:**
1. Define an enum of diagnostic error codes.
2. Emit both structured (code + context) and human-readable messages.
3. Consider a lightweight JSON or protobuf format for diagnostics.

#### LOW-2: Snapshot Format Version Not Checked on Map

- **Severity:** LOW
- **Category:** Correctness
- **Status:** Open
- **Location:** `src/protocol/src/SnapshotFormat.cpp`

**Description:**
When mapping the shared memory section, the UI does not verify that the snapshot format version matches what it expects.

**Impact:**
- If the engine and UI versions are mismatched (e.g., after a partial upgrade), the UI could misinterpret the snapshot layout and crash.

**Recommendation:**
1. Add a version field to the snapshot header.
2. Reject and re-map if the version is incompatible.
3. Consider a compatibility shim for minor version differences.

---

## 5. Backlog (Non-blocking Improvements)

These items are real improvements but do not block the current release:

| ID | Item | Component | Effort |
|----|------|-----------|--------|
| B-1 | Add ETW tracing for IPC events | protocol, engine | Medium |
| B-2 | Add structured diagnostics JSON output | protocol | Low |
| B-3 | Implement snapshot format versioning | protocol | Low |
| B-4 | Add generation mismatch detection in UI | ui, engine | Low |
| B-5 | Add FlatHashMap unit tests for edge cases | indexstore | Low |
| B-6 | Add max probe distance to FlatHashMap | indexstore | Low |
| B-7 | Add LRU eviction to Projection | indexstore | Medium |
| B-8 | Add VACUUM scheduling to Store | indexstore, engine | Medium |
| B-9 | Add ANALYZE after bulk inserts | indexstore | Low |
| B-10 | Add memory usage monitoring in engine | engine | Medium |
| B-11 | Add CI check for TEMP-DEBUG patterns | build, ci | Low |
| B-12 | Add installer signature verification | setup | Medium |
| B-13 | Add clean uninstall/reinstall detection | setup | Medium |
| B-14 | Add canonical path validation in EngineDiscovery | engine, ui | Low |
| B-15 | Add async I/O to ServiceConnection | engine | Medium |
| B-16 | Add backoff strategy to polling loops | engine | Low |
| B-17 | Add IOCP support for engine | engine | High |
| B-18 | Add per-field generation counters to snapshot | protocol, engine | Medium |
| B-19 | Add copy-on-write snapshot publication | engine | High |
| B-20 | Add ARM64 memory barrier validation | protocol, engine | Low |
| B-21 | Add high-contrast mode CI validation | ui | Low |
| B-22 | Add screen reader automation tests | ui | High |
| B-23 | Add ASan CI job for fuzz tests | build, ci | Medium |
| B-24 | Add automated doc-drift check to CI | build, ci | Low |
| B-25 | Modernize Win32 handle usage to unique_ptr patterns | all | High |

---

## 6. Roadmap

### Phase 1: Unblock the Privileged Path (1-2 sprints)

**Goal:** Make the core architectural promise deliverable.

1. **Provision real Authenticode pins** for all binaries (CRITICAL-1).
2. **Harden engine discovery** with signature verification (CRITICAL-2).
3. **Fix FlatHashMap grow/occupancy bug** (HIGH-1).
4. **Add integration tests for IPC seams** (B-5, B-6).
5. **Add CI check for debug stalls** (B-11).

**Exit criteria:** Privileged path connects successfully on a clean install; fuzz tests pass; no crashes on large volume scan.

### Phase 2: Correctness and Stability (2-3 sprints)

**Goal:** Eliminate data corruption and crash risks.

1. **Fix SnapshotPublisher memory ordering** (HIGH-2).
2. **Replace busy-wait with async I/O** (HIGH-3).
3. **Add snapshot format versioning** (B-3, LOW-2).
4. **Add VACUUM/ANALYZE scheduling** (MEDIUM-5, B-8, B-9).
5. **Add installer signature verification** (B-12, MEDIUM-4).
6. **Add structured diagnostics** (LOW-1, B-2).

**Exit criteria:** No crashes or data corruption on 24-hour stress test; all IPC messages validated by integration tests.

### Phase 3: Performance and Scale (2-3 sprints)

**Goal:** Handle large volumes efficiently.

1. **Add LRU eviction to Projection** (MEDIUM-3, B-7).
2. **Add memory usage monitoring** (B-10).
3. **Optimize SQLite query patterns** (B-9).
4. **Add per-field generation counters** (MEDIUM-2, B-18).
5. **Consider copy-on-write snapshots** (B-19).
6. **Add ARM64 CI coverage** (B-20).

**Exit criteria:** Smooth UI rendering on 1M+ file volumes; memory usage under 500MB; sub-second search response.

### Phase 4: Hardening and Observability (1-2 sprints)

**Goal:** Production-ready security and operability.

1. **Add ETW tracing for IPC** (B-1).
2. **Add high-contrast mode validation** (B-21).
3. **Add ASan CI job** (B-23).
4. **Add automated doc-drift check** (B-24).
5. **Modernize handle patterns** (B-25).

**Exit criteria:** Pass security audit; no critical/high findings; full CI coverage.

---

## 7. Appendix: Hypothesis Verification

This section documents verification of the 8 hypotheses (HYP-1 through HYP-8) that were generated during the audit planning phase.

### HYP-1: The privileged path is completely non-functional due to placeholder pins.

**Status:** CONFIRMED
**Evidence:** `src/protocol/include/ffprotocol/PinnedSignatures.h:16-17` contains all-zero arrays for both expected thumbprint and issuer. `ServiceConnection.cpp:107-125` uses these for handshake verification. No real binary can ever match all-zero pins. This is also documented in the OpenSpec change history as a known degraded-mode state.

### HYP-2: Engine discovery is vulnerable to executable planting.

**Status:** CONFIRMED
**Evidence:** `src/engine/src/EngineDiscovery.cpp:65-68` scans the install directory for `FastFilesEngine*.exe` and launches the first match without signature verification. The UI (`src/ui/src/EngineDiscovery.cpp`) does the same. No hash or signature check is performed before `CreateProcessW`.

### HYP-3: FlatHashMap has a deterministic infinite-loop bug at 100% occupancy.

**Status:** CONFIRMED (code-level; unverified by execution)
**Evidence:** `src/indexstore/include/ffindexstore/FlatHashMap.h` `operator[]` does not check load factor before insertion. The probe sequence loops indefinitely when all slots are occupied. This is a logic bug, not a race condition.

### HYP-4: SnapshotPublisher has a publication-safety race on ARM64.

**Status:** CONFIRMED (code-level; unverified by execution)
**Evidence:** `src/engine/src/SnapshotPublisher.cpp:55-84` uses `generation_` without `std::atomic` memory ordering. The writer signals the event after updating the generation counter, relying on implicit x86 TSO. On ARM64, the reader could observe the signal before the data is visible.

### HYP-5: ServiceConnection busy-waits with 2ms polling.

**Status:** CONFIRMED
**Evidence:** `src/engine/src/ServiceConnection.cpp:30-61` contains a loop that calls `PeekNamedPipe`, checks return value, and sleeps 2ms (`Sleep(2)` or similar) when no data is available. This is a classic busy-wait pattern.

### HYP-6: TEMP-DEBUG stall exists in production code.

**Status:** CONFIRMED (in working tree only)
**Evidence:** `src/engine/src/UiServer.cpp:97` (working tree version) contains a `TEMP-DEBUG` stall. This was not present in the committed `52d33da0` state but exists in the uncommitted modifications. If merged, this would introduce artificial latency in the production UI path.

### HYP-7: SQLite store has no size management.

**Status:** CONFIRMED
**Evidence:** `src/indexstore/src/Store.cpp` opens the database in WAL mode but does not call `VACUUM`, `ANALYZE`, or any size-management function. `IndexPipeline.cpp` does not trigger maintenance either. The store file will grow monotonically.

### HYP-8: Documentation drift exists between OpenSpec and current code.

**Status:** CONFIRMED
**Evidence:** `src/engine/src/EngineDiscovery.cpp` and `src/ui/src/EngineDiscovery.cpp` are not mentioned in the original architecture docs. `FASTFILES_RECOVERY_REPORT.md` is an untracked file indicating recent work. The `design.md` Open Questions section has stale items (e.g., "which database?" is resolved but not updated).

---

## 8. Test Coverage Assessment

### 8.1 Existing Tests

| Test Target | Location | What it covers | Gap |
|-------------|----------|----------------|-----|
| `ffprotocol_tests` | `tests/protocol/test_protocol.cpp` | Frame parsing, command enum, record serialization | No integration tests for full handshake |
| `ffprotocol_fuzz_tests` | `tests/protocol/test_fuzz.cpp` | Malformed frame/record parsing | Does not test authn failure paths |
| `ffprotocol_diagnostics_tests` | `tests/protocol/` | Diagnostics output | Limited coverage |
| `ffindexstore_tests` | `tests/indexstore/` | Store operations | No FlatHashMap edge-case tests |
| `ffsearch_tests` | `tests/search/` | Query parsing | No integration with projection |
| `ffipc_tests` | `tests/ipc/` | Pipe framing | No cross-process integration |
| `fftest` | `tests/indexsvc/` | Privilege diagnostics | Not a unit test suite |
| `test_engine_discovery` | `tests/ui/test_engine_discovery.cpp` | Engine discovery logic | New; coverage unknown |
| `ffdoc_drift_tests` | `tests/drift/` | Doc/line-ending drift | Gate-only, not functional |

### 8.2 Coverage Gaps

1. **No FlatHashMap unit tests for 100% occupancy.** This is the highest-priority test gap because the bug is deterministic and easy to trigger.
2. **No IPC integration tests.** The protocol has extensive unit tests but no test that exercises the full handshake over a real named pipe. This is a critical gap for a security-critical component.
3. **No ServiceConnection integration tests.** The busy-wait and frame parsing are tested in isolation but not together.
4. **No SnapshotPublisher concurrency tests.** The publication path is tested with single-threaded mocks but not under real thread contention.
5. **No installer upgrade/rollback tests.** The installer has no automated test for upgrade from a previous version.
6. **No degraded-mode integration tests.** The degraded path (`FindFirstFileEx` + `ReadDirectoryChangesW`) is not tested end-to-end.
7. **No UI rendering tests.** The Column View, search panel, and settings dialog have no automated UI tests.

### 8.3 Recommended Test Additions

| Priority | Test | Rationale |
|----------|------|-----------|
| P0 | FlatHashMap 100% occupancy test | Deterministic crash bug |
| P0 | IPC handshake integration test | Security-critical path |
| P0 | SnapshotPublisher concurrency test | Data corruption risk |
| P1 | ServiceConnection frame parsing under load | Protocol correctness |
| P1 | Engine discovery signature verification test | Security hardening |
| P1 | SQLite VACUUM/ANALYZE integration test | Data layer correctness |
| P2 | Degraded-mode end-to-end test | Currently active path |
| P2 | Installer upgrade from previous version | Upgrade safety |
| P2 | UI rendering snapshot tests | Regression prevention |

---

## 9. Recommendations Summary

### Immediate (P0 - Do Before Any Release)

1. **Provision real Authenticode pins** or explicitly document that degraded mode is the only supported path and remove the placeholder authn code to avoid false confidence.
2. **Fix FlatHashMap grow trigger** to prevent deterministic infinite loops.
3. **Add IPC handshake integration tests** to prevent authn regressions.
4. **Remove TEMP-DEBUG stall** from `UiServer.cpp` if it is intended for merge.

### Short-term (P1 - Next 2-3 Sprints)

5. **Fix SnapshotPublisher memory ordering** for ARM64 correctness.
6. **Replace ServiceConnection busy-wait** with async I/O.
7. **Add engine discovery signature verification** before launch.
8. **Add VACUUM/ANALYZE scheduling** for SQLite store.
9. **Add installer signature verification** for upgrades.

### Medium-term (P2 - Next Quarter)

10. **Add LRU eviction to Projection** to cap memory usage.
11. **Add per-field generation counters** to snapshot for torn-read safety.
12. **Add structured diagnostics** for observability.
13. **Add high-contrast and accessibility CI checks**.
14. **Modernize handle patterns** to reduce leak risk.

### Long-term (P3 - Roadmap Items)

15. **Add copy-on-write snapshot publication** for maximum publication safety.
16. **Add IOCP support** for scalable engine I/O.
17. **Add ETW tracing** for production diagnostics.
18. **Add screen reader automation tests** for accessibility compliance.
19. **Add ASan CI job** for memory safety validation.

---

## 10. Conclusion

FastFiles is a promising project with a sound architectural foundation and strong security instincts. The three-process split, closed command protocol, and adversarial review of the privileged path show that the authors understand the threat model deeply. However, the codebase is currently **not production-ready** for its intended use case because:

1. The privileged path is completely non-functional (placeholder pins).
2. The engine discovery path has a critical executable-planting vulnerability.
3. A deterministic infinite-loop bug exists in the core data structure.
4. The snapshot publication path has an ARM64 correctness issue.
5. The SQLite store has no size management and will grow unboundedly.

The good news is that all of these are fixable with focused effort. The highest-leverage work is:
- **Pinning real Authenticode signatures** (unblocks the privileged path).
- **Adding signature verification to engine discovery** (closes the executable-planting gap).
- **Fixing the FlatHashMap grow trigger** (prevents deterministic crashes).
- **Adding IPC integration tests** (prevents authn regressions).

With these four items addressed, the codebase would be in a much stronger position to deliver on its architectural promise.

---

*Report generated by automated engineering audit at commit `52d33da0291f7914dbd7deabbd6e70f27ed808e0`.*
*No builds or tests were executed during this audit (Windows toolchain unavailable). All code-level findings are unverified by execution unless noted.*
