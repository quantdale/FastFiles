## Purpose

Define when a consolidated FastFiles candidate can truthfully be accepted as a coherent native file manager, with traceable integration inputs, real user journeys, independent review and separately qualified release boundaries.

## ADDED Requirements

### Requirement: Fresh and complete integration inventory

The campaign SHALL establish a machine-readable, versioned inventory before product integration. It MUST identify current main, every live open PR and relevant unpublished local input; pin head/base/fork identities; distinguish ancestry from content equivalence; identify unique commits, changed files/tests and OpenSpec obligations; and record validation, reviews, findings and disposition. Unknown required identities or inconsistent records SHALL block acceptance of that input.

#### Scenario: Stacked and unpublished inputs
- **WHEN** an open PR contains predecessor heads and an unpublished branch contains additional changes
- **THEN** the inventory attributes each unique change and test once, records its inclusion or reasoned rejection, and does not assume the unpublished branch is either obsolete or already accepted

#### Scenario: Inputs change after intake
- **WHEN** main or an incoming head changes before publication or merge
- **THEN** the campaign records the delta and explicit disposition, reevaluates affected gates, and does not present the old input snapshot as current

### Requirement: One preserved-history candidate

All accepted work SHALL converge into one named central candidate based on freshly reconciled main. The campaign MUST preserve unrelated dirty/divergent work, avoid force pushes/history rewrites, and give every additional writer branch an integration destination. It SHALL NOT create a successor PR merely to isolate the next repair.

#### Scenario: Dirty original checkout
- **WHEN** unrelated files are modified in an existing worktree
- **THEN** consolidation uses its isolated worktree without stashing, overwriting, committing or discarding those files

### Requirement: Progressive semantic integration

Each material stage SHALL preserve all compatible accepted behavior and unique tests, record material resolution decisions, compile the affected candidate in Debug and release-test, and pass affected tests before another stage is accepted. Material resolutions MUST receive independent review. Source/test registrations, schema compatibility, ownership and security invariants SHALL NOT be weakened to resolve a conflict.

#### Scenario: Two branches protect different behaviors in one file
- **WHEN** integration encounters competing source or test edits
- **THEN** the resolution preserves both compatible contracts with discriminating regressions, records any intentional rejection, and is not accepted solely because the file merged or compiled

#### Scenario: Stage gate fails
- **WHEN** a stage loses a test, fails a required available test, or regresses behavior
- **THEN** the stage remains unaccepted until repaired and revalidated rather than being buried under more unvalidated integrations

### Requirement: Bounded acceptance and explicit outcomes

The campaign SHALL freeze a finite matrix of journeys A–H, predecessor invariants, observable postconditions, fixture ownership and required environments before product edits. New repairs MUST address demonstrated safety defects, integration regressions or blocked existing acceptance. Each cell SHALL record PASS, FAIL, NOT_RUN, SKIPPED, BLOCKED_ENVIRONMENT or BLOCKED_EXTERNAL with evidence and scope; only executed matching postconditions support PASS.

#### Scenario: Feature idea is unrelated to required journeys
- **WHEN** a new feature opportunity does not repair a required acceptance failure
- **THEN** it is deferred without starting another implementation campaign

#### Scenario: Mandatory environment is unavailable
- **WHEN** a required cell cannot execute safely in the available environment
- **THEN** it remains explicitly unqualified with the precise prerequisite, while independent permitted cells continue

### Requirement: Useful first launch without privilege

Journey A SHALL demonstrate actual first launch into Home, discoverable available locations, truthful degraded coverage and browsing of a permitted owned directory without a privileged service or blocking enablement dialog.

#### Scenario: Service absent
- **WHEN** an isolated ordinary-user launch has no privileged connection
- **THEN** Home and browsing function, the coverage limitation is accurate, and the journey does not claim whole-computer indexing

### Requirement: Search reveals the correct navigation context

