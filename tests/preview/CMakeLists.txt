add_executable(ffpreview_tests
  test_preview.cpp
  ../../src/ui/src/Preview.cpp
)
target_include_directories(ffpreview_tests PRIVATE ../../src/ui/src)
target_link_libraries(ffpreview_tests PRIVATE windowscodecs ole32)
add_test(NAME ffpreview_tests COMMAND ffpreview_tests)
