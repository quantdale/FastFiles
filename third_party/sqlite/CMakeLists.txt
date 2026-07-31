# Vendored SQLite amalgamation, built as a small static library.
# SQLITE_THREADSAFE=1 (serialized mode): the engine's ingestion pipeline,
# reconciliation sweeps, and diagnostic reads (filesystem-index-store design.md
# D1) all touch the same sqlite3* connection pool from different threads.
add_library(sqlite3 STATIC sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(sqlite3 PUBLIC
  SQLITE_THREADSAFE=1
  SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
  SQLITE_OMIT_DEPRECATED
  SQLITE_OMIT_LOAD_EXTENSION
  SQLITE_ENABLE_FTS3=0
)

if(MSVC)
  # Vendored, unmodified third-party C source -- do not hold it to this
  # project's own /W4 /WX bar (task 1.1 note: "bundled/statically-linked
  # dependency", not first-party code).
  target_compile_options(sqlite3 PRIVATE /W0 /wd4100 /wd4267)
  # Silence the same source's warnings without disabling the whole-project
  # /WX for anyone consuming this target.
  set_source_files_properties(sqlite3.c PROPERTIES COMPILE_FLAGS "/W0")
endif()
