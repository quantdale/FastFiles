add_executable(ffindexstore_store_tests test_store.cpp)
target_link_libraries(ffindexstore_store_tests PRIVATE ffindexstore)
add_test(NAME ffindexstore_store_tests COMMAND ffindexstore_store_tests)

# large-index-lifecycle-and-search-scalability review-remediation (P2-A):
# compiles Projection.cpp with the FF_INDEXSTORE_TESTING fold-exceptional seam
# into this target only (same pattern as the isolation bench below), so the
# exceptional-name candidate fallback is deterministically testable on hosts
# whose real ordinal table has no cross-ASCII fold classes. The production
# ffindexstore build contains no seam.
add_executable(ffindexstore_projection_tests test_projection.cpp
  ${CMAKE_SOURCE_DIR}/src/indexstore/src/Projection.cpp)
target_compile_definitions(ffindexstore_projection_tests PRIVATE FF_INDEXSTORE_TESTING=1)
target_link_libraries(ffindexstore_projection_tests PRIVATE ffindexstore)
add_test(NAME ffindexstore_projection_tests COMMAND ffindexstore_projection_tests)

add_executable(ffindexstore_flat_hash_map_tests test_flat_hash_map.cpp)
target_link_libraries(ffindexstore_flat_hash_map_tests PRIVATE ffindexstore)
add_test(NAME ffindexstore_flat_hash_map_tests COMMAND ffindexstore_flat_hash_map_tests)

# nonblocking-indexed-read-plane 3.x: one independent WAL reader snapshot,
# one writer, deterministic overlap, and fail-closed reader faults.
add_executable(ffindexstore_read_session_tests test_store_read_session.cpp)
target_link_libraries(ffindexstore_read_session_tests PRIVATE ffindexstore)
add_test(NAME ffindexstore_read_session_tests COMMAND ffindexstore_read_session_tests)

# Informational only (tasks.md 9.7) -- not registered with add_test, since
# it reports a measurement rather than passing/failing.
add_executable(ffindexstore_bench_projection_memory bench_projection_memory.cpp)
target_link_libraries(ffindexstore_bench_projection_memory PRIVATE ffindexstore psapi)

# large-index-lifecycle-and-search-scalability 4.2: informational only --
# differential isolation matrix for the repeated-rebuild removal pathology.
# Never registered with CTest (wall-clock measurement, not a gate).
# FF_INDEXSTORE_TESTING compiles the benchmark-only removal-maintenance seam
# into this target's Projection.cpp only; the production ffindexstore build
# contains no seam and behaves exactly as Production mode.
add_executable(ffindexstore_bench_rebuild_isolation
  bench_rebuild_isolation.cpp
  ${CMAKE_SOURCE_DIR}/src/indexstore/src/Projection.cpp)
target_compile_definitions(ffindexstore_bench_rebuild_isolation PRIVATE FF_INDEXSTORE_TESTING=1)
target_link_libraries(ffindexstore_bench_rebuild_isolation PRIVATE ffindexstore ffsetup psapi)
# The report embeds ffsetup::kBuildIdentityAscii; a targeted build must
# refresh the generated identity first or the report could claim a stale tree.
add_dependencies(ffindexstore_bench_rebuild_isolation fastfiles_build_identity)

set_tests_properties(ffindexstore_store_tests ffindexstore_projection_tests ffindexstore_flat_hash_map_tests ffindexstore_read_session_tests
                     PROPERTIES TIMEOUT 120)

# durable-cold-search 4.2: candidate-index resource measurements (bench-only;
# never a CTest gate). Runs the production Store schema with the maintenance
# A/B seam.
add_executable(ffindexstore_bench_candidate_resources bench_candidate_index_resources.cpp)
target_link_libraries(ffindexstore_bench_candidate_resources PRIVATE ffindexstore sqlite3 psapi)
