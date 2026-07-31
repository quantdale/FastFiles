# Vendored SQLite

Amalgamation `sqlite3.c`/`sqlite3.h` from the official SQLite amalgamation
release `sqlite-amalgamation-3530400` (SQLite 3.53.4). Provenance, download
URL, and the zip checksum verified at vendoring time are recorded in
[NOTICE.md](NOTICE.md).

Bundled/statically-linked per `openspec/changes/index-storage-and-scanning`
task 1.1 -- no separate SQLite install or system dependency is required to
build `FastFilesEngine`.

Do not hand-edit `sqlite3.c`/`sqlite3.h`. To upgrade, replace both files with
a newer amalgamation release and update NOTICE.md.
