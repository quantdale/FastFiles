## ADDED Requirements

### Requirement: Verb-Driven Orchestration Surface
The harness SHALL expose a fixed set of agent-invokable verbs — `build`, `install`, `run <suite>`, `diagnose`, `report`, `repair`, and `gate` — where each verb runs non-interactively, emits a machine-readable result, and returns a process exit code that distinguishes success, failure, and skipped-for-context.

#### Scenario: A verb runs without human interaction
- **WHEN** the agent invokes any harness verb
- **THEN** the verb SHALL complete without prompting for interactive input and SHALL write its outcome as structured JSON into the current run's artifact tree

#### Scenario: Exit codes are distinguishable
- **WHEN** a verb finishes in a passed, failed, or skipped-for-missing-context state
- **THEN** the harness SHALL return a distinct, documented exit code for each of those states so the caller can branch without parsing prose

### Requirement: Capability Registry And Runtime Discovery
The harness SHALL be organized as a registry of independently discoverable capabilities (including Build, Installer, Windows Service, Privilege, IPC, Filesystem, UI Automation, Diagnostics, Performance, Stress, Crash Analysis, Repair Loop, and Archive Gate); the core SHALL discover capabilities at runtime, evaluate each capability's availability probe against the environment, and execute only the capabilities whose prerequisites are met.

#### Scenario: Only available capabilities execute
- **WHEN** a verification run starts
- **THEN** the harness SHALL enumerate registered capabilities, run each capability's availability probe, and execute only those reported available, recording the availability verdict of every capability in the run

#### Scenario: An unavailable capability is skipped, not failed
- **WHEN** a registered capability's availability probe reports its context or prerequisites are absent
- **THEN** that capability SHALL be recorded as `SKIPPED` with a machine-readable reason and required-context descriptor, and SHALL NOT be executed, reported as `PASSED`, or reported as `FAILED`

### Requirement: Capability Interface And Versioning
Every capability SHALL implement a common versioned interface exposing at least an id, an interface version, a tier, an optional list of capability-id dependencies, an availability probe, a run entry point that emits a schema-conformant result, its declared diagnostics, and a declared repair posture (`repair-supported`, `repair-unsupported`, or `repair-unavailable`, with a repair entry point required only when the posture is `repair-supported`); the core SHALL load only capabilities whose interface version is within its supported range and SHALL make adding a new capability possible without modifying the orchestrator core.

#### Scenario: A new capability is added without touching the core
- **WHEN** a new capability module conforming to the interface is added to the capability registry
- **THEN** the core SHALL discover and run it based solely on its declared metadata, with no change to the orchestrator core

#### Scenario: An incompatible interface version is refused, not silently dropped
- **WHEN** a capability declares an interface version outside the core's supported range
- **THEN** the core SHALL refuse to load it and SHALL record a load diagnostic identifying the capability and the version mismatch, rather than silently ignoring it

#### Scenario: A capability declares its repair posture
- **WHEN** a capability is loaded
- **THEN** its declared repair posture SHALL be one of `repair-supported`, `repair-unsupported`, or `repair-unavailable`, and a `repair-supported` capability SHALL expose a repair entry point that the orchestrator invokes without containing any capability-specific repair logic itself

### Requirement: Capability Registry Hardening And Dependency Validation
Before executing any capability, the core SHALL validate the full set of discovered capability manifests: no two capabilities SHALL share an id, no capability id SHALL be loaded at two different versions simultaneously, every declared dependency SHALL resolve to another loaded capability, and the dependency graph SHALL contain no cycle; any violation SHALL be recorded as a load diagnostic identifying the offending capabilities and SHALL exclude only the invalid/cyclically-dependent capabilities from execution, never silently.

#### Scenario: Duplicate capability ids are rejected
- **WHEN** two discovered manifests declare the same capability id
- **THEN** the core SHALL refuse to load the duplicate, recording a load diagnostic identifying both manifest paths, and SHALL NOT execute either as if only one existed silently

#### Scenario: A missing dependency is a load diagnostic, not a silent drop
- **WHEN** a capability declares a dependency on a capability id that is not present among the loaded capabilities
- **THEN** the core SHALL record a load diagnostic naming the missing dependency and SHALL exclude the dependent capability from execution

