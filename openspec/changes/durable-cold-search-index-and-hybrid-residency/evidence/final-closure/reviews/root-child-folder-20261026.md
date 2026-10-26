# Independent root-child folder review

Read-only native explorer `/root/root_folder_review`, fresh context, inspected acceptance branch production patch and regression; no files modified and no experiments run.

Production fix correct and narrowly scoped: append separator only after a successful durable walk with zero non-root segments. Hot root-child folder is `C:\`, old cold folder `C:`; Windows path joining in SearchPanel Open and WindowShell Reveal can resolve the latter relative to the drive current directory. Nested paths and root candidates retain existing behavior. No P1/P2 found in production delta.

Regression covers direct-root file/directory, nested file, root candidate, ordered every-field equality/truncation and absolute joined paths. Reviewer requested explicit resident/hot-row and cold/zero-hot-row premises to prevent three-cold-arm false confidence. Parent added those checks and focused suite passes1/1 (5.67s). Source-only approval does not certify complete PR15 or product runtime.

Historical scope:269d1c3..db7887c1 changes only Projection::EvictSubtree; callers are memory-enforcement paths. Explicit oracle Unload calls unchanged EvictVolume. Fresh single-volume2M setup exceeds400MiB at512B/row and returns cold before streaming/enforcement. Retained13-cell cold oracle has bounded equivalence over that subtree-only historical delta; not extended10 cells, automatic transitions, whole PR, or new root-folder fix.
