# Semantic invariant matrix

| Subject | Valid rule | Failure reason family |
| --- | --- | --- |
| Completed/superseded/external-only change | at least one authoritative evidence object | `authoritative-evidence-required` |
| Supersession | target exists, is authoritative, is not self/cyclic/ambiguous/superseded | `supersession-*` |
| Active override | parent active, local category, not unchecked PASS | `active-task-*` |
| External override | non-local category and no local component | `external-task-*` |
| Mixed override | both local and non-local categories | `mixed-task-*` |
| Historical override | completed or external-only parent plus authoritative evidence | `historical-task-*` |
| Superseded override | superseded parent with target plus authoritative evidence | `superseded-task-*` |
| Status/category | environment/required statuses require matching blocked categories | `*-status-category-mismatch` |

Invalid metadata is retained in raw inventory and projects to ambiguity; it
never silently becomes a zero-work STOP or a production-ready result.
