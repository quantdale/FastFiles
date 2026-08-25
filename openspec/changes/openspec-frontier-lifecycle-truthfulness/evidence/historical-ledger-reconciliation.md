# Historical ledger reconciliation

The lifecycle model is intentionally conservative. Only delivery tasks with
later Git/CI evidence are checked; external prerequisites remain unchecked.

## Checked from later evidence

- `nonblocking-indexed-read-plane/8.2`: later runtime-certification validation
  recorded Debug CTest `83/83` and release-test CTest `83/83` through the
  supported Visual Studio environment; the original host-pressure observation
  remains in `docs/agent/WORKSTATE.md`. Evidence commits include `e621b70` and
  `08b794d`.
- `nonblocking-indexed-read-plane/8.5` and `8.6`: the phase-9 delivery was
  reconciled and pushed in `f332f22`; the exact hosted run was recorded in the
  durable handoff evidence. The current repository is again verified
  `HEAD == origin/main` before this campaign.
- `phase8-release-readiness-convergence/4.5`: the phase-8 implementation and
  handoff were reconciled into main by `55408b7` and later pushed mainline
  checkpoints record hosted CI evidence.
- `real-user-dogfooding-release-engineering/5.4`: the final dogfood evidence
  was recorded in `83e668c`, followed by later mainline pushes and hosted
  handoff records in `docs/agent/WORKSTATE.md`.

Each checked line will retain a short annotation naming the later commit and
the durable evidence source. This is not a claim that production signing,
privileged MFT/USN, guest/provider, clean-host, or interactive evidence exists.

## Intentionally open

The following remain unchecked and are explicitly represented as external or
environment-blocked in `openspec/lifecycle.json`:

- authorized signer/pins and signed privileged runtime;
- real elevated MFT/USN and `fftest` service evidence;
- clean-host and snapshot-restorable guest/provider validation;
- physical/interactive/UIA repeatability and display matrices;
- authenticated PR #6 administration;
- any task whose final clause still depends on those prerequisites.

The model does not close an external task merely because local implementation or
portable tests passed.
