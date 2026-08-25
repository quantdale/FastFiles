# Current manifest audit

The migration inventory is intentionally explicit. Every entry is retained;
only its evidence representation changes.

| Entry | State | Evidence disposition |
| --- | --- | --- |
| `autonomous-runtime-verification` | external-evidence-only | Workstate and task ledger repository artifacts; provider/guest gates remain open. |
| `close-independent-validation-gaps` | external-evidence-only | Production-validation finding matrix and workstate; signing/privileged gates remain open. |
| `interactive-ui-runtime-determinism` | external-evidence-only | Workstate/task ledger plus explicit interactive task override. |
| `nonblocking-indexed-read-plane` | completed | Historical ledger artifact plus three full reachable implementation commits. |
| `phase8-release-readiness-convergence` | external-evidence-only | Historical ledger, full delivery commit, and workstate; external release gates remain open. |
| `real-user-dogfooding-release-engineering` | completed | Historical ledger, full delivery commit, and workstate. |
| `runtime-certification-and-verifier-convergence` | external-evidence-only | Workstate/implementation evidence and explicit provider/interactive/clean-host overrides. |
| `ship-ready-ui-ux-hardening-convergence` | external-evidence-only | Workstate, audit artifact, full implementation commit, and explicit external overrides. |
| `zero-touch-autonomous-engineering` | external-evidence-only | Workstate/task ledger; provider and production gates remain open. |
| `openspec-frontier-lifecycle-truthfulness` | completed | Prompt/task artifacts, full implementation commit, and a hosted-run record bound to that SHA. |
| `stabilize-live-uia-e2e` | superseded | Workstate and the named replacement change directory. |
| `openspec-lifecycle-evidence-integrity-and-semantic-convergence` | active | This campaign's proposal/design/tasks artifacts. |

Abbreviated commit labels were expanded with `git rev-parse`; the hosted run
number is not accepted alone and is bound to the tracked implementation
evidence record.
