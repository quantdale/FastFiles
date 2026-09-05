add_executable(ffmftparser_tests test_mft_parser.cpp)
target_link_libraries(ffmftparser_tests PRIVATE ffmftparser)

add_test(NAME ffmftparser_tests COMMAND ffmftparser_tests)

add_executable(ffusnrecord_tests test_usn_record_parser.cpp)
target_link_libraries(ffusnrecord_tests PRIVATE ffusnparser)
add_test(NAME ffusnrecord_tests COMMAND ffusnrecord_tests)

add_executable(ffprivilege_diagnostics_tests test_privilege_diagnostics.cpp)
target_link_libraries(ffprivilege_diagnostics_tests PRIVATE ffindexsvcprobes)
add_test(NAME ffprivilege_diagnostics_tests COMMAND ffprivilege_diagnostics_tests)

add_executable(ffcommand_surface_tests test_command_surface.cpp)
target_link_libraries(ffcommand_surface_tests PRIVATE ffindexsvcprobes)
add_test(NAME ffcommand_surface_tests COMMAND ffcommand_surface_tests)

add_executable(ffconnection_registry_tests test_connection_registry.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/indexsvc/src/ConnectionRegistry.cpp)
target_link_libraries(ffconnection_registry_tests PRIVATE ffprotocol)
target_include_directories(ffconnection_registry_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src/indexsvc/src)
add_test(NAME ffconnection_registry_tests COMMAND ffconnection_registry_tests)

# index-durability-false-success-closure 2.3: the real RunVolumeScan against
# an unmounted drive letter must emit the ScanFailed terminal to {2,2}+ peers
# and the legacy ScanComplete to older peers (compiled source pattern follows
# ffconnection_registry_tests; the probe/parse deps are static libraries).
add_executable(ffvolumescanner_terminal_tests test_volume_scanner_terminal.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/indexsvc/src/VolumeScanner.cpp)
target_link_libraries(ffvolumescanner_terminal_tests PRIVATE ffmftparser ffindexsvcprobes ffipc ffprotocol)
target_include_directories(ffvolumescanner_terminal_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src/indexsvc/src)
add_test(NAME ffvolumescanner_terminal_tests COMMAND ffvolumescanner_terminal_tests)

# fastfiles-audit-remediation FF-AUDIT-08: keep the service's live write path
# on the shared absolute-deadline framing primitive; source-policy assertions
# prevent a helper-thread timeout fallback from returning.
add_executable(ffindexsvc_write_deadline_policy_tests test_write_deadline_policy.cpp)
add_test(NAME ffindexsvc_write_deadline_policy_tests COMMAND ffindexsvc_write_deadline_policy_tests)

# ntfs-observe certification-probe contract (FC-003/FC-004): synthetic
# fixtures only, never a live volume.
add_executable(ffntfsobserve_tests test_ntfs_observe.cpp)
target_link_libraries(ffntfsobserve_tests PRIVATE ffntfsobserve)
add_test(NAME ffntfsobserve_tests COMMAND ffntfsobserve_tests)

set_tests_properties(ffmftparser_tests ffusnrecord_tests ffprivilege_diagnostics_tests ffcommand_surface_tests
    ffconnection_registry_tests ffvolumescanner_terminal_tests ffindexsvc_write_deadline_policy_tests ffntfsobserve_tests PROPERTIES TIMEOUT 120)
