add_executable(ffcommand_tests
  test_command_system.cpp
  ../../src/ui/src/CommandSystem.cpp
  ../../src/ui/src/QuickActions.cpp
)
target_include_directories(ffcommand_tests PRIVATE ../../src/ui/src)
target_link_libraries(ffcommand_tests PRIVATE shell32)
add_test(NAME ffcommand_tests COMMAND ffcommand_tests)
set_tests_properties(ffcommand_tests PROPERTIES TIMEOUT 120)
