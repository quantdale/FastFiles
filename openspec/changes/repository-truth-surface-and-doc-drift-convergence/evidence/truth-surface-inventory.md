# Truth-surface inventory

Every surface an agent or maintainer can reasonably read to decide what the
system is, what is active, and how to build/test it, with the claim classes it
carries. Classification per planner Workstream 1: machine-derived (M), stable
rationale (R), historical (H), dynamic status (D), external prerequisite (E).

| Surface | Role | Claim classes | Current-state risk | Drift-detectable today? |
| --- | --- | --- | --- | --- |
| `README.md` | Public overview; build/test/verify entry | M (verbs, presets, targets), R (architecture rationale), E (release/signing) | HIGH — verb list stale; "current change" claims stale | No |
| `AGENTS.md` | Primary agent guide | M (verbs/exit codes/presets/tests), R (security invariants), H-adjacent campaign notes | MEDIUM — snapshot mechanics stale ("double-buffered") | No |
| `CLAUDE.md` | Stub pointing at AGENTS + quick facts | M (build commands), current-work pointer | HIGH — names closed campaign as current open work | No |
| `CODE_INDEX.md` | Component/target map for navigation | M (targets, snapshot/data-flow mechanics, test table) | HIGH — SnapshotPublisher mechanics stale; data-flow wording imprecise | No |
| `AUTONOMOUS.md` | Autonomous loop contract | M (loop stages, intake verbs) | LOW — no live contradiction found | No |
| `verify/README.md` | Verifier usage authority-mirror | M (verb table, exit codes, tiers) | NONE found — accurate vs verify.ps1 | No |
| `progress.md` | Status narrative | D (counts), pointers | OK by design — counts delegated to generator | n/a (dynamic) |
| `docs/agent/WORKSTATE.md` | Durable execution memory | H checkpoints + one current frontier | Out of semantic scope by design (checkpoint log); current-frontier section maintained per protocol | n/a (historical) |
| `.agent/PLANNER_HANDOFF.md`, `.agent/EXECUTION_PROMPT.md` | Planner/executor contract | M (Status field consumed by continuation semantics) | Authoritative input, not a restating surface | Via lifecycle workstream 7 (existing) |
| `openspec/**` (proposals/designs/tasks/evidence) | Campaign ledgers | H once completed | Historical by definition; excluded from guard scope | n/a |
| `docs/release/**`, `docs/quality/**`, `docs/benchmarks/**` | Release/quality/bench records | H/E | Spot-audited; no current-facing enumeration claims beyond recorded runs | n/a |
| `docs/agent/history/` audit/recovery/frontier reports (`ENGINEERING_AUDIT_REPORT.md`, `FASTFILES_FRONTIER_HANDOFF.md`, `FASTFILES_RECOVERY_REPORT.md`) and `codebase-audit/sessions/**` | Point-in-time engineering reports | H | Classified historical: they describe the tree at a recorded moment (several still use pre-generation snapshot wording). Adding them to the guard's surface set would rewrite evidence; their age is visible from content/paths, and no continuation contract consumes them as current authority. | n/a |

## Duplicated machine-derived claims mapped for checking

1. Verifier verbs: README §Runtime verification; verify/README §Verbs;
   AGENTS §Runtime verification. Authority: `verify/verify.ps1` ValidateSet.
2. Exit codes: AGENTS §Runtime verification; verify/README. Authority:
   `$Exit*` constants in verify.ps1.
3. Configure/build presets: README §Building; AGENTS §Kilo configuration.
   Authority: `CMakePresets.json`.
4. Test presets: AGENTS ("CTest presets exist for debug, release, release-test").
   Authority: `CMakePresets.json.testPresets`.
5. Executable topology: README Architecture table; AGENTS Architecture.
   Authority: `add_executable(FastFiles*)` under src/**.
6. Static-lib topology: README Shared static libraries; CODE_INDEX sections.
   Authority: `add_library(ff* STATIC)` under src/**.
7. Current-campaign claims: README §conventions; CLAUDE quick facts.
   Authority: `openspec/lifecycle.json` states (+ prompt Status).
8. Snapshot publication: AGENTS keystroke-search paragraph; CODE_INDEX
   UiServer bullet + Data flow step 2. Authority:
   `src/engine/src/SnapshotPublisher.cpp` seam.

## Consciously out of mechanical scope (recorded, with reason)

- Product version literals: docs reference `VERSION` as pointer; no numeric
  copy exists to check (verified by search).
- Dynamic counts (raw unchecked tasks etc.): must never be fenced; prose
  points to the status generator.
- Hosted-run IDs inside historical evidence: immutable history.
