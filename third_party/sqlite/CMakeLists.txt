# Vendored SQLite amalgamation, built as a small static library.
# SQLITE_THREADSAFE=1 (serialized mode): the engine's ingestion pipeline,
# reconciliation sweeps, and diagnostic reads (filesystem-index-store design.md
# D1) all touch the same sqlite3* connection pool from different threads.
#
# durable-cold-search-index-and-hybrid-residency 3.1: SQLITE_ENABLE_FTS5 is
# now enabled — the durable cold candidate index (entry_name_terms) is an
# FTS5 trigram table maintained inside the store's write transactions. The
# historical "SQLITE_ENABLE_FTS3=0" define is REMOVED: in the amalgamation
# the macro's mere DEFINITION enables FTS3/4 (lane A P2), so it did the
# opposite of its apparent intent. No other compile option changes.
add_library(sqlite3 STATIC sqlite3.c)
source_group("SQLite" FILES sqlite3.c sqlite3.h)
target_sources(sqlite3 PRIVATE sqlite3.h)
set_target_properties(sqlite3 PROPERTIES FOLDER "Third Party/SQLite")
target_include_directories(sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(sqlite3 PUBLIC
  SQLITE_THREADSAFE=1
  SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
  SQLITE_OMIT_DEPRECATED
  SQLITE_OMIT_LOAD_EXTENSION
  SQLITE_ENABLE_FTS5
)

if(MSVC)
  # Vendored, unmodified third-party C source -- do not hold it to this
  # project's own /W4 /WX bar (task 1.1 note: "bundled/statically-linked
  # dependency", not first-party code).
  target_compile_options(sqlite3 PRIVATE /W0 /wd4100 /wd4267)
  # Silence the same source's warnings without disabling the whole-project
  # /WX for anyone consuming this target. /analyze- too: the FTS5 code paths
  # pull winnt.h rotation intrinsics whose SAL declarations trip C28301
  # under /analyze (SDK-side noise, observed in the parity analyze step).
  set_source_files_properties(sqlite3.c PROPERTIES COMPILE_FLAGS "/W0 /analyze-")
endif()
