add_executable(ffsearch_tests test_query.cpp)
target_link_libraries(ffsearch_tests PRIVATE ffsearch)
add_test(NAME ffsearch_tests COMMAND ffsearch_tests)
