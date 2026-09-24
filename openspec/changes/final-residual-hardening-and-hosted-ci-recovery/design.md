## Context

See [proposal.md](proposal.md) for motivation and [the residual certification spec](specs/residual-certification-integrity/spec.md) for the observable contract. The current `main` checkpoint is `a8533a772048ea39ad44c319a96ee91dc844506f`; this is a starting observation, not a fixed implementation target. The existing residual change's task 1.4 remains unchecked. `ffipc::WriteFrameOverlapped` serializes a complete header and payload into one buffer, while `WriteExactOverlapped` advances through short writes. `ReadFrameOverlappedDetailed` reads header and payload separately and treats a partial payload timeout as a lost frame boundary. The current scanner test covers a closed-reader terminal write, not selected partial byte counts. `verify/core/Provenance.psm1` records binary SHA-256, embedded commit identity, source commit/tree, and clean state; its own comment says the tree is not separately embedded. Current CI defines three build matrix entries plus ASan, hygiene, and release qualification. All historical results require fresh checking before certification.

The process and security constraints come from `openspec/changes/establish-architecture-foundation/design.md` D2–D4 and its Risks section: named pipes are the privilege boundary, the service is a constrained broker, commands are closed, handles are connection scoped, and authentication is symmetric. The earlier residual design's transport-fatal decision remains in force; this change supplies direct partial-write proof and changes product code only if that proof exposes a defect.

## Goals / Non-Goals

**Goals:**

- Exercise the serialized frame and real byte stream at deterministic cut points, including receiver and next-connection observations.
- Reach a documented, test-backed provenance decision without assuming that another embedded digest improves assurance.
- Bind local and hosted claims to actual candidate identity and evidence, then hand off exact external prerequisites.

**Non-Goals:**

- Rebuild the IPC architecture, extend the wire command set, or relax authentication, service ACLs, signing pins, or validation gates.
- Provision certificates, alter billing or payment settings, reconfigure displays, or claim production MFT/USN validation from a local test fixture.
- Reopen completed UI, visual, or prior certification campaigns without a new reproducer.

## Decisions

### D1 — Baseline is live state, not the supplied checkpoint

**Why:** The prompt's SHA, counts, and billing annotation are historical. Before implementation, capture current branch, HEAD, upstream, `origin/main`, tracked/untracked status, worktrees, concurrent activity, workflow, and existing tasks. Reconcile any advance without resetting. Preserve unrelated files and use the prior task 1.4 as the known uncovered behavior. **Alternative:** Treat `a8533a7` as authoritative and replay old certification; rejected because it could certify stale code. **Trade-off:** Fresh state collection takes time but prevents false exact-SHA claims.

### D2 — Inject a real byte prefix at the narrow transport write boundary

**Why:** A closed reader proves a failed write but not which bytes were emitted. The fixture will use the production serializer, frame validation, and connected byte-mode named pipes. A narrow write-operation seam around the `WriteExactOverlapped` completion loop will cap one real write at a chosen byte offset, then return a controlled failure on the next operation. The test will compare received prefix bytes and production read status, not merely a mocked return value. The implementation investigation must first confirm the smallest seam that keeps overlapped lifetime and cancellation behavior intact; it may use an internal adapter compiled into a focused test target. **Alternatives:** Timing-dependent reader closure is nondeterministic; a test-only serializer would not prove production bytes; rewriting `ffipc` is disproportionate. **Trade-off:** The seam must be isolated so ordinary production calls always use the same Win32 path and cannot be influenced by a remote peer.

The boundary matrix is zero bytes; partial header; complete header; partial payload; and all but the final byte. The service-facing fixture must also interrupt terminal delivery, observe `TransportFailed` and connection abort, confirm no subsequent terminal, then use a new authenticated connection to check fresh volume/journal state. A pure framing fixture cannot by itself prove service teardown or engine retry; focused integration tests will cover those layers. Existing cancellation, failed-terminal, and bounded `ScanFailed` retry tests are retained and extended only where coverage is missing.

### D3 — Make provenance a threat-model decision gate