Journey B SHALL exercise actual filename entry, results, filtering/sorting, selection, containing-folder reveal and return to the previous workspace. Acceptance MUST verify exact selected identity/path, hierarchy, active pane, focus and visible destination rather than search-panel dismissal alone. Snapshot refresh while the panel remains open SHALL receive explicit coverage distinct from closing and reopening it.

#### Scenario: Search starts from Home
- **WHEN** the user selects a deep result from a permitted indexed/browsed fixture
- **THEN** its actual containing folder and selected file appear in the correct visible pane with synchronized navigation context and a usable route back

#### Scenario: Result generation changes
- **WHEN** results are replaced or refreshed with a different file occupying the old row position
- **THEN** selection and actions do not silently retarget that different identity, and stale accessible elements are rejected

### Requirement: Hierarchical browsing and workspace ownership

Journeys C and D SHALL demonstrate nested Column View navigation by keyboard, parent/child traversal, horizontal reveal, selection, Back/Forward, tabs, dual panes, source/destination ownership and persisted restoration. Missing or inaccessible locations MUST produce truthful recoverable states rather than actions on stale hidden selections.

#### Scenario: Dual-pane transfer
- **WHEN** a user selects source items and a destination in different panes or tabs and performs copy/move
- **THEN** the actual filesystem result matches those visible owners, unrelated fixture files remain unchanged, and switching/restoring the workspace preserves its intended context

#### Scenario: Selected location disappears
- **WHEN** the current permitted fixture path becomes missing or inaccessible
- **THEN** the app exposes the real state and allows recovery without operating on a different hidden target

### Requirement: Actionable and truthfully scoped storage intelligence

Journey E SHALL demonstrate Storage ranking, folder selection, Treemap entry/drill-down, exact file reveal and return to browsing. Rankings, logical/allocated sizes and partial aggregates MUST describe their actual known coverage; category summaries SHALL NOT become unrelated filesystem targets. Narrow and wide mode controls MUST preserve real selection/focus and the shared action target.

#### Scenario: Partial nested storage fixture
- **WHEN** available coverage contains nested large files, duplicate leaf names and incomplete aggregates
- **THEN** ranking and drill-down preserve exact descendant identities and known lower bounds without claiming complete-volume totals, and reveal navigates to the selected file

### Requirement: Exclusive creation and non-replacing rename in the app

Journey F SHALL verify new file → inline rename → rename again → Undo, new folder → inline rename → enter, and rename collision → error → recovery through the integrated application. Unexpected occupied destinations MUST retain their bytes/children and SHALL NOT be reported as newly created; failed creation MUST NOT initiate inline rename of an existing item. Case-only rename, actual leaf casing, cancellation and reversible history SHALL preserve the existing accepted contracts.

#### Scenario: Destination becomes occupied
- **WHEN** rename or exact-name creation encounters an unexpected existing destination
- **THEN** the operation refuses unintended replacement, reports the native failure truthfully, preserves existing data, and allows a correct subsequent user action

#### Scenario: Successful create and rename history
- **WHEN** the user creates an absent item and completes successive ordinary or case-only renames
- **THEN** the UI targets only the newly created item, actual leaf spelling matches the accepted names, and Undo reverses the recorded rename without overwriting a new occupant

### Requirement: Request-scoped conflict consent and complete operation outcomes

Journey F SHALL exercise copy, move, shortcut/link creation, delete, supported Restore/Undo, conflict decisions, cancellation, failure feedback and recovery against owned fixtures. Replace requires applicable explicit consent; generated incoming results MUST survive later replacement work. Apply-to-all SHALL default unchecked, stay within the current request, distinguish Skip from Cancel and report per-item success/skip/failure accurately. Keep-both MUST reserve distinct names across a multi-item plan.

#### Scenario: Multiple collisions and next request
- **WHEN** a user applies Skip, Keep-both or Replace to all remaining collisions in a request and then starts another request
- **THEN** that choice affects only the first request, the next request starts unchecked without inherited consent, and actual inventories/content and reported counts agree

