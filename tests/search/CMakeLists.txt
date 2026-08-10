add_executable(ffsearch_tests test_query.cpp)
target_link_libraries(ffsearch_tests PRIVATE ffsearch)
add_test(NAME ffsearch_tests COMMAND ffsearch_tests)

add_executable(ffsearch_scan_tests test_search.cpp)
target_link_libraries(ffsearch_scan_tests PRIVATE ffsearch)
add_test(NAME ffsearch_scan_tests COMMAND ffsearch_scan_tests)

# Informational only: isolated literal matcher evidence, never run by CTest.
add_executable(ffsearch_bench_literal_matcher bench_literal_matcher.cpp)
target_link_libraries(ffsearch_bench_literal_matcher PRIVATE ffsearch)

set_tests_properties(ffsearch_tests ffsearch_scan_tests PROPERTIES TIMEOUT 120)
