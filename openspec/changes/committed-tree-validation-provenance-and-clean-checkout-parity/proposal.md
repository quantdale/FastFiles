# Proposal: committed-tree validation provenance and clean-checkout parity

## Problem

FastFiles already binds verifier capabilities to `runId`/`invocationId` and hosted
lifecycle evidence to exact Git SHAs, but a local PASS does not currently prove
which Git tree the checks read. The preceding repository-truth campaign reproduced
a false-green handoff: local validation observed corrected dirty files while the
pushed commit still contained stale tracked content and failed CI.

This is an evidence-identity defect, not a request to make ordinary development
clean-tree-only.

## Change

Add one repository-state provenance authority and a dedicated exact-tree
qualification lane. Qualifying evidence records full HEAD and tree SHAs, source
cleanliness, deterministic source-state digests, and start/finish mutation checks.
A unique detached local worktree validates the committed tree without a network
clone or destructive cleanup. Status/release-frontier consumers re-bind explicitly
selected local results to the current candidate and reject stale or development-only
PASS evidence.

## Scope

In scope:

- Git/source-state provenance for local validation evidence;
- explicit development vs exact-tree qualification semantics;
- hermetic adversarial Git-state fixtures;
- fast committed-tree parity for repository-sensitive gates;
- project-status/release-frontier stale-result rejection;
- current guidance for the qualification workflow.

Out of scope:

- product/UI behavior changes;
- production signer or pin provisioning;
- privileged MFT/USN certification;
- clean-host/provider/guest or interactive UI evidence;
- destructive worktree management;
- replacing hosted CI exact-SHA evidence.

## Success criteria

A dirty development PASS cannot silently qualify HEAD; an exact-tree result is
accepted only for the same full commit/tree it recorded; source mutation during a
qualifying run invalidates it; and a local detached materialization proves the fast
repository-sensitive gates against the committed tree while preserving all existing
external release boundaries.