#### Scenario: Partial completion followed by failure or cancellation
- **WHEN** a transfer partially completes before a real error or accepted cancellation
- **THEN** completed results and untouched destinations remain intact, failures and unexecuted items are accounted for, and cancellation is neither false success nor an unrelated suppressed IO error

#### Scenario: Restore acceptance uses different seams
- **WHEN** a holding-folder Restore worker test passes but real multi-item Recycle Bin Undo has not executed
- **THEN** the worker result is retained for its seam and the actual Recycle Bin journey remains separately unqualified until safely demonstrated

### Requirement: Recovery and lifetime qualification

Journey G SHALL exercise missing/disconnected location behavior, engine unavailability, search cancellation, index rebuilding, empty directories, permission denial, failed-operation recovery and window shutdown/reopen in permitted owned environments. Acceptance MUST preserve bounded normal close, correct owned-process/window cleanup and user data; unavailable fault environments remain explicit rather than being destructively manufactured.

#### Scenario: Restore or preview intermittent is observed
- **WHEN** a cold-restore accessibility omission or preview terminal-state failure occurs
- **THEN** the first failure and later observations are preserved separately, safe diagnostic attempts are bounded, and a later pass alone does not establish a root-cause fix or unrestricted acceptance

### Requirement: Native accessibility and visual acceptance

Journey H SHALL exercise keyboard-only navigation, public UIA focus/selected-row/active-pane identity, labels, dialog focus and visible state. Native runtime screenshots SHALL receive actual visual inspection, separately from semantic assertions. Light/dark, High Contrast, DPI and reduced-motion claims MUST identify the live or synthetic environment actually tested.

#### Scenario: Element exists but pixels are wrong
- **WHEN** UIA exposes a surface whose content is occluded, clipped or targets the wrong visible workspace
- **THEN** semantic success does not pass visual or product acceptance, and the defect requires repair or remains unqualified

#### Scenario: Physical profile is unavailable
- **WHEN** geometry tests cover a DPI or accessibility profile absent from the physical runtime
- **THEN** those tests support only their software scope, not live physical-display or assistive-technology certification

### Requirement: Security and persistent-index contracts survive consolidation

Acceptance SHALL preserve the privileged/unprivileged split, closed privileged commands, signed-peer trust, parser bounds, fail-closed release configuration, ordinal search correctness, stable identities, transaction durability, candidate readiness/admission and additive migration/downgrade protections. No generic privileged file-operation facility or relaxed trust policy SHALL be introduced to make tests pass.

#### Scenario: Existing durable index is upgraded
- **WHEN** migration, interrupted recovery or candidate maintenance is exercised on a disposable database
- **THEN** committed metadata and canonical search results remain correct, unavailable/unready acceleration falls back safely, and an incompatible older application refuses destructive downgrade

### Requirement: Independent integrated review

Material resolutions and the final candidate SHALL receive read-only independent review of the actual relevant source state. Final review MUST cover distinct product/data-correctness and evidence/security/regression angles, record findings and scope, and re-review confirmed fixes. Known P0/P1 and unresolved material P2 findings SHALL block acceptance; lack of reviewer capacity SHALL remain a pending gate rather than approval.

#### Scenario: Individual PR reviews exist
- **WHEN** all component changes have prior scoped approvals but their composite has changed
- **THEN** those approvals remain attributed evidence and do not substitute for review of the new integrated behavior

### Requirement: Exact certification totals and protected exclusions

The certification report SHALL identify current configured/selected/passed/failed/skipped/excluded tests and commands by configuration and source state. Debug/release-test, Analyze, supported ASan, OpenSpec/lifecycle/drift, native E2E/UIA/visual and required journey results MUST remain distinct from hosted execution. Restricted process-reaper execution SHALL remain registered and unqualified unless its required permitted environment is available; no ownership restriction or test assertion is weakened.

#### Scenario: Selected suite passes while one required test is excluded
- **WHEN** all selected tests pass but a restricted required test did not run
- **THEN** the report names the exclusion, reports selected totals accurately, and does not claim unrestricted full-suite or complete local certification