**Why:** `Get-SourceProvenanceRecord`, `Test-BinarySourceBinding`, exact-tree qualification, release manifest, and binary hashes already cover distinct mismatch classes. Compare each proposed attack to an actual acceptance path. Record `IMPLEMENT` only for a reproducible inappropriate artifact that passes current gates and a digest would reject; otherwise choose `ALREADY COVERED` or `NOT JUSTIFIED`. **Alternative:** Add an embedded tree digest unconditionally; rejected because it duplicates the current clean commit/tree identity for many cases and cannot prove compiler inputs or build provenance on its own. **Trade-off:** A conditional outcome leaves the digest design unfinished unless the evidence triggers it, which avoids adding misleading complexity.

If triggered, the digest design must fix included and excluded files, normalized paths, ordering, per-file content hashing, clean/untracked treatment, generated inputs, configuration/toolchain scope, and an independent verification point. Avoid a digest cycle through the binary containing itself. Negative checks cover modified/missing inputs, malformed or wrong digest, mixed and stale binaries, and dirty-state mismatch. Documentation must state exactly which claim the digest supports.

### D4 — Certify hosted jobs only from the final pushed SHA

**Why:** The current workflow's required six executions are debug, release-test, analyze, ASan protocol/IPC/lifecycle, repository hygiene/contracts, and unsigned release candidate qualification. Confirm the live workflow and GitHub run state first. If an account-level annotation still blocks job startup, record the run and exact text, avoid repeat dispatches, and classify hosted CI externally blocked. Once available, trigger or observe an authorized run for the final pushed SHA and inspect each job, skip, and required artifact. **Alternative:** Use local parity or workflow aggregate status as hosted proof; rejected because neither proves all hosted jobs executed successfully. **Trade-off:** Hosted closure remains external if the account cannot start jobs.

### D5 — Validate by candidate and review by independent attack surface

**Why:** The final implementation candidate needs focused tests first, then the complete applicable local ladder from the prompt: debug and release-test CTest, Analyze, ASan slice, strict OpenSpec, CI parity, identity/provenance negatives, governance contracts, UIA/E2E, visual catalog, hygiene, and documentation drift. Preserve actual skips and bind retained evidence to the candidate's commit and tree. Independent reviewers separately attack transport failure, provenance, certification truth, and regression/scope; each finding needs a reproduction or source-backed explanation. **Alternative:** Rely on historical green counts; rejected because relevant source edits invalidate them. **Trade-off:** Full certification is expensive, so implementation iterations use focused validation before the final ladder.

### D6 — External handoff uses existing certification infrastructure

**Why:** Production signer and pins, clean host, elevated MFT/USN, disposable guest, and display configurations require capabilities outside ordinary development. Inventory the canonical commands and expected evidence already present in `verify/` and report which prerequisite unlocks each. Do not create replacement infrastructure or call a missing scenario passed. **Alternative:** Rebuild a campaign around external gates; rejected by the supplied scope and existing harness coverage. **Trade-off:** Local residual closure can be complete while external certification stays open.

## Risks / Trade-offs

- **[Risk] Injected short writes bypass real serialization or completion semantics.** → Inspect the actual prefix on a named pipe, invoke production read code, and keep injection at the write operation rather than a replacement frame builder.
- **[Risk] A partial frame blocks the receiver or leaves an overlapped worker live.** → Use bounded deadlines and explicit worker/handle cleanup assertions; report a timeout as failure, never silently retry the same byte stream.
- **[Risk] A source digest is mistaken for a build attestation.** → State the precise accepted mismatch it closes and keep binary hashes, build inputs, and compiler provenance separate.
- **[Risk] New commits make a previously certified SHA stale.** → Rebuild and rerun affected gates on the final candidate, compare tree identities, and inspect hosted jobs for the pushed SHA.
- **[Risk] External blockers tempt a local pass to be promoted to hosted or production success.** → Maintain explicit `BLOCKED_EXTERNAL` and scenario skip classifications and include exact unlock actions.

## Migration Plan

No wire or on-disk migration is expected. Land focused implementation commits, certify the final source candidate, then commit the evidence handoff with executable-tree equivalence documented if only documents change. Revert a faulty fixture or optional digest through an ordinary corrective commit; preserve history and revalidate the affected candidate. Do not force-push or alter repository security policy.