#### Scenario: A cyclic dependency is detected and rejected
- **WHEN** two or more capabilities' declared dependencies form a cycle
- **THEN** the core SHALL detect the cycle, record a load diagnostic naming every capability in the cycle, and SHALL NOT execute any capability in the cycle

#### Scenario: Invalid capabilities never execute
- **WHEN** the registry hardening validation runs
- **THEN** every capability that fails schema validation, interface-version support, duplicate-id/version checks, or dependency/cycle validation SHALL be excluded from execution before any capability's run entry point is invoked in that pass

### Requirement: Execution-Context Fingerprint
Before running any suite, the harness SHALL capture an environment fingerprint including OS build, selected VS toolset and Windows SDK versions, elevation state, session id and kind, and the validation target identity, and SHALL persist it as `manifest.json` in the run's artifact tree.

#### Scenario: Fingerprint precedes execution
- **WHEN** a verification run begins
- **THEN** the harness SHALL write the environment fingerprint to the run manifest before executing any validation suite

#### Scenario: Fingerprint records elevation and session context
- **WHEN** the fingerprint is captured
- **THEN** it SHALL record whether the harness process holds an elevated (administrator) token and the session id/kind it is running in, so downstream tier gating can be evaluated against real context

### Requirement: Tier Gating With Explicit Skip
Each validation suite SHALL declare the execution-context tier it requires, and the harness SHALL compare that requirement against the fingerprint; when the required context is absent the suite SHALL be reported as `SKIPPED` with a machine-readable reason and required-context descriptor, and SHALL NEVER be reported as passed.

#### Scenario: Missing elevation skips a Tier-1 suite with a reason
- **WHEN** a Tier-1 suite is selected but the fingerprint shows the harness is not elevated
- **THEN** the harness SHALL mark that suite `SKIPPED`, record the reason (`not-elevated`) and the required context, and SHALL NOT execute or pass it

#### Scenario: Skipped is never silently a pass
- **WHEN** any suite cannot run because its required context is unavailable
- **THEN** the suite result SHALL carry the distinct status `SKIPPED` (not `PASSED`) so reports and the archive gate can treat the two differently

### Requirement: Per-Run Artifact Tree And Result Contract
The harness SHALL write every run to a dedicated directory `verify/runs/<change>/<timestamp>/` containing the run manifest, a metadata index (`index.json`), a namespaced `artifacts/<capability>/` subtree per executed capability, a repair log, and the generated reports; and each capability's subtree SHALL contain a `result.json` envelope conforming to a stable schema (capability id, interface version, tier, status, timing, pass/fail predicate outcome, and the list of produced artifact paths + types).

#### Scenario: Results are structured, not scraped
- **WHEN** a capability completes
- **THEN** its outcome SHALL be written as a schema-conformant `result.json` envelope such that the reporter and the archive gate consume the JSON without parsing console output

#### Scenario: Runs are isolated from each other
- **WHEN** two verification runs execute for the same change
- **THEN** each SHALL write into its own timestamped run directory without overwriting the other's artifacts

### Requirement: Capability Artifact Contract
Every capability SHALL write its outputs under its own namespaced subtree `artifacts/<capability>/` with a common `result.json` envelope at that subtree's root declaring its produced artifacts; the core SHALL archive the whole `artifacts/` tree and build the run's metadata index solely from the envelopes, and SHALL NOT parse capability-specific payloads.

#### Scenario: Capability outputs are namespaced
- **WHEN** a capability produces artifacts (e.g. ETL traces, dumps, a filesystem scan, a benchmark, a UI-automation result)
- **THEN** it SHALL write them under `artifacts/<capability>/…` (e.g. `artifacts/diagnostics/etl/…`, `artifacts/filesystem/scan.json`, `artifacts/performance/benchmark.json`, `artifacts/ui/automation.json`) and list them in its `result.json` envelope

#### Scenario: The core does not understand capability internals
- **WHEN** the core archives a run and builds `index.json`
- **THEN** it SHALL index metadata from each capability's envelope only, treating all other files in the capability's subtree as opaque, and SHALL NOT parse capability-specific payload formats

