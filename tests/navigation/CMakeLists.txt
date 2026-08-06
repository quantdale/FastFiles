add_executable(ffnavigation_tests
  test_navigation_workspace.cpp
  ../../src/ui/src/NavigationWorkspace.cpp
)
target_include_directories(ffnavigation_tests PRIVATE ../../src/ui/src)
target_link_libraries(ffnavigation_tests PRIVATE shell32 ole32)
add_test(NAME ffnavigation_tests COMMAND ffnavigation_tests)
set_tests_properties(ffnavigation_tests PROPERTIES TIMEOUT 120)
