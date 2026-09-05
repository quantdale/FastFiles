## Context

The repository already has a PowerShell capability registry, per-run envelopes, provider resolution, release-candidate packaging, release-readiness composition, and CTest-registered hygiene scripts. The gap is that these surfaces are individually authoritative but not composed into a small, deterministic frontier artifact. Existing OpenSpec task text and historical `progress.md` prose also use different vocabularies for external availability. See `proposal.md` and the three delta specs for the externally visible contract.

## Goals / Non-Goals

**Goals:**

- Derive status from Git, CTest metadata/results, OpenSpec ledgers, capability manifests/doctor probes, and release tooling at execution time.
- Keep structured JSON as the source of truth and make Markdown a sanitized projection.
- Give provider adapters stable diagnostics and a fake lifecycle contract without claiming real guest evidence.
- Make hosted CI fail with named, bounded gates while preserving unsigned non-production release preparation.

**Non-Goals:**

- No new product IPC, service command, privilege, Authenticode, trust-store, or snapshot contract.
- No automatic Windows-media acquisition, EULA acceptance, credential generation, guest provisioning, or production signing.
- No deletion or rewriting of historical evidence and no inclusion of `verify/runs`, signing state, or screenshots in generated tracked status.

## Decisions

### D1 — Status JSON is authoritative; Markdown is derived

`verify/status/Get-ProjectStatus.ps1` will gather a schema-versioned object and write an explicitly named JSON artifact plus Markdown projection. The generator will use Git commands, `ctest -N`/last result evidence, `openspec` task ledgers, registry manifests, doctor inventory, and release-candidate/readiness artifacts. `progress.md` will become a short entry point containing only stable architecture guidance and a generated-status pointer; dynamic counts will not be hand-maintained.

Why: a single structured source prevents the recurring stale-count problem and permits CI/tests to validate fields without scraping prose. Alternatives rejected: keeping `progress.md` authoritative (already failed) and making the release-readiness report the only source (it intentionally has a different gate scope and does not enumerate OpenSpec tasks).

### D2 — Classification is rule-based and evidence-preserving

Task classification will inspect task text and explicit markers for `interactive`, `elevated`, `provider`, `signing`, `clean-host`, `external`, `required-but-unavailable`, and environment terms. Ambiguous tasks default to `local` rather than being hidden, and every classification includes the source line and reason. Status values remain separate from categories: `PASS`, `FAIL`, `SKIPPED`, `BLOCKED_ENVIRONMENT`, and `REQUIRED-BUT-UNAVAILABLE` are never collapsed.

Why: deterministic, explainable classification is more useful than an opaque heuristic, while a conservative local default surfaces work for review. The classifier is a pure PowerShell function so fixture ledgers can test it without changing the repository.

### D3 — Providers advertise preflight and own all resources

The provider descriptor gains optional preflight/source metadata entry points while retaining interface-major compatibility. The core calls preflight before activation and writes a sanitized result. A fake provider module is used for contract tests; the real vendor-specific VM-provider adapter remains a plug-in and reports stable unavailable reasons when its source image or guest is absent. User-supplied paths are expanded only at runtime, validated as files with defensive metadata checks, and represented by hashes/size/type rather than persisted absolute paths in committed evidence.

Why: this leaves the only missing real resource—the lawful Windows guest—outside the code while avoiding an adapter that claims a zero-byte VHDX is runnable. Alternatives rejected: an OS-less fake vendor-specific VM-provider success and hardcoded machine paths.

### D4 — CI composes existing gates with bounded jobs

The workflow will use named jobs for repository hygiene/status, build/test matrix, ASAN, provider/release tooling, and optional interactive/provider jobs that are explicitly skipped unless their lawful context is present. Portable trust tests remain default; privileged trust-store tests and production signing remain opt-in. Safe artifacts are limited to JSON/JUnit/Markdown summaries and redacted logs.

Why: separate jobs make failures diagnosable and avoid repeating the full build for every policy check. The workflow will not set production pins or `FASTFILES_ENABLE_PRIVILEGED_TRUST_TESTS`.

### D5 — Release classification is fail-closed and non-production by construction

The status generator consumes the existing unsigned candidate manifest and release-readiness gates, but never infers production readiness from unsigned preparation. Missing/placeholder/malformed signer or pin inputs remain blocked/fail-closed. The final report maps gate reasons and OpenSpec tasks into the five release-frontier categories and includes exact next commands without copying sensitive paths.

## Risks / Trade-offs

- **[Risk] Text classification can misclassify a novel task.** → Preserve source text/line and classify unknowns as local with `classificationConfidence=low`; add fixture cases for new markers.
- **[Risk] CTest output is not always available outside a configured build.** → Record `SKIPPED` with `ctest-not-run` and discovered count `null`; never use the maintained count as a substitute.
- **[Risk] CI artifacts can contain user paths.** → Sanitize absolute paths to repository-relative or `<redacted>` values before projection/upload; keep raw run trees untracked.
- **[Risk] Provider preflight may be mistaken for guest validation.** → Use `REQUIRED-BUT-UNAVAILABLE` for absent guest evidence and state in every report that fake-provider PASS proves orchestration only.
- **[Risk] Adding a status command can drift from existing release readiness.** → Include source identity and schema version in both artifacts and add cross-check tests for shared gate fields.

## Migration Plan

1. Add the status/provider/release contract tests and generator behind existing PowerShell/CTest discovery.
2. Generate status artifacts in an ignored `verify/status/` output directory by default; allow an explicit output path for CI/evidence collection.
3. Replace dynamic prose in `progress.md` with a pointer and stable interpretation rules.
4. Add CI jobs incrementally; a failed optional provider/interactive preflight remains a named unavailable result rather than a generic workflow failure.
5. Roll back by removing the new CI jobs and generator invocation; no product data or installer state is migrated.
