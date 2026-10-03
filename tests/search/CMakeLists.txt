add_executable(ffsearch_tests test_query.cpp)
target_link_libraries(ffsearch_tests PRIVATE ffsearch)
add_test(NAME ffsearch_tests COMMAND ffsearch_tests)

add_executable(ffsearch_ascii_fold_tests test_ascii_fold_matcher.cpp)
target_link_libraries(ffsearch_ascii_fold_tests PRIVATE ffsearch)
add_test(NAME ffsearch_ascii_fold_tests COMMAND ffsearch_ascii_fold_tests)

add_executable(ffsearch_scan_tests test_search.cpp)
target_link_libraries(ffsearch_scan_tests PRIVATE ffsearch)
add_test(NAME ffsearch_scan_tests COMMAND ffsearch_scan_tests)

add_executable(ffsearch_ondisk_filter_tests test_ondisk_filter.cpp)
target_link_libraries(ffsearch_ondisk_filter_tests PRIVATE ffsearch)
add_test(NAME ffsearch_ondisk_filter_tests COMMAND ffsearch_ondisk_filter_tests)

add_executable(ffsearch_history_tests test_history.cpp)
target_link_libraries(ffsearch_history_tests PRIVATE ffsearch)
add_test(NAME ffsearch_history_tests COMMAND ffsearch_history_tests)

# Informational only: isolated literal matcher evidence, never run by CTest.

# performance-closure-and-multisurface-acceleration 7.3: bounded top-N
# admission property test (gated adds and view-matched adds must reproduce
# the reference result set exactly for every sort field and direction).
add_executable(ffsearch_admission_tests test_search_admission.cpp)
target_link_libraries(ffsearch_admission_tests PRIVATE ffsearch)
add_test(NAME ffsearch_admission_tests COMMAND ffsearch_admission_tests)

add_executable(ffsearch_bench_literal_matcher bench_literal_matcher.cpp)
target_link_libraries(ffsearch_bench_literal_matcher PRIVATE ffsearch)

set_tests_properties(ffsearch_tests ffsearch_ascii_fold_tests ffsearch_scan_tests ffsearch_ondisk_filter_tests ffsearch_history_tests PROPERTIES TIMEOUT 120)
