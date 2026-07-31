# Vendored SQLite amalgamation (see NOTICE.md for version/provenance).
# Compiled as C, deliberately exempted from this repo's strict /W4 /WX
# policy -- that policy governs FastFiles' own code, not a ~9500-line
# third-party amalgamation we don't maintain.
add_library(sqlite3 STATIC sqlite3.c)
target_include_directories(sqlite3 PUBLIC .)

target_compile_definitions(sqlite3 PRIVATE
  SQLITE_THREADSAFE=1
  SQLITE_DQS=0
  SQLITE_DEFAULT_MEMSTATUS=0
  SQLITE_OMIT_DEPRECATED
  SQLITE_OMIT_SHARED_CACHE
  SQLITE_USE_URI=0
  SQLITE_ENABLE_FTS3=0
  SQLITE_ENABLE_FTS4=0
  SQLITE_ENABLE_FTS5=0
  SQLITE_ENABLE_RTREE=0
)

if(MSVC)
  # Overrides win: target-level flags are appended after the directory-
  # level /W4 /WX /permissive- /sdl inherited from the root CMakeLists.
  target_compile_options(sqlite3 PRIVATE /W3 /WX- /wd4996)
endif()
