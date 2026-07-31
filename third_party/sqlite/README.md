# Vendored SQLite

Amalgamation `sqlite3.c`/`sqlite3.h` from the official SQLite amalgamation
release `sqlite-amalgamation-3450300` (SQLite 3.45.3), downloaded from
https://www.sqlite.org/2024/sqlite-amalgamation-3450300.zip.

Bundled/statically-linked per `openspec/changes/index-storage-and-scanning`
task 1.1 -- no separate SQLite install or system dependency is required to
build `FastFilesEngine`.

Do not hand-edit `sqlite3.c`/`sqlite3.h`. To upgrade, replace both files with
a newer amalgamation release and update the version noted above.