#### Scenario: A new capability's format needs no core change
- **WHEN** a capability introduces a new internal artifact format
- **THEN** the core SHALL archive and index it via the common envelope without any core code change, since the core reads only the envelope, not the payload

### Requirement: Suite Descriptors As The Extensibility Surface
Validation suites SHALL be defined by declarative descriptors mapping a suite id to its command, tier, required privileges, pass/fail predicate, and on-failure diagnostics set; adding a new validation SHALL require only adding or editing a descriptor, not modifying the orchestrator engine.

#### Scenario: A new validation is added by descriptor
- **WHEN** a new runtime check is introduced
- **THEN** it SHALL be expressible as a new suite descriptor entry that the existing orchestrator can discover and run without engine code changes

### Requirement: Diagnostics Collection On Failure
On any capability failure the harness SHALL collect the diagnostics declared for that capability — orchestrating Windows-native tooling where appropriate, which MAY include Event Viewer channels, ETW / Windows Performance Recorder traces (WPA-openable), Process Monitor captures, WER/mini crash dumps, ProcDump on-exception/hang dumps, Application Verifier / PageHeap findings, WinDbg-resolved stack traces, installer/MSI logs, and IPC traces — into the run's `diagnostics/` subtree, referenced from the capability result, and with enough context for rapid root-cause analysis.

#### Scenario: Failure evidence is captured automatically
- **WHEN** a capability fails
- **THEN** the harness SHALL gather that capability's declared diagnostics into the run's diagnostics subtree and link them from the failing `result.json`, without requiring a separate manual collection step

#### Scenario: Crash dumps are captured to a controlled location
- **WHEN** a monitored process crashes during a capability run
- **THEN** its WER/mini dump SHALL be captured to a run-local, access-controlled path rather than left in a user-readable default location

#### Scenario: A missing diagnostic tool degrades gracefully
- **WHEN** a declared native diagnostic tool (e.g. WinDbg, WPR, ProcMon, Application Verifier) is not available on the host
- **THEN** the harness SHALL record that collector as `SKIPPED` with a reason and continue collecting the remaining diagnostics, rather than failing the run because a tool is absent

### Requirement: Diagnostics As An Independently Runnable, Self-Describing Capability
Diagnostics SHALL be registered as its own capability in the registry (not only a cross-cutting on-failure mechanism): it SHALL expose an availability probe and a run entry point invokable directly (e.g. `run diagnostics`), and its run result SHALL enumerate each native diagnostic tool it knows about (at least ETW, Windows Performance Recorder, Windows Performance Analyzer, Process Monitor, ProcDump, WinDbg/`cdb`, Application Verifier, and PageHeap) with structured metadata — available/absent, and version/path when available — discovered without any hardcoded path. An absent tool SHALL be recorded as `SKIPPED(reason)` for that tool, never as a capability failure; the archive gate MAY still resolve the Diagnostics capability itself to `REQUIRED-BUT-UNAVAILABLE` if a change's gate policy marks it required and it was skipped overall, per D5/D8 — individual tool absence never bypasses that gate-level distinction by reporting itself as required-but-unavailable directly.

#### Scenario: Diagnostics runs standalone and enumerates tooling
- **WHEN** the Diagnostics capability is run directly
- **THEN** it SHALL probe for each of its known native tools without assuming any is present, and SHALL report each tool's availability, version, and path (when found) in a structured, capability-owned artifact

#### Scenario: An absent tool is a per-tool skip, not a capability failure
- **WHEN** a declared native diagnostic tool is not found on the host
- **THEN** Diagnostics SHALL record that tool as `SKIPPED` with a reason, SHALL continue probing the remaining tools, and SHALL NOT fail the capability merely because one tool is absent

#### Scenario: No hardcoded tool paths
- **WHEN** Diagnostics locates a native tool
- **THEN** it SHALL discover the tool's location via environment/registry/well-known-install-root probing rather than a single hardcoded path, so the check remains valid across hosts with different install layouts

### Requirement: Crash Analysis
The harness SHALL provide a Crash Analysis capability that, when a monitored process crashes, captures a dump, identifies the crashing executable and its faulting thread, resolves symbols, generates a stack trace, classifies the crash into a bucket, preserves reproduction artifacts, attaches all of this to the validation report, and exposes a structured verdict consumable by the repair loop.

