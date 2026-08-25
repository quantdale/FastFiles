# Verification-orchestration baseline

This is the short campaign entry point required by the planner handoff. The
authoritative baseline and root-cause trace is maintained in
[`baseline-and-root-cause.md`](baseline-and-root-cause.md), which preserves the
historical 23 PASS / 6 SKIP / 0 FAIL export, the opaque `scenario-failed`
wrapper observations, exact interrupted-run identities, focused comparison,
and current resource-boundary classifications.

The current conclusion is a verifier lifecycle/composition defect: the prior
supported wrapper had no killable outer capability boundary and could lose the
authoritative result while UIA/PowerShell work remained active. No product
state transition is inferred from that missing terminal result.
