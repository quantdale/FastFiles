add_executable(fffileoperations_tests
  test_file_operation_policy.cpp
  ../../src/ui/src/FileOperationPolicy.cpp
  ../../src/ui/src/ConflictDialog.cpp
  ../../src/ui/src/OleDragDrop.cpp
)
target_include_directories(fffileoperations_tests PRIVATE ../../src/ui/src ../../src/protocol/include)
target_link_libraries(fffileoperations_tests PRIVATE shell32 ole32)
add_test(NAME fffileoperations_tests COMMAND fffileoperations_tests)

add_executable(fffileoperations_e2e_tests
  test_file_operations_e2e.cpp
  ../../src/ui/src/FileOperations.cpp
  ../../src/ui/src/NewFileWriter.cpp
  ../../src/ui/src/FileOperationPolicy.cpp
)
target_include_directories(fffileoperations_e2e_tests PRIVATE ../../src/ui/src ../../src/protocol/include)
target_link_libraries(fffileoperations_e2e_tests PRIVATE shell32 ole32 advapi32)
add_test(NAME fffileoperations_e2e_tests COMMAND fffileoperations_e2e_tests)

add_executable(ffcreation_collision_tests
  test_creation_collisions.cpp
  ../../src/ui/src/FileOperations.cpp
  ../../src/ui/src/NewFileWriter.cpp
  ../../src/ui/src/FileOperationPolicy.cpp
)
target_include_directories(ffcreation_collision_tests PRIVATE ../../src/ui/src ../../src/protocol/include)
target_link_libraries(ffcreation_collision_tests PRIVATE shell32 ole32 advapi32)
add_test(NAME ffcreation_collision_tests COMMAND ffcreation_collision_tests)
set_tests_properties(ffcreation_collision_tests PROPERTIES TIMEOUT 60)

add_executable(ffselection_tests test_selection.cpp)

add_executable(ffrestore_plan_tests test_restore_plan.cpp ../../src/ui/src/FileOperationPolicy.cpp)
target_include_directories(ffrestore_plan_tests PRIVATE ../../src/ui/src ../../src/protocol/include)
add_test(NAME ffrestore_plan_tests COMMAND ffrestore_plan_tests)
set_tests_properties(ffrestore_plan_tests PROPERTIES TIMEOUT 30)
target_include_directories(ffselection_tests PRIVATE ../../src/ui/src)
add_test(NAME ffselection_tests COMMAND ffselection_tests)
set_tests_properties(fffileoperations_tests fffileoperations_e2e_tests ffselection_tests PROPERTIES TIMEOUT 120)

add_test(NAME fffileoperations_execution_consent_tests COMMAND fffileoperations_e2e_tests --post-preflight-consent)
set_tests_properties(fffileoperations_execution_consent_tests PROPERTIES TIMEOUT 60)

add_test(NAME fftransfer_result_protection_tests COMMAND fffileoperations_e2e_tests --replacement-overlap)
set_tests_properties(fftransfer_result_protection_tests PROPERTIES TIMEOUT 60)

add_test(NAME ffshortcut_creation_tests COMMAND fffileoperations_e2e_tests --shortcut-safety)
set_tests_properties(ffshortcut_creation_tests PROPERTIES TIMEOUT 60)

add_executable(ffnew_file_write_tests test_new_file_writer.cpp ../../src/ui/src/NewFileWriter.cpp)
target_include_directories(ffnew_file_write_tests PRIVATE ../../src/ui/src)
target_link_libraries(ffnew_file_write_tests PRIVATE ole32)
add_test(NAME ffnew_file_write_tests COMMAND ffnew_file_write_tests)
set_tests_properties(ffnew_file_write_tests PROPERTIES TIMEOUT 30)
