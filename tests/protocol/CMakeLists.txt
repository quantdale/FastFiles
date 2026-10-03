add_executable(ffprotocol_tests test_protocol.cpp)
target_link_libraries(ffprotocol_tests PRIVATE ffprotocol)

add_test(NAME ffprotocol_tests COMMAND ffprotocol_tests)

add_executable(ffprotocol_diagnostics_tests test_diagnostics.cpp)
target_link_libraries(ffprotocol_diagnostics_tests PRIVATE ffprotocol)
add_test(NAME ffprotocol_diagnostics_tests COMMAND ffprotocol_diagnostics_tests)

add_executable(ffprotocol_diagnostics_concurrency_tests test_diagnostics_concurrency.cpp)
target_link_libraries(ffprotocol_diagnostics_concurrency_tests PRIVATE ffprotocol)
add_test(NAME ffprotocol_diagnostics_concurrency_tests COMMAND ffprotocol_diagnostics_concurrency_tests)

# performance-closure-and-multisurface-acceleration 6.3: pooled-vs-owning
# byte-identity and name-representation equivalence for the pooled export
# model (wire format version 3 unchanged).
add_executable(ffprotocol_snapshot_pooled_equivalence_tests test_snapshot_pooled_equivalence.cpp)
target_link_libraries(ffprotocol_snapshot_pooled_equivalence_tests PRIVATE ffprotocol)
add_test(NAME ffprotocol_snapshot_pooled_equivalence_tests COMMAND ffprotocol_snapshot_pooled_equivalence_tests)

add_executable(ffprotocol_fuzz_tests test_fuzz.cpp)
target_link_libraries(ffprotocol_fuzz_tests PRIVATE ffprotocol)

add_test(NAME ffprotocol_fuzz_tests COMMAND ffprotocol_fuzz_tests)
set_tests_properties(ffprotocol_tests ffprotocol_diagnostics_tests ffprotocol_diagnostics_concurrency_tests
                     ffprotocol_fuzz_tests PROPERTIES TIMEOUT 120)