#### Scenario: Failure is followed by a repair
- **WHEN** a test fails and a source or harness cause is corrected
- **THEN** the first result remains preserved, focused verification and the affected full available gate are rerun, and the correction does not rewrite the original result

### Requirement: Resource-safe and comparable performance qualification

The campaign SHALL preflight resources before expensive measurements and remeasure critical behavior affected by integration. Retained prior evidence MUST have explicit executable-input, fixture, toolchain/configuration and methodology applicability. Correctness or material common-path responsiveness regressions SHALL NOT be accepted in exchange for unrelated speedups; incompatible measurements SHALL NOT support absolute equivalence claims.

#### Scenario: Unchanged critical inputs have valid retained evidence
- **WHEN** the required source/build-input equivalence and fixture applicability are demonstrated
- **THEN** the old measurement is cited for that bounded scope without rerunning every expensive benchmark or rebinding its original source identity

### Requirement: Traceable candidate and evidence provenance

The final manifest SHALL connect starting main, accepted PR/local heads, resolution commits, implementation SHA, executed-test SHA, tree/build inputs, executable SHA-256 values and evidence references. Documentation-only follow-ups MUST receive executable-input equivalence validation after the final tracked edit. Component equality SHALL NOT imply whole-executable equivalence, and source equivalence SHALL NOT imply reproducible binaries.

#### Scenario: Handoff follows documentation-only commits
- **WHEN** a final report or WORKSTATE commit follows the tested implementation
- **THEN** the report preserves the tested identity, validates relevant-input equivalence to the actual final tip and derives live tip/hosted state externally without requiring its own containing SHA

### Requirement: Evidence-driven OpenSpec reconciliation

Predecessor task/lifecycle closure SHALL require surviving implementation and matching acceptance evidence or an explicit reviewed task-level transfer preserving the obligation. Historical failures MUST remain discoverable. Integration alone SHALL NOT bulk-complete or supersede feature changes; required unavailable tasks remain unchecked with evidenced lifecycle disposition.

#### Scenario: Task closure differs between branches
- **WHEN** a task is unchecked in an old simulation but checked with valid evidence on an incoming head
- **THEN** reconciliation verifies implementation and evidence, preserves the justified closure, and does not create duplicate implementation work from the stale checkbox alone

### Requirement: Policy-compliant publication and PR disposition

The campaign SHALL publish one clean consolidation candidate and use only normal non-force PR integration after positive verification of applicable branch policies, required local/hosted gates, reviews, provenance and absence of unresolved material regressions. Unknown protection policy SHALL NOT mean no protection. Original PRs MUST receive truthful content/test attribution and disposition before closure; branches SHALL NOT be automatically deleted.

#### Scenario: Hosted jobs cannot start
- **WHEN** the candidate's hosted jobs fail before steps due to an external account prerequisite
- **THEN** the report records hosted certification not obtained, no billing/protection setting is changed and no direct main push bypasses the blocked merge gate

#### Scenario: Consolidation includes an original PR
- **WHEN** an accepted consolidation merge includes that PR's unique accepted changes and tests without an independent merge of the original PR
- **THEN** its disposition is INCLUDED_BY_CONSOLIDATION with attribution rather than falsely MERGED

### Requirement: Independent readiness states and terminal handoff

The owner report SHALL account for every exact named success state and qualification condition enumerated in design D11 independently, with evidence and remaining actions, covering integration, safety, journeys, performance, review, OpenSpec, local/hosted/main and release readiness. It MUST distinguish code completion, integration, local qualification, hosted qualification, signing, clean-host/privileged/physical validation and production release authorization. A blocked handoff SHALL preserve a clean resumable published candidate, not fabricate completion or automatically launch another campaign.

#### Scenario: Safe executable work is complete but external requirements remain
- **WHEN** remaining mandatory tasks require unavailable permitted infrastructure or owner action
- **THEN** those tasks and success states remain explicitly unqualified, daily-use and production-readiness claims retain their scope, and the report identifies the exact external action without deploying a release