#### Scenario: A crash produces a classified, symbolized artifact
- **WHEN** a monitored FastFiles process crashes during a run
- **THEN** Crash Analysis SHALL capture the dump, identify the crashing executable and its faulting thread, resolve symbols, produce a stack trace for the faulting thread, assign a classification bucket (faulting module + exception code + normalized top frames), and preserve the reproduction context (inputs + environment fingerprint) into the run tree

#### Scenario: Crash analysis feeds the repair loop
- **WHEN** the repair loop reaches its root-cause step for a crash failure
- **THEN** it SHALL consume the Crash Analysis structured verdict as input rather than re-deriving the root cause

#### Scenario: Missing symbols degrade to dump-only
- **WHEN** symbols or a post-mortem debugger are unavailable
- **THEN** Crash Analysis SHALL still capture and preserve the dump and reproduction artifacts, recording that symbolization was `SKIPPED` with a reason

### Requirement: Environment Providers And Lifecycle
The harness SHALL execute against a selectable Environment Provider behind a common interface — at least `local`, and disposable/isolated providers such as Hyper-V, VMware, VirtualBox, Windows Sandbox, GitHub Actions Windows runners, and self-hosted runners — managing each provider's lifecycle: provision, activate, collect logs, cleanup, and snapshot restore where supported.

#### Scenario: A disposable environment is provisioned and cleaned up
- **WHEN** a run targets a disposable Environment Provider
- **THEN** the harness SHALL provision the environment, run the selected capabilities inside it, collect logs/artifacts back to the run tree, and clean up (or restore a snapshot) so the environment does not accumulate state between runs

#### Scenario: The local provider leaves the host clean
- **WHEN** a run targets the `local` provider and mutated system state
- **THEN** the harness SHALL run idempotent teardown so the developer machine is left without leftover installation, service, task, or created account, even if the run failed

#### Scenario: Same capabilities run across providers unchanged
- **WHEN** the same capability is run under two different Environment Providers
- **THEN** it SHALL execute against the provider interface without capability-specific changes, and the run manifest SHALL record which provider was used

### Requirement: Multi-Format Reporting
The harness SHALL generate reports for a run in Markdown, HTML, JSON, and JUnit XML formats, plus a performance summary and a failure summary, where every report is a deterministic projection of the run's artifact tree.

#### Scenario: All required report formats are produced
- **WHEN** the `report` verb runs for a completed run
- **THEN** the harness SHALL emit Markdown, HTML, JSON, and JUnit XML reports plus performance and failure summaries derived solely from that run's artifacts

#### Scenario: Reports agree with the artifacts
- **WHEN** a report is regenerated from an unchanged run tree
- **THEN** it SHALL reflect the same pass/fail/skip outcomes as the underlying `result.json` files, containing no verdicts not present in the artifacts

