# Evidence reference model

| Kind | Required identity | Offline resolution | Authority |
| --- | --- | --- | --- |
| `repository-path` | repository-relative `path` | exists under the root; no absolute/traversal/reparse-point path | authoritative artifact |
| `git-commit` | full `sha` | resolves as a commit and is reachable from `HEAD` | authoritative history |
| `hosted-run` | numeric `runId`, full `sha`, tracked `record` | record contains the exact run ID and SHA; no network lookup | authoritative durable CI binding |
| `note` | non-empty `text` | safe-text redaction only | non-authoritative context |

Schema shape is necessary but not sufficient. Runtime resolution adds
`valid`, `authority`, and `authoritative` to projected evidence. A note alone
cannot justify completed, superseded, external-evidence-only, historical, or
superseded task state.
