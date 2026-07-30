## Why

FastFiles' three pillars (instant search, storage visualization, Finder-style navigation) all depend on a filesystem index that can be updated incrementally without full rescans — which in turn requires continuous elevated access to the NTFS USN Change Journal, not a one-shot admin toggle. Building any pillar directly on top of an ad-hoc or unelevated implementation would either violate the "no admin required for normal operation" constraint or force a redesign later. This change establishes the technology stack and the privileged/unprivileged process split as working, tested code — decided through comparative stack research and a three-lens adversarial security review — so every subsequent capability (search, storage analysis, file operations) has a safe foundation to build on rather than being blocked on or rewritten around this decision later.

## What Changes

- Establish the native C++ (Win32 + COM + Direct2D/DirectComposition) project skeleton: solution/build structure for three executable targets, no UI framework dependency.
- Introduce the three-process architecture: `FastFilesIndexSvc` (privileged, stateless Windows Service, `SeBackupPrivilege` only), `FastFilesEngine` (unprivileged, per-logon-session index owner), `FastFiles` (UI shell) — as buildable, running stubs.
- Implement the hardened named-pipe IPC contract between `FastFilesEngine` and `FastFilesIndexSvc`: symmetric mutual authentication (image path + pinned Authenticode signature on both sides, re-validated periodically, not just group membership), connection-scoped opaque handles, bounded/validated frame format, no SCM control rights granted to the client group, self-directed staleness detection instead of externally-triggered restarts.
- Implement the `FastFilesEngine` degraded-mode fallback (unprivileged `FindFirstFileEx` enumeration + `ReadDirectoryChangesW` watches) as a first-class, permanently-supported state — not an error path — so the app is fully usable when the service is absent, declined, or unavailable.
- Implement basic Finder-style Column View navigation in `FastFiles`, backed initially by the engine's degraded-mode (non-privileged) directory listing, so there is an end-to-end, usable slice of the product before any MFT/USN parsing exists.
- **Explicitly out of scope for this change** (deferred to follow-up changes once the index-storage-shape decision is made): raw MFT parsing and USN journal reading inside `FastFilesIndexSvc` (the service ships as a structural/protocol skeleton first), the in-memory search index and search UI, storage/treemap analysis, file operations beyond read-only navigation, tabs, dual-pane, bookmarks, and all other product-surface features from the original brief.

## Capabilities

### New Capabilities
- `privileged-index-service`: The minimal, stateless Windows Service (`FastFilesIndexSvc`) that will provide privileged raw-volume/MFT/USN access — this change specs and stands up its process shape, command protocol, and security model (auth, framing, privilege minimization); MFT/USN parsing logic itself lands in a follow-up change.
- `index-engine`: The unprivileged per-session background process (`FastFilesEngine`) that owns the filesystem index abstraction, manages the privileged-connection lifecycle (connect/handshake/reconnect/degrade), and publishes index state to UI clients over a same-privilege IPC seam.
- `column-view-browsing`: The Finder-style multi-column hierarchical navigation UI in `FastFiles`, reading directory contents through the index-engine's degraded-mode path.

### Modified Capabilities
(none — greenfield project, no existing specs)

## Impact

- New repository structure: a C++ solution with three executable targets (`FastFilesIndexSvc`, `FastFilesEngine`, `FastFiles`) plus a shared IPC-protocol library.
- New build tooling (CMake or MSBuild-based; decided in design.md) and a Windows-only development/test setup.
- No existing code affected (greenfield project).
- Establishes the IPC protocol and process-boundary contract that every later capability (search, storage analysis, file operations, tabs, dual-pane) will depend on — this is the highest-leverage, hardest-to-change decision in the whole product, which is why it is being locked down and built first.