#### Scenario: Reports surface duration, version, fingerprint, tool versions, artifacts, and repair attempts without changing the result envelope schema
- **WHEN** a report is generated
- **THEN** it SHALL surface each capability's execution duration and interface version (already present in the envelope), the run's environment fingerprint (from the manifest), any tool-version metadata a capability recorded in its own payload (e.g. Diagnostics' tooling inventory), the capability's produced artifacts, any repair attempts recorded in `repair-log.jsonl` for that run, and the reason for any unavailable/skipped capability — all without adding new fields to the shared `result.json` envelope schema (D16); capability-specific data (e.g. tool versions) SHALL be read from that capability's own artifact payload, never encoded into the shared envelope

### Requirement: Bounded Autonomous Repair Loop
Repair logic SHALL be owned by the capability that failed, not by the orchestrator: every capability SHALL declare a repair posture (`repair-supported`, `repair-unsupported`, or `repair-unavailable`), and the orchestrator SHALL only coordinate — invoking a `repair-supported` capability's own repair entry point, never containing capability-specific repair logic itself. On top of that, the harness SHALL support a repair loop (build → install → run → verify → diagnose → identify root cause → apply fix → rebuild → reinstall → re-run) that classifies each fix, applies harness/config fixes automatically, gates product-source fixes, stops on a repeated failure signature, and halts at a hard iteration cap; every iteration SHALL be appended to a repair log.

#### Scenario: The orchestrator coordinates but never repairs directly
- **WHEN** the repair loop identifies a failing capability whose posture is `repair-supported`
- **THEN** the orchestrator SHALL invoke that capability's own repair entry point and apply the Class A/B gating to its result, and SHALL NOT contain repair logic specific to that capability

#### Scenario: Repair-unsupported or repair-unavailable capabilities are not repaired
- **WHEN** a failing capability's posture is `repair-unsupported` or `repair-unavailable`
- **THEN** the repair loop SHALL record that no repair was attempted and SHALL surface the failure for manual handling rather than guessing at a fix

#### Scenario: Harness/config fixes are auto-applied; product-code fixes are gated
- **WHEN** the loop identifies a fix
- **THEN** a harness/config/environment (Class A) fix SHALL be applied automatically and logged, while a product-source (Class B) fix SHALL be applied only on a work branch with a recorded root cause and diff and SHALL be flagged for review rather than silently accepted

#### Scenario: No-progress detection stops the loop
- **WHEN** the same normalized failure signature (error + failing suite + phase) recurs on a subsequent iteration
- **THEN** the loop SHALL stop and escalate rather than continue retrying

#### Scenario: The loop is bounded and fully logged
- **WHEN** the repair loop runs
- **THEN** it SHALL halt at or before a hard iteration cap and SHALL append every iteration (root cause, fix class, action, outcome) to the run's repair log

### Requirement: Four-State OpenSpec Archive Gate
The harness SHALL provide a gate, consulted before a change is archived, that resolves each capability the change's gate policy marks required to one of `PASS`, `FAIL`, `SKIPPED`, or `REQUIRED-BUT-UNAVAILABLE`, and passes only when every required capability is `PASS` and the run contains no product-source edit unrepresented in the change's tasks or specs.

#### Scenario: A red run blocks archival
- **WHEN** a change's latest verification run has any required capability in state `FAIL`
- **THEN** the gate SHALL not pass and the change SHALL not be archivable until the failure is resolved or re-run green

#### Scenario: Required-but-unavailable blocks archival
- **WHEN** a capability the gate policy marks required for the change was `SKIPPED` because its context or prerequisites were unavailable
- **THEN** the gate SHALL resolve that capability to `REQUIRED-BUT-UNAVAILABLE` and SHALL not pass, distinguishing it from an acceptable non-required skip

#### Scenario: A non-required skip does not block archival
- **WHEN** a capability that is `SKIPPED` is marked non-required by the change's gate policy
- **THEN** the gate SHALL treat that skip as acceptable and SHALL NOT block archival on its account

#### Scenario: An unrepresented product edit blocks archival
- **WHEN** the run's repair loop applied a product-source change that does not map to any task or spec in the change
- **THEN** the gate SHALL not pass until that change is represented in the change's tasks/specs or reverted

### Requirement: Developer-Experience Inspection Verbs — `list` And `doctor`
In addition to the validation-lifecycle verbs, the harness SHALL expose two read-only inspection verbs: `list`, which enumerates every discovered capability (including ones rejected by registry hardening) with its id, interface version, tier, load status, and declared dependencies; and `doctor`, which probes and reports environment readiness — PowerShell version, Visual Studio/VC toolset, Windows SDK, CMake, Ninja, WPR, WPA, Process Monitor, ProcDump, WinDbg, Application Verifier, Hyper-V, and Windows Sandbox — as presence/version detection only, with no hardcoded paths and no environment provisioning. Neither verb SHALL create a run in the per-run artifact tree.

#### Scenario: list enumerates the registry, including rejected capabilities
- **WHEN** `list` runs
- **THEN** it SHALL report every discovered capability's id, interface version, tier, load status (loaded, or rejected with reason), and declared dependencies, in both human-readable and machine-readable form

#### Scenario: doctor reports environment readiness without provisioning anything
- **WHEN** `doctor` runs
- **THEN** it SHALL probe for each of its known prerequisites and native tools and report each as present (with version where available) or absent, without installing, provisioning, or modifying any of them

#### Scenario: Inspection verbs do not create run artifacts
- **WHEN** `list` or `doctor` completes
- **THEN** no new directory SHALL be created under `verify/runs/`, since neither verb executes a capability's run entry point
