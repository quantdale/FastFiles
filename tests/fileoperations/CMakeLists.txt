add_executable(fffileoperations_tests
  test_file_operation_policy.cpp
  ../../src/ui/src/FileOperationPolicy.cpp
  ../../src/ui/src/ConflictDialog.cpp
  ../../src/ui/src/OleDragDrop.cpp
)
target_include_directories(fffileoperations_tests PRIVATE ../../src/ui/src)
target_link_libraries(fffileoperations_tests PRIVATE shell32 ole32)
add_test(NAME fffileoperations_tests COMMAND fffileoperations_tests)

add_executable(fffileoperations_e2e_tests
  test_file_operations_e2e.cpp
  ../../src/ui/src/FileOperations.cpp
  ../../src/ui/src/FileOperationPolicy.cpp
)
target_include_directories(fffileoperations_e2e_tests PRIVATE ../../src/ui/src)
target_link_libraries(fffileoperations_e2e_tests PRIVATE shell32 ole32 advapi32)
add_test(NAME fffileoperations_e2e_tests COMMAND fffileoperations_e2e_tests)

add_executable(ffselection_tests test_selection.cpp)
target_include_directories(ffselection_tests PRIVATE ../../src/ui/src)
add_test(NAME ffselection_tests COMMAND ffselection_tests)
set_tests_properties(fffileoperations_tests fffileoperations_e2e_tests ffselection_tests PROPERTIES TIMEOUT 120)
