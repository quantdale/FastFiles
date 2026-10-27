## Evidence and problem

Fresh PR27ec5bd95 source: sourcesA/B, A target absent, B collides. While answering the real B question, CREATE_NEW creates A target with unique sentinel bytes. Skip B; legacy worker overwrites A and reports completed1/failures0/skipped1. Source bytes and unrelated sentinel remain; owned fixture cleans normally. This is a demonstrated data-preservation defect, not a timing inference.

Temporary callback logging shows flags554 (including TSF_OVERWRITE_EXIST) for both genuinely absent and newly occupied targets. Logging was removed before implementation. Therefore flags alone cannot recognize unexpected replacement; another existence check merely narrows a race. Public Shell FOF_RENAMEONCOLLISION delegates name collision handling to the write boundary.

## Decision

The ordinary operation uses FOF_RENAMEONCOLLISION for Copy/Move/Restore. A late collision keeps both rather than overwriting; explicit approved Replace items go to a separately configured IFileOperation. All preflight decisions complete before either executes, and both setups/Advise steps must succeed before mutation. Execute ordinary group then explicit replacements; preserve relative order inside groups. One shared sink accumulates actual callbacks and produces one terminal event. Skip counts and original totals remain request-scoped. Cancel or Shell abort stops before the replacement phase. Each StartOperations clears ETA samples to avoid unsigned subtraction across reset work-unit counters. Progress within a native phase remains Shell work units; no new overall latency bound claimed.

Move undo uses real resulting callback paths including Shell-generated collision names. Explicit Replace continues to suppress destructive Move undo for the entire batch. Restores retain opaque holding/Shell identities. No source/destination data is predeleted. Existing setup/submission/error behavior remains fail-closed.

## Scope and tradeoffs

Late collision behavior intentionally preserves both items rather than failing or adding a second modal prompt. Explicit preflight Skip/Keep both/Replace remains intact. Actual directory Copy reports callbacks for root and child, inflating completed2 plus skipped1 against total2. Register submitted Shell roots using canonical filesystem paths (ordered case-insensitive lookup) and canonical IShellItem fallback for virtual identities; only root successes enter completion/history, while all child failures remain retained. Recycle identity capture attaches only when the current root callback appended a path, preventing a child identity from retargeting the preceding root. Existing byte progress remains Shell work units. Link and Rename/NewItem policies are outside this transfer repair; no atomic guarantee is inferred for those routes. Public provider behavior must be verified using actual file/directory transfers; portable pure tests alone cannot close this change.

## Validation

Controlled source/destination exact bytes/inventory and callback counts for Copy/Move/Restore, late and absent targets; actual Move undo path. Directory collision preserves an already existing nested name. Existing controlled worker covers explicit Replace/Skip/KeepBoth and mixed collision/noncollision Restore. Combined native conflicts/cancellation and broader portable tests remain integrator requirements. No release/privileged qualification inferred.
