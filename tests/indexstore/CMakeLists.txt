add_executable(ffindexstore_store_tests test_store.cpp)
target_link_libraries(ffindexstore_store_tests PRIVATE ffindexstore)
add_test(NAME ffindexstore_store_tests COMMAND ffindexstore_store_tests)

add_executable(ffindexstore_projection_tests test_projection.cpp)
target_link_libraries(ffindexstore_projection_tests PRIVATE ffindexstore)
add_test(NAME ffindexstore_projection_tests COMMAND ffindexstore_projection_tests)

# Informational only (tasks.md 9.7) -- not registered with add_test, since
# it reports a measurement rather than passing/failing.
add_executable(ffindexstore_bench_projection_memory bench_projection_memory.cpp)
target_link_libraries(ffindexstore_bench_projection_memory PRIVATE ffindexstore psapi)

set_tests_properties(ffindexstore_store_tests ffindexstore_projection_tests PROPERTIES TIMEOUT 120)
